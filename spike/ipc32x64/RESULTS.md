# Measured on the box (Core 2 Duo T7500, 6 GB, 10.4.11 build 8S2167)

Frame geometry throughout: 1440x900 BGRA, 5,184,000 bytes, which is 4.94 MiB. All figures from
`parent32` + `child64`; `RESULT` lines in the programs' own output are the raw source.

## Measurement caveat, read this first

**The box was never idle while these were taken.** Load average sat near 3.0 on a dual-core machine
throughout, with another agent's CPU-bound `cgprobe` running alongside. Every metric degraded
monotonically with the amount of concurrent work: round-trip latency was measured at 11.2 us during
a quiet moment and 49.8 us at peak contention, from the same binary.

So the figures below are the **minimum across nine runs**, which is the closest available estimate of
the uncontended case. Treat them as conservative floors rather than as the machine's ceiling. They
are good enough to decide the drawing-area design, since the design conclusions hold with a wide
margin, but anyone tuning against them should re-measure on an idle box first.

An earlier draft of this file presented a "quiet machine" column. That was wrong: the machine was not
quiet, and the run labelled as such was in fact the most contended of the set.

## Results, best of nine runs

| measurement | figure | notes |
|---|---|---|
| Mach RPC round trip | 11.2 us | one `mach_msg` doing send+receive, 10k iterations |
| 64 KB inline message | 241 MB/s | 2000 round trips, ack per message |
| 32-bit side memcpy of a frame out of shared memory | 4.55 ms, 1086 MB/s | |
| 64-bit side painting a frame into shared memory | 5.09 ms, 971 MB/s | sequential 32-bit writes |
| double-buffered pipeline | 176 fps, 869 MB/s | paint and copy overlapped |
| GL draw of a full-screen textured quad | 1.74 ms | baseline, no upload |
| `glTexSubImage2D` from the mapped pointer | 8.11 ms, 610 MB/s | marginal cost, draw baseline subtracted |
| `GL_APPLE_client_storage` + `GL_APPLE_texture_range` | 13.43 ms, 368 MB/s | same accounting |

Out-of-line descriptors, shared memory mapping and exception-port delivery worked on every one of the
nine runs, contention or not. Those are pass/fail results and the load does not affect them.

## Two results that contradict the obvious expectation

**Client storage is slower than a plain upload here, by about 1.6x.** The zero-copy path is meant to
avoid the upload entirely, and it does avoid the copy at specification time, but the GPU then reads
the pixels across the bus on every draw instead of once into video memory. For a texture whose
contents change every frame and which is drawn once per frame, that trade loses. It would win for a
texture that is uploaded rarely and drawn often, which is not the compositing case.

**An earlier version of this benchmark reported 16.8 GB/s for client storage.** That number was
meaningless: it specified the texture and called `glFinish` without ever drawing, and client storage
does no work at specification time, so nothing was being measured. The benchmark now draws a
full-screen quad and subtracts a draw-only baseline. Worth remembering for any future GL timing on
this port: `glFinish` after `glTexImage2D` forces nothing when client storage is on.

## What this says about the drawing area

The IPC is not the bottleneck and is not close to being one. A round trip is tens of microseconds
against a 16.7 ms frame budget, so per-frame control messages are free. That conclusion survives the
contention caveat easily: even the worst measured round trip, 49.8 us, is a third of a percent of a
frame. Bulk pixel movement must go through the shared mapping rather than through messages: at
241 MB/s a 4.94 MiB frame would take 21 ms as inline message payload, which alone exceeds the frame
budget.

The real ceiling is the GL upload on the 32-bit side. At 8.1 ms for a full-screen upload plus 1.7 ms
to draw, a full-screen repaint costs about 10 ms of the 16.7 ms available at 60 Hz, and that is the
optimistic end of the measured range. The design should upload damaged tiles rather than whole
frames, and should not count on client storage to rescue it. This is the one conclusion that the
contention caveat genuinely threatens, so it deserves a clean re-measure on an idle box before any
decision rests on the exact headroom.

The 32-bit side does not need to memcpy at all. The 4.6 ms copy measured above exists only because
the benchmark copies to a sink; in the real pipeline the mapped pointer goes straight to
`glTexSubImage2D`. Removing that copy is worth more than any of the IPC tuning.
