/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "window.h"

#include "core/output.h"
#include "tiles/tilemanager.h"
#include "utils/common.h"

#if KWIN_BUILD_ACTIVITIES
#include "activities.h"
#endif
#include "appmenu.h"
#include "atoms.h"
#include "client_machine.h"
#include "composite.h"
#include "decorations/decoratedclient.h"
#include "decorations/decorationbridge.h"
#include "decorations/decorationpalette.h"
#include "effects.h"
#include "focuschain.h"
#include "input.h"
#include "outline.h"
#include "placement.h"
#if KWIN_BUILD_TABBOX
#include "tabbox.h"
#endif
#include "scene/shadowitem.h"
#include "scene/surfaceitem_x11.h"
#include "scene/windowitem.h"
#include "screenedge.h"
#include "shadow.h"
#include "useractions.h"
#include "virtualdesktops.h"
#include "wayland/output_interface.h"
#include "wayland/plasmawindowmanagement_interface.h"
#include "wayland/surface_interface.h"
#include "wayland_server.h"
#include "workspace.h"

#include <KDecoration2/DecoratedClient>
#include <KDecoration2/Decoration>

#include <KDesktopFile>

#include <QDebug>
#include <QDir>
#include <QMouseEvent>
#include <QStyleHints>

namespace KWin
{

static inline int sign(int v)
{
    return (v > 0) - (v < 0);
}

QHash<QString, std::weak_ptr<Decoration::DecorationPalette>> Window::s_palettes;
std::shared_ptr<Decoration::DecorationPalette> Window::s_defaultPalette;

Window::Window()
    : m_output(workspace()->activeOutput())
    , m_visual(XCB_NONE)
    , bit_depth(24)
    , info(nullptr)
    , ready_for_painting(false)
    , m_internalId(QUuid::createUuid())
    , m_client()
    , is_shape(false)
    , m_clientMachine(new ClientMachine(this))
    , m_wmClientLeader(XCB_WINDOW_NONE)
    , m_skipCloseAnimation(false)
#if KWIN_BUILD_TABBOX
    , m_tabBoxClient(QSharedPointer<TabBox::TabBoxClientImpl>::create(this))
#endif
    , m_colorScheme(QStringLiteral("kdeglobals"))
    , m_moveResizeOutput(workspace()->activeOutput())
{
    connect(this, &Window::bufferGeometryChanged, this, &Window::inputTransformationChanged);

    // Only for compatibility reasons, drop in the next major release.
    connect(this, &Window::frameGeometryChanged, this, &Window::geometryChanged);
    connect(this, &Window::geometryShapeChanged, this, &Window::discardShapeRegion);

    connect(this, &Window::clientStartUserMovedResized, this, &Window::moveResizedChanged);
    connect(this, &Window::clientFinishUserMovedResized, this, &Window::moveResizedChanged);

    connect(this, &Window::windowShown, this, &Window::hiddenChanged);
    connect(this, &Window::windowHidden, this, &Window::hiddenChanged);

    connect(this, &Window::paletteChanged, this, &Window::triggerDecorationRepaint);

    // If the user manually moved the window, don't restore it after the keyboard closes
    connect(this, &Window::clientFinishUserMovedResized, this, [this]() {
        m_keyboardGeometryRestore = QRectF();
    });
    connect(this, qOverload<Window *, bool, bool>(&Window::clientMaximizedStateChanged), this, [this]() {
        m_keyboardGeometryRestore = QRectF();
    });
    connect(this, &Window::fullScreenChanged, this, [this]() {
        m_keyboardGeometryRestore = QRectF();
    });

    // replace on-screen-display on size changes
    connect(this, &Window::frameGeometryChanged, this, [this](Window *c, const QRectF &old) {
        if (isOnScreenDisplay() && !frameGeometry().isEmpty() && old.size() != frameGeometry().size() && isPlaceable()) {
            GeometryUpdatesBlocker blocker(this);
            workspace()->placement()->place(this, workspace()->clientArea(PlacementArea, this, workspace()->activeOutput()));
        }
    });

    connect(Workspace::self()->applicationMenu(), &ApplicationMenu::applicationMenuEnabledChanged, this, [this] {
        Q_EMIT hasApplicationMenuChanged(hasApplicationMenu());
    });
    connect(&m_offscreenFramecallbackTimer, &QTimer::timeout, this, &Window::maybeSendFrameCallback);
}

Window::~Window()
{
    if (m_tile) {
        m_tile->removeWindow(this);
    }
    Q_ASSERT(m_blockGeometryUpdates == 0);
    Q_ASSERT(m_decoration.decoration == nullptr);
    delete info;
}

QDebug operator<<(QDebug debug, const Window *window)
{
    QDebugStateSaver saver(debug);
    debug.nospace();
    if (window) {
        debug << window->metaObject()->className() << '(' << static_cast<const void *>(window);
        if (window->window()) {
            debug << ", windowId=0x" << Qt::hex << window->window() << Qt::dec;
        }
        if (const KWaylandServer::SurfaceInterface *surface = window->surface()) {
            debug << ", surface=" << surface;
        }
        if (window->isClient()) {
            if (!window->isPopupWindow()) {
                debug << ", caption=" << window->caption();
            }
            if (window->transientFor()) {
                debug << ", transientFor=" << window->transientFor();
            }
        }
        if (debug.verbosity() > 2) {
            debug << ", frameGeometry=" << window->frameGeometry();
            debug << ", resourceName=" << window->resourceName();
            debug << ", resourceClass=" << window->resourceClass();
        }
        debug << ')';
    } else {
        debug << "Window(0x0)";
    }
    return debug;
}

void Window::detectShape(xcb_window_t id)
{
    const bool wasShape = is_shape;
    is_shape = Xcb::Extensions::self()->hasShape(id);
    if (wasShape != is_shape) {
        Q_EMIT shapedChanged();
    }
}

// used only by Deleted::copy()
void Window::copyToDeleted(Window *c)
{
    m_internalId = c->internalId();
    m_bufferGeometry = c->m_bufferGeometry;
    m_frameGeometry = c->m_frameGeometry;
    m_clientGeometry = c->m_clientGeometry;
    m_visual = c->m_visual;
    bit_depth = c->bit_depth;
    info = c->info;
    m_client.reset(c->m_client, false);
    ready_for_painting = c->ready_for_painting;
    is_shape = c->is_shape;
    m_effectWindow = std::move(c->m_effectWindow);
    if (m_effectWindow != nullptr) {
        m_effectWindow->setWindow(this);
    }
    m_windowItem = std::move(c->m_windowItem);
    m_shadow = std::move(c->m_shadow);
    if (m_shadow) {
        m_shadow->setWindow(this);
    }
    resource_name = c->resourceName();
    resource_class = c->resourceClass();
    m_clientMachine = c->m_clientMachine;
    m_clientMachine->setParent(this);
    m_wmClientLeader = c->wmClientLeader();
    opaque_region = c->opaqueRegion();
    m_output = c->m_output;
    m_skipCloseAnimation = c->m_skipCloseAnimation;
    m_internalFBO = c->m_internalFBO;
    m_internalImage = c->m_internalImage;
    m_opacity = c->m_opacity;
    m_shapeRegionIsValid = c->m_shapeRegionIsValid;
    m_shapeRegion = c->m_shapeRegion;
    m_stackingOrder = c->m_stackingOrder;
}

// before being deleted, remove references to everything that's now
// owner by Deleted
void Window::disownDataPassedToDeleted()
{
    info = nullptr;
}

QRectF Window::visibleGeometry() const
{
    if (const WindowItem *item = windowItem()) {
        return item->mapToGlobal(item->boundingRect());
    }
    return QRectF();
}

Xcb::Property Window::fetchWmClientLeader() const
{
    return Xcb::Property(false, window(), atoms->wm_client_leader, XCB_ATOM_WINDOW, 0, 10000);
}

void Window::readWmClientLeader(Xcb::Property &prop)
{
    m_wmClientLeader = prop.value<xcb_window_t>(window());
}

void Window::getWmClientLeader()
{
    auto prop = fetchWmClientLeader();
    readWmClientLeader(prop);
}

/**
 * Returns sessionId for this window,
 * taken either from its window or from the leader window.
 */
QByteArray Window::sessionId() const
{
    QByteArray result = Xcb::StringProperty(window(), atoms->sm_client_id);
    if (result.isEmpty() && m_wmClientLeader && m_wmClientLeader != window()) {
        result = Xcb::StringProperty(m_wmClientLeader, atoms->sm_client_id);
    }
    return result;
}

/**
 * Returns command property for this window,
 * taken either from its window or from the leader window.
 */
QString Window::wmCommand()
{
    QByteArray result = Xcb::StringProperty(window(), XCB_ATOM_WM_COMMAND);
    if (result.isEmpty() && m_wmClientLeader && m_wmClientLeader != window()) {
        result = Xcb::StringProperty(m_wmClientLeader, XCB_ATOM_WM_COMMAND);
    }
    result.replace(0, ' ');
    return result;
}

void Window::getWmClientMachine()
{
    m_clientMachine->resolve(window(), wmClientLeader());
}

/**
 * Returns client machine for this window,
 * taken either from its window or from the leader window.
 */
QString Window::wmClientMachine(bool use_localhost) const
{
    if (!m_clientMachine) {
        // this should never happen
        return QString();
    }
    if (use_localhost && m_clientMachine->isLocal()) {
        // special name for the local machine (localhost)
        return ClientMachine::localhost();
    }
    return m_clientMachine->hostName();
}

/**
 * Returns client leader window for this client.
 * Returns the client window itself if no leader window is defined.
 */
xcb_window_t Window::wmClientLeader() const
{
    if (m_wmClientLeader != XCB_WINDOW_NONE) {
        return m_wmClientLeader;
    }
    return window();
}

void Window::getResourceClass()
{
    if (!info) {
        return;
    }
    setResourceClass(QString::fromLatin1(info->windowClassName()).toLower(), QString::fromLatin1(info->windowClassClass()).toLower());
}

void Window::setResourceClass(const QString &name, const QString &className)
{
    resource_name = name;
    resource_class = className;
    Q_EMIT windowClassChanged();
}

bool Window::resourceMatch(const Window *c1, const Window *c2)
{
    return c1->resourceClass() == c2->resourceClass();
}

qreal Window::opacity() const
{
    return m_opacity;
}

void Window::setOpacity(qreal opacity)
{
    opacity = std::clamp(opacity, 0.0, 1.0);
    if (m_opacity == opacity) {
        return;
    }
    const qreal oldOpacity = m_opacity;
    m_opacity = opacity;
    Q_EMIT opacityChanged(this, oldOpacity);
}

bool Window::setupCompositing()
{
    WorkspaceScene *scene = Compositor::self()->scene();
    if (!scene) {
        return false;
    }

    m_effectWindow = std::make_unique<EffectWindowImpl>(this);
    updateShadow();

    m_windowItem = createItem(scene);
    m_effectWindow->setWindowItem(m_windowItem.get());

    connect(windowItem(), &WindowItem::positionChanged, this, &Window::visibleGeometryChanged);
    connect(windowItem(), &WindowItem::boundingRectChanged, this, &Window::visibleGeometryChanged);

    return true;
}

void Window::finishCompositing(ReleaseReason releaseReason)
{
    // If the X11 window has been destroyed, avoid calling XDamageDestroy.
    if (releaseReason != ReleaseReason::Destroyed) {
        if (SurfaceItemX11 *item = qobject_cast<SurfaceItemX11 *>(surfaceItem())) {
            item->destroyDamage();
        }
    }
    m_shadow.reset();
    m_effectWindow.reset();
    m_windowItem.reset();
}

void Window::addWorkspaceRepaint(int x, int y, int w, int h)
{
    addWorkspaceRepaint(QRectF(x, y, w, h));
}

void Window::addWorkspaceRepaint(const QRectF &r2)
{
    if (Compositor::compositing()) {
        Compositor::self()->scene()->addRepaint(r2.toAlignedRect());
    }
}

void Window::addWorkspaceRepaint(const QRegion &region)
{
    if (Compositor::compositing()) {
        Compositor::self()->scene()->addRepaint(region);
    }
}

void Window::setReadyForPainting()
{
    if (!ready_for_painting) {
        ready_for_painting = true;
        if (Compositor::compositing()) {
            Q_EMIT windowShown(this);
        }
    }
}

int Window::screen() const
{
    return workspace()->outputs().indexOf(m_output);
}

Output *Window::output() const
{
    return m_output;
}

void Window::setOutput(Output *output)
{
    if (m_output != output) {
        m_output = output;
        Q_EMIT screenChanged();
    }
}

bool Window::isOnActiveOutput() const
{
    return isOnOutput(workspace()->activeOutput());
}

bool Window::isOnOutput(Output *output) const
{
    return output->geometry().intersects(frameGeometry().toRect());
}

Shadow *Window::shadow() const
{
    return m_shadow.get();
}

void Window::updateShadow()
{
    if (!Compositor::compositing()) {
        return;
    }
    if (m_shadow) {
        if (!m_shadow->updateShadow()) {
            m_shadow.reset();
        }
        Q_EMIT shadowChanged();
    } else {
        m_shadow = Shadow::createShadow(this);
        if (m_shadow) {
            Q_EMIT shadowChanged();
        }
    }
}

SurfaceItem *Window::surfaceItem() const
{
    if (m_windowItem) {
        return m_windowItem->surfaceItem();
    }
    return nullptr;
}

bool Window::wantsShadowToBeRendered() const
{
    return !isFullScreen() && maximizeMode() != MaximizeFull;
}

void Window::getWmOpaqueRegion()
{
    if (!info) {
        return;
    }

    const auto rects = info->opaqueRegion();
    QRegion new_opaque_region;
    for (const auto &r : rects) {
        new_opaque_region |= Xcb::fromXNative(QRect(r.pos.x, r.pos.y, r.size.width, r.size.height)).toRect();
    }
    opaque_region = new_opaque_region;
}

QVector<QRectF> Window::shapeRegion() const
{
    if (m_shapeRegionIsValid) {
        return m_shapeRegion;
    }

    const QRectF bufferGeometry = this->bufferGeometry();

    if (shape()) {
        auto cookie = xcb_shape_get_rectangles_unchecked(kwinApp()->x11Connection(), frameId(), XCB_SHAPE_SK_BOUNDING);
        UniqueCPtr<xcb_shape_get_rectangles_reply_t> reply(xcb_shape_get_rectangles_reply(kwinApp()->x11Connection(), cookie, nullptr));
        if (reply) {
            m_shapeRegion.clear();
            const xcb_rectangle_t *rects = xcb_shape_get_rectangles_rectangles(reply.get());
            const int rectCount = xcb_shape_get_rectangles_rectangles_length(reply.get());
            for (int i = 0; i < rectCount; ++i) {
                QRectF region = Xcb::fromXNative(QRect(rects[i].x, rects[i].y, rects[i].width, rects[i].height)).toAlignedRect();
                // make sure the shape is sane (X is async, maybe even XShape is broken)
                region = region.intersected(QRectF(QPointF(0, 0), bufferGeometry.size()));

                m_shapeRegion += region;
            }
        } else {
            m_shapeRegion.clear();
        }
    } else {
        m_shapeRegion = {QRectF(0, 0, bufferGeometry.width(), bufferGeometry.height())};
    }

    m_shapeRegionIsValid = true;
    return m_shapeRegion;
}

void Window::discardShapeRegion()
{
    m_shapeRegionIsValid = false;
    m_shapeRegion.clear();
}

bool Window::isClient() const
{
    return false;
}

bool Window::isUnmanaged() const
{
    return false;
}

bool Window::isDeleted() const
{
    return false;
}

bool Window::isOnCurrentActivity() const
{
#if KWIN_BUILD_ACTIVITIES
    if (!Workspace::self()->activities()) {
        return true;
    }
    return isOnActivity(Workspace::self()->activities()->current());
#else
    return true;
#endif
}

void Window::elevate(bool elevate)
{
    if (!effectWindow()) {
        return;
    }
    effectWindow()->elevate(elevate);
    addWorkspaceRepaint(visibleGeometry());
}

pid_t Window::pid() const
{
    if (!info) {
        return -1;
    }
    return info->pid();
}

xcb_window_t Window::frameId() const
{
    return m_client;
}

Xcb::Property Window::fetchSkipCloseAnimation() const
{
    return Xcb::Property(false, window(), atoms->kde_skip_close_animation, XCB_ATOM_CARDINAL, 0, 1);
}

void Window::readSkipCloseAnimation(Xcb::Property &property)
{
    setSkipCloseAnimation(property.toBool());
}

void Window::getSkipCloseAnimation()
{
    Xcb::Property property = fetchSkipCloseAnimation();
    readSkipCloseAnimation(property);
}

bool Window::skipsCloseAnimation() const
{
    return m_skipCloseAnimation;
}

void Window::setSkipCloseAnimation(bool set)
{
    if (set == m_skipCloseAnimation) {
        return;
    }
    m_skipCloseAnimation = set;
    Q_EMIT skipCloseAnimationChanged();
}

KWaylandServer::SurfaceInterface *Window::surface() const
{
    return m_surface;
}

void Window::setSurface(KWaylandServer::SurfaceInterface *surface)
{
    if (m_surface == surface) {
        return;
    }
    m_surface = surface;
    m_pendingSurfaceId = 0;
    Q_EMIT surfaceChanged();
}

int Window::stackingOrder() const
{
    return m_stackingOrder;
}

void Window::setStackingOrder(int order)
{
    if (m_stackingOrder != order) {
        m_stackingOrder = order;
        Q_EMIT stackingOrderChanged();
    }
}

QString Window::windowRole() const
{
    if (!info) {
        return {};
    }
    return QString::fromLatin1(info->windowRole());
}

void Window::setDepth(int depth)
{
    if (bit_depth == depth) {
        return;
    }
    const bool oldAlpha = hasAlpha();
    bit_depth = depth;
    if (oldAlpha != hasAlpha()) {
        Q_EMIT hasAlphaChanged();
    }
}

QRegion Window::inputShape() const
{
    if (m_surface) {
        return m_surface->input();
    } else {
        // TODO: maybe also for X11?
        return QRegion();
    }
}

QMatrix4x4 Window::inputTransformation() const
{
    QMatrix4x4 m;
    m.translate(-x(), -y());
    return m;
}

bool Window::hitTest(const QPointF &point) const
{
    if (isDecorated()) {
        if (m_decoration.inputRegion.contains(flooredPoint(mapToFrame(point)))) {
            return true;
        }
    }
    if (m_surface && m_surface->isMapped()) {
        return m_surface->inputSurfaceAt(mapToLocal(point));
    }
    const QPointF relativePoint = point - inputGeometry().topLeft();
    return relativePoint.x() >= 0 && relativePoint.y() >= 0 && relativePoint.x() < inputGeometry().width() && relativePoint.y() < inputGeometry().height();
}

QPointF Window::mapToFrame(const QPointF &point) const
{
    return point - frameGeometry().topLeft();
}

QPointF Window::mapToLocal(const QPointF &point) const
{
    return point - bufferGeometry().topLeft();
}

QPointF Window::mapFromLocal(const QPointF &point) const
{
    return point + bufferGeometry().topLeft();
}

QRectF Window::inputGeometry() const
{
    if (isDecorated()) {
        return frameGeometry() + decoration()->resizeOnlyBorders();
    }
    return frameGeometry();
}

bool Window::isLocalhost() const
{
    if (!m_clientMachine) {
        return true;
    }
    return m_clientMachine->isLocal();
}

QMargins Window::frameMargins() const
{
    return QMargins(borderLeft(), borderTop(), borderRight(), borderBottom());
}

bool Window::isOnDesktop(VirtualDesktop *desktop) const
{
    return isOnAllDesktops() || desktops().contains(desktop);
}

bool Window::isOnDesktop(int d) const
{
    return isOnDesktop(VirtualDesktopManager::self()->desktopForX11Id(d));
}

bool Window::isOnCurrentDesktop() const
{
    return isOnDesktop(VirtualDesktopManager::self()->currentDesktop());
}

void Window::updateMouseGrab()
{
}

bool Window::belongToSameApplication(const Window *c1, const Window *c2, SameApplicationChecks checks)
{
    return c1->belongsToSameApplication(c2, checks);
}

bool Window::isTransient() const
{
    return false;
}

xcb_timestamp_t Window::userTime() const
{
    return XCB_TIME_CURRENT_TIME;
}

void Window::setSkipSwitcher(bool set)
{
    set = rules()->checkSkipSwitcher(set);
    if (set == skipSwitcher()) {
        return;
    }
    m_skipSwitcher = set;
    doSetSkipSwitcher();
    updateWindowRules(Rules::SkipSwitcher);
    Q_EMIT skipSwitcherChanged();
}

void Window::setSkipPager(bool b)
{
    b = rules()->checkSkipPager(b);
    if (b == skipPager()) {
        return;
    }
    m_skipPager = b;
    doSetSkipPager();
    updateWindowRules(Rules::SkipPager);
    Q_EMIT skipPagerChanged();
}

void Window::doSetSkipPager()
{
}

void Window::setSkipTaskbar(bool b)
{
    int was_wants_tab_focus = wantsTabFocus();
    if (b == skipTaskbar()) {
        return;
    }
    m_skipTaskbar = b;
    doSetSkipTaskbar();
    updateWindowRules(Rules::SkipTaskbar);
    if (was_wants_tab_focus != wantsTabFocus()) {
        Workspace::self()->focusChain()->update(this, isActive() ? FocusChain::MakeFirst : FocusChain::Update);
    }
    Q_EMIT skipTaskbarChanged();
}

void Window::setOriginalSkipTaskbar(bool b)
{
    m_originalSkipTaskbar = rules()->checkSkipTaskbar(b);
    setSkipTaskbar(m_originalSkipTaskbar);
}

void Window::doSetSkipTaskbar()
{
}

void Window::doSetSkipSwitcher()
{
}

void Window::setIcon(const QIcon &icon)
{
    m_icon = icon;
    Q_EMIT iconChanged();
}

void Window::setActive(bool act)
{
    if (isZombie()) {
        return;
    }
    if (m_active == act) {
        return;
    }
    m_active = act;
    const int ruledOpacity = m_active
        ? rules()->checkOpacityActive(qRound(opacity() * 100.0))
        : rules()->checkOpacityInactive(qRound(opacity() * 100.0));
    setOpacity(ruledOpacity / 100.0);
    workspace()->setActiveWindow(act ? this : nullptr);

    if (!m_active) {
        cancelAutoRaise();
    }

    if (!m_active && shadeMode() == ShadeActivated) {
        setShade(ShadeNormal);
    }

    StackingUpdatesBlocker blocker(workspace());
    updateLayer(); // active windows may get different layer
    auto mainwindows = mainWindows();
    for (auto it = mainwindows.constBegin(); it != mainwindows.constEnd(); ++it) {
        if ((*it)->isFullScreen()) { // fullscreens go high even if their transient is active
            (*it)->updateLayer();
        }
    }

    doSetActive();
    Q_EMIT activeChanged();
    updateMouseGrab();
}

void Window::doSetActive()
{
}

bool Window::isZombie() const
{
    return m_zombie;
}

void Window::markAsZombie()
{
    Q_ASSERT(!m_zombie);
    m_zombie = true;
}

Layer Window::layer() const
{
    if (m_layer == UnknownLayer) {
        const_cast<Window *>(this)->m_layer = belongsToLayer();
    }
    return m_layer;
}

void Window::updateLayer()
{
    if (layer() == belongsToLayer()) {
        return;
    }
    StackingUpdatesBlocker blocker(workspace());
    invalidateLayer(); // invalidate, will be updated when doing restacking
    for (auto it = transients().constBegin(), end = transients().constEnd(); it != end; ++it) {
        (*it)->updateLayer();
    }
}

void Window::invalidateLayer()
{
    m_layer = UnknownLayer;
}

Layer Window::belongsToLayer() const
{
    // NOTICE while showingDesktop, desktops move to the AboveLayer
    // (interchangeable w/ eg. yakuake etc. which will at first remain visible)
    // and the docks move into the NotificationLayer (which is between Above- and
    // ActiveLayer, so that active fullscreen windows will still cover everything)
    // Since the desktop is also activated, nothing should be in the ActiveLayer, though
    if (isUnmanaged() || isInternal()) {
        return UnmanagedLayer;
    }
    if (isLockScreen() && !waylandServer()) {
        return UnmanagedLayer;
    }
    if (isInputMethod()) {
        return UnmanagedLayer;
    }
    if (isLockScreenOverlay() && waylandServer() && waylandServer()->isScreenLocked()) {
        return UnmanagedLayer;
    }
    if (isDesktop()) {
        return workspace()->showingDesktop() ? AboveLayer : DesktopLayer;
    }
    if (isSplash()) { // no damn annoying splashscreens
        return NormalLayer; // getting in the way of everything else
    }
    if (isDock() || isAppletPopup()) {
        if (workspace()->showingDesktop()) {
            return NotificationLayer;
        }
        return layerForDock();
    }
    if (isPopupWindow()) {
        return PopupLayer;
    }
    if (isOnScreenDisplay()) {
        return OnScreenDisplayLayer;
    }
    if (isNotification()) {
        return NotificationLayer;
    }
    if (isCriticalNotification()) {
        return CriticalNotificationLayer;
    }
    if (workspace()->showingDesktop() && belongsToDesktop()) {
        return AboveLayer;
    }
    if (keepBelow()) {
        return BelowLayer;
    }
    if (isActiveFullScreen()) {
        return ActiveLayer;
    }
    if (keepAbove()) {
        return AboveLayer;
    }

    return NormalLayer;
}

bool Window::belongsToDesktop() const
{
    return false;
}

Layer Window::layerForDock() const
{
    // slight hack for the 'allow window to cover panel' Kicker setting
    // don't move keepbelow docks below normal window, but only to the same
    // layer, so that both may be raised to cover the other
    if (keepBelow()) {
        return NormalLayer;
    }
    if (keepAbove()) { // slight hack for the autohiding panels
        return AboveLayer;
    }
    return DockLayer;
}

void Window::setKeepAbove(bool b)
{
    b = rules()->checkKeepAbove(b);
    if (b && !rules()->checkKeepBelow(false)) {
        setKeepBelow(false);
    }
    if (b == keepAbove()) {
        return;
    }
    m_keepAbove = b;
    doSetKeepAbove();
    updateLayer();
    updateWindowRules(Rules::Above);

    Q_EMIT keepAboveChanged(m_keepAbove);
}

void Window::doSetKeepAbove()
{
}

void Window::setKeepBelow(bool b)
{
    b = rules()->checkKeepBelow(b);
    if (b && !rules()->checkKeepAbove(false)) {
        setKeepAbove(false);
    }
    if (b == keepBelow()) {
        return;
    }
    m_keepBelow = b;
    doSetKeepBelow();
    updateLayer();
    updateWindowRules(Rules::Below);

    Q_EMIT keepBelowChanged(m_keepBelow);
}

void Window::doSetKeepBelow()
{
}

void Window::startAutoRaise()
{
    delete m_autoRaiseTimer;
    m_autoRaiseTimer = new QTimer(this);
    connect(m_autoRaiseTimer, &QTimer::timeout, this, &Window::autoRaise);
    m_autoRaiseTimer->setSingleShot(true);
    m_autoRaiseTimer->start(options->autoRaiseInterval());
}

void Window::cancelAutoRaise()
{
    delete m_autoRaiseTimer;
    m_autoRaiseTimer = nullptr;
}

void Window::autoRaise()
{
    workspace()->raiseWindow(this);
    cancelAutoRaise();
}

bool Window::isMostRecentlyRaised() const
{
    // The last window in the unconstrained stacking order is the most recently raised one.
    return workspace()->topWindowOnDesktop(VirtualDesktopManager::self()->currentDesktop(), nullptr, true, false) == this;
}

bool Window::wantsTabFocus() const
{
    return (isNormalWindow() || isDialog() || isAppletPopup()) && wantsInput();
}

bool Window::isSpecialWindow() const
{
    // TODO
    return isDesktop() || isDock() || isSplash() || isToolbar() || isNotification() || isOnScreenDisplay() || isCriticalNotification();
}

void Window::demandAttention(bool set)
{
    if (isActive()) {
        set = false;
    }
    if (m_demandsAttention == set) {
        return;
    }
    m_demandsAttention = set;
    doSetDemandsAttention();
    workspace()->windowAttentionChanged(this, set);
    Q_EMIT demandsAttentionChanged();
}

void Window::doSetDemandsAttention()
{
}

void Window::setDesktop(int desktop)
{
    const int numberOfDesktops = VirtualDesktopManager::self()->count();
    if (desktop != NET::OnAllDesktops) { // Do range check
        desktop = std::max(1, std::min(numberOfDesktops, desktop));
    }

    QVector<VirtualDesktop *> desktops;
    if (desktop != NET::OnAllDesktops) {
        desktops << VirtualDesktopManager::self()->desktopForX11Id(desktop);
    }
    setDesktops(desktops);
}

void Window::setDesktops(QVector<VirtualDesktop *> desktops)
{
    // on x11 we can have only one desktop at a time
    if (kwinApp()->operationMode() == Application::OperationModeX11 && desktops.size() > 1) {
        desktops = QVector<VirtualDesktop *>({desktops.last()});
    }

    desktops = rules()->checkDesktops(desktops);
    if (desktops == m_desktops) {
        return;
    }

    int was_desk = Window::desktop();
    const bool wasOnCurrentDesktop = isOnCurrentDesktop() && was_desk >= 0;

    m_desktops = desktops;

    if (windowManagementInterface()) {
        if (m_desktops.isEmpty()) {
            windowManagementInterface()->setOnAllDesktops(true);
        } else {
            windowManagementInterface()->setOnAllDesktops(false);
            auto currentDesktops = windowManagementInterface()->plasmaVirtualDesktops();
            for (auto desktop : std::as_const(m_desktops)) {
                if (!currentDesktops.contains(desktop->id())) {
                    windowManagementInterface()->addPlasmaVirtualDesktop(desktop->id());
                } else {
                    currentDesktops.removeOne(desktop->id());
                }
            }
            for (const auto &desktopId : std::as_const(currentDesktops)) {
                windowManagementInterface()->removePlasmaVirtualDesktop(desktopId);
            }
        }
    }
    if (info) {
        info->setDesktop(desktop());
    }

    if ((was_desk == NET::OnAllDesktops) != (desktop() == NET::OnAllDesktops)) {
        // onAllDesktops changed
        workspace()->updateOnAllDesktopsOfTransients(this);
    }

    auto transients_stacking_order = workspace()->ensureStackingOrder(transients());
    for (auto it = transients_stacking_order.constBegin(); it != transients_stacking_order.constEnd(); ++it) {
        (*it)->setDesktops(desktops);
    }

    if (isModal()) // if a modal dialog is moved, move the mainwindow with it as otherwise
                   // the (just moved) modal dialog will confusingly return to the mainwindow with
                   // the next desktop change
    {
        const auto windows = mainWindows();
        for (Window *other : windows) {
            other->setDesktops(desktops);
        }
    }

    doSetDesktop();

    Workspace::self()->focusChain()->update(this, FocusChain::MakeFirst);
    updateWindowRules(Rules::Desktops);

    Q_EMIT desktopChanged();
    if (wasOnCurrentDesktop != isOnCurrentDesktop()) {
        Q_EMIT desktopPresenceChanged(this, was_desk);
    }
    Q_EMIT x11DesktopIdsChanged();
}

void Window::doSetDesktop()
{
}

void Window::doSetOnActivities(const QStringList &activityList)
{
}

void Window::enterDesktop(VirtualDesktop *virtualDesktop)
{
    if (m_desktops.contains(virtualDesktop)) {
        return;
    }
    auto desktops = m_desktops;
    desktops.append(virtualDesktop);
    setDesktops(desktops);
}

void Window::leaveDesktop(VirtualDesktop *virtualDesktop)
{
    QVector<VirtualDesktop *> currentDesktops;
    if (m_desktops.isEmpty()) {
        currentDesktops = VirtualDesktopManager::self()->desktops();
    } else {
        currentDesktops = m_desktops;
    }

    if (!currentDesktops.contains(virtualDesktop)) {
        return;
    }
    auto desktops = currentDesktops;
    desktops.removeOne(virtualDesktop);
    setDesktops(desktops);
}

void Window::setOnAllDesktops(bool b)
{
    if (b == isOnAllDesktops()) {
        return;
    }
    if (b) {
        setDesktops({});
    } else {
        setDesktops({VirtualDesktopManager::self()->currentDesktop()});
    }
}

int Window::desktop() const
{
    return m_desktops.isEmpty() ? (int)NET::OnAllDesktops : m_desktops.last()->x11DesktopNumber();
}

QVector<VirtualDesktop *> Window::desktops() const
{
    return m_desktops;
}

QVector<uint> Window::x11DesktopIds() const
{
    const auto desks = desktops();
    QVector<uint> x11Ids;
    x11Ids.reserve(desks.count());
    std::transform(desks.constBegin(), desks.constEnd(),
                   std::back_inserter(x11Ids),
                   [](const VirtualDesktop *vd) {
                       return vd->x11DesktopNumber();
                   });
    return x11Ids;
}

QStringList Window::desktopIds() const
{
    const auto desks = desktops();
    QStringList ids;
    ids.reserve(desks.count());
    std::transform(desks.constBegin(), desks.constEnd(),
                   std::back_inserter(ids),
                   [](const VirtualDesktop *vd) {
                       return vd->id();
                   });
    return ids;
};

ShadeMode Window::shadeMode() const
{
    return m_shadeMode;
}

bool Window::isShadeable() const
{
    return false;
}

void Window::setShade(bool set)
{
    set ? setShade(ShadeNormal) : setShade(ShadeNone);
}

void Window::setShade(ShadeMode mode)
{
    if (!isShadeable()) {
        return;
    }
    if (mode == ShadeHover && isInteractiveMove()) {
        return; // causes geometry breaks and is probably nasty
    }
    if (isSpecialWindow() || !isDecorated()) {
        mode = ShadeNone;
    }

    mode = rules()->checkShade(mode);
    if (m_shadeMode == mode) {
        return;
    }

    const bool wasShade = isShade();
    const ShadeMode previousShadeMode = shadeMode();
    m_shadeMode = mode;

    if (wasShade == isShade()) {
        // Decoration may want to update after e.g. hover-shade changes
        Q_EMIT shadeChanged();
        return; // No real change in shaded state
    }

    Q_ASSERT(isDecorated());
    GeometryUpdatesBlocker blocker(this);

    doSetShade(previousShadeMode);
    updateWindowRules(Rules::Shade);

    Q_EMIT shadeChanged();
}

void Window::doSetShade(ShadeMode previousShadeMode)
{
}

void Window::shadeHover()
{
    setShade(ShadeHover);
    cancelShadeHoverTimer();
}

void Window::shadeUnhover()
{
    setShade(ShadeNormal);
    cancelShadeHoverTimer();
}

void Window::startShadeHoverTimer()
{
    if (!isShade()) {
        return;
    }
    m_shadeHoverTimer = new QTimer(this);
    connect(m_shadeHoverTimer, &QTimer::timeout, this, &Window::shadeHover);
    m_shadeHoverTimer->setSingleShot(true);
    m_shadeHoverTimer->start(options->shadeHoverInterval());
}

void Window::startShadeUnhoverTimer()
{
    if (m_shadeMode == ShadeHover && !isInteractiveMoveResize() && !isInteractiveMoveResizePointerButtonDown()) {
        m_shadeHoverTimer = new QTimer(this);
        connect(m_shadeHoverTimer, &QTimer::timeout, this, &Window::shadeUnhover);
        m_shadeHoverTimer->setSingleShot(true);
        m_shadeHoverTimer->start(options->shadeHoverInterval());
    }
}

void Window::cancelShadeHoverTimer()
{
    delete m_shadeHoverTimer;
    m_shadeHoverTimer = nullptr;
}

void Window::toggleShade()
{
    // If the mode is ShadeHover or ShadeActive, cancel shade too.
    setShade(shadeMode() == ShadeNone ? ShadeNormal : ShadeNone);
}

Qt::Edge Window::titlebarPosition() const
{
    // TODO: still needed, remove?
    return Qt::TopEdge;
}

bool Window::titlebarPositionUnderMouse() const
{
    if (!isDecorated()) {
        return false;
    }
    const auto sectionUnderMouse = decoration()->sectionUnderMouse();
    if (sectionUnderMouse == Qt::TitleBarArea) {
        return true;
    }
    // check other sections based on titlebarPosition
    switch (titlebarPosition()) {
    case Qt::TopEdge:
        return (sectionUnderMouse == Qt::TopLeftSection || sectionUnderMouse == Qt::TopSection || sectionUnderMouse == Qt::TopRightSection);
    case Qt::LeftEdge:
        return (sectionUnderMouse == Qt::TopLeftSection || sectionUnderMouse == Qt::LeftSection || sectionUnderMouse == Qt::BottomLeftSection);
    case Qt::RightEdge:
        return (sectionUnderMouse == Qt::BottomRightSection || sectionUnderMouse == Qt::RightSection || sectionUnderMouse == Qt::TopRightSection);
    case Qt::BottomEdge:
        return (sectionUnderMouse == Qt::BottomLeftSection || sectionUnderMouse == Qt::BottomSection || sectionUnderMouse == Qt::BottomRightSection);
    default:
        // nothing
        return false;
    }
}

void Window::setMinimized(bool set)
{
    set ? minimize() : unminimize();
}

void Window::minimize(bool avoid_animation)
{
    if (!isMinimizable() || isMinimized()) {
        return;
    }

    m_minimized = true;
    doMinimize();

    updateWindowRules(Rules::Minimize);

    if (options->moveMinimizedWindowsToEndOfTabBoxFocusChain()) {
        Workspace::self()->focusChain()->update(this, FocusChain::MakeFirstMinimized);
    }

    // TODO: merge signal with s_minimized
    Q_EMIT clientMinimized(this, !avoid_animation);
    Q_EMIT minimizedChanged();
}

void Window::unminimize(bool avoid_animation)
{
    if (!isMinimized()) {
        return;
    }

    if (rules()->checkMinimize(false)) {
        return;
    }

    m_minimized = false;
    doMinimize();

    updateWindowRules(Rules::Minimize);
    Q_EMIT clientUnminimized(this, !avoid_animation);
    Q_EMIT minimizedChanged();
}

void Window::doMinimize()
{
}

QPalette Window::palette()
{
    ensurePalette();
    return m_palette->palette();
}

const Decoration::DecorationPalette *Window::decorationPalette()
{
    ensurePalette();
    return m_palette.get();
}

QString Window::preferredColorScheme() const
{
    return rules()->checkDecoColor(QString());
}

QString Window::colorScheme() const
{
    return m_colorScheme;
}

void Window::setColorScheme(const QString &colorScheme)
{
    QString requestedColorScheme = colorScheme;
    if (requestedColorScheme.isEmpty()) {
        requestedColorScheme = QStringLiteral("kdeglobals");
    }

    if (m_colorScheme == requestedColorScheme) {
        return;
    }

    m_colorScheme = requestedColorScheme;

    if (m_palette) {
        disconnect(m_palette.get(), &Decoration::DecorationPalette::changed, this, &Window::handlePaletteChange);
        m_palette.reset();

        // If there already was a palette, re-create it right away
        // so the signals for repainting the decoration are emitted.
        ensurePalette();
    }

    Q_EMIT colorSchemeChanged();
}

void Window::updateColorScheme()
{
    setColorScheme(preferredColorScheme());
}

void Window::ensurePalette()
{
    if (m_palette) {
        return;
    }

    auto it = s_palettes.find(m_colorScheme);

    if (it == s_palettes.end() || it->expired()) {
        m_palette = std::make_shared<Decoration::DecorationPalette>(m_colorScheme);
        if (m_palette->isValid()) {
            s_palettes[m_colorScheme] = m_palette;
        } else {
            if (!s_defaultPalette) {
                s_defaultPalette = std::make_shared<Decoration::DecorationPalette>(QStringLiteral("kdeglobals"));
                s_palettes[QStringLiteral("kdeglobals")] = s_defaultPalette;
            }

            m_palette = s_defaultPalette;
        }

        if (m_colorScheme == QStringLiteral("kdeglobals")) {
            s_defaultPalette = m_palette;
        }
    } else {
        m_palette = it->lock();
    }

    connect(m_palette.get(), &Decoration::DecorationPalette::changed, this, &Window::handlePaletteChange);

    handlePaletteChange();
}

void Window::handlePaletteChange()
{
    Q_EMIT paletteChanged(palette());
}

QRectF Window::keepInArea(QRectF geometry, QRectF area, bool partial)
{
    if (partial) {
        // increase the area so that can have only 100 pixels in the area
        const QRectF geometry = moveResizeGeometry();
        area.setLeft(std::min(area.left() - geometry.width() + 100, area.left()));
        area.setTop(std::min(area.top() - geometry.height() + 100, area.top()));
        area.setRight(std::max(area.right() + geometry.width() - 100, area.right()));
        area.setBottom(std::max(area.bottom() + geometry.height() - 100, area.bottom()));
    }
    if (!partial) {
        // resize to fit into area
        if (area.width() < geometry.width() || area.height() < geometry.height()) {
            geometry = resizeWithChecks(geometry, geometry.size().boundedTo(area.size()));
        }
    }

    if (geometry.right() > area.right() && geometry.width() <= area.width()) {
        geometry.moveRight(area.right());
    }
    if (geometry.bottom() > area.bottom() && geometry.height() <= area.height()) {
        geometry.moveBottom(area.bottom());
    }

    if (geometry.left() < area.left()) {
        geometry.moveLeft(area.left());
    }
    if (geometry.top() < area.top()) {
        geometry.moveTop(area.top());
    }
    return geometry;
}

void Window::keepInArea(QRectF area, bool partial)
{
    moveResize(keepInArea(moveResizeGeometry(), area, partial));
}

/**
 * Returns the maximum client size, not the maximum frame size.
 */
QSizeF Window::maxSize() const
{
    return rules()->checkMaxSize(QSize(INT_MAX, INT_MAX));
}

/**
 * Returns the minimum client size, not the minimum frame size.
 */
QSizeF Window::minSize() const
{
    return rules()->checkMinSize(QSize(0, 0));
}

void Window::blockGeometryUpdates(bool block)
{
    if (block) {
        if (m_blockGeometryUpdates == 0) {
            m_pendingMoveResizeMode = MoveResizeMode::None;
        }
        ++m_blockGeometryUpdates;
    } else {
        if (--m_blockGeometryUpdates == 0) {
            if (m_pendingMoveResizeMode != MoveResizeMode::None) {
                moveResizeInternal(moveResizeGeometry(), m_pendingMoveResizeMode);
                m_pendingMoveResizeMode = MoveResizeMode::None;
            }
        }
    }
}

void Window::maximize(MaximizeMode mode)
{
    qCWarning(KWIN_CORE, "%s doesn't support setting maximized state", metaObject()->className());
}

void Window::setMaximize(bool vertically, bool horizontally)
{
    MaximizeMode mode = MaximizeRestore;
    if (vertically) {
        mode = MaximizeMode(mode | MaximizeVertical);
    }
    if (horizontally) {
        mode = MaximizeMode(mode | MaximizeHorizontal);
    }
    setTile(nullptr);
    maximize(mode);
}

bool Window::startInteractiveMoveResize()
{
    Q_ASSERT(!isInteractiveMoveResize());
    Q_ASSERT(QWidget::keyboardGrabber() == nullptr);
    Q_ASSERT(QWidget::mouseGrabber() == nullptr);
    stopDelayedInteractiveMoveResize();
    if (QApplication::activePopupWidget() != nullptr) {
        return false; // popups have grab
    }
    if (isRequestedFullScreen() && (workspace()->outputs().count() < 2 || !isMovableAcrossScreens())) {
        return false;
    }
    if (!doStartInteractiveMoveResize()) {
        return false;
    }

    invalidateDecorationDoubleClickTimer();

    setInteractiveMoveResize(true);
    workspace()->setMoveResizeWindow(this);

    m_interactiveMoveResize.initialGeometry = moveResizeGeometry();
    m_interactiveMoveResize.startOutput = moveResizeOutput();
    m_interactiveMoveResize.initialMaximizeMode = requestedMaximizeMode();
    m_interactiveMoveResize.initialQuickTileMode = quickTileMode();
    m_interactiveMoveResize.initialGeometryRestore = geometryRestore();

    if (requestedMaximizeMode() != MaximizeRestore) {
        switch (interactiveMoveResizeGravity()) {
        case Gravity::Left:
        case Gravity::Right:
            // Quit maximized horizontally state if the window is resized horizontally.
            if (requestedMaximizeMode() & MaximizeHorizontal) {
                QRectF originalGeometry = geometryRestore();
                originalGeometry.setX(moveResizeGeometry().x());
                originalGeometry.setWidth(moveResizeGeometry().width());
                setGeometryRestore(originalGeometry);
                maximize(requestedMaximizeMode() ^ MaximizeHorizontal);
            }
            break;
        case Gravity::Top:
        case Gravity::Bottom:
            // Quit maximized vertically state if the window is resized vertically.
            if (requestedMaximizeMode() & MaximizeVertical) {
                QRectF originalGeometry = geometryRestore();
                originalGeometry.setY(moveResizeGeometry().y());
                originalGeometry.setHeight(moveResizeGeometry().height());
                setGeometryRestore(originalGeometry);
                maximize(requestedMaximizeMode() ^ MaximizeVertical);
            }
            break;
        case Gravity::TopLeft:
        case Gravity::BottomLeft:
        case Gravity::TopRight:
        case Gravity::BottomRight:
            // Quit the maximized mode if the window is resized by dragging one of its corners.
            setGeometryRestore(moveResizeGeometry());
            maximize(MaximizeRestore);
            break;
        default:
            break;
        }
    }

    if (m_tile && !m_tile->supportsResizeGravity(interactiveMoveResizeGravity())) {
        setQuickTileMode(QuickTileFlag::None);
    }

    updateElectricGeometryRestore();
    checkUnrestrictedInteractiveMoveResize();
    Q_EMIT clientStartUserMovedResized(this);
    if (workspace()->screenEdges()->isDesktopSwitchingMovingClients()) {
        workspace()->screenEdges()->reserveDesktopSwitching(true, Qt::Vertical | Qt::Horizontal);
    }
    return true;
}

void Window::finishInteractiveMoveResize(bool cancel)
{
    const bool wasMove = isInteractiveMove();
    GeometryUpdatesBlocker blocker(this);
    leaveInteractiveMoveResize();

    doFinishInteractiveMoveResize();

    if (cancel) {
        moveResize(initialInteractiveMoveResizeGeometry());
        if (m_interactiveMoveResize.initialMaximizeMode != MaximizeMode::MaximizeRestore) {
            setMaximize(m_interactiveMoveResize.initialMaximizeMode & MaximizeMode::MaximizeVertical, m_interactiveMoveResize.initialMaximizeMode & MaximizeMode::MaximizeHorizontal);
            setGeometryRestore(m_interactiveMoveResize.initialGeometryRestore);
        } else if (m_interactiveMoveResize.initialQuickTileMode) {
            setQuickTileMode(m_interactiveMoveResize.initialQuickTileMode, true);
            setGeometryRestore(m_interactiveMoveResize.initialGeometryRestore);
        }
    } else if (moveResizeOutput() != interactiveMoveResizeStartOutput()) {
        workspace()->sendWindowToOutput(this, moveResizeOutput()); // checks rule validity
        if (isRequestedFullScreen() || requestedMaximizeMode() != MaximizeRestore) {
            checkWorkspacePosition();
        }
    }

    if (isElectricBorderMaximizing()) {
        setQuickTileMode(electricBorderMode());
        setElectricBorderMaximizing(false);
    } else if (wasMove && (input()->modifiersRelevantForGlobalShortcuts() & Qt::ShiftModifier)) {
        setQuickTileMode(QuickTileFlag::Custom);
    }
    setElectricBorderMode(QuickTileMode(QuickTileFlag::None));
    workspace()->outline()->hide();

    m_interactiveMoveResize.counter++;
    Q_EMIT clientFinishUserMovedResized(this);
}

// This function checks if it actually makes sense to perform a restricted move/resize.
// If e.g. the titlebar is already outside of the workarea, there's no point in performing
// a restricted move resize, because then e.g. resize would also move the window (#74555).
// NOTE: Most of it is duplicated from handleMoveResize().
void Window::checkUnrestrictedInteractiveMoveResize()
{
    if (isUnrestrictedInteractiveMoveResize()) {
        return;
    }
    const QRectF &moveResizeGeom = moveResizeGeometry();
    QRectF desktopArea = workspace()->clientArea(WorkArea, this, moveResizeGeom.center());
    int left_marge, right_marge, top_marge, bottom_marge, titlebar_marge;
    // restricted move/resize - keep at least part of the titlebar always visible
    // how much must remain visible when moved away in that direction
    left_marge = std::min(100. + borderRight(), moveResizeGeom.width());
    right_marge = std::min(100. + borderLeft(), moveResizeGeom.width());
    // width/height change with opaque resizing, use the initial ones
    titlebar_marge = initialInteractiveMoveResizeGeometry().height();
    top_marge = borderBottom();
    bottom_marge = borderTop();
    if (isInteractiveResize()) {
        if (moveResizeGeom.bottom() < desktopArea.top() + top_marge) {
            setUnrestrictedInteractiveMoveResize(true);
        }
        if (moveResizeGeom.top() > desktopArea.bottom() - bottom_marge) {
            setUnrestrictedInteractiveMoveResize(true);
        }
        if (moveResizeGeom.right() < desktopArea.left() + left_marge) {
            setUnrestrictedInteractiveMoveResize(true);
        }
        if (moveResizeGeom.left() > desktopArea.right() - right_marge) {
            setUnrestrictedInteractiveMoveResize(true);
        }
        if (!isUnrestrictedInteractiveMoveResize() && moveResizeGeom.top() < desktopArea.top()) { // titlebar mustn't go out
            setUnrestrictedInteractiveMoveResize(true);
        }
    }
    if (isInteractiveMove()) {
        if (moveResizeGeom.bottom() < desktopArea.top() + titlebar_marge) {
            setUnrestrictedInteractiveMoveResize(true);
        }
        // no need to check top_marge, titlebar_marge already handles it
        if (moveResizeGeom.top() > desktopArea.bottom() - bottom_marge) { // titlebar mustn't go out
            setUnrestrictedInteractiveMoveResize(true);
        }
        if (moveResizeGeom.right() < desktopArea.left() + left_marge) {
            setUnrestrictedInteractiveMoveResize(true);
        }
        if (moveResizeGeom.left() > desktopArea.right() - right_marge) {
            setUnrestrictedInteractiveMoveResize(true);
        }
    }
}

// When the user pressed mouse on the titlebar, don't activate move immediately,
// since it may be just a click. Activate instead after a delay. Move used to be
// activated only after moving by several pixels, but that looks bad.
void Window::startDelayedInteractiveMoveResize()
{
    Q_ASSERT(!m_interactiveMoveResize.delayedTimer);
    m_interactiveMoveResize.delayedTimer = new QTimer(this);
    m_interactiveMoveResize.delayedTimer->setSingleShot(true);
    connect(m_interactiveMoveResize.delayedTimer, &QTimer::timeout, this, [this]() {
        Q_ASSERT(isInteractiveMoveResizePointerButtonDown());
        if (!startInteractiveMoveResize()) {
            setInteractiveMoveResizePointerButtonDown(false);
        }
        updateCursor();
        stopDelayedInteractiveMoveResize();
    });
    m_interactiveMoveResize.delayedTimer->start(QApplication::startDragTime());
}

void Window::stopDelayedInteractiveMoveResize()
{
    delete m_interactiveMoveResize.delayedTimer;
    m_interactiveMoveResize.delayedTimer = nullptr;
}

void Window::updateInteractiveMoveResize(const QPointF &currentGlobalCursor)
{
    handleInteractiveMoveResize(pos(), currentGlobalCursor);
}

void Window::handleInteractiveMoveResize(const QPointF &local, const QPointF &global)
{
    const QRectF oldGeo = moveResizeGeometry();
    handleInteractiveMoveResize(local.x(), local.y(), global.x(), global.y());
    if (!isRequestedFullScreen() && isInteractiveMove()) {
        if (quickTileMode() != QuickTileMode(QuickTileFlag::None) && oldGeo != moveResizeGeometry()) {
            GeometryUpdatesBlocker blocker(this);
            setQuickTileMode(QuickTileFlag::None);
            const QRectF &geom_restore = geometryRestore();
            setInteractiveMoveOffset(QPointF(double(interactiveMoveOffset().x()) / double(oldGeo.width()) * double(geom_restore.width()),
                                             double(interactiveMoveOffset().y()) / double(oldGeo.height()) * double(geom_restore.height())));
            if (rules()->checkMaximize(MaximizeRestore) == MaximizeRestore) {
                setMoveResizeGeometry(geom_restore);
            }
            handleInteractiveMoveResize(local.x(), local.y(), global.x(), global.y()); // fix position
        }

        if (input()->modifiersRelevantForGlobalShortcuts() & Qt::ShiftModifier) {
            resetQuickTilingMaximizationZones();
            const auto &r = quickTileGeometry(QuickTileFlag::Custom, global);
            if (r.isEmpty()) {
                workspace()->outline()->hide();
            } else {
                if (!workspace()->outline()->isActive() || workspace()->outline()->geometry() != r.toRect()) {
                    workspace()->outline()->show(r.toRect(), moveResizeGeometry().toRect());
                }
            }
        } else {
            if (quickTileMode() == QuickTileMode(QuickTileFlag::None) && isResizable()) {
                checkQuickTilingMaximizationZones(global.x(), global.y());
            }
            if (!m_electricMaximizing) {
                // Only if we are in an electric maximizing gesture we should keep the outline,
                // otherwise we must make sure it's hidden
                workspace()->outline()->hide();
            }
        }
    }
}

void Window::handleInteractiveMoveResize(int x, int y, int x_root, int y_root)
{
    if (isWaitingForInteractiveMoveResizeSync()) {
        return; // we're still waiting for the client or the timeout
    }

    const Gravity gravity = interactiveMoveResizeGravity();
    if ((gravity == Gravity::None && !isMovableAcrossScreens())
        || (gravity != Gravity::None && (isShade() || !isResizable()))) {
        return;
    }

    if (!isInteractiveMoveResize()) {
        QPointF p(QPointF(x /* - padding_left*/, y /* - padding_top*/) - interactiveMoveOffset());
        if (p.manhattanLength() >= QApplication::startDragDistance()) {
            if (!startInteractiveMoveResize()) {
                setInteractiveMoveResizePointerButtonDown(false);
                updateCursor();
                return;
            }
            updateCursor();
        } else {
            return;
        }
    }

    // ShadeHover or ShadeActive, ShadeNormal was already avoided above
    if (gravity != Gravity::None && shadeMode() != ShadeNone) {
        setShade(ShadeNone);
    }

    QPointF globalPos(x_root, y_root);
    // these two points limit the geometry rectangle, i.e. if bottomleft resizing is done,
    // the bottomleft corner should be at is at (topleft.x(), bottomright().y())
    QPointF topleft = globalPos - interactiveMoveOffset();
    QPointF bottomright = globalPos + invertedInteractiveMoveOffset();
    const QRectF currentMoveResizeGeom = moveResizeGeometry();
    QRectF nextMoveResizeGeom = moveResizeGeometry();

    // TODO move whole group when moving its leader or when the leader is not mapped?

    auto titleBarRect = [this](const QRectF &rect, bool &transposed, int &requiredPixels) -> QRectF {
        QRectF titleRect = rect;
        titleRect.moveTopLeft(QPointF(0, 0));
        switch (titlebarPosition()) {
        default:
        case Qt::TopEdge:
            titleRect.setHeight(borderTop());
            break;
        case Qt::LeftEdge:
            titleRect.setWidth(borderLeft());
            transposed = true;
            break;
        case Qt::BottomEdge:
            titleRect.setTop(titleRect.bottom() - borderBottom());
            break;
        case Qt::RightEdge:
            titleRect.setLeft(titleRect.right() - borderRight());
            transposed = true;
            break;
        }
        // When doing a restricted move we must always keep 100px of the titlebar
        // visible to allow the user to be able to move it again.
        requiredPixels = std::min(100 * (transposed ? titleRect.width() : titleRect.height()),
                                  rect.width() * rect.height());
        return titleRect;
    };

    if (isInteractiveResize()) {
        if (m_tile && m_tile->supportsResizeGravity(gravity)) {
            m_tile->resizeFromGravity(gravity, x_root, y_root);
            return;
        }

        QRectF orig = initialInteractiveMoveResizeGeometry();
        SizeMode sizeMode = SizeModeAny;
        auto calculateMoveResizeGeom = [&topleft, &bottomright, &orig, &nextMoveResizeGeom, &sizeMode, &gravity]() {
            switch (gravity) {
            case Gravity::TopLeft:
                nextMoveResizeGeom = QRectF(topleft, orig.bottomRight());
                break;
            case Gravity::BottomRight:
                nextMoveResizeGeom = QRectF(orig.topLeft(), bottomright);
                break;
            case Gravity::BottomLeft:
                nextMoveResizeGeom = QRectF(QPointF(topleft.x(), orig.y()), QPointF(orig.right(), bottomright.y()));
                break;
            case Gravity::TopRight:
                nextMoveResizeGeom = QRectF(QPointF(orig.x(), topleft.y()), QPointF(bottomright.x(), orig.bottom()));
                break;
            case Gravity::Top:
                nextMoveResizeGeom = QRectF(QPointF(orig.left(), topleft.y()), orig.bottomRight());
                sizeMode = SizeModeFixedH; // try not to affect height
                break;
            case Gravity::Bottom:
                nextMoveResizeGeom = QRectF(orig.topLeft(), QPointF(orig.right(), bottomright.y()));
                sizeMode = SizeModeFixedH;
                break;
            case Gravity::Left:
                nextMoveResizeGeom = QRectF(QPointF(topleft.x(), orig.top()), orig.bottomRight());
                sizeMode = SizeModeFixedW;
                break;
            case Gravity::Right:
                nextMoveResizeGeom = QRectF(orig.topLeft(), QPointF(bottomright.x(), orig.bottom()));
                sizeMode = SizeModeFixedW;
                break;
            case Gravity::None:
                Q_UNREACHABLE();
                break;
            }
        };

        // first resize (without checking constrains), then snap, then check bounds, then check constrains
        calculateMoveResizeGeom();
        // adjust new size to snap to other windows/borders
        nextMoveResizeGeom = workspace()->adjustWindowSize(this, nextMoveResizeGeom, gravity);

        if (!isUnrestrictedInteractiveMoveResize()) {
            // Make sure the titlebar isn't behind a restricted area. We don't need to restrict
            // the other directions. If not visible enough, move the window to the closest valid
            // point. We bruteforce this by slowly moving the window back to its previous position
            const StrutRects strut = workspace()->restrictedMoveArea(VirtualDesktopManager::self()->currentDesktop());
            QRegion availableArea(workspace()->clientArea(FullArea, this, workspace()->activeOutput()).toRect());
            for (const QRect &rect : strut) {
                availableArea -= rect;
            }
            bool transposed = false;
            int requiredPixels;
            QRectF bTitleRect = titleBarRect(nextMoveResizeGeom, transposed, requiredPixels);
            int lastVisiblePixels = -1;
            QRectF lastTry = nextMoveResizeGeom;
            bool titleFailed = false;
            for (;;) {
                const QRect titleRect = bTitleRect.translated(nextMoveResizeGeom.topLeft()).toRect();
                int visiblePixels = 0;
                int realVisiblePixels = 0;
                for (const QRect &rect : availableArea) {
                    const QRect r = rect & titleRect;
                    realVisiblePixels += r.width() * r.height();
                    if ((transposed && r.width() == titleRect.width()) || // Only the full size regions...
                        (!transposed && r.height() == titleRect.height())) { // ...prevents long slim areas
                        visiblePixels += r.width() * r.height();
                    }
                }

                if (visiblePixels >= requiredPixels) {
                    break; // We have reached a valid position
                }

                if (realVisiblePixels <= lastVisiblePixels) {
                    if (titleFailed && realVisiblePixels < lastVisiblePixels) {
                        break; // we won't become better
                    } else {
                        if (!titleFailed) {
                            nextMoveResizeGeom = lastTry;
                        }
                        titleFailed = true;
                    }
                }
                lastVisiblePixels = realVisiblePixels;
                QRectF currentTry = nextMoveResizeGeom;
                lastTry = currentTry;

                // Not visible enough, move the window to the closest valid point. We bruteforce
                // this by slowly moving the window back to its previous position.
                // The geometry changes at up to two edges, the one with the title (if) shall take
                // precedence. The opposing edge has no impact on visiblePixels and only one of
                // the adjacent can alter at a time, ie. it's enough to ignore adjacent edges
                // if the title edge altered
                bool leftChanged = !qFuzzyCompare(currentMoveResizeGeom.left(), currentTry.left());
                bool rightChanged = !qFuzzyCompare(currentMoveResizeGeom.right(), currentTry.right());
                bool topChanged = !qFuzzyCompare(currentMoveResizeGeom.top(), currentTry.top());
                bool btmChanged = !qFuzzyCompare(currentMoveResizeGeom.bottom(), currentTry.bottom());
                auto fixChangedState = [titleFailed](bool &major, bool &counter, bool &ad1, bool &ad2) {
                    counter = false;
                    if (titleFailed) {
                        major = false;
                    }
                    if (major) {
                        ad1 = ad2 = false;
                    }
                };
                switch (titlebarPosition()) {
                default:
                case Qt::TopEdge:
                    fixChangedState(topChanged, btmChanged, leftChanged, rightChanged);
                    break;
                case Qt::LeftEdge:
                    fixChangedState(leftChanged, rightChanged, topChanged, btmChanged);
                    break;
                case Qt::BottomEdge:
                    fixChangedState(btmChanged, topChanged, leftChanged, rightChanged);
                    break;
                case Qt::RightEdge:
                    fixChangedState(rightChanged, leftChanged, topChanged, btmChanged);
                    break;
                }
                if (topChanged) {
                    currentTry.setTop(currentTry.y() + qBound(-1.0, currentMoveResizeGeom.y() - currentTry.y(), 1.0));
                } else if (leftChanged) {
                    currentTry.setLeft(currentTry.x() + qBound(-1.0, currentMoveResizeGeom.x() - currentTry.x(), 1.0));
                } else if (btmChanged) {
                    currentTry.setBottom(currentTry.bottom() + qBound(-1.0, currentMoveResizeGeom.bottom() - currentTry.bottom(), 1.0));
                } else if (rightChanged) {
                    currentTry.setRight(currentTry.right() + qBound(-1.0, currentMoveResizeGeom.right() - currentTry.right(), 1.0));
                } else {
                    break; // no position changed - that's certainly not good
                }
                nextMoveResizeGeom = currentTry;
            }
        }

        // Always obey size hints, even when in "unrestricted" mode
        QSizeF size = constrainFrameSize(nextMoveResizeGeom.size(), sizeMode);
        // the new topleft and bottomright corners (after checking size constrains), if they'll be needed
        topleft = QPointF(nextMoveResizeGeom.right() - size.width(), nextMoveResizeGeom.bottom() - size.height());
        bottomright = QPointF(nextMoveResizeGeom.left() + size.width(), nextMoveResizeGeom.top() + size.height());
        orig = nextMoveResizeGeom;

        // if aspect ratios are specified, both dimensions may change.
        // Therefore grow to the right/bottom if needed.
        // TODO it should probably obey gravity rather than always using right/bottom ?
        if (sizeMode == SizeModeFixedH) {
            orig.setRight(bottomright.x());
        } else if (sizeMode == SizeModeFixedW) {
            orig.setBottom(bottomright.y());
        }

        calculateMoveResizeGeom();
    } else if (isInteractiveMove()) {
        Q_ASSERT(gravity == Gravity::None);
        if (!isMovable()) { // isMovableAcrossScreens() must have been true to get here
            // Special moving of maximized windows on Xinerama screens
            Output *output = workspace()->outputAt(globalPos);
            if (isRequestedFullScreen()) {
                nextMoveResizeGeom = workspace()->clientArea(FullScreenArea, this, output);
            } else {
                nextMoveResizeGeom = workspace()->clientArea(MaximizeArea, this, output);
                const QSizeF adjSize = constrainFrameSize(nextMoveResizeGeom.size(), SizeModeMax);
                if (adjSize != nextMoveResizeGeom.size()) {
                    QRectF r(nextMoveResizeGeom);
                    nextMoveResizeGeom.setSize(adjSize);
                    nextMoveResizeGeom.moveCenter(r.center());
                }
            }
        } else {
            // first move, then snap, then check bounds
            QRectF geometry = nextMoveResizeGeom;
            geometry.moveTopLeft(topleft);
            geometry.moveTopLeft(workspace()->adjustWindowPosition(this, geometry.topLeft(),
                                                                   isUnrestrictedInteractiveMoveResize()));
            nextMoveResizeGeom = geometry;

            if (!isUnrestrictedInteractiveMoveResize()) {
                const StrutRects strut = workspace()->restrictedMoveArea(VirtualDesktopManager::self()->currentDesktop());
                QRegion availableArea(workspace()->clientArea(FullArea, this, workspace()->activeOutput()).toRect());
                for (const QRect &rect : strut) {
                    availableArea -= rect; // Strut areas
                }
                bool transposed = false;
                int requiredPixels;
                QRectF bTitleRect = titleBarRect(nextMoveResizeGeom, transposed, requiredPixels);
                for (;;) {
                    QRectF currentTry = nextMoveResizeGeom;
                    const QRectF titleRect(bTitleRect.translated(currentTry.topLeft()));
                    int visiblePixels = 0;
                    for (const QRect &rect : availableArea) {
                        const QRect r = rect & titleRect.toRect();
                        if ((transposed && r.width() == titleRect.width()) || // Only the full size regions...
                            (!transposed && r.height() == titleRect.height())) { // ...prevents long slim areas
                            visiblePixels += r.width() * r.height();
                        }
                    }
                    if (visiblePixels >= requiredPixels) {
                        break; // We have reached a valid position
                    }

                    // (esp.) if there're more screens with different struts (panels) it the titlebar
                    // will be movable outside the movearea (covering one of the panels) until it
                    // crosses the panel "too much" (not enough visiblePixels) and then stucks because
                    // it's usually only pushed by 1px to either direction
                    // so we first check whether we intersect suc strut and move the window below it
                    // immediately (it's still possible to hit the visiblePixels >= titlebarArea break
                    // by moving the window slightly downwards, but it won't stuck)
                    // see bug #274466
                    // and bug #301805 for why we can't just match the titlearea against the screen
                    if (workspace()->outputs().count() > 1) { // optimization
                        // TODO: could be useful on partial screen struts (half-width panels etc.)
                        int newTitleTop = -1;
                        for (const QRect &region : strut) {
                            QRectF r = region;
                            if (r.top() == 0 && r.width() > r.height() && // "top panel"
                                r.intersects(currentTry) && currentTry.top() < r.bottom()) {
                                newTitleTop = r.bottom();
                                break;
                            }
                        }
                        if (newTitleTop > -1) {
                            currentTry.moveTop(newTitleTop); // invalid position, possibly on screen change
                            nextMoveResizeGeom = currentTry;
                            break;
                        }
                    }

                    int dx = sign(currentMoveResizeGeom.x() - currentTry.x()),
                        dy = sign(currentMoveResizeGeom.y() - currentTry.y());
                    if (visiblePixels && dx) { // means there's no full width cap -> favor horizontally
                        dy = 0;
                    } else if (dy) {
                        dx = 0;
                    }

                    // Move it back
                    currentTry.translate(dx, dy);
                    nextMoveResizeGeom = currentTry;

                    if (nextMoveResizeGeom == currentMoveResizeGeom) {
                        break; // Prevent lockup
                    }
                }
            }
        }
    } else {
        Q_UNREACHABLE();
    }

    if (nextMoveResizeGeom != currentMoveResizeGeom) {
        if (isInteractiveMove()) {
            move(nextMoveResizeGeom.topLeft());
        } else {
            doInteractiveResizeSync(nextMoveResizeGeom);
        }

        Q_EMIT clientStepUserMovedResized(this, nextMoveResizeGeom);
    }
}

StrutRect Window::strutRect(StrutArea area) const
{
    return StrutRect();
}

StrutRects Window::strutRects() const
{
    StrutRects region;
    if (const StrutRect strut = strutRect(StrutAreaTop); strut.isValid()) {
        region += strut;
    }
    if (const StrutRect strut = strutRect(StrutAreaRight); strut.isValid()) {
        region += strut;
    }
    if (const StrutRect strut = strutRect(StrutAreaBottom); strut.isValid()) {
        region += strut;
    }
    if (const StrutRect strut = strutRect(StrutAreaLeft); strut.isValid()) {
        region += strut;
    }
    return region;
}

bool Window::hasStrut() const
{
    return false;
}

void Window::setupWindowManagementInterface()
{
    if (m_windowManagementInterface) {
        // already setup
        return;
    }
    if (!waylandServer() || !waylandServer()->windowManagement()) {
        return;
    }
    using namespace KWaylandServer;
    auto w = waylandServer()->windowManagement()->createWindow(this, internalId());
    w->setTitle(caption());
    w->setActive(isActive());
    w->setFullscreen(isFullScreen());
    w->setKeepAbove(keepAbove());
    w->setKeepBelow(keepBelow());
    w->setMaximized(maximizeMode() == KWin::MaximizeFull);
    w->setMinimized(isMinimized());
    w->setDemandsAttention(isDemandingAttention());
    w->setCloseable(isCloseable());
    w->setMaximizeable(isMaximizable());
    w->setMinimizeable(isMinimizable());
    w->setFullscreenable(isFullScreenable());
    w->setApplicationMenuPaths(applicationMenuServiceName(), applicationMenuObjectPath());
    w->setIcon(icon());
    auto updateAppId = [this, w] {
        w->setResourceName(resourceName());
        w->setAppId(m_desktopFileName.isEmpty() ? resourceClass() : m_desktopFileName);
    };
    updateAppId();
    w->setSkipTaskbar(skipTaskbar());
    w->setSkipSwitcher(skipSwitcher());
    w->setPid(pid());
    w->setShadeable(isShadeable());
    w->setShaded(isShade());
    w->setResizable(isResizable());
    w->setMovable(isMovable());
    w->setVirtualDesktopChangeable(true); // FIXME Matches X11Window::actionSupported(), but both should be implemented.
    w->setParentWindow(transientFor() ? transientFor()->windowManagementInterface() : nullptr);
    w->setGeometry(frameGeometry().toRect());
    connect(this, &Window::skipTaskbarChanged, w, [w, this]() {
        w->setSkipTaskbar(skipTaskbar());
    });
    connect(this, &Window::skipSwitcherChanged, w, [w, this]() {
        w->setSkipSwitcher(skipSwitcher());
    });
    connect(this, &Window::captionChanged, w, [w, this] {
        w->setTitle(caption());
    });

    connect(this, &Window::activeChanged, w, [w, this] {
        w->setActive(isActive());
    });
    connect(this, &Window::fullScreenChanged, w, [w, this] {
        w->setFullscreen(isFullScreen());
    });
    connect(this, &Window::keepAboveChanged, w, &PlasmaWindowInterface::setKeepAbove);
    connect(this, &Window::keepBelowChanged, w, &PlasmaWindowInterface::setKeepBelow);
    connect(this, &Window::minimizedChanged, w, [w, this] {
        w->setMinimized(isMinimized());
    });
    connect(this, static_cast<void (Window::*)(Window *, MaximizeMode)>(&Window::clientMaximizedStateChanged), w, [w](KWin::Window *c, MaximizeMode mode) {
        w->setMaximized(mode == KWin::MaximizeFull);
    });
    connect(this, &Window::demandsAttentionChanged, w, [w, this] {
        w->setDemandsAttention(isDemandingAttention());
    });
    connect(this, &Window::iconChanged, w, [w, this]() {
        w->setIcon(icon());
    });
    connect(this, &Window::windowClassChanged, w, updateAppId);
    connect(this, &Window::desktopFileNameChanged, w, updateAppId);
    connect(this, &Window::shadeChanged, w, [w, this] {
        w->setShaded(isShade());
    });
    connect(this, &Window::transientChanged, w, [w, this]() {
        w->setParentWindow(transientFor() ? transientFor()->windowManagementInterface() : nullptr);
    });
    connect(this, &Window::frameGeometryChanged, w, [w, this]() {
        w->setGeometry(frameGeometry().toRect());
    });
    connect(this, &Window::applicationMenuChanged, w, [w, this]() {
        w->setApplicationMenuPaths(applicationMenuServiceName(), applicationMenuObjectPath());
    });
    connect(w, &PlasmaWindowInterface::closeRequested, this, [this] {
        closeWindow();
    });
    connect(w, &PlasmaWindowInterface::moveRequested, this, [this]() {
        Cursors::self()->mouse()->setPos(frameGeometry().center());
        performMouseCommand(Options::MouseMove, Cursors::self()->mouse()->pos());
    });
    connect(w, &PlasmaWindowInterface::resizeRequested, this, [this]() {
        Cursors::self()->mouse()->setPos(frameGeometry().bottomRight());
        performMouseCommand(Options::MouseResize, Cursors::self()->mouse()->pos());
    });
    connect(w, &PlasmaWindowInterface::fullscreenRequested, this, [this](bool set) {
        setFullScreen(set, false);
    });
    connect(w, &PlasmaWindowInterface::minimizedRequested, this, [this](bool set) {
        if (set) {
            minimize();
        } else {
            unminimize();
        }
    });
    connect(w, &PlasmaWindowInterface::maximizedRequested, this, [this](bool set) {
        maximize(set ? MaximizeFull : MaximizeRestore);
    });
    connect(w, &PlasmaWindowInterface::keepAboveRequested, this, [this](bool set) {
        setKeepAbove(set);
    });
    connect(w, &PlasmaWindowInterface::keepBelowRequested, this, [this](bool set) {
        setKeepBelow(set);
    });
    connect(w, &PlasmaWindowInterface::demandsAttentionRequested, this, [this](bool set) {
        demandAttention(set);
    });
    connect(w, &PlasmaWindowInterface::activeRequested, this, [this](bool set) {
        if (set) {
            workspace()->activateWindow(this, true);
        }
    });
    connect(w, &PlasmaWindowInterface::shadedRequested, this, [this](bool set) {
        setShade(set);
    });

    for (const auto vd : std::as_const(m_desktops)) {
        w->addPlasmaVirtualDesktop(vd->id());
    }
    // We need to set `OnAllDesktops` after the actual VD list has been added.
    // Otherwise it will unconditionally add the current desktop to the interface
    // which may not be the case, for example, when using rules
    w->setOnAllDesktops(isOnAllDesktops());

    // Plasma Virtual desktop management
    // show/hide when the window enters/exits from desktop
    connect(w, &PlasmaWindowInterface::enterPlasmaVirtualDesktopRequested, this, [this](const QString &desktopId) {
        VirtualDesktop *vd = VirtualDesktopManager::self()->desktopForId(desktopId);
        if (vd) {
            enterDesktop(vd);
        }
    });
    connect(w, &PlasmaWindowInterface::enterNewPlasmaVirtualDesktopRequested, this, [this]() {
        VirtualDesktopManager::self()->setCount(VirtualDesktopManager::self()->count() + 1);
        enterDesktop(VirtualDesktopManager::self()->desktops().last());
    });
    connect(w, &PlasmaWindowInterface::leavePlasmaVirtualDesktopRequested, this, [this](const QString &desktopId) {
        VirtualDesktop *vd = VirtualDesktopManager::self()->desktopForId(desktopId);
        if (vd) {
            leaveDesktop(vd);
        }
    });

    for (const auto &activity : std::as_const(m_activityList)) {
        w->addPlasmaActivity(activity);
    }

    connect(this, &Window::activitiesChanged, w, [w, this] {
        const auto newActivities = QSet<QString>(m_activityList.begin(), m_activityList.end());
        const auto oldActivitiesList = w->plasmaActivities();
        const auto oldActivities = QSet<QString>(oldActivitiesList.begin(), oldActivitiesList.end());

        const auto activitiesToAdd = newActivities - oldActivities;
        for (const auto &activity : activitiesToAdd) {
            w->addPlasmaActivity(activity);
        }

        const auto activitiesToRemove = oldActivities - newActivities;
        for (const auto &activity : activitiesToRemove) {
            w->removePlasmaActivity(activity);
        }
    });

    // Plasma Activities management
    // show/hide when the window enters/exits activity
    connect(w, &PlasmaWindowInterface::enterPlasmaActivityRequested, this, [this](const QString &activityId) {
        setOnActivity(activityId, true);
    });
    connect(w, &PlasmaWindowInterface::leavePlasmaActivityRequested, this, [this](const QString &activityId) {
        setOnActivity(activityId, false);
    });
    connect(w, &PlasmaWindowInterface::sendToOutput, this, [this](KWaylandServer::OutputInterface *output) {
        sendToOutput(output->handle());
    });

    m_windowManagementInterface = w;
}

Options::MouseCommand Window::getMouseCommand(Qt::MouseButton button, bool *handled) const
{
    *handled = false;
    if (button == Qt::NoButton) {
        return Options::MouseNothing;
    }
    if (isActive()) {
        if (options->isClickRaise() && !isMostRecentlyRaised()) {
            *handled = true;
            return Options::MouseActivateRaiseAndPassClick;
        }
    } else {
        *handled = true;
        switch (button) {
        case Qt::LeftButton:
            return options->commandWindow1();
        case Qt::MiddleButton:
            return options->commandWindow2();
        case Qt::RightButton:
            return options->commandWindow3();
        default:
            // all other buttons pass Activate & Pass Client
            return Options::MouseActivateAndPassClick;
        }
    }
    return Options::MouseNothing;
}

Options::MouseCommand Window::getWheelCommand(Qt::Orientation orientation, bool *handled) const
{
    *handled = false;
    if (orientation != Qt::Vertical) {
        return Options::MouseNothing;
    }
    if (!isActive()) {
        *handled = true;
        return options->commandWindowWheel();
    }
    return Options::MouseNothing;
}

bool Window::performMouseCommand(Options::MouseCommand cmd, const QPointF &globalPos)
{
    bool replay = false;
    switch (cmd) {
    case Options::MouseRaise:
        workspace()->raiseWindow(this);
        break;
    case Options::MouseLower: {
        workspace()->lowerWindow(this);
        // used to be activateNextWindow(this), then topWindowOnDesktop
        // since this is a mouseOp it's however safe to use the window under the mouse instead
        if (isActive() && options->focusPolicyIsReasonable()) {
            Window *next = workspace()->windowUnderMouse(output());
            if (next && next != this) {
                workspace()->requestFocus(next, false);
            }
        }
        break;
    }
    case Options::MouseOperationsMenu:
        if (isActive() && options->isClickRaise()) {
            autoRaise();
        }
        workspace()->showWindowMenu(QRect(globalPos.toPoint(), globalPos.toPoint()), this);
        break;
    case Options::MouseToggleRaiseAndLower:
        workspace()->raiseOrLowerWindow(this);
        break;
    case Options::MouseActivateAndRaise: {
        replay = isActive(); // for clickraise mode
        bool mustReplay = !rules()->checkAcceptFocus(acceptsFocus());
        if (mustReplay) {
            auto it = workspace()->stackingOrder().constEnd(),
                 begin = workspace()->stackingOrder().constBegin();
            while (mustReplay && --it != begin && *it != this) {
                auto c = *it;
                if (!c->isClient() || (c->keepAbove() && !keepAbove()) || (keepBelow() && !c->keepBelow())) {
                    continue; // can never raise above "it"
                }
                mustReplay = !(c->isOnCurrentDesktop() && c->isOnCurrentActivity() && c->frameGeometry().intersects(frameGeometry()));
            }
        }
        workspace()->takeActivity(this, Workspace::ActivityFocus | Workspace::ActivityRaise);
        workspace()->setActiveOutput(globalPos);
        replay = replay || mustReplay;
        break;
    }
    case Options::MouseActivateAndLower:
        workspace()->requestFocus(this);
        workspace()->lowerWindow(this);
        workspace()->setActiveOutput(globalPos);
        replay = replay || !rules()->checkAcceptFocus(acceptsFocus());
        break;
    case Options::MouseActivate:
        replay = isActive(); // for clickraise mode
        workspace()->takeActivity(this, Workspace::ActivityFocus);
        workspace()->setActiveOutput(globalPos);
        replay = replay || !rules()->checkAcceptFocus(acceptsFocus());
        break;
    case Options::MouseActivateRaiseAndPassClick:
        workspace()->takeActivity(this, Workspace::ActivityFocus | Workspace::ActivityRaise);
        workspace()->setActiveOutput(globalPos);
        replay = true;
        break;
    case Options::MouseActivateAndPassClick:
        workspace()->takeActivity(this, Workspace::ActivityFocus);
        workspace()->setActiveOutput(globalPos);
        replay = true;
        break;
    case Options::MouseMaximize:
        maximize(MaximizeFull);
        break;
    case Options::MouseRestore:
        maximize(MaximizeRestore);
        break;
    case Options::MouseMinimize:
        minimize();
        break;
    case Options::MouseAbove: {
        StackingUpdatesBlocker blocker(workspace());
        if (keepBelow()) {
            setKeepBelow(false);
        } else {
            setKeepAbove(true);
        }
        break;
    }
    case Options::MouseBelow: {
        StackingUpdatesBlocker blocker(workspace());
        if (keepAbove()) {
            setKeepAbove(false);
        } else {
            setKeepBelow(true);
        }
        break;
    }
    case Options::MousePreviousDesktop:
        workspace()->windowToPreviousDesktop(this);
        break;
    case Options::MouseNextDesktop:
        workspace()->windowToNextDesktop(this);
        break;
    case Options::MouseOpacityMore:
        if (!isDesktop()) { // No point in changing the opacity of the desktop
            setOpacity(std::min(opacity() + 0.1, 1.0));
        }
        break;
    case Options::MouseOpacityLess:
        if (!isDesktop()) { // No point in changing the opacity of the desktop
            setOpacity(std::max(opacity() - 0.1, 0.1));
        }
        break;
    case Options::MouseClose:
        closeWindow();
        break;
    case Options::MouseActivateRaiseAndMove:
    case Options::MouseActivateRaiseAndUnrestrictedMove:
        workspace()->raiseWindow(this);
        workspace()->requestFocus(this);
        workspace()->setActiveOutput(globalPos);
        // fallthrough
    case Options::MouseMove:
    case Options::MouseUnrestrictedMove: {
        if (!isMovableAcrossScreens()) {
            break;
        }
        if (isInteractiveMoveResize()) {
            finishInteractiveMoveResize(false);
        }
        setInteractiveMoveResizeGravity(Gravity::None);
        setInteractiveMoveResizePointerButtonDown(true);
        setInteractiveMoveOffset(QPointF(globalPos.x() - x(), globalPos.y() - y())); // map from global
        setInvertedInteractiveMoveOffset(rect().bottomRight() - interactiveMoveOffset());
        setUnrestrictedInteractiveMoveResize((cmd == Options::MouseActivateRaiseAndUnrestrictedMove
                                              || cmd == Options::MouseUnrestrictedMove));
        if (!startInteractiveMoveResize()) {
            setInteractiveMoveResizePointerButtonDown(false);
        }
        updateCursor();
        break;
    }
    case Options::MouseResize:
    case Options::MouseUnrestrictedResize: {
        if (!isResizable() || isShade()) {
            break;
        }
        if (isInteractiveMoveResize()) {
            finishInteractiveMoveResize(false);
        }
        setInteractiveMoveResizePointerButtonDown(true);
        const QPointF moveOffset = QPointF(globalPos.x() - x(), globalPos.y() - y()); // map from global
        setInteractiveMoveOffset(moveOffset);
        int x = moveOffset.x(), y = moveOffset.y();
        bool left = x < width() / 3;
        bool right = x >= 2 * width() / 3;
        bool top = y < height() / 3;
        bool bot = y >= 2 * height() / 3;
        Gravity gravity;
        if (top) {
            gravity = left ? Gravity::TopLeft : (right ? Gravity::TopRight : Gravity::Top);
        } else if (bot) {
            gravity = left ? Gravity::BottomLeft : (right ? Gravity::BottomRight : Gravity::Bottom);
        } else {
            gravity = (x < width() / 2) ? Gravity::Left : Gravity::Right;
        }
        setInteractiveMoveResizeGravity(gravity);
        setInvertedInteractiveMoveOffset(rect().bottomRight() - moveOffset);
        setUnrestrictedInteractiveMoveResize((cmd == Options::MouseUnrestrictedResize));
        if (!startInteractiveMoveResize()) {
            setInteractiveMoveResizePointerButtonDown(false);
        }
        updateCursor();
        break;
    }
    case Options::MouseShade:
        toggleShade();
        cancelShadeHoverTimer();
        break;
    case Options::MouseSetShade:
        setShade(ShadeNormal);
        cancelShadeHoverTimer();
        break;
    case Options::MouseUnsetShade:
        setShade(ShadeNone);
        cancelShadeHoverTimer();
        break;
    case Options::MouseNothing:
    default:
        replay = true;
        break;
    }
    return replay;
}

void Window::setTransientFor(Window *transientFor)
{
    if (transientFor == this) {
        // cannot be transient for one self
        return;
    }
    if (m_transientFor == transientFor) {
        return;
    }
    m_transientFor = transientFor;
    Q_EMIT transientChanged();
}

const Window *Window::transientFor() const
{
    return m_transientFor;
}

Window *Window::transientFor()
{
    return m_transientFor;
}

bool Window::hasTransientPlacementHint() const
{
    return false;
}

QRectF Window::transientPlacement(const QRectF &bounds) const
{
    Q_UNREACHABLE();
    return QRectF();
}

bool Window::hasTransient(const Window *c, bool indirect) const
{
    return c->transientFor() == this;
}

QList<Window *> Window::mainWindows() const
{
    if (const Window *t = transientFor()) {
        return QList<Window *>{const_cast<Window *>(t)};
    }
    return QList<Window *>();
}

QList<Window *> Window::allMainWindows() const
{
    auto result = mainWindows();
    for (const auto *window : result) {
        result += window->allMainWindows();
    }
    return result;
}

void Window::setModal(bool m)
{
    // Qt-3.2 can have even modal normal windows :(
    if (m_modal == m) {
        return;
    }
    m_modal = m;
    Q_EMIT modalChanged();
    // Changing modality for a mapped window is weird (?)
    // _NET_WM_STATE_MODAL should possibly rather be _NET_WM_WINDOW_TYPE_MODAL_DIALOG
}

bool Window::isModal() const
{
    return m_modal;
}

// check whether a transient should be actually kept above its mainwindow
// there may be some special cases where this rule shouldn't be enfored
static bool shouldKeepTransientAbove(const Window *parent, const Window *transient)
{
    // #93832 - don't keep splashscreens above dialogs
    if (transient->isSplash() && parent->isDialog()) {
        return false;
    }
    // This is rather a hack for #76026. Don't keep non-modal dialogs above
    // the mainwindow, but only if they're group transient (since only such dialogs
    // have taskbar entry in Kicker). A proper way of doing this (both kwin and kicker)
    // needs to be found.
    if (transient->isDialog() && !transient->isModal() && transient->groupTransient()) {
        return false;
    }
    // #63223 - don't keep transients above docks, because the dock is kept high,
    // and e.g. dialogs for them would be too high too
    // ignore this if the transient has a placement hint which indicates it should go above it's parent
    if (parent->isDock() && !transient->hasTransientPlacementHint()) {
        return false;
    }
    return true;
}

void Window::addTransient(Window *cl)
{
    Q_ASSERT(!m_transients.contains(cl));
    Q_ASSERT(cl != this);
    m_transients.append(cl);
    if (shouldKeepTransientAbove(this, cl)) {
        workspace()->constrain(this, cl);
    }
}

void Window::removeTransient(Window *cl)
{
    m_transients.removeAll(cl);
    if (cl->transientFor() == this) {
        cl->setTransientFor(nullptr);
    }
    workspace()->unconstrain(this, cl);
}

void Window::removeTransientFromList(Window *cl)
{
    m_transients.removeAll(cl);
}

bool Window::isActiveFullScreen() const
{
    if (!isFullScreen()) {
        return false;
    }

    const auto ac = workspace()->mostRecentlyActivatedWindow(); // instead of activeWindow() - avoids flicker
    // according to NETWM spec implementation notes suggests
    // "focused windows having state _NET_WM_STATE_FULLSCREEN" to be on the highest layer.
    // we'll also take the screen into account
    return ac && (ac == this || !ac->isOnOutput(output()) || ac->allMainWindows().contains(const_cast<Window *>(this)));
}

int Window::borderBottom() const
{
    return isDecorated() ? decoration()->borderBottom() : 0;
}

int Window::borderLeft() const
{
    return isDecorated() ? decoration()->borderLeft() : 0;
}

int Window::borderRight() const
{
    return isDecorated() ? decoration()->borderRight() : 0;
}

int Window::borderTop() const
{
    return isDecorated() ? decoration()->borderTop() : 0;
}

void Window::updateCursor()
{
    Gravity gravity = interactiveMoveResizeGravity();
    if (!isResizable() || isShade()) {
        gravity = Gravity::None;
    }
    CursorShape c = Qt::ArrowCursor;
    switch (gravity) {
    case Gravity::TopLeft:
        c = KWin::ExtendedCursor::SizeNorthWest;
        break;
    case Gravity::BottomRight:
        c = KWin::ExtendedCursor::SizeSouthEast;
        break;
    case Gravity::BottomLeft:
        c = KWin::ExtendedCursor::SizeSouthWest;
        break;
    case Gravity::TopRight:
        c = KWin::ExtendedCursor::SizeNorthEast;
        break;
    case Gravity::Top:
        c = KWin::ExtendedCursor::SizeNorth;
        break;
    case Gravity::Bottom:
        c = KWin::ExtendedCursor::SizeSouth;
        break;
    case Gravity::Left:
        c = KWin::ExtendedCursor::SizeWest;
        break;
    case Gravity::Right:
        c = KWin::ExtendedCursor::SizeEast;
        break;
    default:
        if (isInteractiveMoveResize()) {
            c = Qt::SizeAllCursor;
        } else {
            c = Qt::ArrowCursor;
        }
        break;
    }
    if (c == m_interactiveMoveResize.cursor) {
        return;
    }
    m_interactiveMoveResize.cursor = c;
    Q_EMIT moveResizeCursorChanged(c);
}

void Window::leaveInteractiveMoveResize()
{
    workspace()->setMoveResizeWindow(nullptr);
    setInteractiveMoveResize(false);
    if (workspace()->screenEdges()->isDesktopSwitchingMovingClients()) {
        workspace()->screenEdges()->reserveDesktopSwitching(false, Qt::Vertical | Qt::Horizontal);
    }
    if (isElectricBorderMaximizing()) {
        workspace()->outline()->hide();
        elevate(false);
    }
}

bool Window::doStartInteractiveMoveResize()
{
    return true;
}

void Window::doFinishInteractiveMoveResize()
{
}

bool Window::isWaitingForInteractiveMoveResizeSync() const
{
    return false;
}

void Window::doInteractiveResizeSync(const QRectF &)
{
}

void Window::checkQuickTilingMaximizationZones(int xroot, int yroot)
{
    QuickTileMode mode = QuickTileFlag::None;
    bool innerBorder = false;

    const auto outputs = workspace()->outputs();
    for (const Output *output : outputs) {
        if (!output->geometry().contains(QPoint(xroot, yroot))) {
            continue;
        }

        auto isInScreen = [&output, &outputs](const QPoint &pt) {
            for (const Output *other : outputs) {
                if (other == output) {
                    continue;
                }
                if (other->geometry().contains(pt)) {
                    return true;
                }
            }
            return false;
        };

        QRectF area = workspace()->clientArea(MaximizeArea, this, QPointF(xroot, yroot));
        if (options->electricBorderTiling()) {
            if (xroot <= area.x() + 20) {
                mode |= QuickTileFlag::Left;
                innerBorder = isInScreen(QPoint(area.x() - 1, yroot));
            } else if (xroot >= area.x() + area.width() - 20) {
                mode |= QuickTileFlag::Right;
                innerBorder = isInScreen(QPoint(area.right() + 1, yroot));
            }
        }

        if (mode != QuickTileMode(QuickTileFlag::None)) {
            if (yroot <= area.y() + area.height() * options->electricBorderCornerRatio()) {
                mode |= QuickTileFlag::Top;
            } else if (yroot >= area.y() + area.height() - area.height() * options->electricBorderCornerRatio()) {
                mode |= QuickTileFlag::Bottom;
            }
        } else if (options->electricBorderMaximize() && yroot <= area.y() + 5 && isMaximizable()) {
            mode = QuickTileFlag::Maximize;
            innerBorder = isInScreen(QPoint(xroot, area.y() - 1));
        }
        break; // no point in checking other screens to contain this... "point"...
    }
    if (mode != electricBorderMode()) {
        setElectricBorderMode(mode);
        if (innerBorder) {
            if (!m_electricMaximizingDelay) {
                m_electricMaximizingDelay = new QTimer(this);
                m_electricMaximizingDelay->setInterval(250);
                m_electricMaximizingDelay->setSingleShot(true);
                connect(m_electricMaximizingDelay, &QTimer::timeout, this, [this]() {
                    if (isInteractiveMove()) {
                        setElectricBorderMaximizing(electricBorderMode() != QuickTileMode(QuickTileFlag::None));
                    }
                });
            }
            m_electricMaximizingDelay->start();
        } else {
            setElectricBorderMaximizing(mode != QuickTileMode(QuickTileFlag::None));
        }
    }
}

void Window::resetQuickTilingMaximizationZones()
{
    if (electricBorderMode() != QuickTileMode(QuickTileFlag::None)) {
        if (m_electricMaximizingDelay) {
            m_electricMaximizingDelay->stop();
        }
        setElectricBorderMaximizing(false);
        setElectricBorderMode(QuickTileFlag::None);
    }
}

void Window::keyPressEvent(uint key_code)
{
    if (!isInteractiveMove() && !isInteractiveResize()) {
        return;
    }
    bool is_control = key_code & Qt::CTRL;
    bool is_alt = key_code & Qt::ALT;
    key_code = key_code & ~Qt::KeyboardModifierMask;
    int delta = is_control ? 1 : is_alt ? 32
                                        : 8;
    QPointF pos = Cursors::self()->mouse()->pos();
    switch (key_code) {
    case Qt::Key_Left:
        pos.rx() -= delta;
        break;
    case Qt::Key_Right:
        pos.rx() += delta;
        break;
    case Qt::Key_Up:
        pos.ry() -= delta;
        break;
    case Qt::Key_Down:
        pos.ry() += delta;
        break;
    case Qt::Key_Space:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        setInteractiveMoveResizePointerButtonDown(false);
        finishInteractiveMoveResize(false);
        updateCursor();
        break;
    case Qt::Key_Escape:
        setInteractiveMoveResizePointerButtonDown(false);
        finishInteractiveMoveResize(true);
        updateCursor();
        break;
    default:
        return;
    }
    Cursors::self()->mouse()->setPos(pos);
}

QSizeF Window::resizeIncrements() const
{
    return QSizeF(1, 1);
}

void Window::dontInteractiveMoveResize()
{
    setInteractiveMoveResizePointerButtonDown(false);
    stopDelayedInteractiveMoveResize();
    if (isInteractiveMoveResize()) {
        finishInteractiveMoveResize(false);
    }
}

Gravity Window::mouseGravity() const
{
    if (isDecorated()) {
        switch (decoration()->sectionUnderMouse()) {
        case Qt::BottomLeftSection:
            return Gravity::BottomLeft;
        case Qt::BottomRightSection:
            return Gravity::BottomRight;
        case Qt::BottomSection:
            return Gravity::Bottom;
        case Qt::LeftSection:
            return Gravity::Left;
        case Qt::RightSection:
            return Gravity::Right;
        case Qt::TopSection:
            return Gravity::Top;
        case Qt::TopLeftSection:
            return Gravity::TopLeft;
        case Qt::TopRightSection:
            return Gravity::TopRight;
        default:
            return Gravity::None;
        }
    }
    return Gravity::None;
}

void Window::endInteractiveMoveResize()
{
    setInteractiveMoveResizePointerButtonDown(false);
    stopDelayedInteractiveMoveResize();
    if (isInteractiveMoveResize()) {
        finishInteractiveMoveResize(false);
        setInteractiveMoveResizeGravity(mouseGravity());
    }
    updateCursor();
}

void Window::setDecoration(std::shared_ptr<KDecoration2::Decoration> decoration)
{
    if (m_decoration.decoration == decoration) {
        return;
    }
    if (decoration) {
        QMetaObject::invokeMethod(decoration.get(), QOverload<>::of(&KDecoration2::Decoration::update), Qt::QueuedConnection);
        connect(decoration.get(), &KDecoration2::Decoration::shadowChanged, this, &Window::updateShadow);
        connect(decoration.get(), &KDecoration2::Decoration::bordersChanged,
                this, &Window::updateDecorationInputShape);
        connect(decoration.get(), &KDecoration2::Decoration::resizeOnlyBordersChanged,
                this, &Window::updateDecorationInputShape);
        connect(decoration.get(), &KDecoration2::Decoration::bordersChanged, this, [this]() {
            GeometryUpdatesBlocker blocker(this);
            const QRectF oldGeometry = moveResizeGeometry();
            if (!isShade()) {
                checkWorkspacePosition(oldGeometry);
            }
            Q_EMIT geometryShapeChanged(this, oldGeometry);
        });
        connect(decoratedClient()->decoratedClient(), &KDecoration2::DecoratedClient::sizeChanged,
                this, &Window::updateDecorationInputShape);
    }
    m_decoration.decoration = decoration;
    updateDecorationInputShape();
    Q_EMIT decorationChanged();
}

void Window::updateDecorationInputShape()
{
    if (!isDecorated()) {
        m_decoration.inputRegion = QRegion();
        return;
    }

    const QMargins borders = decoration()->borders();
    const QMargins resizeBorders = decoration()->resizeOnlyBorders();

    const QRectF innerRect = QRectF(QPointF(borderLeft(), borderTop()), decoratedClient()->size());
    const QRectF outerRect = innerRect + borders + resizeBorders;

    m_decoration.inputRegion = QRegion(outerRect.toAlignedRect()) - innerRect.toAlignedRect();
}

bool Window::decorationHasAlpha() const
{
    if (!isDecorated() || decoration()->isOpaque()) {
        // either no decoration or decoration has alpha disabled
        return false;
    }
    return true;
}

void Window::triggerDecorationRepaint()
{
    if (isDecorated()) {
        decoration()->update();
    }
}

void Window::layoutDecorationRects(QRectF &left, QRectF &top, QRectF &right, QRectF &bottom) const
{
    if (!isDecorated()) {
        return;
    }
    QRectF r = decoration()->rect();

    top = QRectF(r.x(), r.y(), r.width(), borderTop());
    bottom = QRectF(r.x(), r.y() + r.height() - borderBottom(),
                    r.width(), borderBottom());
    left = QRectF(r.x(), r.y() + top.height(),
                  borderLeft(), r.height() - top.height() - bottom.height());
    right = QRectF(r.x() + r.width() - borderRight(), r.y() + top.height(),
                   borderRight(), r.height() - top.height() - bottom.height());
}

void Window::processDecorationMove(const QPointF &localPos, const QPointF &globalPos)
{
    if (isInteractiveMoveResizePointerButtonDown()) {
        handleInteractiveMoveResize(localPos.x(), localPos.y(), globalPos.x(), globalPos.y());
        return;
    }
    // TODO: handle modifiers
    Gravity newGravity = mouseGravity();
    if (newGravity != interactiveMoveResizeGravity()) {
        setInteractiveMoveResizeGravity(newGravity);
        updateCursor();
    }
}

bool Window::processDecorationButtonPress(QMouseEvent *event, bool ignoreMenu)
{
    Options::MouseCommand com = Options::MouseNothing;
    bool active = isActive();
    if (!wantsInput()) { // we cannot be active, use it anyway
        active = true;
    }

    // check whether it is a double click
    if (event->button() == Qt::LeftButton && titlebarPositionUnderMouse()) {
        if (m_decoration.doubleClickTimer.isValid()) {
            const qint64 interval = m_decoration.doubleClickTimer.elapsed();
            m_decoration.doubleClickTimer.invalidate();
            if (interval > QGuiApplication::styleHints()->mouseDoubleClickInterval()) {
                m_decoration.doubleClickTimer.start(); // expired -> new first click and pot. init
            } else {
                Workspace::self()->performWindowOperation(this, options->operationTitlebarDblClick());
                dontInteractiveMoveResize();
                return false;
            }
        } else {
            m_decoration.doubleClickTimer.start(); // new first click and pot. init, could be invalidated by release - see below
        }
    }

    if (event->button() == Qt::LeftButton) {
        com = active ? options->commandActiveTitlebar1() : options->commandInactiveTitlebar1();
    } else if (event->button() == Qt::MiddleButton) {
        com = active ? options->commandActiveTitlebar2() : options->commandInactiveTitlebar2();
    } else if (event->button() == Qt::RightButton) {
        com = active ? options->commandActiveTitlebar3() : options->commandInactiveTitlebar3();
    }
    if (event->button() == Qt::LeftButton
        && com != Options::MouseOperationsMenu // actions where it's not possible to get the matching
        && com != Options::MouseMinimize) // mouse release event
    {
        setInteractiveMoveResizeGravity(mouseGravity());
        setInteractiveMoveResizePointerButtonDown(true);
        setInteractiveMoveOffset(event->pos());
        setInvertedInteractiveMoveOffset(rect().bottomRight() - interactiveMoveOffset());
        setUnrestrictedInteractiveMoveResize(false);
        startDelayedInteractiveMoveResize();
        updateCursor();
    }
    // In the new API the decoration may process the menu action to display an inactive tab's menu.
    // If the event is unhandled then the core will create one for the active window in the group.
    if (!ignoreMenu || com != Options::MouseOperationsMenu) {
        performMouseCommand(com, event->globalPos());
    }
    return !( // Return events that should be passed to the decoration in the new API
        com == Options::MouseRaise || com == Options::MouseOperationsMenu || com == Options::MouseActivateAndRaise || com == Options::MouseActivate || com == Options::MouseActivateRaiseAndPassClick || com == Options::MouseActivateAndPassClick || com == Options::MouseNothing);
}

void Window::processDecorationButtonRelease(QMouseEvent *event)
{
    if (isDecorated()) {
        if (event->isAccepted() || !titlebarPositionUnderMouse()) {
            invalidateDecorationDoubleClickTimer(); // click was for the deco and shall not init a doubleclick
        }
    }

    if (event->buttons() == Qt::NoButton) {
        setInteractiveMoveResizePointerButtonDown(false);
        stopDelayedInteractiveMoveResize();
        if (isInteractiveMoveResize()) {
            finishInteractiveMoveResize(false);
            setInteractiveMoveResizeGravity(mouseGravity());
        }
        updateCursor();
    }
}

void Window::startDecorationDoubleClickTimer()
{
    m_decoration.doubleClickTimer.start();
}

void Window::invalidateDecorationDoubleClickTimer()
{
    m_decoration.doubleClickTimer.invalidate();
}

bool Window::providesContextHelp() const
{
    return false;
}

void Window::showContextHelp()
{
}

QPointer<Decoration::DecoratedClientImpl> Window::decoratedClient() const
{
    return m_decoration.client;
}

void Window::setDecoratedClient(QPointer<Decoration::DecoratedClientImpl> client)
{
    m_decoration.client = client;
}

void Window::pointerEnterEvent(const QPointF &globalPos)
{
    if (options->isShadeHover()) {
        cancelShadeHoverTimer();
        startShadeHoverTimer();
    }

    if (options->focusPolicy() == Options::ClickToFocus || workspace()->userActionsMenu()->isShown()) {
        return;
    }

    if (options->isAutoRaise() && !isDesktop() && !isDock() && workspace()->focusChangeEnabled() && globalPos != workspace()->focusMousePosition() && workspace()->topWindowOnDesktop(VirtualDesktopManager::self()->currentDesktop(), options->isSeparateScreenFocus() ? output() : nullptr) != this) {
        startAutoRaise();
    }

    if (isDesktop() || isDock()) {
        return;
    }
    // for FocusFollowsMouse, change focus only if the mouse has actually been moved, not if the focus
    // change came because of window changes (e.g. closing a window) - #92290
    if (options->focusPolicy() != Options::FocusFollowsMouse
        || globalPos != workspace()->focusMousePosition()) {
        workspace()->requestDelayFocus(this);
    }
}

void Window::pointerLeaveEvent()
{
    cancelAutoRaise();
    workspace()->cancelDelayFocus();
    cancelShadeHoverTimer();
    startShadeUnhoverTimer();
    // TODO: send hover leave to deco
    // TODO: handle Options::FocusStrictlyUnderMouse
}

QRectF Window::iconGeometry() const
{
    if (!windowManagementInterface() || !waylandServer()) {
        // window management interface is only available if the surface is mapped
        return QRectF();
    }

    int minDistance = INT_MAX;
    Window *candidatePanel = nullptr;
    QRectF candidateGeom;

    const auto minGeometries = windowManagementInterface()->minimizedGeometries();
    for (auto i = minGeometries.constBegin(), end = minGeometries.constEnd(); i != end; ++i) {
        Window *panel = waylandServer()->findWindow(i.key());
        if (!panel) {
            continue;
        }
        const int distance = QPointF(panel->pos() - pos()).manhattanLength();
        if (distance < minDistance) {
            minDistance = distance;
            candidatePanel = panel;
            candidateGeom = i.value();
        }
    }
    if (!candidatePanel) {
        return QRectF();
    }
    return candidateGeom.translated(candidatePanel->pos());
}

QRectF Window::virtualKeyboardGeometry() const
{
    return m_virtualKeyboardGeometry;
}

void Window::setVirtualKeyboardGeometry(const QRectF &geo)
{
    // No keyboard anymore
    if (geo.isEmpty() && !m_keyboardGeometryRestore.isEmpty()) {
        const QRectF availableArea = workspace()->clientArea(MaximizeArea, this);
        QRectF newWindowGeometry = (requestedMaximizeMode() & MaximizeHorizontal) ? availableArea : m_keyboardGeometryRestore;
        moveResize(newWindowGeometry);
        m_keyboardGeometryRestore = QRectF();
    } else if (geo.isEmpty()) {
        return;
        // The keyboard has just been opened (rather than resized) save window geometry for a restore
    } else if (m_keyboardGeometryRestore.isEmpty()) {
        m_keyboardGeometryRestore = moveResizeGeometry();
    }

    m_virtualKeyboardGeometry = geo;

    // Don't resize Desktop and fullscreen windows
    if (isRequestedFullScreen() || isDesktop()) {
        return;
    }

    if (!geo.intersects(m_keyboardGeometryRestore)) {
        return;
    }

    const QRectF availableArea = workspace()->clientArea(MaximizeArea, this);
    QRectF newWindowGeometry = (requestedMaximizeMode() & MaximizeHorizontal) ? availableArea : m_keyboardGeometryRestore;
    newWindowGeometry.setHeight(std::min(newWindowGeometry.height(), geo.top() - availableArea.top()));
    newWindowGeometry.moveTop(std::max(geo.top() - newWindowGeometry.height(), availableArea.top()));
    newWindowGeometry = newWindowGeometry.intersected(availableArea);
    moveResize(newWindowGeometry);
}

QRectF Window::keyboardGeometryRestore() const
{
    return m_keyboardGeometryRestore;
}

void Window::setKeyboardGeometryRestore(const QRectF &geom)
{
    m_keyboardGeometryRestore = geom;
}

bool Window::dockWantsInput() const
{
    return false;
}

void Window::setDesktopFileName(const QString &name)
{
    const QString effectiveName = rules()->checkDesktopFile(name);
    if (effectiveName == m_desktopFileName) {
        return;
    }
    m_desktopFileName = effectiveName;
    updateWindowRules(Rules::DesktopFile);
    Q_EMIT desktopFileNameChanged();
}

QString Window::iconFromDesktopFile(const QString &desktopFileName)
{
    const QString absolutePath = findDesktopFile(desktopFileName);
    if (absolutePath.isEmpty()) {
        return {};
    }

    KDesktopFile df(absolutePath);
    return df.readIcon();
}

QString Window::iconFromDesktopFile() const
{
    return iconFromDesktopFile(m_desktopFileName);
}

QString Window::findDesktopFile(const QString &desktopFileName)
{
    if (desktopFileName.isEmpty()) {
        return {};
    }

    const QString desktopFileNameWithPrefix = desktopFileName + QLatin1String(".desktop");
    QString desktopFilePath;

    if (QDir::isAbsolutePath(desktopFileName)) {
        if (QFile::exists(desktopFileNameWithPrefix)) {
            desktopFilePath = desktopFileNameWithPrefix;
        } else {
            desktopFilePath = desktopFileName;
        }
    }

    if (desktopFilePath.isEmpty()) {
        desktopFilePath = QStandardPaths::locate(QStandardPaths::ApplicationsLocation,
                                                 desktopFileNameWithPrefix);
    }
    if (desktopFilePath.isEmpty()) {
        desktopFilePath = QStandardPaths::locate(QStandardPaths::ApplicationsLocation,
                                                 desktopFileName);
    }
    return desktopFilePath;
}

bool Window::hasApplicationMenu() const
{
    return Workspace::self()->applicationMenu()->applicationMenuEnabled() && !m_applicationMenuServiceName.isEmpty() && !m_applicationMenuObjectPath.isEmpty();
}

void Window::updateApplicationMenuServiceName(const QString &serviceName)
{
    const bool old_hasApplicationMenu = hasApplicationMenu();

    m_applicationMenuServiceName = serviceName;

    const bool new_hasApplicationMenu = hasApplicationMenu();

    Q_EMIT applicationMenuChanged();
    if (old_hasApplicationMenu != new_hasApplicationMenu) {
        Q_EMIT hasApplicationMenuChanged(new_hasApplicationMenu);
    }
}

void Window::updateApplicationMenuObjectPath(const QString &objectPath)
{
    const bool old_hasApplicationMenu = hasApplicationMenu();

    m_applicationMenuObjectPath = objectPath;

    const bool new_hasApplicationMenu = hasApplicationMenu();

    Q_EMIT applicationMenuChanged();
    if (old_hasApplicationMenu != new_hasApplicationMenu) {
        Q_EMIT hasApplicationMenuChanged(new_hasApplicationMenu);
    }
}

void Window::setApplicationMenuActive(bool applicationMenuActive)
{
    if (m_applicationMenuActive != applicationMenuActive) {
        m_applicationMenuActive = applicationMenuActive;
        Q_EMIT applicationMenuActiveChanged(applicationMenuActive);
    }
}

void Window::showApplicationMenu(int actionId)
{
    if (isDecorated()) {
        decoration()->showApplicationMenu(actionId);
    } else {
        // we don't know where the application menu button will be, show it in the top left corner instead
        Workspace::self()->showApplicationMenu(QRect(), this, actionId);
    }
}

bool Window::unresponsive() const
{
    return m_unresponsive;
}

void Window::setUnresponsive(bool unresponsive)
{
    if (m_unresponsive != unresponsive) {
        m_unresponsive = unresponsive;
        Q_EMIT unresponsiveChanged(m_unresponsive);
        Q_EMIT captionChanged();
    }
}

QString Window::shortcutCaptionSuffix() const
{
    if (shortcut().isEmpty()) {
        return QString();
    }
    return QLatin1String(" {") + shortcut().toString() + QLatin1Char('}');
}

Window *Window::findWindowWithSameCaption() const
{
    auto fetchNameInternalPredicate = [this](const Window *cl) {
        return (!cl->isSpecialWindow() || cl->isToolbar()) && cl != this && cl->captionNormal() == captionNormal() && cl->captionSuffix() == captionSuffix();
    };
    return workspace()->findAbstractClient(fetchNameInternalPredicate);
}

QString Window::caption() const
{
    QString cap = captionNormal() + captionSuffix();
    if (unresponsive()) {
        cap += QLatin1String(" ");
        cap += i18nc("Application is not responding, appended to window title", "(Not Responding)");
    }
    return cap;
}

void Window::removeRule(Rules *rule)
{
    m_rules.remove(rule);
}

void Window::discardTemporaryRules()
{
    m_rules.discardTemporary();
}

void Window::evaluateWindowRules()
{
    setupWindowRules(true);
    applyWindowRules();
}

/**
 * Returns the list of activities the window window is on.
 * if it's on all activities, the list will be empty.
 * Don't use this, use isOnActivity() and friends (from class Window)
 */
QStringList Window::activities() const
{
    return m_activityList;
}

/**
 * Sets whether the window is on @p activity.
 * If you remove it from its last activity, then it's on all activities.
 *
 * Note: If it was on all activities and you try to remove it from one, nothing will happen;
 * I don't think that's an important enough use case to handle here.
 */
void Window::setOnActivity(const QString &activity, bool enable)
{
#if KWIN_BUILD_ACTIVITIES
    if (!Workspace::self()->activities()) {
        return;
    }
    QStringList newActivitiesList = activities();
    if (newActivitiesList.contains(activity) == enable) {
        // nothing to do
        return;
    }
    if (enable) {
        QStringList allActivities = Workspace::self()->activities()->all();
        if (!allActivities.contains(activity)) {
            // bogus ID
            return;
        }
        newActivitiesList.append(activity);
    } else {
        newActivitiesList.removeOne(activity);
    }
    setOnActivities(newActivitiesList);
#endif
}

/**
 * set exactly which activities this window is on
 */
void Window::setOnActivities(const QStringList &newActivitiesList)
{
#if KWIN_BUILD_ACTIVITIES
    if (!Workspace::self()->activities()) {
        return;
    }
    if (Workspace::self()->activities()->serviceStatus() != KActivities::Consumer::Running) {
        return;
    }
    const auto allActivities = Workspace::self()->activities()->all();
    const auto activityList = [&] {
        auto result = rules()->checkActivity(newActivitiesList);

        const auto it = std::remove_if(result.begin(), result.end(), [=](const QString &activity) {
            return !allActivities.contains(activity);
        });
        result.erase(it, result.end());
        return result;
    }();

    const auto allActivityExplicitlyRequested = activityList.isEmpty() || activityList.contains(Activities::nullUuid());
    const auto allActivitiesCovered = activityList.size() > 1 && activityList.size() == allActivities.size();

    if (allActivityExplicitlyRequested || allActivitiesCovered) {
        if (!m_activityList.isEmpty()) {
            m_activityList.clear();
            doSetOnActivities(m_activityList);
        }
    } else {
        if (m_activityList != activityList) {
            m_activityList = activityList;
            doSetOnActivities(m_activityList);
        }
    }

    updateActivities(false);
#endif
}

/**
 * if @p all is true, sets on all activities.
 * if it's false, sets it to only be on the current activity
 */
void Window::setOnAllActivities(bool all)
{
#if KWIN_BUILD_ACTIVITIES
    if (all == isOnAllActivities()) {
        return;
    }
    if (all) {
        setOnActivities(QStringList());
    } else {
        setOnActivity(Workspace::self()->activities()->current(), true);
    }
#endif
}

/**
 * update after activities changed
 */
void Window::updateActivities(bool includeTransients)
{
    if (m_activityUpdatesBlocked) {
        m_blockedActivityUpdatesRequireTransients |= includeTransients;
        return;
    }
    Q_EMIT activitiesChanged(this);
    m_blockedActivityUpdatesRequireTransients = false; // reset
    Workspace::self()->focusChain()->update(this, FocusChain::MakeFirst);
    updateWindowRules(Rules::Activity);
}

void Window::blockActivityUpdates(bool b)
{
    if (b) {
        ++m_activityUpdatesBlocked;
    } else {
        Q_ASSERT(m_activityUpdatesBlocked);
        --m_activityUpdatesBlocked;
        if (!m_activityUpdatesBlocked) {
            updateActivities(m_blockedActivityUpdatesRequireTransients);
        }
    }
}

void Window::checkNoBorder()
{
    setNoBorder(false);
}

bool Window::groupTransient() const
{
    return false;
}

const Group *Window::group() const
{
    return nullptr;
}

Group *Window::group()
{
    return nullptr;
}

bool Window::supportsWindowRules() const
{
    return false;
}

QPointF Window::framePosToClientPos(const QPointF &point) const
{
    return point + QPointF(borderLeft(), borderTop());
}

QPointF Window::clientPosToFramePos(const QPointF &point) const
{
    return point - QPointF(borderLeft(), borderTop());
}

QSizeF Window::frameSizeToClientSize(const QSizeF &size) const
{
    const qreal width = size.width() - borderLeft() - borderRight();
    const qreal height = size.height() - borderTop() - borderBottom();
    return QSizeF(width, height);
}

QSizeF Window::clientSizeToFrameSize(const QSizeF &size) const
{
    const qreal width = size.width() + borderLeft() + borderRight();
    const qreal height = size.height() + borderTop() + borderBottom();
    return QSizeF(width, height);
}

QRectF Window::frameRectToClientRect(const QRectF &rect) const
{
    const QPointF position = framePosToClientPos(rect.topLeft());
    const QSizeF size = frameSizeToClientSize(rect.size());
    return QRectF(position, size);
}

QRectF Window::clientRectToFrameRect(const QRectF &rect) const
{
    const QPointF position = clientPosToFramePos(rect.topLeft());
    const QSizeF size = clientSizeToFrameSize(rect.size());
    return QRectF(position, size);
}

QRectF Window::moveResizeGeometry() const
{
    return m_moveResizeGeometry;
}

void Window::setMoveResizeGeometry(const QRectF &geo)
{
    m_moveResizeGeometry = geo;
    m_moveResizeOutput = workspace()->outputAt(geo.center());
}

Output *Window::moveResizeOutput() const
{
    return m_moveResizeOutput;
}

void Window::setMoveResizeOutput(Output *output)
{
    m_moveResizeOutput = output;
}

void Window::move(const QPointF &point)
{
    const QRectF rect = QRectF(point, m_moveResizeGeometry.size());

    setMoveResizeGeometry(rect);
    moveResizeInternal(rect, MoveResizeMode::Move);
}

void Window::resize(const QSizeF &size)
{
    const QRectF rect = QRectF(m_moveResizeGeometry.topLeft(), size);

    setMoveResizeGeometry(rect);
    moveResizeInternal(rect, MoveResizeMode::Resize);
}

void Window::moveResize(const QRectF &rect)
{
    setMoveResizeGeometry(rect);
    moveResizeInternal(rect, MoveResizeMode::MoveResize);
}

void Window::setElectricBorderMode(QuickTileMode mode)
{
    if (mode != QuickTileMode(QuickTileFlag::Maximize)) {
        // sanitize the mode, ie. simplify "invalid" combinations
        if ((mode & QuickTileFlag::Horizontal) == QuickTileMode(QuickTileFlag::Horizontal)) {
            mode &= ~QuickTileMode(QuickTileFlag::Horizontal);
        }
        if ((mode & QuickTileFlag::Vertical) == QuickTileMode(QuickTileFlag::Vertical)) {
            mode &= ~QuickTileMode(QuickTileFlag::Vertical);
        }
    }
    m_electricMode = mode;
}

void Window::setElectricBorderMaximizing(bool maximizing)
{
    m_electricMaximizing = maximizing;
    if (maximizing) {
        workspace()->outline()->show(quickTileGeometry(electricBorderMode(), Cursors::self()->mouse()->pos()).toRect(), moveResizeGeometry().toRect());
    } else {
        workspace()->outline()->hide();
    }
    elevate(maximizing);
}

QRectF Window::quickTileGeometry(QuickTileMode mode, const QPointF &pos) const
{
    if (mode == QuickTileMode(QuickTileFlag::Maximize)) {
        if (requestedMaximizeMode() == MaximizeFull) {
            return geometryRestore();
        } else {
            return workspace()->clientArea(MaximizeArea, this, pos);
        }
    }

    Output *output = workspace()->outputAt(pos);

    if (mode & QuickTileFlag::Custom) {
        Tile *tile = workspace()->tileManager(output)->bestTileForPosition(pos);
        if (tile) {
            return tile->windowGeometry();
        } else {
            return QRectF();
        }
    }

    Tile *tile = workspace()->tileManager(output)->quickTile(mode);
    if (tile) {
        return tile->windowGeometry();
    }
    return workspace()->clientArea(MaximizeArea, this, pos);
}

void Window::updateElectricGeometryRestore()
{
    m_electricGeometryRestore = geometryRestore();
    if (quickTileMode() == QuickTileMode(QuickTileFlag::None)) {
        if (!(requestedMaximizeMode() & MaximizeHorizontal)) {
            m_electricGeometryRestore.setX(x());
            m_electricGeometryRestore.setWidth(width());
        }
        if (!(requestedMaximizeMode() & MaximizeVertical)) {
            m_electricGeometryRestore.setY(y());
            m_electricGeometryRestore.setHeight(height());
        }
    }
}

QRectF Window::quickTileGeometryRestore() const
{
    if (quickTileMode() != QuickTileMode(QuickTileFlag::None)) {
        // If the window is tiled, geometryRestore() already has a good value.
        return geometryRestore();
    }

    if (isElectricBorderMaximizing()) {
        return m_electricGeometryRestore;
    } else {
        return moveResizeGeometry();
    }
}

void Window::setQuickTileMode(QuickTileMode mode, bool keyboard)
{
    // Only allow quick tile on a regular window.
    if (!isResizable()) {
        return;
    }
    if (isAppletPopup()) {
        return;
    }

    workspace()->updateFocusMousePosition(Cursors::self()->mouse()->pos()); // may cause leave event

    GeometryUpdatesBlocker blocker(this);

    setTile(nullptr);

    if (mode == QuickTileMode(QuickTileFlag::Maximize)) {
        if (requestedMaximizeMode() == MaximizeFull) {
            m_quickTileMode = int(QuickTileFlag::None);
            setMaximize(false, false);
        } else {
            QRectF effectiveGeometryRestore = quickTileGeometryRestore();
            m_quickTileMode = int(QuickTileFlag::Maximize);
            setMaximize(true, true);
            setGeometryRestore(effectiveGeometryRestore);
        }
        doSetQuickTileMode();
        Q_EMIT quickTileModeChanged();
        return;
    }

    // sanitize the mode, ie. simplify "invalid" combinations
    if ((mode & QuickTileFlag::Horizontal) == QuickTileMode(QuickTileFlag::Horizontal)) {
        mode &= ~QuickTileMode(QuickTileFlag::Horizontal);
    }
    if ((mode & QuickTileFlag::Vertical) == QuickTileMode(QuickTileFlag::Vertical)) {
        mode &= ~QuickTileMode(QuickTileFlag::Vertical);
    }

    // restore from maximized so that it is possible to tile maximized windows with one hit or by dragging
    if (requestedMaximizeMode() != MaximizeRestore) {

        if (mode != QuickTileMode(QuickTileFlag::None)) {
            m_quickTileMode = int(QuickTileFlag::None); // Temporary, so the maximize code doesn't get all confused

            setMaximize(false, false);

            moveResize(quickTileGeometry(mode, keyboard ? moveResizeGeometry().center() : Cursors::self()->mouse()->pos()));
            // Store the mode change
            m_quickTileMode = mode;
        } else {
            m_quickTileMode = mode;
            setMaximize(false, false);
        }

        doSetQuickTileMode();
        Q_EMIT quickTileModeChanged();

        return;
    }

    QPointF whichScreen = keyboard ? moveResizeGeometry().center() : Cursors::self()->mouse()->pos();
    if (mode != QuickTileMode(QuickTileFlag::None)) {
        // If trying to tile to the side that the window is already tiled to move the window to the next
        // screen near the tile if it exists and swap the tile side, otherwise toggle the mode (set QuickTileFlag::None)
        if (quickTileMode() == mode) {
            Output *currentOutput = moveResizeOutput();
            Output *nextOutput = currentOutput;
            Output *candidateOutput = currentOutput;
            if ((mode & QuickTileFlag::Horizontal) == QuickTileMode(QuickTileFlag::Left)) {
                candidateOutput = workspace()->findOutput(nextOutput, Workspace::DirectionWest);
            } else if ((mode & QuickTileFlag::Horizontal) == QuickTileMode(QuickTileFlag::Right)) {
                candidateOutput = workspace()->findOutput(nextOutput, Workspace::DirectionEast);
            }
            bool shiftHorizontal = candidateOutput != nextOutput;
            nextOutput = candidateOutput;
            if ((mode & QuickTileFlag::Vertical) == QuickTileMode(QuickTileFlag::Top)) {
                candidateOutput = workspace()->findOutput(nextOutput, Workspace::DirectionNorth);
            } else if ((mode & QuickTileFlag::Vertical) == QuickTileMode(QuickTileFlag::Bottom)) {
                candidateOutput = workspace()->findOutput(nextOutput, Workspace::DirectionSouth);
            }
            bool shiftVertical = candidateOutput != nextOutput;
            nextOutput = candidateOutput;

            if (nextOutput == currentOutput) {
                mode = QuickTileFlag::None; // No other screens in the tile direction, toggle tiling
            } else {
                // Move to other screen
                moveResize(geometryRestore().translated(nextOutput->geometry().topLeft() - currentOutput->geometry().topLeft()));
                whichScreen = nextOutput->geometry().center();

                // Swap sides
                if (shiftHorizontal) {
                    mode = (~mode & QuickTileFlag::Horizontal) | (mode & QuickTileFlag::Vertical);
                }
                if (shiftVertical) {
                    mode = (~mode & QuickTileFlag::Vertical) | (mode & QuickTileFlag::Horizontal);
                }
            }
        } else if (quickTileMode() == QuickTileMode(QuickTileFlag::None)) {
            // Not coming out of an existing tile, not shifting monitors, we're setting a brand new tile.
            // Store geometry first, so we can go out of this tile later.
            setGeometryRestore(quickTileGeometryRestore());
        }

        m_quickTileMode = mode;
    }

    if (mode == QuickTileMode(QuickTileFlag::None)) {
        setTile(nullptr);
        m_quickTileMode = int(QuickTileFlag::None);
        // Untiling, so just restore geometry, and we're done.
        if (geometryRestore().isValid()) { // invalid if we started maximized and wait for placement
            moveResize(geometryRestore());
        }
        checkWorkspacePosition(); // Just in case it's a different screen
    } else if (mode == QuickTileMode(QuickTileFlag::Custom)) {
        Tile *tile = nullptr;
        if (keyboard) {
            tile = workspace()->tileManager(output())->bestTileForPosition(moveResizeGeometry().center());
        } else {
            Output *output = workspace()->outputAt(Cursors::self()->mouse()->pos());
            tile = workspace()->tileManager(output)->bestTileForPosition(Cursors::self()->mouse()->pos());
        }
        setTile(tile);
    } else {
        // Use whichScreen to move to next screen when retiling to the same edge as the old behavior
        Output *output = workspace()->outputAt(whichScreen);
        Tile *tile = workspace()->tileManager(output)->quickTile(mode);
        setTile(tile);
    }

    doSetQuickTileMode();
    Q_EMIT quickTileModeChanged();
}

void Window::setTile(Tile *tile)
{
    if (m_tile == tile) {
        return;
    } else if (m_tile) {
        m_tile->removeWindow(this);
    }

    m_tile = tile;

    if (m_tile) {
        m_tile->addWindow(this);
    }

    Q_EMIT tileChanged(tile);
}

Tile *Window::tile() const
{
    return m_tile;
}

void Window::doSetQuickTileMode()
{
}

QRectF Window::moveToArea(const QRectF &geometry, const QRectF &oldArea, const QRectF &newArea)
{
    QRectF ret = geometry;
    // move the window to have the same relative position to the center of the screen
    // (i.e. one near the middle of the right edge will also end up near the middle of the right edge)
    QPointF center = geometry.center() - oldArea.center();
    center.setX(center.x() * newArea.width() / oldArea.width());
    center.setY(center.y() * newArea.height() / oldArea.height());
    center += newArea.center();
    ret.moveCenter(center);

    // If the window was inside the old screen area, explicitly make sure its inside also the new screen area
    if (oldArea.contains(geometry)) {
        ret = keepInArea(ret, newArea);
    }
    return ret;
}

QRectF Window::ensureSpecialStateGeometry(const QRectF &geometry)
{
    if (isRequestedFullScreen()) {
        return workspace()->clientArea(FullScreenArea, this, geometry.center());
    } else if (requestedMaximizeMode() != MaximizeRestore) {
        const QRectF maximizeArea = workspace()->clientArea(MaximizeArea, this, geometry.center());
        QRectF ret = geometry;
        if (requestedMaximizeMode() & MaximizeHorizontal) {
            ret.setX(maximizeArea.x());
            ret.setWidth(maximizeArea.width());
        }
        if (requestedMaximizeMode() & MaximizeVertical) {
            ret.setY(maximizeArea.y());
            ret.setHeight(maximizeArea.height());
        }
        return ret;
    } else if (quickTileMode() != QuickTileMode(QuickTileFlag::None)) {
        return quickTileGeometry(quickTileMode(), geometry.center());
    } else {
        return geometry;
    }
}

void Window::sendToOutput(Output *newOutput)
{
    newOutput = rules()->checkOutput(newOutput);
    if (isActive()) {
        workspace()->setActiveOutput(newOutput);
        // might impact the layer of a fullscreen window
        const auto windows = workspace()->allClientList();
        for (Window *other : windows) {
            if (other->isFullScreen() && other->output() == newOutput) {
                other->updateLayer();
            }
        }
    }
    if (moveResizeOutput() == newOutput) {
        return;
    }

    const QRectF oldGeom = moveResizeGeometry();
    const QRectF oldScreenArea = workspace()->clientArea(MaximizeArea, this, moveResizeOutput());
    const QRectF screenArea = workspace()->clientArea(MaximizeArea, this, newOutput);

    if (m_quickTileMode == QuickTileMode(QuickTileFlag::Custom)) {
        setTile(nullptr);
    }

    QRectF newGeom = moveToArea(oldGeom, oldScreenArea, screenArea);
    newGeom = ensureSpecialStateGeometry(newGeom);
    moveResize(newGeom);

    // move geometry restores to the new output as well
    m_fullscreenGeometryRestore = moveToArea(m_fullscreenGeometryRestore, oldScreenArea, screenArea);
    m_maximizeGeometryRestore = moveToArea(m_maximizeGeometryRestore, oldScreenArea, screenArea);

    auto tso = workspace()->ensureStackingOrder(transients());
    for (auto it = tso.constBegin(), end = tso.constEnd(); it != end; ++it) {
        (*it)->sendToOutput(newOutput);
    }
}

void Window::checkWorkspacePosition(QRectF oldGeometry, const VirtualDesktop *oldDesktop)
{
    if (isDock() || isDesktop() || !isPlaceable()) {
        return;
    }

    QRectF newGeom = moveResizeGeometry();

    if (!oldGeometry.isValid()) {
        oldGeometry = newGeom;
    }

    VirtualDesktop *desktop = !isOnCurrentDesktop() ? desktops().constLast() : VirtualDesktopManager::self()->currentDesktop();
    if (!oldDesktop) {
        oldDesktop = desktop;
    }

    // If the window was touching an edge before but not now move it so it is again.
    // Old and new maximums have different starting values so windows on the screen
    // edge will move when a new strut is placed on the edge.
    QRect oldScreenArea;
    QRect screenArea;
    if (workspace()->inUpdateClientArea()) {
        // check if the window is on an about to be destroyed output
        Output *newOutput = moveResizeOutput();
        if (!workspace()->outputs().contains(newOutput)) {
            newOutput = workspace()->outputAt(newGeom.center());
        }
        // we need to find the screen area as it was before the change
        oldScreenArea = workspace()->previousScreenSizes().value(moveResizeOutput());
        if (oldScreenArea.isNull()) {
            oldScreenArea = newOutput->geometry();
        }
        screenArea = newOutput->geometry();
        newGeom.translate(screenArea.topLeft() - oldScreenArea.topLeft());
    } else {
        oldScreenArea = workspace()->clientArea(ScreenArea, workspace()->outputAt(oldGeometry.center()), oldDesktop).toRect();
        screenArea = workspace()->clientArea(ScreenArea, this, newGeom.center()).toRect();
    }

    if (isRequestedFullScreen() || requestedMaximizeMode() != MaximizeRestore || quickTileMode() != QuickTileMode(QuickTileFlag::None)) {
        moveResize(ensureSpecialStateGeometry(newGeom));
        m_fullscreenGeometryRestore = moveToArea(m_fullscreenGeometryRestore, oldScreenArea, screenArea);
        m_maximizeGeometryRestore = moveToArea(m_maximizeGeometryRestore, oldScreenArea, screenArea);
        return;
    }

    const QRect oldGeomTall = QRect(oldGeometry.x(), oldScreenArea.y(), oldGeometry.width(), oldScreenArea.height()); // Full screen height
    const QRect oldGeomWide = QRect(oldScreenArea.x(), oldGeometry.y(), oldScreenArea.width(), oldGeometry.height()); // Full screen width
    int oldTopMax = oldScreenArea.y();
    int oldRightMax = oldScreenArea.x() + oldScreenArea.width();
    int oldBottomMax = oldScreenArea.y() + oldScreenArea.height();
    int oldLeftMax = oldScreenArea.x();
    int topMax = screenArea.y();
    int rightMax = screenArea.x() + screenArea.width();
    int bottomMax = screenArea.y() + screenArea.height();
    int leftMax = screenArea.x();
    const QRect newGeomTall = QRect(newGeom.x(), screenArea.y(), newGeom.width(), screenArea.height()); // Full screen height
    const QRect newGeomWide = QRect(screenArea.x(), newGeom.y(), screenArea.width(), newGeom.height()); // Full screen width
    // Get the max strut point for each side where the window is (E.g. Highest point for
    // the bottom struts bounded by the window's left and right sides).

    // These 4 compute old bounds ...
    auto moveAreaFunc = workspace()->inUpdateClientArea() ? &Workspace::previousRestrictedMoveArea : //... the restricted areas changed
        &Workspace::restrictedMoveArea; //... when e.g. active desktop or screen changes

    for (const QRect &r : (workspace()->*moveAreaFunc)(oldDesktop, StrutAreaTop)) {
        QRect rect = r & oldGeomTall;
        if (!rect.isEmpty()) {
            oldTopMax = std::max(oldTopMax, rect.y() + rect.height());
        }
    }
    for (const QRect &r : (workspace()->*moveAreaFunc)(oldDesktop, StrutAreaRight)) {
        QRect rect = r & oldGeomWide;
        if (!rect.isEmpty()) {
            oldRightMax = std::min(oldRightMax, rect.x());
        }
    }
    for (const QRect &r : (workspace()->*moveAreaFunc)(oldDesktop, StrutAreaBottom)) {
        QRect rect = r & oldGeomTall;
        if (!rect.isEmpty()) {
            oldBottomMax = std::min(oldBottomMax, rect.y());
        }
    }
    for (const QRect &r : (workspace()->*moveAreaFunc)(oldDesktop, StrutAreaLeft)) {
        QRect rect = r & oldGeomWide;
        if (!rect.isEmpty()) {
            oldLeftMax = std::max(oldLeftMax, rect.x() + rect.width());
        }
    }

    // These 4 compute new bounds
    for (const QRect &r : workspace()->restrictedMoveArea(desktop, StrutAreaTop)) {
        QRect rect = r & newGeomTall;
        if (!rect.isEmpty()) {
            topMax = std::max(topMax, rect.y() + rect.height());
        }
    }
    for (const QRect &r : workspace()->restrictedMoveArea(desktop, StrutAreaRight)) {
        QRect rect = r & newGeomWide;
        if (!rect.isEmpty()) {
            rightMax = std::min(rightMax, rect.x());
        }
    }
    for (const QRect &r : workspace()->restrictedMoveArea(desktop, StrutAreaBottom)) {
        QRect rect = r & newGeomTall;
        if (!rect.isEmpty()) {
            bottomMax = std::min(bottomMax, rect.y());
        }
    }
    for (const QRect &r : workspace()->restrictedMoveArea(desktop, StrutAreaLeft)) {
        QRect rect = r & newGeomWide;
        if (!rect.isEmpty()) {
            leftMax = std::max(leftMax, rect.x() + rect.width());
        }
    }

    // Check if the sides were inside or touching but are no longer
    enum {
        Left = 0,
        Top,
        Right,
        Bottom,
    };
    bool keep[4] = {false, false, false, false};
    bool save[4] = {false, false, false, false};
    if (oldGeometry.x() >= oldLeftMax) {
        save[Left] = newGeom.x() < leftMax;
    }
    if (oldGeometry.x() == oldLeftMax) {
        keep[Left] = newGeom.x() != leftMax;
    }

    if (oldGeometry.y() >= oldTopMax) {
        save[Top] = newGeom.y() < topMax;
    }
    if (oldGeometry.y() == oldTopMax) {
        keep[Top] = newGeom.y() != topMax;
    }

    if (oldGeometry.right() <= oldRightMax) {
        save[Right] = newGeom.right() > rightMax;
    }
    if (oldGeometry.right() == oldRightMax) {
        keep[Right] = newGeom.right() != rightMax;
    }

    if (oldGeometry.bottom() <= oldBottomMax) {
        save[Bottom] = newGeom.bottom() > bottomMax;
    }
    if (oldGeometry.bottom() == oldBottomMax) {
        keep[Bottom] = newGeom.bottom() != bottomMax;
    }

    // if randomly touches opposing edges, do not favor either
    if (keep[Left] && keep[Right]) {
        keep[Left] = keep[Right] = false;
    }
    if (keep[Top] && keep[Bottom]) {
        keep[Top] = keep[Bottom] = false;
    }

    if (save[Left] || keep[Left]) {
        newGeom.moveLeft(std::max(leftMax, screenArea.x()));
    }
    if (save[Top] || keep[Top]) {
        newGeom.moveTop(std::max(topMax, screenArea.y()));
    }
    if (save[Right] || keep[Right]) {
        newGeom.moveRight(std::min(rightMax, screenArea.right()) + 1);
    }
    if (save[Bottom] || keep[Bottom]) {
        newGeom.moveBottom(std::min(bottomMax, screenArea.bottom()) + 1);
    }

    if (oldGeometry.x() >= oldLeftMax && newGeom.x() < leftMax) {
        newGeom.setLeft(std::max(leftMax, screenArea.x()));
    }
    if (oldGeometry.y() >= oldTopMax && newGeom.y() < topMax) {
        newGeom.setTop(std::max(topMax, screenArea.y()));
    }

    checkOffscreenPosition(&newGeom, screenArea);
    // Obey size hints. TODO: We really should make sure it stays in the right place
    if (!isShade()) {
        newGeom.setSize(constrainFrameSize(newGeom.size()));
    }

    moveResize(newGeom);
}

void Window::checkOffscreenPosition(QRectF *geom, const QRectF &screenArea)
{
    if (geom->left() > screenArea.right()) {
        geom->moveLeft(screenArea.right() - screenArea.width() / 4);
    } else if (geom->right() < screenArea.left()) {
        geom->moveRight(screenArea.left() + screenArea.width() / 4);
    }
    if (geom->top() > screenArea.bottom()) {
        geom->moveTop(screenArea.bottom() - screenArea.height() / 4);
    } else if (geom->bottom() < screenArea.top()) {
        geom->moveBottom(screenArea.top() + screenArea.width() / 4);
    }
}

/**
 * Constrains the client size @p size according to a set of the window's size hints.
 *
 * Default implementation applies only minimum and maximum size constraints.
 */
QSizeF Window::constrainClientSize(const QSizeF &size, SizeMode mode) const
{
    qreal width = size.width();
    qreal height = size.height();

    // When user is resizing the window, the move resize geometry may have negative width or
    // height. In which case, we need to set negative dimensions to reasonable values.
    if (width < 1) {
        width = 1;
    }
    if (height < 1) {
        height = 1;
    }

    const QSizeF minimumSize = minSize();
    const QSizeF maximumSize = maxSize();

    width = std::clamp(width, minimumSize.width(), maximumSize.width());
    height = std::clamp(height, minimumSize.height(), maximumSize.height());

    return QSizeF(width, height);
}

/**
 * Constrains the frame size @p size according to a set of the window's size hints.
 */
QSizeF Window::constrainFrameSize(const QSizeF &size, SizeMode mode) const
{
    const QSizeF unconstrainedClientSize = frameSizeToClientSize(size);
    const QSizeF constrainedClientSize = constrainClientSize(unconstrainedClientSize, mode);
    return clientSizeToFrameSize(constrainedClientSize);
}

/**
 * Returns @c true if the Window can be shown in full screen mode; otherwise @c false.
 *
 * Default implementation returns @c false.
 */
bool Window::isFullScreenable() const
{
    return false;
}

/**
 * Returns @c true if the Window is currently being shown in full screen mode; otherwise @c false.
 *
 * A window in full screen mode occupies the entire screen with no window frame around it.
 *
 * Default implementation returns @c false.
 */
bool Window::isFullScreen() const
{
    return false;
}

bool Window::isRequestedFullScreen() const
{
    return isFullScreen();
}

/**
 * Returns whether requests initiated by the user to enter or leave full screen mode are honored.
 *
 * Default implementation returns @c false.
 */
bool Window::userCanSetFullScreen() const
{
    return false;
}

/**
 * Asks the Window to enter or leave full screen mode.
 *
 * Default implementation does nothing.
 *
 * @param set @c true if the Window has to be shown in full screen mode, otherwise @c false
 * @param user @c true if the request is initiated by the user, otherwise @c false
 */
void Window::setFullScreen(bool set, bool user)
{
    qCWarning(KWIN_CORE, "%s doesn't support setting fullscreen state", metaObject()->className());
}

/**
 * Returns @c true if the Window can be minimized; otherwise @c false.
 *
 * Default implementation returns @c false.
 */
bool Window::isMinimizable() const
{
    return false;
}

/**
 * Returns @c true if the Window can be maximized; otherwise @c false.
 *
 * Default implementation returns @c false.
 */
bool Window::isMaximizable() const
{
    return false;
}

/**
 * Returns the currently applied maximize mode.
 *
 * Default implementation returns MaximizeRestore.
 */
MaximizeMode Window::maximizeMode() const
{
    return MaximizeRestore;
}

/**
 * Returns the last requested maximize mode.
 *
 * On X11, this method always matches maximizeMode(). On Wayland, it is asynchronous.
 *
 * Default implementation matches maximizeMode().
 */
MaximizeMode Window::requestedMaximizeMode() const
{
    return maximizeMode();
}

/**
 * Returns the geometry of the Window before it was maximized or quick tiled.
 */
QRectF Window::geometryRestore() const
{
    return m_maximizeGeometryRestore;
}

/**
 * Sets the geometry of the Window before it was maximized or quick tiled to @p rect.
 */
void Window::setGeometryRestore(const QRectF &rect)
{
    m_maximizeGeometryRestore = rect;
}

void Window::invalidateDecoration()
{
}

bool Window::noBorder() const
{
    return true;
}

bool Window::userCanSetNoBorder() const
{
    return false;
}

void Window::setNoBorder(bool set)
{
    qCWarning(KWIN_CORE, "%s doesn't support setting decorations", metaObject()->className());
}

void Window::showOnScreenEdge()
{
    qCWarning(KWIN_CORE, "%s doesn't support screen edge activation", metaObject()->className());
}

bool Window::isPlaceable() const
{
    return true;
}

QRectF Window::fullscreenGeometryRestore() const
{
    return m_fullscreenGeometryRestore;
}

void Window::setFullscreenGeometryRestore(const QRectF &geom)
{
    m_fullscreenGeometryRestore = geom;
}

void Window::cleanTabBox()
{
#if KWIN_BUILD_TABBOX
    TabBox::TabBox *tabBox = workspace()->tabbox();
    if (tabBox && tabBox->isDisplayed() && tabBox->currentClient() == this) {
        tabBox->nextPrev(true);
    }
#endif
}

void Window::setupWindowRules(bool ignore_temporary)
{
    disconnect(this, &Window::captionChanged, this, &Window::evaluateWindowRules);
    m_rules = workspace()->rulebook()->find(this, ignore_temporary);
    // check only after getting the rules, because there may be a rule forcing window type
}

void Window::updateWindowRules(Rules::Types selection)
{
    if (workspace()->rulebook()->areUpdatesDisabled()) {
        return;
    }
    m_rules.update(this, selection);
}

void Window::finishWindowRules()
{
    updateWindowRules(Rules::All);
    m_rules = WindowRules();
}

// Applies Force, ForceTemporarily and ApplyNow rules
// Used e.g. after the rules have been modified using the kcm.
void Window::applyWindowRules()
{
    // apply force rules
    // Placement - does need explicit update, just like some others below
    // Geometry : setGeometry() doesn't check rules
    auto client_rules = rules();
    const QRectF oldGeometry = moveResizeGeometry();
    const QRectF geometry = client_rules->checkGeometrySafe(oldGeometry);
    if (geometry != oldGeometry) {
        moveResize(geometry);
    }
    // MinSize, MaxSize handled by Geometry
    // IgnoreGeometry
    setDesktops(desktops());
    workspace()->sendWindowToOutput(this, moveResizeOutput());
    setOnActivities(activities());
    // Type
    maximize(requestedMaximizeMode());
    // Minimize : functions don't check, and there are two functions
    if (client_rules->checkMinimize(isMinimized())) {
        minimize();
    } else {
        unminimize();
    }
    setShade(shadeMode());
    setOriginalSkipTaskbar(skipTaskbar());
    setSkipPager(skipPager());
    setSkipSwitcher(skipSwitcher());
    setKeepAbove(keepAbove());
    setKeepBelow(keepBelow());
    setFullScreen(isRequestedFullScreen(), true);
    setNoBorder(noBorder());
    setForceBlur(forceBlur());
    updateColorScheme();
    // FSP
    // AcceptFocus :
    if (workspace()->mostRecentlyActivatedWindow() == this
        && !client_rules->checkAcceptFocus(true)) {
        workspace()->activateNextWindow(this);
    }
    // Autogrouping : Only checked on window manage
    // AutogroupInForeground : Only checked on window manage
    // AutogroupById : Only checked on window manage
    // StrictGeometry
    setShortcut(rules()->checkShortcut(shortcut().toString()));
    // see also X11Window::setActive()
    if (isActive()) {
        setOpacity(rules()->checkOpacityActive(qRound(opacity() * 100.0)) / 100.0);
        workspace()->disableGlobalShortcutsForClient(rules()->checkDisableGlobalShortcuts(false));
    } else {
        setOpacity(rules()->checkOpacityInactive(qRound(opacity() * 100.0)) / 100.0);
    }
    setDesktopFileName(rules()->checkDesktopFile(desktopFileName()));
}

void Window::setLastUsageSerial(quint32 serial)
{
    if (m_lastUsageSerial < serial) {
        m_lastUsageSerial = serial;
    }
}

quint32 Window::lastUsageSerial() const
{
    return m_lastUsageSerial;
}

uint32_t Window::interactiveMoveResizeCount() const
{
    return m_interactiveMoveResize.counter;
}

void Window::setLockScreenOverlay(bool allowed)
{
    if (m_lockScreenOverlay == allowed) {
        return;
    }
    m_lockScreenOverlay = allowed;
    Q_EMIT lockScreenOverlayChanged();
}

bool Window::isLockScreenOverlay() const
{
    return m_lockScreenOverlay;
}

void Window::refOffscreenRendering()
{
    if (m_offscreenRenderCount == 0) {
        m_offscreenFramecallbackTimer.start(1'000'000 / output()->refreshRate());
    }
    m_offscreenRenderCount++;
}

void Window::unrefOffscreenRendering()
{
    Q_ASSERT(m_offscreenRenderCount);
    m_offscreenRenderCount--;
    if (m_offscreenRenderCount == 0) {
        m_offscreenFramecallbackTimer.stop();
    }
}

void Window::maybeSendFrameCallback()
{
    if (m_surface && !m_windowItem->isVisible()) {
        m_surface->frameRendered(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        // update refresh rate, it might have changed
        m_offscreenFramecallbackTimer.start(1'000'000 / output()->refreshRate());
    }
}

void Window::setForceBlur(bool b)
{
    b = rules()->checkForceBlur(b);
    if (b == forceBlur()) {
        return;
    }
    m_forceBlur = b;
    updateWindowRules(Rules::ForceBlur);
}

WindowOffscreenRenderRef::WindowOffscreenRenderRef(Window *window)
    : m_window(window)
{
    window->refOffscreenRendering();
}

WindowOffscreenRenderRef::~WindowOffscreenRenderRef()
{
    if (m_window) {
        m_window->unrefOffscreenRendering();
    }
}

} // namespace KWin

#include "moc_window.cpp"
