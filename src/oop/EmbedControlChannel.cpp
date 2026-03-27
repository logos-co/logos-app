#include "EmbedControlChannel.h"
#include <QLocalServer>
#include <QLocalSocket>
#include <QJsonDocument>
#include <QDebug>

EmbedControlChannel::EmbedControlChannel(QObject* parent)
    : QObject(parent)
{
}

EmbedControlChannel::~EmbedControlChannel()
{
    if (m_socket) {
        m_socket->disconnectFromServer();
    }
}

EmbedControlChannel* EmbedControlChannel::createServer(const QString& socketName, QObject* parent)
{
    auto* channel = new EmbedControlChannel(parent);

    QLocalServer::removeServer(socketName);
    channel->m_server = new QLocalServer(channel);

    if (!channel->m_server->listen(socketName)) {
        qWarning() << "EmbedControlChannel: failed to listen on" << socketName
                    << channel->m_server->errorString();
        delete channel;
        return nullptr;
    }

    QObject::connect(channel->m_server, &QLocalServer::newConnection, channel, [channel]() {
        if (channel->m_socket) {
            // Only accept one connection
            auto* pending = channel->m_server->nextPendingConnection();
            pending->disconnectFromServer();
            pending->deleteLater();
            return;
        }
        channel->m_socket = channel->m_server->nextPendingConnection();
        QObject::connect(channel->m_socket, &QLocalSocket::readyRead, channel, &EmbedControlChannel::onDataReady);
        QObject::connect(channel->m_socket, &QLocalSocket::disconnected, channel, &EmbedControlChannel::disconnected);
        emit channel->connected();
    });

    return channel;
}

EmbedControlChannel* EmbedControlChannel::createClient(const QString& socketName, QObject* parent)
{
    auto* channel = new EmbedControlChannel(parent);
    channel->m_socket = new QLocalSocket(channel);

    QObject::connect(channel->m_socket, &QLocalSocket::connected, channel, &EmbedControlChannel::connected);
    QObject::connect(channel->m_socket, &QLocalSocket::disconnected, channel, &EmbedControlChannel::disconnected);
    QObject::connect(channel->m_socket, &QLocalSocket::readyRead, channel, &EmbedControlChannel::onDataReady);

    // Retry connection up to 1 second (the server may not be ready yet)
    bool ok = false;
    for (int i = 0; i < 10; ++i) {
        channel->m_socket->connectToServer(socketName);
        if (channel->m_socket->waitForConnected(100)) {
            ok = true;
            break;
        }
    }

    if (!ok) {
        qWarning() << "EmbedControlChannel: failed to connect to" << socketName;
        delete channel;
        return nullptr;
    }

    return channel;
}

bool EmbedControlChannel::isConnected() const
{
    return m_socket && m_socket->state() == QLocalSocket::ConnectedState;
}

// ---- Sending -----------------------------------------------------------

void EmbedControlChannel::sendMessage(const QJsonObject& msg)
{
    if (!m_socket || m_socket->state() != QLocalSocket::ConnectedState)
        return;

    QByteArray data = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    data.append('\n');
    m_socket->write(data);
    m_socket->flush();
}

void EmbedControlChannel::sendSurfaceHandle(const QJsonObject& handle)
{
    QJsonObject msg = handle;
    msg["type"] = QStringLiteral("surface_handle");
    sendMessage(msg);
}

void EmbedControlChannel::sendResize(int width, int height)
{
    sendMessage({{"type", "resize"}, {"width", width}, {"height", height}});
}

void EmbedControlChannel::sendFocusChange(bool focused)
{
    sendMessage({{"type", "focus"}, {"focused", focused}});
}

void EmbedControlChannel::sendClose()
{
    sendMessage({{"type", "close"}});
}

void EmbedControlChannel::sendReposition(int x, int y, int width, int height)
{
    sendMessage({{"type", "reposition"}, {"x", x}, {"y", y}, {"width", width}, {"height", height}});
}

void EmbedControlChannel::sendVisibility(bool visible)
{
    sendMessage({{"type", "visibility"}, {"visible", visible}});
}

void EmbedControlChannel::sendHeartbeat()
{
    sendMessage({{"type", "heartbeat"}});
}

void EmbedControlChannel::sendCustom(const QString& type, const QJsonObject& data)
{
    QJsonObject msg = data;
    msg["type"] = type;
    sendMessage(msg);
}

// ---- Receiving ---------------------------------------------------------

void EmbedControlChannel::onDataReady()
{
    m_buffer.append(m_socket->readAll());

    int idx;
    while ((idx = m_buffer.indexOf('\n')) >= 0) {
        QByteArray line = m_buffer.left(idx).trimmed();
        m_buffer.remove(0, idx + 1);
        if (!line.isEmpty())
            processLine(line);
    }
}

void EmbedControlChannel::processLine(const QByteArray& line)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "EmbedControlChannel: bad message:" << line;
        return;
    }

    QJsonObject msg = doc.object();
    QString type = msg.value("type").toString();

    if (type == QLatin1String("surface_handle")) {
        emit surfaceHandleReceived(msg);
    } else if (type == QLatin1String("resize")) {
        emit resizeRequested(msg["width"].toInt(), msg["height"].toInt());
    } else if (type == QLatin1String("focus")) {
        emit focusChangeRequested(msg["focused"].toBool());
    } else if (type == QLatin1String("close")) {
        emit closeRequested();
    } else if (type == QLatin1String("reposition")) {
        emit repositionRequested(msg["x"].toInt(), msg["y"].toInt(),
                                  msg["width"].toInt(), msg["height"].toInt());
    } else if (type == QLatin1String("visibility")) {
        emit visibilityChangeRequested(msg["visible"].toBool());
    } else if (type == QLatin1String("heartbeat")) {
        emit heartbeatReceived();
    } else {
        emit customMessageReceived(type, msg);
    }
}
