#pragma once

#include "../OopEmbedWidget.h"

#ifdef Q_OS_MACOS

class QTimer;

// macOS backend: uses an overlay-tracking approach for cross-process embedding.
//
// On macOS, QWidget::winId() returns an NSView* that is only valid within
// the originating process. QWindow::fromWinId() crashes when given a pointer
// from a foreign address space.
//
// Instead, this widget acts as a placeholder in the MDI tab.  It tracks its
// own global screen position and sends "reposition" + "visibility" messages
// to the child process via the control channel.  The child keeps its own
// frameless Qt::Tool window and moves it to overlay the placeholder exactly.
class MacEmbedWidget : public OopEmbedWidget {
    Q_OBJECT

public:
    explicit MacEmbedWidget(EmbedControlChannel* channel, QWidget* parent = nullptr);
    ~MacEmbedWidget() override;

    bool embedSurface(const QJsonObject& handleData) override;

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool event(QEvent* event) override;

private:
    void sendPositionUpdate();

    QTimer* m_positionTimer = nullptr;
    QPoint m_lastGlobalPos;
    QSize m_lastSize;
    qint64 m_childWinId = 0;
    bool m_embedded = false;
};

#else

// Stub for non-macOS builds
class MacEmbedWidget : public OopEmbedWidget {
public:
    explicit MacEmbedWidget(EmbedControlChannel* channel, QWidget* parent = nullptr)
        : OopEmbedWidget(channel, parent) {}
    bool embedSurface(const QJsonObject&) override { return false; }
};

#endif // Q_OS_MACOS
