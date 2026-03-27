#include "MacEmbedWidget.h"

#ifdef Q_OS_MACOS

#include "../EmbedControlChannel.h"
#include <QApplication>
#include <QJsonObject>
#include <QDebug>
#include <QTimer>
#include <QResizeEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QEvent>

MacEmbedWidget::MacEmbedWidget(EmbedControlChannel* channel, QWidget* parent)
    : OopEmbedWidget(channel, parent)
{
    // Dark placeholder so the user sees something before the child overlays it
    setStyleSheet(QStringLiteral("background-color: #2d2d2d;"));

    // Timer to continuously track our global screen position.
    // This handles cases where the parent window is dragged (which doesn't
    // generate events on the child widget).
    m_positionTimer = new QTimer(this);
    m_positionTimer->setInterval(33); // ~30 fps — balances responsiveness vs CPU
    connect(m_positionTimer, &QTimer::timeout, this, &MacEmbedWidget::sendPositionUpdate);

    // Track application activation state: hide child OOP windows when the
    // user switches to another app, show them when switching back.
    // Without this, child windows (which are in a separate process) would
    // float above other applications.
    connect(qApp, &QApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
                if (!m_embedded || !m_channel || !m_channel->isConnected())
                    return;
                if (state == Qt::ApplicationActive) {
                    if (isVisible()) {
                        sendPositionUpdate();
                        m_channel->sendVisibility(true);
                        m_positionTimer->start();
                    }
                } else if (state == Qt::ApplicationInactive) {
                    m_positionTimer->stop();
                    m_channel->sendVisibility(false);
                }
            });
}

MacEmbedWidget::~MacEmbedWidget()
{
    if (m_positionTimer)
        m_positionTimer->stop();

    // Tell the child to hide before we go away
    if (m_channel && m_channel->isConnected())
        m_channel->sendVisibility(false);
}

bool MacEmbedWidget::embedSurface(const QJsonObject& handleData)
{
    bool ok = false;
    m_childWinId = handleData.value("winId").toVariant().toLongLong(&ok);
    if (!ok || m_childWinId == 0) {
        qWarning() << "MacEmbedWidget: invalid winId in surface handle";
        return false;
    }

    m_embedded = true;
    qDebug() << "MacEmbedWidget: overlay-tracking macOS child window" << m_childWinId;

    // If we're already visible, start tracking immediately
    if (isVisible()) {
        qDebug() << "MacEmbedWidget: already visible, sending initial position";
        sendPositionUpdate();
        m_channel->sendVisibility(true);
        m_positionTimer->start();
    } else {
        qDebug() << "MacEmbedWidget: not visible yet, waiting for showEvent";
        // Fallback: in case showEvent doesn't fire (e.g. MDI quirk),
        // schedule a deferred check.
        QTimer::singleShot(500, this, [this]() {
            if (m_embedded && isVisible() && !m_positionTimer->isActive()) {
                qDebug() << "MacEmbedWidget: deferred start of position tracking";
                sendPositionUpdate();
                m_channel->sendVisibility(true);
                m_positionTimer->start();
            }
        });
    }

    return true;
}

void MacEmbedWidget::sendPositionUpdate()
{
    if (!m_channel || !m_channel->isConnected() || !m_embedded)
        return;

    QPoint globalPos = mapToGlobal(QPoint(0, 0));
    QSize currentSize = size();

    if (globalPos != m_lastGlobalPos || currentSize != m_lastSize) {
        m_lastGlobalPos = globalPos;
        m_lastSize = currentSize;
        m_channel->sendReposition(globalPos.x(), globalPos.y(),
                                   currentSize.width(), currentSize.height());
    }
}

void MacEmbedWidget::resizeEvent(QResizeEvent* event)
{
    OopEmbedWidget::resizeEvent(event);
    sendPositionUpdate();
}

void MacEmbedWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    qDebug() << "MacEmbedWidget: showEvent, embedded:" << m_embedded
             << "channelConnected:" << (m_channel && m_channel->isConnected());
    if (m_embedded) {
        sendPositionUpdate();
        if (m_channel) m_channel->sendVisibility(true);
        m_positionTimer->start();
    }
}

void MacEmbedWidget::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    qDebug() << "MacEmbedWidget: hideEvent";
    m_positionTimer->stop();
    if (m_channel && m_channel->isConnected())
        m_channel->sendVisibility(false);
}

bool MacEmbedWidget::event(QEvent* event)
{
    // Also catch Move events (in case layout/MDI triggers them)
    if (event->type() == QEvent::Move)
        sendPositionUpdate();
    return OopEmbedWidget::event(event);
}

#endif // Q_OS_MACOS
