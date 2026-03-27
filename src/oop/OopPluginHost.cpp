#include "OopPluginHost.h"
#include "EmbedControlChannel.h"
#include "OopEmbedWidget.h"
#include "backends/X11EmbedWidget.h"
#include "backends/WaylandEmbedWidget.h"
#include "backends/MacEmbedWidget.h"
#include "backends/WinEmbedWidget.h"
#include "logos_api.h"
#include "token_manager.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLocalSocket>
#include <QTimer>
#include <QThread>
#include <QUuid>
#include <QDebug>

extern "C" {
    int logos_core_load_plugin_with_dependencies(const char* plugin_name);
}

OopPluginHost::OopPluginHost(const QString& pluginName,
                             const QString& pluginPath,
                             HostingMode mode,
                             LogosAPI* logosAPI,
                             QObject* parent)
    : QObject(parent)
    , m_pluginName(pluginName)
    , m_pluginPath(pluginPath)
    , m_mode(mode)
    , m_logosAPI(logosAPI)
{
}

OopPluginHost::~OopPluginHost()
{
    stop();
}

QString OopPluginHost::controlSocketName() const
{
    return QStringLiteral("logos_ui_ctrl_%1").arg(m_pluginName);
}

QString OopPluginHost::waylandSocketName() const
{
    return QStringLiteral("logos_ui_wl_%1").arg(m_pluginName);
}

QString OopPluginHost::findUiHostBinary() const
{
    // 1. Environment override
    QByteArray envPath = qgetenv("LOGOS_UI_HOST_PATH");
    if (!envPath.isEmpty() && QFile::exists(QString::fromUtf8(envPath)))
        return QString::fromUtf8(envPath);

    // 2. Next to the app executable
    QString appDir = QCoreApplication::applicationDirPath();
    QString candidate = QDir::cleanPath(appDir + "/logos_ui_host");
    if (QFile::exists(candidate))
        return candidate;

    // 3. Same directory as logos_host (../bin/ from modules dir)
    candidate = QDir::cleanPath(appDir + "/../bin/logos_ui_host");
    if (QFile::exists(candidate))
        return candidate;

    return {};
}

void OopPluginHost::start()
{
    if (m_process) {
        qWarning() << "OopPluginHost: already running for" << m_pluginName;
        return;
    }

    QString hostBin = findUiHostBinary();
    if (hostBin.isEmpty()) {
        qCritical() << "OopPluginHost: logos_ui_host not found for" << m_pluginName;
        return;
    }

    // Create the control channel server before starting the child process
    m_controlChannel = EmbedControlChannel::createServer(controlSocketName(), this);
    if (!m_controlChannel) {
        qCritical() << "OopPluginHost: failed to create control channel for" << m_pluginName;
        return;
    }

    connect(m_controlChannel, &EmbedControlChannel::surfaceHandleReceived,
            this, &OopPluginHost::onSurfaceHandleReceived);

    // For Wayland mode, create the embed widget (compositor) before the child starts
    // so it's listening on the Wayland socket when the child connects.
    if (m_mode == HostingMode::Wayland) {
        m_embedWidget = createEmbedWidget();
    }

    // Build arguments
    QStringList args;
    args << "--name" << m_pluginName;
    args << "--path" << m_pluginPath;
    args << "--mode" << QString::fromLatin1(HostingModeResolver::modeName(m_mode)).toLower();
    args << "--control-socket" << controlSocketName();

    if (m_mode == HostingMode::Wayland) {
        args << "--wayland-display" << waylandSocketName();
    }

    qDebug() << "OopPluginHost: starting logos_ui_host for" << m_pluginName
             << "mode:" << HostingModeResolver::modeName(m_mode);

    // Spawn
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &OopPluginHost::onProcessFinished);

    // Forward child stdout/stderr with prefix
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        QByteArray output = m_process->readAllStandardOutput();
        for (const QByteArray& line : output.split('\n')) {
            QByteArray trimmed = line.trimmed();
            if (!trimmed.isEmpty())
                qDebug() << "[UI_HOST" << m_pluginName << "]:" << trimmed;
        }
    });

    m_process->start(hostBin, args);

    if (!m_process->waitForStarted(5000)) {
        qCritical() << "OopPluginHost: failed to start logos_ui_host:"
                     << m_process->errorString();
        delete m_process;
        m_process = nullptr;
        return;
    }

    qDebug() << "OopPluginHost: logos_ui_host started, PID:" << m_process->processId();

    // Send auth token (mirrors plugin_manager.cpp pattern)
    sendAuthToken();

    // Heartbeat timer
    m_heartbeatTimer = new QTimer(this);
    connect(m_heartbeatTimer, &QTimer::timeout, this, [this]() {
        if (m_controlChannel && m_controlChannel->isConnected())
            m_controlChannel->sendHeartbeat();
    });
    m_heartbeatTimer->start(5000);
}

void OopPluginHost::stop()
{
    if (m_heartbeatTimer) {
        m_heartbeatTimer->stop();
        delete m_heartbeatTimer;
        m_heartbeatTimer = nullptr;
    }

    if (m_process) {
        disconnect(m_process, nullptr, this, nullptr);
        m_process->terminate();
        if (!m_process->waitForFinished(3000)) {
            qWarning() << "OopPluginHost: killing" << m_pluginName;
            m_process->kill();
            m_process->waitForFinished(1000);
        }
        delete m_process;
        m_process = nullptr;
    }

    if (m_controlChannel) {
        delete m_controlChannel;
        m_controlChannel = nullptr;
    }

    // Don't delete the embed widget here — MdiView owns it
    m_embedWidget = nullptr;
}

bool OopPluginHost::isRunning() const
{
    return m_process && m_process->state() == QProcess::Running;
}

QWidget* OopPluginHost::embedWidget() const
{
    return m_embedWidget;
}

void OopPluginHost::sendAuthToken()
{
    // Mirror the pattern from plugin_manager.cpp:
    // logos_ui_host creates a QLocalServer on "logos_token_ui_<name>",
    // we connect and send a UUID token.
    QString socketName = QStringLiteral("logos_token_ui_%1").arg(m_pluginName);
    QLocalSocket* tokenSocket = new QLocalSocket(this);

    bool connected = false;
    for (int i = 0; i < 10; ++i) {
        tokenSocket->connectToServer(socketName);
        if (tokenSocket->waitForConnected(100)) {
            connected = true;
            break;
        }
        QThread::msleep(100);
    }

    if (!connected) {
        qCritical() << "OopPluginHost: failed to connect token socket for" << m_pluginName;
        tokenSocket->deleteLater();
        return;
    }

    QString authToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
    tokenSocket->write(authToken.toUtf8());
    tokenSocket->waitForBytesWritten(1000);
    tokenSocket->disconnectFromServer();
    tokenSocket->deleteLater();

    // Store token in the token manager so other modules can communicate
    if (m_logosAPI) {
        m_logosAPI->getTokenManager()->saveToken(QStringLiteral("ui_%1").arg(m_pluginName), authToken);
    }

    qDebug() << "OopPluginHost: auth token sent to" << m_pluginName;
}

OopEmbedWidget* OopPluginHost::createEmbedWidget()
{
    switch (m_mode) {
    case HostingMode::Wayland:
        return new WaylandEmbedWidget(m_controlChannel, waylandSocketName());
    case HostingMode::X11:
        return new X11EmbedWidget(m_controlChannel);
    case HostingMode::MacNative:
        return new MacEmbedWidget(m_controlChannel);
    case HostingMode::WinNative:
        return new WinEmbedWidget(m_controlChannel);
    case HostingMode::InProcess:
        return nullptr;
    }
    return nullptr;
}

void OopPluginHost::onSurfaceHandleReceived(const QJsonObject& handle)
{
    qDebug() << "OopPluginHost: surface handle received for" << m_pluginName;

    // For non-Wayland modes, create the embed widget now
    if (!m_embedWidget) {
        m_embedWidget = createEmbedWidget();
    }

    if (!m_embedWidget) {
        qWarning() << "OopPluginHost: failed to create embed widget for" << m_pluginName;
        return;
    }

    if (!m_embedWidget->embedSurface(handle)) {
        qWarning() << "OopPluginHost: failed to embed surface for" << m_pluginName;
        return;
    }

    emit widgetReady(m_embedWidget);
}

void OopPluginHost::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    qDebug() << "OopPluginHost:" << m_pluginName << "exited, code:" << exitCode
             << "status:" << status;

    m_process->deleteLater();
    m_process = nullptr;

    if (status == QProcess::CrashExit) {
        qWarning() << "OopPluginHost: plugin" << m_pluginName << "crashed!";
        emit hostCrashed(m_pluginName);
    } else {
        emit hostStopped(m_pluginName);
    }
}
