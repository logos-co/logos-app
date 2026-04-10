#include "main_ui_plugin.h"
#include "MainContainer.h"
#include <QDebug>
#include "logos_api.h"
#include "token_manager.h"

MainUIPlugin::MainUIPlugin(QObject* parent)
    : QObject(parent)
    , m_mainContainer(nullptr)
    , m_logosAPI(nullptr)
{
    qDebug() << "MainUIPlugin created";
}

MainUIPlugin::~MainUIPlugin()
{
    qDebug() << "MainUIPlugin destroyed";
    // m_mainContainer may already have been destroyed by its Qt parent
    // (e.g. the central widget owned by Window). QPointer auto-nulls in
    // that case, so destroyWidget() becomes a no-op and we avoid a
    // double-delete during plugin unload at process exit.
    destroyWidget(m_mainContainer.data());
}

QWidget* MainUIPlugin::createWidget(LogosAPI* logosAPI)
{
    qDebug() << "-----> MainUIPlugin::createWidget: logosAPI:" << logosAPI;
    if (logosAPI) {
        m_logosAPI = logosAPI;
    }

    // print keys
    if (m_logosAPI) {
        QList<QString> keys = m_logosAPI->getTokenManager()->getTokenKeys();
        for (const QString& key : keys) {
            qDebug() << "-----> MainUIPlugin::createWidget: Token key:" << key << "value:" << m_logosAPI->getTokenManager()->getToken(key);
        }
    }
    
    if (!m_mainContainer) {
        m_mainContainer = new MainContainer(m_logosAPI);
    }
    return m_mainContainer;
}

void MainUIPlugin::destroyWidget(QWidget* widget)
{
    if (widget) {
        delete widget;
        if (widget == m_mainContainer) {
            m_mainContainer = nullptr;
        }
    }
} 