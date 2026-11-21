#pragma once
#include <QHash>
#include <QString>

// Literal-safe native-player integration planning. These functions only edit
// strings; they do not load a DLL, execute player code, or write any file.
// The module import is optional and runs outside DllMain, from the native root.
namespace ShaderSettingsSource {
struct Plan {
    bool ok = false;
    bool alreadyApplied = false;
    int tabIndex = -1;
    QString player;
    QString window;
    QString error;
};
Plan prepare(const QString &player, const QString &window,
             const QHash<QString, int> &registeredPageCounts);
bool hasPlayer(const QString &text);
bool hasWindow(const QString &text);
bool hasFragments(const QString &text);
// Only exact, intact owned fragments can be removed. Everything outside those
// fragments, including line endings and later compatible edits, is preserved.
bool removePlayer(const QString &text, QString *base, QString *error);
bool removeWindow(const QString &text, QString *base, QString *error);
bool addWindow(const QString &base, QString *output, QString *error);
QString pageUrl();
} // namespace ShaderSettingsSource
