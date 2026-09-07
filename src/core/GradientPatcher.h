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
    // True only for the player top bar / title bar chrome: the surfaces the
    // Hex (old) method's gradient editor is allowed to paint. Everything else
    // (sidebar, buttons, palette bindings) belongs to the QML (new) method or
    // to the solid hex color list.
    bool hexGradient = false;
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
    // Gradient properties the palette declares but no other resource ever reads.
    // Editing those cannot change a pixel, so they are left out of the list and
    // only counted here (see analyze()).
    int skippedUnreferenced = 0;
};
struct Result { bool ok = false; QString message; };
Analysis analyze(const QByteArray &image);
// In-memory planning/verification used by the file patcher and regressions.
bool apply(QByteArray &image, const QString &targetKey, const Spec &spec, QString *error);
Result patch(const QString &exePath, const QString &targetKey, const Spec &spec);
// Hex (old) gradient editor scope. The legacy method can paint a custom
// gradient onto the player's top bar / title bar only - exactly the targets
// whose Target::hexGradient flag is set (plr.qml's PlrTopbar/title-bar
// instances and the type-level top-bar/title-bar surfaces). The key
// allTopbarsKey() applies one spec to every hex target at once; any other
// key must resolve to a single flagged target - a sidebar/button/palette key
// is refused without touching the image.
QString allTopbarsKey();
bool applyTopbar(QByteArray &image, const QString &key, const Spec &spec, QString *error);
Result patchTopbar(const QString &exePath, const QString &key, const Spec &spec);
// Whether this player's UI can show the gradients the QML method edits: Qt 6 builds
// read their whole UI from the embedded QML resources, Qt 5 / MSI 5 builds paint
// from compiled units and hex constants. Decided from the PE import table, and it
// fails open - an unreadable table must never hide a working editor.
bool qmlMethodSupported(const QByteArray &image, int *qtMajor = nullptr, QString *why = nullptr);
}
