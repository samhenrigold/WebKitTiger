/* os/log.h -- Tiger polyfill. Everything goes to stderr as printf.
 *
 * Controlled by $TIGER_OS_LOG:
 *   unset / "" : error + fault only
 *   "0", "off" : silent
 *   "1", "on"  : + default
 *   "info"     : + info
 *   "debug", "all" : everything
 * Apple format modifiers (%{public}s, %{private}d, %{signpost.description:x}llu)
 * are stripped before the string reaches vfprintf.
 */
#ifndef __OS_LOG_TIGER__
#define __OS_LOG_TIGER__

#include <os/base.h>
#include <os/object.h>

#ifdef __cplusplus
extern "C" {
#endif

OS_OBJECT_DECL_CLASS(os_log);

/* A link-time constant: wtf/Assertions.h uses OS_LOG_DEFAULT in a static initializer. */
OS_EXPORT struct os_log_s _os_log_default;
#define OS_LOG_DEFAULT      ((os_log_t)&_os_log_default)
#define OS_LOG_DISABLED     ((os_log_t)0)

OS_ENUM(os_log_type_t, uint8_t,
    OS_LOG_TYPE_DEFAULT = 0x00,
    OS_LOG_TYPE_INFO    = 0x01,
    OS_LOG_TYPE_DEBUG   = 0x02,
    OS_LOG_TYPE_ERROR   = 0x10,
    OS_LOG_TYPE_FAULT   = 0x11,
);

OS_EXPORT OS_OBJECT_RETURNS_RETAINED os_log_t os_log_create(const char *subsystem, const char *category);
OS_EXPORT bool os_log_type_enabled(os_log_t oslog, os_log_type_t type);

OS_EXPORT OS_NOT_TAIL_CALLED
void _os_log_impl_tiger(os_log_t oslog, os_log_type_t type, const char *format, ...);

OS_EXPORT OS_NOT_TAIL_CALLED
void os_log_with_args(os_log_t oslog, os_log_type_t type, const char *format, va_list args, void *ret_addr);

#define os_log_with_type(log, type, format, ...) \
        _os_log_impl_tiger((log), (type), format, ##__VA_ARGS__)
#define os_log(log, format, ...)       os_log_with_type(log, OS_LOG_TYPE_DEFAULT, format, ##__VA_ARGS__)
#define os_log_info(log, format, ...)  os_log_with_type(log, OS_LOG_TYPE_INFO,    format, ##__VA_ARGS__)
#define os_log_debug(log, format, ...) os_log_with_type(log, OS_LOG_TYPE_DEBUG,   format, ##__VA_ARGS__)
#define os_log_error(log, format, ...) os_log_with_type(log, OS_LOG_TYPE_ERROR,   format, ##__VA_ARGS__)
#define os_log_fault(log, format, ...) os_log_with_type(log, OS_LOG_TYPE_FAULT,   format, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* __OS_LOG_TIGER__ */
