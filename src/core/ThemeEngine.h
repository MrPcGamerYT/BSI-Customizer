#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <functional>

// ---------------------------------------------------------------------------
// ThemeEngine — C++ port of the Python tool's theme changer.
//
// Three storage formats are auto-detected in HD-Player.exe:
//   * zlib : new builds — UiThemeObject.qml stored as a compressed Qt
//            resource ([be32 storedSize][be32 usize][zlib stream]).
//            Patch in place when the recompressed QML fits the old slot
//            (including trailing zero slack); otherwise RELOCATE the record
//            into a mapped zero cave and re-point the qresource_struct node.
//   * plain: uncompressed QML text (ASCII or UTF-16LE) edited in place.
//   * hex  : old builds — literal "#RRGGBB" values (ascii / utf-16le) plus
//            camelCase QML color names ("accentNormal" etc.) with a hex
//            value right after; every occurrence is replaced in place.
// ---------------------------------------------------------------------------

struct QmlEntry
{
    QString name;
    QString value;
    QString original;
    bool quoted = false;
    int exprStart = 0;
    int exprLen = 0;
};

struct HexColorItem
{
    QString name;
    QString currentHex;
    QString color;    // new value ("#RRGGBB"), empty = unchanged
    bool enabled = false;
};

struct ThemeInfo
{
    bool found = false;
    QString mode;                 // "zlib" | "plain" | "hex"
    QString qml;                  // zlib / plain: the QML text
    QString profile;              // hex: "MSI5" | "NXT"

    // zlib
    qint64 entryOff = 0;          // file offset of [storedSize][usize]
    qint64 zoff = 0;              // file offset of the zlib stream
    quint32 storedSize = 0;       // as stored in the header
    quint32 compSize = 0;         // validated compressed length
    quint32 uncompSize = 0;
    bool sizeIncludesUSize = true;

    // plain
    QString enc;                  // "utf-16-le" | "utf-8"
    int mult = 1;                 // bytes per char
    qint64 winStart = 0;
    int winLen = 0;

    // qrc relocation (zlib)
    bool hasQrcNode = false;
    bool qrcStrict = false;       // node matched the strict qrc geometry test
    int qrcSupport = 0;           // neighbouring tree entries back qrcDataBaseRaw
    qint64 qrcNodeOff = -1;
    qint64 qrcNameRecordOff = -1;
    qint64 qrcNameBaseRaw = -1;
    qint64 qrcDataBaseRaw = -1;
    qint64 qrcDataBaseRva = -1;
    quint32 qrcDataOffset = 0;

    int entryCount = 0;
    // 2 = looks exactly like UiThemeObject.qml (or the qrc tree proves it),
    // 1 = a compressed QML blob with several color properties (fallback tier),
    // 0 = not a theme.
    int contentQuality = 0;
    int candidateCount = 0;       // zlib: equally good theme records in the file
    int hexMatchCount = 0;        // hex: how many default targets matched
};

class ThemeEngine
{
public:
    struct PatchResult
    {
        bool ok = false;
        QString msg;
    };
    using Logger = std::function<void(const QString &msg, const QString &color)>;

    // ---- detection ----
    static ThemeInfo detectTheme(const QByteArray &data,
                                 const QString &method = QStringLiteral("auto"),
                                 const QString &hexProfile = QString());
    static ThemeInfo findThemeZlib(const QByteArray &data);
    static ThemeInfo findThemePlain(const QByteArray &data);
    static ThemeInfo findThemeHex(const QByteArray &data, const QString &profile = QString());

    // ---- QML parse / build ----
    static QList<QmlEntry> parseQmlEntries(const QString &qml);
    // allowGrow: the zlib record is recompressed/relocated, so a hex color may
    // be longer than the expression it replaces.  Anything that could not be
    // applied is appended to *skipped instead of being dropped silently.
    static QString buildQml(const QString &qml,
                            const QList<QmlEntry> &entries,
                            const QMap<QString, QString> &edits,
                            bool allowGrow = false,
                            QStringList *skipped = nullptr);

    // ---- patching ----
    static PatchResult patchTheme(const QString &exePath,
                                  const ThemeInfo &info,
                                  const QList<QmlEntry> &entries,
                                  const QMap<QString, QString> &edits,
                                  Logger logger = {});

    static PatchResult applyHexTheme(const QString &exePath,
                                     const QString &profile,
                                     const QList<HexColorItem> &colors,
                                     const QList<HexColorItem> &qmlColors,
                                     Logger logger = {});

    // ---- hex helpers (exposed for preview + self-test) ----
    struct Replacement
    {
        qint64 pos = 0;
        int len = 0;
        QByteArray rep;
    };
    static QList<Replacement> collectThemeReplacements(const QByteArray &data,
                                                       const QString &profile,
                                                       const QList<HexColorItem> &colors,
                                                       const QList<HexColorItem> &qmlColors);
    static QString findQmlColor(const QByteArray &data, const QString &name);
    static QList<QPair<QByteArray, QString>> oldPatterns(const QString &hexValue,
                                                         const QString &mode);

    // ---- tables ----
    static QStringList qmlColorNames();
    static QString purpose(const QString &name);          // "what it controls"
    static QMap<QString, QPair<QString, QString>> hexTargets(const QString &profile);
    static QString detectProfileFromPath(const QString &exePath);

    // ---- backup ----
    static bool backup(const QString &path, Logger logger = {});
};
