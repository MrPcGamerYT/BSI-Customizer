#pragma once

#include <QByteArray>
#include <QString>

// Replaces the BlueStacks "Settings → About" page content in the player's
// embedded QML (PlrAboutSetting.qml). When enabled it hides every stock block
// of the About content column (logo/version banner, update check, support
// e-mail, support site, website, QQ links and the copyright/Terms-of-use
// line) and inserts one custom text block with an optional single link.
// Every edit is tagged, so turning the option off writes the stock document
// back byte-for-byte without needing the .bak.
namespace AboutPatcher {

struct Config
{
    QString text;
    QString linkLabel;
    QString linkUrl;
};

struct State
{
    bool supported = false; // the player's About QML has the expected shape
    bool applied = false;   // our content is currently written into the EXE
    QString detail;         // why not supported / what is currently there
    Config config;          // values read back from the patched document
};

struct Result
{
    bool ok = false;
    QString msg;
};

State analyze(const QByteArray &image);
Result apply(const QString &exePath, bool enabled, const Config &config);

} // namespace AboutPatcher
