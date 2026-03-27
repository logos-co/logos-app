#pragma once

// macOS-specific helpers for logos_ui_host.
// Implemented in macos_helpers.mm (Objective-C++) on Apple, stub on others.

/// Mark this process as an "accessory" application:
/// - No Dock icon
/// - No entry in Cmd-Tab app switcher
/// Should be called before showing any windows.
void macosConfigureAccessoryApp();
