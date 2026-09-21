# tifftclConfig.sh --
# 
# This shell script (for sh) is generated automatically by tifftcl's
# configure script.  It will create shell variables for most of
# the configuration options discovered by the configure script.
# This script is intended to be included by the configure scripts
# for tifftcl extensions so that they don't have to figure this all
# out for themselves.  This file does not duplicate information
# already provided by tclConfig.sh, so you may need to use that
# file in addition to this one.
#
# The information in this file is specific to a single platform.

# tifftcl's version number.
tifftcl_VERSION='1.0'
tifftcl_MAJOR_VERSION='1'
tifftcl_MINOR_VERSION='0'
tifftcl_RELEASE_LEVEL=''

# The name of the tifftcl library (may be either a .a file or a shared library):
tifftcl_LIB_FILE=libtifftcl1.0.dylib

# String to pass to linker to pick up the tifftcl library from its
# build directory.
tifftcl_BUILD_LIB_SPEC='-L/var/tmp/tcl_ext/tcl_ext-46~3/tcl_ext/tkimg/libtiff/tcl -ltifftcl1.0'

# String to pass to linker to pick up the tifftcl library from its
# installed directory.
tifftcl_LIB_SPEC='-L/System/Library/Tcl/Img1.3 -ltifftcl1.0'

# The name of the tifftcl stub library (a .a file):
tifftcl_STUB_LIB_FILE=libtifftclstub1.0.a

# String to pass to linker to pick up the tifftcl stub library from its
# build directory.
tifftcl_BUILD_STUB_LIB_SPEC='-L/var/tmp/tcl_ext/tcl_ext-46~3/tcl_ext/tkimg/libtiff/tcl -ltifftclstub1.0'

# String to pass to linker to pick up the tifftcl stub library from its
# installed directory.
tifftcl_STUB_LIB_SPEC='-L/System/Library/Tcl/Img1.3 -ltifftclstub1.0'

# String to pass to linker to pick up the tifftcl stub library from its
# build directory.
tifftcl_BUILD_STUB_LIB_PATH='/var/tmp/tcl_ext/tcl_ext-46~3/tcl_ext/tkimg/libtiff/tcl/libtifftclstub1.0.a'

# String to pass to linker to pick up the tifftcl stub library from its
# installed directory.
tifftcl_STUB_LIB_PATH='/System/Library/Tcl/Img1.3/libtifftclstub1.0.a'

# Location of the top-level source directories from which tifftcl
# was built.  This is the directory that contains generic, unix, etc.
# If tifftcl was compiled in a different place than the directory
# containing the source files, this points to the location of the
# sources, not the location where tifftcl was compiled. This can
# be relative to the build directory.

tifftcl_SRC_DIR='/BinaryCache/tcl_ext/tcl_ext-46~3/Symbols/SRC/tcl_ext/tkimg/tkimg/libtiff/tcl'
