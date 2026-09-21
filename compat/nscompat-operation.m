/* TIGER: NSOperation / NSBlockOperation / NSOperationQueue (10.5) over
 * libtigerdispatch.
 *
 * Scope is deliberately the WebCore async ResourceHandle delegate and nothing
 * else: serial or concurrent block execution, a drain barrier, suspend/resume.
 * No inter-operation dependencies, no KVO on isFinished, no queue priorities,
 * and cancelling an operation that has already started does nothing.
 *
 * ponytail: maxConcurrentOperationCount is honoured only as "1 means serial,
 * anything else means the global concurrent queue". Per-queue concurrency
 * limits would need a counting semaphore; add one if a caller ever asks for a
 * specific width.
 */

#import <Foundation/Foundation.h>
#import <TigerCompat/NSCompat.h>

#include <dispatch/dispatch.h>
#include <unistd.h>

@implementation NSOperation

- (void)start
{
    if (_cancelled) {
        __sync_synchronize();   /* -waitUntilFinished polls _finished from another thread */
        _finished = YES;
        return;
    }
    _executing = YES;
    [self main];
    _executing = NO;
    __sync_synchronize();
    _finished = YES;
}

- (void)main { }

- (void)cancel { _cancelled = YES; }
- (BOOL)isCancelled { return _cancelled; }
- (BOOL)isFinished { return _finished; }
- (BOOL)isExecuting { return _executing; }

/* WebCoreNSURLSession.mm adds an operation to the delegate queue and then blocks on it.
 * ponytail: poll the flag rather than add a condition variable and an ivar to the fragile-ABI
 * class layout. Ceiling: up to 1 ms of latency past completion and a wakeup every 1 ms while
 * waiting. Upgrade: an NSConditionLock ivar signalled from -start, if anything ever waits hot. */
- (void)waitUntilFinished
{
    while (!*(volatile BOOL *)&_finished)
        usleep(1000);
    __sync_synchronize();
}

@end

#if TIGER_NS_BLOCKS

@implementation NSBlockOperation

+ (id)blockOperationWithBlock:(void (^)(void))block
{
    NSBlockOperation *operation = [[[self alloc] init] autorelease];
    [operation addExecutionBlock:block];
    return operation;
}

- (id)init
{
    if ((self = [super init]))
        _blocks = [[NSMutableArray alloc] init];
    return self;
}

- (void)dealloc
{
    [_blocks release];
    [super dealloc];
}

- (void)addExecutionBlock:(void (^)(void))block
{
    if (!block)
        return;
    /* Blocks are real ObjC objects here (compat/blockclasses.m). */
    [_blocks addObject:[[block copy] autorelease]];
}

- (void)main
{
    NSEnumerator *e = [_blocks objectEnumerator];
    id blockObject;
    while ((blockObject = [e nextObject]) != nil)
        ((void (^)(void))blockObject)();
}

@end

#endif /* TIGER_NS_BLOCKS */

static NSOperationQueue *tigerMainOperationQueue;

@implementation NSOperationQueue

+ (NSOperationQueue *)mainQueue
{
    @synchronized ([NSOperationQueue class]) {
        if (!tigerMainOperationQueue) {
            tigerMainOperationQueue = [[NSOperationQueue alloc] init];
            tigerMainOperationQueue->_queue = dispatch_get_main_queue();
            dispatch_retain((dispatch_queue_t)tigerMainOperationQueue->_queue);
            tigerMainOperationQueue->_maxConcurrent = 1;
            tigerMainOperationQueue->_name = [@"NSOperationQueue Main Queue" copy];
        }
    }
    return tigerMainOperationQueue;
}

/* There is no queue-to-operation-queue mapping on Tiger. Callers use this to
 * pick a queue to call back on, and the main queue is the safe answer. */
+ (NSOperationQueue *)currentQueue
{
    if ([NSThread isMainThread])
        return [self mainQueue];
    return nil;
}

- (id)init
{
    if (!(self = [super init]))
        return nil;
    _maxConcurrent = -1;    /* NSOperationQueueDefaultMaxConcurrentOperationCount */
    _queue = dispatch_queue_create("org.webkit.tiger.NSOperationQueue", NULL);
    _group = dispatch_group_create();
    return self;
}

- (void)dealloc
{
    /* Releasing a suspended queue is a crash in real GCD; balance the suspend first. */
    if (_suspended && self != tigerMainOperationQueue && _maxConcurrent == 1 && _queue)
        dispatch_resume((dispatch_queue_t)_queue);
    if (_group)
        dispatch_release((dispatch_group_t)_group);
    if (_queue)
        dispatch_release((dispatch_queue_t)_queue);
    [_name release];
    [super dealloc];
}

/* Serial unless the caller explicitly asked for more than one at a time. */
- (dispatch_queue_t)tigerTargetQueue
{
    if (_maxConcurrent == 1 || self == tigerMainOperationQueue)
        return (dispatch_queue_t)_queue;
    /* dispatch_get_global_queue returns the distinct dispatch_queue_global_t
     * type in libtigerdispatch's headers; it is a dispatch_queue_t. */
    return (dispatch_queue_t)dispatch_get_global_queue(0, 0);
}

- (void)addOperation:(NSOperation *)op
{
    if (!op)
        return;
    [op retain];
    dispatch_group_async((dispatch_group_t)_group, [self tigerTargetQueue], ^{
        /* A dispatch worker thread has no pool of its own, and an operation is
         * free to autorelease. */
        NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
        [op start];
        [op release];
        [pool release];
    });
}

#if TIGER_NS_BLOCKS
- (void)addOperationWithBlock:(void (^)(void))block
{
    if (!block)
        return;
    void (^wrapped)(void) = ^{
        NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
        block();
        [pool release];
    };
    dispatch_group_async((dispatch_group_t)_group, [self tigerTargetQueue], wrapped);
}
#endif

- (void)setMaxConcurrentOperationCount:(NSInteger)count { _maxConcurrent = count; }
- (NSInteger)maxConcurrentOperationCount { return _maxConcurrent; }

- (void)setName:(NSString *)name
{
    if (name == _name)
        return;
    [_name release];
    _name = [name copy];
}
- (NSString *)name { return _name; }

/* Suspend really holds work back, via dispatch_suspend on the queue, rather than dropping it:
 * the previous version skipped -start for anything dequeued while suspended, so the operation
 * never ran at all, never became isFinished, and a waiter would block forever.
 *
 * ponytail: this only bites on the serial path, where -tigerTargetQueue returns _queue. A queue
 * with maxConcurrentOperationCount other than 1 runs on the shared global queue and +mainQueue
 * runs on the main queue; suspending either of those is not ours to do, so suspend is a no-op
 * there. Upgrade: a per-queue holding array drained on resume, if a concurrent queue ever needs
 * real suspend. */
- (void)setSuspended:(BOOL)suspended
{
    if (suspended == _suspended)
        return;
    _suspended = suspended;
    if (self == tigerMainOperationQueue || _maxConcurrent != 1)
        return;
    if (suspended)
        dispatch_suspend((dispatch_queue_t)_queue);
    else
        dispatch_resume((dispatch_queue_t)_queue);
}
- (BOOL)isSuspended { return _suspended; }

- (void)cancelAllOperations { }

- (void)waitUntilAllOperationsAreFinished
{
    dispatch_group_wait((dispatch_group_t)_group, DISPATCH_TIME_FOREVER);
}

- (NSUInteger)operationCount { return 0; }

@end
