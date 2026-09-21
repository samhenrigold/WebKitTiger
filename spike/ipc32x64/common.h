/* Shared by the 32-bit parent and the 64-bit child. Every field in every message is a fixed-width
 * type or mach_port_t (4 bytes in both ABIs), so the structs are layout-identical across the split.
 * Nothing here may contain a pointer, long, or size_t: those are the fields that change width. */
#ifndef IPC32X64_COMMON_H
#define IPC32X64_COMMON_H

#include <mach/mach.h>
#include <mach/mach_time.h>
#include <servers/bootstrap.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define IPC_MSG_PING      100  /* inline payload, expects PONG */
#define IPC_MSG_PONG      101
#define IPC_MSG_PORT      102  /* carries a port right */
#define IPC_MSG_BULK      103  /* 64 KB inline payload */
#define IPC_MSG_OOL       104  /* out-of-line payload: tests 32/64 descriptor compatibility */
#define IPC_MSG_SHMEM     105  /* carries a memory entry port */
#define IPC_MSG_FRAME     106  /* "buffer N is ready" */
#define IPC_MSG_START     107  /* begin producing N frames */
#define IPC_MSG_ACK       108
#define IPC_MSG_CRASH     109  /* child dereferences NULL, to test exception forwarding */
#define IPC_MSG_TASKPORT  110
#define IPC_MSG_QUIT      199

#define IPC_BULK_BYTES    (64 * 1024)

/* Frame geometry for the compositing measurement: 1440x900 BGRA. */
#define IPC_FRAME_W       1440
#define IPC_FRAME_H       900
#define IPC_FRAME_BYTES   (IPC_FRAME_W * IPC_FRAME_H * 4)
#define IPC_BUFFERS       2
#define IPC_SHM_BYTES     (IPC_FRAME_BYTES * IPC_BUFFERS)

typedef struct {
    mach_msg_header_t header;
    uint32_t          op;
    uint32_t          seq;
    uint64_t          value;
} IPCSimpleMsg;

typedef struct {
    mach_msg_header_t header;
    uint32_t          op;
    uint32_t          seq;
    uint64_t          value;
    mach_msg_trailer_t trailer;
} IPCSimpleRcv;

typedef struct {
    mach_msg_header_t          header;
    mach_msg_body_t            body;
    mach_msg_port_descriptor_t port;   /* 12 bytes in both ABIs */
    uint32_t                   op;
    uint32_t                   seq;
} IPCPortMsg;

typedef struct {
    mach_msg_header_t          header;
    mach_msg_body_t            body;
    mach_msg_port_descriptor_t port;
    uint32_t                   op;
    uint32_t                   seq;
    mach_msg_trailer_t         trailer;
} IPCPortRcv;

typedef struct {
    mach_msg_header_t header;
    uint32_t          op;
    uint32_t          seq;
    uint8_t           payload[IPC_BULK_BYTES];
} IPCBulkMsg;

typedef struct {
    mach_msg_header_t header;
    uint32_t          op;
    uint32_t          seq;
    uint8_t           payload[IPC_BULK_BYTES];
    mach_msg_trailer_t trailer;
} IPCBulkRcv;

/* mach_msg_ool_descriptor_t holds a pointer, so it is 12 bytes on i386 and 16 on x86_64. This is the
 * one descriptor whose layout differs across the split; whether the kernel translates it is measured. */
typedef struct {
    mach_msg_header_t         header;
    mach_msg_body_t           body;
    mach_msg_ool_descriptor_t ool;
    uint32_t                  op;
    uint32_t                  seq;
} IPCOolMsg;

typedef struct {
    mach_msg_header_t         header;
    mach_msg_body_t           body;
    mach_msg_ool_descriptor_t ool;
    uint32_t                  op;
    uint32_t                  seq;
    mach_msg_trailer_t        trailer;
} IPCOolRcv;

/* The i386 ABI 4-byte-aligns an 8-byte field while x86_64 8-byte-aligns it, so a single uint64_t in
 * the wrong place shifts every later offset on one side only. Matching sizeof is not enough to prove
 * that: check the offsets. Both compilers compile this header, so a future field that breaks the
 * layout fails the build on at least one side instead of corrupting messages at runtime. */
_Static_assert(sizeof(mach_msg_header_t) == 24, "mach_msg_header_t must be 24 bytes in both ABIs");
_Static_assert(sizeof(mach_msg_body_t) == 4, "mach_msg_body_t must be 4 bytes");
_Static_assert(sizeof(mach_msg_port_descriptor_t) == 12, "port descriptor must be 12 bytes in both ABIs");

_Static_assert(offsetof(IPCSimpleMsg, op) == 24, "IPCSimpleMsg.op");
_Static_assert(offsetof(IPCSimpleMsg, seq) == 28, "IPCSimpleMsg.seq");
_Static_assert(offsetof(IPCSimpleMsg, value) == 32, "IPCSimpleMsg.value");
_Static_assert(sizeof(IPCSimpleMsg) == 40, "IPCSimpleMsg");

_Static_assert(offsetof(IPCPortMsg, body) == 24, "IPCPortMsg.body");
_Static_assert(offsetof(IPCPortMsg, port) == 28, "IPCPortMsg.port");
_Static_assert(offsetof(IPCPortMsg, op) == 40, "IPCPortMsg.op");
_Static_assert(offsetof(IPCPortMsg, seq) == 44, "IPCPortMsg.seq");
_Static_assert(sizeof(IPCPortMsg) == 48, "IPCPortMsg");

_Static_assert(offsetof(IPCBulkMsg, op) == 24, "IPCBulkMsg.op");
_Static_assert(offsetof(IPCBulkMsg, seq) == 28, "IPCBulkMsg.seq");
_Static_assert(offsetof(IPCBulkMsg, payload) == 32, "IPCBulkMsg.payload");

/* IPCOolMsg is deliberately NOT asserted to a fixed layout: mach_msg_ool_descriptor_t holds a
 * pointer, so it is 12 bytes on i386 and 16 on x86_64 and the kernel translates it on delivery.
 * It is the one message here whose offsets legitimately differ between the two sides. */
_Static_assert(sizeof(mach_msg_ool_descriptor_t) == (sizeof(void *) == 8 ? 16 : 12),
               "ool descriptor size follows pointer width");

static inline double ipcNowSeconds(void)
{
    static mach_timebase_info_data_t tb;
    if (!tb.denom) mach_timebase_info(&tb);
    return (double)mach_absolute_time() * tb.numer / tb.denom / 1e9;
}

static inline const char *ipcErr(kern_return_t kr) { return mach_error_string(kr); }

#endif
