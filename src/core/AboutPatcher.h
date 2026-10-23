#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// Rewrites the BlueStacks "Settings → About" page in the player's embedded QML
// (PlrAboutSetting.qml) instead of blanking it. The player window logo,
// product name, version and page structure stay. What the page says is read
// back into an editable configuration - the About text and every stock link
// row (its name and its target) - and applying it rewrites exactly those
// values: each link can be renamed, re-pointed or hidden, and the sections the
// MSI build hides (visible: uiBackend.oem !== "msi5") can be switched back on.
// The "Check for updates" button is always hidden. Every edit is tagged, so
// turning the option off writes the stock document back byte-for-byte without
// needing the .bak.
namespace AboutPatcher {

// One stock link row of the About page ("Support email", "Support", "Website").
struct Link
{
    QString id;     // the row's id in the QML (iSupportEmailText, ...)
    QString name;   // the words before the link ("Support email")
    QString url;    // where the link opens
    bool hidden = false;
};

struct Config
{
    QString text;              // the About sentence
    QList<Link> links;         // one entry per stock link row
    bool showSections = true;  // force the MSI-hidden sections visible
};

struct State
{
    bool supported = false; // the player's About QML has the expected shape
    bool applied = false;   // our content is currently written into the EXE
    QString detail;         // why not supported / what is currently there
    bool sectionsHidden = false; // the build hides the links/About sections
    Config config;          // what the page says now (or what was applied)
};

struct Result
{
    bool ok = false;
    QString msg;
};

State analyze(const QByteArray &image);
Result apply(const QString &exePath, bool enabled, const Config &config);

// Everything analyze() concluded about one file, printed for the command line
// (`BSI-Customizer --about-probe <exe>`). Same role as ThemeEngine::probeReport:
// when the About page reports "not supported", this says which step failed and
// what the page actually contains, instead of leaving it to guesswork.
QString probeReport(const QString &exePath);


} // namespace AboutPatcher
