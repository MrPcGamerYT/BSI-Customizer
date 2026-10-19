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
    QString detail;         // why not supported, when applicable
};

QList<Tweak> tweaks();
State analyze(const QByteArray &image, const QString &id);
Result apply(const QString &exePath, const QString &id, bool enabled);

} // namespace LayoutPatcher
