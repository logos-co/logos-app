#pragma once

#include <QObject>
#include <QByteArray>
#include <QJsonObject>

class QLocalServer;
class QLocalSocket;

// Bidirectional control channel between basecamp and logos_ui_host.
// Carries surface handles, resize/focus/close commands, and heartbeats
// over a QLocalSocket using newline-delimited JSON messages.
class EmbedControlChannel : public QObject {
    Q_OBJECT

public:
    // Server side (basecamp) — listens for the UI host to connect
    static EmbedControlChannel* createServer(const QString& socketName, QObject* parent = nullptr);
    // Client side (logos_ui_host) — connects to basecamp's server
    static EmbedControlChannel* createClient(const QString& socketName, QObject* parent = nullptr);

    ~EmbedControlChannel();

    bool isConnected() const;

    // --- Messages sent by logos_ui_host -> basecamp ---
    void sendSurfaceHandle(const QJsonObject& handle);

    // --- Messages sent by basecamp -> logos_ui_host ---
    void sendResize(int width, int height);
    void sendFocusChange(bool focused);
    void sendClose();
    void sendReposition(int x, int y, int width, int height);
    void sendVisibility(bool visible);

    // --- Bidirectional ---
    void sendHeartbeat();
    void sendCustom(const QString& type, const QJsonObject& data = {});

signals:
    void connected();
    void disconnected();
    void surfaceHandleReceived(const QJsonObject& handle);
    void resizeRequested(int width, int height);
    void focusChangeRequested(bool focused);
    void closeRequested();
    void repositionRequested(int x, int y, int width, int height);
    void visibilityChangeRequested(bool visible);
    void heartbeatReceived();
    void customMessageReceived(const QString& type, const QJsonObject& data);

private:
    explicit EmbedControlChannel(QObject* parent = nullptr);
    void sendMessage(const QJsonObject& msg);
    void onDataReady();
    void processLine(const QByteArray& line);

    QLocalServer* m_server = nullptr;
    QLocalSocket* m_socket = nullptr;
    QByteArray m_buffer;
};
