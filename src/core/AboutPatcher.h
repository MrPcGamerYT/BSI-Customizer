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
    // The words the anchor itself prints. The address and the text are two
    // independent things: a row can open https://example.com/join and still
    // print "Join the channel". Empty means "print the address the way the
    // stock page does", which is also what an older config file implies.
    QString label;
    bool hidden = false;
    // How many characters the page's record has room for, when the link came
    // from the compiled copy of the page (a fixed-size record cannot grow).
    // 0 means "no known limit": the .qml this card also edits is a text file.
    int nameMax = 0;
    int labelMax = 0;
    int urlMax = 0;
};

struct Config
{
    QString text;              // the About sentence
    QList<Link> links;         // one entry per stock link row
    bool showSections = true;  // force the MSI-hidden sections visible
};

// One literal of the About page as the player *loads* it. BlueStacks ships
// PlrAboutSetting.qml as text and a compiled copy of the same page, and the
// window reads the compiled one: every word on the page is a UTF-16LE record
// of that unit's string table. Editing the .qml alone therefore reaches a copy
// nothing displays, which is why the About card looked like it did nothing.
struct TableString
{
    int index = 0;      // position in the page's string table, in file order
    QString text;       // what the page prints now
    int maxChars = 0;   // characters that still fit, padding included
};

struct State
{
    bool supported = false; // the player's About QML has the expected shape
    bool applied = false;   // our content is currently written into the EXE
    QString detail;         // why not supported / what is currently there
    bool sectionsHidden = false; // the build hides the links/About sections
    Config config;          // what the page says now (or what was applied)
    // The page source exactly as it was read out of the EXE. The editor shows
    // the first lines of it, so a build whose page cannot be parsed says what
    // it does contain instead of showing an empty card.
    QString source;
    // True when the page was found by the byte scan (no resource tree behind
    // it): it can be read and analyzed, but not rewritten.
    bool scanned = false;
    // The words the compiled copy of the page prints, in file order. Empty on a
    // build whose About page is plain QML only. Every one of them is rewritten
    // in place - never moved, never grown - when the config is applied.
    QList<TableString> table;
    bool compiledPage = false;
    // How many characters the About sentence's record holds. The page's records
    // do not grow, so this is the longest text the editor can accept.
    int textMax = 0;
    // True when the .qml copy is a shape the editor can rewrite. A build whose
    // page is only recognizable as compiled code reports supported and can have
    // its strings rewritten, but there is no .qml to edit.
    bool qmlEditable = false;
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
QString summary(const QString &exePath);


} // namespace AboutPatcher
