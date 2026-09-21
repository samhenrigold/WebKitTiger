# tkimgConfig.sh --
# 
# This shell script (for sh) is generated automatically by tkimg's
# configure script.  It will create shell variables for most of
# the configuration options discovered by the configure script.
# This script is intended to be included by the configure scripts
# for tkimg extensions so that they don't have to figure this all
# out for themselves.  This file does not duplicate information
# already provided by tclConfig.sh, so you may need to use that
# file in addition to this one.
#
# The information in this file is specific to a single platform.

# tkimg's version number.
tkimg_VERSION='1.3'
tkimg_MAJOR_VERSION='1'
tkimg_MINOR_VERSION='3'
tkimg_RELEASE_LEVEL=''

# The name of the tkimg library (may be either a .a file or a shared library):
tkimg_LIB_FILE=libtkimg1.3.dylib

# String to pass to linker to pick up the tkimg library from its
# build directory.
tkimg_BUILD_LIB_SPEC='-L/var/tmp/tcl_ext/tcl_ext-46~3/tcl_ext/tkimg/base -ltkimg1.3'

# String to pass to linker to pick up the tkimg library from its
# installed directory.
tkimg_LIB_SPEC='-L/System/Library/Tcl/Img1.3 -ltkimg1.3'

# The name of the tkimg stub library (a .a file):
tkimg_STUB_LIB_FILE=libtkimgstub1.3.a

# String to pass to linker to pick up the tkimg stub library from its
# build directory.
tkimg_BUILD_STUB_LIB_SPEC='-L/var/tmp/tcl_ext/tcl_ext-46~3/tcl_ext/tkimg/base -ltkimgstub1.3'

# String to pass to linker to pick up the tkimg stub library from its
# installed directory.
tkimg_STUB_LIB_SPEC='-L/System/Library/Tcl/Img1.3 -ltkimgstub1.3'

# String to pass to linker to pick up the tkimg stub library from its
# build directory.
tkimg_BUILD_STUB_LIB_PATH='/var/tmp/tcl_ext/tcl_ext-46~3/tcl_ext/tkimg/base/libtkimgstub1.3.a'

# String to pass to linker to pick up the tkimg stub library from its
# installed directory.
tkimg_STUB_LIB_PATH='/System/Library/Tcl/Img1.3/libtkimgstub1.3.a'

# Location of the top-level source directories from which tkimg
# was built.  This is the directory that contains generic, unix, etc.
# If tkimg was compiled in a different place than the directory
# containing the source files, this points to the location of the
# sources, not the location where tkimg was compiled. This can
# be relative to the build directory.

tkimg_SRC_DIR='/BinaryCache/tcl_ext/tcl_ext-46~3/Symbols/SRC/tcl_ext/tkimg/tkimg/base'
