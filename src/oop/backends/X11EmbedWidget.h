#pragma once

#include "../OopEmbedWidget.h"

#if defined(Q_OS_LINUX)

class QWindow;

// Linux/X11 backend: embeds a foreign X11 window inside a QWidget using
// QWindow::fromWinId() + QWidget::createWindowContainer().
// Sets _NET_WM_STATE_SKIP_TASKBAR so the plugin window never appears in the
// desktop taskbar or Alt-Tab switcher.
class X11EmbedWidget : public OopEmbedWidget {
    Q_OBJECT

public:
    explicit X11EmbedWidget(EmbedControlChannel* channel, QWidget* parent = nullptr);
    ~X11EmbedWidget() override;

    bool embedSurface(const QJsonObject& handleData) override;

private:
    QWindow* m_foreignWindow = nullptr;
    QWidget* m_containerWidget = nullptr;
};

#else

// Stub for non-Linux builds
class X11EmbedWidget : public OopEmbedWidget {
public:
    explicit X11EmbedWidget(EmbedControlChannel* channel, QWidget* parent = nullptr)
        : OopEmbedWidget(channel, parent) {}
    bool embedSurface(const QJsonObject&) override { return false; }
};

#endif
