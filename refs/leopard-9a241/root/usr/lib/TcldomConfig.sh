# tcldomConfig.sh --
# 
# This shell script (for sh) is generated automatically by Tcldom's
# configure script.  It will create shell variables for most of
# the configuration options discovered by the configure script.
# This script is intended to be included by the configure scripts
# for Tcldom extensions so that they don't have to figure this all
# out for themselves.  This file does not duplicate information
# already provided by tclConfig.sh, so you may need to use that
# file in addition to this one.
#
# The information in this file is specific to a single platform.

# Tcldom's version number.
Tcldom_VERSION='2.6'
Tcldom_MAJOR_VERSION='2'
Tcldom_MINOR_VERSION='6'
Tcldom_RELEASE_LEVEL=''

# The name of the Tcldom library (may be either a .a file or a shared library):
Tcldom_LIB_FILE=libTcldom2.6.dylib

# String to pass to linker to pick up the Tcldom library from its
# build directory.
Tcldom_BUILD_LIB_SPEC='-L/var/tmp/tcl_ext/tcl_ext-46~3/tcl_ext/tclxml/tcldom -lTcldom2.6'

# String to pass to linker to pick up the Tcldom library from its
# installed directory.
Tcldom_LIB_SPEC='-L/System/Library/Tcl/Tcldom2.6 -lTcldom2.6'

# The name of the Tcldom stub library (a .a file):
Tcldom_STUB_LIB_FILE=libTcldomstub2.6.a

# String to pass to linker to pick up the Tcldom stub library from its
# build directory.
Tcldom_BUILD_STUB_LIB_SPEC='-L/var/tmp/tcl_ext/tcl_ext-46~3/tcl_ext/tclxml/tcldom -lTcldomstub2.6'

# String to pass to linker to pick up the Tcldom stub library from its
# installed directory.
Tcldom_STUB_LIB_SPEC='-L/System/Library/Tcl/Tcldom2.6 -lTcldomstub2.6'

# String to pass to linker to pick up the Tcldom stub library from its
# build directory.
Tcldom_BUILD_STUB_LIB_PATH='/var/tmp/tcl_ext/tcl_ext-46~3/tcl_ext/tclxml/tcldom/libTcldomstub2.6.a'

# String to pass to linker to pick up the Tcldom stub library from its
# installed directory.
Tcldom_STUB_LIB_PATH='/System/Library/Tcl/Tcldom2.6/libTcldomstub2.6.a'

# Location of the top-level source directories from which [incr Tcl]
# was built.  This is the directory that contains generic, unix, etc.
# If [incr Tcl] was compiled in a different place than the directory
# containing the source files, this points to the location of the sources,
# not the location where [incr Tcl] was compiled.
Tcldom_SRC_DIR='/BinaryCache/tcl_ext/tcl_ext-46~3/Symbols/SRC/tcl_ext/tclxml/tcldom'
