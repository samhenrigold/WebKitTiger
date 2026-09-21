/* TIGER: Tiger's <mach/vm_statistics.h> stops at VM_MEMORY_DYLD_MALLOC (61).
   This shadows it, includes the real one, and adds the VM_MEMORY_* allocation
   tags introduced afterwards, with their XNU values. The tags only label VM
   regions for diagnostics, so an unknown-to-the-kernel tag is harmless: Tiger's
   vm_allocate stores whatever flag byte it is given. */
#ifndef __TIGER_MACH_VM_STATISTICS_H__
#define __TIGER_MACH_VM_STATISTICS_H__

#include_next <mach/vm_statistics.h>

#define VM_MEMORY_MALLOC_LARGE_REUSABLE 8
#define VM_MEMORY_MALLOC_LARGE_REUSED   9
#define VM_MEMORY_MALLOC_NANO          11
#define VM_MEMORY_MALLOC_MEDIUM        12
#define VM_MEMORY_OBJC_DISPATCHERS     34
#define VM_MEMORY_UNSHARED_PMAP        35
#define VM_MEMORY_CORESERVICES         43   /* renamed from VM_MEMORY_CARBON */
#define VM_MEMORY_COREDATA             45
#define VM_MEMORY_COREDATA_OBJECTIDS   46
#define VM_MEMORY_LAYERKIT             51
#define VM_MEMORY_CGIMAGE              52
#define VM_MEMORY_TCMALLOC             53
#define VM_MEMORY_COREGRAPHICS_DATA    54
#define VM_MEMORY_COREGRAPHICS_SHARED  55
#define VM_MEMORY_COREGRAPHICS_FRAMEBUFFERS 56
#define VM_MEMORY_COREGRAPHICS_BACKINGSTORES 57
#define VM_MEMORY_COREGRAPHICS_XALLOC  58
#define VM_MEMORY_SQLITE               62
#define VM_MEMORY_JAVASCRIPT_CORE      63
#define VM_MEMORY_JAVASCRIPT_JIT_EXECUTABLE_ALLOCATOR 64
#define VM_MEMORY_JAVASCRIPT_JIT_REGISTER_FILE 65
#define VM_MEMORY_GLSL                 66
#define VM_MEMORY_OPENCL               67
#define VM_MEMORY_COREIMAGE            68
#define VM_MEMORY_WEBCORE_PURGEABLE_BUFFERS 69
#define VM_MEMORY_IMAGEIO              70
#define VM_MEMORY_COREPROFILE          71
#define VM_MEMORY_ASSETSD              72
#define VM_MEMORY_OS_ALLOC_ONCE        73
#define VM_MEMORY_LIBDISPATCH          74
#define VM_MEMORY_ACCELERATE           75
#define VM_MEMORY_COREUI               76
#define VM_MEMORY_COREUIFILE           77
#define VM_MEMORY_GENEALOGY            78
#define VM_MEMORY_RAWCAMERA            79
#define VM_MEMORY_CORPSEINFO           80
#define VM_MEMORY_ASL                  81
#define VM_MEMORY_SWIFT_RUNTIME        82
#define VM_MEMORY_SWIFT_METADATA       83
#define VM_MEMORY_DHMM                 84
#define VM_MEMORY_SCENEKIT             86
#define VM_MEMORY_SKYWALK              87
#define VM_MEMORY_IOSURFACE            88
#define VM_MEMORY_LIBNETWORK           89
#define VM_MEMORY_AUDIO                90
#define VM_MEMORY_VIDEOBITSTREAM       91
#define VM_MEMORY_IOACCELERATOR       100

/* VM_FLAGS_PERMANENT marks a mapping that cannot be unmapped or reprotected.
   Tiger's vm_map ignores unknown flag bits, so a region tagged with it is an
   ordinary region: WTFConfig's write-protected config page is still mapped and
   still read-only, it just is not permanent. */
#ifndef VM_FLAGS_PERMANENT
#define VM_FLAGS_PERMANENT 0x0010
#endif

#endif /* __TIGER_MACH_VM_STATISTICS_H__ */
