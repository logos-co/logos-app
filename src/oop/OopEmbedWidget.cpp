#include "OopEmbedWidget.h"
#include "EmbedControlChannel.h"
#include <QResizeEvent>

OopEmbedWidget::OopEmbedWidget(EmbedControlChannel* channel, QWidget* parent)
    : QWidget(parent)
    , m_channel(channel)
{
}

OopEmbedWidget::~OopEmbedWidget() = default;

void OopEmbedWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_channel)
        m_channel->sendResize(event->size().width(), event->size().height());
}

void OopEmbedWidget::focusInEvent(QFocusEvent* event)
{
    QWidget::focusInEvent(event);
    if (m_channel)
        m_channel->sendFocusChange(true);
}

void OopEmbedWidget::focusOutEvent(QFocusEvent* event)
{
    QWidget::focusOutEvent(event);
    if (m_channel)
        m_channel->sendFocusChange(false);
}

void OopEmbedWidget::closeEvent(QCloseEvent* event)
{
    if (m_channel)
        m_channel->sendClose();
    QWidget::closeEvent(event);
}
