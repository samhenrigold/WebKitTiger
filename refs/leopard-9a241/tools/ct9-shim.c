/* Shim for 9A241 CoreText on Tiger 10.4.11.
 *
 * Three symbols Tiger lacks, plus one deliberate override.
 *
 * The override: CoreText decides whether a CFTypeRef is a native CTFont with an
 * inlined CoreFoundation bridging test, `obj->isa == __CFRuntimeObjCClassTable[typeID]`.
 * CF 401/476 give an unbridged object a per-type isa, so that holds. Tiger's CF 368
 * instead gives every unbridged object one shared sentinel, the table's own base
 * address, so the test fails, CoreText concludes the object is a bridged ObjC font and
 * messages it, and objc_msgSend jumps through a null class.
 *
 * Pointing CoreText at a private table whose every entry is Tiger's sentinel makes the
 * test agree for all unbridged types, so CoreText takes its native path.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <objc/objc.h>
#include <dlfcn.h>
#include <signal.h>
#include <stdio.h>

int __CFRuntimeClassTableSize = 1024;   /* must exceed CTFontGetTypeID() */

static void *g_table[1024];
void **__CFRuntimeObjCClassTable = g_table;

__attribute__((constructor)) static void init(void) {
  void *cf = dlopen("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation", RTLD_LAZY);
  void **real = cf ? (void **)dlsym(cf, "__CFRuntimeObjCClassTable") : 0;
  void *sentinel = real ? *real : 0;      /* Tiger's unbridged isa == the table base */
  for (int i = 0; i < 1024; i++) g_table[i] = sentinel;
  if (getenv("CT9_VERBOSE")) fprintf(stderr, "[shim] CF sentinel isa = %p\n", sentinel);
}

/* Bootstrap hook: bind one CF type id to the isa its instances actually carry.
   Needed for types whose instances do not use CF's unbridged sentinel. */
void ct9_bind(unsigned long typeID, void *isa) {
  if (typeID < 1024) g_table[typeID] = isa;
}

/* Leopard libSystem's UNIX 2003 alias; Tiger has only the plain name. */
int kill$UNIX2003(pid_t p, int sig) { return kill(p, sig); }
/* ObjC 2 accessor absent from objc4-227. */
Class object_getClass(id obj) { return obj ? obj->isa : (Class)0; }
