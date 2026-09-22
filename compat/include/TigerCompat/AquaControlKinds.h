/* TigerCompat/AquaControlKinds.h -- the control kind and state enumerations, alone.
 *
 * Split out of AquaControls.h because the x86_64 web process needs the same
 * enumerations to describe a control it cannot draw (there is no CoreGraphics and
 * no AppKit in a 64-bit process on 10.4), and AquaControls.h's drawing entry points
 * are declared in terms of CGContextRef and CGRect. This header includes nothing.
 */

#ifndef TIGERCOMPAT_AQUACONTROLKINDS_H
#define TIGERCOMPAT_AQUACONTROLKINDS_H

/* Mirrors the StyleAppearance cases that have a Mac control class. */
typedef enum {
    TigerControlButton = 0,
    TigerControlDefaultButton,
    TigerControlSquareButton,
    TigerControlCheckbox,
    TigerControlRadio,
    TigerControlMenuList,
    TigerControlMenuListButton,
    TigerControlTextField,
    TigerControlTextArea,
    TigerControlSearchField,
    TigerControlSliderTrackHorizontal,
    TigerControlSliderTrackVertical,
    TigerControlSliderThumbHorizontal,
    TigerControlSliderThumbVertical,
    TigerControlProgressBar,
    TigerControlMeter,
    TigerControlInnerSpinButton,
    TigerControlScrollbarVertical,
    TigerControlScrollbarHorizontal,
    TigerControlFocusRing,
    TigerControlKindCount
} TigerControlKind;

/* Bit-for-bit the values of WebCore::ControlStyle::State, so the content
 * process can pass its OptionSet straight through without a translation table
 * that could drift. The states with no meaning on Tiger are listed for that
 * reason and ignored. */
enum {
    TigerControlStateHovered                  = 1 << 0,
    TigerControlStatePressed                  = 1 << 1,
    TigerControlStateFocused                  = 1 << 2,
    TigerControlStateEnabled                  = 1 << 3,
    TigerControlStateChecked                  = 1 << 4,
    TigerControlStateDefault                  = 1 << 5,
    TigerControlStateWindowActive             = 1 << 6,
    TigerControlStateIndeterminate            = 1 << 7,
    TigerControlStateSpinUp                   = 1 << 8,
    TigerControlStatePresenting               = 1 << 9,
    TigerControlStateFormSemanticContext      = 1 << 10,
    TigerControlStateDarkAppearance           = 1 << 11,  /* no dark Aqua on 10.4 */
    TigerControlStateInlineFlippedWritingMode = 1 << 12,
    TigerControlStateLargeControls            = 1 << 13,  /* 10.16 size class */
    TigerControlStateReadOnly                 = 1 << 14,
    TigerControlStateListButton               = 1 << 15,
    TigerControlStateListButtonPressed        = 1 << 16,
    TigerControlStateVerticalWritingMode      = 1 << 17
};

#endif /* TIGERCOMPAT_AQUACONTROLKINDS_H */
