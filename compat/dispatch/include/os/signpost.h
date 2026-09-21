/* os/signpost.h -- Tiger polyfill. All no-ops.
 * ponytail: signposts are Instruments telemetry; there is no Instruments on 10.4.
 * Arguments are still parsed (sizeof-discarded) so format/type errors are caught.
 * Upgrade path: route into kdebug_trace if anyone ever profiles this box. */
#ifndef __OS_SIGNPOST_TIGER__
#define __OS_SIGNPOST_TIGER__

#include <os/base.h>
#include <os/log.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t os_signpost_id_t;
#define OS_SIGNPOST_ID_NULL      ((os_signpost_id_t)0)
#define OS_SIGNPOST_ID_INVALID   ((os_signpost_id_t)~0ull)
#define OS_SIGNPOST_ID_EXCLUSIVE ((os_signpost_id_t)0xEEEEB0B5B2B2EEEEull)

OS_EXPORT os_signpost_id_t os_signpost_id_generate(os_log_t log);
OS_EXPORT os_signpost_id_t os_signpost_id_make_with_pointer(os_log_t log, const void *ptr);
OS_EXPORT bool os_signpost_enabled(os_log_t log);

/* Emit nothing. We still consume `log` and `sid` so the locals WTF's
 * SystemTracing.h declares for them don't trip -Wunused-variable. */
#define _os_signpost_noop(log, sid) do { (void)(log); (void)(sid); } while (0)

#define os_signpost_event_emit(log, sid, ...)     _os_signpost_noop(log, sid)
#define os_signpost_interval_begin(log, sid, ...) _os_signpost_noop(log, sid)
#define os_signpost_interval_end(log, sid, ...)   _os_signpost_noop(log, sid)
#define os_signpost_animation_interval_begin(log, sid, ...) _os_signpost_noop(log, sid)

#ifdef __cplusplus
}
#endif

#endif /* __OS_SIGNPOST_TIGER__ */
