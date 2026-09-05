#include "Backend.h"

#include "BootVideoPatcher.h"
#include "BsDetector.h"
#include "ExtrasPatcher.h"
#include "IntegrityPatcher.h"
#include "SettingsManager.h"
#include "ThemeEngine.h"
#include "ThemePresets.h"

#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

Backend::Backend(SettingsManager *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    connect(settings, &SettingsManager::dllPathChanged, this, &Backend::dllPathChanged);
    connect(settings, &SettingsManager::confPathChanged, this, &Backend::confPathChanged);
    connect(settings, &SettingsManager::exePathChanged, this, [this] {
        m_themeInfo = {};
        m_themeEntries.clear();
        m_hexQmlColors.clear();
        m_edits.clear();
        m_themeFilePath.clear();
        m_themeStatus.clear();
        m_svgs.clear();
        emit exePathChanged();
        emit themeChanged();
        emit svgsChanged();
    });
}

QString Backend::exePath() const { return m_settings->exePath(); }
void Backend::setExePath(const QString &p) { m_settings->setExePath(p); }
QString Backend::dllPath() const { return m_settings->dllPath(); }
void Backend::setDllPath(const QString &p) { m_settings->setDllPath(p); }
QString Backend::confPath() const { return m_settings->confPath(); }
void Backend::setConfPath(const QString &p) { m_settings->setConfPath(p); }

void Backend::doLog(const QString &msg, const QString &color)
{
    emit log(msg, color);
}

QVariantMap Backend::result(bool ok, const QString &msg, const QString &color)
{
    doLog(msg, ok ? color : QStringLiteral("red"));
    return { { "ok", ok }, { "msg", msg } };
}

QByteArray Backend::readExe()
{
    QFile f(m_settings->exePath());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

// Remembers the EXE state the on-screen theme was read from, so the UI can tell
// when another tool (or BlueStacks itself) changed the file underneath us.
void Backend::noteThemeFileState()
{
    const QFileInfo fi(m_settings->exePath());
    m_themeFilePath = fi.absoluteFilePath();
    m_themeFileMsecs = fi.exists() ? fi.lastModified().toMSecsSinceEpoch() : -1;
    m_themeFileBytes = fi.exists() ? fi.size() : -1;
}

bool Backend::themeFileChanged() const
{
    if (m_themeFilePath.isEmpty())
        return false;
    const QFileInfo fi(m_themeFilePath);
    if (!fi.exists())
        return true; // deleted, moved, or replaced by a restore
    return fi.lastModified().toMSecsSinceEpoch() != m_themeFileMsecs
        || fi.size() != m_themeFileBytes;
}

bool Backend::themeStale() const
{
    return m_themeInfo.found && themeFileChanged();
}

void Backend::reloadTheme()
{
    loadTheme(m_themeMethod);
}

// ------------------------------------------------------------------ detection

QVariantList Backend::detectInstalls()
{
    m_installs = BsDetector::findBlueStacks();
    emit installsChanged();
    if (m_installs.isEmpty())
        doLog(QStringLiteral("BlueStacks not auto-detected - set the paths manually."),
              QStringLiteral("orange"));
    else
        for (const QVariant &v : m_installs)
            doLog(QStringLiteral("Detected %1 at: %2")
                      .arg(v.toMap().value(QStringLiteral("profile")).toString(),
                           v.toMap().value(QStringLiteral("root")).toString()),
                  QStringLiteral("blue"));
    return m_installs;
}

void Backend::useInstall(int index)
{
    if (index < 0 || index >= m_installs.size())
        return;
    const QVariantMap m = m_installs.at(index).toMap();
    m_settings->setExePath(m.value(QStringLiteral("exe")).toString());
    m_settings->setDllPath(m.value(QStringLiteral("fps_dll")).toString());
    m_settings->setConfPath(m.value(QStringLiteral("conf")).toString());
    doLog(QStringLiteral("Using %1 install: %2")
              .arg(m.value(QStringLiteral("profile")).toString(),
                   m.value(QStringLiteral("root")).toString()),
          QStringLiteral("green"));
}

QVariantList Backend::detectInstances()
{
    const QVariantList list = ExtrasPatcher::detectInstances(m_settings->confPath());
    if (list.isEmpty())
        doLog(QStringLiteral("No instances found in %1")
                  .arg(QDir::toNativeSeparators(m_settings->confPath())),
              QStringLiteral("orange"));
    else
        doLog(QStringLiteral("%1 instance(s) detected.").arg(list.size()),
              QStringLiteral("blue"));
    return list;
}

// --------------------------------------------------------------------- theme

QVariantList Backend::themeEntries() const
{
    QVariantList out;
    for (const QmlEntry &e : m_themeEntries) {
        out << QVariantMap { { "name", e.name },
                             { "value", e.value },
                             { "edit", m_edits.value(e.name) },
                             { "purpose", ThemeEngine::purpose(e.name) } };
    }
    return out;
}

QVariantList Backend::hexColors() const
{
    QVariantList out;
    for (const HexColorItem &c : m_hexQmlColors) {
        out << QVariantMap { { "name", c.name },
                             { "current", c.currentHex },
                             { "color", c.color },
                             { "enabled", c.enabled },
                             { "purpose", ThemeEngine::purpose(c.name) } };
    }
    return out;
}

void Backend::loadTheme(const QString &method)
{
    m_themeInfo = {};
    m_themeEntries.clear();
    m_edits.clear();
    m_hexQmlColors.clear();
    m_themeStatus.clear();
    m_themeMethod = method;
    m_themeFilePath.clear();
    m_themeFileMsecs = -1;
    m_themeFileBytes = -1;

    const QString exe = m_settings->exePath();
    if (!QFileInfo::exists(exe)) {
        m_themeStatus = QStringLiteral("HD-Player.exe not found.");
        doLog(m_themeStatus, QStringLiteral("red"));
        emit themeChanged();
        return;
    }

    const QByteArray data = readExe();
    if (data.isEmpty()) {
        m_themeStatus = QStringLiteral("Cannot read EXE (run as administrator?).");
        doLog(m_themeStatus, QStringLiteral("red"));
        emit themeChanged();
        return;
    }
    noteThemeFileState();
    // Say exactly which file the colors come from: reading a backup, a second
    // install or the wrong profile is the other way this page can show a theme
    // BlueStacks does not have.
    doLog(QStringLiteral("Theme source: %1 (%2 bytes, modified %3)")
              .arg(m_themeFilePath)
              .arg(m_themeFileBytes)
              .arg(m_themeFileMsecs > 0
                       ? QDateTime::fromMSecsSinceEpoch(m_themeFileMsecs).toString(Qt::ISODate)
                       : QStringLiteral("unknown")),
          QStringLiteral("gray"));
    if (BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe")))
        doLog(QStringLiteral("BlueStacks is RUNNING. Colors shown are read from disk and are "
                             "correct, but a running BlueStacks keeps the theme it loaded at "
                             "start - close it before patching, then start it again."),
              QStringLiteral("orange"));

    if (method == QLatin1String("hex")) {
        m_themeInfo = ThemeEngine::findThemeHex(
            data, ThemeEngine::detectProfileFromPath(exe));
        for (const QString &name : ThemeEngine::qmlColorNames()) {
            const QString cur = ThemeEngine::findQmlColor(data, name);
            if (!cur.isEmpty()) {
                HexColorItem item;
                item.name = name;
                item.currentHex = cur;
                m_hexQmlColors << item;
            }
        }
        m_themeStatus = QStringLiteral("Hex theme ready (%1 profile, %2 color(s) found).")
                            .arg(m_themeInfo.profile)
                            .arg(m_hexQmlColors.size());
        doLog(QStringLiteral("Theme loaded (hex): %1 color(s) found.")
                  .arg(m_hexQmlColors.size()),
              QStringLiteral("blue"));
        doLog(QStringLiteral("Tick the colors you want, then Apply Selected."),
              QStringLiteral("gray"));
        emit themeChanged();
        return;
    }

    ThemeInfo t = ThemeEngine::detectTheme(data, QStringLiteral("qml"));
    if (!t.found) {
        m_themeStatus = QStringLiteral("No QML theme found in this EXE.");
        doLog(m_themeStatus, QStringLiteral("red"));
        doLog(QStringLiteral("Could not find the QML theme record in this file. Try the Hex (old) "
                             "method, or check the EXE path points at your BlueStacks install's "
                             "HD-Player.exe and not at a backup."),
              QStringLiteral("orange"));
        emit themeChanged();
        return;
    }

    m_themeInfo = t;
    m_themeEntries = ThemeEngine::parseQmlEntries(t.qml);
    const QString kind = (t.mode == QLatin1String("plain")) ? QStringLiteral("plain text")
                                                            : QStringLiteral("zlib");

    // Where the record lives, so a wrong/orphaned copy is visible at a glance.
    QString where;
    if (t.mode == QLatin1String("zlib")) {
        where = QStringLiteral(" @0x%1").arg(t.entryOff, 0, 16);
        if (!t.hasQrcNode)
            where += QStringLiteral(" [NOT qrc-linked]");
        else if (t.qrcStrict)
            where += QStringLiteral(" [qrc-linked]");
        else
            where += QStringLiteral(" [qrc-linked, loose match]");
    }

    m_themeStatus = QStringLiteral("QML theme loaded (%1): %2 color entries%3.")
                        .arg(kind)
                        .arg(m_themeEntries.size())
                        .arg(where);
    doLog(m_themeStatus, QStringLiteral("blue"));

    if (t.ambiguous) {
        doLog(QStringLiteral("Multiple theme resources could be active. Patching is blocked "
                             "rather than guessing; run --theme-probe on this EXE."),
              QStringLiteral("orange"));
    } else if (t.candidateCount > 1 && t.hasQrcNode) {
        doLog(QStringLiteral("Found %1 theme-like records%2. The selected UiThemeObject.qml "
                             "is validated against the Qt resource tree; other records were "
                             "not selected for editing.")
                  .arg(t.candidateCount).arg(where),
              QStringLiteral("orange"));
    }
    if (t.mode == QLatin1String("zlib") && !t.hasQrcNode) {
        doLog(QStringLiteral("No qrc node found for UiThemeObject.qml - a theme larger than the "
                             "current slot cannot be relocated in this EXE."),
              QStringLiteral("orange"));
    }

    // Anchor values: compare these with what BlueStacks actually shows to tell
    // a stale read apart from a stale UI.
    QStringList sample;
    for (int i = 0; i < m_themeEntries.size() && i < 4; ++i)
        sample << QStringLiteral("%1=%2").arg(m_themeEntries.at(i).name,
                                              m_themeEntries.at(i).value);
    if (!sample.isEmpty())
        doLog(QStringLiteral("Live values in the EXE: %1").arg(sample.join(QLatin1String(", "))),
              QStringLiteral("gray"));

    doLog(QStringLiteral("Set some colors, then PATCH THEME."), QStringLiteral("gray"));
    emit themeChanged();
}

void Backend::setThemeEdit(const QString &name, const QString &value)
{
    if (value.trimmed().isEmpty())
        m_edits.remove(name);
    else
        m_edits[name] = value.trimmed();
    emit themeChanged();
}

void Backend::clearThemeEdits()
{
    m_edits.clear();
    emit themeChanged();
}

QVariantMap Backend::patchThemeQml()
{
    if (!m_themeInfo.found || m_themeInfo.mode == QLatin1String("hex")) {
        return result(false,
                      QStringLiteral("PATCH THEME is for the QML method; use Apply Selected for hex."),
                      QStringLiteral("orange"));
    }
    if (m_edits.isEmpty() && !m_themeInfo.hasQrcNode)
        return result(false, QStringLiteral("Nothing to patch - set some colors first."),
                      QStringLiteral("orange"));
    if (themeFileChanged())
        doLog(QStringLiteral("HD-Player.exe changed since it was loaded - patching the theme "
                             "that is in the file now, not the one on screen."),
              QStringLiteral("orange"));
    if (BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe")))
        doLog(QStringLiteral("BlueStacks is RUNNING - close it first, otherwise the write can be "
                             "blocked and the running instance keeps its old theme."),
              QStringLiteral("orange"));

    const auto r = ThemeEngine::patchTheme(m_settings->exePath(), m_themeInfo, m_themeEntries,
                                           m_edits,
                                           [this](const QString &m, const QString &c) { doLog(m, c); });
    if (r.ok) {
        m_edits.clear();
        reloadTheme(); // reload fresh state from the file we just wrote
        doLog(QStringLiteral("Start BlueStacks now to see the new theme."),
              QStringLiteral("gray"));
    }
    return result(r.ok, r.msg, QStringLiteral("green"));
}

// ------------------------------------------------------------------- presets

QVariantList Backend::themePresets() const { return ThemePresets::toVariantList(); }

int Backend::applyPreset(const QString &id)
{
    const ThemePreset *p = ThemePresets::byId(id);
    if (!p) {
        doLog(QStringLiteral("Unknown preset: %1").arg(id), QStringLiteral("red"));
        return 0;
    }
    if (m_themeEntries.isEmpty()) {
        doLog(QStringLiteral("Load the QML theme first, then pick a preset."),
              QStringLiteral("orange"));
        return 0;
    }

    const QMap<QString, QString> matched = ThemePresets::matchEntries(*p, m_themeEntries);
    for (auto it = matched.constBegin(); it != matched.constEnd(); ++it)
        m_edits[it.key()] = it.value();
    emit themeChanged();

    const int already = m_themeEntries.size() - matched.size();
    doLog(QStringLiteral("Preset \"%1\": %2 color(s) staged, %3 already match. "
                         "Press PATCH THEME to write them into HD-Player.exe.")
              .arg(p->name)
              .arg(matched.size())
              .arg(already),
          QStringLiteral("blue"));
    if (matched.isEmpty())
        doLog(QStringLiteral("The EXE already has this palette. Press PATCH THEME to refresh its cache."),
              QStringLiteral("orange"));
    return matched.size();
}

QString Backend::presetToggleOff(const QString &id) const
{
    const ThemePreset *p = ThemePresets::byId(id);
    return p ? p->toggleOff : QString();
}

QString Backend::presetToggleOn(const QString &id) const
{
    const ThemePreset *p = ThemePresets::byId(id);
    return p ? p->toggleOn : QString();
}

void Backend::setHexColor(const QString &name, const QString &hex)
{
    for (HexColorItem &c : m_hexQmlColors) {
        if (c.name == name) {
            c.color = hex;
            if (!hex.isEmpty())
                c.enabled = true;
            break;
        }
    }
    emit themeChanged();
}

void Backend::setHexEnabled(const QString &name, bool on)
{
    for (HexColorItem &c : m_hexQmlColors) {
        if (c.name == name) {
            c.enabled = on;
            break;
        }
    }
    emit themeChanged();
}

void Backend::resetHexColors()
{
    for (HexColorItem &c : m_hexQmlColors) {
        c.enabled = false;
        c.color.clear();
    }
    doLog(QStringLiteral("All colors reset to default."), QStringLiteral("green"));
    emit themeChanged();
}

int Backend::previewHex()
{
    const QByteArray data = readExe();
    if (data.isEmpty()) {
        doLog(QStringLiteral("Cannot read EXE."), QStringLiteral("red"));
        return 0;
    }
    const auto repls = ThemeEngine::collectThemeReplacements(data, m_themeInfo.profile, {},
                                                             m_hexQmlColors);
    doLog(QStringLiteral("Preview: %1 color change(s) would be made.").arg(repls.size()),
          QStringLiteral("blue"));
    return repls.size();
}

QVariantMap Backend::applyHex()
{
    if (!m_themeInfo.found || m_themeInfo.mode != QLatin1String("hex"))
        return result(false, QStringLiteral("Load an old (hex) theme first."),
                      QStringLiteral("orange"));

    if (BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe")))
        doLog(QStringLiteral("BlueStacks is RUNNING - close it first, otherwise the write can be "
                             "blocked and the running instance keeps its old theme."),
              QStringLiteral("orange"));

    auto r = ThemeEngine::applyHexTheme(m_settings->exePath(), m_themeInfo.profile, {},
                                        m_hexQmlColors,
                                        [this](const QString &m, const QString &c) { doLog(m, c); });
    if (r.ok) {
        // Update current values + rescan from disk.
        for (HexColorItem &c : m_hexQmlColors) {
            if (c.enabled && !c.color.isEmpty()) {
                c.currentHex = c.color;
                c.color.clear();
                c.enabled = false;
            }
        }
        reloadTheme(); // re-read the file so the list shows what is really in it
        doLog(QStringLiteral("Start BlueStacks now to see the new theme."),
              QStringLiteral("gray"));
    }
    return result(r.ok, r.msg, QStringLiteral("green"));
}

// ---------------------------------------------------------------------- svg

QVariantList Backend::svgInfos() const
{
    const SvgEngine::ToggleGuess guess = SvgEngine::guessToggles(m_svgs);
    QVariantList out;
    for (const SvgBlob &b : m_svgs) {
        QString hint;
        if (b.index == guess.on)
            hint = QStringLiteral("switch ON?");
        else if (b.index == guess.off)
            hint = QStringLiteral("switch OFF?");
        out << QVariantMap { { "index", b.index },
                             { "size", b.size },
                             { "offset", b.offset },
                             { "preview", SvgEngine::preview(b.data) },
                             // Full text so the QML list can render a live
                             // thumbnail of each icon (data: URI Image source).
                             { "svg", QString::fromLatin1(b.data) },
                             { "colors", SvgEngine::colorsIn(b.data) },
                             { "hint", hint } };
    }
    return out;
}

QVariantMap Backend::guessToggleSvgs()
{
    if (m_svgs.isEmpty())
        scanSvgs();
    const SvgEngine::ToggleGuess g = SvgEngine::guessToggles(m_svgs);

    auto previewAt = [this](int index) {
        if (index < 0 || index >= m_svgs.size())
            return QString();
        return SvgEngine::preview(m_svgs.at(index).data);
    };
    // The color worth pre-filling is the track, not the knob: a switch knob is
    // nearly always pure white or black, and recolor keeps it that way.
    auto trackColorAt = [this](int index) {
        if (index < 0 || index >= m_svgs.size())
            return QString();
        const QStringList cols = SvgEngine::colorsIn(m_svgs.at(index).data);
        // The color field only speaks "#RRGGBB"; SVG also allows "#RRGGBBAA"
        // (alpha last, unlike Qt) and "#RGB".
        auto toSix = [](const QString &c) {
            QString h = c;
            if (h.size() == 9)
                h = h.left(7);
            else if (h.size() == 4)
                h = QStringLiteral("#") + h.mid(1, 1) + h.mid(1, 1) + h.mid(2, 1) + h.mid(2, 1)
                    + h.mid(3, 1) + h.mid(3, 1);
            return h.toUpper();
        };
        for (const QString &c : cols) {
            const QString six = toSix(c);
            const QColor col(six);
            if (!col.isValid())
                continue;
            const bool knob = (col.red() == 255 && col.green() == 255 && col.blue() == 255)
                              || (col.red() == 0 && col.green() == 0 && col.blue() == 0);
            if (!knob)
                return six;
        }
        return cols.isEmpty() ? QString() : toSix(cols.first());
    };

    return { { "off", g.off }, { "on", g.on },
             { "offColor", trackColorAt(g.off) }, { "onColor", trackColorAt(g.on) },
             { "offPreview", previewAt(g.off) }, { "onPreview", previewAt(g.on) } };
}

QVariantMap Backend::recolorToggleSvgs(int offIndex, int onIndex, const QString &offHex,
                                       const QString &onHex)
{
    if (m_svgs.isEmpty())
        scanSvgs();
    const auto r = SvgEngine::recolorToggles(
        m_settings->exePath(), offIndex, onIndex, offHex, onHex,
        [this](const QString &m, const QString &c) { doLog(m, c); });
    if (r.ok) {
        m_svgs.clear();          // colors on screen would be stale
        noteThemeFileState();    // the theme record itself was not touched
        doLog(QStringLiteral("The QML theme record was not touched - staged colors stay valid. "
                             "Start BlueStacks to see the new switch colors."),
              QStringLiteral("gray"));
        emit svgsChanged();
    }
    return result(r.ok, r.msg, QStringLiteral("green"));
}

QVariantMap Backend::recolorTogglesFromPreset(const QString &id)
{
    const ThemePreset *p = ThemePresets::byId(id);
    if (!p)
        return result(false, QStringLiteral("Unknown preset: %1").arg(id), QStringLiteral("red"));
    if (m_svgs.isEmpty())
        scanSvgs();
    const SvgEngine::ToggleGuess g = SvgEngine::guessToggles(m_svgs);
    if (g.off < 0 && g.on < 0) {
        return result(false,
                      QStringLiteral("No switch/toggle SVG found in this EXE. Pick the two blobs "
                                     "manually on the SVG page and recolor them there."),
                      QStringLiteral("orange"));
    }
    doLog(QStringLiteral("Switch SVGs found - OFF index %1, ON index %2. Recoloring both from "
                         "preset \"%3\" (off %4, on %5).")
              .arg(g.off)
              .arg(g.on)
              .arg(p->name)
              .arg(p->toggleOff)
              .arg(p->toggleOn),
          QStringLiteral("gray"));
    return recolorToggleSvgs(g.off, g.on, p->toggleOff, p->toggleOn);
}

int Backend::scanSvgs()
{
    m_svgs.clear();
    const QByteArray data = readExe();
    if (!data.isEmpty()) {
        m_svgs = SvgEngine::findSvgs(data);
        doLog(QStringLiteral("SVG: found %1 SVG(s) in %2")
                  .arg(m_svgs.size())
                  .arg(QFileInfo(m_settings->exePath()).fileName()),
              QStringLiteral("blue"));
    } else {
        doLog(QStringLiteral("SVG: EXE not found."), QStringLiteral("red"));
    }
    emit svgsChanged();
    return m_svgs.size();
}

QString Backend::svgTextAt(int index) const
{
    if (index < 0 || index >= m_svgs.size())
        return {};
    return QString::fromLatin1(m_svgs.at(index).data);
}

QVariantMap Backend::extractSvgs()
{
    const QString outDir = QFileInfo(m_settings->exePath()).absolutePath()
                           + QStringLiteral("/svg_extracted");
    int count = 0;
    SvgEngine::extractSvgs(m_settings->exePath(), outDir, &count);
    return result(count > 0,
                  QStringLiteral("Extracted %1 SVG(s) to %2")
                      .arg(count)
                      .arg(QDir::toNativeSeparators(outDir)),
                  QStringLiteral("green"));
}

QVariantMap Backend::replaceSvg(int index, const QString &newSvgPath)
{
    const auto r = SvgEngine::replaceSvg(m_settings->exePath(), index, newSvgPath);
    if (r.ok)
        scanSvgs();
    return result(r.ok, r.msg, QStringLiteral("green"));
}

// ---------------------------------------------------------------------- fps

static QVariantMap analysisToMap(const FpsPatcher::Analysis &a)
{
    QVariantList targets;
    for (const FpsPatcher::Target &t : a.targets)
        targets << QVariantMap { { "name", t.name }, { "cls", t.cls }, { "state", t.state } };
    return { { "ok", a.ok }, { "state", a.state }, { "msg", a.msg }, { "targets", targets } };
}

QVariantMap Backend::fpsAnalyze()
{
    if (!FpsPatcher::isSupportedPath(m_settings->dllPath()))
        return result(false, QStringLiteral("Select Qt5QuickTemplates2.dll or Qt6QuickTemplates2.dll "
                                            "from your BlueStacks installation."), QStringLiteral("red"));
    QFile f(m_settings->dllPath());
    if (!f.open(QIODevice::ReadOnly))
        return result(false, QStringLiteral("Cannot read %1").arg(m_settings->dllPath()),
                      QStringLiteral("red"));
    const auto a = FpsPatcher::analyze(f.readAll());
    f.close();
    doLog(a.msg, a.ok ? QStringLiteral("blue") : QStringLiteral("orange"));
    auto m = analysisToMap(a);
    m.insert(QStringLiteral("msg"), a.msg);
    return m;
}

QVariantMap Backend::fpsPatch(double hi)
{
    const auto r = FpsPatcher::patch(m_settings->dllPath(), hi);
    return result(r.ok, r.msg, QStringLiteral("green"));
}

QVariantMap Backend::fpsRestore()
{
    const auto r = FpsPatcher::restore(m_settings->dllPath());
    return result(r.ok, r.msg, QStringLiteral("green"));
}

// ---------------------------------------------------------------- integrity

QVariantMap Backend::integrityCheck()
{
    const auto a = IntegrityPatcher::analyze(m_settings->exePath());
    QString color = QStringLiteral("blue");
    if (a.state == QLatin1String("bad") || a.state == QLatin1String("notfound")
        || a.state == QLatin1String("ambig") || a.state == QLatin1String("nofile"))
        color = QStringLiteral("red");
    else if (a.state == QLatin1String("patched") || a.state == QLatin1String("absent"))
        color = QStringLiteral("green");
    return result(a.ok, a.msg, color);
}

QVariantMap Backend::integrityPatch()
{
    const auto r = IntegrityPatcher::patch(m_settings->exePath());
    return result(r.ok, r.msg, QStringLiteral("green"));
}

QVariantMap Backend::integrityRestore()
{
    const auto r = IntegrityPatcher::restore(m_settings->exePath());
    return result(r.ok, r.msg, QStringLiteral("green"));
}

// -------------------------------------------------------------- boot video

QVariantMap Backend::bootPatch()
{
    const auto r = BootVideoPatcher::patch(m_settings->exePath());
    return result(r.ok, r.msg, QStringLiteral("green"));
}

// ------------------------------------------------------------------ extras

QVariantList Backend::findVersions()
{
    const QByteArray data = readExe();
    if (data.isEmpty()) {
        doLog(QStringLiteral("EXE not found."), QStringLiteral("red"));
        m_versions.clear();
        emit versionsChanged();
        return m_versions;
    }
    const auto found = ExtrasPatcher::findVersions(data);
    m_versions.clear();
    for (const QVariantMap &m : found)
        m_versions << m.value(QStringLiteral("version")).toString();
    emit versionsChanged();
    doLog(m_versions.isEmpty() ? QStringLiteral("No version strings found.")
                               : QStringLiteral("Found %1 version string(s).").arg(m_versions.size()),
          m_versions.isEmpty() ? QStringLiteral("orange") : QStringLiteral("blue"));
    return m_versions;
}

QVariantMap Backend::applyVersion(const QString &oldV, const QString &newV)
{
    if (oldV.isEmpty() || newV.isEmpty())
        return result(false, QStringLiteral("Select a version and enter a new one."),
                      QStringLiteral("orange"));
    ThemeEngine::backup(m_settings->exePath());
    const auto r = ExtrasPatcher::applyVersion(m_settings->exePath(), oldV, newV);
    return result(r.ok, r.msg, QStringLiteral("green"));
}

QVariantMap Backend::applyRemovals(const QStringList &names)
{
    if (names.isEmpty())
        return result(false, QStringLiteral("Select at least one item."),
                      QStringLiteral("orange"));
    ThemeEngine::backup(m_settings->exePath());
    const auto r = ExtrasPatcher::applyRemovals(m_settings->exePath(), names);
    return result(r.ok, r.msg, QStringLiteral("green"));
}

QVariantMap Backend::restoreBackup(const QString &path)
{
    const QString bak = path + QStringLiteral(".bak");
    if (!QFileInfo::exists(bak))
        return result(false, QStringLiteral("No backup found."), QStringLiteral("red"));
    QFile::remove(path);
    if (!QFile::copy(bak, path))
        return result(false, QStringLiteral("Restore failed."), QStringLiteral("red"));
    return result(true, QStringLiteral("Restored from backup."), QStringLiteral("green"));
}

QVariantList Backend::removalNames() const
{
    QVariantList out;
    for (const QString &n : ExtrasPatcher::removalNames())
        out << n;
    return out;
}
