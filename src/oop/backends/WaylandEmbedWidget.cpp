#include "WaylandEmbedWidget.h"

#ifdef HAS_WAYLAND_COMPOSITOR

#include "../EmbedControlChannel.h"
#include <QVBoxLayout>
#include <QQuickWidget>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QDebug>
#include <QWaylandQuickItem>
#include <QWaylandSeat>

WaylandEmbedWidget::WaylandEmbedWidget(EmbedControlChannel* channel,
                                       const QString& socketName,
                                       QWidget* parent)
    : OopEmbedWidget(channel, parent)
    , m_socketName(socketName)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    setLayout(layout);

    initCompositor();
}

WaylandEmbedWidget::~WaylandEmbedWidget()
{
    // Compositor cleanup is parented to this widget
}

void WaylandEmbedWidget::initCompositor()
{
    // Create a QQuickWidget to host the compositor scene
    m_quickWidget = new QQuickWidget(this);
    m_quickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    layout()->addWidget(m_quickWidget);

    // Create the compositor attached to this quick widget's window
    m_compositor = new QWaylandCompositor(this);

    // Listen on our private socket
    m_compositor->setSocketName(m_socketName.toUtf8());

    // Create output
    QWindow* window = m_quickWidget->quickWindow();
    m_output = new QWaylandOutput(m_compositor, window);

    // XDG shell for proper surface management
    m_xdgShell = new QWaylandXdgShell(m_compositor);

    connect(m_compositor, &QWaylandCompositor::surfaceCreated,
            this, &WaylandEmbedWidget::onSurfaceCreated);
    connect(m_xdgShell, &QWaylandXdgShell::toplevelCreated,
            this, &WaylandEmbedWidget::onXdgToplevelCreated);

    m_compositor->create();

    qDebug() << "WaylandEmbedWidget: compositor listening on" << m_socketName;
}

bool WaylandEmbedWidget::embedSurface(const QJsonObject& handleData)
{
    // For the Wayland backend, the surface is received asynchronously via
    // onSurfaceCreated/onXdgToplevelCreated when the client connects.
    // The handleData may contain the socket name for verification.
    Q_UNUSED(handleData);
    return true;
}

void WaylandEmbedWidget::onSurfaceCreated(QWaylandSurface* surface)
{
    qDebug() << "WaylandEmbedWidget: client surface created";
    m_clientSurface = surface;
}

void WaylandEmbedWidget::onXdgToplevelCreated(QWaylandXdgToplevel* toplevel,
                                               QWaylandXdgSurface* xdgSurface)
{
    Q_UNUSED(toplevel);
    qDebug() << "WaylandEmbedWidget: XDG toplevel created, embedding surface";

    // Create a QWaylandQuickItem to render the client surface
    QQuickItem* rootItem = m_quickWidget->rootObject();
    if (!rootItem) {
        // If no QML root yet, set a minimal scene
        m_quickWidget->setSource(QUrl());
        m_quickWidget->engine()->clearComponentCache();
    }

    auto* surfaceItem = new QWaylandQuickItem();
    surfaceItem->setSurface(xdgSurface->surface());
    surfaceItem->setParentItem(m_quickWidget->rootObject());

    // Fill the entire widget area
    if (m_quickWidget->rootObject()) {
        surfaceItem->setWidth(m_quickWidget->rootObject()->width());
        surfaceItem->setHeight(m_quickWidget->rootObject()->height());
    }

    // Assign this surface as the input focus
    QWaylandSeat* seat = m_compositor->defaultSeat();
    if (seat) {
        seat->setKeyboardFocus(xdgSurface->surface());
    }
}

#endif // HAS_WAYLAND_COMPOSITOR
