/* Cross-ABI IPC wire-format proof.
 *
 * WHAT IS REAL HERE: wtf/ArgumentCoder.h is included from the tiger-ipc-abi branch, so
 * IPC::wireAlignmentOf and IPC::isWireUnstableArithmeticType are the patched definitions
 * themselves, compiled for both targets.
 *
 * WHAT IS MIRRORED: IPC::Encoder and IPC::Decoder cannot be compiled here. Encoder.h includes the
 * generated MessageNames.h, and no build in this tree has ever configured Source/WebKit, so that
 * header does not exist. The two functions that determine the wire format are transcribed verbatim
 * below from Encoder.cpp and Decoder.h on the branch:
 *
 *   Encoder::grow(alignment, size): alignedSize = roundUpToMultipleOf(alignment, m_bufferSize);
 *                                   zero the gap; m_bufferSize = alignedSize + size;
 *                                   return buffer.subspan(alignedSize)
 *   Decoder::decodeSpan<T>(size):   pos = roundUpToMultipleOf<ALIGN(T)>(pos);
 *                                   advance by size * sizeof(T)
 *
 * The real decoder rounds the buffer *pointer* rather than the offset, which is equivalent because
 * Decoder's constructor rejects a buffer whose base is not suitably aligned. We align the base here
 * for the same reason.
 *
 * The only thing that differs between the patched and unpatched builds is ALIGN(T). */
#ifndef ABIWIRE_H
#define ABIWIRE_H

#include <wtf/ArgumentCoder.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#if IPCABI_PATCHED
#  define ABI_ALIGN(T) (IPC::wireAlignmentOf<T>)
#  define ABI_BUILD_NAME "patched"
#else
#  define ABI_ALIGN(T) (alignof(T))
#  define ABI_BUILD_NAME "unpatched"
#endif

#define ABI_MSG_OFF     0u
#define ABI_MSG_MAX     (64u * 1024u)
#define ABI_TABLE_OFF   (64u * 1024u)
#define ABI_RESULT_OFF  (128u * 1024u)
#define ABI_SHM_BYTES   (192u * 1024u)
#define ABI_MAX_FIELDS  32u

/* Fixed width everywhere: this record itself crosses the split. */
typedef struct {
    char     name[24];
    uint32_t offset;
    uint32_t size;
} AbiFieldRecord;

typedef struct {
    uint32_t count;
    uint32_t pad;
    AbiFieldRecord fields[ABI_MAX_FIELDS];
} AbiFieldTable;

typedef struct {
    uint32_t decoded;      /* 1 if every field decoded to the expected value */
    uint32_t checked;
    uint32_t mismatched;   /* offset-table entries that disagree */
    uint32_t totalEncoded; /* encoder's total message size */
    uint32_t totalDecoded; /* decoder's final read position */
    uint32_t pad;
    char     detail[768];
} AbiResult;

/* ---- the transcribed primitives ---- */

typedef struct {
    uint8_t *base;
    uint32_t size;
    AbiFieldTable *table;
} AbiEncoder;

static inline uint32_t abiRoundUp(uint32_t alignment, uint32_t value)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

static inline void abiNote(AbiFieldTable *t, const char *name, uint32_t off, uint32_t size)
{
    if (!t || t->count >= ABI_MAX_FIELDS) return;
    AbiFieldRecord *r = &t->fields[t->count++];
    memset(r, 0, sizeof(*r));
    strncpy(r->name, name, sizeof(r->name) - 1);
    r->offset = off;
    r->size = size;
}

/* Encoder::grow */
#define ABI_ENCODE(enc, T, value, label) do { \
    uint32_t alignedSize = abiRoundUp((uint32_t)ABI_ALIGN(T), (enc)->size); \
    memset((enc)->base + (enc)->size, 0, alignedSize - (enc)->size); \
    T tmp_ = (T)(value); \
    memcpy((enc)->base + alignedSize, &tmp_, sizeof(T)); \
    abiNote((enc)->table, label, alignedSize, (uint32_t)sizeof(T)); \
    (enc)->size = alignedSize + (uint32_t)sizeof(T); \
} while (0)

typedef struct {
    const uint8_t *base;
    uint32_t pos;
    uint32_t limit;
    AbiFieldTable *table;
} AbiDecoder;

/* Decoder::decodeSpan */
#define ABI_DECODE(dec, T, out, label) do { \
    uint32_t aligned_ = abiRoundUp((uint32_t)ABI_ALIGN(T), (dec)->pos); \
    if (aligned_ + sizeof(T) > (dec)->limit) { (dec)->pos = (dec)->limit + 1; } \
    else { memcpy(&(out), (dec)->base + aligned_, sizeof(T)); \
           abiNote((dec)->table, label, aligned_, (uint32_t)sizeof(T)); \
           (dec)->pos = aligned_ + (uint32_t)sizeof(T); } \
} while (0)

/* ---- the message: deliberately mixed field widths ---- */

typedef struct {
    uint8_t  a;
    uint64_t b;
    double   c;
    int32_t  d;
    char     s[32];      /* stands in for a String: uint64 length then bytes */
    uint32_t sLen;
    uint8_t  hasOptional;
    uint64_t optional;
    uint32_t vecCount;
    uint64_t vec[4];     /* Vector<uint64_t> */
    uint32_t nestedX;    /* nested struct { uint32_t x; uint64_t y; } */
    uint64_t nestedY;
} AbiMessage;

static inline void abiFillMessage(AbiMessage *m)
{
    memset(m, 0, sizeof(*m));
    m->a = 0xA5;
    m->b = 0x0123456789ABCDEFull;
    m->c = 1234.5678;
    m->d = -77777;
    strcpy(m->s, "tiger-ipc");
    m->sLen = (uint32_t)strlen(m->s);
    m->hasOptional = 1;
    m->optional = 0xFEEDFACECAFEBEEFull;
    m->vecCount = 3;
    m->vec[0] = 1ull << 40; m->vec[1] = 2ull << 41; m->vec[2] = 3ull << 42;
    m->nestedX = 0xDEADBEEF;
    m->nestedY = 0x1122334455667788ull;
}

static inline uint32_t abiEncodeMessage(uint8_t *buffer, const AbiMessage *m, AbiFieldTable *table)
{
    AbiEncoder e = { buffer, 0, table };
    if (table) { table->count = 0; table->pad = 0; }
    ABI_ENCODE(&e, uint8_t,  m->a, "a:u8");
    ABI_ENCODE(&e, uint64_t, m->b, "b:u64");
    ABI_ENCODE(&e, double,   m->c, "c:double");
    ABI_ENCODE(&e, int32_t,  m->d, "d:i32");
    ABI_ENCODE(&e, uint64_t, m->sLen, "s.len:u64");
    for (uint32_t i = 0; i < m->sLen; ++i) ABI_ENCODE(&e, uint8_t, (uint8_t)m->s[i], "s.ch:u8");
    ABI_ENCODE(&e, uint8_t,  m->hasOptional, "opt.has:u8");
    ABI_ENCODE(&e, uint64_t, m->optional, "opt.val:u64");
    ABI_ENCODE(&e, uint64_t, m->vecCount, "vec.count:u64");
    for (uint32_t i = 0; i < m->vecCount; ++i) ABI_ENCODE(&e, uint64_t, m->vec[i], "vec.elem:u64");
    ABI_ENCODE(&e, uint32_t, m->nestedX, "nested.x:u32");
    ABI_ENCODE(&e, uint64_t, m->nestedY, "nested.y:u64");
    return e.size;
}

static inline uint32_t abiDecodeMessage(const uint8_t *buffer, uint32_t limit, AbiMessage *out, AbiFieldTable *table)
{
    AbiDecoder d = { buffer, 0, limit, table };
    memset(out, 0, sizeof(*out));
    if (table) { table->count = 0; table->pad = 0; }
    uint64_t sLen = 0, vecCount = 0;
    ABI_DECODE(&d, uint8_t,  out->a, "a:u8");
    ABI_DECODE(&d, uint64_t, out->b, "b:u64");
    ABI_DECODE(&d, double,   out->c, "c:double");
    ABI_DECODE(&d, int32_t,  out->d, "d:i32");
    ABI_DECODE(&d, uint64_t, sLen, "s.len:u64");
    out->sLen = (uint32_t)(sLen < 31 ? sLen : 31);
    for (uint32_t i = 0; i < out->sLen; ++i) { uint8_t ch = 0; ABI_DECODE(&d, uint8_t, ch, "s.ch:u8"); out->s[i] = (char)ch; }
    ABI_DECODE(&d, uint8_t,  out->hasOptional, "opt.has:u8");
    ABI_DECODE(&d, uint64_t, out->optional, "opt.val:u64");
    ABI_DECODE(&d, uint64_t, vecCount, "vec.count:u64");
    out->vecCount = (uint32_t)(vecCount < 4 ? vecCount : 4);
    for (uint32_t i = 0; i < out->vecCount; ++i) ABI_DECODE(&d, uint64_t, out->vec[i], "vec.elem:u64");
    ABI_DECODE(&d, uint32_t, out->nestedX, "nested.x:u32");
    ABI_DECODE(&d, uint64_t, out->nestedY, "nested.y:u64");
    return d.pos;
}

static inline int abiMessagesEqual(const AbiMessage *x, const AbiMessage *y)
{
    return x->a == y->a && x->b == y->b && x->c == y->c && x->d == y->d
        && x->sLen == y->sLen && strncmp(x->s, y->s, 31) == 0
        && x->hasOptional == y->hasOptional && x->optional == y->optional
        && x->vecCount == y->vecCount
        && x->vec[0] == y->vec[0] && x->vec[1] == y->vec[1] && x->vec[2] == y->vec[2]
        && x->nestedX == y->nestedX && x->nestedY == y->nestedY;
}

#endif
