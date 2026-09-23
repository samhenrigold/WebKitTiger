/* TIGER64: how OSAllocator::decommit gives pages back on 10.4 x86_64, and that the
 * mapping's protection survives it.
 *
 * MADV_FREE is EINVAL here and MADV_DONTNEED / msync(MS_KILLPAGES) succeed without
 * releasing anything (NOTES, "libpas on TIGER64"); only a MAP_FIXED zero-fill remap frees
 * the pages. OSAllocator::decommit is called on RW heap memory and on freed RWX JIT pages
 * alike and is not told which, so the remap has to read each region's protection back
 * with vm_region_64 and reuse it. This checks, on the box:
 *   1. vm_region_64 works from a 64-bit task on 10.4 and reports RW vs RWX correctly;
 *   2. the remap drops RSS for both (256 MB RW, 64 MB RWX);
 *   3. the RWX region is still RWX afterwards: new code written there runs, and pages
 *      read back as zero;
 *   4. a range that spans two regions of different protection keeps both.
 *
 * Build: toolchain/bin/tiger-clang64 -O1 -o decommit64 decommit64.c
 */
#include <mach/mach.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static int failures;
#define CHECK(cond, ...) do { printf((cond) ? "PASS  " : "FAIL  "); printf(__VA_ARGS__); printf("\n"); if (!(cond)) failures++; } while (0)

static unsigned long residentMB(void)
{
    struct task_basic_info info;
    mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&info, &count) != KERN_SUCCESS)
        return (unsigned long)-1;
    return (unsigned long)info.resident_size >> 20;
}

static int protectionAt(void* address)
{
    vm_address_t region = (vm_address_t)address;
    vm_size_t size = 0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object = MACH_PORT_NULL;
    if (vm_region_64(mach_task_self(), &region, &size, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &count, &object) != KERN_SUCCESS)
        return -1;
    if (region > (vm_address_t)address)
        return -2;
    return info.protection;
}

/* The same loop OSAllocatorPOSIX.cpp's TIGER64 decommit runs. */
static void decommit(void* address, size_t bytes)
{
    vm_address_t cursor = (vm_address_t)address;
    vm_address_t end = cursor + bytes;
    while (cursor < end) {
        vm_address_t region = cursor;
        vm_size_t size = 0;
        vm_region_basic_info_data_64_t info;
        mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t object = MACH_PORT_NULL;
        if (vm_region_64(mach_task_self(), &region, &size, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &count, &object) != KERN_SUCCESS)
            return;
        if (region >= end)
            return;
        vm_address_t start = region > cursor ? region : cursor;
        vm_address_t stop = region + size < end ? region + size : end;
        int prot = 0;
        if (info.protection & VM_PROT_READ) prot |= PROT_READ;
        if (info.protection & VM_PROT_WRITE) prot |= PROT_WRITE;
        if (info.protection & VM_PROT_EXECUTE) prot |= PROT_EXEC;
        if (prot)
            mmap((void*)start, stop - start, prot, MAP_FIXED | MAP_PRIVATE | MAP_ANON, -1, 0);
        cursor = stop;
    }
}

int main(void)
{
    const size_t rwSize = 256u << 20, rwxSize = 64u << 20;
    unsigned long base = residentMB();

    unsigned char* rw = mmap(0, rwSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    unsigned char* rwx = mmap(0, rwxSize, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    memset(rw, 0xab, rwSize);
    memset(rwx, 0xcd, rwxSize);
    unsigned long dirty = residentMB();
    printf("resident: base %lu MB, dirty %lu MB\n", base, dirty);

    CHECK(protectionAt(rw) == (VM_PROT_READ | VM_PROT_WRITE), "vm_region_64 sees RW (got %d)", protectionAt(rw));
    CHECK(protectionAt(rwx) == (VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE), "vm_region_64 sees RWX (got %d)", protectionAt(rwx));

    madvise(rw, rwSize, MADV_DONTNEED);
    CHECK(residentMB() + 200 > dirty, "control: MADV_DONTNEED leaves it resident (%lu MB)", residentMB());

    decommit(rw, rwSize);
    unsigned long afterRW = residentMB();
    CHECK(afterRW + 200 < dirty, "RW decommit drops RSS: %lu -> %lu MB", dirty, afterRW);
    CHECK(protectionAt(rw) == (VM_PROT_READ | VM_PROT_WRITE), "RW still RW (got %d)", protectionAt(rw));
    CHECK(rw[12345] == 0, "RW reads back zero");
    rw[12345] = 7;
    CHECK(rw[12345] == 7, "RW still writable");

    decommit(rwx, rwxSize);
    unsigned long afterRWX = residentMB();
    CHECK(afterRWX + 50 < afterRW, "RWX decommit drops RSS: %lu -> %lu MB", afterRW, afterRWX);
    CHECK(protectionAt(rwx) == (VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE), "RWX still RWX (got %d)", protectionAt(rwx));
    CHECK(rwx[4096 * 7] == 0, "RWX reads back zero");
    /* mov eax, 42; ret -- written into decommitted JIT memory and run, like a recommit. */
    static const unsigned char code[] = { 0xb8, 0x2a, 0x00, 0x00, 0x00, 0xc3 };
    memcpy(rwx + 8192, code, sizeof(code));
    CHECK(((int (*)(void))(rwx + 8192))() == 42, "code written after decommit runs");

    /* One range over two regions with different protection. */
    unsigned char* pair = mmap(0, 8 << 20, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    mprotect(pair + (4 << 20), 4 << 20, PROT_READ | PROT_WRITE);
    memset(pair, 1, 8 << 20);
    decommit(pair, 8 << 20);
    CHECK(protectionAt(pair) == 7 && protectionAt(pair + (4 << 20)) == 3, "split range keeps both protections (%d, %d)", protectionAt(pair), protectionAt(pair + (4 << 20)));
    CHECK(pair[100] == 0 && pair[(4 << 20) + 100] == 0, "split range zeroed");

    printf("%s (%d failures)\n", failures ? "FAILED" : "decommit by remap works", failures);
    return failures != 0;
}
