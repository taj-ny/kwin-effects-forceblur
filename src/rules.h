/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2004 Lubos Lunak <l.lunak@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QRectF>
#include <QVector>
#include <netwm_def.h>

#include "options.h"
#include "utils/common.h"

class QDebug;
class KConfig;
class KXMessages;

namespace KWin
{

class Window;
class Output;
class Rules;
class RuleSettings;
class VirtualDesktop;

#ifndef KCMRULES // only for kwin core

class WindowRules
{
public:
    explicit WindowRules(const QVector<Rules *> &rules);
    WindowRules();
    void update(Window *, int selection);
    void discardTemporary();
    bool contains(const Rules *rule) const;
    void remove(Rules *rule);
    PlacementPolicy checkPlacement(PlacementPolicy placement) const;
    QRectF checkGeometry(QRectF rect, bool init = false) const;
    QRectF checkGeometrySafe(QRectF rect, bool init = false) const;
    // use 'invalidPoint' with checkPosition, unlike QSize() and QRect(), QPoint() is a valid point
    QPointF checkPositionSafe(QPointF pos, bool init = false) const;
    QPointF checkPosition(QPointF pos, bool init = false) const;
    QSizeF checkSize(QSizeF s, bool init = false) const;
    QSizeF checkMinSize(QSizeF s) const;
    QSizeF checkMaxSize(QSizeF s) const;
    int checkOpacityActive(int s) const;
    int checkOpacityInactive(int s) const;
    bool checkIgnoreGeometry(bool ignore, bool init = false) const;
    QVector<VirtualDesktop *> checkDesktops(QVector<VirtualDesktop *> desktops, bool init = false) const;
    Output *checkOutput(Output *output, bool init = false) const;
    QStringList checkActivity(QStringList activity, bool init = false) const;
    NET::WindowType checkType(NET::WindowType type) const;
    MaximizeMode checkMaximize(MaximizeMode mode, bool init = false) const;
    bool checkMinimize(bool minimized, bool init = false) const;
    ShadeMode checkShade(ShadeMode shade, bool init = false) const;
    bool checkSkipTaskbar(bool skip, bool init = false) const;
    bool checkSkipPager(bool skip, bool init = false) const;
    bool checkSkipSwitcher(bool skip, bool init = false) const;
    bool checkKeepAbove(bool above, bool init = false) const;
    bool checkKeepBelow(bool below, bool init = false) const;
    bool checkFullScreen(bool fs, bool init = false) const;
    bool checkNoBorder(bool noborder, bool init = false) const;
    QString checkDecoColor(QString schemeFile) const;
    bool checkBlockCompositing(bool block) const;
    int checkFSP(int fsp) const;
    int checkFPP(int fpp) const;
    bool checkAcceptFocus(bool focus) const;
    bool checkCloseable(bool closeable) const;
    bool checkAutogrouping(bool autogroup) const;
    bool checkAutogroupInForeground(bool fg) const;
    QString checkAutogroupById(QString id) const;
    bool checkStrictGeometry(bool strict) const;
    QString checkShortcut(QString s, bool init = false) const;
    bool checkDisableGlobalShortcuts(bool disable) const;
    QString checkDesktopFile(QString desktopFile, bool init = false) const;
    bool checkForceBlur(bool blur) const;

private:
    MaximizeMode checkMaximizeVert(MaximizeMode mode, bool init) const;
    MaximizeMode checkMaximizeHoriz(MaximizeMode mode, bool init) const;
    QVector<Rules *> rules;
};

#endif

class Rules
{
public:
    Rules();
    explicit Rules(const RuleSettings *);
    Rules(const QString &, bool temporary);
    enum Type {
        Position = 1 << 0,
        Size = 1 << 1,
        Desktops = 1 << 2,
        MaximizeVert = 1 << 3,
        MaximizeHoriz = 1 << 4,
        Minimize = 1 << 5,
        Shade = 1 << 6,
        SkipTaskbar = 1 << 7,
        SkipPager = 1 << 8,
        SkipSwitcher = 1 << 9,
        Above = 1 << 10,
        Below = 1 << 11,
        Fullscreen = 1 << 12,
        NoBorder = 1 << 13,
        OpacityActive = 1 << 14,
        OpacityInactive = 1 << 15,
        Activity = 1 << 16,
        Screen = 1 << 17,
        DesktopFile = 1 << 18,
        ForceBlur = 1 << 19,
        All = 0xffffffff
    };
    Q_DECLARE_FLAGS(Types, Type)
    // All these values are saved to the cfg file, and are also used in kstart!
    enum {
        Unused = 0,
        DontAffect, // use the default value
        Force, // force the given value
        Apply, // apply only after initial mapping
        Remember, // like apply, and remember the value when the window is withdrawn
        ApplyNow, // apply immediatelly, then forget the setting
        ForceTemporarily // apply and force until the window is withdrawn
    };
    enum StringMatch {
        FirstStringMatch,
        UnimportantMatch = FirstStringMatch,
        ExactMatch,
        SubstringMatch,
        RegExpMatch,
        LastStringMatch = RegExpMatch
    };
    enum SetRule {
        UnusedSetRule = Unused,
        SetRuleDummy = 256 // so that it's at least short int
    };
    enum ForceRule {
        UnusedForceRule = Unused,
        ForceRuleDummy = 256 // so that it's at least short int
    };
    void write(RuleSettings *) const;
    bool isEmpty() const;
#ifndef KCMRULES
    bool discardUsed(bool withdrawn);
    bool match(const Window *c) const;
    bool update(Window *, int selection);
    bool isTemporary() const;
    bool discardTemporary(bool force); // removes if temporary and forced or too old
    bool applyPlacement(PlacementPolicy &placement) const;
    bool applyGeometry(QRectF &rect, bool init) const;
    // use 'invalidPoint' with applyPosition, unlike QSize() and QRect(), QPoint() is a valid point
    bool applyPosition(QPointF &pos, bool init) const;
    bool applySize(QSizeF &s, bool init) const;
    bool applyMinSize(QSizeF &s) const;
    bool applyMaxSize(QSizeF &s) const;
    bool applyOpacityActive(int &s) const;
    bool applyOpacityInactive(int &s) const;
    bool applyIgnoreGeometry(bool &ignore, bool init) const;
    bool applyDesktops(QVector<VirtualDesktop *> &desktops, bool init) const;
    bool applyScreen(int &desktop, bool init) const;
    bool applyActivity(QStringList &activity, bool init) const;
    bool applyType(NET::WindowType &type) const;
    bool applyMaximizeVert(MaximizeMode &mode, bool init) const;
    bool applyMaximizeHoriz(MaximizeMode &mode, bool init) const;
    bool applyMinimize(bool &minimized, bool init) const;
    bool applyShade(ShadeMode &shade, bool init) const;
    bool applySkipTaskbar(bool &skip, bool init) const;
    bool applySkipPager(bool &skip, bool init) const;
    bool applySkipSwitcher(bool &skip, bool init) const;
    bool applyKeepAbove(bool &above, bool init) const;
    bool applyKeepBelow(bool &below, bool init) const;
    bool applyFullScreen(bool &fs, bool init) const;
    bool applyNoBorder(bool &noborder, bool init) const;
    bool applyDecoColor(QString &schemeFile) const;
    bool applyBlockCompositing(bool &block) const;
    bool applyFSP(int &fsp) const;
    bool applyFPP(int &fpp) const;
    bool applyAcceptFocus(bool &focus) const;
    bool applyCloseable(bool &closeable) const;
    bool applyAutogrouping(bool &autogroup) const;
    bool applyAutogroupInForeground(bool &fg) const;
    bool applyAutogroupById(QString &id) const;
    bool applyStrictGeometry(bool &strict) const;
    bool applyShortcut(QString &shortcut, bool init) const;
    bool applyDisableGlobalShortcuts(bool &disable) const;
    bool applyDesktopFile(QString &desktopFile, bool init) const;
    bool applyForceBlur(bool &blur) const;

private:
#endif
    bool matchType(NET::WindowType match_type) const;
    bool matchWMClass(const QString &match_class, const QString &match_name) const;
    bool matchRole(const QString &match_role) const;
    bool matchTitle(const QString &match_title) const;
    bool matchClientMachine(const QString &match_machine, bool local) const;
#ifdef KCMRULES
private:
#endif
    void readFromSettings(const RuleSettings *settings);
    static ForceRule convertForceRule(int v);
    static QString getDecoColor(const QString &themeName);
#ifndef KCMRULES
    static bool checkSetRule(SetRule rule, bool init);
    static bool checkForceRule(ForceRule rule);
    static bool checkSetStop(SetRule rule);
    static bool checkForceStop(ForceRule rule);
#endif
    int temporary_state; // e.g. for kstart
    QString description;
    QString wmclass;
    StringMatch wmclassmatch;
    bool wmclasscomplete;
    QString windowrole;
    StringMatch windowrolematch;
    QString title;
    StringMatch titlematch;
    QString clientmachine;
    StringMatch clientmachinematch;
    NET::WindowTypes types; // types for matching
    PlacementPolicy placement;
    ForceRule placementrule;
    QPoint position;
    SetRule positionrule;
    QSize size;
    SetRule sizerule;
    QSize minsize;
    ForceRule minsizerule;
    QSize maxsize;
    ForceRule maxsizerule;
    int opacityactive;
    ForceRule opacityactiverule;
    int opacityinactive;
    ForceRule opacityinactiverule;
    bool ignoregeometry;
    SetRule ignoregeometryrule;
    QStringList desktops;
    SetRule desktopsrule;
    int screen;
    SetRule screenrule;
    QStringList activity;
    SetRule activityrule;
    NET::WindowType type; // type for setting
    ForceRule typerule;
    bool maximizevert;
    SetRule maximizevertrule;
    bool maximizehoriz;
    SetRule maximizehorizrule;
    bool minimize;
    SetRule minimizerule;
    bool shade;
    SetRule shaderule;
    bool skiptaskbar;
    SetRule skiptaskbarrule;
    bool skippager;
    SetRule skippagerrule;
    bool skipswitcher;
    SetRule skipswitcherrule;
    bool above;
    SetRule aboverule;
    bool below;
    SetRule belowrule;
    bool fullscreen;
    SetRule fullscreenrule;
    bool noborder;
    SetRule noborderrule;
    QString decocolor;
    ForceRule decocolorrule;
    bool blockcompositing;
    ForceRule blockcompositingrule;
    int fsplevel;
    int fpplevel;
    ForceRule fsplevelrule;
    ForceRule fpplevelrule;
    bool acceptfocus;
    ForceRule acceptfocusrule;
    bool closeable;
    ForceRule closeablerule;
    bool autogroup;
    ForceRule autogrouprule;
    bool autogroupfg;
    ForceRule autogroupfgrule;
    QString autogroupid;
    ForceRule autogroupidrule;
    bool strictgeometry;
    ForceRule strictgeometryrule;
    QString shortcut;
    SetRule shortcutrule;
    bool disableglobalshortcuts;
    ForceRule disableglobalshortcutsrule;
    QString desktopfile;
    SetRule desktopfilerule;
    bool forceblur;
    ForceRule forceblurrule;
    friend QDebug &operator<<(QDebug &stream, const Rules *);
};

#ifndef KCMRULES
class KWIN_EXPORT RuleBook : public QObject
{
    Q_OBJECT
public:
    explicit RuleBook();
    ~RuleBook() override;
    WindowRules find(const Window *, bool);
    void discardUsed(Window *c, bool withdraw);
    void setUpdatesDisabled(bool disable);
    bool areUpdatesDisabled() const;
    void load();
    void edit(Window *c, bool whole_app);
    void requestDiskStorage();

    void setConfig(const KSharedConfig::Ptr &config)
    {
        m_config = config;
    }

private Q_SLOTS:
    void temporaryRulesMessage(const QString &);
    void cleanupTemporaryRules();
    void save();

private:
    void deleteAll();
    void initializeX11();
    void cleanupX11();
    QTimer *m_updateTimer;
    bool m_updatesDisabled;
    QList<Rules *> m_rules;
    std::unique_ptr<KXMessages> m_temporaryRulesMessages;
    KSharedConfig::Ptr m_config;
};

inline bool RuleBook::areUpdatesDisabled() const
{
    return m_updatesDisabled;
}

inline bool Rules::checkSetRule(SetRule rule, bool init)
{
    if (rule > (SetRule)DontAffect) { // Unused or DontAffect
        if (rule == (SetRule)Force || rule == (SetRule)ApplyNow
            || rule == (SetRule)ForceTemporarily || init) {
            return true;
        }
    }
    return false;
}

inline bool Rules::checkForceRule(ForceRule rule)
{
    return rule == (ForceRule)Force || rule == (ForceRule)ForceTemporarily;
}

inline bool Rules::checkSetStop(SetRule rule)
{
    return rule != UnusedSetRule;
}

inline bool Rules::checkForceStop(ForceRule rule)
{
    return rule != UnusedForceRule;
}

inline WindowRules::WindowRules(const QVector<Rules *> &r)
    : rules(r)
{
}

inline WindowRules::WindowRules()
{
}

inline bool WindowRules::contains(const Rules *rule) const
{
    return rules.contains(const_cast<Rules *>(rule));
}

inline void WindowRules::remove(Rules *rule)
{
    rules.removeOne(rule);
}

#endif

QDebug &operator<<(QDebug &stream, const Rules *);

} // namespace

Q_DECLARE_OPERATORS_FOR_FLAGS(KWin::Rules::Types)
