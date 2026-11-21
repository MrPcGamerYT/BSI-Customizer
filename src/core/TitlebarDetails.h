#pragma once

#include <QString>

// A source-only, component-scoped rewrite. Callers must supply the registered
// PlrTopbar.qml document, never an executable or a shared translation table.
namespace TitlebarDetails {

// Hide the version + instance/engine label together and vertically centre the
// remaining player name. Preserve text bindings, title text, theme and input.
// Returns an empty string on an unsupported/ambiguous shape; repeated calls
// return the same document. Undo uses the existing verified EXE backup.
QString hide(const QString &source, QString *error = nullptr);

} // namespace TitlebarDetails
