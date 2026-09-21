#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#include <vector>
struct Cxx { int v; Cxx():v(41){} };
@interface Foo : NSObject { NSString *_name; Cxx _c; }
@property (nonatomic, retain) NSString *name;
- (int)answer;
@end
@implementation Foo
@synthesize name = _name;
- (int)answer { return _c.v + 1; }
- (void)dealloc { [_name release]; [super dealloc]; }
@end
int main(){ NSAutoreleasePool *p=[NSAutoreleasePool new]; Foo *f=[[Foo new] autorelease]; f.name=@"tiger"; NSLog(@"ObjC++ on %@: answer=%d osver=%@ appkit=%g", f.name, [f answer], [[NSProcessInfo processInfo] operatingSystemVersionString], NSAppKitVersionNumber); [p release]; return 0; }
