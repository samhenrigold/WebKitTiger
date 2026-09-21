/* TIGER64: can a 64-bit process on 10.4.11 find out its own resident size?
 *
 * jsc --footprint printed 0 because WTF's ProcessMemoryFootprint is written
 * against proc_pid_rusage(), which is 10.5+.  The replacement has to come from
 * something xnu-792 implements, and since this project has already been bitten
 * by a 10.4 API answering a 64-bit process with 32-bit values, each candidate is
 * checked against a known amount of freshly touched memory rather than trusted.
 *
 * Build: toolchain/bin/tiger-clang64 -O1 -o rss64 rss64.c
 */
#include <mach/mach.h>
#include <mach/task_info.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

static int failures = 0;
#define CHECK(cond, ...) do { \
    if (cond) printf("PASS  " __VA_ARGS__); else { printf("FAIL  " __VA_ARGS__); failures++; } \
    printf("\n"); \
} while (0)

static uint64_t basicInfo64(uint64_t *virt)
{
    task_basic_info_data_t info;
    mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
    kern_return_t kr = task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&info, &count);
    if (kr != KERN_SUCCESS) { printf("      task_info(TASK_BASIC_INFO) -> %d\n", kr); return 0; }
    if (virt) *virt = (uint64_t)info.virtual_size;
    return (uint64_t)info.resident_size;
}

static uint64_t basicInfo32(void)
{
    struct task_basic_info_32 info;
    mach_msg_type_number_t count = TASK_BASIC_INFO_32_COUNT;
    kern_return_t kr = task_info(mach_task_self(), TASK_BASIC_INFO_32, (task_info_t)&info, &count);
    if (kr != KERN_SUCCESS) { printf("      task_info(TASK_BASIC_INFO_32) -> %d\n", kr); return 0; }
    return (uint64_t)info.resident_size;
}

static uint64_t maxrss(void)
{
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru)) return 0;
    return (uint64_t)ru.ru_maxrss;
}

#define MB (1024ull * 1024ull)

int main(void)
{
    uint64_t virt0 = 0;
    uint64_t rss0 = basicInfo64(&virt0);
    uint64_t rss32_0 = basicInfo32();
    uint64_t max0 = maxrss();
    printf("before: TASK_BASIC_INFO(64) resident %llu (%llu MB), virtual %llu MB; "
           "TASK_BASIC_INFO_32 resident %llu MB; getrusage ru_maxrss %llu\n",
           (unsigned long long)rss0, (unsigned long long)(rss0 / MB), (unsigned long long)(virt0 / MB),
           (unsigned long long)(rss32_0 / MB), (unsigned long long)max0);

    CHECK(rss0 > 0, "TASK_BASIC_INFO reports a non-zero resident size");

    const size_t bytes = 200 * MB;
    unsigned char *p = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED) { printf("FAIL  mmap 200 MB\n"); return 1; }
    for (size_t i = 0; i < bytes; i += 4096) p[i] = (unsigned char)i;

    uint64_t virt1 = 0;
    uint64_t rss1 = basicInfo64(&virt1);
    uint64_t rss32_1 = basicInfo32();
    uint64_t max1 = maxrss();
    printf("after:  TASK_BASIC_INFO(64) resident %llu (%llu MB), virtual %llu MB; "
           "TASK_BASIC_INFO_32 resident %llu MB; getrusage ru_maxrss %llu\n",
           (unsigned long long)rss1, (unsigned long long)(rss1 / MB), (unsigned long long)(virt1 / MB),
           (unsigned long long)(rss32_1 / MB), (unsigned long long)max1);

    /* 200 MB touched: the reported growth has to be within 10% of that, or the
     * counter is not measuring what its name says. */
    uint64_t grew = rss1 > rss0 ? rss1 - rss0 : 0;
    CHECK(grew > (uint64_t)(180 * MB) && grew < (uint64_t)(230 * MB),
          "resident size grew by %llu MB after touching 200 MB", (unsigned long long)(grew / MB));
    CHECK(virt1 > virt0 && (virt1 - virt0) >= (uint64_t)(200 * MB),
          "virtual size grew by %llu MB", (unsigned long long)((virt1 - virt0) / MB));
    CHECK(rss32_1 == 0 || rss32_1 == rss1,
          "TASK_BASIC_INFO_32 agrees or is unimplemented (%llu MB)", (unsigned long long)(rss32_1 / MB));
    printf("      getrusage ru_maxrss went %llu -> %llu (%s)\n",
           (unsigned long long)max0, (unsigned long long)max1,
           max1 > max0 ? "tracks" : "NOT populated on 10.4");

    munmap(p, bytes);
    printf("%s (%d failure%s)\n", failures ? "FAILURES" : "ALL PASS", failures, failures == 1 ? "" : "s");
    return failures != 0;
}
