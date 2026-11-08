#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// Player Appearance Studio — safe numeric QML design tokens.
//
// This is deliberately not a raw QML/hex editor. It discovers a small catalog
// of literal integer tokens in the player's own UiThemeObject.qml / plr.qml,
// edits only those literals through the validated Qt resource writer, and is
// driven by CustomizationWorkspace's inspected -> staged -> dry-run -> apply
// transaction. Font/icon packs, gradients and compiled-bytecode editing are
// separate future work.
namespace PlayerAppearancePatcher {

struct Token
{
    QString key;       // <logical resource path>#<stable object path>
    QString resource;  // logical resource path, never a file offset
    QString path;      // stable object path inside that resource
    QString label;
    QString group;
    QString property;
    int value = 0;
    int minimum = 0;
    int maximum = 0;
    int step = 1;
};

struct Analysis
{
    bool ok = false;
    QString msg;
    QList<Token> tokens;
    QStringList resources;
    QStringList ambiguous;
    int compiledUnits = 0;
    int activeCompiledUnits = 0;
};

// Read-only inspection of an in-memory player image.
Analysis analyze(const QByteArray &image);
QVariantMap toMap(const Analysis &analysis);

// Validate a key -> integer edit map against the current image without writing.
// Returns {ok, msg, edits, resources}; edits is normalized to integer values.
QVariantMap validateEdits(const QByteArray &image, const QVariantMap &edits);

// In-memory rewrite used by Workspace dry-run/apply. On failure image is
// unchanged. The EXE is never grown; oversized records must fit verified free
// space. Matching compiled QML units are invalidated and the rewritten source
// is read back before the image is accepted.
bool apply(QByteArray &image, const QVariantMap &edits, QString *error);

} // namespace PlayerAppearancePatcher
