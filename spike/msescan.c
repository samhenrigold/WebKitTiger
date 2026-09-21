#include "msescan.h"
#include <string.h>

/* ---- ISO-BMFF ---------------------------------------------------------- */

size_t mse_scan_mp4(const uint8_t *p, size_t n)
{
    size_t off = 0, frag_end = 0;

    while (off + 8 <= n) {
        uint64_t size = ((uint64_t)p[off] << 24) | ((uint64_t)p[off + 1] << 16)
                      | ((uint64_t)p[off + 2] << 8) | (uint64_t)p[off + 3];
        const uint8_t *type = p + off + 4;
        size_t hdr = 8;

        if (size == 1) {
            /* 64-bit largesize in the 8 bytes after the type. */
            if (off + 16 > n) break;
            size = 0;
            for (int i = 0; i < 8; i++) size = (size << 8) | p[off + 8 + i];
            hdr = 16;
        } else if (size == 0) {
            /* "To the end of the file." A growing buffer has no end, so this
             * box can never be known complete. Correct for MSE: stop here. */
            break;
        }

        if (size < hdr) break;                       /* malformed */
        if (size - hdr > n - off - hdr) break;       /* not all here yet */

        off += (size_t)size;
        if (!memcmp(type, "mdat", 4))
            frag_end = off;                          /* a fragment just closed */
    }
    return frag_end;
}

/* ---- EBML / WebM -------------------------------------------------------- */

/* Read one EBML variable-length integer at `off`. `keep_marker` leaves the
 * length marker bit in place, which is how element IDs are compared; sizes want
 * it stripped. Returns the encoded length, or 0 if the value is not all here. */
static int ebml_vint(const uint8_t *p, size_t n, size_t off, int keep_marker, uint64_t *val)
{
    if (off >= n) return 0;

    uint8_t first = p[off];
    int len = 0;
    for (int i = 0; i < 8; i++) {
        if (first & (0x80 >> i)) { len = i + 1; break; }
    }
    if (!len) return 0;                              /* 0x00: invalid */
    if (off + (size_t)len > n) return 0;

    uint64_t v = keep_marker ? first : (uint64_t)(first & (0xFF >> len));
    for (int i = 1; i < len; i++) v = (v << 8) | p[off + i];
    *val = v;
    return len;
}

#define EBML_ID_CLUSTER 0x1F43B675ULL

size_t mse_scan_webm(const uint8_t *p, size_t n)
{
    size_t off = 0, cluster_end = 0;

    for (;;) {
        uint64_t id, size;
        int idlen = ebml_vint(p, n, off, 1, &id);
        if (!idlen) break;
        if (idlen > 4) break;                        /* not a valid element ID */

        int szlen = ebml_vint(p, n, off + idlen, 0, &size);
        if (!szlen) break;

        /* All value bits set means "unknown size": only resolvable by finding
         * the next element, which a growing buffer cannot guarantee. */
        uint64_t unknown = (1ULL << (7 * szlen)) - 1;
        if (size == unknown) break;

        size_t hdr = (size_t)idlen + (size_t)szlen;
        if (size > n - off - hdr) break;             /* not all here yet */

        off += hdr + (size_t)size;
        if (id == EBML_ID_CLUSTER)
            cluster_end = off;
    }
    return cluster_end;
}
