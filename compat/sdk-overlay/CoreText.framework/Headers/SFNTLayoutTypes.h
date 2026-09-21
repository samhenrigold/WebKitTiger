/* TIGER SDK OVERLAY: <CoreText/SFNTLayoutTypes.h>
 *
 * Modern SDKs re-export this from CoreText; on Tiger it only ever lived in ATS,
 * and the 10.4u SDK's copy is complete for its era, so forward to it and add
 * what came later.
 *
 * Note for anyone editing the SDK's copy: it has classic Mac CR line endings and
 * a non-UTF-8 byte in the copyright line, so plain grep and tr both report it as
 * empty or binary. `LC_ALL=C grep -a` reads it.
 */
#ifndef __CORETEXT_SFNTLAYOUTTYPES__
#define __CORETEXT_SFNTLAYOUTTYPES__

#include <ATS/SFNTLayoutTypes.h>

/* AAT case features, as WebCore's font-variant-caps code spells them.
 *
 * Tiger predates the split. Its header has only `kLetterCaseType` (3), the
 * single feature type Apple later divided into separate lower-case and
 * upper-case types, with `kSmallCapsSelector` (3) and
 * `kInitialCapsAndSmallCapsSelector` (5) among its selectors. Those are
 * deprecated in the modern header but still declared, so both spellings are
 * available here and a font declaring either can be matched.
 *
 * Values taken from the Xcode 27 SDK's CoreText/SFNTLayoutTypes.h, not from
 * memory: these are `feat` table identifiers, so a wrong number silently
 * selects a different feature rather than failing to build. Whether Tiger's
 * shaper honours the split types at all is a separate question, answered in
 * compat/CT-SURVEY.md. */
enum {
    kLowerCaseType                  = 37,
    kUpperCaseType                  = 38
};

enum {
    kDefaultLowerCaseSelector       = 0,
    kLowerCaseSmallCapsSelector     = 1,
    kLowerCasePetiteCapsSelector    = 2
};

enum {
    kDefaultUpperCaseSelector       = 0,
    kUpperCaseSmallCapsSelector     = 1,
    kUpperCasePetiteCapsSelector    = 2
};

#endif /* __CORETEXT_SFNTLAYOUTTYPES__ */
