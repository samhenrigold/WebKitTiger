/* TIGER SDK OVERLAY: <CoreText/CTDefines.h>
 *
 * Tiger's CoreText ships no headers at all, so this whole framework overlay is
 * ours. It declares the MODERN public CoreText API, spelled the way WebCore
 * calls it, but bound to what the 10.4.11 binary actually does. The three cases
 * and the disassembly behind them are written up in compat/CT-SURVEY.md:
 *
 *   1. Exports whose ABI already matches      -> declared normally.
 *   2. Exports taking a by-value double       -> declared with `double`, which
 *      is what the binary reads off the stack. C converts the caller's CGFloat
 *      at the call site, so WebCore's source is unchanged.
 *   3. Exports that share a name with the modern API but take different
 *      arguments or do nothing at all, and names Tiger lacks entirely
 *      -> declared with the modern prototype and an asm label pointing at the
 *      adapter or shim in libtigercompat (compat/ctcompat.c).
 *
 * The asm label is why WebCore needs no edits: it writes CTFontCopyTable and
 * the call lands on _TigerCTFontCopyTable.
 */

#ifndef __CTDEFINES__
#define __CTDEFINES__

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>

/* CGFloat is 10.5+. Same guard CGCompat.h uses, so both may be included. */
#ifndef CGFLOAT_DEFINED
#include <float.h>
typedef float CGFloat;
#define CGFLOAT_DEFINED 1
#define CGFLOAT_IS_DOUBLE 0
#define CGFLOAT_MIN FLT_MIN
#define CGFLOAT_MAX FLT_MAX
#endif

/* CFError is 10.5+ and Tiger's CoreFoundation has neither the type nor the API.
 * CTFontManagerRegisterFontsForURL takes one and only ever writes NULL. */
#ifndef __COREFOUNDATION_CFERROR__
typedef struct __CFError* CFErrorRef;
#endif

#if defined(__cplusplus)
#define CT_EXTERN extern "C"
#else
#define CT_EXTERN extern
#endif

/* Binds a declaration to a symbol in libtigercompat rather than to CoreText. */
#define CT_TIGER_ADAPTER(sym) __asm__("_" #sym)

#endif /* __CTDEFINES__ */
