/* TIGER SDK OVERLAY: <CoreText/CTTextTab.h> — see CTDefines.h. */
#ifndef __CTTEXTTAB__
#define __CTTEXTTAB__
#include <CoreText/CTDefines.h>
#include <CoreText/CTParagraphStyle.h>
typedef const struct __CTTextTab* CTTextTabRef;
CT_EXTERN const CFStringRef kCTTabColumnTerminatorsAttributeName;
CT_EXTERN CFTypeID CTTextTabGetTypeID(void);
/* Tiger reads the location as a by-value double, as the modern header says. */
CT_EXTERN CTTextTabRef CTTextTabCreate(CTTextAlignment, double location, CFDictionaryRef options);
CT_EXTERN double CTTextTabGetLocation(CTTextTabRef);
CT_EXTERN CTTextAlignment CTTextTabGetAlignment(CTTextTabRef);
CT_EXTERN CFDictionaryRef CTTextTabGetOptions(CTTextTabRef);
#endif /* __CTTEXTTAB__ */
