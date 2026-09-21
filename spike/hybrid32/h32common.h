// hybrid32: shared scaffolding for the 32-bit-host / 64-bit-guest probes.
//
// All 64-bit guest code is written as `.code64` inline asm inside these i386
// programs, bracketed by global labels, and memcpy'd at runtime onto an RWX
// page. That keeps one file per probe and needs no separate assembler step --
// the only rule is that the guest code must be position-independent (relative
// branches only, no symbol references) and must make no libSystem/syscall of
// its own.
#ifndef H32COMMON_H
#define H32COMMON_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
#include <architecture/i386/table.h>
#include <i386/user_ldt.h>

static uint16_t h32_cs64_sel;   // long-mode code selector
static uint16_t h32_cs32_sel;   // our own (32-bit) code selector

// Install an L=1 (long mode) user code descriptor in the task's LDT.
static uint16_t h32_cs64(void) {
    if (h32_cs64_sel) return h32_cs64_sel;
    union ldt_entry d; memset(&d, 0, sizeof d);
    uint32_t* raw = (uint32_t*)&d;
    raw[0] = 0x0000ffff;   // base 0, limit 0xfffff
    raw[1] = 0x00affa00;   // G=1, D=0, L=1, P=1, DPL=3, code, readable
    int sel = i386_set_ldt(LDT_AUTO_ALLOC, &d, 1);
    if (sel < 0) { fprintf(stderr, "i386_set_ldt: %s\n", strerror(errno)); exit(1); }
    __asm__ volatile ("movw %%cs, %0" : "=r"(h32_cs32_sel));
    h32_cs64_sel = (uint16_t)((sel << 3) | 7);
    return h32_cs64_sel;
}

// Copy [beg,end) onto a fresh RWX page (optionally at a fixed hint) and return it.
static unsigned char* h32_load(const void* beg, const void* end, void* hint) {
    size_t n = (const char*)end - (const char*)beg;
    size_t sz = (n + 4095) & ~(size_t)4095;
    unsigned char* p = mmap(hint, sz, PROT_READ|PROT_WRITE|PROT_EXEC,
                            MAP_PRIVATE|MAP_ANON|(hint ? MAP_FIXED : 0), -1, 0);
    if (p == MAP_FAILED) { perror("mmap"); exit(2); }
    memcpy(p, beg, n);
    return p;
}

// Far-call into long mode. arg lands in rdi (zero-extended from edi).
// The guest must preserve rbx/rbp and end with `lret`.
static uint32_t h32_call64(void* fn, uint32_t arg) {
    struct { uint32_t off; uint16_t seg; } __attribute__((packed))
        fp = { (uint32_t)(uintptr_t)fn, h32_cs64() };
    uint32_t r;
    __asm__ volatile ("lcall *%2"
                      : "=a"(r), "+D"(arg)
                      : "m"(fp) : "memory", "ecx", "edx", "esi", "cc");
    return r;
}
#endif
