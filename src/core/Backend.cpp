#include "Backend.h"

#include "BootVideoPatcher.h"
#include "BsDetector.h"
#include "ExtrasPatcher.h"
#include "IntegrityPatcher.h"
#include "SettingsManager.h"
#include "ThemeEngine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

Backend::Backend(SettingsManager *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
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
    m_settings->setDllPath(m.value(QStringLiteral("opengl_dll")).toString());
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
        emit themeChanged();
        return;
    }

    m_themeInfo = t;
    m_themeEntries = ThemeEngine::parseQmlEntries(t.qml);
    const QString kind = (t.mode == QLatin1String("plain")) ? QStringLiteral("plain text")
                                                            : QStringLiteral("zlib");
    m_themeStatus = QStringLiteral("QML theme loaded (%1): %2 color entries%3.")
                        .arg(kind)
                        .arg(m_themeEntries.size())
                        .arg(t.hasQrcNode ? QStringLiteral(", qrc-linked (relocation ready)")
                                          : QString());
    doLog(m_themeStatus, QStringLiteral("blue"));
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
    if (m_edits.isEmpty())
        return result(false, QStringLiteral("Nothing to patch - set some colors first."),
                      QStringLiteral("orange"));

    const auto r = ThemeEngine::patchTheme(m_settings->exePath(), m_themeInfo, m_themeEntries,
                                           m_edits,
                                           [this](const QString &m, const QString &c) { doLog(m, c); });
    if (r.ok) {
        m_edits.clear();
        loadTheme(QStringLiteral("qml")); // reload fresh state
    }
    return result(r.ok, r.msg, QStringLiteral("green"));
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
        emit themeChanged();
    }
    return result(r.ok, r.msg, QStringLiteral("green"));
}

// ---------------------------------------------------------------------- svg

QVariantList Backend::svgInfos() const
{
    QVariantList out;
    for (const SvgBlob &b : m_svgs)
        out << QVariantMap { { "index", b.index }, { "size", b.size } };
    return out;
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
