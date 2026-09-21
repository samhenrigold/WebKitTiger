#include "h32common.h"
__asm__(
".text\n.align 4\n"
".globl _sm_beg\n_sm_beg:\n"
".code64\n"
".globl _sm_entry\n_sm_entry:\n"
"   movl %edi, %r13d\n"
"   movabsq $0x1122334455667788, %r15\n"
"   movq %r15, (%r13)\n"
"   movl $1, %eax\n"
"   incq %rax\n"          // 64-bit decode -> 2 ; 32-bit decode -> 1 (dec eax)
"   lret\n"
".code32\n"
".globl _sm_end\n_sm_end:\n");
extern const unsigned char sm_beg[], sm_entry[], sm_end[];
int main(void) {
    unsigned char* p = h32_load(sm_beg, sm_end, NULL);
    uint64_t out = 0;
    uint32_t r = h32_call64(p + (sm_entry - sm_beg), (uint32_t)(uintptr_t)&out);
    printf("cs64=0x%x eax=%u r15=0x%llx -> %s\n", h32_cs64_sel, r, out,
           (r == 2 && out == 0x1122334455667788ULL) ? "LONG MODE OK" : "FAIL");
    return !(r == 2 && out == 0x1122334455667788ULL);
}
