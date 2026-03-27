#pragma once

#include <QString>
#include <QJsonObject>

// Hosting modes for UI plugins.
// InProcess = legacy path (QPluginLoader / QQuickWidget in the main process).
// Other modes launch the plugin in a separate logos_ui_host process and embed
// the rendered surface via a platform-specific mechanism.
enum class HostingMode {
    InProcess,
    Wayland,
    X11,
    MacNative,
    WinNative
};

// Resolves the hosting mode for a given UI plugin based on:
//   1. The plugin's manifest/metadata ("hosting" field)
//   2. The LOGOS_UI_HOSTING_MODE environment variable
//   3. Runtime platform detection
class HostingModeResolver {
public:
    // Main entry point — returns the mode that should be used.
    static HostingMode resolve(const QString& pluginName, const QJsonObject& manifest);

    // Platform probes
    static bool isWaylandAvailable();
    static bool isX11Available();

    // Returns the best OOP mode for the current platform, or InProcess if none.
    static HostingMode platformDefault();

    // Human-readable name (for logging / debug)
    static const char* modeName(HostingMode mode);
};
