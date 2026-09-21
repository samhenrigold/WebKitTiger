#!/bin/bash
# Verify the 10.4u SDK against its checksum manifest (sdk/MacOSX10.4u.sdk.sha256). Exit 1 on any difference.
cd "$(dirname "$0")/../sdk" || exit 2
if shasum -a 256 -c --quiet MacOSX10.4u.sdk.sha256; then echo "SDK OK"; else echo "SDK MODIFIED (see above); restore from MacOSX10.4u.sdk.tar.xz"; exit 1; fi
