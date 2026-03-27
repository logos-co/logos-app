#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPluginLoader>
#include <QQuickWidget>
#include <QQmlEngine>
#include <QQmlContext>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include <QWindow>
#include "IComponent.h"
#include "logos_api.h"
#include "logos_api_provider.h"
#include "token_manager.h"
#include "macos_helpers.h"

// Reuse the EmbedControlChannel from the main app (linked as a library or
// compiled directly into this executable).
#include "oop/EmbedControlChannel.h"

// -----------------------------------------------------------------------
// logos_ui_host — out-of-process UI plugin host
//
// Mirrors the pattern of logos_host (for core modules) but creates a
// QApplication so UI plugins can render widgets.
//
// Communication:
//   * Auth token: received via QLocalSocket ("logos_token_ui_<name>")
//   * Control channel: bidirectional JSON-line protocol via QLocalSocket
//   * LogosAPI: Qt Remote Objects (same as core modules)
// -----------------------------------------------------------------------

struct UiHostArgs {
    QString name;
    QString path;
    QString mode;          // "wayland", "x11", "macnative", "winnative"
    QString controlSocket; // QLocalSocket name for control channel
    QString waylandDisplay; // Wayland socket name (only for wayland mode)
};

static UiHostArgs parseArgs(QCoreApplication& app)
{
    QCommandLineParser parser;
    parser.setApplicationDescription("Logos UI Host — out-of-process UI plugin runner");
    parser.addHelpOption();

    QCommandLineOption nameOpt("name", "Plugin name", "name");
    QCommandLineOption pathOpt("path", "Plugin path", "path");
    QCommandLineOption modeOpt("mode", "Hosting mode (wayland|x11|macnative|winnative)", "mode");
    QCommandLineOption ctrlOpt("control-socket", "Control channel socket name", "socket");
    QCommandLineOption wlOpt("wayland-display", "Wayland display socket name", "wl");

    parser.addOption(nameOpt);
    parser.addOption(pathOpt);
    parser.addOption(modeOpt);
    parser.addOption(ctrlOpt);
    parser.addOption(wlOpt);

    parser.process(app);

    return {
        parser.value(nameOpt),
        parser.value(pathOpt),
        parser.value(modeOpt),
        parser.value(ctrlOpt),
        parser.value(wlOpt)
    };
}

static QString receiveAuthToken(const QString& pluginName)
{
    // Same pattern as logos_host's plugin_initializer.cpp
    QString socketName = QStringLiteral("logos_token_ui_%1").arg(pluginName);
    QLocalServer tokenServer;
    QLocalServer::removeServer(socketName);

    if (!tokenServer.listen(socketName)) {
        qCritical() << "logos_ui_host: failed to listen for auth token:" << tokenServer.errorString();
        return {};
    }

    qDebug() << "logos_ui_host: waiting for auth token on" << socketName;

    if (!tokenServer.waitForNewConnection(10000)) {
        qCritical() << "logos_ui_host: timeout waiting for auth token";
        return {};
    }

    QLocalSocket* client = tokenServer.nextPendingConnection();
    if (!client->waitForReadyRead(5000)) {
        qCritical() << "logos_ui_host: timeout reading auth token";
        client->deleteLater();
        return {};
    }

    QString token = QString::fromUtf8(client->readAll());
    client->deleteLater();

    qDebug() << "logos_ui_host: auth token received";
    return token;
}

static void publishSurfaceHandle(EmbedControlChannel* channel, const QString& mode, QWidget* widget)
{
    QJsonObject handle;
    handle["platform"] = mode;

    if (mode == QLatin1String("wayland")) {
        // Wayland mode: the surface is published implicitly when the widget
        // connects to the compositor. Just acknowledge.
        handle["status"] = QStringLiteral("connected");
    } else {
        // X11 / macOS / Windows: send the native window ID
        WId winId = widget->winId();
        handle["winId"] = QString::number(static_cast<qint64>(winId));
    }

    channel->sendSurfaceHandle(handle);
    qDebug() << "logos_ui_host: surface handle sent:" << QJsonDocument(handle).toJson(QJsonDocument::Compact);
}

static void hideFromTaskbar(QWidget* widget, const QString& mode)
{
    Q_UNUSED(mode);
#if defined(Q_OS_MACOS)
    // macOS overlay mode: use Qt::Tool so the window doesn't appear in
    // Dock or Cmd-Tab. Combined with NSApplicationActivationPolicyAccessory
    // (set in main()), Qt::Tool windows stay visible and interactive even
    // though this is a separate process.
    widget->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
    // Start off-screen; the parent will send the real position.
    widget->move(-10000, -10000);
#else
    // Linux / Windows: Tool + Frameless keeps it out of taskbar / Alt-Tab.
    widget->setWindowFlags(widget->windowFlags() | Qt::Tool | Qt::FramelessWindowHint);
#endif
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("logos_ui_host");
    app.setApplicationVersion("1.0");

    // On macOS, mark this process as an accessory app so it doesn't
    // appear in the Dock or Cmd-Tab. Must be called before showing windows.
    macosConfigureAccessoryApp();

    UiHostArgs args = parseArgs(app);

    if (args.name.isEmpty() || args.path.isEmpty()) {
        qCritical() << "logos_ui_host: --name and --path are required";
        return 1;
    }

    qDebug() << "logos_ui_host: starting for plugin" << args.name << "mode:" << args.mode;

    // For Wayland mode, set the display so Qt renders to our private compositor
    if (args.mode == QLatin1String("wayland") && !args.waylandDisplay.isEmpty()) {
        qputenv("WAYLAND_DISPLAY", args.waylandDisplay.toUtf8());
        qDebug() << "logos_ui_host: set WAYLAND_DISPLAY to" << args.waylandDisplay;
    }

    // 1. Receive auth token
    QString authToken = receiveAuthToken(args.name);
    if (authToken.isEmpty())
        return 1;

    // 2. Connect to the control channel
    EmbedControlChannel* channel = EmbedControlChannel::createClient(args.controlSocket);
    if (!channel) {
        qCritical() << "logos_ui_host: failed to connect control channel" << args.controlSocket;
        return 1;
    }

    // 3. Set up LogosAPI for this UI plugin
    LogosAPI logosAPI(QStringLiteral("ui_%1").arg(args.name));
    logosAPI.getTokenManager()->saveToken("core", authToken);
    logosAPI.getTokenManager()->saveToken("capability_module", authToken);

    // 4. Load the UI plugin
    QWidget* pluginWidget = nullptr;

    // Check if it's a QML plugin by looking at the manifest/metadata
    auto loadManifest = [&]() -> QJsonObject {
        for (const QString& filename : {"manifest.json", "metadata.json"}) {
            QFile f(args.path + "/" + filename);
            if (f.open(QIODevice::ReadOnly)) {
                QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
                if (doc.isObject()) return doc.object();
            }
            // Also try the path as a direct plugin file — manifest may be alongside
            QFileInfo fi(args.path);
            QFile f2(fi.absolutePath() + "/" + filename);
            if (f2.open(QIODevice::ReadOnly)) {
                QJsonDocument doc = QJsonDocument::fromJson(f2.readAll());
                if (doc.isObject()) return doc.object();
            }
        }
        return {};
    };

    QJsonObject manifest = loadManifest();
    QString pluginType = manifest.value("type").toString();
    bool isQml = (pluginType == QLatin1String("ui_qml"));

    if (isQml) {
        // QML plugin loading (mirrors MainUIBackend::loadUiModule QML path)
        QString mainFile = manifest.value("main").toString("Main.qml");
        QString pluginDir = args.path;
        QString qmlFilePath = QDir(pluginDir).filePath(mainFile);

        if (!QFile::exists(qmlFilePath)) {
            qCritical() << "logos_ui_host: QML file not found:" << qmlFilePath;
            return 1;
        }

        QQuickWidget* qmlWidget = new QQuickWidget();
        qmlWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);

        QQmlEngine* engine = qmlWidget->engine();
        // logos_ui_host is already process-isolated, so no need to sandbox
        // the QML engine's import paths (unlike the in-process path which
        // restricts to qrc-only).  Just add the plugin dir to the defaults.
        // The defaults already include QLibraryInfo::QmlImportsPath and
        // QML2_IMPORT_PATH entries (set by the nix wrapper).
        engine->addImportPath(pluginDir);

        qDebug() << "logos_ui_host: QML import paths:" << engine->importPathList();

        // Note: we could add LogosQmlBridge here if needed
        // LogosQmlBridge* bridge = new LogosQmlBridge(&logosAPI, qmlWidget);
        // qmlWidget->rootContext()->setContextProperty("logos", bridge);

        qmlWidget->setSource(QUrl::fromLocalFile(qmlFilePath));

        if (qmlWidget->status() == QQuickWidget::Error) {
            for (const auto& error : qmlWidget->errors())
                qCritical() << "logos_ui_host: QML error:" << error.toString();
            delete qmlWidget;
            return 1;
        }

        pluginWidget = qmlWidget;
    } else {
        // C++ plugin loading (mirrors MainUIBackend::loadUiModule C++ path)
        QString pluginLibPath = args.path;

        QPluginLoader loader(pluginLibPath);
        if (!loader.load()) {
            qCritical() << "logos_ui_host: failed to load plugin:" << loader.errorString();
            return 1;
        }

        QObject* plugin = loader.instance();
        if (!plugin) {
            qCritical() << "logos_ui_host: failed to get plugin instance";
            return 1;
        }

        IComponent* component = qobject_cast<IComponent*>(plugin);
        if (!component) {
            qCritical() << "logos_ui_host: plugin does not implement IComponent";
            return 1;
        }

        pluginWidget = component->createWidget(&logosAPI);
        if (!pluginWidget) {
            qCritical() << "logos_ui_host: component returned null widget";
            return 1;
        }
    }

    // 5. Configure the widget for embedding
    if (args.mode != QLatin1String("wayland")) {
        hideFromTaskbar(pluginWidget, args.mode);
    }

    pluginWidget->resize(800, 600);
    pluginWidget->show();

    // Force window ID creation (needed for X11/macOS/Win to get the native handle)
    pluginWidget->winId();

    // 6. Publish the surface handle to basecamp
    publishSurfaceHandle(channel, args.mode, pluginWidget);

    // 7. Handle control channel commands
    QObject::connect(channel, &EmbedControlChannel::resizeRequested,
                     pluginWidget, [pluginWidget](int w, int h) {
                         pluginWidget->resize(w, h);
                     });

    QObject::connect(channel, &EmbedControlChannel::closeRequested,
                     &app, &QApplication::quit);

    QObject::connect(channel, &EmbedControlChannel::focusChangeRequested,
                     pluginWidget, [pluginWidget](bool focused) {
                         if (focused) {
                             pluginWidget->activateWindow();
                             pluginWidget->setFocus();
                         }
                     });

    // Overlay mode (macOS): parent sends screen coordinates and we reposition.
    // Also used on other platforms if the embed backend sends reposition msgs.
    QObject::connect(channel, &EmbedControlChannel::repositionRequested,
                     pluginWidget, [pluginWidget, &args](int x, int y, int w, int h) {
                         qDebug() << "logos_ui_host: reposition" << args.name
                                  << "to" << x << y << w << h;
                         pluginWidget->move(x, y);
                         pluginWidget->resize(w, h);
                         pluginWidget->raise(); // ensure we're above the parent window
                     });

    QObject::connect(channel, &EmbedControlChannel::visibilityChangeRequested,
                     pluginWidget, [pluginWidget, &args](bool visible) {
                         qDebug() << "logos_ui_host: visibility" << args.name << visible;
                         pluginWidget->setVisible(visible);
                         if (visible)
                             pluginWidget->raise();
                     });

    QObject::connect(channel, &EmbedControlChannel::disconnected,
                     &app, [&]() {
                         qDebug() << "logos_ui_host: control channel disconnected, exiting";
                         app.quit();
                     });

    qDebug() << "logos_ui_host: ready, entering event loop for" << args.name;
    int result = app.exec();

    qDebug() << "logos_ui_host: shutting down" << args.name;
    delete pluginWidget;

    return result;
}
