#import <Foundation/Foundation.h>

/* Lives in a dylib, so recovering its ext record exercises the per-image symbol lookup. */
@protocol TigerDylibProto
@required
- (int)dylibRequired;
@optional
- (int)dylibOptionalOne;
- (int)dylibOptionalTwo;
+ (int)dylibOptionalClassMethod;
@property (nonatomic, copy) NSString *alpha;
@property (nonatomic, assign) int beta;
@property (nonatomic, assign, readonly) double gamma;
@end

Protocol *tigerDylibProtocol(void);
