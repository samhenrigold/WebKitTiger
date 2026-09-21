#!/bin/bash
# Build DASH-style init + media segments, the way YouTube feeds MediaSource.
# Host (homebrew) ffmpeg. Inputs come from spike/make-media64.sh output.
# Output: spike/media64/fmp4/  (H.264 High 720p + AAC)
#         spike/media64/fwebm/ (VP9 720p + Opus)
set -euo pipefail
cd "$(dirname "$0")/media64"
Q="-hide_banner -loglevel error -y"

# 30 s sources (the 10 s clip looped 3x) with a 2 s closed GOP, so the dash muxer can
# cut 2 s segments that each start on a keyframe - the shape YouTube serves.
KF="-g 60 -keyint_min 60 -sc_threshold 0"
ffmpeg $Q -stream_loop 2 -i bbb-src -i aac-128k.m4a -t 30 -map 0:v -map 1:a \
  -c:v libx264 -profile:v high -preset medium -b:v 2500k -vf scale=1280:720 -r 30 $KF \
  -c:a aac -b:a 128k -ac 2 mse-src.mp4
ffmpeg $Q -stream_loop 2 -i bbb-src -i opus-96k.webm -t 30 -map 0:v -map 1:a \
  -c:v libvpx-vp9 -b:v 2000k -row-mt 1 -speed 4 -vf scale=1280:720 -r 30 $KF \
  -c:a libopus -b:a 96k mse-src.webm

rm -rf fmp4 fwebm && mkdir -p fmp4 fwebm

# frag_keyframe+empty_moov+default_base_moof is what the dash muxer already emits;
# spell it out so the fragment shape is explicit and matches the MSE feed.
ffmpeg $Q -i mse-src.mp4 -c copy -f dash -seg_duration 2 -use_template 0 -use_timeline 0 \
  -movflags "+frag_keyframe+empty_moov+default_base_moof" fmp4/out.mpd
ffmpeg $Q -i mse-src.webm -c copy -f dash -dash_segment_type webm -seg_duration 2 \
  -use_template 0 -use_timeline 0 fwebm/out.mpd

echo "--- fmp4"; ls -la fmp4 | head -30
echo "--- fwebm"; ls -la fwebm | head -30
