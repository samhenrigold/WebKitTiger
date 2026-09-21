/* TIGER: VM_PURGABLE_DENY (deny further purgable allocations) is 10.5+. */
#ifndef __TIGER_MACH_VM_PURGABLE_H__
#define __TIGER_MACH_VM_PURGABLE_H__
#include_next <mach/vm_purgable.h>
#ifndef VM_PURGABLE_DENY
#define VM_PURGABLE_DENY ((vm_purgable_t) 3)
#endif
#endif
