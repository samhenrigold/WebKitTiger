/* TIGER SDK OVERLAY: <CoreText/CoreText.h>
 *
 * The umbrella. Tiger's CoreText ships no headers at all, so this framework
 * overlay is ours end to end. Read CTDefines.h first: it explains the three
 * kinds of declaration in here and why some of them carry an asm label.
 * The full survey is compat/CT-SURVEY.md. */
#ifndef __CORETEXT__
#define __CORETEXT__

#include <CoreText/CTDefines.h>
#include <CoreText/SFNTLayoutTypes.h>
#include <CoreText/CTFontTraits.h>
#include <CoreText/CTFontDescriptor.h>
#include <CoreText/CTFont.h>
#include <CoreText/CTFontCollection.h>
#include <CoreText/CTFontManager.h>
#include <CoreText/CTGlyphInfo.h>
#include <CoreText/CTParagraphStyle.h>
#include <CoreText/CTTextTab.h>
#include <CoreText/CTStringAttributes.h>
#include <CoreText/CTLine.h>
#include <CoreText/CTRun.h>
#include <CoreText/CTTypesetter.h>
#include <CoreText/CTFrame.h>
#include <CoreText/CTFramesetter.h>

#endif /* __CORETEXT__ */
