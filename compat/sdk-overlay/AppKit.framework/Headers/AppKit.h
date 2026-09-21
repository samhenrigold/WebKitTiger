/*	AppKit.h -- TIGER SDK OVERLAY
	The 10.4u SDK's umbrella header plus the AppKit-wide types that arrived
	after 10.4 and have nowhere else to live.

	The umbrella is the safe place to hook: every AppKit translation unit
	reaches it, and unlike a leaf header it cannot be included partway through
	the framework's own emission. */

/*
	AppKit.h
	Application Kit
	Copyright (c) 1994-2005, Apple Computer, Inc.
	All rights reserved.

	This file is included by all AppKit application source files for easy building.  Using this file is preferred over importing individual files because it will use a precompiled version.
*/

#import <Foundation/Foundation.h>
#import <AppKit/AppKitDefines.h>
#import <AppKit/AppKitErrors.h>
#import <AppKit/NSGraphicsContext.h>
#import <AppKit/NSAccessibility.h>
#import <AppKit/NSActionCell.h>
#import <AppKit/NSAlert.h>
#import <AppKit/NSAppleScriptExtensions.h>
#import <AppKit/NSApplication.h>
#import <AppKit/NSBox.h>
#import <AppKit/NSButton.h>
#import <AppKit/NSButtonCell.h>
#import <AppKit/NSCell.h>
#import <AppKit/NSClipView.h>
#import <AppKit/NSControl.h>
#import <AppKit/NSFont.h>
#import <AppKit/NSFontDescriptor.h>
#import <AppKit/NSFontManager.h>
#import <AppKit/NSFontPanel.h>
#import <AppKit/NSForm.h>
#import <AppKit/NSFormCell.h>
#import <AppKit/NSMatrix.h>
#import <AppKit/NSMenu.h>
#import <AppKit/NSMenuItem.h>
#import <AppKit/NSColor.h>
#import <AppKit/NSColorSpace.h>
#import <AppKit/NSBitmapImageRep.h>
#import <AppKit/NSBrowser.h>
#import <AppKit/NSBrowserCell.h>
#import <AppKit/NSCachedImageRep.h>
#import <AppKit/NSCIImageRep.h>
#import <AppKit/NSColorList.h>
#import <AppKit/NSColorPanel.h>
#import <AppKit/NSColorPicking.h>
#import <AppKit/NSColorPicker.h>
#import <AppKit/NSColorWell.h>
#import <AppKit/NSCursor.h>
#import <AppKit/NSCustomImageRep.h>
#import <AppKit/NSDocument.h>
#import <AppKit/NSDocumentController.h>
#import <AppKit/NSDragging.h>
#import <AppKit/NSEPSImageRep.h>
#import <AppKit/NSErrors.h>
#import <AppKit/NSEvent.h>
#import <AppKit/NSFileWrapper.h>
#import <AppKit/NSHelpManager.h>
#import <AppKit/NSGraphics.h>
#import <AppKit/NSImage.h>
#import <AppKit/NSImageCell.h>
#import <AppKit/NSImageRep.h>
#import <AppKit/NSImageView.h>
#import <AppKit/NSNib.h>
#import <AppKit/NSNibLoading.h>
#import <AppKit/NSPrinter.h>
#import <AppKit/NSSpeechRecognizer.h>
#import <AppKit/NSSpeechSynthesizer.h>
#import <AppKit/NSSpellChecker.h>
#import <AppKit/NSSplitView.h>
#import <AppKit/NSOpenPanel.h>
#import <AppKit/NSPageLayout.h>
#import <AppKit/NSPanel.h>
#import <AppKit/NSPasteboard.h>
#import <AppKit/NSPopUpButton.h>
#import <AppKit/NSPrintInfo.h>
#import <AppKit/NSPrintOperation.h>
#import <AppKit/NSPrintPanel.h>
#import <AppKit/NSResponder.h>
#import <AppKit/NSSavePanel.h>
#import <AppKit/NSScreen.h>
#import <AppKit/NSScrollView.h>
#import <AppKit/NSScroller.h>
#import <AppKit/NSSegmentedControl.h>
#import <AppKit/NSSegmentedCell.h>
#import <AppKit/NSSlider.h>
#import <AppKit/NSSliderCell.h>
#import <AppKit/NSSpellProtocol.h>
#import <AppKit/NSText.h>
#import <AppKit/NSTextField.h>
#import <AppKit/NSTextFieldCell.h>
#import <AppKit/NSText.h>
#import <AppKit/NSTokenField.h>
#import <AppKit/NSTokenFieldCell.h>
#import <AppKit/NSView.h>
#import <AppKit/NSWindow.h>
#import <AppKit/NSWindowController.h>
#import <AppKit/NSWorkspace.h>
#import <AppKit/NSComboBox.h>
#import <AppKit/NSComboBoxCell.h>
#import <AppKit/NSTableColumn.h>
#import <AppKit/NSTableHeaderCell.h>
#import <AppKit/NSTableHeaderView.h>
#import <AppKit/NSTableView.h>
#import <AppKit/NSOutlineView.h>
#import <AppKit/NSAttributedString.h>
#import <AppKit/NSLayoutManager.h>
#import <AppKit/NSParagraphStyle.h>
#import <AppKit/NSTextStorage.h>
#import <AppKit/NSTextView.h>
#import <AppKit/NSTextContainer.h>
#import <AppKit/NSTextAttachment.h>
#import <AppKit/NSInputManager.h>
#import <AppKit/NSInputServer.h>
#import <AppKit/NSStringDrawing.h>
#import <AppKit/NSRulerMarker.h>
#import <AppKit/NSRulerView.h>
#import <AppKit/NSSecureTextField.h>
#import <AppKit/NSInterfaceStyle.h>
#import <AppKit/NSNibDeclarations.h>
#import <AppKit/NSProgressIndicator.h>
#import <AppKit/NSTabView.h>
#import <AppKit/NSTabViewItem.h>
#import <AppKit/NSMenuView.h>
#import <AppKit/NSMenuItemCell.h>
#import <AppKit/NSPopUpButtonCell.h>
#import <AppKit/NSGraphicsContext.h>
#import <AppKit/NSAffineTransform.h>
#import <AppKit/NSBezierPath.h>
#import <AppKit/NSPICTImageRep.h>
#import <AppKit/NSStatusBar.h>
#import <AppKit/NSStatusItem.h>
#import <AppKit/NSSound.h>
#import <AppKit/NSMovie.h>
#import <AppKit/NSMovieView.h>
#import <AppKit/NSPDFImageRep.h>
#import <AppKit/NSQuickDrawView.h>
#import <AppKit/NSDrawer.h>
#import <AppKit/NSOpenGL.h>
#import <AppKit/NSOpenGLView.h>
#import <AppKit/NSApplicationScripting.h>
#import <AppKit/NSDocumentScripting.h>
#import <AppKit/NSTextStorageScripting.h>
#import <AppKit/NSToolbar.h>
#import <AppKit/NSToolbarItem.h>
#import <AppKit/NSWindowScripting.h>
#import <AppKit/NSStepper.h>
#import <AppKit/NSStepperCell.h>
#import <AppKit/NSGlyphInfo.h>
#import <AppKit/NSShadow.h>
#import <AppKit/NSATSTypesetter.h>
#import <AppKit/NSGlyphGenerator.h>
#import <AppKit/NSSearchField.h>
#import <AppKit/NSSearchFieldCell.h>
#import <AppKit/NSController.h>
#import <AppKit/NSObjectController.h>
#import <AppKit/NSArrayController.h>
#import <AppKit/NSTreeController.h>
#import <AppKit/NSUserDefaultsController.h>
#import <AppKit/NSKeyValueBinding.h>
#import <AppKit/NSTextList.h>
#import <AppKit/NSTextTable.h>
#import <AppKit/NSDatePickerCell.h>
#import <AppKit/NSDatePicker.h>
#import <AppKit/NSLevelIndicatorCell.h>
#import <AppKit/NSLevelIndicator.h>
#import <AppKit/NSAnimation.h>
#import <AppKit/NSPersistentDocument.h>

/* ===== TIGER: additions below this line, not SDK content ================ */



/* 10.6. Tiger's AppKit has no notion of a right-to-left interface; the type
   exists so that PopupMenu.mm, ScrollbarThemeMac.mm and WebView.mm can name it.
   The accessors that return one need implementations and are in
   <TigerCompat/AppKitCompat.h>. */
#if !defined(TIGER_NSUSERINTERFACELAYOUTDIRECTION_DEFINED)
#define TIGER_NSUSERINTERFACELAYOUTDIRECTION_DEFINED 1
enum {
    NSUserInterfaceLayoutDirectionLeftToRight = 0,
    NSUserInterfaceLayoutDirectionRightToLeft = 1
};
/* int, not NSInteger: this header is reached from translation units that have
   not seen <TigerCompat/NSCompat.h>, which is where NSInteger comes from on
   Tiger. On i386 NSInteger is int, so the two spellings are the same type. */
typedef int NSUserInterfaceLayoutDirection;
#endif

/* The AppKit that needs an IMPLEMENTATION rather than a rename lives in
   <TigerCompat/AppKitCompat.h> (compat/nscompat-appkit.m): the NSEvent 10.7-10.10
   accessors, -[NSGraphicsContext CGContext], the NSColor semantic colours, the
   NSWorkspace accessibility switches. It is imported here, at the very end of
   the umbrella, rather than file by file at ~40 call sites -- by this point
   every AppKit class it extends is declared, and its own `#import <AppKit/AppKit.h>`
   is a no-op because this file's include guard is already set.

   Placed BEFORE the pure renames below on purpose: those are macros, and a
   macro named NSBezelStyleRounded must not be live while AppKitCompat.h's own
   declarations are parsed. */
#import <TigerCompat/AppKitCompat.h>

/* -------------------------------------------------------------------------
 * The 10.12 "Swiftification" enum renames, and two later additions.
 *
 * Every one of these is the SAME VALUE under a new spelling -- the values are
 * the 10.4u SDK's own enumerators, not constants repeated by hand -- so this is
 * a rename table and nothing more. They matter because they are what the Aqua
 * control path (ControlFactoryMac.mm, ButtonMac.mm, ColorWellMac.mm,
 * ControlMac.mm) uses to configure the real NSCells it draws with, which is the
 * DrawControlPart remoting this port depends on.
 *
 * Casted macros rather than a second enum: in C++ an unnamed enum's constants
 * are a DISTINCT type, so `[cell setBezelStyle:NSBezelStyleRounded]` would not
 * compile. Every value here is inside its target enum's existing range, so the
 * cast is also a constant expression and works as a `case` label -- unlike
 * kCGInterpolationMedium, which had to be widened at the declaration instead.
 * ------------------------------------------------------------------------- */

/* NSBezelStyle (NSButtonCell.h). */
#define NSBezelStyleRounded           ((NSBezelStyle)NSRoundedBezelStyle)
#define NSBezelStyleRegularSquare     ((NSBezelStyle)NSRegularSquareBezelStyle)
#define NSBezelStyleDisclosure        ((NSBezelStyle)NSDisclosureBezelStyle)
#define NSBezelStyleShadowlessSquare  ((NSBezelStyle)NSShadowlessSquareBezelStyle)
#define NSBezelStyleCircular          ((NSBezelStyle)NSCircularBezelStyle)
#define NSBezelStyleTexturedSquare    ((NSBezelStyle)NSTexturedSquareBezelStyle)
#define NSBezelStyleHelpButton        ((NSBezelStyle)NSHelpButtonBezelStyle)
#define NSBezelStyleSmallSquare       ((NSBezelStyle)NSSmallSquareBezelStyle)
#define NSBezelStyleTexturedRounded   ((NSBezelStyle)NSTexturedRoundedBezelStyle)
#define NSBezelStyleRoundRect         ((NSBezelStyle)NSRoundRectBezelStyle)
#define NSBezelStyleRecessed          ((NSBezelStyle)NSRecessedBezelStyle)
#define NSBezelStyleRoundedDisclosure ((NSBezelStyle)NSRoundedDisclosureBezelStyle)

/* NSButtonType (NSButtonCell.h). */
#define NSButtonTypeMomentaryLight  ((NSButtonType)NSMomentaryLightButton)
#define NSButtonTypePushOnPushOff   ((NSButtonType)NSPushOnPushOffButton)
#define NSButtonTypeToggle          ((NSButtonType)NSToggleButton)
#define NSButtonTypeSwitch          ((NSButtonType)NSSwitchButton)
#define NSButtonTypeRadio           ((NSButtonType)NSRadioButton)
#define NSButtonTypeMomentaryChange ((NSButtonType)NSMomentaryChangeButton)
#define NSButtonTypeOnOff           ((NSButtonType)NSOnOffButton)
#define NSButtonTypeMomentaryPushIn ((NSButtonType)NSMomentaryPushInButton)

/* NSControlStateValue (NSCell.h). int, not NSInteger: this header is reached
   before <TigerCompat/NSCompat.h>, and on i386 the two are the same type. */
typedef int NSControlStateValue;
#define NSControlStateValueMixed ((NSControlStateValue)NSMixedState)
#define NSControlStateValueOff   ((NSControlStateValue)NSOffState)
#define NSControlStateValueOn    ((NSControlStateValue)NSOnState)

/* NSSliderType (NSSliderCell.h) and NSLevelIndicatorStyle
   (NSLevelIndicatorCell.h). */
#define NSSliderTypeLinear   ((NSSliderType)NSLinearSlider)
#define NSSliderTypeCircular ((NSSliderType)NSCircularSlider)
#define NSLevelIndicatorStyleRelevancy          ((NSLevelIndicatorStyle)NSRelevancyLevelIndicatorStyle)
#define NSLevelIndicatorStyleContinuousCapacity ((NSLevelIndicatorStyle)NSContinuousCapacityLevelIndicatorStyle)
#define NSLevelIndicatorStyleDiscreteCapacity   ((NSLevelIndicatorStyle)NSDiscreteCapacityLevelIndicatorStyle)
#define NSLevelIndicatorStyleRating             ((NSLevelIndicatorStyle)NSRatingLevelIndicatorStyle)

/* NSCompositingOperation, 10.12 renames (NSGraphics.h). */
#define NSCompositingOperationClear           ((NSCompositingOperation)NSCompositeClear)
#define NSCompositingOperationCopy            ((NSCompositingOperation)NSCompositeCopy)
#define NSCompositingOperationSourceOver      ((NSCompositingOperation)NSCompositeSourceOver)
#define NSCompositingOperationSourceIn        ((NSCompositingOperation)NSCompositeSourceIn)
#define NSCompositingOperationSourceOut       ((NSCompositingOperation)NSCompositeSourceOut)
#define NSCompositingOperationSourceAtop      ((NSCompositingOperation)NSCompositeSourceAtop)
#define NSCompositingOperationDestinationOver ((NSCompositingOperation)NSCompositeDestinationOver)
#define NSCompositingOperationDestinationIn   ((NSCompositingOperation)NSCompositeDestinationIn)
#define NSCompositingOperationDestinationOut  ((NSCompositingOperation)NSCompositeDestinationOut)
#define NSCompositingOperationDestinationAtop ((NSCompositingOperation)NSCompositeDestinationAtop)
#define NSCompositingOperationXOR             ((NSCompositingOperation)NSCompositeXOR)
#define NSCompositingOperationPlusDarker      ((NSCompositingOperation)NSCompositePlusDarker)
#define NSCompositingOperationPlusLighter     ((NSCompositingOperation)NSCompositePlusLighter)

/* NSSizeFromCGSize and friends, 10.5. NSSize and CGSize are layout-identical on
   i386 (two floats), which is why Apple could make these inline in the first
   place. */
static inline NSSize NSSizeFromCGSize(CGSize size) { NSSize s; s.width = size.width; s.height = size.height; return s; }
static inline CGSize NSSizeToCGSize(NSSize size) { CGSize s; s.width = size.width; s.height = size.height; return s; }
static inline NSPoint NSPointFromCGPoint(CGPoint p) { NSPoint q; q.x = p.x; q.y = p.y; return q; }
static inline CGPoint NSPointToCGPoint(NSPoint p) { CGPoint q; q.x = p.x; q.y = p.y; return q; }
static inline NSRect NSRectFromCGRect(CGRect r) { NSRect s; s.origin = NSPointFromCGPoint(r.origin); s.size = NSSizeFromCGSize(r.size); return s; }
static inline CGRect NSRectToCGRect(NSRect r) { CGRect s; s.origin = NSPointToCGPoint(r.origin); s.size = NSSizeToCGSize(r.size); return s; }

/* NSImageHintCTM, 10.6. A key for the hints dictionary of the 10.6
   -drawInRect:fromRect:operation:fraction:respectFlipped:hints:, which Tiger's
   NSImage does not have -- so nothing on this system ever reads it. A macro
   rather than an extern NSString so that no call site needs to link anything
   extra to build a dictionary that is then ignored. The string is Apple's.

   NSScrollerStyle is deliberately NOT here: WebCore's own
   PAL/pal/spi/mac/NSScrollerImpDetails.h declares it, and a second typedef is a
   conflict rather than a shim. */
#ifndef NSImageHintCTM
#define NSImageHintCTM ((NSString *)@"NSImageHintCTM")
#endif
