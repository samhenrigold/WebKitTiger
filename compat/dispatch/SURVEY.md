# libdispatch / os_* API surface used by WebKit (Tiger port)

Scanned: `WebKit/Source/{WTF,JavaScriptCore,bmalloc,WebCore,WebKitLegacy/mac}`, all
`*.h *.cpp *.mm *.m *.c`. **`Source/WebKit` (WebKit2) was not scanned — we do not build it.**
`Source/WebKit` is by far the heaviest GCD client in the tree (xpc, dispatch_io,
dispatch_mach, dispatch_source MACH_RECV/PROC, os_state, os_activity); none of that is in scope.

Counts below are raw token occurrences (declaration + use sites), deduped to real API
identifiers. `dispatchEvent`, `DISPATCH_OPCODE`, `DISPATCH_ON_KIND` etc. are WebKit's own
identifiers and are excluded.

## Headers included

| header | include sites | action |
|---|---|---|
| `<dispatch/dispatch.h>` | 19 | implement |
| `<os/log.h>` | 7 | implement |
| `<os/object.h>` | 5 | implement |
| `<os/lock.h>` | 3 | implement |
| `<os/signpost.h>` | 1 (`wtf/SystemTracing.h`) | implement (no-ops) |
| `<dispatch/group.h>` | 1 (`SourceBufferPrivateAVFObjC.h`) | forwarder to dispatch.h |
| `<dispatch/private.h>`, `<os/log_private.h>`, `<os/lock_private.h>`, `<os/transaction_private.h>`, `<os/variant_private.h>`, `<os/reason_private.h>`, `<os/feature_private.h>`, `<os/script_config_private.h>` | 1 each | **not provided** — all behind `USE(APPLE_INTERNAL_SDK)` or `__has_include`, so WebKit's own fallback definitions kick in |

`<sys/qos.h>` is also missing on Tiger and is needed by `dispatch_queue_attr_make_with_qos_class`
and by `WTF/wtf/Threading.h` (`qos_class_t`, `QOS_CLASS_*`), so it ships here too.

## dispatch API used

### Queues
| symbol | n | notes |
|---|---|---|
| `dispatch_queue_t` | 68 | |
| `dispatch_queue_create` | 12 | `WorkQueueCocoa.mm:92`, `MemoryPressureHandlerCocoa.mm:49`, `PeriodicSharedTimer.mm:39` |
| `dispatch_queue_attr_t` | 3 | |
| `DISPATCH_QUEUE_SERIAL_WITH_AUTORELEASE_POOL` | 1 | `DispatchExtras.h` |
| `DISPATCH_QUEUE_CONCURRENT_WITH_AUTORELEASE_POOL` | 1 | `DispatchExtras.h` |
| `dispatch_queue_attr_make_with_qos_class` | 1 | `WorkQueueCocoa.mm:93` |
| `dispatch_queue_create_with_target` | 1 | `AVVideoCaptureSource.mm:129` |
| `dispatch_get_main_queue` / `dispatch_queue_main_t` | 1 / 1 | `DispatchExtras.h` |
| `dispatch_get_global_queue` | 1 | `DispatchExtras.h` |
| `DISPATCH_QUEUE_PRIORITY_DEFAULT` | 16 | |
| `DISPATCH_QUEUE_PRIORITY_HIGH` | 2 | |
| `DISPATCH_TARGET_QUEUE_DEFAULT` | 1 | `Signals.cpp:167` |
| `dispatch_queue_set_specific` / `dispatch_get_specific` | 1 / 1 | `WorkQueueCocoa.mm:97`, `Threading.cpp:146` |
| `dispatch_set_context` | 2 | `WorkQueueCocoa.mm:96`, `MemoryPressureHandlerCocoa.mm:211` |
| `dispatch_queue_global_t`, `dispatch_object_t` | 1 each | `DispatchOSObject.h` type list |

`dispatch_set_target_queue` is **not used** in our dirs (only `dispatch_queue_create_with_target`).
`dispatch_main` is **not used**.

### Submission
| symbol | n |
|---|---|
| `dispatch_async` | 28 |
| `dispatch_async_f` | 1 (`WorkQueueCocoa.mm:52`) |
| `dispatch_sync` | 5 |
| `dispatch_sync_f` | 1 (`WorkQueueCocoa.mm:76`) |
| `dispatch_barrier_sync` | 10 (9 in `WebPreferences.mm`) |
| `dispatch_barrier_async` | 1 (test) |
| `dispatch_after` | 8 |
| `dispatch_after_f` | 1 (`WorkQueueCocoa.mm:71`) |
| `dispatch_apply` | 2 (`WorkQueueCocoa.mm:111`, `ParallelJobsLibdispatch.h:61`) |
| `dispatch_once` / `dispatch_once_t` | 24 / 26 |
| `dispatch_once_f` | 2 (`pas_mte_config.c`, win) |
| `dispatch_block_t` | 3 |
| `dispatch_block_create_with_qos_class` + `DISPATCH_BLOCK_ENFORCE_QOS_CLASS` | 1 + 1 (`WorkQueueCocoa.mm:63`) |

### Time, groups, semaphores
`dispatch_time` (12), `DISPATCH_TIME_NOW` (13), `DISPATCH_TIME_FOREVER` (2), `dispatch_time_t` (1);
`dispatch_group_create/enter/leave/notify/wait/async` (4/4/4/2/1/1 — `MediaPlayerPrivateAVFoundationObjC.mm`,
`WebItemProviderPasteboard.mm`, tests); `dispatch_semaphore_create/wait/signal` (2/2/2 — JSC tests only).

### Sources
| symbol | n | sites |
|---|---|---|
| `dispatch_source_t` | 19 | |
| `dispatch_source_create` | 7 | |
| `dispatch_source_set_event_handler` | 7 | |
| `dispatch_source_set_timer` | 2 | `MemoryPressureHandlerCocoa.mm:214`, `PeriodicSharedTimer.mm:43` |
| `dispatch_source_cancel` | 8 | |
| `dispatch_source_set_cancel_handler` | 1 | `FileMonitorCocoa.mm:74` |
| `dispatch_source_testcancel` | 1 | `FileMonitorCocoa.mm:60` |
| `dispatch_source_get_data` | 2 | `MemoryPressureHandlerCocoa.mm:99` |
| `dispatch_resume` / `dispatch_suspend` / `dispatch_activate` | 6 / 1 / 2 | |
| `DISPATCH_SOURCE_TYPE_TIMER` | 4 | real |
| `DISPATCH_SOURCE_TYPE_MEMORYPRESSURE` + `DISPATCH_MEMORYPRESSURE_*` | 1 + 12 | inert |
| `DISPATCH_SOURCE_TYPE_VNODE` + `DISPATCH_VNODE_*` | 1 + 8 | `FileMonitorCocoa.mm` — inert |
| `DISPATCH_SOURCE_TYPE_MACH_RECV` | 1 | `Signals.cpp:167` — inert |

### Data / misc
`dispatch_data_t` (7), `dispatch_data_create` (1, `VectorCocoa.h:121`), `dispatch_data_apply` (1,
`SpanCocoa.mm:41`), `dispatch_io_t` (1, type list only — no dispatch_io calls).
`dispatch_retain`/`dispatch_release` (2/2, `DispatchOSObject.h`), `dispatch_qos_class_t` (2).

## os_* API used

### os/object.h
`os_retain` / `os_release` (`OSObjectPtr.h:49,57`), `OS_OBJECT_DECL`, `OS_OBJECT_DECL_CLASS`,
`OS_OBJECT_CLASS`, `OS_OBJECT_USE_OBJC`, `OS_OBJECT_USE_OBJC_RETAIN_RELEASE`, `OS_OBJECT_BRIDGE`,
`OS_OBJECT_RETURNS_RETAINED` (15). `WTF/wtf/darwin/DispatchOSObject.h` forward-declares
`struct dispatch_{data,group,io,object,queue,queue_global,semaphore,source}_s`, so the
non-ObjC (`OS_OBJECT_USE_OBJC 0`) shape of the typedefs is mandatory.

### os/log.h
`os_log_t` (22), `os_log` (11), `os_log_error` (3), `os_log_debug` (2), `os_log_info` (1),
`os_log_fault` (1), `os_log_with_type` (1), `os_log_create` (6), `os_log_type_t` (9),
`OS_LOG_TYPE_{DEFAULT,ERROR,FAULT,INFO,DEBUG}`, `OS_LOG_DEFAULT` (2).
SPI `os_log_with_args` is *called* by `WTF/wtf/Assertions.cpp:185` — must be provided.
`os_log_set_hook`, `os_log_copy_message_string`, `os_log_message_t`, `os_trace_*` appear only as
declarations inside `WTF/wtf/spi/cocoa/OSLogSPI.h` (nothing in our dirs calls them) — skipped, but
`<os/base.h>` must define `OS_ENUM`, `OS_EXPORT`, `OS_NOTHROW`, `OS_NOT_TAIL_CALLED`, `OS_NONNULL5`
for that header to parse.

Clients: `wtf/Assertions.{h,cpp}`, `wtf/Logger.{h,cpp}`, `wtf/darwin/OSLogPrintStream.{h,mm}`,
`bmalloc/BAssert.h`, `bmalloc/TZoneLog.{h,cpp}`, `WebCore/WebCorePrefix.h`.

### os/lock.h
`os_unfair_lock` (3), `os_unfair_lock_lock` (3), `os_unfair_lock_unlock` (3),
`os_unfair_lock_assert_owner` (3), `os_unfair_lock_trylock` (1), `OS_UNFAIR_LOCK_INIT`.
Clients: `WTF/wtf/Lock.h:170-186` (`WTF::UnfairLock`, gated on `__has_include(<os/lock.h>)` so
providing the header *enables* `ENABLE(UNFAIR_LOCK)`), `bmalloc/libpas/src/libpas/pas_lock.h:67`.
The `*_inline` / `*_with_options` / `*_with_flags` variants are only reached through
`<os/lock_private.h>`, which we deliberately do not provide, so libpas falls back to the public four.

### os/signpost.h
`os_signpost_interval_begin` (6), `os_signpost_interval_end` (6), `os_signpost_event_emit` (5),
`os_signpost_id_make_with_pointer` (2), `os_signpost_id_t` (1), `OS_SIGNPOST_ID_EXCLUSIVE` (3),
`os_signpost_enabled`. Single client: `WTF/wtf/SystemTracing.h` under `HAVE(OS_SIGNPOST)`.
All implemented as no-ops (arguments still parsed for type-checking).

### Not used at all in our dirs
`os_activity_*`, `os_state_*`, `os_workgroup_*`, `os_variant_*`, `dispatch_io_*`, `dispatch_mach_*`,
`dispatch_main`, `dispatch_set_target_queue`, `dispatch_group_async_f`, `dispatch_read`/`write`.
`os_transaction_*` appears only in `WTF/wtf/spi/darwin/XPCSPI.h` (self-declared, WebKit2-only path).
These are **skipped**.

## Notable clients (per lead's request)
- `WTF/wtf/cocoa/WorkQueueCocoa.mm` — the densest user: `dispatch_queue_create` with the
  autorelease-pool attrs + QoS, `dispatch_{async,sync,after}_f`, `dispatch_block_create_with_qos_class`,
  `dispatch_set_context`, `dispatch_queue_set_specific`, `dispatch_apply`, `dispatch_get_main_queue`.
- `WTF/wtf/RunLoop.h` / `WTF/wtf/cf/RunLoopCF.cpp` — **no dispatch at all**, pure CFRunLoop. Good.
- `WTF/wtf/cocoa/MainThreadCocoa.mm` — **no dispatch at all** (NSNotification + CFRunLoop).
- `WTF/wtf/cocoa/MemoryPressureHandlerCocoa.mm` (42 hits) — MEMORYPRESSURE source + a TIMER source
  + `dispatch_after`. The memory-pressure source is inert here; the timer path must work.
- `WebCore/platform/graphics/cocoa/PeriodicSharedTimer.mm` — a real repeating
  `DISPATCH_SOURCE_TYPE_TIMER` with suspend/resume. This is WebCore's shared timer; it must work.
- `WebCore/platform/cocoa/FileMonitorCocoa.mm` — VNODE source; inert (file monitoring silently
  never fires). Acceptable: it only drives cookie-file change notifications.
- `WTF/wtf/threads/Signals.cpp` — MACH_RECV source for the mach exception port; inert.
- `WebKitLegacy/mac/WebView/WebPreferences.mm` — 9 × `dispatch_barrier_sync` on a serial queue.
