# pngtclConfig.sh --
# 
# This shell script (for sh) is generated automatically by pngtcl's
# configure script.  It will create shell variables for most of
# the configuration options discovered by the configure script.
# This script is intended to be included by the configure scripts
# for pngtcl extensions so that they don't have to figure this all
# out for themselves.  This file does not duplicate information
# already provided by tclConfig.sh, so you may need to use that
# file in addition to this one.
#
# The information in this file is specific to a single platform.

# pngtcl's version number.
pngtcl_VERSION='1.0'
pngtcl_MAJOR_VERSION='1'
pngtcl_MINOR_VERSION='0'
pngtcl_RELEASE_LEVEL=''

# The name of the pngtcl library (may be either a .a file or a shared library):
pngtcl_LIB_FILE=libpngtcl1.0.dylib

# String to pass to linker to pick up the pngtcl library from its
# build directory.
pngtcl_BUILD_LIB_SPEC='-L/var/tmp/tcl_ext/tcl_ext-46~3/tcl_ext/tkimg/libpng/tcl -lpngtcl1.0'

# String to pass to linker to pick up the pngtcl library from its
# installed directory.
pngtcl_LIB_SPEC='-L/System/Library/Tcl/Img1.3 -lpngtcl1.0'

# The name of the pngtcl stub library (a .a file):
pngtcl_STUB_LIB_FILE=libpngtclstub1.0.a

# String to pass to linker to pick up the pngtcl stub library from its
# build directory.
pngtcl_BUILD_STUB_LIB_SPEC='-L/var/tmp/tcl_ext/tcl_ext-46~3/tcl_ext/tkimg/libpng/tcl -lpngtclstub1.0'

# String to pass to linker to pick up the pngtcl stub library from its
# installed directory.
pngtcl_STUB_LIB_SPEC='-L/System/Library/Tcl/Img1.3 -lpngtclstub1.0'

# String to pass to linker to pick up the pngtcl stub library from its
# build directory.
pngtcl_BUILD_STUB_LIB_PATH='/var/tmp/tcl_ext/tcl_ext-46~3/tcl_ext/tkimg/libpng/tcl/libpngtclstub1.0.a'

# String to pass to linker to pick up the pngtcl stub library from its
# installed directory.
pngtcl_STUB_LIB_PATH='/System/Library/Tcl/Img1.3/libpngtclstub1.0.a'

# Location of the top-level source directories from which [incr Tcl]
# was built.  This is the directory that contains generic, unix, etc.
# If [incr Tcl] was compiled in a different place than the directory
# containing the source files, this points to the location of the sources,
# not the location where [incr Tcl] was compiled.
pngtcl_SRC_DIR='/BinaryCache/tcl_ext/tcl_ext-46~3/Symbols/SRC/tcl_ext/tkimg/tkimg/libpng/tcl'
