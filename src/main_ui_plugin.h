#pragma once

#include <IComponent.h>
#include <QObject>
#include <QPointer>

class MainContainer;
class LogosAPI;

class MainUIPlugin : public QObject, public IComponent
{
    Q_OBJECT
    Q_INTERFACES(IComponent)
    Q_PLUGIN_METADATA(IID IComponent_iid FILE "metadata.json")

public:
    explicit MainUIPlugin(QObject* parent = nullptr);
    ~MainUIPlugin();

    // IComponent implementation
    Q_INVOKABLE QWidget* createWidget(LogosAPI* logosAPI = nullptr) override;
    void destroyWidget(QWidget* widget) override;

private:
    // QPointer auto-nulls when the widget is destroyed by its Qt parent
    // (e.g. by Window's destructor), so ~MainUIPlugin() won't double-delete
    // it later during QLibraryStore::cleanup() at process exit.
    QPointer<MainContainer> m_mainContainer;
    LogosAPI* m_logosAPI;
}; 