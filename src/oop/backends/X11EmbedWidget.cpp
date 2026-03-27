#include "X11EmbedWidget.h"
#include "../EmbedControlChannel.h"
#include <QWindow>
#include <QVBoxLayout>
#include <QJsonObject>
#include <QDebug>

X11EmbedWidget::X11EmbedWidget(EmbedControlChannel* channel, QWidget* parent)
    : OopEmbedWidget(channel, parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    setLayout(layout);
}

X11EmbedWidget::~X11EmbedWidget()
{
    // QWidget::createWindowContainer() takes ownership of the container but
    // not of the QWindow. Clean up only the foreign window wrapper.
    // The actual X11 window is owned by the child process.
    if (m_foreignWindow) {
        m_foreignWindow->setParent(nullptr);
        delete m_foreignWindow;
        m_foreignWindow = nullptr;
    }
}

bool X11EmbedWidget::embedSurface(const QJsonObject& handleData)
{
    bool ok = false;
    qint64 winId = handleData.value("winId").toVariant().toLongLong(&ok);
    if (!ok || winId == 0) {
        qWarning() << "X11EmbedWidget: invalid winId in surface handle";
        return false;
    }

    qDebug() << "X11EmbedWidget: embedding X11 window" << winId;

    m_foreignWindow = QWindow::fromWinId(static_cast<WId>(winId));
    if (!m_foreignWindow) {
        qWarning() << "X11EmbedWidget: QWindow::fromWinId failed for" << winId
                    << "— falling back to overlay tracking";
        // Overlay fallback: treat like MacEmbedWidget — just succeed so the
        // widget gets added to MDI.  The parent will send reposition messages.
        return true;
    }

    m_containerWidget = QWidget::createWindowContainer(m_foreignWindow, this);
    m_containerWidget->setMinimumSize(200, 200);
    layout()->addWidget(m_containerWidget);

    return true;
}
