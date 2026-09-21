/* TIGER: task_info(TASK_VM_INFO) is 10.9+. The flavor and its struct are
   declared here so the WebKit call sites compile; on a Tiger kernel the call
   returns KERN_INVALID_ARGUMENT and the callers already handle the failure, so
   phys_footprint reads as unavailable rather than wrong. */
#ifndef __TIGER_MACH_TASK_INFO_H__
#define __TIGER_MACH_TASK_INFO_H__

#include_next <mach/task_info.h>
#include <mach/vm_types.h>
#include <stdint.h>

#define TASK_VM_INFO 22

struct task_vm_info {
    mach_vm_size_t  virtual_size;
    integer_t       region_count;
    integer_t       page_size;
    mach_vm_size_t  resident_size;
    mach_vm_size_t  resident_size_peak;
    mach_vm_size_t  device;
    mach_vm_size_t  device_peak;
    mach_vm_size_t  internal;
    mach_vm_size_t  internal_peak;
    mach_vm_size_t  external;
    mach_vm_size_t  external_peak;
    mach_vm_size_t  reusable;
    mach_vm_size_t  reusable_peak;
    mach_vm_size_t  purgeable_volatile_pmap;
    mach_vm_size_t  purgeable_volatile_resident;
    mach_vm_size_t  purgeable_volatile_virtual;
    mach_vm_size_t  compressed;
    mach_vm_size_t  compressed_peak;
    mach_vm_size_t  compressed_lifetime;
    /* added for rev1 */
    mach_vm_size_t  phys_footprint;
    /* added for rev2 */
    mach_vm_address_t min_address;
    mach_vm_address_t max_address;
};
typedef struct task_vm_info  task_vm_info_data_t;
typedef struct task_vm_info *task_vm_info_t;

#define TASK_VM_INFO_COUNT      ((mach_msg_type_number_t)(sizeof(task_vm_info_data_t) / sizeof(natural_t)))
#define TASK_VM_INFO_REV2_COUNT TASK_VM_INFO_COUNT
#define TASK_VM_INFO_REV1_COUNT ((mach_msg_type_number_t)(TASK_VM_INFO_REV2_COUNT - 4))
#define TASK_VM_INFO_REV0_COUNT ((mach_msg_type_number_t)(TASK_VM_INFO_REV1_COUNT - 2))

#endif /* __TIGER_MACH_TASK_INFO_H__ */
