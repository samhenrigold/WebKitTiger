# icmprobe: QuickTime 7 ICMDecompressionSession H.264 decode throughput on Tiger, i386.
#   make -C spike/media32            build
#   make -C spike/media32 run        copy binary + clips to tiger:/tmp/media32, run, print numbers
ROOT  := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../..)
CC    := $(ROOT)/toolchain/bin/tiger-clang
CLIPS := $(ROOT)/spike/media64/h264-480p30.mp4 $(ROOT)/spike/media64/h264-720p30.mp4 \
         $(ROOT)/spike/media32/h264-360p30.mp4 $(ROOT)/spike/qtkit-media/test.mp4

icmprobe: icmprobe.c
	$(CC) -O2 -g -Wall -Wno-deprecated-declarations -o $@ $< \
	    -framework QuickTime -framework CoreVideo -framework ApplicationServices -framework Carbon

# 360p High profile, what YouTube's itag 18 / 360p DASH looks like. Needs ffmpeg on this Mac.
h264-360p30.mp4: $(ROOT)/spike/media64/bbb-src
	ffmpeg -v error -y -i $< -t 10 -an -vf scale=640:360 -c:v libx264 -profile:v high -preset medium -b:v 700k -g 60 -r 30 $@

run: icmprobe h264-360p30.mp4
	ssh tiger 'mkdir -p /tmp/media32'
	scp -qO icmprobe $(CLIPS) tiger:/tmp/media32/
	ssh tiger 'cd /tmp/media32 && for c in test.mp4 h264-360p30.mp4 h264-480p30.mp4 h264-720p30.mp4; do ./icmprobe $$c; ./icmprobe $$c copy; done'

clean:
	rm -f icmprobe h264-360p30.mp4
.PHONY: run clean
