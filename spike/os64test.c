/* os64test -- the x86_64 half of the os_* polyfill, on Tiger's 64-bit libSystem.
 * Tiger ships no 64-bit CoreFoundation, so a 64-bit process gets the os_* surface
 * and no dispatch queues. os.c's 64-bit home is compat's archive, not
 * libtigerdispatch: `make -C compat ARCH=x86_64 install` compiles it in. Build:
 *   toolchain/bin/tiger-clang64 -O1 -g spike/os64test.c -o build/os64test -ltigercompat
 * Run on the box; prints PASS lines and exits 0.
 */
#include <os/log.h>
#include <os/lock.h>
#include <os/signpost.h>
#include <sys/qos.h>
#include <pthread.h>
#include <stdio.h>
static os_unfair_lock g_l = OS_UNFAIR_LOCK_INIT;
static long g_n;
static void *worker(void *u) { (void)u; for (int i=0;i<50000;i++){ os_unfair_lock_lock(&g_l); g_n++; os_unfair_lock_assert_owner(&g_l); os_unfair_lock_unlock(&g_l);} return 0; }
int main(void) {
    printf("64-bit os polyfill test (sizeof(long)=%zu)\n", sizeof(long));
    pthread_t t[4];
    for (int i=0;i<4;i++) pthread_create(&t[i],0,worker,0);
    for (int i=0;i<4;i++) pthread_join(t[i],0);
    printf("%s: os_unfair_lock across 4 threads (%ld)\n", g_n==200000?"PASS":"FAIL", g_n);
    os_log_t log = os_log_create("com.tiger.os64", "Test");
    os_log_error(log, "error line %{public}s %d", "visible", 7);
    os_log(log, "default line (hidden unless TIGER_OS_LOG=1)");
    printf("%s: os_log_type_enabled(ERROR)\n", os_log_type_enabled(log, OS_LOG_TYPE_ERROR)?"PASS":"FAIL");
    os_signpost_id_t sid = os_signpost_id_make_with_pointer(OS_LOG_DEFAULT, &g_n);
    os_signpost_interval_begin(OS_LOG_DEFAULT, sid, "Span");
    os_signpost_interval_end(OS_LOG_DEFAULT, sid, "Span");
    printf("%s: os_signpost no-ops\n", sid?"PASS":"FAIL");
    os_release(log);
    return (g_n==200000 && sid) ? 0 : 1;
}
