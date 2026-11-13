#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// QML-side rearrangements of the BlueStacks player UI. Unlike the string
// removals these are structural edits to the embedded player QML (plr.qml,
// PlrSidebar.qml, PlrTopbar.qml) and they are reversible: applying the same
// tweak again with the option turned off restores the stock layout
// byte-for-byte, so no backup restore is required to undo one.
namespace LayoutPatcher {

struct Result { bool ok = false; QString msg; };

struct Tweak
{
    QString id;
    QString label;
    QString detail;
};

struct State
{
    bool supported = false; // the player's QML has the expected shape
    bool applied = false;   // the tweak is currently written into the EXE
    bool needsUpdate = false; // older title-bar patch can be upgraded in place
    QString detail;         // why not supported, when applicable
};

// One button of the row the title-bar layout produces, in the order it is
// shown. `canHide` is false for the title bar's own Back/Home/Recent, which
// the user asked to keep.
struct RowButton
{
    QString key;
    QString label;
    bool hidden = false;
    bool canHide = true;
};

struct RowState
{
    bool supported = false;
    bool applied = false;
    QString detail;
    QList<RowButton> buttons;
};

QList<Tweak> tweaks();
State analyze(const QByteArray &image, const QString &id);
Result apply(const QString &exePath, const QString &id, bool enabled);
// The row's current order and hidden buttons, read from the player itself.
RowState buttonRows(const QByteArray &image);
// Reorders and hides buttons of the row. An empty `order` restores the default
// (title bar navigation, Fullscreen, Sound, Files / APK, Settings, then the
// rest in the player's own order).
Result applyOrder(const QString &exePath, const QStringList &order, const QStringList &hidden);

} // namespace LayoutPatcher
