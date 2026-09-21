// Can a 64-bit task on Mac OS X 10.4 install a 32-bit code segment via the
// machdep i386_set_ldt syscall, and execute compatibility-mode code through it?
// libSystem's x86_64 slice has no wrapper, so this is the raw syscall:
// class MDEP (3 << 24), number 5 (set_ldt) / 6 (get_ldt), per xnu's mdep_call table.
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>

union ldt_entry { struct { uint32_t lo, hi; } raw; };

static long mdep(long num, long a, long b, long c) {
    long ret; unsigned char cf;
    __asm__ volatile ("syscall; setc %1" : "=a"(ret), "=r"(cf)
                      : "0"(0x3000000L | num), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory");
    if (cf) { errno = (int)ret; return -1; }
    return ret;
}

// 32-bit code placed below 4 GB: mov eax, 0x2a ; lret  (far return to the 64-bit caller)
static const unsigned char code32[] = { 0xb8, 0x2a, 0x00, 0x00, 0x00, 0xcb };

int main(void) {
    union ldt_entry d; memset(&d, 0, sizeof d);
    // base 0, limit 0xfffff, 4K granularity, 32-bit default (D=1), present, DPL3, code, readable
    d.raw.lo = 0x0000ffff;
    d.raw.hi = 0x00cffa00;
    long sel = mdep(5, -1 /* LDT_AUTO_ALLOC */, (long)&d, 1);
    printf("i386_set_ldt(alloc, code32) -> %ld", sel);
    if (sel < 0) { printf("  errno=%d (%s)\n", errno, strerror(errno)); return 1; }
    long cs32 = (sel << 3) | 7;   // LDT, RPL3
    printf("  selector 0x%lx\n", cs32);

    void* low = mmap((void*)0x10000000, 4096, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANON|MAP_FIXED, -1, 0);
    if (low == MAP_FAILED || (uintptr_t)low >= 0x100000000ULL) { printf("no <4GB page: %s\n", strerror(errno)); return 2; }
    memcpy(low, code32, sizeof code32);

    // Far call: push 64-bit CS and return address is done by lcall with a 32-bit far pointer.
    struct { uint32_t off; uint16_t seg; } __attribute__((packed)) fp = { (uint32_t)(uintptr_t)low, (uint16_t)cs32 };
    uint32_t result = 0;
    printf("far-calling 0x%x:0x%x ...\n", fp.seg, fp.off); fflush(stdout);
    __asm__ volatile ("lcall *%1" : "=a"(result) : "m"(fp) : "memory", "rcx", "rdx");
    printf("returned to long mode, eax=0x%x (%s)\n", result, result == 0x2a ? "32-bit code ran" : "unexpected");
    return 0;
}
