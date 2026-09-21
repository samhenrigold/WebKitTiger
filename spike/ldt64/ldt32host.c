// The other direction: can a 32-bit task on Mac OS X 10.4 install a long-mode (L=1) code
// segment, execute 64-bit code through it, and does that code survive preemption? A
// 32-bit task's kernel save area holds only the 32-bit register set; if the upper halves
// of rax..r15 are not preserved across a timer interrupt, 64-bit code in a 32-bit task is
// unusable no matter what the segment machinery allows.
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
#include <architecture/i386/table.h>
#include <i386/user_ldt.h>

// 64-bit: mov eax,1 ; inc rax ; lret     -> eax=2 in long mode, 1 if decoded as 32-bit (48 = dec eax)
static const unsigned char probe64[] = { 0xb8,1,0,0,0, 0x48,0xff,0xc0, 0xcb };

// 64-bit: r15 = 0x1122334455667788; edx = 0 (bad count); ecx = 0x20000000
// loop: rax = r15 >> 32; if (eax != 0x11223344) edx++; r15 = const; if (--ecx) loop; eax = edx; lret
static const unsigned char loop64[] = {
    0x49,0xbf, 0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11,   // mov r15, imm64
    0x31,0xd2,                                            // xor edx, edx
    0xb9, 0x00,0x00,0x00,0x20,                            // mov ecx, 0x20000000
    // loop:
    0x4c,0x89,0xf8,                                       // mov rax, r15
    0x48,0xc1,0xe8,0x20,                                  // shr rax, 32
    0x3d, 0x44,0x33,0x22,0x11,                            // cmp eax, 0x11223344
    0x74,0x02,                                            // je ok
    0xff,0xc2,                                            // inc edx
    // ok:
    0x49,0xbf, 0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11,   // mov r15, imm64
    0xff,0xc9,                                            // dec ecx
    0x75,0xe2,                                            // jnz loop  (-30)
    0x89,0xd0,                                            // mov eax, edx
    0xcb                                                  // lret
};

int main(void) {
    union ldt_entry d; memset(&d, 0, sizeof d);
    uint32_t* raw = (uint32_t*)&d;
    raw[0] = 0x0000ffff;          // base 0, limit 0xfffff
    raw[1] = 0x00affa00;          // G=1, D=0, L=1, limit hi=f, P, DPL3, code readable
    int sel = i386_set_ldt(LDT_AUTO_ALLOC, &d, 1);
    printf("i386_set_ldt(alloc, L=1 code64) -> %d", sel);
    if (sel < 0) { printf("  errno=%d (%s)\n", errno, strerror(errno)); return 1; }
    uint16_t cs64 = (uint16_t)((sel << 3) | 7);
    printf("  selector 0x%x\n", cs64);

    unsigned char* page = mmap(NULL, 4096, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANON, -1, 0);
    if (page == MAP_FAILED) { perror("mmap"); return 2; }
    memcpy(page, probe64, sizeof probe64);
    memcpy(page + 64, loop64, sizeof loop64);

    struct { uint32_t off; uint16_t seg; } __attribute__((packed)) fp = { (uint32_t)(uintptr_t)page, cs64 };
    uint32_t r = 0;
    printf("far-calling mode probe ..."); fflush(stdout);
    __asm__ volatile ("lcall *%1" : "=a"(r) : "m"(fp) : "memory", "ecx", "edx");
    printf(" eax=%u -> %s\n", r, r == 2 ? "LONG MODE (64-bit decode)" : r == 1 ? "32-bit decode (L bit ignored)" : "??");
    if (r != 2) return 3;

    fp.off = (uint32_t)(uintptr_t)(page + 64);
    printf("running ~0.5G iterations of 64-bit code checking r15's upper half across preemptions ..."); fflush(stdout);
    __asm__ volatile ("lcall *%1" : "=a"(r) : "m"(fp) : "memory", "ecx", "edx");
    printf(" corrupted iterations: %u\n", r);
    printf(r ? "VERDICT: 64-bit state is NOT preserved for a 32-bit task\n" : "VERDICT: survived preemption with upper halves intact\n");
    return 0;
}
