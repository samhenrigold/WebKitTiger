#import "protolib.h"
@interface TigerDylibAdopter : NSObject <TigerDylibProto> { NSString *_alpha; int _beta; double _gamma; }
@end
@implementation TigerDylibAdopter
@synthesize alpha = _alpha; @synthesize beta = _beta; @synthesize gamma = _gamma;
- (int)dylibRequired { return 1; }
@end
Protocol *tigerDylibProtocol(void) { return @protocol(TigerDylibProto); }
