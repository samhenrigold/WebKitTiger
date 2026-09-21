/* Wire format for the cross-ABI text test.
 *
 * A 64-bit process picks fonts and shapes with HarfBuzz; a 32-bit process
 * resolves the face and rasterises with Tiger's CoreText. Every field is
 * fixed-width, so the structs are layout-identical across the split. Nothing
 * here may hold a pointer, long or size_t. See spike/ipc32x64/common.h, whose
 * rule this follows and whose static assertions are the reason it is safe.
 *
 * The font bytes for a web font travel inline in 60 KB chunks rather than
 * out-of-line, because mach_msg_ool_descriptor_t is the one descriptor whose
 * size differs across the ABI (12 bytes on i386, 16 on x86_64) and this test
 * should not depend on that also being right.
 */
#ifndef TEXTPIXEL_COMMON_H
#define TEXTPIXEL_COMMON_H

#include <mach/mach.h>
#include <servers/bootstrap.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define TP_MSG_FONTDATA 200   /* one chunk of a web font's bytes */
#define TP_MSG_RUN      201   /* one shaped run, ready to rasterise */
#define TP_MSG_DONE     202
#define TP_MSG_ACK      203

#define TP_CHUNK      (60 * 1024)
#define TP_MAX_GLYPHS 192
#define TP_MAX_TEXT   96
#define TP_PATH       320
#define TP_LABEL      48

typedef struct {
    mach_msg_header_t header;
    uint32_t op;
    uint32_t seq;
    uint32_t dataId;          /* which web font these bytes belong to */
    uint32_t totalBytes;
    uint32_t offset;
    uint32_t length;
    uint8_t  bytes[TP_CHUNK];
} TPDataMsg;

typedef struct {
    TPDataMsg          msg;
    mach_msg_trailer_t trailer;
} TPDataRcv;

typedef struct {
    mach_msg_header_t header;
    uint32_t op;
    uint32_t seq;
    char     label[TP_LABEL];
    char     path[TP_PATH];   /* empty when dataId is set */
    int32_t  dataId;          /* -1 for a face that came from a file */
    int32_t  faceIndex;
    float    size;
    uint32_t rtl;
    uint32_t glyphCount;
    uint32_t textLength;
    uint32_t fellBack;        /* the shaper had to leave the requested family */
    uint16_t text[TP_MAX_TEXT];
    uint16_t glyphs[TP_MAX_GLYPHS];
    float    posX[TP_MAX_GLYPHS];
    float    posY[TP_MAX_GLYPHS];
} TPRunMsg;

typedef struct {
    TPRunMsg           msg;
    mach_msg_trailer_t trailer;
} TPRunRcv;

typedef struct {
    mach_msg_header_t header;
    uint32_t op;
    uint32_t seq;
} TPSimpleMsg;

typedef struct {
    TPSimpleMsg        msg;
    mach_msg_trailer_t trailer;
} TPSimpleRcv;

/* The whole point of the fixed-width rule: if a future field breaks the layout,
 * one of the two compilers fails here instead of both silently disagreeing. */
_Static_assert(sizeof(mach_msg_header_t) == 24, "header must be 24 bytes in both ABIs");
_Static_assert(offsetof(TPRunMsg, text) == 24 + 4 + 4 + TP_LABEL + TP_PATH + 4 + 4 + 4 + 4 + 4 + 4 + 4,
    "TPRunMsg field offsets must match across the split");
_Static_assert(sizeof(TPRunMsg) == 24 + 4 + 4 + TP_LABEL + TP_PATH + 4 * 7
    + TP_MAX_TEXT * 2 + TP_MAX_GLYPHS * 2 + TP_MAX_GLYPHS * 4 * 2,
    "TPRunMsg must have no padding either side");

#endif
