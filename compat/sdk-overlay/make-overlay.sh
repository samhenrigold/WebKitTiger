#!/bin/sh
# TIGER SDK OVERLAY: (re)build the framework header directories.
#
# A framework on the search path binds by name: once clang resolves
# "Foundation" to this overlay, every <Foundation/*.h> must be found here -- it
# does not fall through to the next -F. So each overlaid framework needs a
# complete Headers directory.
#
# This script symlinks every header the 10.4u SDK has and never touches a real
# file, so the headers we patch stay ours and everything else tracks the SDK.
# `ls -l` tells the two apart at a glance; README.md records why each real file
# is real.
set -e

# The SDK is read-only and this script only ever creates symlinks into it, but a
# damaged SDK would be silently baked into the overlay, so check first. A write
# through one of these symlinks -- shell redirection and open(,'w') both follow
# them -- is what truncated two SDK headers once. Delete the symlink before
# creating a real file at any path this script manages.
if [ -x "$(dirname "$0")/../../toolchain/verify-sdk.sh" ]; then
    "$(dirname "$0")/../../toolchain/verify-sdk.sh" || {
        echo "make-overlay.sh: the 10.4u SDK does not match its manifest; refusing to run" >&2
        exit 1
    }
fi
WKT=${WKT:-/Users/shg/Developer/WebKitTiger}
SDK=$WKT/sdk/MacOSX10.4u.sdk/System/Library/Frameworks
HERE=$(cd "$(dirname "$0")" && pwd)

link_framework() {
    name=$1 src=$2
    dest="$HERE/$name.framework/Headers"
    mkdir -p "$dest"
    for header in "$src/$name.framework/Headers"/*.h; do
        base=$(basename "$header")
        # A real file here is one of ours; leave it alone.
        if [ -f "$dest/$base" ] && [ ! -L "$dest/$base" ]; then
            continue
        fi
        ln -sfn "$header" "$dest/$base"
    done

    # WARNING for anyone adding an overlaid header by hand: every entry above is
    # a symlink INTO THE SDK, and both shell redirection and a plain open(,'w')
    # follow it. "cat $SDK/Foo.h > $dest/Foo.h" therefore truncates the SDK's own
    # header and then cats the file into itself, which eats the disk. Delete the
    # symlink first, or read the SDK copy fully into memory before writing.
}

link_framework Foundation "$SDK"
link_framework AppKit "$SDK"
link_framework CoreFoundation "$SDK"
link_framework CoreGraphics "$SDK/ApplicationServices.framework/Frameworks"
link_framework ImageIO "$SDK/ApplicationServices.framework/Frameworks"

echo "overlay rebuilt: $HERE"
