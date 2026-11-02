#pragma once

#include <QObject>
#include <functional>
#include <QVariantList>
#include <QVariantMap>
#include <QUrl>

#include "EmbeddedResources.h"
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
    Q_PROPERTY(QVariantMap customizationProfile READ customizationProfile NOTIFY customizationProfileChanged)
    Q_PROPERTY(QVariantList workspaceOperations READ workspaceOperations NOTIFY workspaceChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString exePath READ exePath WRITE setExePath NOTIFY exePathChanged)
    Q_PROPERTY(QString dllPath READ dllPath WRITE setDllPath NOTIFY dllPathChanged)
    Q_PROPERTY(QString confPath READ confPath WRITE setConfPath NOTIFY confPathChanged)

    Q_PROPERTY(bool themeWritable READ themeWritable NOTIFY themeChanged)
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
    // Every file in the player's Qt resource pools (QML, SVG, pictures, caches).
    Q_PROPERTY(QVariantList resourceInfos READ resourceInfos NOTIFY resourcesChanged)
    Q_PROPERTY(QVariantList installs READ installs NOTIFY installsChanged)
    Q_PROPERTY(QVariantList removalNames READ removalNames CONSTANT)

public:
    explicit Backend(SettingsManager *settings, QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    QVariantMap customizationProfile() const { return m_customizationProfile; }
    QVariantList workspaceOperations() const { return m_workspaceOperations; }
    Q_INVOKABLE QVariantMap workspace(const QString &action, const QVariantMap &options);

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

    bool themeWritable() const { return m_themeInfo.found && !m_themeInfo.ambiguous && (m_themeInfo.mode != QLatin1String("zlib") || m_themeInfo.hasQrcNode || m_themeInfo.headerBytes != 8); }
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
    QVariantList resourceInfos() const { return m_resourceInfos; }
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
    // Switch/toggle SVG colors (BS_THEME_CHANGER's PatchToggleSvgs). The blob
    // indexes are found by scanSvgs(); the standalone SVG tab is gone, so the
    // Theme page's preset button is the only caller.
    Q_INVOKABLE QVariantMap recolorToggleSvgs(int offIndex, int onIndex, const QString &offHex,
                                              const QString &onHex);
    Q_INVOKABLE QVariantMap recolorTogglesFromPreset(const QString &id);

    // ---- embedded resources (every file in the Qt resource pools) ----
    // One list for QML, SVG, pictures and the rest. Text entries are shown as
    // code and edited in place; pictures are previewed and replaced. A write is
    // bounded by encoded storage capacity, and the EXE never grows by
    // a single byte: an oversized payload is rebuilt in free space the file
    // already has, or refused.
    Q_INVOKABLE int scanResources(bool includeBinary);
    Q_INVOKABLE QString resourcePreviewAt(int index, int maxSide) const;
    // Empty when the preview is a picture; a sentence when it is not.
    Q_INVOKABLE QString resourcePreviewNote(int index) const;
    Q_INVOKABLE QString resourceTextAt(int index) const;
    Q_INVOKABLE QVariantMap resourceBudgetAt(int index);
    // The payload size `text` would take in that entry (compressed when the
    // entry is compressed), so the editor can show the budget before saving.
    // No EXE read: the entry's compression flag and BOM are all it needs.
    Q_INVOKABLE int resourcePayloadSize(int index, const QString &text, bool bom) const;
    // Decoded UTF-8 size, BOM included. Storage capacity is measured separately;
    // a larger decoded resource is permitted when verified payload storage fits.
    Q_INVOKABLE int resourceTextSize(const QString &text, bool bom) const;
    Q_INVOKABLE QVariantMap saveResourceText(int index, const QString &text);
    Q_INVOKABLE QVariantMap replaceResourceWithFile(int index, const QString &filePath);
    Q_INVOKABLE QVariantMap extractResource(int index, const QString &targetPath);
    Q_INVOKABLE QVariantMap extractAllResources();

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
    Q_INVOKABLE QVariantMap bootPatch(int holdMs = 3000, const QString &mode = QString(), const QString &videoPath = QString());
    Q_INVOKABLE QVariantMap bootLogoPreview();
    Q_INVOKABLE QVariantMap purgePlayerCache();
    Q_INVOKABLE QVariantMap gifRpcInspect(const QString &path);
    Q_INVOKABLE QVariantMap gifRpcApply(const QString &path, const QVariantMap &settings, bool replaceExisting = false);
    Q_INVOKABLE QVariantMap gifRpcRestore(const QString &path);

    // ---- extras ----
    Q_INVOKABLE QVariantMap applyRemovals(const QStringList &names);
    Q_INVOKABLE QVariantList layoutTweaks();
    Q_INVOKABLE QVariantMap applyLayoutTweak(const QString &id, bool enabled);
    // The buttons of the title-bar row, in display order, and the editor that
    // moves them.
    Q_INVOKABLE QVariantMap sidebarButtonRows();
    Q_INVOKABLE QVariantMap applySidebarOrder(const QStringList &order, const QStringList &hidden);
    Q_INVOKABLE QVariantMap aboutPageState();
    Q_INVOKABLE QVariantMap applyAboutPage(const QString &text, const QVariantList &links,
                                           bool showSections, bool enabled);
    // What the About editor sees, in a few lines: what was located, what the
    // page prints now and why it is refused when it is.
    Q_INVOKABLE QString aboutPageReport();
    Q_INVOKABLE QVariantMap settingsPanelRebuildState();
    Q_INVOKABLE QVariantMap applySettingsPanelRebuild(bool enabled = true);
    Q_INVOKABLE QVariantMap restoreBackup(const QString &path);

    // ---- BlueStacks Debloat (config, hosts, cloud companions) ----
    // The switch list with this installation's current state, the instance
    // names bluestacks.conf declares, and the cloud folders that were found.
    Q_INVOKABLE QVariantMap debloatState();
    Q_INVOKABLE QVariantList debloatOptions();
    Q_INVOKABLE QVariantMap debloatApply(const QStringList &optionIds, const QString &instance);
    Q_INVOKABLE QVariantMap debloatRestore();

    // ---- Android, inside the running instance (ADB) ----
    // The reviewed preinstalled-app list and the guest storage as they are now;
    // scan() and the two write calls need the instance to be running.
    Q_INVOKABLE QVariantMap androidState();
    Q_INVOKABLE QVariantMap androidScan(const QString &instance);
    Q_INVOKABLE QVariantMap androidApply(const QStringList &packages, bool wipeDownloads,
                                         bool keepCopy, bool clearAppData,
                                         const QString &instance);
    Q_INVOKABLE QVariantMap androidRestore(const QString &instance);

    // ---- the hardcode debloater: Root.vhd as a file manager ----
    // Every write is journalled first, so undo() puts the image back exactly.
    Q_INVOKABLE QString rootvhdFind();
    Q_INVOKABLE QVariantMap rootvhdOpenEngineArchive(const QString &archive);
    Q_INVOKABLE QVariantMap rootvhdInfo(const QString &image);
    Q_INVOKABLE QVariantMap rootvhdEnableWrite(const QString &image);
    Q_INVOKABLE QVariantMap rootvhdList(const QString &image, const QString &innerPath);
    Q_INVOKABLE QVariantMap rootvhdSearch(const QString &image, const QString &query);
    Q_INVOKABLE QVariantMap rootvhdExtract(const QString &image, const QString &innerPath,
                                           const QString &localPath);
    Q_INVOKABLE QVariantMap rootvhdAdd(const QString &image, const QString &innerDir,
                                       const QString &localFile);
    Q_INVOKABLE QVariantMap rootvhdReplace(const QString &image, const QString &innerPath,
                                           const QString &localFile);
    Q_INVOKABLE QVariantMap rootvhdAddFolder(const QString &image, const QString &innerDir,
                                             const QString &localDir);
    // A file or a whole folder tree, whichever the path turns out to be.
    Q_INVOKABLE QVariantMap rootvhdAddPath(const QString &image, const QString &innerDir,
                                           const QString &localPath);
    Q_INVOKABLE QVariantMap rootvhdMkdir(const QString &image, const QString &innerDir,
                                         const QString &name);
    Q_INVOKABLE QVariantMap rootvhdRename(const QString &image, const QString &innerPath,
                                          const QString &newName);
    Q_INVOKABLE QVariantMap rootvhdMove(const QString &image, const QString &innerPath,
                                        const QString &destinationDir);
    Q_INVOKABLE QVariantMap rootvhdCopy(const QString &image, const QString &innerPath,
                                        const QString &destinationDir, const QString &newName);
    Q_INVOKABLE QVariantMap rootvhdRemove(const QString &image, const QString &innerPath);
    Q_INVOKABLE QVariantMap rootvhdUndo(const QString &image);
    Q_INVOKABLE QVariantMap rootvhdDiscardUndo(const QString &image);
    // Engine folders found for this installation, so the page can say where
    // Root.vhd would be before the user picks one.
    Q_INVOKABLE QStringList rootvhdEngineFolders();

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
    void customizationProfileChanged();
    void workspaceChanged();
    void busyChanged();
    void exePathChanged();
    void dllPathChanged();
    void confPathChanged();
    void themeChanged();
    void gradientTargetsChanged();
    void svgsChanged();
    void resourcesChanged();
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

    QVariantMap m_customizationProfile;
    QVariantList m_workspaceOperations;
    QString m_workspaceHash;
    QByteArray m_themeFileHash;
    QByteArray m_resourceFileHash;
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
    QList<EmbeddedResources::Entry> m_resources;
    // Built once by the scan worker, not by each QML property access/filter.
    QVariantList m_resourceInfos;
    quint64 m_resourceRevision = 0;
    void rebuildResourceInfos(const QByteArray &image);
    bool m_resourcesIncludeBinary = true; // the list on screen keeps binary entries
    QVariantList m_installs;
};
