/* Not linked into anything: `make check` compiles this with ARC to prove our headers stay ARC-clean.
 * All of WTF is built with ARC, so a missing ownership qualifier in objc/runtime.h breaks translation
 * units that merely include it transitively and never touch the ObjC2 API. */
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
int tigerARCHeaderCheck(void);
int tigerARCHeaderCheck(void) { return 1; }
