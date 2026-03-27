#include "HostingModeResolver.h"
#include <QProcessEnvironment>
#include <QDebug>

HostingMode HostingModeResolver::resolve(const QString& pluginName, const QJsonObject& manifest)
{
    // 1. Check manifest "hosting" field
    QString hosting = manifest.value("hosting").toString();
    if (hosting == QLatin1String("in-process") || hosting == QLatin1String("inprocess"))
        return HostingMode::InProcess;

    // 2. Check environment override
    QString envMode = qEnvironmentVariable("LOGOS_UI_HOSTING_MODE");
    if (!envMode.isEmpty()) {
        if (envMode == QLatin1String("in-process") || envMode == QLatin1String("inprocess"))
            return HostingMode::InProcess;
        if (envMode == QLatin1String("wayland"))
            return isWaylandAvailable() ? HostingMode::Wayland : HostingMode::InProcess;
        if (envMode == QLatin1String("x11"))
            return isX11Available() ? HostingMode::X11 : HostingMode::InProcess;
        if (envMode == QLatin1String("native"))
            return platformDefault();
        if (envMode == QLatin1String("oop") || envMode == QLatin1String("auto")) {
            HostingMode mode = platformDefault();
            qDebug() << "OOP UI hosting for" << pluginName << "resolved to" << modeName(mode);
            return mode;
        }
    }

    // 3. Manifest says "oop" — resolve to best platform mode
    if (hosting == QLatin1String("oop") || hosting == QLatin1String("auto")) {
        HostingMode mode = platformDefault();
        qDebug() << "OOP UI hosting for" << pluginName << "resolved to" << modeName(mode);
        return mode;
    }

    // Default: in-process (backward compatible)
    return HostingMode::InProcess;
}

bool HostingModeResolver::isWaylandAvailable()
{
#if defined(Q_OS_LINUX) && defined(HAS_WAYLAND_COMPOSITOR)
    return !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
#else
    return false;
#endif
}

bool HostingModeResolver::isX11Available()
{
#if defined(Q_OS_LINUX)
    return !qEnvironmentVariableIsEmpty("DISPLAY");
#else
    return false;
#endif
}

HostingMode HostingModeResolver::platformDefault()
{
#if defined(Q_OS_LINUX)
    if (isWaylandAvailable())
        return HostingMode::Wayland;
    if (isX11Available())
        return HostingMode::X11;
    return HostingMode::InProcess;
#elif defined(Q_OS_MACOS)
    return HostingMode::MacNative;
#elif defined(Q_OS_WIN)
    return HostingMode::WinNative;
#else
    return HostingMode::InProcess;
#endif
}

const char* HostingModeResolver::modeName(HostingMode mode)
{
    switch (mode) {
    case HostingMode::InProcess:  return "InProcess";
    case HostingMode::Wayland:    return "Wayland";
    case HostingMode::X11:        return "X11";
    case HostingMode::MacNative:  return "MacNative";
    case HostingMode::WinNative:  return "WinNative";
    }
    return "Unknown";
}
