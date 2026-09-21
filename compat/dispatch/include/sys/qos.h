/* sys/qos.h -- Tiger polyfill. QoS is advisory here: nothing in the scheduler
 * honours it. Values match Apple's so any serialized constant still round-trips. */
#ifndef __SYS_QOS_TIGER__
#define __SYS_QOS_TIGER__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int qos_class_t;
enum {
    QOS_CLASS_USER_INTERACTIVE = 0x21,
    QOS_CLASS_USER_INITIATED = 0x19,
    QOS_CLASS_DEFAULT = 0x15,
    QOS_CLASS_UTILITY = 0x11,
    QOS_CLASS_BACKGROUND = 0x09,
    QOS_CLASS_UNSPECIFIED = 0x00,
};

#define QOS_MIN_RELATIVE_PRIORITY (-15)

qos_class_t qos_class_self(void);
qos_class_t qos_class_main(void);

#ifdef __cplusplus
}
#endif

#endif /* __SYS_QOS_TIGER__ */
