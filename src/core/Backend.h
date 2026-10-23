#pragma once

#include <QObject>
#include <functional>
#include <QVariantList>
#include <QVariantMap>
#include <QUrl>

#include "FpsPatcher.h"
#include "GradientPatcher.h"
#include "SvgEngine.h"
#include "ThemeEngine.h"

class SettingsManager;
class AuthManager;

/**
 * Backend — the single QML-facing facade for all patcher engines.
 * Every operation logs through the log() signal (msg + color) which the
 * UI's log panel renders; nothing in the application shells out to another
 * program, interpreter or helper script to do its work.
 */
class Backend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString exePath READ exePath WRITE setExePath NOTIFY exePathChanged)
    Q_PROPERTY(QString dllPath READ dllPath WRITE setDllPath NOTIFY dllPathChanged)
    Q_PROPERTY(QString confPath READ confPath WRITE setConfPath NOTIFY confPathChanged)

    Q_PROPERTY(bool themeFound READ themeFound NOTIFY themeChanged)
    Q_PROPERTY(bool qmlThemeAvailable READ qmlThemeAvailable NOTIFY themeChanged)
    Q_PROPERTY(QString themeMode READ themeMode NOTIFY themeChanged)
    Q_PROPERTY(QString resolvedThemeMethod READ resolvedThemeMethod NOTIFY themeChanged)
    Q_PROPERTY(QString themeDetection READ themeDetection NOTIFY themeChanged)
    Q_PROPERTY(int detectedQtMajor READ detectedQtMajor NOTIFY themeChanged)
    Q_PROPERTY(QString themeStatus READ themeStatus NOTIFY themeChanged)
    Q_PROPERTY(bool themeStale READ themeStale NOTIFY themeChanged)
    Q_PROPERTY(QVariantList themeEntries READ themeEntries NOTIFY themeChanged)
    Q_PROPERTY(QVariantList hexColors READ hexColors NOTIFY themeChanged)
    Q_PROPERTY(QVariantList themePresets READ themePresets CONSTANT)
    Q_PROPERTY(QVariantList gradientTargets READ gradientTargets NOTIFY gradientTargetsChanged)

    Q_PROPERTY(QVariantList svgInfos READ svgInfos NOTIFY svgsChanged)
    Q_PROPERTY(QVariantList installs READ installs NOTIFY installsChanged)
    Q_PROPERTY(QVariantList removalNames READ removalNames CONSTANT)

public:
    explicit Backend(SettingsManager *settings, QObject *parent = nullptr);

    bool busy() const { return m_busy; }

    // ---- sign-in gate (the Premium-Optimizer rule: the renderer is not the
    //      gate) ----
    // While an AuthManager is attached and the Discord sign-in has not been
    // verified, every action below refuses to run. Tests and the CLI build
    // Backend without an AuthManager and stay ungated.
    void setAuthManager(AuthManager *auth);

    QString exePath() const;
    void setExePath(const QString &p);
    QString dllPath() const;
    void setDllPath(const QString &p);
    QString confPath() const;
    void setConfPath(const QString &p);

    bool themeFound() const { return m_themeInfo.found; }
    // Qt 6 players use the QML (new) method. Older Qt 5 / MSI 5 builds hide it
    // and use Hex (old); both expose the same per-surface gradient editor for
    // every gradient-capable surface the catalog finds.
    bool qmlThemeAvailable() const { return m_qmlThemeAvailable; }
    QString themeMode() const { return m_themeInfo.mode; }
    // The method the last loadTheme() ended up with: after an "auto" load this
    // is the auto-detected "qml" or "hex"; before any load it is just the last
    // request ("auto" initially).
    QString resolvedThemeMethod() const { return m_themeMethod; }
    // One human-readable line describing how the method above was arrived at,
    // e.g. "Auto-detected a Qt 5 player → using Hex (old)."
    QString themeDetection() const { return m_themeDetection.isEmpty() ? QStringLiteral("Pick a method, then load the theme - Auto detects the player automatically.") : m_themeDetection; }
    // Qt generation the auto-detect found in the EXE's imports: 0 = unknown, 5 or 6.
    int detectedQtMajor() const { return m_detectedQtMajor; }
    QString themeStatus() const { return m_themeStatus; }
    // True when HD-Player.exe changed on disk after the theme was loaded, i.e.
    // the color list on screen no longer describes the file.
    bool themeStale() const;
    QVariantList themeEntries() const;
    QVariantList hexColors() const;
    // [{id, name, desc, light, toggleOff, toggleOn, colorCount, preview}]
    QVariantList themePresets() const;
    QVariantList gradientTargets() const;

    QVariantList svgInfos() const;
    QVariantList installs() const { return m_installs; }
    QVariantList removalNames() const;

    // ---- detection ----
    Q_INVOKABLE QVariantList detectInstalls();
    Q_INVOKABLE QString localFilePath(const QUrl &url) const;
    Q_INVOKABLE void useInstall(int index);
    Q_INVOKABLE QVariantList detectInstances();

    // ---- theme ----
    Q_INVOKABLE void loadTheme(const QString &method); // "qml" | "hex" | "auto"
    Q_INVOKABLE void reloadTheme();                    // re-load with the last method
    Q_INVOKABLE bool themeFileChanged() const;         // EXE touched since Load?
    Q_INVOKABLE void setThemeEdit(const QString &name, const QString &value);
    Q_INVOKABLE void clearThemeEdits();
    Q_INVOKABLE QVariantMap patchThemeQml();
    Q_INVOKABLE QVariantMap loadGradientTargets();
    Q_INVOKABLE QVariantMap applyThemeGradient(const QString &key, const QString &first, const QString &second,
                                              bool horizontal, bool enabled);
    // Hex (old) gradient editor: key is one of the catalog gradient targets —
    // with the Hex (old) method every listed surface is paintable, not only
    // the top bar / title bar — or GradientPatcher::allGradientKey() to paint
    // them all in one go.
    Q_INVOKABLE QVariantMap applyHexGradient(const QString &key, const QString &first, const QString &second,
                                            bool horizontal, bool enabled);
    // Stage a whole palette as edits (still needs PATCH THEME).  Returns how
    // many colors the preset covers in this EXE.
    Q_INVOKABLE int applyPreset(const QString &id);
    // Same presets for the Hex (old) method: fills the New value column of
    // every legacy palette color the EXE carries (still needs APPLY THEME).
    // Returns how many rows were staged.
    Q_INVOKABLE int applyHexPreset(const QString &id);
    Q_INVOKABLE QString presetToggleOff(const QString &id) const;
    Q_INVOKABLE QString presetToggleOn(const QString &id) const;

    Q_INVOKABLE void setHexColor(const QString &name, const QString &hex);
    Q_INVOKABLE void setHexEnabled(const QString &name, bool on);
    Q_INVOKABLE void resetHexColors();
    Q_INVOKABLE int previewHex();
    Q_INVOKABLE QVariantMap applyHex();

    // ---- svg ----
    Q_INVOKABLE int scanSvgs();
    Q_INVOKABLE QString svgTextAt(int index) const;
    Q_INVOKABLE QVariantMap extractSvgs();
    Q_INVOKABLE QVariantMap replaceSvg(int index, const QString &newSvgPath);
    // Switch/toggle SVG colors (BS_THEME_CHANGER's PatchToggleSvgs).
    // guessToggleSvgs -> { off, on, offColor, onColor, offPreview, onPreview }:
    // blob indexes (-1 = not recognised), the track color already in each blob
    // (6-digit hex, the knob's white/black is skipped) and a short text snippet
    // of each blob for a status line.
    Q_INVOKABLE QVariantMap guessToggleSvgs();
    Q_INVOKABLE QVariantMap recolorToggleSvgs(int offIndex, int onIndex, const QString &offHex,
                                              const QString &onHex);
    Q_INVOKABLE QVariantMap recolorTogglesFromPreset(const QString &id);

    // ---- fps ----
    Q_INVOKABLE QVariantMap fpsDetect();
    Q_INVOKABLE QVariantMap fpsAnalyze();
    Q_INVOKABLE QVariantMap fpsPatch(double hi);
    Q_INVOKABLE QVariantMap fpsRestore();

    // ---- integrity ----
    Q_INVOKABLE QVariantMap integrityCheck();
    Q_INVOKABLE QVariantMap integrityPatch();
    Q_INVOKABLE QVariantMap integrityRestore();

    // ---- boot video ----
    Q_INVOKABLE QVariantMap bootPatch();

    // ---- extras ----
    Q_INVOKABLE QVariantMap applyRemovals(const QStringList &names);
    Q_INVOKABLE QVariantList layoutTweaks();
    Q_INVOKABLE QVariantMap applyLayoutTweak(const QString &id, bool enabled);
    Q_INVOKABLE QVariantMap aboutPageState();
    Q_INVOKABLE QVariantMap applyAboutPage(const QString &text, const QVariantList &links,
                                           bool showSections, bool enabled);
    Q_INVOKABLE QVariantMap restoreBackup(const QString &path);

    // ---- Discord Rich Presence ID utility ----
    Q_INVOKABLE QVariantMap rpcidFind(const QString &path, bool noNetwork = true);
    Q_INVOKABLE QVariantMap rpcidReplace(const QString &path, const QString &newId,
                                         const QString &oldId = QString(),
                                         bool dryRun = false, bool noNetwork = true);
    Q_INVOKABLE QVariantMap rpcidStatus(const QString &path);
    Q_INVOKABLE QVariantMap rpcidUndo(const QString &path);
    // Windows version-resource names (what Explorer/Task Manager show).
    Q_INVOKABLE QVariantMap rpcidNames(const QString &path);
    Q_INVOKABLE QVariantMap rpcidSetName(const QString &path, const QString &text,
                                         const QString &key = QString(),
                                         bool dryRun = false);

signals:
    void busyChanged();
    void exePathChanged();
    void dllPathChanged();
    void confPathChanged();
    void themeChanged();
    void gradientTargetsChanged();
    void svgsChanged();
    void installsChanged();
    void log(const QString &message, const QString &color);

private:
    QVariantMap runBackground(const std::function<QVariantMap(Backend &)> &operation);
    void copyPatchState(const Backend &source);
    bool m_busy = false;
    bool m_backgroundWorker = false;
    bool actionsLocked() const;
    QVariantMap lockedMap() const;
    FpsPatcher::DllDetection resolveFpsTarget();
    void doLog(const QString &msg, const QString &color);
    QVariantMap result(bool ok, const QString &msg, const QString &color);
    QByteArray readExe();
    void noteThemeFileState();     // remember the EXE mtime/size we loaded from

    AuthManager *m_auth = nullptr;
    SettingsManager *m_settings;
    ThemeInfo m_themeInfo;
    QString m_themeStatus;
    QString m_themeMethod = QStringLiteral("auto");
    QString m_themeDetection;
    int m_detectedQtMajor = 0;
    bool m_qmlThemeAvailable = false;
    QString m_themeFilePath;
    qint64 m_themeFileMsecs = -1;  // EXE lastModified when the theme was loaded
    qint64 m_themeFileBytes = -1;  // EXE size when the theme was loaded
    QList<QmlEntry> m_themeEntries;
    QList<GradientPatcher::Target> m_gradientTargets;
    QMap<QString, QString> m_edits;
    QList<HexColorItem> m_hexQmlColors;

    QList<SvgBlob> m_svgs;
    QVariantList m_installs;
};
