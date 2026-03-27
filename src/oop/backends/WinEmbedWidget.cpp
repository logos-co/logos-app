#include "WinEmbedWidget.h"

#ifdef Q_OS_WIN

#include "../EmbedControlChannel.h"
#include <QWindow>
#include <QVBoxLayout>
#include <QJsonObject>
#include <QDebug>
#include <windows.h>

WinEmbedWidget::WinEmbedWidget(EmbedControlChannel* channel, QWidget* parent)
    : OopEmbedWidget(channel, parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    setLayout(layout);
}

WinEmbedWidget::~WinEmbedWidget()
{
    if (m_foreignWindow) {
        m_foreignWindow->setParent(nullptr);
        delete m_foreignWindow;
        m_foreignWindow = nullptr;
    }
}

bool WinEmbedWidget::embedSurface(const QJsonObject& handleData)
{
    bool ok = false;
    qint64 hwndVal = handleData.value("winId").toVariant().toLongLong(&ok);
    if (!ok || hwndVal == 0) {
        qWarning() << "WinEmbedWidget: invalid winId (HWND) in surface handle";
        return false;
    }

    HWND hwnd = reinterpret_cast<HWND>(hwndVal);

    qDebug() << "WinEmbedWidget: embedding HWND" << hwndVal;

    // Convert to a child window so it never appears in the taskbar
    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    style = (style & ~WS_POPUP) | WS_CHILD;
    SetWindowLong(hwnd, GWL_STYLE, style);

    // Remove extended styles that would show it in Alt-Tab
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    exStyle &= ~WS_EX_APPWINDOW;
    exStyle |= WS_EX_TOOLWINDOW;
    SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);

    m_foreignWindow = QWindow::fromWinId(reinterpret_cast<WId>(hwnd));
    if (!m_foreignWindow) {
        qWarning() << "WinEmbedWidget: QWindow::fromWinId failed for HWND" << hwndVal;
        return false;
    }

    m_containerWidget = QWidget::createWindowContainer(m_foreignWindow, this);
    m_containerWidget->setMinimumSize(200, 200);
    layout()->addWidget(m_containerWidget);

    return true;
}

#endif // Q_OS_WIN
