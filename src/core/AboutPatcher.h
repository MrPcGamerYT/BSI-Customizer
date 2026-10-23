#pragma once

#include <QByteArray>
#include <QString>

// Rewrites the BlueStacks "Settings → About" page in the player's embedded QML
// (PlrAboutSetting.qml) instead of blanking it. The player window logo,
// product name, version and page structure stay; the stock About text is
// replaced with the custom text, the stock support / website / QQ links are
// replaced with one user link, and the "Check for updates" button is hidden
// so the page can never start an update. Builds that hide the whole links and
// About sections (MSI: `visible: uiBackend.oem !== "msi5"`) get those sections
// switched visible, so the About page is never empty. Every edit is tagged,
// so turning the option off writes the stock document back byte-for-byte
// without needing the .bak.
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
