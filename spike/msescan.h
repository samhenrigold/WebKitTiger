/* msescan - completeness scanning for MSE appends.
 *
 * MediaSource hands a SourceBuffer arbitrary byte ranges, not whole segments,
 * and libavformat cannot be given a truncated fragment: the mov demuxer parses
 * a complete moof, builds the sample table from trun, then reads samples out of
 * a short mdat and emits truncated packets instead of failing. So before any
 * parse the caller must find where the last COMPLETE fragment ends and hand over
 * only that prefix, keeping the remainder buffered for the next append.
 *
 * Both functions are pure: no allocation, no state, no logging. They return the
 * byte length of the leading prefix of (p, n) that is safe to parse, which is 0
 * when nothing complete has arrived yet.
 *
 * See logs/mse-demux.md for why this is needed and spike/msescantest.c for the
 * tests, which run on the Tiger box against real DASH segments.
 */
#ifndef MSESCAN_H
#define MSESCAN_H

#include <stddef.h>
#include <stdint.h>

/* ISO-BMFF (fragmented MP4). Returns the offset just past the last complete
 * mdat, i.e. the end of the last complete moof+mdat fragment. A moof whose mdat
 * has not arrived is deliberately excluded: handing it over alone makes mov
 * consume it and emit nothing. An mdat declared with size 0 ("extends to end of
 * file") can never be known complete from a growing buffer and stops the walk. */
size_t mse_scan_mp4(const uint8_t *p, size_t n);

/* WebM/Matroska. Returns the offset just past the last complete Cluster.
 * Elements other than Clusters are walked over but never end the prefix, so a
 * trailing Cues or Tags cannot be committed on its own. A Cluster declared with
 * the all-ones "unknown size" (live streaming) stops the walk. */
size_t mse_scan_webm(const uint8_t *p, size_t n);

#endif
