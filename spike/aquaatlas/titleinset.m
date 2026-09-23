/* titleinset -- how much wider than its title a 10.4 push button and pop-up are.
 *
 * The port hosts live NSControls over the page, so AppKit draws the title and the page only
 * reserves the box. If the box is the width of the web text plus a guess, AppKit truncates
 * ("Butto"). This prints the real number: cellSize.width minus the width of the title in the
 * font the cell draws it in, per control size, which is exactly the horizontal padding
 * RenderThemeTiger has to put on the element.
 *
 * Plain Tiger-era Cocoa, MRR; no libtigercompat on purpose.
 */

#import <Cocoa/Cocoa.h>
#include <stdio.h>

static const NSControlSize kSizes[3] = { NSRegularControlSize, NSSmallControlSize, NSMiniControlSize };
static const char* kNames[3] = { "regular", "small", "mini" };

static float titleWidth(NSString* title, NSFont* font)
{
    NSDictionary* attributes = [NSDictionary dictionaryWithObject:font forKey:NSFontAttributeName];
    return [title sizeWithAttributes:attributes].width;
}

int main(int argc, const char* argv[])
{
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    [NSApplication sharedApplication];

    /* Several titles: the inset must not depend on the string, and if it does the widest
     * difference is the one to use. */
    NSArray* titles = [NSArray arrayWithObjects:@"OK", @"Button", @"Popup button", @"A much longer button title", nil];

    for (int s = 0; s < 3; ++s) {
        NSFont* font = [NSFont systemFontOfSize:[NSFont systemFontSizeForControlSize:kSizes[s]]];
        printf("%-8s systemFontSize %.2f\n", kNames[s], (double)[font pointSize]);

        for (unsigned t = 0; t < [titles count]; ++t) {
            NSString* title = [titles objectAtIndex:t];
            float text = titleWidth(title, font);

            NSButtonCell* button = [[NSButtonCell alloc] init];
            [button setButtonType:NSMomentaryPushInButton];
            [button setBezelStyle:NSRoundedBezelStyle];
            [button setControlSize:kSizes[s]];
            [button setFont:font];
            [button setTitle:title];
            NSSize buttonSize = [button cellSize];

            NSPopUpButtonCell* popUp = [[NSPopUpButtonCell alloc] initTextCell:@"" pullsDown:NO];
            [popUp setControlSize:kSizes[s]];
            [popUp setFont:font];
            [popUp addItemWithTitle:title];
            [popUp selectItemAtIndex:0];
            NSSize popUpSize = [popUp cellSize];

            printf("  %-28s text %6.2f  button %6.2f (+%5.2f) h %5.2f  popup %6.2f (+%5.2f) h %5.2f\n",
                [title UTF8String], (double)text,
                (double)buttonSize.width, (double)(buttonSize.width - text), (double)buttonSize.height,
                (double)popUpSize.width, (double)(popUpSize.width - text), (double)popUpSize.height);

            [button release];
            [popUp release];
        }
    }

    [pool release];
    return 0;
}
