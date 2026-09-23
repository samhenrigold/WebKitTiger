/* TIGER64: the SSE4.1-free rounding sequence MacroAssemblerX86_64::roundWithX87 emits.
 *
 * A 2007 Core 2 has no roundsd/roundss, and JSC emits them in a dozen places without
 * asking supportsFloatingPointRounding() first. The replacement is x87: frndint rounds
 * with the mode in the control word's bits 11:10, in the same encoding as roundsd's
 * immediate, and it needs no scratch register.
 *
 * This runs the exact instruction sequence (same instructions, same order) against libm
 * for every awkward value, so the JIT change does not have to be trusted on inspection.
 *
 * Build: toolchain/bin/tiger-clang64 -O1 -o x87round64 x87round64.c && run on the box.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

/* mode is the whole x87 control word the JIT stores: 0x37f (Darwin's default: all
   exceptions masked, extended precision) with the rounding-control field in bits 11:10 --
   0 = nearest-even, 0x400 = down, 0x800 = up, 0xc00 = toward zero. */
static double roundDouble(double x, int mode)
{
    double out;
    __asm__ volatile (
        "leaq -16(%%rsp), %%rsp\n\t"
        "movsd %1, (%%rsp)\n\t"
        "fnstcw 8(%%rsp)\n\t"
        "movl %2, 12(%%rsp)\n\t"
        "fldcw 12(%%rsp)\n\t"
        "fldl (%%rsp)\n\t"
        "frndint\n\t"
        "fstpl (%%rsp)\n\t"
        "fldcw 8(%%rsp)\n\t"
        "movsd (%%rsp), %0\n\t"
        "leaq 16(%%rsp), %%rsp\n\t"
        : "=x"(out) : "x"(x), "r"(mode) : "memory", "cc");
    return out;
}

static float roundFloat(float x, int mode)
{
    float out;
    __asm__ volatile (
        "leaq -16(%%rsp), %%rsp\n\t"
        "movss %1, (%%rsp)\n\t"
        "fnstcw 8(%%rsp)\n\t"
        "movl %2, 12(%%rsp)\n\t"
        "fldcw 12(%%rsp)\n\t"
        "flds (%%rsp)\n\t"
        "frndint\n\t"
        "fstps (%%rsp)\n\t"
        "fldcw 8(%%rsp)\n\t"
        "movss (%%rsp), %0\n\t"
        "leaq 16(%%rsp), %%rsp\n\t"
        : "=x"(out) : "x"(x), "r"(mode) : "memory", "cc");
    return out;
}

static int sameDouble(double a, double b)
{
    if (isnan(a) && isnan(b))
        return 1;
    uint64_t ua, ub;
    memcpy(&ua, &a, 8);
    memcpy(&ub, &b, 8);
    return ua == ub; /* bit-exact, so -0.0 != 0.0 */
}

static void checkDouble(const char* name, double got, double want, double input)
{
    if (sameDouble(got, want))
        return;
    printf("FAIL  %s(%.17g): got %.17g want %.17g\n", name, input, got, want);
    failures++;
}

static void checkFloat(const char* name, float got, float want, float input)
{
    uint32_t ug, uw;
    memcpy(&ug, &got, 4);
    memcpy(&uw, &want, 4);
    if (ug == uw || (isnan(got) && isnan(want)))
        return;
    printf("FAIL  %s(%.9g): got %.9g want %.9g\n", name, (double)input, (double)got, (double)want);
    failures++;
}

int main(void)
{
    static const double values[] = {
        0.0, -0.0, 0.5, -0.5, 1.5, -1.5, 2.5, -2.5, 3.5, -3.5,
        0.49999999999999994, -0.49999999999999994, 1.0, -1.0,
        1e-300, -1e-300, 4503599627370495.5 /* 2^52-0.5 */, -4503599627370495.5,
        4503599627370496.0 /* 2^52 */, -4503599627370496.0, 1e300, -1e300,
        9007199254740993.0, 123456789.75, -123456789.75, 1500.9, -1500.9,
        255.5, 254.5, 2147483647.5, -2147483648.5,
        INFINITY, -INFINITY, NAN,
    };
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        double x = values[i];
        checkDouble("nearest", roundDouble(x, 0x37f + 0x000), nearbyint(x), x); /* default mode is ties-to-even */
        checkDouble("floor", roundDouble(x, 0x37f + 0x400), floor(x), x);
        checkDouble("ceil", roundDouble(x, 0x37f + 0x800), ceil(x), x);
        // 10.4's libm trunc(-0.5) returns +0; C99 and roundsd both say -0, so the sign
        // is taken from the input rather than from trunc().
        checkDouble("trunc", roundDouble(x, 0x37f + 0xc00), copysign(trunc(x), x), x);

        float f = (float)x;
        checkFloat("nearestf", roundFloat(f, 0x37f + 0x000), nearbyintf(f), f);
        checkFloat("floorf", roundFloat(f, 0x37f + 0x400), floorf(f), f);
        checkFloat("ceilf", roundFloat(f, 0x37f + 0x800), ceilf(f), f);
        checkFloat("truncf", roundFloat(f, 0x37f + 0xc00), copysignf(truncf(f), f), f);
    }

    /* The control word must come back unchanged, or every later x87 user is affected. */
    int one = 1;
    double value = 1.5;
    uint16_t before = 0, after = 0;
    __asm__ volatile ("fnstcw %0" : "=m"(before));
    (void)roundDouble(1.5, 0x37f + 0x400);
    __asm__ volatile ("fnstcw %0" : "=m"(after));
    if (before != after) {
        printf("FAIL  control word: 0x%04x -> 0x%04x\n", before, after);
        failures++;
    }

    /* roundsd does not touch the flags, so neither may the replacement. */
    int zeroFlagSurvived = 0;
    __asm__ volatile (
        "cmpl $1, %1\n\t"          /* ZF = 1 */
        "leaq -16(%%rsp), %%rsp\n\t"
        "movsd %2, (%%rsp)\n\t"
        "fnstcw 8(%%rsp)\n\t"
        "movl $0x77f, 12(%%rsp)\n\t"
        "fldcw 12(%%rsp)\n\t"
        "fldl (%%rsp)\n\t"
        "frndint\n\t"
        "fstpl (%%rsp)\n\t"
        "fldcw 8(%%rsp)\n\t"
        "movsd (%%rsp), %2\n\t"
        "leaq 16(%%rsp), %%rsp\n\t"
        "sete %b0\n\t"
        : "=q"(zeroFlagSurvived), "+r"(one), "+x"(value) :: "memory", "cc");
    if (!zeroFlagSurvived) {
        printf("FAIL  the sequence clobbered the flags\n");
        failures++;
    }

    printf("%s (%d failures)\n", failures ? "FAILED" : "all rounding modes match libm", failures);
    return failures != 0;
}
