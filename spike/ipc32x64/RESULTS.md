# Measured on the box (Core 2 Duo T7500, 6 GB, 10.4.11 build 8S2167)

Frame geometry throughout: 1440x900 BGRA, 5,184,000 bytes, which is 4.94 MiB. All figures from
`parent32` + `child64`; `RESULT` lines in the programs' own output are the raw source.

## Measurement caveat, read this first

**The box is shared with other agents' work, and load changes every number here.** Early runs were
taken while another agent's CPU-bound `cgprobe` held load average near 3.0 on two cores, and every
metric degraded monotonically with contention: the same binary measured an 11.2 us round trip when
quiet and 49.8 us at peak. The shared-memory comparison below was taken later on a quiet box and
repeated three times, and those runs agree with each other to about a percent, so that table can be
read at face value. The single figures further down are minimums across all twelve runs, which is a
conservative floor rather than a ceiling.

An earlier draft of this file presented a "quiet machine" column. That was wrong: the machine was not
quiet, and the run labelled as such was in fact the most contended of the set.

## Shared memory: mach memory entry vs POSIX shm

Both work across the split, and they are the same speed. Three consecutive runs on a quiet box, with
the two paths measured back to back in the same process against the same 10,368,000-byte double
buffer:

| measurement | mach memory entry | POSIX shm |
|---|---|---|
| 32-bit side memcpy of a frame | 5.27, 5.28, 5.45 ms | 5.16, 5.20, 5.37 ms |
| copy bandwidth | 906 to 938 MB/s | 920 to 959 MB/s |
| double-buffered pipeline | 136, 178, 181 fps | 157, 181, 185 fps |
| `glTexSubImage2D` upload, draw baseline subtracted | 7.76, 7.76, 7.94 ms | 7.74, 7.75, 7.76 ms |
| GL upload bandwidth | 622 to 637 MB/s | 637 to 639 MB/s |

POSIX shm is consistently a percent or two ahead, which is inside the noise, but it is never worse.
**OpenGL uploads straight from either mapping at the same rate**, 637 MB/s, matching to three
significant figures. There is no GL-side reason to prefer one.

`shm_open` + `ftruncate` + `mmap` handled the full 10.37 MB region with no size limit trouble, so the
audio bridge's finding scales from a PCM ring to tile buffers.

The tiebreaker is lifetime, not speed. A mach memory entry is a port: it needs no name, collides with
nothing, and disappears when the last right goes away, including when a process crashes. POSIX shm
needs a name in a global namespace and an explicit `shm_unlink`. This spike unlinks on the normal
quit path, which means a crashed content process leaks its segment until reboot. The usual fix is to
unlink immediately after both sides have mapped it, since the mapping outlives the name, but that
needs a handshake the mach path does not.

## Other results, best of twelve runs

| measurement | figure | notes |
|---|---|---|
| Mach RPC round trip | 11.2 us | one `mach_msg` doing send+receive, 10k iterations |
| 64 KB inline message | 241 MB/s | 2000 round trips, ack per message |
| 64-bit side painting a frame | 4.12 ms, 1199 MB/s | sequential 32-bit writes |
| GL draw of a full-screen textured quad | 1.55 ms | baseline, no upload |
| `GL_APPLE_client_storage` + `GL_APPLE_texture_range` | 12.08 ms, 409 MB/s | slower than a plain upload |

Out-of-line descriptors, shared memory mapping and exception-port delivery worked on every run,
contended or not. Those are pass/fail and load does not affect them.

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
