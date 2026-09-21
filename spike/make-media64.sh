#!/bin/bash
# Generate the decodebench test clips from a 10 s Big Buck Bunny 1080p source (CC-BY, Blender Foundation).
# Uses the HOST (homebrew) ffmpeg. Source: spike/media64/bbb-src
#   curl -L -o bbb-src https://test-videos.co.uk/vids/bigbuckbunny/mp4/h264/1080/Big_Buck_Bunny_1080_10s_30MB.mp4
set -euo pipefail
cd "$(dirname "$0")/media64"
SRC=bbb-src
Q="-hide_banner -loglevel error -y -t 10 -i $SRC"

# H.264 High profile, YouTube-ish bitrates
ffmpeg $Q -c:v libx264 -profile:v high -preset medium -b:v 4500k -vf scale=1920:1080 -r 30 -an h264-1080p30.mp4
ffmpeg $Q -c:v libx264 -profile:v high -preset medium -b:v 2500k -vf scale=1280:720  -r 30 -an h264-720p30.mp4
ffmpeg $Q -c:v libx264 -profile:v high -preset medium -b:v 1100k -vf scale=854:480   -r 30 -an h264-480p30.mp4

# VP9 in WebM
ffmpeg $Q -c:v libvpx-vp9 -b:v 2000k -row-mt 1 -speed 2 -vf scale=1280:720 -r 30 -an vp9-720p30.webm
ffmpeg $Q -c:v libvpx-vp9 -b:v 900k  -row-mt 1 -speed 2 -vf scale=854:480  -r 30 -an vp9-480p30.webm

# AV1 (decoded by libdav1d)
ffmpeg $Q -c:v libsvtav1 -crf 40 -preset 8 -vf scale=854:480 -r 30 -an av1-480p30.mp4

# Audio (the source clip is video-only, so synthesize 60 s of broadband stereo)
A="-hide_banner -loglevel error -y -f lavfi -i anoisesrc=d=60:c=pink:r=48000:a=0.4 -f lavfi -i sine=f=440:d=60:r=48000 -filter_complex [0][1]amix=inputs=2,aformat=channel_layouts=stereo"
ffmpeg $A -c:a aac     -b:a 128k aac-128k.m4a
ffmpeg $A -c:a libopus -b:a 96k  opus-96k.webm
ffmpeg $A -c:a libmp3lame -b:a 128k mp3-128k.mp3

ls -la
