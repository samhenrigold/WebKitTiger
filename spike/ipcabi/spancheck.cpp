/* The fifth offender class: ArgumentCoder<std::span<T>> bulk-copies size * sizeof(T), so sizeof(T)
 * is on the wire. Uses the real isWireStableSpanElement from wtf/ArgumentCoder.h on the branch.
 *
 * Build with -DSPANCHECK_EXPECT_REJECT=1 to compile the case that must FAIL. */
#include <wtf/ArgumentCoder.h>
#include <cstdint>
#include <cstddef>
#include <cstdio>

// Safe: every field is 4 bytes or smaller, so i386 and x86_64 lay it out identically.
struct AllFourByte { uint32_t a; int32_t b; float c; uint8_t d; };

// Unsafe: the uint64_t is 4-byte-aligned on i386 and 8-byte-aligned on x86_64, so the struct is
// 12 bytes on one side and 16 on the other, and a bulk copy of N of them disagrees by 4N bytes.
struct MixedField { uint32_t x; uint64_t y; };

// Also unsafe, and in a way no field-type audit would flag: the divergence comes from the nested
// member rather than from any field declared here.
struct NestsAMixedField { uint32_t head; MixedField inner; };

static_assert(IPC::isWireStableSpanElement<uint8_t>, "scalars are fine");
static_assert(IPC::isWireStableSpanElement<uint64_t>, "an 8-byte scalar is the same width in both");
static_assert(IPC::isWireStableSpanElement<double>, "likewise");
static_assert(!IPC::isWireStableSpanElement<size_t>, "size_t changes width");
static_assert(IPC::isWireStableSpanElement<AllFourByte>, "all-4-byte composites are fine");

#if SPANCHECK_EXPECT_REJECT
// Mirrors the static_assert the real span coder now carries.
template<typename T> struct SpanCoderGate {
    static_assert(IPC::isWireStableSpanElement<std::remove_cv_t<T>>,
        "span element size differs across the split");
};
template struct SpanCoderGate<MixedField>;
template struct SpanCoderGate<NestsAMixedField>;
#endif

int main()
{
    std::printf("%d-bit  sizeof: AllFourByte=%u MixedField=%u NestsAMixedField=%u\n",
        (int)(sizeof(void*) * 8), (unsigned)sizeof(AllFourByte),
        (unsigned)sizeof(MixedField), (unsigned)sizeof(NestsAMixedField));
    std::printf("        alignof: MixedField=%u   wire-stable verdict: AllFourByte=%d MixedField=%d Nested=%d\n",
        (unsigned)alignof(MixedField),
        (int)IPC::isWireStableSpanElement<AllFourByte>,
        (int)IPC::isWireStableSpanElement<MixedField>,
        (int)IPC::isWireStableSpanElement<NestsAMixedField>);
    return 0;
}
