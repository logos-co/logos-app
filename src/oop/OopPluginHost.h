#pragma once

#include <QObject>
#include <QProcess>
#include "HostingModeResolver.h"

class LogosAPI;
class EmbedControlChannel;
class OopEmbedWidget;
class QTimer;

// Manages the lifecycle of a single out-of-process UI plugin:
//   1. Spawns logos_ui_host with the correct arguments
//   2. Sends the auth token (same pattern as logos_host / plugin_manager)
//   3. Receives the surface handle via EmbedControlChannel
//   4. Creates the platform-specific OopEmbedWidget
//   5. Monitors the child process for crashes
class OopPluginHost : public QObject {
    Q_OBJECT

public:
    OopPluginHost(const QString& pluginName,
                  const QString& pluginPath,
                  HostingMode mode,
                  LogosAPI* logosAPI,
                  QObject* parent = nullptr);
    ~OopPluginHost();

    void start();
    void stop();
    bool isRunning() const;

    QWidget* embedWidget() const;
    QString pluginName() const { return m_pluginName; }

signals:
    void widgetReady(QWidget* embedWidget);
    void hostCrashed(const QString& pluginName);
    void hostStopped(const QString& pluginName);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onSurfaceHandleReceived(const QJsonObject& handle);

private:
    QString findUiHostBinary() const;
    void sendAuthToken();
    OopEmbedWidget* createEmbedWidget();
    QString controlSocketName() const;
    QString waylandSocketName() const;

    QString m_pluginName;
    QString m_pluginPath;
    HostingMode m_mode;
    LogosAPI* m_logosAPI;

    QProcess* m_process = nullptr;
    EmbedControlChannel* m_controlChannel = nullptr;
    OopEmbedWidget* m_embedWidget = nullptr;
    QTimer* m_heartbeatTimer = nullptr;
};
