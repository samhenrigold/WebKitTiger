# tcldomConfig.sh --
# 
# This shell script (for sh) is generated automatically by tcldomxml's
# configure script.  It will create shell variables for most of
# the configuration options discovered by the configure script.
# This script is intended to be included by the configure scripts
# for tcldomxml extensions so that they don't have to figure this all
# out for themselves.  This file does not duplicate information
# already provided by tclConfig.sh, so you may need to use that
# file in addition to this one.
#
# The information in this file is specific to a single platform.

# tcldomxml's version number.
tcldomxml_VERSION='2.6'
tcldomxml_MAJOR_VERSION='2'
tcldomxml_MINOR_VERSION='6'
tcldomxml_RELEASE_LEVEL=''

# The name of the tcldomxml library (may be either a .a file or a shared library):
tcldomxml_LIB_FILE=libtcldomxml2.6.dylib

# String to pass to linker to pick up the tcldomxml library from its
# build directory.
tcldomxml_BUILD_LIB_SPEC='-L/var/tmp/tcl_ext/tcl_ext-46~3/tcl_ext/tclxml/tcldom/src-libxml2 -ltcldomxml2.6'

# String to pass to linker to pick up the tcldomxml library from its
# installed directory.
tcldomxml_LIB_SPEC='-L/System/Library/Tcl/tcldomxml2.6 -ltcldomxml2.6'

# The name of the tcldomxml stub library (a .a file):
tcldomxml_STUB_LIB_FILE=libtcldomxmlstub2.6.a

# String to pass to linker to pick up the tcldomxml stub library from its
# build directory.
tcldomxml_BUILD_STUB_LIB_SPEC='-L/var/tmp/tcl_ext/tcl_ext-46~3/tcl_ext/tclxml/tcldom/src-libxml2 -ltcldomxmlstub2.6'

# String to pass to linker to pick up the tcldomxml stub library from its
# installed directory.
tcldomxml_STUB_LIB_SPEC='-L/System/Library/Tcl/tcldomxml2.6 -ltcldomxmlstub2.6'

# String to pass to linker to pick up the tcldomxml stub library from its
# build directory.
tcldomxml_BUILD_STUB_LIB_PATH='/var/tmp/tcl_ext/tcl_ext-46~3/tcl_ext/tclxml/tcldom/src-libxml2/libtcldomxmlstub2.6.a'

# String to pass to linker to pick up the tcldomxml stub library from its
# installed directory.
tcldomxml_STUB_LIB_PATH='/System/Library/Tcl/tcldomxml2.6/libtcldomxmlstub2.6.a'

# Location of the top-level source directories from which [incr Tcl]
# was built.  This is the directory that contains generic, unix, etc.
# If [incr Tcl] was compiled in a different place than the directory
# containing the source files, this points to the location of the sources,
# not the location where [incr Tcl] was compiled.
tcldomxml_SRC_DIR='/BinaryCache/tcl_ext/tcl_ext-46~3/Symbols/SRC/tcl_ext/tclxml/tcldom/src-libxml2'
