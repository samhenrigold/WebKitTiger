/* TIGER: <mach/vm_page_size.h> is a 10.9+ SDK header. Tiger declares the same
   globals in <mach/mach_init.h>; only the vm_kernel_* aliases are new, and on a
   32-bit i386 kernel the kernel page size is the user page size (4 KB). */
#ifndef __TIGER_MACH_VM_PAGE_SIZE_H__
#define __TIGER_MACH_VM_PAGE_SIZE_H__

#include <mach/mach_init.h>
#include <mach/vm_types.h>

#define vm_kernel_page_size  vm_page_size
#define vm_kernel_page_mask  vm_page_mask
#define vm_kernel_page_shift vm_page_shift

#ifndef mach_vm_trunc_page
#define mach_vm_trunc_page(x) ((mach_vm_offset_t)(x) & ~((signed)vm_page_mask))
#endif
#ifndef mach_vm_round_page
#define mach_vm_round_page(x) (((mach_vm_offset_t)(x) + vm_page_mask) & ~((signed)vm_page_mask))
#endif

#endif /* __TIGER_MACH_VM_PAGE_SIZE_H__ */
