#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include "FpsPatcher.h"
#include "SvgEngine.h"
#include "ThemeEngine.h"

class SettingsManager;

/**
 * Backend — the single QML-facing facade for all patcher engines.
 * Every operation logs through the log() signal (msg + color) which the
 * UI's log panel renders, exactly like the Python tool's LOG hook.
 */
class Backend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString exePath READ exePath WRITE setExePath NOTIFY exePathChanged)
    Q_PROPERTY(QString dllPath READ dllPath WRITE setDllPath NOTIFY dllPathChanged)
    Q_PROPERTY(QString confPath READ confPath WRITE setConfPath NOTIFY confPathChanged)

    Q_PROPERTY(bool themeFound READ themeFound NOTIFY themeChanged)
    Q_PROPERTY(QString themeMode READ themeMode NOTIFY themeChanged)
    Q_PROPERTY(QString themeStatus READ themeStatus NOTIFY themeChanged)
    Q_PROPERTY(bool themeStale READ themeStale NOTIFY themeChanged)
    Q_PROPERTY(QVariantList themeEntries READ themeEntries NOTIFY themeChanged)
    Q_PROPERTY(QVariantList hexColors READ hexColors NOTIFY themeChanged)
    Q_PROPERTY(QVariantList themePresets READ themePresets CONSTANT)

    Q_PROPERTY(QVariantList svgInfos READ svgInfos NOTIFY svgsChanged)
    Q_PROPERTY(QVariantList installs READ installs NOTIFY installsChanged)
    Q_PROPERTY(QVariantList versions READ versions NOTIFY versionsChanged)
    Q_PROPERTY(QVariantList removalNames READ removalNames CONSTANT)

public:
    explicit Backend(SettingsManager *settings, QObject *parent = nullptr);

    QString exePath() const;
    void setExePath(const QString &p);
    QString dllPath() const;
    void setDllPath(const QString &p);
    QString confPath() const;
    void setConfPath(const QString &p);

    bool themeFound() const { return m_themeInfo.found; }
    QString themeMode() const { return m_themeInfo.mode; }
    QString themeStatus() const { return m_themeStatus; }
    // True when HD-Player.exe changed on disk after the theme was loaded, i.e.
    // the color list on screen no longer describes the file.
    bool themeStale() const;
    QVariantList themeEntries() const;
    QVariantList hexColors() const;
    // [{id, name, desc, light, toggleOff, toggleOn, colorCount, preview}]
    QVariantList themePresets() const;

    QVariantList svgInfos() const;
    QVariantList installs() const { return m_installs; }
    QVariantList versions() const { return m_versions; }
    QVariantList removalNames() const;

    // ---- detection ----
    Q_INVOKABLE QVariantList detectInstalls();
    Q_INVOKABLE void useInstall(int index);
    Q_INVOKABLE QVariantList detectInstances();

    // ---- theme ----
    Q_INVOKABLE void loadTheme(const QString &method); // "qml" | "hex"
    Q_INVOKABLE void reloadTheme();                    // re-load with the last method
    Q_INVOKABLE bool themeFileChanged() const;         // EXE touched since Load?
    Q_INVOKABLE void setThemeEdit(const QString &name, const QString &value);
    Q_INVOKABLE void clearThemeEdits();
    Q_INVOKABLE QVariantMap patchThemeQml();
    // Stage a whole palette as edits (still needs PATCH THEME).  Returns how
    // many colors the preset covers in this EXE.
    Q_INVOKABLE int applyPreset(const QString &id);
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
    Q_INVOKABLE QVariantList findVersions();
    Q_INVOKABLE QVariantMap applyVersion(const QString &oldV, const QString &newV);
    Q_INVOKABLE QVariantMap applyRemovals(const QStringList &names);
    Q_INVOKABLE QVariantMap restoreBackup(const QString &path);

signals:
    void exePathChanged();
    void dllPathChanged();
    void confPathChanged();
    void themeChanged();
    void svgsChanged();
    void installsChanged();
    void versionsChanged();
    void log(const QString &message, const QString &color);

private:
    void doLog(const QString &msg, const QString &color);
    QVariantMap result(bool ok, const QString &msg, const QString &color);
    QByteArray readExe();
    void noteThemeFileState();     // remember the EXE mtime/size we loaded from

    SettingsManager *m_settings;
    ThemeInfo m_themeInfo;
    QString m_themeStatus;
    QString m_themeMethod = QStringLiteral("qml");
    QString m_themeFilePath;
    qint64 m_themeFileMsecs = -1;  // EXE lastModified when the theme was loaded
    qint64 m_themeFileBytes = -1;  // EXE size when the theme was loaded
    QList<QmlEntry> m_themeEntries;
    QMap<QString, QString> m_edits;
    QList<HexColorItem> m_hexQmlColors;

    QList<SvgBlob> m_svgs;
    QVariantList m_installs;
    QVariantList m_versions;
};
