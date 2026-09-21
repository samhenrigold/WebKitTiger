/* TIGER SDK OVERLAY: Carbon's <AssertMacros.h> defines lowercase macros named
   check, verify, require and their variants. They collide with ordinary C++
   member names -- JavaScriptCore's Fits.h has `static bool check(T)`, and it is
   far from the only one -- and the header reaches WebKit through any include of
   ApplicationServices, which is how <TigerCompat/CGCompat.h> pulls it in.

   Later Apple SDKs added __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES to
   turn the unprefixed spellings off. The 10.4 header predates it, so the real
   header is included and the unprefixed names are undefined afterwards. The
   __Check / __Verify / __Require forms, and everything else the header defines,
   are untouched.

   toolchain/tiger.cmake passes
   -D__ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES=0. */
#ifndef __TIGER_ASSERTMACROS_H__
#define __TIGER_ASSERTMACROS_H__

#include_next <AssertMacros.h>

#if defined(__ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES) && !__ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES

#undef check
#undef check_noerr
#undef check_noerr_string
#undef check_string
#undef debug_string
#undef ncheck
#undef ncheck_string
#undef nrequire
#undef nrequire_action
#undef nrequire_action_quiet
#undef nrequire_action_string
#undef nrequire_quiet
#undef nrequire_string
#undef nverify
#undef nverify_string
#undef require
#undef require_action
#undef require_action_quiet
#undef require_action_string
#undef require_noerr
#undef require_noerr_action
#undef require_noerr_action_quiet
#undef require_noerr_action_string
#undef require_noerr_quiet
#undef require_noerr_string
#undef require_quiet
#undef require_string
#undef verify
#undef verify_action
#undef verify_noerr
#undef verify_noerr_string
#undef verify_string

#endif

#endif /* __TIGER_ASSERTMACROS_H__ */
