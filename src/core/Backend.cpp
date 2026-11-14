#include "Backend.h"

#include "AdbClient.h"
#include "AndroidCleanup.h"
#include "AuthManager.h"
#include "CustomizationWorkspace.h"
#include "AddonPacks.h"
#include "SkinProfiles.h"
#include "PlayerSkinCompiler.h"
#include <QCryptographicHash>
#include "BootVideoPatcher.h"
#include "PlayerCache.h"
#include "PlayerAppearancePatcher.h"
#include "GifRpc.h"
#include "BsDetector.h"
#include "ExtrasPatcher.h"
#include "IntegrityPatcher.h"
#include "AboutPatcher.h"
#include "SettingsPanelPatcher.h"
#include "DebloatPatcher.h"
#include "LayoutPatcher.h"
#include "RootVhd.h"
#include "RpcIdPatcher.h"
#include "QtResources.h"
#include "SettingsManager.h"
#include "ThemeEngine.h"
#include "ThemePresets.h"

#include <QColor>
#include <QSet>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QThread>

#include <algorithm>
#include <exception>

Backend::Backend(SettingsManager *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_customizationProfile = {{"schema", "bsi.customization.profile"}, {"version", 1}, {"name", "My workspace"}, {"theme", QVariantMap{}}, {"boot", QVariantMap{{"holdMs", 3000}}}, {"gifRpc", QVariantMap{}}, {"about", QVariantMap{}}, {"appearance", QVariantMap{}}};
    connect(settings, &SettingsManager::dllPathChanged, this, &Backend::dllPathChanged);
    connect(settings, &SettingsManager::confPathChanged, this, &Backend::confPathChanged);
    connect(settings, &SettingsManager::exePathChanged, this, [this] {
        m_workspaceOperations.clear(); m_workspaceHash.clear(); m_themeFileHash.clear(); m_resourceFileHash.clear();
        emit workspaceChanged();
        m_themeInfo = {};
        m_themeEntries.clear();
        m_gradientTargets.clear();
        emit gradientTargetsChanged();
        m_hexQmlColors.clear();
        m_edits.clear();
        m_themeFilePath.clear();
        m_themeStatus.clear();
        m_themeDetection.clear();
        m_detectedQtMajor = 0;
        m_qmlThemeAvailable = false;
        m_svgs.clear();
        m_resources.clear();
        m_resourceInfos.clear();
        ++m_resourceRevision;
        m_resourcesIncludeBinary = true;
        emit exePathChanged();
        emit themeChanged();
        emit svgsChanged();
        // The Resources tab reads entry offsets out of this list: keeping it on
        // screen after the target changed made every preview read the new file
        // at the old file's offsets, which looks like "the pictures do not
        // show" on whichever build was selected second.
        emit resourcesChanged();
    });
}

// An isolated facade/settings snapshot keeps all engine state thread-local.
// Keep the existing return contract while allowing the GUI to paint and respond.
void Backend::copyPatchState(const Backend &source)
{
    m_customizationProfile = source.m_customizationProfile;
    m_workspaceOperations = source.m_workspaceOperations;
    m_workspaceHash = source.m_workspaceHash;
    m_themeFileHash = source.m_themeFileHash;
    m_resourceFileHash = source.m_resourceFileHash;
    m_themeInfo = source.m_themeInfo;
    m_themeStatus = source.m_themeStatus;
    m_themeMethod = source.m_themeMethod;
    m_themeDetection = source.m_themeDetection;
    m_detectedQtMajor = source.m_detectedQtMajor;
    m_qmlThemeAvailable = source.m_qmlThemeAvailable;
    m_themeFilePath = source.m_themeFilePath;
    m_themeFileMsecs = source.m_themeFileMsecs;
    m_themeFileBytes = source.m_themeFileBytes;
    m_themeEntries = source.m_themeEntries;
    m_gradientTargets = source.m_gradientTargets;
    m_edits = source.m_edits;
    m_hexQmlColors = source.m_hexQmlColors;
    m_svgs = source.m_svgs;
    m_resources = source.m_resources;
    m_resourceInfos = source.m_resourceInfos;
    m_resourceRevision = source.m_resourceRevision;
    m_resourcesIncludeBinary = source.m_resourcesIncludeBinary;
    m_installs = source.m_installs;
}

QVariantMap Backend::runBackground(const std::function<QVariantMap(Backend &)> &operation)
{
    if (actionsLocked()) return lockedMap();
    auto *settings = new SettingsManager(nullptr, false);
    settings->m_exePath = m_settings->m_exePath;
    settings->m_dllPath = m_settings->m_dllPath;
    settings->m_confPath = m_settings->m_confPath;
    settings->m_fpsAutoDetect = m_settings->m_fpsAutoDetect;
    settings->m_aboutDeveloperName = m_settings->m_aboutDeveloperName;
    settings->m_aboutTagline = m_settings->m_aboutTagline;
    settings->m_aboutLinks = m_settings->m_aboutLinks;
    settings->m_settingsPanelProfile = m_settings->m_settingsPanelProfile;
    auto *worker = new Backend(settings);
    settings->setParent(worker);
    worker->copyPatchState(*this);
    worker->m_backgroundWorker = true;
    connect(worker, &Backend::log, this, &Backend::log, Qt::QueuedConnection);
    QVariantMap response;
    QEventLoop loop;
    QEventLoopLocker quitLock;
    QThread *guiThread = thread();
    auto *job = QThread::create([worker, operation, &response, guiThread] {
        try {
            response = operation(*worker);
        } catch (const std::exception &e) {
            response = {{"ok", false}, {"msg", QString::fromUtf8(e.what())}};
        } catch (...) {
            response = {{"ok", false}, {"msg", QStringLiteral("Patch operation failed unexpectedly.")}};
        }
        worker->moveToThread(guiThread);
    });
    worker->moveToThread(job);
    connect(job, &QThread::finished, &loop, &QEventLoop::quit);
    m_busy = true;
    emit busyChanged();
    job->start();
    loop.exec();
    job->wait();
    // Do not trigger a second expensive DLL scan while synchronizing paths.
    if (m_settings->m_exePath != settings->m_exePath) {
        m_settings->m_exePath = settings->m_exePath;
        emit m_settings->exePathChanged();
    }
    if (m_settings->m_dllPath != settings->m_dllPath) {
        m_settings->m_dllPath = settings->m_dllPath;
        emit m_settings->dllPathChanged();
    }
    if (m_settings->m_confPath != settings->m_confPath) {
        m_settings->m_confPath = settings->m_confPath;
        emit m_settings->confPathChanged();
    }
    if (m_settings->m_fpsAutoDetect != settings->m_fpsAutoDetect) {
        m_settings->m_fpsAutoDetect = settings->m_fpsAutoDetect;
        emit m_settings->fpsAutoDetectChanged();
    }
    const bool resourcesUpdated = m_resourceRevision != worker->m_resourceRevision;
    const bool profileUpdated = m_customizationProfile != worker->m_customizationProfile;
    copyPatchState(*worker);
    if (profileUpdated) emit customizationProfileChanged();
    emit workspaceChanged();
    delete worker;
    delete job;
    emit themeChanged();
    emit gradientTargetsChanged();
    emit svgsChanged();
    emit installsChanged();
    m_busy = false;
    emit busyChanged();
    // Selection handlers may measure the new row's budget. Publish only after
    // releasing the busy guard, otherwise that read is refused as "busy".
    if (resourcesUpdated) emit resourcesChanged();
    return response;
}

QString Backend::exePath() const { return m_settings->exePath(); }
void Backend::setExePath(const QString &p)
{
    if (actionsLocked()) return;
    if (!m_backgroundWorker) {
        runBackground([=](Backend &worker) { worker.setExePath(p); return QVariantMap{}; });
        return;
    }
    m_settings->setExePath(p);
}
QString Backend::dllPath() const { return m_settings->dllPath(); }
void Backend::setDllPath(const QString &p) { if (!m_busy) m_settings->setDllPath(p); }
QString Backend::confPath() const { return m_settings->confPath(); }
void Backend::setConfPath(const QString &p) { if (!m_busy) m_settings->setConfPath(p); }

void Backend::setAuthManager(AuthManager *auth)
{
    m_auth = auth;
}

bool Backend::actionsLocked() const
{
    // No AuthManager attached (tests, scripts) -> ungated. With one attached,
    // the Discord sign-in must be verified first - same authority rule as
    // FalcoForge's Core::needsPaidAccess.
    return m_busy || (m_auth && !m_auth->isAuthorized());
}

QVariantMap Backend::lockedMap() const
{
    if (m_busy)
        return {{"ok", false}, {"msg", QStringLiteral("An operation is already running. Please wait.")}};
    QVariantMap locked;
    locked.insert(QStringLiteral("ok"), false);
    locked.insert(QStringLiteral("error"),
                  QStringLiteral("Sign in with Discord to unlock BSI Customizer."));
    locked.insert(QStringLiteral("message"),
                  QStringLiteral("Sign in with Discord to unlock BSI Customizer."));
    // QML reads `msg` from every apply result (the toast uses it), so the
    // locked short-circuit must carry one too.
    locked.insert(QStringLiteral("msg"),
                  QStringLiteral("Sign in with Discord to unlock BSI Customizer."));
    return locked;
}

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
    if (actionsLocked()) return;
    loadTheme(m_themeMethod);
}

// ------------------------------------------------------------------ detection

QString Backend::localFilePath(const QUrl &url) const
{
    return url.isLocalFile() ? QDir::toNativeSeparators(url.toLocalFile()) : QString();
}

QVariantList Backend::detectInstalls()
{
    if (actionsLocked()) return {};
    if (!m_backgroundWorker)
        return runBackground([](Backend &worker) { return QVariantMap{{QStringLiteral("rows"), worker.detectInstalls()}}; }).value(QStringLiteral("rows")).toList();
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
    if (actionsLocked()) return;
    if (!m_backgroundWorker) {
        runBackground([=](Backend &worker) { worker.useInstall(index); return QVariantMap{}; });
        return;
    }
    if (index < 0 || index >= m_installs.size())
        return;
    const QVariantMap m = m_installs.at(index).toMap();
    m_settings->setFpsAutoDetect(true);
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
    if (actionsLocked()) return {};
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
    if (!m_backgroundWorker) {
        const auto response = runBackground([=](Backend &worker) { worker.loadTheme(method); return QVariantMap{}; });
        Q_UNUSED(response);
        return;
    }
    if (actionsLocked()) return;
    m_themeInfo = {};
    m_themeEntries.clear();
    m_edits.clear();
    m_hexQmlColors.clear();
    m_themeStatus.clear();
    m_themeMethod = method;
    m_qmlThemeAvailable = false;
    m_themeDetection.clear();
    m_detectedQtMajor = 0;
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
    m_themeFileHash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
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

    // Qt 6 HD-Player uses compiled-in QML resources. Qt 5 / MSI 5 still has a
    // UiThemeObject blob, but the player paints from compiled units + hex
    // constants: the QML (new) editor must stay hidden on those builds.
    // A byte search for "Qt6Core" decides the mode for the wrong reason: the string
    // can sit in any embedded resource, and a launcher that reaches Qt through a
    // local DLL imports no Qt6 name at all. The import table is what actually binds
    // this exe to a Qt generation, so the gate reads that (and fails open).
    int qtMajor = 0;
    QString gateWhy;
    m_qmlThemeAvailable = GradientPatcher::qmlMethodSupported(data, &qtMajor, &gateWhy);
    m_detectedQtMajor = qtMajor;
    QString resolved = method;
    if (method == QLatin1String("auto"))
        resolved = m_qmlThemeAvailable ? QStringLiteral("qml") : QStringLiteral("hex");
    m_themeMethod = resolved;
    // Say out loud how the method was picked. Auto-detection is the default
    // for the whole theme changer: the EXE's import table decides QML (new)
    // vs Hex (old), and the page shows this line so there is no guessing.
    const QString methodName = resolved == QLatin1String("qml")
        ? QStringLiteral("QML (new)") : QStringLiteral("Hex (old)");
    if (method == QLatin1String("auto")) {
        m_themeDetection = qtMajor >= 5
            ? QStringLiteral("Auto-detected a Qt %1 player → using %2.").arg(qtMajor).arg(methodName)
            : QStringLiteral("Auto-detect could not read this EXE's Qt imports → using %1 "
                             "(a modern QML player is assumed when the table is unreadable).").arg(methodName);
    } else {
        m_themeDetection = qtMajor >= 5
            ? QStringLiteral("Manually selected %1 (this EXE is detected as a Qt %2 player).")
                  .arg(methodName).arg(qtMajor)
            : QStringLiteral("Manually selected %1.").arg(methodName);
    }
    doLog(m_themeDetection, QStringLiteral("blue"));

    if (resolved == QLatin1String("qml") && !m_qmlThemeAvailable) {
        m_themeStatus = gateWhy.isEmpty()
            ? QStringLiteral("This is an older Qt 5 BlueStacks/MSI 5 player. "
                              "The QML method is not used here — use Hex (old). "
                              "Its Gradient theme paints the same compatible surfaces (top bar, title bar, "
                              "sidebar, buttons…) as the QML editor.")
            : QStringLiteral("This is a Qt %1 player: its UI never reads the palette's gradient properties, so "
                             "a QML gradient target there changes nothing you can see. Use the Gradient theme "
                             "control in the Hex (old) method - it applies your custom stops to any compatible "
                             "surface (top bar, title bar, sidebar, buttons…), just like the QML editor.").arg(qtMajor);
        doLog(m_themeStatus, QStringLiteral("orange"));
        emit themeChanged();
        return;
    }

    if (resolved == QLatin1String("hex")) {
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
        // Count the gradient surfaces the gradient editor offers for this
        // player into the status line, so a player with no gradient-capable
        // surface is visible at load time.
        int hexGradientTargets = 0;
        for (const auto &target : GradientPatcher::analyze(data).targets)
            if (target.hexGradient)
                ++hexGradientTargets;
        m_themeStatus = m_hexQmlColors.isEmpty()
            ? QStringLiteral("No editable Hex colors found (%1 profile). %2 gradient surface(s) available separately. Export Diagnostics for inspection; Hex is not a bypass for an unverified QML mapping.").arg(m_themeInfo.profile).arg(hexGradientTargets)
            : QStringLiteral("Hex theme ready (%1 profile, %2 color(s) found, %3 gradient surface(s)).")
                .arg(m_themeInfo.profile).arg(m_hexQmlColors.size()).arg(hexGradientTargets);
        doLog(m_themeStatus, m_hexQmlColors.isEmpty() ? QStringLiteral("orange") : QStringLiteral("blue"));
        if (!m_hexQmlColors.isEmpty())
            doLog(QStringLiteral("Pick a preset or tick the colors you want, then Apply Theme. Gradient theme paints any listed surface with stops you choose."), QStringLiteral("gray"));
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
        doLog(QStringLiteral("Theme mapping is not verified. Resource-backed theme writing is blocked, even if the replacement appears to fit. Export Diagnostics for this build."),
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
    if (actionsLocked()) return;
    if (value.trimmed().isEmpty())
        m_edits.remove(name);
    else
        m_edits[name] = value.trimmed();
    emit themeChanged();
}

void Backend::clearThemeEdits()
{
    if (actionsLocked()) return;
    m_edits.clear();
    emit themeChanged();
}

QVariantMap Backend::patchThemeQml()
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.patchThemeQml(); });
    if (!m_themeInfo.found || m_themeInfo.mode == QLatin1String("hex")) {
        return result(false,
                      QStringLiteral("PATCH THEME is for the QML method; use Apply Selected for hex."),
                      QStringLiteral("orange"));
    }
    if (m_edits.isEmpty() && !m_themeInfo.hasQrcNode)
        return result(false, QStringLiteral("Nothing to patch - set some colors first."),
                      QStringLiteral("orange"));
    if (themeFileChanged() || m_themeFileHash != QCryptographicHash::hash(readExe(), QCryptographicHash::Sha256))
        return result(false, QStringLiteral("HD-Player.exe changed since Load. Reload and review your edits; stale offsets/settings will not be applied."), QStringLiteral("red"));
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

QVariantList Backend::gradientTargets() const
{
    QVariantList result;
    for (const auto &target : m_gradientTargets)
        result << QVariantMap { { "key", target.key }, { "label", target.label }, { "resource", target.resource },
                                { "kind", target.kind }, { "enabled", target.current.enabled },
                                { "first", target.current.first }, { "second", target.current.second },
                                { "horizontal", target.current.horizontal },
                                { "supportsOrientation", target.supportsOrientation },
                                { "hexGradient", target.hexGradient } };
    return result;
}

QVariantMap Backend::loadGradientTargets()
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.loadGradientTargets(); });
    const auto bytes = readExe();
    m_gradientTargets.clear();
    if (bytes.isEmpty()) {
        emit gradientTargetsChanged();
        return result(false, QStringLiteral("Select a readable HD-Player.exe first."), QStringLiteral("red"));
    }
    const auto analysis = GradientPatcher::analyze(bytes);
    m_gradientTargets = analysis.targets;
    QString message = analysis.message;
    // Older (Qt 5 based) players expose Gradient but not Gradient::orientation, so
    // the direction cannot be honoured there. Say so instead of silently ignoring it.
    if (std::any_of(m_gradientTargets.cbegin(), m_gradientTargets.cend(),
                    [](const GradientPatcher::Target &t) { return !t.supportsOrientation; }))
        message += QStringLiteral(" Some of these documents come from a QtQuick version older than 2.12:"
                                  " their gradient is applied top to bottom and the direction stays fixed there.");
    emit gradientTargetsChanged();
    return result(analysis.ok, message, QStringLiteral("blue"));
}

QVariantMap Backend::applyThemeGradient(const QString &key, const QString &first, const QString &second,
                                      bool horizontal, bool enabled)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.applyThemeGradient(key, first, second, horizontal, enabled); });
    if (!m_edits.isEmpty())
        return result(false, QStringLiteral("Apply or clear the pending solid-color edits before applying a gradient."), QStringLiteral("orange"));
    const auto applied = GradientPatcher::patch(m_settings->exePath(), key, { enabled, first, second, horizontal });
    if (applied.ok) {
        reloadTheme();
        loadGradientTargets();
    }
    return result(applied.ok, applied.message, QStringLiteral("green"));
}

QVariantMap Backend::applyHexGradient(const QString &key, const QString &first, const QString &second,
                                      bool horizontal, bool enabled)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.applyHexGradient(key, first, second, horizontal, enabled); });
    if (!m_edits.isEmpty())
        return result(false, QStringLiteral("Apply or clear the pending solid-color edits before applying a gradient."), QStringLiteral("orange"));
    const auto applied = GradientPatcher::patchSurfaces(m_settings->exePath(), key,
                                                      { enabled, first, second, horizontal });
    if (applied.ok) {
        reloadTheme();
        loadGradientTargets();
    }
    return result(applied.ok, applied.message, QStringLiteral("green"));
}

// ------------------------------------------------------------------- presets

QVariantList Backend::themePresets() const { return ThemePresets::toVariantList(); }

int Backend::applyPreset(const QString &id)
{
    if (actionsLocked()) return 0;
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

int Backend::applyHexPreset(const QString &id)
{
    if (actionsLocked()) return 0;
    const ThemePreset *p = ThemePresets::byId(id);
    if (!p) {
        doLog(QStringLiteral("Unknown preset: %1").arg(id), QStringLiteral("red"));
        return 0;
    }
    if (!m_themeInfo.found || m_themeInfo.mode != QLatin1String("hex")) {
        doLog(QStringLiteral("Load the Hex (old) theme first, then pick a preset."),
              QStringLiteral("orange"));
        return 0;
    }
    if (m_hexQmlColors.isEmpty()) {
        doLog(QStringLiteral("This player has no palette color names the Hex (old) editor can "
                             "match, so a preset cannot be staged. Use Gradient theme for its "
                             "surfaces instead."),
              QStringLiteral("orange"));
        return 0;
    }

    const QMap<QString, QString> matched = ThemePresets::matchHexColors(*p, m_hexQmlColors);
    for (HexColorItem &c : m_hexQmlColors) {
        const QString staged = matched.value(c.name);
        if (staged.isEmpty())
            continue;
        c.color = staged;
        c.enabled = true;
    }
    emit themeChanged();

    // "Already match" counts the rows this preset covers, not every row the
    // EXE happens to expose.
    int covered = 0;
    for (const HexColorItem &c : m_hexQmlColors) {
        if (!p->colors.value(ThemePresets::normalize(c.name)).isEmpty())
            ++covered;
    }
    const int already = covered - matched.size();
    doLog(QStringLiteral("Preset \"%1\" (Hex (old)): %2 color(s) staged, %3 already match. "
                         "Press APPLY THEME to write them into HD-Player.exe.")
              .arg(p->name)
              .arg(matched.size())
              .arg(already),
          QStringLiteral("blue"));
    if (matched.isEmpty())
        doLog(QStringLiteral("The EXE already has this palette. Press APPLY THEME to write it again "
                             "if BlueStacks still shows the old colors."),
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
    if (actionsLocked()) return;
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
    if (actionsLocked()) return;
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
    if (actionsLocked()) return;
    for (HexColorItem &c : m_hexQmlColors) {
        c.enabled = false;
        c.color.clear();
    }
    doLog(QStringLiteral("All colors reset to default."), QStringLiteral("green"));
    emit themeChanged();
}

int Backend::previewHex()
{
    if (!m_backgroundWorker) {
        const auto response = runBackground([=](Backend &worker) { return QVariantMap{{"count", worker.previewHex()}}; });
        return response.value("count").toInt();
    }
    if (actionsLocked()) return 0;
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
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.applyHex(); });
    if (!m_themeInfo.found || m_themeInfo.mode != QLatin1String("hex"))
        return result(false, QStringLiteral("Load an old (hex) theme first."),
                      QStringLiteral("orange"));

    if (m_themeFileHash != QCryptographicHash::hash(readExe(), QCryptographicHash::Sha256))
        return result(false, QStringLiteral("EXE changed since Load. Reload before applying the legacy palette."), QStringLiteral("red"));
    for (auto &c : m_hexQmlColors) if (c.enabled && !c.color.isEmpty()) {
        const QColor color(c.color);
        if (!color.isValid()) return result(false, QStringLiteral("Invalid color: %1.").arg(c.name), QStringLiteral("red"));
        c.color = color.name(color.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb);
    }
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

QVariantMap Backend::recolorToggleSvgs(int offIndex, int onIndex, const QString &offHex,
                                       const QString &onHex)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.recolorToggleSvgs(offIndex, onIndex, offHex, onHex); });
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
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.recolorTogglesFromPreset(id); });
    const ThemePreset *p = ThemePresets::byId(id);
    if (!p)
        return result(false, QStringLiteral("Unknown preset: %1").arg(id), QStringLiteral("red"));
    if (m_svgs.isEmpty())
        scanSvgs();
    const SvgEngine::ToggleGuess g = SvgEngine::guessToggles(m_svgs);
    if (g.off < 0 && g.on < 0) {
        return result(false,
                      QStringLiteral("No switch/toggle SVG was found in this EXE, so the preset "
                                     "colors were not applied to the switches."),
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
    if (!m_backgroundWorker) {
        const auto response = runBackground([=](Backend &worker) { return QVariantMap{{"count", worker.scanSvgs()}}; });
        return response.value("count").toInt();
    }
    if (actionsLocked()) return 0;
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

// --------------------------------------------------------- embedded resources

void Backend::rebuildResourceInfos(const QByteArray &image)
{
    // Compare only during the explicit scan; keystroke filters never read files.
    QByteArray backupImage; QMap<QString, QByteArray> originalRecords;
    QFile backupFile(m_settings->exePath() + QStringLiteral(".bak"));
    if (backupFile.open(QIODevice::ReadOnly) && backupFile.size() <= 256LL * 1024 * 1024) {
        backupImage = backupFile.readAll();
        for (const auto &e : EmbeddedResources::list(backupImage, true))
            originalRecords[e.path] = QCryptographicHash::hash(backupImage.mid(e.entryOff + 4, e.stored), QCryptographicHash::Sha256);
    }

    QVariantList out;
    out.reserve(m_resources.size());
    // Thumbnails are prepared only during the explicit worker scan. Filtering
    // and scrolling must not call synchronous file/decode functions on the GUI.
    // Bound the metadata cache; very large artwork gets a format badge instead.
    qsizetype thumbnailBytes = 0;
    constexpr qsizetype maxThumbnail = 128 * 1024;
    constexpr qsizetype maxThumbnails = 8 * 1024 * 1024;
    for (int i = 0; i < m_resources.size(); ++i) {
        const EmbeddedResources::Entry &entry = m_resources.at(i);
        QString kind = QStringLiteral("binary");
        switch (entry.kind) {
        case EmbeddedResources::Kind::Image: kind = QStringLiteral("image"); break;
        case EmbeddedResources::Kind::Svg: kind = QStringLiteral("svg"); break;
        case EmbeddedResources::Kind::Text: kind = QStringLiteral("text"); break;
        case EmbeddedResources::Kind::Binary: kind = QStringLiteral("binary"); break;
        }
        QString thumbnail;
        if ((entry.kind == EmbeddedResources::Kind::Image || entry.kind == EmbeddedResources::Kind::Svg)
            && entry.size > 0 && entry.size <= 4 * 1024 * 1024
            && thumbnailBytes < maxThumbnails) {
            const auto bytes = QtResources::readFile(image, { entry.leaf, entry.entryOff, entry.flags },
                                                      4 * 1024 * 1024);
            const auto preview = EmbeddedResources::previewDataUrl(bytes, entry.format, 64);
            const qsizetype cost = preview.size() * qsizetype(sizeof(QChar));
            if (cost <= maxThumbnail && thumbnailBytes + cost <= maxThumbnails) {
                thumbnail = preview;
                thumbnailBytes += cost;
            }
        }
        out << QVariantMap { { "index", i }, { "thumbnail", thumbnail },
                             { "path", entry.path },
                             { "modifiedKnown", originalRecords.contains(entry.path) },
                             { "modified", originalRecords.contains(entry.path) && originalRecords.value(entry.path) != QCryptographicHash::hash(image.mid(entry.entryOff + 4, entry.stored), QCryptographicHash::Sha256) },
                             { "name", entry.leaf },
                             { "folder", entry.folder },
                             { "format", entry.format.toUpper() },
                             { "kind", kind },
                             { "text", entry.text },
                             { "size", entry.size },
                             { "stored", entry.stored },
                             { "compressed", entry.flags == 1 },
                             { "width", entry.width },
                             { "height", entry.height },
                             { "entryOff", entry.entryOff } };
    }
    m_resourceInfos = out;
}

int Backend::scanResources(bool includeBinary)
{
    if (!m_backgroundWorker) {
        const auto response = runBackground(
            [includeBinary](Backend &worker) {
                return QVariantMap { { "count", worker.scanResources(includeBinary) } };
            });
        return response.value("count").toInt();
    }
    if (actionsLocked()) return 0;
    m_resources.clear();
    m_resourcesIncludeBinary = includeBinary;
    const QByteArray data = readExe();
    m_resourceFileHash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
    if (data.isEmpty()) {
        doLog(QStringLiteral("Resources: EXE not found."), QStringLiteral("red"));
    } else {
        m_resources = EmbeddedResources::list(data, includeBinary);
        if (m_resources.isEmpty()) {
            doLog(QStringLiteral("Resources: no Qt resource pool could be read from this file. "
                                 "Check that it is BlueStacks' HD-Player.exe and that the player "
                                 "is not running."), QStringLiteral("red"));
        }
        int text = 0, raster = 0, vector = 0, binary = 0;
        for (const auto &entry : m_resources) {
            if (entry.text) ++text;
            if (entry.kind == EmbeddedResources::Kind::Image) ++raster;
            else if (entry.kind == EmbeddedResources::Kind::Svg) ++vector;
            else if (entry.kind == EmbeddedResources::Kind::Binary) ++binary;
        }
        // Say what was found, not just how many: "21 pictures" and "424 pictures
        // (399 SVG)" are very different answers when a build carries its whole
        // artwork set as vectors.
        doLog(QStringLiteral("Resources: %1 embedded file(s) - %2 editable text, %3 picture(s) "
                             "(%4 raster + %5 SVG), %6 other - in %7")
                  .arg(m_resources.size())
                  .arg(text)
                  .arg(raster + vector)
                  .arg(raster)
                  .arg(vector)
                  .arg(binary)
                  .arg(QFileInfo(m_settings->exePath()).fileName()),
              QStringLiteral("blue"));
    }
    rebuildResourceInfos(data);
    ++m_resourceRevision;
    emit resourcesChanged();
    return m_resources.size();
}

QString Backend::resourcePreviewAt(int index, int maxSide) const
{
    if (index < 0 || index >= m_resources.size()) return {};
    const EmbeddedResources::Entry &entry = m_resources.at(index);
    // Not filtered by the listed kind: a build may carry the picture under a
    // name the extension table does not know (or with no extension at all),
    // and the bytes are a better judge than the file name.
    const QByteArray bytes = EmbeddedResources::readEntry(m_settings->exePath(), entry.entryOff,
                                                          entry.flags);
    if (bytes.isEmpty()) return {};
    const QString dataUrl = EmbeddedResources::previewDataUrl(bytes, entry.format, maxSide);
    if (!dataUrl.isEmpty()) return dataUrl;
    // Second chance through a real file: the SVG plugin sniffs the suffix, and
    // QML rejects a data: URL it cannot type. Same bytes, same element.
    return EmbeddedResources::previewFileUrl(bytes, entry.format);
}

// Why the preview looks the way it does, for the line under it. Empty when the
// entry has a picture (or nothing worth saying), a sentence when it does not.
QString Backend::resourcePreviewNote(int index) const
{
    if (index < 0 || index >= m_resources.size()) return {};
    const EmbeddedResources::Entry &entry = m_resources.at(index);
    const bool picture = entry.kind == EmbeddedResources::Kind::Image
        || entry.kind == EmbeddedResources::Kind::Svg;
    if (!picture && !entry.text) return {};
    const QByteArray bytes = EmbeddedResources::readEntry(m_settings->exePath(), entry.entryOff,
                                                          entry.flags);
    if (bytes.isEmpty()) {
        return QStringLiteral("This entry cannot be read back from the EXE (%1 stored bytes), so "
                              "there is nothing to preview.").arg(entry.stored);
    }
    if (!picture) return {};
    if (!EmbeddedResources::previewDataUrl(bytes, entry.format, 64).isEmpty()
        || !EmbeddedResources::previewFileUrl(bytes, entry.format).isEmpty()) {
        return {};
    }
    return QStringLiteral("No picture preview for this entry: the %1 bytes stored under “%2” are "
                          "not an image this build can decode. The file can still be extracted "
                          "or replaced.").arg(bytes.size()).arg(entry.format);
}

QString Backend::resourceTextAt(int index) const
{
    if (index < 0 || index >= m_resources.size()) return {};
    const EmbeddedResources::Entry &entry = m_resources.at(index);
    if (!entry.text) return {};
    const QByteArray bytes = EmbeddedResources::readEntry(m_settings->exePath(), entry.entryOff,
                                                          entry.flags);
    return EmbeddedResources::decodeText(bytes);
}

QVariantMap Backend::resourceBudgetAt(int index)
{
    if (actionsLocked()) return lockedMap();
    // Measured on the calling thread on purpose: this is one validated lookup
    // for the row the user just selected, and routing it through
    // runBackground() would emit resourcesChanged() and re-enter the page's
    // selection handler (which asks for this measurement again).
    if (index < 0 || index >= m_resources.size())
        return result(false, QStringLiteral("That embedded file is no longer listed; scan again."),
                      QStringLiteral("red"));
    const EmbeddedResources::Entry entry = m_resources.at(index);
    const QByteArray data = readExe();
    if (data.isEmpty())
        return result(false, QStringLiteral("Cannot read the selected EXE."), QStringLiteral("red"));
    const auto room = EmbeddedResources::budget(data, entry.entryOff, entry.leaf, entry.scanned);
    // The editor needs to know whether a UTF-8 BOM is part of the record it
    // has to stay inside.
    bool bom = false;
    if (entry.text) {
        const QByteArray bytes = EmbeddedResources::readEntry(m_settings->exePath(),
                                                              entry.entryOff, entry.flags);
        bom = bytes.startsWith(QByteArray::fromHex("efbbbf"));
    }
    return { { QStringLiteral("ok"), room.known },
             { QStringLiteral("known"), room.known },
             { QStringLiteral("capacity"), room.capacity },
             { QStringLiteral("used"), room.used },
             { QStringLiteral("bom"), bom },
             { QStringLiteral("msg"), room.detail } };
}

int Backend::resourcePayloadSize(int index, const QString &text, bool bom) const
{
    if (index < 0 || index >= m_resources.size()) return -1;
    QByteArray bytes = text.toUtf8();
    if (bom) bytes.prepend(QByteArray::fromHex("efbbbf"));
    return int(EmbeddedResources::payloadSizeFor(bytes, m_resources.at(index).flags));
}

int Backend::resourceTextSize(const QString &text, bool bom) const
{
    return int(text.toUtf8().size()) + (bom ? 3 : 0);
}

QVariantMap Backend::saveResourceText(int index, const QString &text)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.saveResourceText(index, text); });
    if (m_resourceFileHash != QCryptographicHash::hash(readExe(), QCryptographicHash::Sha256))
        return result(false, QStringLiteral("EXE changed since Scan Resources. Scan manually again before replacing anything."), QStringLiteral("red"));
    if (index < 0 || index >= m_resources.size())
        return result(false, QStringLiteral("That embedded file is no longer listed; scan again."),
                      QStringLiteral("red"));
    const QString path = m_resources.at(index).path;
    const auto r = EmbeddedResources::replaceText(m_settings->exePath(), path, text);
    if (r.ok) scanResources(m_resourcesIncludeBinary);
    return result(r.ok, r.msg, r.ok ? QStringLiteral("green") : QStringLiteral("red"));
}

QVariantMap Backend::replaceResourceWithFile(int index, const QString &filePath)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) {
            return worker.replaceResourceWithFile(index, filePath);
        });
    if (m_resourceFileHash != QCryptographicHash::hash(readExe(), QCryptographicHash::Sha256))
        return result(false, QStringLiteral("EXE changed since Scan Resources. Scan manually again before replacing anything."), QStringLiteral("red"));
    if (index < 0 || index >= m_resources.size())
        return result(false, QStringLiteral("That embedded file is no longer listed; scan again."),
                      QStringLiteral("red"));
    const EmbeddedResources::Entry entry = m_resources.at(index);
    EmbeddedResources::Result r;
    if (entry.kind == EmbeddedResources::Kind::Image || entry.kind == EmbeddedResources::Kind::Svg) {
        r = EmbeddedResources::replacePicture(m_settings->exePath(), entry.path, filePath);
    } else {
        QFile in(filePath);
        if (!in.open(QIODevice::ReadOnly))
            return result(false, QStringLiteral("Cannot read the replacement file."),
                          QStringLiteral("red"));
        const QByteArray bytes = in.readAll();
        in.close();
        r = EmbeddedResources::replaceBytes(m_settings->exePath(), entry.path, bytes);
    }
    if (r.ok) scanResources(m_resourcesIncludeBinary);
    return result(r.ok, r.msg, r.ok ? QStringLiteral("green") : QStringLiteral("red"));
}

QVariantMap Backend::extractResource(int index, const QString &targetPath)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.extractResource(index, targetPath); });
    if (index < 0 || index >= m_resources.size())
        return result(false, QStringLiteral("Select an embedded file first."), QStringLiteral("red"));
    const EmbeddedResources::Entry &entry = m_resources.at(index);
    const QByteArray bytes = EmbeddedResources::readEntry(m_settings->exePath(), entry.entryOff,
                                                          entry.flags);
    if (bytes.isEmpty())
        return result(false, QStringLiteral("That entry could not be read from the EXE."),
                      QStringLiteral("red"));
    QFile out(targetPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(bytes) != bytes.size())
        return result(false, QStringLiteral("Could not write %1.").arg(QDir::toNativeSeparators(targetPath)),
                      QStringLiteral("red"));
    out.close();
    return result(true, QStringLiteral("Saved %1 (%2 bytes) to %3")
                           .arg(entry.leaf)
                           .arg(bytes.size())
                           .arg(QDir::toNativeSeparators(targetPath)),
                  QStringLiteral("green"));
}

QVariantMap Backend::extractAllResources()
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.extractAllResources(); });
    if (m_resources.isEmpty())
        return result(false, QStringLiteral("Scan the EXE first; there is no resource list yet."),
                      QStringLiteral("red"));
    const QString root = QFileInfo(m_settings->exePath()).absolutePath()
        + QStringLiteral("/embedded_resources");
    int count = 0;
    QStringList failed;
    for (const auto &entry : m_resources) {
        // Keep the pool's folder layout so two "logo.png" in different folders
        // cannot overwrite each other, but never let a crafted resource name
        // walk out of the extraction folder.
        QStringList segments;
        for (const QString &part : entry.path.split(QLatin1Char('/'), Qt::SkipEmptyParts))
            if (part != QLatin1String(".") && part != QLatin1String("..")
                && !part.contains(QLatin1Char(':')))
                segments << part;
        if (segments.isEmpty()) continue;
        const QString target = root + QLatin1Char('/') + segments.join(QLatin1Char('/'));
        QDir().mkpath(QFileInfo(target).absolutePath());
        const QByteArray bytes = EmbeddedResources::readEntry(m_settings->exePath(), entry.entryOff,
                                                              entry.flags);
        QFile out(target);
        if (bytes.isEmpty() || !out.open(QIODevice::WriteOnly | QIODevice::Truncate)
            || out.write(bytes) != bytes.size()) {
            failed << entry.path;
            continue;
        }
        out.close();
        ++count;
    }
    const QString dir = QDir::toNativeSeparators(root);
    if (count == 0)
        return result(false, QStringLiteral("No embedded file could be written to %1.").arg(dir),
                      QStringLiteral("red"));
    return result(true, QStringLiteral("Extracted %1 embedded file(s) to %2%3")
                           .arg(count)
                           .arg(dir)
                           .arg(failed.isEmpty()
                                    ? QString()
                                    : QStringLiteral(" (%1 could not be read)").arg(failed.size())),
                  QStringLiteral("green"));
}

// ---------------------------------------------------------------------- fps

static QVariantMap analysisToMap(const FpsPatcher::Analysis &a)
{
    QVariantList targets;
    for (const FpsPatcher::Target &t : a.targets)
        targets << QVariantMap { { "name", t.name }, { "cls", t.cls }, { "state", t.state },
                                 { "entryRva", t.entryRva }, { "rva", t.rva }, { "offset", t.off },
                                 { "bytes", QString::fromLatin1(t.entryBytes.toHex(' ')) }, { "detail", t.detail } };
    return { { "ok", a.ok }, { "state", a.state }, { "msg", a.msg }, { "targets", targets } };
}

FpsPatcher::DllDetection Backend::resolveFpsTarget()
{
    if (!QFileInfo(m_settings->exePath()).isFile()) {
        m_installs = BsDetector::findBlueStacks();
        emit installsChanged();
        if (m_installs.size() == 1) useInstall(0);
        else {
            m_settings->setDllPath({});
            FpsPatcher::DllDetection result;
            result.msg = m_installs.isEmpty()
                ? QStringLiteral("No BlueStacks/MSI installation was found in Program Files or the registry. Select HD-Player.exe first.")
                : QStringLiteral("Multiple installations were detected. Choose one with Use on the Dashboard, then auto-detect FPS again.");
            return result;
        }
    }
    auto detection = FpsPatcher::detectDll(m_settings->exePath());
    m_settings->setDllPath(detection.ok ? detection.path : QString());
    return detection;
}

QVariantMap Backend::fpsDetect()
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.fpsDetect(); });
    m_settings->setFpsAutoDetect(true);
    const auto detection = resolveFpsTarget();
    auto reply = result(detection.ok, detection.msg, detection.ok ? QStringLiteral("blue") : QStringLiteral("orange"));
    reply.insert(QStringLiteral("qtMajor"), detection.qtMajor);
    reply.insert(QStringLiteral("playerBits"), detection.playerBits);
    reply.insert(QStringLiteral("path"), detection.path);
    return reply;
}

QVariantMap Backend::fpsAnalyze()
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.fpsAnalyze(); });
    if (m_settings->fpsAutoDetect()) {
        const auto detection = resolveFpsTarget();
        if (!detection.ok) return result(false, detection.msg, QStringLiteral("red"));
        doLog(detection.msg, QStringLiteral("blue"));
    }
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
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.fpsPatch(hi); });
    if (m_settings->fpsAutoDetect()) {
        const auto detection = resolveFpsTarget();
        if (!detection.ok) return result(false, detection.msg, QStringLiteral("red"));
        doLog(detection.msg, QStringLiteral("blue"));
    }
    const auto r = FpsPatcher::patch(m_settings->dllPath(), hi);
    return result(r.ok, r.msg, QStringLiteral("green"));
}

QVariantMap Backend::fpsRestore()
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.fpsRestore(); });
    if (m_settings->fpsAutoDetect()) {
        const auto detection = resolveFpsTarget();
        if (!detection.ok) return result(false, detection.msg, QStringLiteral("red"));
        doLog(detection.msg, QStringLiteral("blue"));
    }
    const auto r = FpsPatcher::restore(m_settings->dllPath());
    return result(r.ok, r.msg, QStringLiteral("green"));
}

// ---------------------------------------------------------------- integrity

QVariantMap Backend::integrityCheck()
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.integrityCheck(); });
    const auto a = IntegrityPatcher::analyze(m_settings->exePath());
    QString color = QStringLiteral("blue");
    if (a.state == QLatin1String("bad") || a.state == QLatin1String("notfound")
        || a.state == QLatin1String("ambig") || a.state == QLatin1String("nofile"))
        color = QStringLiteral("red");
    else if (a.state == QLatin1String("patched") || a.state == QLatin1String("absent"))
        color = QStringLiteral("green");
    auto reply = result(a.ok, a.msg, color);
    reply.insert(QStringLiteral("state"), a.state);
    return reply;
}

QVariantMap Backend::integrityPatch()
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.integrityPatch(); });
    const auto r = IntegrityPatcher::patch(m_settings->exePath());
    auto reply = result(r.ok, r.msg, r.ok ? QStringLiteral("green") : QStringLiteral("red"));
    reply.insert(QStringLiteral("state"), r.ok ? QStringLiteral("patched") : QStringLiteral("error"));
    return reply;
}

QVariantMap Backend::integrityRestore()
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.integrityRestore(); });
    const auto r = IntegrityPatcher::restore(m_settings->exePath());
    auto reply = result(r.ok, r.msg, r.ok ? QStringLiteral("green") : QStringLiteral("red"));
    reply.insert(QStringLiteral("state"), r.ok ? QStringLiteral("clean") : QStringLiteral("error"));
    return reply;
}

// -------------------------------------------------------------- boot video

QVariantMap Backend::bootPatch(int holdMs, const QString &mode, const QString &videoPath, const QString &startupText)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.bootPatch(holdMs, mode, videoPath, startupText); });
    const auto r = BootVideoPatcher::patch(m_settings->exePath(), holdMs, mode, videoPath, startupText);
    return result(r.ok, r.msg, r.ok ? QStringLiteral("green") : QStringLiteral("red"));
}

QVariantMap Backend::bootLogoPreview()
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([](Backend &worker) { return worker.bootLogoPreview(); });
    if (actionsLocked()) return lockedMap();
    const QString logo = BootVideoPatcher::logoPreviewDataUrl(m_settings->exePath());
    return {{QStringLiteral("available"), !logo.isEmpty()},
            {QStringLiteral("logo"), logo}};
}

QVariantMap Backend::purgePlayerCache()
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([](Backend &worker) { return worker.purgePlayerCache(); });
    if (BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe")))
        return result(false, QStringLiteral("Close all HD-Player / BlueStacks instances before purging caches."), QStringLiteral("red"));
    const QStringList roots = { qEnvironmentVariable("LOCALAPPDATA"), qEnvironmentVariable("APPDATA") };
    if (roots[0].isEmpty() && roots[1].isEmpty())
        return result(false, QStringLiteral("Windows AppData locations are unavailable; nothing was deleted."), QStringLiteral("red"));
    const auto r = PlayerCache::purge(roots);
    return result(r.ok, r.msg, r.ok ? QStringLiteral("green") : QStringLiteral("orange"));
}

QVariantMap Backend::gifRpcInspect(const QString &path)
{
    return GifRpc::inspect(path);
}
QVariantMap Backend::gifRpcApply(const QString &path, const QVariantMap &settings, bool replaceExisting)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.gifRpcApply(path, settings, replaceExisting); });
    const auto r = GifRpc::apply(path, settings, replaceExisting);
    doLog(r.value(QStringLiteral("msg")).toString(), r.value(QStringLiteral("ok")).toBool() ? QStringLiteral("green") : QStringLiteral("red"));
    return r;
}
QVariantMap Backend::gifRpcRestore(const QString &path)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.gifRpcRestore(path); });
    const auto r = GifRpc::restore(path);
    doLog(r.value(QStringLiteral("msg")).toString(), r.value(QStringLiteral("ok")).toBool() ? QStringLiteral("green") : QStringLiteral("red"));
    return r;
}

// ------------------------------------------------------------------ extras

// ---------------------------------------------------------------------------
// BlueStacks Debloat
// ---------------------------------------------------------------------------

QVariantList Backend::debloatOptions()
{
    QVariantList list;
    for (const auto &option : Debloat::options())
        list << QVariantMap { { QStringLiteral("id"), option.id },
                              { QStringLiteral("group"), option.group },
                              { QStringLiteral("label"), option.label },
                              { QStringLiteral("hint"), option.hint },
                              { QStringLiteral("needsInstance"), option.needsInstance } };
    return list;
}

QVariantMap Backend::debloatState()
{
    const Debloat::Status state = Debloat::status(m_settings->confPath());
    QVariantList cloud;
    for (const auto &entry : state.cloud) cloud << entry;
    return { { QStringLiteral("supported"), state.supported },
             { QStringLiteral("applied"), state.applied },
             { QStringLiteral("conf"), state.conf },
             { QStringLiteral("detail"), state.detail },
             { QStringLiteral("instances"), state.instances },
             { QStringLiteral("instance"), state.instance },
             { QStringLiteral("already"), state.already },
             { QStringLiteral("hostsInstalled"), state.hostsInstalled },
             { QStringLiteral("cloud"), cloud },
             { QStringLiteral("anyCloudMoved"), state.anyCloudMoved },
             { QStringLiteral("recorded"), state.anyRecord },
             { QStringLiteral("pendingChanges"), state.pendingChanges },
             { QStringLiteral("backups"), Debloat::backups().size() } };
}

QVariantMap Backend::debloatApply(const QStringList &optionIds, const QString &instance)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.debloatApply(optionIds, instance); });
    if (optionIds.isEmpty())
        return result(false, QStringLiteral("Select at least one option."), QStringLiteral("orange"));
    Debloat::Request request;
    request.options = optionIds;
    request.instance = instance;
    const auto reply = Debloat::apply(m_settings->confPath(), m_settings->exePath(), request);
    return result(reply.value(QStringLiteral("ok")).toBool(),
                  reply.value(QStringLiteral("msg")).toString(), QStringLiteral("green"));
}

QVariantMap Backend::debloatRestore()
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.debloatRestore(); });
    const auto reply = Debloat::restore(m_settings->confPath(), m_settings->exePath());
    // The Android apps and guest storage are part of the same undo record. If
    // the instance is not running, its part says so and keeps its record; the
    // configuration restore above is never held back by that.
    const QVariantMap android = Android::isEmpty(Android::loadRecord())
        ? QVariantMap()
        : Android::restore(m_settings->exePath(), m_settings->confPath(), QString());
    QString message = reply.value(QStringLiteral("msg")).toString();
    if (!android.isEmpty()) {
        message += QLatin1Char(' ') + android.value(QStringLiteral("msg")).toString();
        const bool ok = reply.value(QStringLiteral("ok")).toBool()
            && android.value(QStringLiteral("ok")).toBool();
        return result(ok, message, ok ? QStringLiteral("green") : QStringLiteral("red"));
    }
    return result(reply.value(QStringLiteral("ok")).toBool(), message, QStringLiteral("green"));
}

// ---------------------------------------------------------------------------
// Android — the inside of the instance, through its own ADB
// ---------------------------------------------------------------------------

QVariantMap Backend::androidState()
{
    return Android::state();
}

QVariantMap Backend::androidScan(const QString &instance)
{
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.androidScan(instance); });
    return Android::scan(m_settings->exePath(), m_settings->confPath(), instance);
}

QVariantMap Backend::androidApply(const QStringList &packages, bool wipeDownloads, bool keepCopy,
                                  bool clearAppData, const QString &instance)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) {
            return worker.androidApply(packages, wipeDownloads, keepCopy, clearAppData, instance);
        });
    Android::Request request;
    request.packages = packages;
    request.wipeDownloads = wipeDownloads;
    request.keepCopy = keepCopy;
    request.clearAppData = clearAppData;
    return Android::apply(m_settings->exePath(), m_settings->confPath(), instance, request);
}

// ---------------------------------------------------------------------------
// the hardcode debloater (Root.vhd)
// ---------------------------------------------------------------------------

QString Backend::rootvhdFind()
{
    return RootVhd::findImage(m_settings->exePath());
}

QVariantMap Backend::rootvhdOpenEngineArchive(const QString &archive)
{
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) {
            return worker.rootvhdOpenEngineArchive(archive);
        });
    return RootVhd::extractEngineArchive(archive);
}

QVariantMap Backend::rootvhdInfo(const QString &image)
{
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.rootvhdInfo(image); });
    return RootVhd::info(image);
}

QVariantMap Backend::rootvhdEnableWrite(const QString &image)
{
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.rootvhdEnableWrite(image); });
    return RootVhd::enableWriteAccess(image);
}

QVariantMap Backend::rootvhdList(const QString &image, const QString &innerPath)
{
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.rootvhdList(image, innerPath); });
    return RootVhd::listing(image, innerPath);
}

QVariantMap Backend::rootvhdSearch(const QString &image, const QString &query)
{
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.rootvhdSearch(image, query); });
    return RootVhd::search(image, query);
}

QVariantMap Backend::rootvhdExtract(const QString &image, const QString &innerPath,
                                    const QString &localPath)
{
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) {
            return worker.rootvhdExtract(image, innerPath, localPath);
        });
    return RootVhd::extract(image, innerPath, localPath);
}

QVariantMap Backend::rootvhdAdd(const QString &image, const QString &innerDir,
                                const QString &localFile)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) {
            return worker.rootvhdAdd(image, innerDir, localFile);
        });
    return RootVhd::addFile(image, innerDir, localFile);
}

QVariantMap Backend::rootvhdReplace(const QString &image, const QString &innerPath,
                                    const QString &localFile)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) {
            return worker.rootvhdReplace(image, innerPath, localFile);
        });
    return RootVhd::replaceFile(image, innerPath, localFile);
}

QVariantMap Backend::rootvhdAddFolder(const QString &image, const QString &innerDir,
                                      const QString &localDir)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) {
            return worker.rootvhdAddFolder(image, innerDir, localDir);
        });
    return RootVhd::addFolder(image, innerDir, localDir);
}

QVariantMap Backend::rootvhdAddPath(const QString &image, const QString &innerDir,
                                    const QString &localPath)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) {
            return worker.rootvhdAddPath(image, innerDir, localPath);
        });
    return RootVhd::addPath(image, innerDir, localPath);
}

QVariantMap Backend::rootvhdMkdir(const QString &image, const QString &innerDir, const QString &name)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.rootvhdMkdir(image, innerDir, name); });
    return RootVhd::makeFolder(image, innerDir, name);
}

QVariantMap Backend::rootvhdRename(const QString &image, const QString &innerPath,
                                   const QString &newName)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) {
            return worker.rootvhdRename(image, innerPath, newName);
        });
    return RootVhd::rename(image, innerPath, newName);
}

QVariantMap Backend::rootvhdMove(const QString &image, const QString &innerPath,
                                 const QString &destinationDir)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) {
            return worker.rootvhdMove(image, innerPath, destinationDir);
        });
    return RootVhd::move(image, innerPath, destinationDir);
}

QVariantMap Backend::rootvhdCopy(const QString &image, const QString &innerPath,
                                 const QString &destinationDir, const QString &newName)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) {
            return worker.rootvhdCopy(image, innerPath, destinationDir, newName);
        });
    return RootVhd::copy(image, innerPath, destinationDir, newName);
}

QVariantMap Backend::rootvhdRemove(const QString &image, const QString &innerPath)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.rootvhdRemove(image, innerPath); });
    return RootVhd::remove(image, innerPath);
}

QVariantMap Backend::rootvhdUndo(const QString &image)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.rootvhdUndo(image); });
    return RootVhd::undo(image);
}

QVariantMap Backend::rootvhdDiscardUndo(const QString &image)
{
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.rootvhdDiscardUndo(image); });
    return RootVhd::discardUndo(image);
}

QStringList Backend::rootvhdEngineFolders()
{
    return RootVhd::engineFolders(m_settings->exePath());
}

QVariantMap Backend::androidRestore(const QString &instance)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.androidRestore(instance); });
    return Android::restore(m_settings->exePath(), m_settings->confPath(), instance);
}

QVariantMap Backend::applyRemovals(const QStringList &names)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.applyRemovals(names); });
    if (names.isEmpty())
        return result(false, QStringLiteral("Select at least one item."),
                      QStringLiteral("orange"));
    ThemeEngine::backup(m_settings->exePath());
    const auto r = ExtrasPatcher::applyRemovals(m_settings->exePath(), names);
    return result(r.ok, r.msg, QStringLiteral("green"));
}

QVariantList Backend::layoutTweaks()
{
    if (actionsLocked()) return {};
    if (!m_backgroundWorker)
        return runBackground([](Backend &worker) { return QVariantMap{{QStringLiteral("rows"), worker.layoutTweaks()}}; }).value(QStringLiteral("rows")).toList();
    QVariantList out;
    QByteArray image;
    const QString path = m_settings->exePath();
    if (!path.isEmpty() && QFileInfo::exists(path)) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            image = f.readAll();
            f.close();
        }
    }
    for (const auto &t : LayoutPatcher::tweaks()) {
        // The "responsive sidebar / aligned panels" helper is installed
        // automatically when moving the buttons to the title bar and is
        // restored when that layout is removed. It is not offered as a
        // separate UI option.
        if (t.id == QStringLiteral("responsive-sidebar"))
            continue;
        const auto state = LayoutPatcher::analyze(image, t.id);
        out << QVariantMap {
            { QStringLiteral("id"), t.id },
            { QStringLiteral("label"), t.label },
            { QStringLiteral("detail"), t.detail },
            { QStringLiteral("supported"), state.supported },
            { QStringLiteral("applied"), state.applied },
            { QStringLiteral("needsUpdate"), state.needsUpdate },
            { QStringLiteral("state"), state.detail },
        };
    }
    return out;
}

QVariantMap Backend::sidebarButtonRows()
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([](Backend &worker) { return worker.sidebarButtonRows(); });
    const auto state = LayoutPatcher::buttonRows(readExe());
    QVariantList buttons;
    for (const auto &button : state.buttons) {
        buttons << QVariantMap {
            { QStringLiteral("key"), button.key },
            { QStringLiteral("label"), button.label.isEmpty() ? button.key : button.label },
            { QStringLiteral("hidden"), button.hidden },
            { QStringLiteral("canHide"), button.canHide },
        };
    }
    return QVariantMap {
        { QStringLiteral("supported"), state.supported },
        { QStringLiteral("applied"), state.applied },
        { QStringLiteral("detail"), state.detail },
        { QStringLiteral("buttons"), buttons },
    };
}

QVariantMap Backend::applySidebarOrder(const QStringList &order, const QStringList &hidden)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.applySidebarOrder(order, hidden); });
    const auto r = LayoutPatcher::applyOrder(m_settings->exePath(), order, hidden);
    return result(r.ok, r.msg, r.ok ? QStringLiteral("green") : QStringLiteral("red"));
}

QVariantMap Backend::applyLayoutTweak(const QString &id, bool enabled)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.applyLayoutTweak(id, enabled); });
    const auto r = LayoutPatcher::apply(m_settings->exePath(), id, enabled);
    return result(r.ok, r.msg, r.ok ? QStringLiteral("green") : QStringLiteral("red"));
}

QVariantMap Backend::aboutPageState()
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([](Backend &worker) { return worker.aboutPageState(); });
    QVariantMap map {
        { QStringLiteral("supported"), false },
        { QStringLiteral("applied"), false },
        { QStringLiteral("detail"), QStringLiteral("Select the HD-Player.exe path first.") },
        { QStringLiteral("text"), QString() },
        { QStringLiteral("links"), QVariantList() },
        { QStringLiteral("showSections"), true },
        { QStringLiteral("sectionsHidden"), false },
        { QStringLiteral("source"), QString() },
        { QStringLiteral("scanned"), false },
        // The words the compiled copy of the page prints - what the window
        // actually shows. Shown in the About card so the editor can prove it
        // found the page that is on screen.
        { QStringLiteral("compiledPage"), false },
        { QStringLiteral("strings"), QVariantList() },
        // The longest sentence the compiled page can hold (0 = no compiled
        // page, so no limit to enforce here).
        { QStringLiteral("textMax"), 0 },
    };
    const QString path = m_settings->exePath();
    if (path.isEmpty() || !QFileInfo::exists(path)) return map;
    // A player that keeps its QML out of HD-Player.exe (a small launcher plus a
    // DLL or .rcc beside it) has the page in that other file. The card reads
    // whichever file carries it, so it does not report "not embedded" on a
    // build the tool can edit.
    const QString host = AboutPatcher::findPageHost(path);
    map[QStringLiteral("hostFile")] = host;
    map[QStringLiteral("hostName")] = host.isEmpty() ? QString() : QFileInfo(host).fileName();
    if (host.isEmpty()) {
        map[QStringLiteral("detail")] =
            QStringLiteral("%1 does not carry the About page, and no file next to it does either. "
                           "Select the player's own HD-Player.exe, or run Diagnose on this one.")
                .arg(QFileInfo(path).fileName());
        return map;
    }
    QFile file(host);
    if (!file.open(QIODevice::ReadOnly)) {
        map[QStringLiteral("detail")] = QStringLiteral("Cannot read the selected EXE.");
        return map;
    }
    const auto state = AboutPatcher::analyze(file.readAll());
    file.close();
    map[QStringLiteral("supported")] = state.supported;
    map[QStringLiteral("applied")] = state.applied;
    map[QStringLiteral("detail")] = state.detail;
    map[QStringLiteral("text")] = state.config.text;
    map[QStringLiteral("showSections")] = state.config.showSections;
    map[QStringLiteral("sectionsHidden")] = state.sectionsHidden;
    map[QStringLiteral("scanned")] = state.scanned;
    // The editor shows what it read. Handing the whole page over would copy
    // ~14 KB into QML for a hint; this is enough to tell a readable page from a
    // mis-decoded one (and to copy out when a build reports something odd).
    map[QStringLiteral("source")] = state.source.left(2000);
    map[QStringLiteral("compiledPage")] = state.compiledPage;
    map[QStringLiteral("textMax")] = state.textMax;
    if (QFileInfo(host).absoluteFilePath() != QFileInfo(path).absoluteFilePath())
        map[QStringLiteral("detail")] =
            QStringLiteral("%1 (the page lives in %2 next to it, and that is the file this edits; "
                           "%3 itself carries none).")
                .arg(state.detail, QFileInfo(host).fileName(), QFileInfo(path).fileName());
    QVariantList strings;
    for (const auto &entry : state.table)
        strings << QVariantMap {
            { QStringLiteral("index"), entry.index },
            { QStringLiteral("text"), entry.text },
            { QStringLiteral("max"), entry.maxChars },
        };
    map[QStringLiteral("strings")] = strings;
    QVariantList links;
    for (const auto &link : state.config.links) {
        links << QVariantMap {
            { QStringLiteral("id"), link.id },
            { QStringLiteral("name"), link.name },
            { QStringLiteral("url"), link.url },
            { QStringLiteral("label"), link.label },
            { QStringLiteral("hidden"), link.hidden },
            // The page's records do not grow, so the editor stops at the room
            // each field really has instead of failing when Apply is pressed.
            { QStringLiteral("nameMax"), link.nameMax },
            { QStringLiteral("labelMax"), link.labelMax },
            { QStringLiteral("urlMax"), link.urlMax },
        };
    }
    map[QStringLiteral("links")] = links;
    return map;
}

QVariantMap Backend::applyAboutPage(const QString &text, const QVariantList &links,
                                    bool showSections, bool enabled)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) {
            return worker.applyAboutPage(text, links, showSections, enabled);
        });
    AboutPatcher::Config config;
    config.text = text;
    config.showSections = showSections;
    for (const auto &value : links) {
        const auto map = value.toMap();
        AboutPatcher::Link link;
        link.id = map.value(QStringLiteral("id")).toString();
        link.name = map.value(QStringLiteral("name")).toString();
        link.url = map.value(QStringLiteral("url")).toString();
        link.label = map.value(QStringLiteral("label")).toString();
        link.hidden = map.value(QStringLiteral("hidden")).toBool();
        if (!link.id.isEmpty()) config.links << link;
    }
    const auto r = AboutPatcher::apply(m_settings->exePath(), enabled, config);
    return result(r.ok, r.msg, r.ok ? QStringLiteral("green") : QStringLiteral("red"));
}

QString Backend::aboutPageReport()
{
    const QString path = m_settings->exePath();
    if (path.isEmpty() || !QFileInfo::exists(path))
        return QStringLiteral("Select the HD-Player.exe path first.");
    return AboutPatcher::summary(path);
}

QVariantMap Backend::settingsPanelRebuildState()
{
    const auto state = SettingsPanelPatcher::analyze(m_settings->exePath());
    return {{QStringLiteral("supported"), state.supported},
            {QStringLiteral("applied"), state.applied},
            {QStringLiteral("restorable"), state.restorable},
            {QStringLiteral("compiledUnitsDetected"), state.compiledUnitsDetected},
            {QStringLiteral("compiledUnitsActive"), state.compiledUnitsActive},
            {QStringLiteral("settingsPageCount"), state.settingsPageCount},
            {QStringLiteral("settingsQmlResourceCount"), state.settingsQmlResourceCount},
            {QStringLiteral("embeddedQmlResourceCount"), state.embeddedQmlResourceCount},
            {QStringLiteral("qmlResourceSamples"), state.qmlResourceSamples},
            {QStringLiteral("tabs"), state.tabs},
            {QStringLiteral("logoFallback"), state.logoFallback},
            {QStringLiteral("logoStatus"), state.logoStatus},
            {QStringLiteral("detail"), state.detail}};
}

QVariantMap Backend::applySettingsPanelRebuild(bool enabled)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.applySettingsPanelRebuild(enabled); });
    QVariantMap panelProfile = m_settings->settingsPanelProfile();
    panelProfile.insert(QStringLiteral("tagline"), m_settings->aboutTagline());
    const auto response = SettingsPanelPatcher::apply(m_settings->exePath(),
        m_settings->aboutDeveloperName(), m_settings->aboutLinks(), panelProfile, enabled);
    if (response.value(QStringLiteral("ok")).toBool()) emit resourcesChanged();
    return result(response.value(QStringLiteral("ok")).toBool(),
                  response.value(QStringLiteral("msg")).toString(),
                  response.value(QStringLiteral("ok")).toBool() ? QStringLiteral("green") : QStringLiteral("red"));
}

QVariantMap Backend::restoreBackup(const QString &path)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.restoreBackup(path); });
    const QString bak = path + QStringLiteral(".bak");
    if (!QFileInfo::exists(bak))
        return result(false, QStringLiteral("No backup found."), QStringLiteral("red"));
    QFile::remove(path);
    if (!QFile::copy(bak, path))
        return result(false, QStringLiteral("Restore failed."), QStringLiteral("red"));
    return result(true, QStringLiteral("Restored from backup."), QStringLiteral("green"));
}

QVariantMap Backend::rpcidFind(const QString &path, bool noNetwork)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.rpcidFind(path, noNetwork); });
    Q_UNUSED(noNetwork)
    const auto r = RpcIdPatcher::find(path);
    doLog(r.msg, r.ok ? QStringLiteral("blue") : QStringLiteral("red"));
    return {{QStringLiteral("ok"), r.ok}, {QStringLiteral("msg"), r.msg}};
}

QVariantMap Backend::rpcidReplace(const QString &path, const QString &newId,
                                  const QString &oldId, bool dryRun, bool noNetwork)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.rpcidReplace(path, newId, oldId, dryRun, noNetwork); });
    Q_UNUSED(noNetwork)
    const QStringList oldIds = oldId.trimmed().isEmpty() ? QStringList{} : QStringList{oldId.trimmed()};
    const auto r = RpcIdPatcher::replace(path, newId, oldIds, dryRun);
    doLog(r.msg, r.ok ? QStringLiteral("green") : QStringLiteral("red"));
    return {{QStringLiteral("ok"), r.ok}, {QStringLiteral("msg"), r.msg}};
}

QVariantMap Backend::rpcidStatus(const QString &path)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.rpcidStatus(path); });
    const auto s = RpcIdPatcher::status(path);
    QVariantMap info{{QStringLiteral("ok"), s.ok},
                     {QStringLiteral("patched"), s.patched},
                     {QStringLiteral("currentId"), s.currentId},
                     {QStringLiteral("originalId"), s.originalId},
                     {QStringLiteral("name"), QString()},
                     {QStringLiteral("known"), s.known},
                     {QStringLiteral("hasBackup"), s.hasBackup},
                     {QStringLiteral("leftoverCount"), s.leftovers.size()},
                     {QStringLiteral("msg"), s.msg}};
    return info;
}

QVariantMap Backend::rpcidUndo(const QString &path)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.rpcidUndo(path); });
    const auto r = RpcIdPatcher::undo(path);
    doLog(r.msg, r.ok ? QStringLiteral("green") : QStringLiteral("red"));
    return {{QStringLiteral("ok"), r.ok}, {QStringLiteral("msg"), r.msg}};
}

QVariantMap Backend::rpcidNames(const QString &path)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.rpcidNames(path); });
    const auto n = RpcIdPatcher::names(path);
    QVariantList entries;
    for (const RpcIdPatcher::NameEntry &entry : n.entries) {
        entries << QVariantMap{{QStringLiteral("key"), entry.key},
                               {QStringLiteral("value"), entry.value},
                               {QStringLiteral("max"), entry.max}};
    }
    return {{QStringLiteral("ok"), n.ok},
            {QStringLiteral("msg"), n.msg},
            {QStringLiteral("entries"), entries}};
}

QVariantMap Backend::rpcidSetName(const QString &path, const QString &text,
                                  const QString &key, bool dryRun)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker)
        return runBackground([=](Backend &worker) { return worker.rpcidSetName(path, text, key, dryRun); });
    const auto r = RpcIdPatcher::setName(path, text, key, dryRun);
    doLog(r.msg, r.ok ? QStringLiteral("green") : QStringLiteral("red"));
    return {{QStringLiteral("ok"), r.ok}, {QStringLiteral("msg"), r.msg}};
}

QVariantList Backend::removalNames() const
{
    QVariantList out;
    for (const QString &n : ExtrasPatcher::removalNames())
        out << n;
    return out;
}

// Explicit user-triggered tools. No inspection or resource scan is performed
// by opening the Workspace page or importing a profile.
QVariantMap Backend::workspace(const QString &action, const QVariantMap &options)
{
    if (actionsLocked()) return lockedMap();
    if (!m_backgroundWorker) return runBackground([=](Backend &w) { return w.workspace(action, options); });
    namespace W = CustomizationWorkspace;
    const QString path = m_settings->exePath();
    QVariantMap edits; for (auto it = m_edits.cbegin(); it != m_edits.cend(); ++it) edits[it.key()] = it.value();
    QVariantMap r;
    if (action == QLatin1String("inspect")) r = W::inspect(path, edits);
    else if (action == QLatin1String("skin-icon-load")) r = SkinProfiles::readIcon(options.value("file").toString());
    else if (action == QLatin1String("skin-save")) r = SkinProfiles::save(options.value("file").toString(), options.value("profile").toMap());
    else if (action == QLatin1String("skin-load")) r = SkinProfiles::read(options.value("file").toString());
    else if (action == QLatin1String("skin-plan")) {
        const auto bytes=readExe();
        if(m_resourceFileHash!=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256))
            return {{"ok",false},{"msg","Scan Resources manually for this exact EXE first."}};
        QStringList paths;for(const auto &v:m_resourceInfos)paths<<v.toMap().value("path").toString();
        r=PlayerSkinCompiler::plan(bytes,options.value("profile").toMap(),paths);
        r.remove("operations"); // inspection never queues or exports raw source
    }
    else if (action == QLatin1String("queue-skin")) {
        const auto profile = options.value("profile").toMap();
        r = SkinProfiles::validate(profile);
        if (!r.value("ok").toBool()) return r;
        if (!m_workspaceOperations.isEmpty()) return {{"ok",false},{"msg","Review or clear your existing Workspace batch first. Nothing was discarded."}};
        const QByteArray bytes = readExe();
        const auto *base = AddonPacks::find(profile.value("base").toString());
        const auto *preset = ThemePresets::byId(QLatin1String(base->preset));
        const auto info = ThemeEngine::detectTheme(bytes, QStringLiteral("qml"));
        const auto entries = ThemeEngine::parseQmlEntries(info.qml);
        const auto analysis = PlayerAppearancePatcher::analyze(bytes);
        if (entries.isEmpty() || !analysis.ok) return {{"ok",false},{"msg","This player does not expose verified palette and design sources. No skin was staged."}};
        auto colors = ThemePresets::matchEntries(*preset,entries);
        const auto overrides = profile.value("palette").toMap();
        QSet<QString> matched;
        for (const auto &entry : entries) {
            const QString normalized = ThemePresets::normalize(entry.name);
            if (normalized == QLatin1String("accentnormal")) colors[entry.name]=profile.value("accent").toString();
            if (normalized == QLatin1String("primary100")) colors[entry.name]=profile.value("surface").toString();
            if (overrides.contains(entry.name)) { colors[entry.name]=overrides.value(entry.name).toString(); matched.insert(entry.name); }
        }
        if (matched.size()!=overrides.size()) return {{"ok",false},{"msg","Some palette override names are not present in this player. Inspect and correct them; no edits were discarded."}};
        AddonPacks::Pack custom = *base;
        custom.radius=profile.value("radius").toInt(); custom.height=profile.value("height").toInt(); custom.padding=profile.value("padding").toInt();
        auto design = AddonPacks::designEdits(custom,analysis);
        const auto tokens=profile.value("tokens").toMap();
        for (auto it=tokens.cbegin();it!=tokens.cend();++it) design[it.key()]=it.value();
        const auto validation = PlayerAppearancePatcher::validateEdits(bytes,design);
        if (!validation.value("ok").toBool()) return validation;
        QVariantMap palette;for(auto it=colors.cbegin();it!=colors.cend();++it) palette[it.key()]=it.value();
        QVariantList pending{QVariantMap{{"kind","theme"},{"pack",profile.value("base")},{"name",profile.value("name")},{"edits",palette},{"design",design}}};
        QVariantList coverage;
        if(profile.value("playerUi",false).toBool()) {
            if(m_resourceFileHash!=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256))
                return {{"ok",false},{"msg","Scan Resources manually before staging the player GUI components and icons."}};
            QStringList paths;for(const auto &v:m_resourceInfos)paths<<v.toMap().value("path").toString();
            const auto gui=PlayerSkinCompiler::plan(bytes,profile,paths);
            if(!gui.value("ok").toBool())return gui;
            pending.append(gui.value("operations").toList());coverage=gui.value("coverage").toList();
        }
        const auto icons=profile.value("icons").toList();
        if (!icons.isEmpty() && m_resourceFileHash != QCryptographicHash::hash(bytes,QCryptographicHash::Sha256))
            return {{"ok",false},{"msg","Scan Resources manually for this exact EXE before staging mapped icons."}};
        for (const auto &v:icons) {
            const auto mapping=v.toMap();const QString resource=mapping.value("path").toString();
            int found=0;for(const auto &row:m_resourceInfos) if(row.toMap().value("path").toString()==resource) ++found;
            if(found!=1) return {{"ok",false},{"msg","An icon target is missing or ambiguous in your manual scan. No pack was queued."}};
            const auto svg=SkinProfiles::mappedIcon(mapping,profile.value("iconStyle").toString(),profile.value("accent").toString());
            // Explicit user mappings override generated icon-family defaults.
            for(int i=pending.size()-1;i>=0;--i)if(pending[i].toMap().value("path").toString()==resource)pending.removeAt(i);
            pending << QVariantMap{{"kind","resource"},{"path",resource},{"data",QString::fromLatin1(svg.toBase64())},{"sha256",W::digest(svg)}};
        }
        if(pending.size()>128)return {{"ok",false},{"msg","Pack exceeds the 128-operation limit. No edits were queued."}};
        m_workspaceOperations=pending;m_workspaceHash=W::digest(bytes);
        r={{"ok",true},{"coverage",coverage},{"msg",profile.value("playerUi",false).toBool()
            ? "Player palette, geometry, rounded QML controls, interaction/popup animations and icon families staged together. Dry Run is required. Native/Android windows, raster art, external DLLs/RCCs and boot remain unchanged."
            : "Palette, numeric tokens and mapped icons staged. Player GUI deployment is off; motion is preview-only."}};
    }
    else if (action == QLatin1String("queue-addon")) {
        // A pack is an isolated, fixed catalog operation. Never discard the user's batch.
        const auto *pack = AddonPacks::find(options.value("id").toString());
        const auto *preset = pack ? ThemePresets::byId(QLatin1String(pack->preset)) : nullptr;
        const QByteArray bytes = readExe();
        const auto theme = ThemeEngine::detectTheme(bytes, QStringLiteral("qml"));
        const auto entries = ThemeEngine::parseQmlEntries(theme.qml);
        const auto colors = preset ? ThemePresets::matchEntries(*preset, entries) : QMap<QString, QString>();
        const auto analysis = PlayerAppearancePatcher::analyze(bytes);
        if (!pack || !preset) r = {{"ok", false}, {"msg", "Unknown add-on pack."}};
        else if (!m_workspaceOperations.isEmpty()) r = {{"ok", false}, {"msg", "Review or clear your existing Workspace batch before staging an add-on. Nothing was discarded."}};
        else if (colors.isEmpty() || !analysis.ok || AddonPacks::designEdits(*pack, analysis).isEmpty())
            r = {{"ok", false}, {"msg", "This player does not expose both a supported QML palette and cataloged button design tokens. No pack was staged. Use Color studio for palette-only targets."}};
        else {
            QVariantMap matched;
            for (auto it = colors.cbegin(); it != colors.cend(); ++it) matched[it.key()] = it.value();
            m_workspaceOperations << QVariantMap{{"kind", "theme"}, {"pack", options.value("id")}, {"edits", matched}};
            m_workspaceHash = W::digest(bytes);
            r = {{"ok", true}, {"msg", "Pack palette and button geometry staged. Run Dry Run, then review Apply. Player animations, icons, boot and native dialogs are not changed."}};
        }
    }
    else if (action == QLatin1String("appearance-inspect")) r = W::inspectAppearance(path);
    else if (action == QLatin1String("theme-dry-run")) {
        const auto bytes = readExe();
        if (m_themeFileHash != QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) || edits.isEmpty()) r = {{"ok", false}, {"msg", "Load the current theme and stage edits first."}};
        else r = W::simulate(path, {QVariantMap{{"kind", "theme"}, {"edits", edits}}}, W::digest(bytes));
    }
    else if (action == QLatin1String("diagnostics")) r = W::exportDiagnostics(path, options.value("file").toString());
    else if (action == QLatin1String("profile-load")) {
        r = W::readProfile(options.value("file").toString());
        if (r.value("ok").toBool()) {
            m_customizationProfile = r.value("profile").toMap();
            const auto wanted = m_customizationProfile.value("theme").toMap();
            m_edits.clear(); for (auto &e : m_hexQmlColors) { e.enabled = false; e.color.clear(); }
            QStringList missing;
            for (auto it = wanted.cbegin(); it != wanted.cend(); ++it) {
                bool present = false;
                for (const auto &e : m_themeEntries) if (e.name == it.key()) { m_edits[it.key()] = it.value().toString(); present = true; }
                for (auto &e : m_hexQmlColors) if (e.name == it.key()) { e.color = it.value().toString(); e.enabled = true; present = true; }
                if (!present) missing << it.key();
            }
            const QString hash = m_customizationProfile.value("sourceSha256").toString();
            if (!hash.isEmpty() && hash != W::digest(readExe())) r["msg"] = r.value("msg").toString() + QStringLiteral(" Source EXE differs: settings are portable, but old offsets are never imported.");
            if (!missing.isEmpty()) r["msg"] = r.value("msg").toString() + QStringLiteral(" Unmatched colors (load the target theme first): ") + missing.join(QStringLiteral(", "));
            m_workspaceOperations.clear(); m_workspaceHash.clear();
        }
    } else if (action == QLatin1String("profile-save") || action == QLatin1String("profile-section")) {
        auto candidate = m_customizationProfile;
        if (action == QLatin1String("profile-section")) candidate[options.value("section").toString()] = options.value("value");
        else {
            if (m_themeMethod == QLatin1String("hex")) for (const auto &e : m_hexQmlColors) if (e.enabled && !e.color.isEmpty()) edits[e.name] = e.color;
            candidate["theme"] = edits;
            const auto bytes = readExe();
            if (!bytes.isEmpty()) candidate["sourceSha256"] = W::digest(bytes); else candidate.remove("sourceSha256");
            candidate["name"] = options.value("name", candidate.value("name"));
        }
        r = W::validateProfile(candidate);
        if (r.value("ok").toBool() && action == QLatin1String("profile-save")) r = W::saveProfile(options.value("file").toString(), candidate);
        if (r.value("ok").toBool()) m_customizationProfile = candidate;
    } else if (action == QLatin1String("clear")) {
        m_workspaceOperations.clear(); m_workspaceHash.clear(); r = {{"ok", true}, {"msg", "Batch cleared; no files changed."}};
    } else if (action == QLatin1String("queue-theme") || action == QLatin1String("queue-resource") || action == QLatin1String("queue-appearance")) {
        const QByteArray current = readExe(); const QString hash = W::digest(current);
        if (current.isEmpty() || (!m_workspaceHash.isEmpty() && m_workspaceHash != hash)) r = {{"ok", false}, {"msg", "EXE changed. Clear the old batch, reload and inspect again."}};
        else if (m_workspaceOperations.size() >= 32) r = {{"ok", false}, {"msg", "Batch limit: 32 operations."}};
        else {
            QVariantMap operation;
            if (action == QLatin1String("queue-theme")) {
                if (edits.isEmpty() || m_themeFileHash != QCryptographicHash::hash(current, QCryptographicHash::Sha256)) r = {{"ok", false}, {"msg", "Load the current QML theme and stage valid color edits first."}};
                else operation = {{"kind", "theme"}, {"edits", edits}};
            } else if (action == QLatin1String("queue-appearance")) {
                r = W::stageAppearance(path, options.value("edits").toMap());
                if (r.value("ok").toBool()) operation = r.value("operation").toMap();
            } else {
                if (m_resourceFileHash != QCryptographicHash::hash(current, QCryptographicHash::Sha256))
                    return result(false, QStringLiteral("Scan Resources manually for the current EXE before staging replacements."), QStringLiteral("red"));
                r = W::stageResource(options.value("path").toString(), options.value("file").toString());
                if (r.value("ok").toBool()) operation = r.value("operation").toMap();
            }
            if (!operation.isEmpty()) {
                qint64 encoded = operation.value("data").toString().size();
                for (const auto &v : m_workspaceOperations) encoded += v.toMap().value("data").toString().size();
                if (encoded > 90LL * 1024 * 1024) return result(false, QStringLiteral("Batch replacement data exceeds the memory budget."), QStringLiteral("red"));
                m_workspaceOperations << operation; m_workspaceHash = hash;
                r["ok"] = true; r["msg"] = QStringLiteral("Operation queued. Dry Run will detect conflicts and verify the complete batch.");
            }
        }
    } else if (action == QLatin1String("dry-run")) r = W::simulate(path, m_workspaceOperations, m_workspaceHash);
    else if (action == QLatin1String("apply")) {
        r = W::apply(path, m_workspaceOperations, m_workspaceHash, options.value("outputSha256").toString());
        if (r.value("ok").toBool()) { m_workspaceOperations.clear(); m_workspaceHash.clear(); m_resources.clear(); m_resourceInfos.clear(); ++m_resourceRevision; reloadTheme(); }
    } else if (action == QLatin1String("history")) r = {{"ok", true}, {"entries", W::history()}, {"msg", "Showing the latest 200 workspace recovery journals. Legacy page writes retain their existing .bak behavior."}};
    else if (action == QLatin1String("restore")) {
        r = W::restore(path, options.value("id").toString());
        if (r.value("ok").toBool()) { m_workspaceOperations.clear(); m_workspaceHash.clear(); m_resources.clear(); m_resourceInfos.clear(); ++m_resourceRevision; reloadTheme(); }
    } else if (action == QLatin1String("image")) r = W::prepareImage(options.value("source").toString(), options.value("file").toString(), options.value("width").toInt(), options.value("height").toInt(), options.value("crop").toBool(), options.value("quality", 95).toInt());
    else if (action == QLatin1String("video")) r = W::inspectVideo(options.value("file").toString());
    else if (action == QLatin1String("rpc")) r = W::rpcDiagnostics(path, m_customizationProfile.value("gifRpc").toMap());
    else r = {{"ok", false}, {"msg", "Unknown workspace action."}};
    doLog(r.value("msg").toString(), r.value("ok").toBool() ? QStringLiteral("blue") : QStringLiteral("red"));
    return r;
}
