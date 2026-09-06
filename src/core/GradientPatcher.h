#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// Gradients are applied to compatible visual bindings or properties typed
// Gradient. A palette property typed color is never replaced with an object.
namespace GradientPatcher {
struct Spec {
    bool enabled = true;
    QString first = QStringLiteral("#7C5CFF");
    QString second = QStringLiteral("#00C2FF");
    bool horizontal = true;
};
struct Target {
    QString key;
    QString label;
    QString resource;
    QString kind;
    Spec current;
    // False for documents whose QtQuick version predates Gradient::orientation:
    // the gradient is applied, but its direction is fixed top to bottom.
    bool supportsOrientation = true;
};
struct Analysis {
    bool ok = false;
    QString message;
    QList<Target> targets;
    // Diagnostics only: how many QML resources were candidates for gradient
    // editing and how many of those could be parsed. Candidates without a single
    // parsed document means the QML reader failed - not that the app has no
    // gradients - and is reported as such instead of an empty target list.
    int candidates = 0;
    int parsed = 0;
};
struct Result { bool ok = false; QString message; };
Analysis analyze(const QByteArray &image);
// In-memory planning/verification used by the file patcher and regressions.
bool apply(QByteArray &image, const QString &targetKey, const Spec &spec, QString *error);
Result patch(const QString &exePath, const QString &targetKey, const Spec &spec);
// Paint the real player chrome: plr.qml's PlrSidebar/PlrTopbar instances
// (the color BlueStacks actually shows) plus the type-level sidebar/topbar
// surfaces. Used by Hex (old) "Gradient theme"; QML Gradients lists the
// same chrome targets first so a single apply can hit what is on screen.
bool applyChrome(QByteArray &image, const Spec &spec, QString *error);
Result patchChrome(const QString &exePath, const Spec &spec);
}
