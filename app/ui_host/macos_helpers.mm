#include "macos_helpers.h"
#import <AppKit/AppKit.h>

void macosConfigureAccessoryApp()
{
    // NSApplicationActivationPolicyAccessory:
    //   - Process does not appear in the Dock
    //   - Process does not appear in Cmd-Tab (app switcher)
    //   - Windows can still be visible and interactive
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
}
