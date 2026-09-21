# itclConfig.sh --
# 
# This shell script (for sh) is generated automatically by Itcl's
# configure script.  It will create shell variables for most of
# the configuration options discovered by the configure script.
# This script is intended to be included by the configure scripts
# for Itcl extensions so that they don't have to figure this all
# out for themselves.  This file does not duplicate information
# already provided by tclConfig.sh, so you may need to use that
# file in addition to this one.
#
# The information in this file is specific to a single platform.

# Itcl's version number.
itcl_VERSION='3.3'
ITCL_VERSION='3.3'
itcl_MAJOR_VERSION='3'
ITCL_MAJOR_VERSION='3'
itcl_MINOR_VERSION='3'
ITCL_MINOR_VERSION='3'
itcl_RELEASE_LEVEL='.0'
ITCL_RELEASE_LEVEL='.0'

# The name of the Itcl library (may be either a .a file or a shared library):
itcl_LIB_FILE=libitcl3.3.dylib
ITCL_LIB_FILE=libitcl3.3.dylib

# String to pass to linker to pick up the Itcl library from its
# build directory.
itcl_BUILD_LIB_SPEC='-L/var/tmp/tcl_ext/tcl_ext-46~3/tcl_ext/incrtcl/itcl -litcl3.3'
ITCL_BUILD_LIB_SPEC='-L/var/tmp/tcl_ext/tcl_ext-46~3/tcl_ext/incrtcl/itcl -litcl3.3'

# String to pass to linker to pick up the Itcl library from its
# installed directory.
itcl_LIB_SPEC='-L/System/Library/Tcl/itcl3.3 -litcl3.3'
ITCL_LIB_SPEC='-L/System/Library/Tcl/itcl3.3 -litcl3.3'

# The name of the Itcl stub library (a .a file):
itcl_STUB_LIB_FILE=libitclstub3.3.a
ITCL_STUB_LIB_FILE=libitclstub3.3.a

# String to pass to linker to pick up the Itcl stub library from its
# build directory.
itcl_BUILD_STUB_LIB_SPEC='-L/var/tmp/tcl_ext/tcl_ext-46~3/tcl_ext/incrtcl/itcl -litclstub3.3'
ITCL_BUILD_STUB_LIB_SPEC='-L/var/tmp/tcl_ext/tcl_ext-46~3/tcl_ext/incrtcl/itcl -litclstub3.3'

# String to pass to linker to pick up the Itcl stub library from its
# installed directory.
itcl_STUB_LIB_SPEC='-L/System/Library/Tcl/itcl3.3 -litclstub3.3'
ITCL_STUB_LIB_SPEC='-L/System/Library/Tcl/itcl3.3 -litclstub3.3'

# String to pass to linker to pick up the Itcl stub library from its
# build directory.
itcl_BUILD_STUB_LIB_PATH='/var/tmp/tcl_ext/tcl_ext-46~3/tcl_ext/incrtcl/itcl/libitclstub3.3.a'
ITCL_BUILD_STUB_LIB_PATH='/var/tmp/tcl_ext/tcl_ext-46~3/tcl_ext/incrtcl/itcl/libitclstub3.3.a'

# String to pass to linker to pick up the Itcl stub library from its
# installed directory.
itcl_STUB_LIB_PATH='/System/Library/Tcl/itcl3.3/libitclstub3.3.a'
ITCL_STUB_LIB_PATH='/System/Library/Tcl/itcl3.3/libitclstub3.3.a'

# Location of the top-level source directories from which [incr Tcl]
# was built.  This is the directory that contains generic, unix, etc.
# If [incr Tcl] was compiled in a different place than the directory
# containing the source files, this points to the location of the sources,
# not the location where [incr Tcl] was compiled.
itcl_SRC_DIR='/BinaryCache/tcl_ext/tcl_ext-46~3/Symbols/SRC/tcl_ext/incrtcl/incrTcl/itcl'
ITCL_SRC_DIR='/BinaryCache/tcl_ext/tcl_ext-46~3/Symbols/SRC/tcl_ext/incrtcl/incrTcl/itcl'
