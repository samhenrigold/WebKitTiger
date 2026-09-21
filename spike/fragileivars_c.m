#import <Foundation/Foundation.h>
@interface Feat : NSObject
@property (nonatomic, copy) NSString *key;
@property (nonatomic) BOOL enabled;
@end
@implementation Feat { NSString *_name; int _count; }
- (void)dealloc { [_name release]; [_key release]; [super dealloc]; }
- (NSString *)describe { _count++; self.key = @"k"; return [NSString stringWithFormat:@"%@ %d %d", self.key, _count, (int)self.enabled]; }
@end
int main(void) { NSAutoreleasePool *p = [NSAutoreleasePool new]; Feat *f = [Feat new]; f.enabled = YES; printf("%s\n", [[f describe] UTF8String]); [f release]; [p release]; return 0; }
