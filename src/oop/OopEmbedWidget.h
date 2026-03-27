#pragma once

#include <QWidget>

class EmbedControlChannel;

// Abstract base class for platform-specific embedding widgets.
// Each subclass knows how to take a surface handle (window ID, Wayland socket,
// NSView pointer, HWND) and present the remote plugin's visuals inside a
// QWidget that can be added to MdiView::addPluginWindow().
//
// The base class forwards resize / focus / close events to the control channel
// so the child process can react.
class OopEmbedWidget : public QWidget {
    Q_OBJECT

public:
    explicit OopEmbedWidget(EmbedControlChannel* channel, QWidget* parent = nullptr);
    ~OopEmbedWidget() override;

    // Subclasses implement this to create the embedded surface from the handle
    // data sent by logos_ui_host. Returns true on success.
    virtual bool embedSurface(const QJsonObject& handleData) = 0;

protected:
    void resizeEvent(QResizeEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

    EmbedControlChannel* m_channel;
};
