#pragma once

#include "../OopEmbedWidget.h"

#ifdef HAS_WAYLAND_COMPOSITOR

#include <QWaylandCompositor>
#include <QWaylandOutput>
#include <QWaylandSurface>
#include <QWaylandXdgShell>
#include <QWaylandXdgSurface>

class QQuickWidget;
class QQmlEngine;

// Linux/Wayland backend: runs a private per-plugin QWaylandCompositor.
// The plugin process connects as a Wayland client (via a dedicated socket,
// e.g. "logos_ui_counter") and its surface is rendered into a QQuickWidget
// that can be added to the MDI area.
//
// Because the plugin connects to our private compositor socket — not the
// desktop compositor — no extra window/taskbar entry is visible to the user.
class WaylandEmbedWidget : public OopEmbedWidget {
    Q_OBJECT

public:
    WaylandEmbedWidget(EmbedControlChannel* channel, const QString& socketName,
                       QWidget* parent = nullptr);
    ~WaylandEmbedWidget() override;

    bool embedSurface(const QJsonObject& handleData) override;

    // The Wayland socket name that the plugin process should connect to.
    QString socketName() const { return m_socketName; }

private slots:
    void onSurfaceCreated(QWaylandSurface* surface);
    void onXdgToplevelCreated(QWaylandXdgToplevel* toplevel, QWaylandXdgSurface* xdgSurface);

private:
    void initCompositor();

    QString m_socketName;
    QWaylandCompositor* m_compositor = nullptr;
    QWaylandOutput* m_output = nullptr;
    QWaylandXdgShell* m_xdgShell = nullptr;
    QQuickWidget* m_quickWidget = nullptr;
    QWaylandSurface* m_clientSurface = nullptr;
};

#else // !HAS_WAYLAND_COMPOSITOR

// Stub when Wayland compositor support is not compiled in.
class WaylandEmbedWidget : public OopEmbedWidget {
public:
    WaylandEmbedWidget(EmbedControlChannel* channel, const QString& socketName,
                       QWidget* parent = nullptr)
        : OopEmbedWidget(channel, parent) { Q_UNUSED(socketName); }
    bool embedSurface(const QJsonObject&) override { return false; }
    QString socketName() const { return {}; }
};

#endif // HAS_WAYLAND_COMPOSITOR
