#pragma once

#include "../OopEmbedWidget.h"

#ifdef Q_OS_WIN

// Windows backend: embeds a foreign HWND from a child process using
// SetParent() + WS_CHILD style. The child window becomes part of the
// basecamp window hierarchy and never appears in the taskbar or Alt-Tab.
class WinEmbedWidget : public OopEmbedWidget {
    Q_OBJECT

public:
    explicit WinEmbedWidget(EmbedControlChannel* channel, QWidget* parent = nullptr);
    ~WinEmbedWidget() override;

    bool embedSurface(const QJsonObject& handleData) override;

private:
    QWindow* m_foreignWindow = nullptr;
    QWidget* m_containerWidget = nullptr;
};

#else

// Stub for non-Windows builds
class WinEmbedWidget : public OopEmbedWidget {
public:
    explicit WinEmbedWidget(EmbedControlChannel* channel, QWidget* parent = nullptr)
        : OopEmbedWidget(channel, parent) {}
    bool embedSurface(const QJsonObject&) override { return false; }
};

#endif // Q_OS_WIN
