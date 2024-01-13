/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "cursor.h"
#include "options.h"
#include "rules.h"
#include "utils/common.h"
#include "utils/xcbutils.h"

#include <functional>
#include <memory>

#include <NETWM>

#include <QElapsedTimer>
#include <QIcon>
#include <QKeySequence>
#include <QMatrix4x4>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QTimer>
#include <QUuid>

class QMouseEvent;
class QOpenGLFramebufferObject;

namespace KWaylandServer
{
class PlasmaWindowInterface;
class SurfaceInterface;
}

namespace KDecoration2
{
class Decoration;
}

namespace KWin
{
class Group;
class Output;
class ClientMachine;
class Deleted;
class EffectWindowImpl;
class Tile;
class Scene;
class Shadow;
class SurfaceItem;
class VirtualDesktop;
class WindowItem;

/**
 * Enum to describe the reason why a Window has to be released.
 */
enum class ReleaseReason {
    Release, ///< Normal Release after e.g. an Unmap notify event (window still valid)
    Destroyed, ///< Release after an Destroy notify event (window no longer valid)
    KWinShutsDown ///< Release on KWin Shutdown (window still valid)
};

namespace TabBox
{
class TabBoxClientImpl;
}

namespace Decoration
{
class DecoratedClientImpl;
class DecorationPalette;
}

class KWIN_EXPORT Window : public QObject
{
    Q_OBJECT

    /**
     * This property holds rectangle that the pixmap or buffer of this Window
     * occupies on the screen. This rectangle includes invisible portions of the
     * window, e.g. client-side drop shadows, etc.
     */
    Q_PROPERTY(QRectF bufferGeometry READ bufferGeometry)

    /**
     * This property holds the position of the Window's frame geometry.
     */
    Q_PROPERTY(QPointF pos READ pos)

    /**
     * This property holds the size of the Window's frame geometry.
     */
    Q_PROPERTY(QSizeF size READ size)

    /**
     * This property holds the x position of the Window's frame geometry.
     */
    Q_PROPERTY(qreal x READ x NOTIFY frameGeometryChanged)

    /**
     * This property holds the y position of the Window's frame geometry.
     */
    Q_PROPERTY(qreal y READ y NOTIFY frameGeometryChanged)

    /**
     * This property holds the width of the Window's frame geometry.
     */
    Q_PROPERTY(qreal width READ width NOTIFY frameGeometryChanged)

    /**
     * This property holds the height of the Window's frame geometry.
     */
    Q_PROPERTY(qreal height READ height NOTIFY frameGeometryChanged)

    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity NOTIFY opacityChanged)

    /**
     * The screen where the window center is on
     */
    Q_PROPERTY(int screen READ screen NOTIFY screenChanged)

    /**
     * The output (screen) where the window center is on
     */
    Q_PROPERTY(KWin::Output *output READ output NOTIFY screenChanged)

    Q_PROPERTY(QRectF rect READ rect)
    Q_PROPERTY(QString resourceName READ resourceName NOTIFY windowClassChanged)
    Q_PROPERTY(QString resourceClass READ resourceClass NOTIFY windowClassChanged)
    Q_PROPERTY(QString windowRole READ windowRole NOTIFY windowRoleChanged)

    /**
     * Returns whether the window is a desktop background window (the one with wallpaper).
     * See _NET_WM_WINDOW_TYPE_DESKTOP at https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    Q_PROPERTY(bool desktopWindow READ isDesktop CONSTANT)

    /**
     * Returns whether the window is a dock (i.e. a panel).
     * See _NET_WM_WINDOW_TYPE_DOCK at https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    Q_PROPERTY(bool dock READ isDock CONSTANT)

    /**
     * Returns whether the window is a standalone (detached) toolbar window.
     * See _NET_WM_WINDOW_TYPE_TOOLBAR at https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    Q_PROPERTY(bool toolbar READ isToolbar CONSTANT)

    /**
     * Returns whether the window is a torn-off menu.
     * See _NET_WM_WINDOW_TYPE_MENU at https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    Q_PROPERTY(bool menu READ isMenu CONSTANT)

    /**
     * Returns whether the window is a "normal" window, i.e. an application or any other window
     * for which none of the specialized window types fit.
     * See _NET_WM_WINDOW_TYPE_NORMAL at https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    Q_PROPERTY(bool normalWindow READ isNormalWindow CONSTANT)

    /**
     * Returns whether the window is a dialog window.
     * See _NET_WM_WINDOW_TYPE_DIALOG at https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    Q_PROPERTY(bool dialog READ isDialog CONSTANT)

    /**
     * Returns whether the window is a splashscreen. Note that many (especially older) applications
     * do not support marking their splash windows with this type.
     * See _NET_WM_WINDOW_TYPE_SPLASH at https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    Q_PROPERTY(bool splash READ isSplash CONSTANT)

    /**
     * Returns whether the window is a utility window, such as a tool window.
     * See _NET_WM_WINDOW_TYPE_UTILITY at https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    Q_PROPERTY(bool utility READ isUtility CONSTANT)

    /**
     * Returns whether the window is a dropdown menu (i.e. a popup directly or indirectly open
     * from the applications menubar).
     * See _NET_WM_WINDOW_TYPE_DROPDOWN_MENU at https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    Q_PROPERTY(bool dropdownMenu READ isDropdownMenu CONSTANT)

    /**
     * Returns whether the window is a popup menu (that is not a torn-off or dropdown menu).
     * See _NET_WM_WINDOW_TYPE_POPUP_MENU at https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    Q_PROPERTY(bool popupMenu READ isPopupMenu CONSTANT)

    /**
     * Returns whether the window is a tooltip.
     * See _NET_WM_WINDOW_TYPE_TOOLTIP at https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    Q_PROPERTY(bool tooltip READ isTooltip CONSTANT)

    /**
     * Returns whether the window is a window with a notification.
     * See _NET_WM_WINDOW_TYPE_NOTIFICATION at https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    Q_PROPERTY(bool notification READ isNotification CONSTANT)

    /**
     * Returns whether the window is a window with a critical notification.
     */
    Q_PROPERTY(bool criticalNotification READ isCriticalNotification CONSTANT)

    /**
     * Returns whether the window is an applet popup.
     */
    Q_PROPERTY(bool appletPopup READ isAppletPopup CONSTANT)

    /**
     * Returns whether the window is an On Screen Display.
     */
    Q_PROPERTY(bool onScreenDisplay READ isOnScreenDisplay CONSTANT)

    /**
     * Returns whether the window is a combobox popup.
     * See _NET_WM_WINDOW_TYPE_COMBO at https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    Q_PROPERTY(bool comboBox READ isComboBox CONSTANT)

    /**
     * Returns whether the window is a Drag&Drop icon.
     * See _NET_WM_WINDOW_TYPE_DND at https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    Q_PROPERTY(bool dndIcon READ isDNDIcon CONSTANT)

    /**
     * Returns the NETWM window type
     * See https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    Q_PROPERTY(int windowType READ windowType CONSTANT)

    /**
     * Whether this Window is managed by KWin (it has control over its placement and other
     * aspects, as opposed to override-redirect windows that are entirely handled by the application).
     */
    Q_PROPERTY(bool managed READ isClient CONSTANT)

    /**
     * Whether this Window represents an already deleted window and only kept for the compositor for animations.
     */
    Q_PROPERTY(bool deleted READ isDeleted CONSTANT)

    /**
     * Whether the window has an own shape
     */
    Q_PROPERTY(bool shaped READ shape NOTIFY shapedChanged)

    /**
     * Whether the window does not want to be animated on window close.
     * There are legit reasons for this like a screenshot application which does not want it's
     * window being captured.
     */
    Q_PROPERTY(bool skipsCloseAnimation READ skipsCloseAnimation WRITE setSkipCloseAnimation NOTIFY skipCloseAnimationChanged)

    /**
     * Whether the window is a popup.
     */
    Q_PROPERTY(bool popupWindow READ isPopupWindow)

    /**
     * Whether this Window represents the outline.
     *
     * @note It's always @c false if compositing is turned off.
     */
    Q_PROPERTY(bool outline READ isOutline)

    /**
     * This property holds a UUID to uniquely identify this Window.
     */
    Q_PROPERTY(QUuid internalId READ internalId CONSTANT)

    /**
     * The pid of the process owning this window.
     *
     * @since 5.20
     */
    Q_PROPERTY(int pid READ pid CONSTANT)

    /**
     * The position of this window within Workspace's window stack.
     */
    Q_PROPERTY(int stackingOrder READ stackingOrder NOTIFY stackingOrderChanged)

    /**
     * Whether this Window is fullScreen. A Window might either be fullScreen due to the _NET_WM property
     * or through a legacy support hack. The fullScreen state can only be changed if the Window does not
     * use the legacy hack. To be sure whether the state changed, connect to the notify signal.
     */
    Q_PROPERTY(bool fullScreen READ isFullScreen WRITE setFullScreen NOTIFY fullScreenChanged)

    /**
     * Whether the Window can be set to fullScreen. The property is evaluated each time it is invoked.
     * Because of that there is no notify signal.
     */
    Q_PROPERTY(bool fullScreenable READ isFullScreenable)

    /**
     * Whether this Window is active or not. Use Workspace::activateWindow() to activate a Window.
     * @see Workspace::activateWindow
     */
    Q_PROPERTY(bool active READ isActive NOTIFY activeChanged)

    /**
     * The desktop this Window is on. If the Window is on all desktops the property has value -1.
     * This is a legacy property, use x11DesktopIds instead
     *
     * @deprecated Use the desktops property instead.
     */
    Q_PROPERTY(int desktop READ desktop WRITE setDesktop NOTIFY desktopChanged)

    /**
     * The virtual desktops this client is on. If it's on all desktops, the list is empty.
     */
    Q_PROPERTY(QVector<KWin::VirtualDesktop *> desktops READ desktops WRITE setDesktops NOTIFY desktopChanged)

    /**
     * Whether the Window is on all desktops. That is desktop is -1.
     */
    Q_PROPERTY(bool onAllDesktops READ isOnAllDesktops WRITE setOnAllDesktops NOTIFY desktopChanged)

    /**
     * The activities this client is on. If it's on all activities the property is empty.
     */
    Q_PROPERTY(QStringList activities READ activities WRITE setOnActivities NOTIFY activitiesChanged)

    /**
     * The x11 ids for all desktops this client is in. On X11 this list will always have a length of 1
     *
     * @deprecated prefer using apis that use VirtualDesktop objects
     */
    Q_PROPERTY(QVector<uint> x11DesktopIds READ x11DesktopIds NOTIFY x11DesktopIdsChanged)

    /**
     * Indicates that the window should not be included on a taskbar.
     */
    Q_PROPERTY(bool skipTaskbar READ skipTaskbar WRITE setSkipTaskbar NOTIFY skipTaskbarChanged)

    /**
     * Indicates that the window should not be included on a Pager.
     */
    Q_PROPERTY(bool skipPager READ skipPager WRITE setSkipPager NOTIFY skipPagerChanged)

    /**
     * Whether the Window should be excluded from window switching effects.
     */
    Q_PROPERTY(bool skipSwitcher READ skipSwitcher WRITE setSkipSwitcher NOTIFY skipSwitcherChanged)

    /**
     * Whether the window can be closed by the user.
     */
    Q_PROPERTY(bool closeable READ isCloseable NOTIFY closeableChanged)

    Q_PROPERTY(QIcon icon READ icon NOTIFY iconChanged)

    /**
     * Whether the Window is set to be kept above other windows.
     */
    Q_PROPERTY(bool keepAbove READ keepAbove WRITE setKeepAbove NOTIFY keepAboveChanged)

    /**
     * Whether the Window is set to be kept below other windows.
     */
    Q_PROPERTY(bool keepBelow READ keepBelow WRITE setKeepBelow NOTIFY keepBelowChanged)

    /**
     * Whether the Window can be shaded. The property is evaluated each time it is invoked.
     * Because of that there is no notify signal.
     */
    Q_PROPERTY(bool shadeable READ isShadeable NOTIFY shadeableChanged)

    /**
     * Whether the Window is shaded.
     */
    Q_PROPERTY(bool shade READ isShade WRITE setShade NOTIFY shadeChanged)

    /**
     * Whether the Window can be minimized. The property is evaluated each time it is invoked.
     * Because of that there is no notify signal.
     */
    Q_PROPERTY(bool minimizable READ isMinimizable)

    /**
     * Whether the Window is minimized.
     */
    Q_PROPERTY(bool minimized READ isMinimized WRITE setMinimized NOTIFY minimizedChanged)

    /**
     * The optional geometry representing the minimized Window in e.g a taskbar.
     * See _NET_WM_ICON_GEOMETRY at https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     * The value is evaluated each time the getter is called.
     * Because of that no changed signal is provided.
     */
    Q_PROPERTY(QRectF iconGeometry READ iconGeometry)

    /**
     * Returns whether the window is any of special windows types (desktop, dock, splash, ...),
     * i.e. window types that usually don't have a window frame and the user does not use window
     * management (moving, raising,...) on them.
     * The value is evaluated each time the getter is called.
     * Because of that no changed signal is provided.
     */
    Q_PROPERTY(bool specialWindow READ isSpecialWindow)

    /**
     * Whether window state _NET_WM_STATE_DEMANDS_ATTENTION is set. This state indicates that some
     * action in or with the window happened. For example, it may be set by the Window Manager if
     * the window requested activation but the Window Manager refused it, or the application may set
     * it if it finished some work. This state may be set by both the Window and the Window Manager.
     * It should be unset by the Window Manager when it decides the window got the required attention
     * (usually, that it got activated).
     */
    Q_PROPERTY(bool demandsAttention READ isDemandingAttention WRITE demandAttention NOTIFY demandsAttentionChanged)

    /**
     * The Caption of the Window. Read from WM_NAME property together with a suffix for hostname and shortcut.
     * To read only the caption as provided by WM_NAME, use the getter with an additional @c false value.
     */
    Q_PROPERTY(QString caption READ caption NOTIFY captionChanged)

    /**
     * Minimum size as specified in WM_NORMAL_HINTS
     */
    Q_PROPERTY(QSizeF minSize READ minSize)

    /**
     * Maximum size as specified in WM_NORMAL_HINTS
     */
    Q_PROPERTY(QSizeF maxSize READ maxSize)

    /**
     * Whether the Window can accept keyboard focus.
     * The value is evaluated each time the getter is called.
     * Because of that no changed signal is provided.
     */
    Q_PROPERTY(bool wantsInput READ wantsInput)

    /**
     * Whether the Window is a transient Window to another Window.
     * @see transientFor
     */
    Q_PROPERTY(bool transient READ isTransient NOTIFY transientChanged)

    /**
     * The Window to which this Window is a transient if any.
     */
    Q_PROPERTY(KWin::Window *transientFor READ transientFor NOTIFY transientChanged)

    /**
     * Whether the Window represents a modal window.
     */
    Q_PROPERTY(bool modal READ isModal NOTIFY modalChanged)

    /**
     * The geometry of this Window. Be aware that depending on resize mode the frameGeometryChanged
     * signal might be emitted at each resize step or only at the end of the resize operation.
     *
     * @deprecated Use frameGeometry
     */
    Q_PROPERTY(QRectF geometry READ frameGeometry WRITE moveResize NOTIFY frameGeometryChanged)

    /**
     * The geometry of this Window. Be aware that depending on resize mode the frameGeometryChanged
     * signal might be emitted at each resize step or only at the end of the resize operation.
     */
    Q_PROPERTY(QRectF frameGeometry READ frameGeometry WRITE moveResize NOTIFY frameGeometryChanged)

    /**
     * Whether the Window is currently being moved by the user.
     * Notify signal is emitted when the Window starts or ends move/resize mode.
     */
    Q_PROPERTY(bool move READ isInteractiveMove NOTIFY moveResizedChanged)

    /**
     * Whether the Window is currently being resized by the user.
     * Notify signal is emitted when the Window starts or ends move/resize mode.
     */
    Q_PROPERTY(bool resize READ isInteractiveResize NOTIFY moveResizedChanged)

    /**
     * Whether the decoration is currently using an alpha channel.
     */
    Q_PROPERTY(bool decorationHasAlpha READ decorationHasAlpha)

    /**
     * Whether the window has a decoration or not.
     * This property is not allowed to be set by applications themselves.
     * The decision whether a window has a border or not belongs to the window manager.
     * If this property gets abused by application developers, it will be removed again.
     */
    Q_PROPERTY(bool noBorder READ noBorder WRITE setNoBorder)

    /**
     * Whether the Window provides context help. Mostly needed by decorations to decide whether to
     * show the help button or not.
     */
    Q_PROPERTY(bool providesContextHelp READ providesContextHelp CONSTANT)

    /**
     * Whether the Window can be maximized both horizontally and vertically.
     * The property is evaluated each time it is invoked.
     * Because of that there is no notify signal.
     */
    Q_PROPERTY(bool maximizable READ isMaximizable)

    /**
     * Whether the Window is moveable. Even if it is not moveable, it might be possible to move
     * it to another screen. The property is evaluated each time it is invoked.
     * Because of that there is no notify signal.
     * @see moveableAcrossScreens
     */
    Q_PROPERTY(bool moveable READ isMovable)

    /**
     * Whether the Window can be moved to another screen. The property is evaluated each time it is invoked.
     * Because of that there is no notify signal.
     * @see moveable
     */
    Q_PROPERTY(bool moveableAcrossScreens READ isMovableAcrossScreens)

    /**
     * Whether the Window can be resized. The property is evaluated each time it is invoked.
     * Because of that there is no notify signal.
     */
    Q_PROPERTY(bool resizeable READ isResizable)

    /**
     * The desktop file name of the application this Window belongs to.
     *
     * This is either the base name without full path and without file extension of the
     * desktop file for the window's application (e.g. "org.kde.foo").
     *
     * The application's desktop file name can also be the full path to the desktop file
     * (e.g. "/opt/kde/share/org.kde.foo.desktop") in case it's not in a standard location.
     */
    Q_PROPERTY(QString desktopFileName READ desktopFileName NOTIFY desktopFileNameChanged)

    /**
     * Whether an application menu is available for this Window
     */
    Q_PROPERTY(bool hasApplicationMenu READ hasApplicationMenu NOTIFY hasApplicationMenuChanged)

    /**
     * Whether the application menu for this Window is currently opened
     */
    Q_PROPERTY(bool applicationMenuActive READ applicationMenuActive NOTIFY applicationMenuActiveChanged)

    /**
     * Whether this window is unresponsive.
     *
     * When an application failed to react on a ping request in time, it is
     * considered unresponsive. This usually indicates that the application froze or crashed.
     */
    Q_PROPERTY(bool unresponsive READ unresponsive NOTIFY unresponsiveChanged)

    /**
     * The color scheme set on this window
     * Absolute file path, or name of palette in the user's config directory following KColorSchemes format.
     * An empty string indicates the default palette from kdeglobals is used.
     * @note this indicates the colour scheme requested, which might differ from the theme applied if the colorScheme cannot be found
     */
    Q_PROPERTY(QString colorScheme READ colorScheme NOTIFY colorSchemeChanged)

    Q_PROPERTY(KWin::Layer layer READ layer)

    /**
     * Whether this window is hidden. It's usually the case with auto-hide panels.
     */
    Q_PROPERTY(bool hidden READ isHiddenInternal NOTIFY hiddenChanged)

    /**
     * The Tile this window is associated to, if any
     */
    Q_PROPERTY(KWin::Tile *tile READ tile WRITE setTile NOTIFY tileChanged)

    Q_PROPERTY(bool forceBlur READ forceBlur WRITE setForceBlur)

public:
    ~Window() override;

    virtual xcb_window_t frameId() const;
    xcb_window_t window() const;
    /**
     * Returns the geometry of the pixmap or buffer attached to this Window.
     *
     * For X11 windows, this method returns server-side geometry of the Window.
     *
     * For Wayland windows, this method returns rectangle that the main surface
     * occupies on the screen, in global screen coordinates.
     */
    QRectF bufferGeometry() const;
    /**
     * Returns the geometry of the Window, excluding invisible portions, e.g.
     * server-side and client-side drop shadows, etc.
     */
    QRectF frameGeometry() const;
    /**
     * Returns the geometry of the client window, in global screen coordinates.
     */
    QRectF clientGeometry() const;
    /**
     * Returns the extents of the server-side decoration.
     *
     * Note that the returned margins object will have all margins set to 0 if
     * the window doesn't have a server-side decoration.
     *
     * Default implementation returns a margins object with all margins set to 0.
     */
    virtual QMargins frameMargins() const;
    /**
     * The geometry of the Window which accepts input events. This might be larger
     * than the actual geometry, e.g. to support resizing outside the window.
     *
     * Default implementation returns same as geometry.
     */
    virtual QRectF inputGeometry() const;
    QSizeF size() const;
    QPointF pos() const;
    QRectF rect() const;
    qreal x() const;
    qreal y() const;
    qreal width() const;
    qreal height() const;
    bool isOnOutput(Output *output) const;
    bool isOnActiveOutput() const;
    int screen() const;
    Output *output() const;
    void setOutput(Output *output);
    virtual QPointF clientPos() const
    {
        return QPointF(borderLeft(), borderTop());
    }; // inside of geometry()
    QSizeF clientSize() const;
    /**
     * Returns a rectangle that the window occupies on the screen, including drop-shadows.
     */
    QRectF visibleGeometry() const;
    virtual bool isClient() const;
    virtual bool isDeleted() const;
    virtual bool isUnmanaged() const;

    /**
     * Maps the specified @a point from the global screen coordinates to the frame coordinates.
     */
    QPointF mapToFrame(const QPointF &point) const;
    /**
     * Maps the specified @a point from the global screen coordinates to the surface-local
     * coordinates of the main surface. For X11 windows, this function maps the specified point
     * from the global screen coordinates to the buffer-local coordinates.
     */
    QPointF mapToLocal(const QPointF &point) const;
    QPointF mapFromLocal(const QPointF &point) const;

    // prefer isXXX() instead
    // 0 for supported types means default for managed/unmanaged types
    virtual NET::WindowType windowType(bool direct = false, int supported_types = 0) const = 0;
    bool hasNETSupport() const;
    bool isDesktop() const;
    bool isDock() const;
    bool isToolbar() const;
    bool isMenu() const;
    bool isNormalWindow() const; // normal as in 'NET::Normal or NET::Unknown non-transient'
    bool isDialog() const;
    bool isSplash() const;
    bool isUtility() const;
    bool isDropdownMenu() const;
    bool isPopupMenu() const; // a context popup, not dropdown, not torn-off
    bool isTooltip() const;
    bool isNotification() const;
    bool isCriticalNotification() const;
    bool isAppletPopup() const;
    bool isOnScreenDisplay() const;
    bool isComboBox() const;
    bool isDNDIcon() const;

    virtual bool isLockScreen() const;
    virtual bool isInputMethod() const;
    virtual bool isOutline() const;
    virtual bool isInternal() const;

    /**
     * Returns the virtual desktop within the workspace() the client window
     * is located in, 0 if it isn't located on any special desktop (not mapped yet),
     * or NET::OnAllDesktops. Do not use desktop() directly, use
     * isOnDesktop() instead.
     */
    virtual int desktop() const;
    virtual QVector<VirtualDesktop *> desktops() const;
    virtual QStringList activities() const;
    bool isOnDesktop(VirtualDesktop *desktop) const;
    bool isOnDesktop(int d) const;
    bool isOnActivity(const QString &activity) const;
    bool isOnCurrentDesktop() const;
    bool isOnCurrentActivity() const;
    bool isOnAllDesktops() const;
    bool isOnAllActivities() const;
    bool isLockScreenOverlay() const;

    void setLockScreenOverlay(bool allowed);

    virtual QString windowRole() const;
    QByteArray sessionId() const;
    QString resourceName() const;
    QString resourceClass() const;
    QString wmCommand();
    QString wmClientMachine(bool use_localhost) const;
    const ClientMachine *clientMachine() const;
    virtual bool isLocalhost() const;
    xcb_window_t wmClientLeader() const;
    virtual pid_t pid() const;
    static bool resourceMatch(const Window *c1, const Window *c2);

    bool readyForPainting() const; // true if the window has been already painted its contents
    xcb_visualid_t visual() const;
    bool shape() const;
    QRegion inputShape() const;
    void setOpacity(qreal opacity);
    qreal opacity() const;
    int depth() const;
    bool hasAlpha() const;
    virtual bool setupCompositing();
    virtual void finishCompositing(ReleaseReason releaseReason = ReleaseReason::Release);
    // these call workspace->addRepaint(), but first transform the damage if needed
    void addWorkspaceRepaint(const QRectF &r);
    void addWorkspaceRepaint(int x, int y, int w, int h);
    void addWorkspaceRepaint(const QRegion &region);
    EffectWindowImpl *effectWindow();
    const EffectWindowImpl *effectWindow() const;
    SurfaceItem *surfaceItem() const;
    WindowItem *windowItem() const;
    /**
     * Window will be temporarily painted as if being at the top of the stack.
     * Only available if Compositor is active, if not active, this method is a no-op.
     */
    void elevate(bool elevate);

    /**
     * Returns the Shadow associated with this Window or @c null if it has no shadow.
     */
    Shadow *shadow() const;
    /**
     * Updates the Shadow associated with this Window from X11 Property.
     * Call this method when the Property changes or Compositing is started.
     */
    void updateShadow();
    /**
     * Whether the Window currently wants the shadow to be rendered. Default
     * implementation always returns @c true.
     */
    virtual bool wantsShadowToBeRendered() const;

    /**
     * This method returns the area that the Window window reports to be opaque.
     * It is supposed to only provide valuable information if hasAlpha is @c true .
     * @see hasAlpha
     */
    const QRegion &opaqueRegion() const;
    QVector<QRectF> shapeRegion() const;

    bool skipsCloseAnimation() const;
    void setSkipCloseAnimation(bool set);

    quint64 surfaceSerial() const;
    quint32 pendingSurfaceId() const;
    KWaylandServer::SurfaceInterface *surface() const;
    void setSurface(KWaylandServer::SurfaceInterface *surface);

    const std::shared_ptr<QOpenGLFramebufferObject> &internalFramebufferObject() const;
    QImage internalImageObject() const;

    /**
     * @returns Transformation to map from global to window coordinates.
     *
     * Default implementation returns a translation on negative pos().
     * @see pos
     */
    virtual QMatrix4x4 inputTransformation() const;

    /**
     * Returns @c true if the window can accept input at the specified position @a point.
     */
    virtual bool hitTest(const QPointF &point) const;

    /**
     * The window has a popup grab. This means that when it got mapped the
     * parent window had an implicit (pointer) grab.
     *
     * Normally this is only relevant for transient windows.
     *
     * Once the popup grab ends (e.g. pointer press outside of any Window of
     * the client), the method popupDone should be invoked.
     *
     * The default implementation returns @c false.
     * @see popupDone
     * @since 5.10
     */
    virtual bool hasPopupGrab() const
    {
        return false;
    }
    /**
     * This method should be invoked for windows with a popup grab when
     * the grab ends.
     *
     * The default implementation does nothing.
     * @see hasPopupGrab
     * @since 5.10
     */
    virtual void popupDone(){};

    /**
     * @brief Finds the Window matching the condition expressed in @p func in @p list.
     *
     * The method is templated to operate on either a list of windows or on a list of
     * a subclass type of Window.
     * @param list The list to search in
     * @param func The condition function (compare std::find_if)
     * @return T* The found Window or @c null if there is no matching Window
     */
    template<class T, class U>
    static T *findInList(const QList<T *> &list, std::function<bool(const U *)> func);

    /**
     * Whether the window is a popup.
     *
     * Popups can be used to implement popup menus, tooltips, combo boxes, etc.
     *
     * @since 5.15
     */
    virtual bool isPopupWindow() const;

    /**
     * A UUID to uniquely identify this Window independent of windowing system.
     */
    QUuid internalId() const
    {
        return m_internalId;
    }

    int stackingOrder() const;
    void setStackingOrder(int order); ///< @internal

    QWeakPointer<TabBox::TabBoxClientImpl> tabBoxClient() const
    {
        return m_tabBoxClient.toWeakRef();
    }
    bool isFirstInTabBox() const
    {
        return m_firstInTabBox;
    }
    bool skipSwitcher() const
    {
        return m_skipSwitcher;
    }
    void setSkipSwitcher(bool set);

    bool skipTaskbar() const
    {
        return m_skipTaskbar;
    }
    void setSkipTaskbar(bool set);
    void setOriginalSkipTaskbar(bool set);
    bool originalSkipTaskbar() const
    {
        return m_originalSkipTaskbar;
    }

    bool skipPager() const
    {
        return m_skipPager;
    }
    void setSkipPager(bool set);

    const QIcon &icon() const
    {
        return m_icon;
    }

    bool isZombie() const;
    bool isActive() const
    {
        return m_active;
    }
    /**
     * Sets the window's active state to \a act.
     *
     * This function does only change the visual appearance of the window,
     * it does not change the focus setting. Use
     * Workspace::activateClient() or Workspace::requestFocus() instead.
     *
     * If a window receives or looses the focus, it calls setActive() on
     * its own.
     */
    void setActive(bool);

    bool keepAbove() const
    {
        return m_keepAbove;
    }
    void setKeepAbove(bool);
    bool keepBelow() const
    {
        return m_keepBelow;
    }
    void setKeepBelow(bool);

    void demandAttention(bool set = true);
    bool isDemandingAttention() const
    {
        return m_demandsAttention;
    }

    bool forceBlur() const
    {
        return m_forceBlur;
    }
    void setForceBlur(bool);

    void cancelAutoRaise();

    bool wantsTabFocus() const;

    virtual void updateMouseGrab();
    /**
     * @returns The caption consisting of captionNormal and captionSuffix
     * @see captionNormal
     * @see captionSuffix
     */
    QString caption() const;
    /**
     * @returns The caption as set by the Window without any suffix.
     * @see caption
     * @see captionSuffix
     */
    virtual QString captionNormal() const = 0;
    /**
     * @returns The suffix added to the caption (e.g. shortcut, machine name, etc.)
     * @see caption
     * @see captionNormal
     */
    virtual QString captionSuffix() const = 0;
    virtual bool isPlaceable() const;
    virtual bool isCloseable() const = 0;
    virtual bool isShown() const = 0;
    virtual bool isHiddenInternal() const = 0;
    virtual void hideClient() = 0;
    virtual void showClient() = 0;
    virtual bool isFullScreenable() const;
    virtual bool isFullScreen() const;
    virtual bool isRequestedFullScreen() const;
    // TODO: remove boolean trap
    virtual Window *findModal(bool allow_itself = false) = 0;
    virtual bool isTransient() const;
    /**
     * @returns Whether there is a hint available to place the Window on it's parent, default @c false.
     * @see transientPlacementHint
     */
    virtual bool hasTransientPlacementHint() const;
    /**
     * Only valid id hasTransientPlacementHint is true
     * @returns The position the transient wishes to position itself
     */
    virtual QRectF transientPlacement(const QRectF &bounds) const;
    const Window *transientFor() const;
    Window *transientFor();
    /**
     * @returns @c true if transient is the transient_for window for this window,
     *  or recursively the transient_for window
     * @todo: remove boolean trap
     */
    virtual bool hasTransient(const Window *transient, bool indirect) const;
    const QList<Window *> &transients() const; // Is not indirect
    virtual void addTransient(Window *transient);
    virtual void removeTransient(Window *transient);
    virtual QList<Window *> mainWindows() const; // Call once before loop , is not indirect
    QList<Window *> allMainWindows() const; // Call once before loop , is indirect
    /**
     * Returns true for "special" windows and false for windows which are "normal"
     * (normal=window which has a border, can be moved by the user, can be closed, etc.)
     * true for Desktop, Dock, Splash, Override and TopMenu (and Toolbar??? - for now)
     * false for Normal, Dialog, Utility and Menu (and Toolbar??? - not yet) TODO
     */
    bool isSpecialWindow() const;
    void sendToOutput(Output *output);
    const QKeySequence &shortcut() const
    {
        return _shortcut;
    }
    void setShortcut(const QString &cut);
    bool performMouseCommand(Options::MouseCommand, const QPointF &globalPos);
    void setOnAllDesktops(bool set);
    void setDesktop(int);
    void enterDesktop(VirtualDesktop *desktop);
    void leaveDesktop(VirtualDesktop *desktop);

    /**
     * Set the window as being on the attached list of desktops
     * On X11 it will be set to the last entry
     */
    void setDesktops(QVector<VirtualDesktop *> desktops);

    QVector<uint> x11DesktopIds() const;
    QStringList desktopIds() const;

    void setMinimized(bool set);
    /**
     * Minimizes this window plus its transients
     */
    void minimize(bool avoid_animation = false);
    void unminimize(bool avoid_animation = false);
    bool isMinimized() const
    {
        return m_minimized;
    }
    virtual void setFullScreen(bool set, bool user = true);

    QRectF geometryRestore() const;
    virtual MaximizeMode maximizeMode() const;
    virtual MaximizeMode requestedMaximizeMode() const;
    virtual void maximize(MaximizeMode mode);
    /**
     * Sets the maximization according to @p vertically and @p horizontally.
     */
    Q_INVOKABLE void setMaximize(bool vertically, bool horizontally);
    virtual bool noBorder() const;
    virtual void setNoBorder(bool set);
    QPalette palette();
    const Decoration::DecorationPalette *decorationPalette();
    /**
     * Returns whether the window is resizable or has a fixed size.
     */
    virtual bool isResizable() const = 0;
    /**
     * Returns whether the window is moveable or has a fixed position.
     */
    virtual bool isMovable() const = 0;
    /**
     * Returns whether the window can be moved to another screen.
     */
    virtual bool isMovableAcrossScreens() const = 0;
    /**
     * Returns @c true if the window is shaded and shadeMode is @c ShadeNormal; otherwise returns @c false.
     */
    virtual bool isShade() const
    {
        return shadeMode() == ShadeNormal;
    }
    ShadeMode shadeMode() const; // Prefer isShade()
    void setShade(bool set);
    void setShade(ShadeMode mode);
    void toggleShade();
    void cancelShadeHoverTimer();
    /**
     * Whether the Window can be shaded. Default implementation returns @c false.
     */
    virtual bool isShadeable() const;
    virtual bool isMaximizable() const;
    virtual bool isMinimizable() const;
    virtual QRectF iconGeometry() const;
    virtual bool userCanSetFullScreen() const;
    virtual bool userCanSetNoBorder() const;
    virtual void checkNoBorder();

    /**
     * Refresh Window's cache of activites
     * Called when activity daemon status changes
     */
    virtual void checkActivities(){};

    void setOnActivity(const QString &activity, bool enable);
    void setOnActivities(const QStringList &newActivitiesList);
    void setOnAllActivities(bool all);
    virtual void updateActivities(bool includeTransients);
    void blockActivityUpdates(bool b = true);

    const WindowRules *rules() const
    {
        return &m_rules;
    }
    void removeRule(Rules *r);
    void setupWindowRules(bool ignore_temporary);
    void evaluateWindowRules();
    virtual void applyWindowRules();
    virtual bool takeFocus() = 0;
    virtual bool wantsInput() const = 0;
    /**
     * Whether a dock window wants input.
     *
     * By default KWin doesn't pass focus to a dock window unless a force activate
     * request is provided.
     *
     * This method allows to have dock windows take focus also through flags set on
     * the window.
     *
     * The default implementation returns @c false.
     */
    virtual bool dockWantsInput() const;
    void checkWorkspacePosition(QRectF oldGeometry = QRectF(), const VirtualDesktop *oldDesktop = nullptr);
    virtual xcb_timestamp_t userTime() const;
    virtual void updateWindowRules(Rules::Types selection);

    void growHorizontal();
    void shrinkHorizontal();
    void growVertical();
    void shrinkVertical();
    void updateInteractiveMoveResize(const QPointF &currentGlobalCursor);
    /**
     * Ends move resize when all pointer buttons are up again.
     */
    void endInteractiveMoveResize();
    void keyPressEvent(uint key_code);

    virtual void pointerEnterEvent(const QPointF &globalPos);
    virtual void pointerLeaveEvent();

    Qt::Edge titlebarPosition() const;
    bool titlebarPositionUnderMouse() const;

    // a helper for the workspace window packing. tests for screen validity and updates since in maximization case as with normal moving
    void packTo(qreal left, qreal top);

    /**
     * Sets the quick tile mode ("snap") of this window.
     * This will also handle preserving and restoring of window geometry as necessary.
     * @param mode The tile mode (left/right) to give this window.
     * @param keyboard Defines whether to take keyboard cursor into account.
     */
    void setQuickTileMode(QuickTileMode mode, bool keyboard = false);
    QuickTileMode quickTileMode() const
    {
        return QuickTileMode(m_quickTileMode);
    }
    virtual Layer layer() const;
    void updateLayer();

    Tile *tile() const;

    void move(const QPointF &point);
    void resize(const QSizeF &size);
    void moveResize(const QRectF &rect);

    virtual QRectF resizeWithChecks(const QRectF &geometry, const QSizeF &s) = 0;
    void keepInArea(QRectF area, bool partial = false);
    QRectF keepInArea(QRectF geometry, QRectF area, bool partial = false);
    virtual QSizeF minSize() const;
    virtual QSizeF maxSize() const;

    /**
     * How to resize the window in order to obey constraints (mainly aspect ratios).
     */
    enum SizeMode {
        SizeModeAny,
        SizeModeFixedW, ///< Try not to affect width
        SizeModeFixedH, ///< Try not to affect height
        SizeModeMax ///< Try not to make it larger in either direction
    };

    virtual QSizeF constrainClientSize(const QSizeF &size, SizeMode mode = SizeModeAny) const;
    QSizeF constrainFrameSize(const QSizeF &size, SizeMode mode = SizeModeAny) const;

    /**
     * Calculates the matching client position for the given frame position @p point.
     */
    virtual QPointF framePosToClientPos(const QPointF &point) const;
    /**
     * Calculates the matching frame position for the given client position @p point.
     */
    virtual QPointF clientPosToFramePos(const QPointF &point) const;
    /**
     * Calculates the matching client size for the given frame size @p size.
     *
     * Notice that size constraints won't be applied.
     *
     * Default implementation returns the frame size with frame margins being excluded.
     */
    virtual QSizeF frameSizeToClientSize(const QSizeF &size) const;
    /**
     * Calculates the matching frame size for the given client size @p size.
     *
     * Notice that size constraints won't be applied.
     *
     * Default implementation returns the client size with frame margins being included.
     */
    virtual QSizeF clientSizeToFrameSize(const QSizeF &size) const;
    /**
     * Calculates the matching client rect for the given frame rect @p rect.
     *
     * Notice that size constraints won't be applied.
     */
    QRectF frameRectToClientRect(const QRectF &rect) const;
    /**
     * Calculates the matching frame rect for the given client rect @p rect.
     *
     * Notice that size constraints won't be applied.
     */
    QRectF clientRectToFrameRect(const QRectF &rect) const;

    /**
     * Returns the last requested geometry. The returned value indicates the bounding
     * geometry, meaning that the client can commit smaller window geometry if the window
     * is resized.
     *
     * The main difference between the frame geometry and the move-resize geometry is
     * that the former specifies the current geometry while the latter specifies the next
     * geometry.
     */
    QRectF moveResizeGeometry() const;

    /**
     * Returns the output where the last move or resize operation has occurred. The
     * window is expected to land on this output after the move/resize operation completes.
     */
    Output *moveResizeOutput() const;
    void setMoveResizeOutput(Output *output);

    /**
     * Returns @c true if the Client is being interactively moved; otherwise @c false.
     */
    bool isInteractiveMove() const
    {
        return isInteractiveMoveResize() && interactiveMoveResizeGravity() == Gravity::None;
    }
    /**
     * Returns @c true if the Client is being interactively resized; otherwise @c false.
     */
    bool isInteractiveResize() const
    {
        return isInteractiveMoveResize() && interactiveMoveResizeGravity() != Gravity::None;
    }
    /**
     * Cursor shape for move/resize mode.
     */
    CursorShape cursor() const
    {
        return m_interactiveMoveResize.cursor;
    }

    virtual StrutRect strutRect(StrutArea area) const;
    StrutRects strutRects() const;
    virtual bool hasStrut() const;

    void setModal(bool modal);
    bool isModal() const;

    /**
     * Determines the mouse command for the given @p button in the current state.
     *
     * The @p handled argument specifies whether the button was handled or not.
     * This value should be used to determine whether the mouse button should be
     * passed to the Window or being filtered out.
     */
    Options::MouseCommand getMouseCommand(Qt::MouseButton button, bool *handled) const;
    Options::MouseCommand getWheelCommand(Qt::Orientation orientation, bool *handled) const;

    // decoration related
    KDecoration2::Decoration *decoration()
    {
        return m_decoration.decoration.get();
    }
    const KDecoration2::Decoration *decoration() const
    {
        return m_decoration.decoration.get();
    }
    bool isDecorated() const
    {
        return m_decoration.decoration != nullptr;
    }
    QPointer<Decoration::DecoratedClientImpl> decoratedClient() const;
    void setDecoratedClient(QPointer<Decoration::DecoratedClientImpl> client);
    bool decorationHasAlpha() const;
    void triggerDecorationRepaint();
    virtual void layoutDecorationRects(QRectF &left, QRectF &top, QRectF &right, QRectF &bottom) const;
    void processDecorationMove(const QPointF &localPos, const QPointF &globalPos);
    bool processDecorationButtonPress(QMouseEvent *event, bool ignoreMenu = false);
    void processDecorationButtonRelease(QMouseEvent *event);

    virtual void invalidateDecoration();

    /**
     * Returns whether the window provides context help or not. If it does,
     * you should show a help menu item or a help button like '?' and call
     * contextHelp() if this is invoked.
     *
     * Default implementation returns @c false.
     * @see showContextHelp;
     */
    virtual bool providesContextHelp() const;

    /**
     * Invokes context help on the window. Only works if the window
     * actually provides context help.
     *
     * Default implementation does nothing.
     *
     * @see providesContextHelp()
     */
    virtual void showContextHelp();

    /**
     * @returns the geometry of the virtual keyboard
     * This geometry is in global coordinates
     */
    QRectF virtualKeyboardGeometry() const;

    /**
     * Sets the geometry of the virtual keyboard, The window may resize itself in order to make space for the keybaord
     * This geometry is in global coordinates
     */
    virtual void setVirtualKeyboardGeometry(const QRectF &geo);

    /**
     * Restores the Window after it had been hidden due to show on screen edge functionality.
     * The Window also gets raised (e.g. Panel mode windows can cover) and the Window
     * gets informed in a window specific way that it is shown and raised again.
     */
    virtual void showOnScreenEdge();

    QString desktopFileName() const
    {
        return m_desktopFileName;
    }

    /**
     * Tries to terminate the process of this Window.
     *
     * Implementing subclasses can perform a windowing system solution for terminating.
     */
    virtual void killWindow() = 0;
    virtual void destroyWindow() = 0;

    enum class SameApplicationCheck {
        RelaxedForActive = 1 << 0,
        AllowCrossProcesses = 1 << 1
    };
    Q_DECLARE_FLAGS(SameApplicationChecks, SameApplicationCheck)
    static bool belongToSameApplication(const Window *c1, const Window *c2, SameApplicationChecks checks = SameApplicationChecks());

    bool hasApplicationMenu() const;
    bool applicationMenuActive() const
    {
        return m_applicationMenuActive;
    }
    void setApplicationMenuActive(bool applicationMenuActive);

    QString applicationMenuServiceName() const
    {
        return m_applicationMenuServiceName;
    }
    QString applicationMenuObjectPath() const
    {
        return m_applicationMenuObjectPath;
    }

    virtual QString preferredColorScheme() const;
    QString colorScheme() const;
    void setColorScheme(const QString &colorScheme);

    /**
     * Request showing the application menu bar
     * @param actionId The DBus menu ID of the action that should be highlighted, 0 for the root menu
     */
    void showApplicationMenu(int actionId);

    bool unresponsive() const;

    /**
     * Default implementation returns @c null.
     * Mostly intended for X11 clients, from EWMH:
     * @verbatim
     * If the WM_TRANSIENT_FOR property is set to None or Root window, the window should be
     * treated as a transient for all other windows in the same group. It has been noted that this
     * is a slight ICCCM violation, but as this behavior is pretty standard for many toolkits and
     * window managers, and is extremely unlikely to break anything, it seems reasonable to document
     * it as standard.
     * @endverbatim
     */
    virtual bool groupTransient() const;
    /**
     * Default implementation returns @c null.
     *
     * Mostly for X11 clients, holds the client group
     */
    virtual const Group *group() const;
    /**
     * Default implementation returns @c null.
     *
     * Mostly for X11 clients, holds the client group
     */
    virtual Group *group();

    /**
     * Returns whether window rules can be applied to this client.
     *
     * Default implementation returns @c false.
     */
    virtual bool supportsWindowRules() const;

    /**
     * Return window management interface
     */
    KWaylandServer::PlasmaWindowInterface *windowManagementInterface() const
    {
        return m_windowManagementInterface;
    }

    QRectF fullscreenGeometryRestore() const;

    /**
     * Helper function to compute the icon out of an application id defined by @p fileName
     *
     * @returns an icon name that can be used with QIcon::fromTheme()
     */
    static QString iconFromDesktopFile(const QString &fileName);

    static QString findDesktopFile(const QString &fileName);

    /**
     * Sets the last user usage serial of the surface as @p serial
     */
    void setLastUsageSerial(quint32 serial);
    quint32 lastUsageSerial() const;

    uint32_t interactiveMoveResizeCount() const;

    void setTile(Tile *tile);

    void refOffscreenRendering();
    void unrefOffscreenRendering();

public Q_SLOTS:
    virtual void closeWindow() = 0;

protected Q_SLOTS:
    void setReadyForPainting();

Q_SIGNALS:
    void stackingOrderChanged();
    void shadeChanged();
    void opacityChanged(KWin::Window *window, qreal oldOpacity);
    void damaged(KWin::Window *window);
    void inputTransformationChanged();
    /**
     * This signal is emitted when the Window's frame geometry changes.
     * @deprecated since 5.19, use frameGeometryChanged instead
     */
    void geometryChanged();
    void geometryShapeChanged(KWin::Window *window, const QRectF &old);
    void windowClosed(KWin::Window *window, KWin::Deleted *deleted);
    void windowShown(KWin::Window *window);
    void windowHidden(KWin::Window *window);
    /**
     * Signal emitted when the window's shape state changed. That is if it did not have a shape
     * and received one or if the shape was withdrawn. Think of Chromium enabling/disabling KWin's
     * decoration.
     */
    void shapedChanged();
    /**
     * Emitted whenever the Window's screen changes. This can happen either in consequence to
     * a screen being removed/added or if the Window's geometry changes.
     * @since 4.11
     */
    void screenChanged();
    void skipCloseAnimationChanged();
    /**
     * Emitted whenever the window role of the window changes.
     * @since 5.0
     */
    void windowRoleChanged();
    /**
     * Emitted whenever the window class name or resource name of the window changes.
     * @since 5.0
     */
    void windowClassChanged();
    /**
     * @since 5.4
     */
    void hasAlphaChanged();

    /**
     * Emitted whenever the Surface for this Window changes.
     */
    void surfaceChanged();

    /**
     * Emitted whenever the window's shadow changes.
     * @since 5.15
     */
    void shadowChanged();

    /**
     * This signal is emitted when the Window's buffer geometry changes.
     */
    void bufferGeometryChanged(KWin::Window *window, const QRectF &oldGeometry);
    /**
     * This signal is emitted when the Window's frame geometry changes.
     */
    void frameGeometryChanged(KWin::Window *window, const QRectF &oldGeometry);
    /**
     * This signal is emitted when the Window's client geometry has changed.
     */
    void clientGeometryChanged(KWin::Window *window, const QRectF &oldGeometry);

    /**
     * This signal is emitted when the frame geometry is about to change. the new geometry is not known yet
     */
    void frameGeometryAboutToChange(KWin::Window *window);

    /**
     * This signal is emitted when the visible geometry has changed.
     */
    void visibleGeometryChanged();

    /**
     * This signal is emitted when associated tile has changed, including from and to none
     */
    void tileChanged(KWin::Tile *tile);

    void fullScreenChanged();
    void skipTaskbarChanged();
    void skipPagerChanged();
    void skipSwitcherChanged();
    void iconChanged();
    void activeChanged();
    void keepAboveChanged(bool);
    void keepBelowChanged(bool);
    /**
     * Emitted whenever the demands attention state changes.
     */
    void demandsAttentionChanged();
    void desktopPresenceChanged(KWin::Window *, int); // to be forwarded by Workspace
    void desktopChanged();
    void activitiesChanged(KWin::Window *window);
    void x11DesktopIdsChanged();
    void minimizedChanged();
    void clientMinimized(KWin::Window *window, bool animate);
    void clientUnminimized(KWin::Window *window, bool animate);
    void paletteChanged(const QPalette &p);
    void colorSchemeChanged();
    void captionChanged();
    void clientMaximizedStateAboutToChange(KWin::Window *, MaximizeMode);
    void clientMaximizedStateChanged(KWin::Window *, MaximizeMode);
    void clientMaximizedStateChanged(KWin::Window *c, bool h, bool v);
    void transientChanged();
    void modalChanged();
    void quickTileModeChanged();
    void moveResizedChanged();
    void moveResizeCursorChanged(CursorShape);
    void clientStartUserMovedResized(KWin::Window *);
    void clientStepUserMovedResized(KWin::Window *, const QRectF &);
    void clientFinishUserMovedResized(KWin::Window *);
    void closeableChanged(bool);
    void minimizeableChanged(bool);
    void shadeableChanged(bool);
    void maximizeableChanged(bool);
    void desktopFileNameChanged();
    void applicationMenuChanged();
    void hasApplicationMenuChanged(bool);
    void applicationMenuActiveChanged(bool);
    void unresponsiveChanged(bool);
    void decorationChanged();
    void hiddenChanged();
    void lockScreenOverlayChanged();

protected:
    void setWindowHandles(xcb_window_t client);
    void detectShape(xcb_window_t id);
    virtual void propertyNotifyEvent(xcb_property_notify_event_t *e);
    virtual void clientMessageEvent(xcb_client_message_event_t *e);
    Xcb::Property fetchWmClientLeader() const;
    void readWmClientLeader(Xcb::Property &p);
    void getWmClientLeader();
    void getWmClientMachine();

    /**
     * This function fetches the opaque region from this Window.
     * Will only be called on corresponding property changes and for initialization.
     */
    void getWmOpaqueRegion();
    void discardShapeRegion();

    virtual std::unique_ptr<WindowItem> createItem(Scene *scene) = 0;

    void getResourceClass();
    void setResourceClass(const QString &name, const QString &className = QString());
    Xcb::Property fetchSkipCloseAnimation() const;
    void readSkipCloseAnimation(Xcb::Property &prop);
    void getSkipCloseAnimation();
    void copyToDeleted(Window *c);
    void disownDataPassedToDeleted();
    void setDepth(int depth);

    Output *m_output = nullptr;
    QRectF m_frameGeometry;
    QRectF m_clientGeometry;
    QRectF m_bufferGeometry;
    xcb_visualid_t m_visual;
    int bit_depth;
    NETWinInfo *info;
    bool ready_for_painting;
    /**
     * An FBO object KWin internal windows might render to.
     */
    std::shared_ptr<QOpenGLFramebufferObject> m_internalFBO;
    QImage m_internalImage;

protected:
    Window();
    void setFirstInTabBox(bool enable)
    {
        m_firstInTabBox = enable;
    }
    void setIcon(const QIcon &icon);
    void startAutoRaise();
    void autoRaise();
    bool isMostRecentlyRaised() const;
    void markAsZombie();
    /**
     * Whether the window accepts focus.
     * The difference to wantsInput is that the implementation should not check rules and return
     * what the window effectively supports.
     */
    virtual bool acceptsFocus() const = 0;
    /**
     * Called from setActive once the active value got updated, but before the changed signal
     * is emitted.
     *
     * Default implementation does nothing.
     */
    virtual void doSetActive();
    /**
     * Called from setKeepAbove once the keepBelow value got updated, but before the changed signal
     * is emitted.
     *
     * Default implementation does nothing.
     */
    virtual void doSetKeepAbove();
    /**
     * Called from setKeepBelow once the keepBelow value got updated, but before the changed signal
     * is emitted.
     *
     * Default implementation does nothing.
     */
    virtual void doSetKeepBelow();
    /**
     * Called from setShade() once the shadeMode value got updated, but before the changed signal
     * is emitted.
     *
     * Default implementation does nothing.
     */
    virtual void doSetShade(ShadeMode previousShadeMode);
    /**
     * Called from setDeskop once the desktop value got updated, but before the changed signal
     * is emitted.
     *
     * Default implementation does nothing.
     */
    virtual void doSetDesktop();
    /**
     * Called from @ref setOnActivities just after the activity list member has been updated, but before
     * @ref updateActivities is called.
     *
     * @param activityList the new list of activities set on that window
     *
     * Default implementation does nothing
     */
    virtual void doSetOnActivities(const QStringList &activityList);
    /**
     * Called from @ref minimize and @ref unminimize once the minimized value got updated, but before the
     * changed signal is emitted.
     *
     * Default implementation does nothig.
     */
    virtual void doMinimize();
    virtual bool belongsToSameApplication(const Window *other, SameApplicationChecks checks) const = 0;

    virtual void doSetSkipTaskbar();
    virtual void doSetSkipPager();
    virtual void doSetSkipSwitcher();
    virtual void doSetDemandsAttention();
    virtual void doSetQuickTileMode();

    void setupWindowManagementInterface();
    void updateColorScheme();
    void ensurePalette();
    void setTransientFor(Window *transientFor);
    /**
     * Just removes the @p cl from the transients without any further checks.
     */
    void removeTransientFromList(Window *cl);

    virtual Layer belongsToLayer() const;
    virtual bool belongsToDesktop() const;
    void invalidateLayer();
    bool isActiveFullScreen() const;
    virtual Layer layerForDock() const;

    // electric border / quick tiling
    void setElectricBorderMode(QuickTileMode mode);
    QuickTileMode electricBorderMode() const
    {
        return m_electricMode;
    }
    void setElectricBorderMaximizing(bool maximizing);
    bool isElectricBorderMaximizing() const
    {
        return m_electricMaximizing;
    }
    void updateElectricGeometryRestore();
    QRectF quickTileGeometryRestore() const;
    QRectF quickTileGeometry(QuickTileMode mode, const QPointF &pos) const;
    void updateQuickTileMode(QuickTileMode newMode)
    {
        m_quickTileMode = newMode;
    }

    // geometry handling
    void checkOffscreenPosition(QRectF *geom, const QRectF &screenArea);
    int borderLeft() const;
    int borderRight() const;
    int borderTop() const;
    int borderBottom() const;
    void setGeometryRestore(const QRectF &rect);

    void blockGeometryUpdates(bool block);
    void blockGeometryUpdates();
    void unblockGeometryUpdates();
    bool areGeometryUpdatesBlocked() const;
    enum class MoveResizeMode : uint {
        None,
        Move = 0x1,
        Resize = 0x2,
        MoveResize = Move | Resize,
    };
    MoveResizeMode pendingMoveResizeMode() const;
    void setPendingMoveResizeMode(MoveResizeMode mode);
    virtual void moveResizeInternal(const QRectF &rect, MoveResizeMode mode) = 0;

    /**
     * @returns whether the Window is currently in move resize mode
     */
    bool isInteractiveMoveResize() const
    {
        return m_interactiveMoveResize.enabled;
    }
    /**
     * Sets whether the Window is in move resize mode to @p enabled.
     */
    void setInteractiveMoveResize(bool enabled)
    {
        m_interactiveMoveResize.enabled = enabled;
    }
    /**
     * @returns whether the move resize mode is unrestricted.
     */
    bool isUnrestrictedInteractiveMoveResize() const
    {
        return m_interactiveMoveResize.unrestricted;
    }
    /**
     * Sets whether move resize mode is unrestricted to @p set.
     */
    void setUnrestrictedInteractiveMoveResize(bool set)
    {
        m_interactiveMoveResize.unrestricted = set;
    }
    QPointF interactiveMoveOffset() const
    {
        return m_interactiveMoveResize.offset;
    }
    void setInteractiveMoveOffset(const QPointF &offset)
    {
        m_interactiveMoveResize.offset = offset;
    }
    QPointF invertedInteractiveMoveOffset() const
    {
        return m_interactiveMoveResize.invertedOffset;
    }
    void setInvertedInteractiveMoveOffset(const QPointF &offset)
    {
        m_interactiveMoveResize.invertedOffset = offset;
    }
    QRectF initialInteractiveMoveResizeGeometry() const
    {
        return m_interactiveMoveResize.initialGeometry;
    }
    void setMoveResizeGeometry(const QRectF &geo);
    Gravity interactiveMoveResizeGravity() const
    {
        return m_interactiveMoveResize.gravity;
    }
    void setInteractiveMoveResizeGravity(Gravity gravity)
    {
        m_interactiveMoveResize.gravity = gravity;
    }
    bool isInteractiveMoveResizePointerButtonDown() const
    {
        return m_interactiveMoveResize.buttonDown;
    }
    void setInteractiveMoveResizePointerButtonDown(bool down)
    {
        m_interactiveMoveResize.buttonDown = down;
    }
    Output *interactiveMoveResizeStartOutput() const
    {
        return m_interactiveMoveResize.startOutput;
    }
    void checkUnrestrictedInteractiveMoveResize();
    /**
     * Sets an appropriate cursor shape for the logical mouse position.
     */
    void updateCursor();
    void startDelayedInteractiveMoveResize();
    void stopDelayedInteractiveMoveResize();
    bool startInteractiveMoveResize();
    /**
     * Called from startMoveResize.
     *
     * Implementing classes should return @c false if starting move resize should
     * get aborted. In that case startMoveResize will also return @c false.
     *
     * Base implementation returns @c true.
     */
    virtual bool doStartInteractiveMoveResize();
    virtual void doFinishInteractiveMoveResize();
    void finishInteractiveMoveResize(bool cancel);
    /**
     * Leaves the move resize mode.
     *
     * Inheriting classes must invoke the base implementation which
     * ensures that the internal mode is properly ended.
     */
    virtual void leaveInteractiveMoveResize();
    /*
     * Checks if the mouse cursor is near the edge of the screen and if so
     * activates quick tiling or maximization
     */
    void checkQuickTilingMaximizationZones(int xroot, int yroot);
    void resetQuickTilingMaximizationZones();
    /**
     * Whether a sync request is still pending.
     * Default implementation returns @c false.
     */
    virtual bool isWaitingForInteractiveMoveResizeSync() const;
    /**
     * Called during handling a resize. Implementing subclasses can use this
     * method to perform windowing system specific syncing.
     *
     * Default implementation does nothing.
     */
    virtual void doInteractiveResizeSync(const QRectF &rect);
    void handleInteractiveMoveResize(int x, int y, int x_root, int y_root);
    void handleInteractiveMoveResize(const QPointF &local, const QPointF &global);
    void dontInteractiveMoveResize();

    virtual QSizeF resizeIncrements() const;

    /**
     * Returns the interactive move resize gravity depending on the Decoration's section
     * under mouse. If no decoration it returns Gravity::None.
     */
    Gravity mouseGravity() const;

    void setDecoration(std::shared_ptr<KDecoration2::Decoration> decoration);
    void startDecorationDoubleClickTimer();
    void invalidateDecorationDoubleClickTimer();
    void updateDecorationInputShape();

    void setDesktopFileName(const QString &name);
    QString iconFromDesktopFile() const;

    void updateApplicationMenuServiceName(const QString &serviceName);
    void updateApplicationMenuObjectPath(const QString &objectPath);

    void setUnresponsive(bool unresponsive);

    virtual void setShortcutInternal();
    QString shortcutCaptionSuffix() const;
    virtual void updateCaption() = 0;

    /**
     * Looks for another Window with same captionNormal and captionSuffix.
     * If no such Window exists @c nullptr is returned.
     */
    Window *findWindowWithSameCaption() const;

    void finishWindowRules();
    void discardTemporaryRules();

    bool tabTo(Window *other, bool behind, bool activate);

    void startShadeHoverTimer();
    void startShadeUnhoverTimer();

    // The geometry that the window should be restored when the virtual keyboard closes
    QRectF keyboardGeometryRestore() const;
    void setKeyboardGeometryRestore(const QRectF &geom);

    QRectF m_virtualKeyboardGeometry;

    void setFullscreenGeometryRestore(const QRectF &geom);

    void cleanTabBox();

    QStringList m_activityList;

private Q_SLOTS:
    void shadeHover();
    void shadeUnhover();

private:
    void maybeSendFrameCallback();

    // when adding new data members, check also copyToDeleted()
    QUuid m_internalId;
    Xcb::Window m_client;
    bool is_shape;
    std::unique_ptr<EffectWindowImpl> m_effectWindow;
    std::unique_ptr<WindowItem> m_windowItem;
    std::unique_ptr<Shadow> m_shadow;
    QString resource_name;
    QString resource_class;
    ClientMachine *m_clientMachine;
    xcb_window_t m_wmClientLeader;
    QRegion opaque_region;
    mutable QVector<QRectF> m_shapeRegion;
    mutable bool m_shapeRegionIsValid = false;
    bool m_skipCloseAnimation;
    quint32 m_pendingSurfaceId = 0;
    quint64 m_surfaceSerial = 0;
    QPointer<KWaylandServer::SurfaceInterface> m_surface;
    // when adding new data members, check also copyToDeleted()
    qreal m_opacity = 1.0;
    int m_stackingOrder = 0;

    void handlePaletteChange();
    QRectF moveToArea(const QRectF &geometry, const QRectF &oldArea, const QRectF &newArea);
    QRectF ensureSpecialStateGeometry(const QRectF &geometry);

    QSharedPointer<TabBox::TabBoxClientImpl> m_tabBoxClient;
    bool m_firstInTabBox = false;
    bool m_skipTaskbar = false;
    /**
     * Unaffected by KWin
     */
    bool m_originalSkipTaskbar = false;
    bool m_skipPager = false;
    bool m_skipSwitcher = false;
    QIcon m_icon;
    bool m_active = false;
    bool m_zombie = false;
    bool m_keepAbove = false;
    bool m_keepBelow = false;
    bool m_demandsAttention = false;
    bool m_minimized = false;
    QTimer *m_autoRaiseTimer = nullptr;
    QTimer *m_shadeHoverTimer = nullptr;
    ShadeMode m_shadeMode = ShadeNone;
    QVector<VirtualDesktop *> m_desktops;

    int m_activityUpdatesBlocked = 0;
    bool m_blockedActivityUpdatesRequireTransients = false;

    QString m_colorScheme;
    std::shared_ptr<Decoration::DecorationPalette> m_palette;
    static QHash<QString, std::weak_ptr<Decoration::DecorationPalette>> s_palettes;
    static std::shared_ptr<Decoration::DecorationPalette> s_defaultPalette;

    KWaylandServer::PlasmaWindowInterface *m_windowManagementInterface = nullptr;

    Window *m_transientFor = nullptr;
    QList<Window *> m_transients;
    bool m_modal = false;
    Layer m_layer = UnknownLayer;
    QPointer<Tile> m_tile;

    // electric border/quick tiling
    QuickTileMode m_electricMode = QuickTileFlag::None;
    QRectF m_electricGeometryRestore;
    bool m_electricMaximizing = false;
    // The quick tile mode of this window.
    int m_quickTileMode = int(QuickTileFlag::None);
    QTimer *m_electricMaximizingDelay = nullptr;

    // geometry
    int m_blockGeometryUpdates = 0; // > 0 = New geometry is remembered, but not actually set
    MoveResizeMode m_pendingMoveResizeMode = MoveResizeMode::None;
    friend class GeometryUpdatesBlocker;
    Output *m_moveResizeOutput;
    QRectF m_moveResizeGeometry;
    QRectF m_keyboardGeometryRestore;
    QRectF m_maximizeGeometryRestore;
    QRectF m_fullscreenGeometryRestore;

    struct
    {
        bool enabled = false;
        bool unrestricted = false;
        QPointF offset;
        QPointF invertedOffset;
        QRectF initialGeometry;
        QRectF initialGeometryRestore;
        Gravity gravity = Gravity::None;
        bool buttonDown = false;
        CursorShape cursor = Qt::ArrowCursor;
        Output *startOutput = nullptr;
        QTimer *delayedTimer = nullptr;
        uint32_t counter = 0;
        MaximizeMode initialMaximizeMode;
        QuickTileMode initialQuickTileMode;
    } m_interactiveMoveResize;

    struct
    {
        std::shared_ptr<KDecoration2::Decoration> decoration;
        QPointer<Decoration::DecoratedClientImpl> client;
        QElapsedTimer doubleClickTimer;
        QRegion inputRegion;
    } m_decoration;
    QString m_desktopFileName;

    bool m_applicationMenuActive = false;
    QString m_applicationMenuServiceName;
    QString m_applicationMenuObjectPath;

    bool m_unresponsive = false;

    QKeySequence _shortcut;

    WindowRules m_rules;
    quint32 m_lastUsageSerial = 0;
    bool m_lockScreenOverlay = false;
    uint32_t m_offscreenRenderCount = 0;
    QTimer m_offscreenFramecallbackTimer;

    bool m_forceBlur = false;
};

/**
 * Helper for Window::blockGeometryUpdates() being called in pairs (true/false)
 */
class GeometryUpdatesBlocker
{
public:
    explicit GeometryUpdatesBlocker(Window *c)
        : cl(c)
    {
        cl->blockGeometryUpdates(true);
    }
    ~GeometryUpdatesBlocker()
    {
        cl->blockGeometryUpdates(false);
    }

private:
    Window *cl;
};

inline xcb_window_t Window::window() const
{
    return m_client;
}

inline void Window::setWindowHandles(xcb_window_t w)
{
    Q_ASSERT(!m_client.isValid() && w != XCB_WINDOW_NONE);
    m_client.reset(w, false);
}

inline QRectF Window::bufferGeometry() const
{
    return m_bufferGeometry;
}

inline QRectF Window::clientGeometry() const
{
    return m_clientGeometry;
}

inline QSizeF Window::clientSize() const
{
    return m_clientGeometry.size();
}

inline QRectF Window::frameGeometry() const
{
    return m_frameGeometry;
}

inline QSizeF Window::size() const
{
    return m_frameGeometry.size();
}

inline QPointF Window::pos() const
{
    return m_frameGeometry.topLeft();
}

inline qreal Window::x() const
{
    return m_frameGeometry.x();
}

inline qreal Window::y() const
{
    return m_frameGeometry.y();
}

inline qreal Window::width() const
{
    return m_frameGeometry.width();
}

inline qreal Window::height() const
{
    return m_frameGeometry.height();
}

inline QRectF Window::rect() const
{
    return QRectF(0, 0, width(), height());
}

inline bool Window::readyForPainting() const
{
    return ready_for_painting;
}

inline xcb_visualid_t Window::visual() const
{
    return m_visual;
}

inline bool Window::isDesktop() const
{
    return windowType() == NET::Desktop;
}

inline bool Window::isDock() const
{
    return windowType() == NET::Dock;
}

inline bool Window::isMenu() const
{
    return windowType() == NET::Menu;
}

inline bool Window::isToolbar() const
{
    return windowType() == NET::Toolbar;
}

inline bool Window::isSplash() const
{
    return windowType() == NET::Splash;
}

inline bool Window::isUtility() const
{
    return windowType() == NET::Utility;
}

inline bool Window::isDialog() const
{
    return windowType() == NET::Dialog;
}

inline bool Window::isNormalWindow() const
{
    return windowType() == NET::Normal;
}

inline bool Window::isDropdownMenu() const
{
    return windowType() == NET::DropdownMenu;
}

inline bool Window::isPopupMenu() const
{
    return windowType() == NET::PopupMenu;
}

inline bool Window::isTooltip() const
{
    return windowType() == NET::Tooltip;
}

inline bool Window::isNotification() const
{
    return windowType() == NET::Notification;
}

inline bool Window::isCriticalNotification() const
{
    return windowType() == NET::CriticalNotification;
}

inline bool Window::isAppletPopup() const
{
    return windowType() == NET::AppletPopup;
}

inline bool Window::isOnScreenDisplay() const
{
    return windowType() == NET::OnScreenDisplay;
}

inline bool Window::isComboBox() const
{
    return windowType() == NET::ComboBox;
}

inline bool Window::isDNDIcon() const
{
    return windowType() == NET::DNDIcon;
}

inline bool Window::isLockScreen() const
{
    return false;
}

inline bool Window::isInputMethod() const
{
    return false;
}

inline bool Window::isOutline() const
{
    return false;
}

inline bool Window::isInternal() const
{
    return false;
}

inline bool Window::shape() const
{
    return is_shape;
}

inline int Window::depth() const
{
    return bit_depth;
}

inline bool Window::hasAlpha() const
{
    return depth() == 32;
}

inline const QRegion &Window::opaqueRegion() const
{
    return opaque_region;
}

inline EffectWindowImpl *Window::effectWindow()
{
    return m_effectWindow.get();
}

inline const EffectWindowImpl *Window::effectWindow() const
{
    return m_effectWindow.get();
}

inline WindowItem *Window::windowItem() const
{
    return m_windowItem.get();
}

inline bool Window::isOnAllDesktops() const
{
    return desktops().isEmpty();
}

inline bool Window::isOnAllActivities() const
{
    return activities().isEmpty();
}

inline bool Window::isOnActivity(const QString &activity) const
{
    return activities().isEmpty() || activities().contains(activity);
}

inline QString Window::resourceName() const
{
    return resource_name; // it is always lowercase
}

inline QString Window::resourceClass() const
{
    return resource_class; // it is always lowercase
}

inline const ClientMachine *Window::clientMachine() const
{
    return m_clientMachine;
}

inline quint64 Window::surfaceSerial() const
{
    return m_surfaceSerial;
}

inline quint32 Window::pendingSurfaceId() const
{
    return m_pendingSurfaceId;
}

inline const std::shared_ptr<QOpenGLFramebufferObject> &Window::internalFramebufferObject() const
{
    return m_internalFBO;
}

inline QImage Window::internalImageObject() const
{
    return m_internalImage;
}

template<class T, class U>
inline T *Window::findInList(const QList<T *> &list, std::function<bool(const U *)> func)
{
    static_assert(std::is_base_of<U, T>::value,
                  "U must be derived from T");
    const auto it = std::find_if(list.begin(), list.end(), func);
    if (it == list.end()) {
        return nullptr;
    }
    return *it;
}

inline bool Window::isPopupWindow() const
{
    switch (windowType()) {
    case NET::ComboBox:
    case NET::DropdownMenu:
    case NET::PopupMenu:
    case NET::Tooltip:
        return true;

    default:
        return false;
    }
}

inline const QList<Window *> &Window::transients() const
{
    return m_transients;
}

inline bool Window::areGeometryUpdatesBlocked() const
{
    return m_blockGeometryUpdates != 0;
}

inline void Window::blockGeometryUpdates()
{
    m_blockGeometryUpdates++;
}

inline void Window::unblockGeometryUpdates()
{
    m_blockGeometryUpdates--;
}

inline Window::MoveResizeMode Window::pendingMoveResizeMode() const
{
    return m_pendingMoveResizeMode;
}

inline void Window::setPendingMoveResizeMode(MoveResizeMode mode)
{
    m_pendingMoveResizeMode = MoveResizeMode(uint(m_pendingMoveResizeMode) | uint(mode));
}

KWIN_EXPORT QDebug operator<<(QDebug debug, const Window *window);

class KWIN_EXPORT WindowOffscreenRenderRef
{
public:
    WindowOffscreenRenderRef(Window *window);
    WindowOffscreenRenderRef() = default;
    ~WindowOffscreenRenderRef();

private:
    QPointer<Window> m_window;
};

} // namespace KWin

Q_DECLARE_METATYPE(KWin::Window *)
Q_DECLARE_METATYPE(QList<KWin::Window *>)
Q_DECLARE_OPERATORS_FOR_FLAGS(KWin::Window::SameApplicationChecks)
