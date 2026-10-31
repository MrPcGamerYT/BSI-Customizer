// ---------------------------------------------------------------------------
// Self-test for BSI Customizer (run with: BSI-Customizer --selftest)
//
// Exercises the REAL shipping engines against synthetic BlueStacks-like
// files (fake PE with Qt resources, fake DLL export table, plain QML, hex
// color strings), plus the app's own QML theme-switching chain.
//
// Exit code 0 = everything passed, 1 = at least one failure.
// The GitHub Actions release workflow runs this before packaging.
// ---------------------------------------------------------------------------

#include <QColor>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdio>

#include "core/Backend.h"
#include "core/AuthManager.h"
#include "core/BootVideoPatcher.h"
#include "core/ByteUtils.h"
#include "core/ExtrasPatcher.h"
#include "core/FpsPatcher.h"
#include "core/IntegrityPatcher.h"
#include "core/SessionStore.h"
#include "core/SettingsManager.h"
#include "core/SvgEngine.h"
#include "core/SysInfo.h"
#include "core/ThemeEngine.h"
#include "core/ThemeManager.h"
#include "core/ThemePresets.h"
#include "core/ZlibUtils.h"

using namespace ByteUtils;

int runPatchRegressionTests();

namespace {

int g_failures = 0;

void check(bool cond, const char *what)
{
    if (cond) {
        std::printf("  PASS  %s\n", what);
    } else {
        std::printf("  FAIL  %s\n", what);
        ++g_failures;
        // Windows CI runs expose the job log only as annotations, so surface
        // each failed assertion as one (same as patch_regression_test.cpp).
        if (qEnvironmentVariableIsSet("GITHUB_ACTIONS"))
            std::printf("::error file=src/selftest.cpp::self-test failure: %s\n", what);
    }
}

QByteArray utf16le(const QString &s)
{
    QByteArray out;
    for (const QChar c : s) {
        out.append(char(c.unicode() & 0xFF));
        out.append(char((c.unicode() >> 8) & 0xFF));
    }
    return out;
}

QByteArray utf16be(const QString &s)
{
    QByteArray out;
    for (const QChar c : s) {
        out.append(char((c.unicode() >> 8) & 0xFF));
        out.append(char(c.unicode() & 0xFF));
    }
    return out;
}

double readDoubleLE(const QByteArray &b, qsizetype off)
{
    QDataStream ds(b.mid(off, 8));
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.setFloatingPointPrecision(QDataStream::DoublePrecision);
    double v = 0;
    ds >> v;
    return v;
}

// --------------------------------------------------------------------------
// Synthetic file builders
// --------------------------------------------------------------------------

// Minimal PE32+ with one identity-mapped .text section (VA == RawPtr).
QByteArray makeFakePe(quint32 exportRva, quint32 sectionSize)
{
    QByteArray d;
    d.resize(0x1000 + qsizetype(sectionSize));
    d.fill('\0');
    d[0] = 'M';
    d[1] = 'Z';
    putLe32(d, 0x3C, 0x80);

    d.replace(0x80, 4, QByteArray("PE\0\0", 4));
    putLe16(d, 0x84, 0x8664);    // machine: x64
    putLe16(d, 0x86, 1);         // one section
    putLe16(d, 0x94, 0xF0);      // optional header size

    const quint32 opt = 0x80 + 24;
    putLe16(d, opt, 0x20B);      // PE32+ magic
    if (exportRva)
        putLe32(d, opt + 112, exportRva);

    const quint32 sec = 0x80 + 24 + 0xF0;
    d.replace(sec, 8, QByteArray(".text\0\0\0", 8));
    putLe32(d, sec + 8, sectionSize);  // VSz
    putLe32(d, sec + 12, 0x1000);      // VA  (identity mapped)
    putLe32(d, sec + 16, sectionSize); // RawSize
    putLe32(d, sec + 20, 0x1000);      // RawPtr
    putLe32(d, sec + 36, 0xE0000060);  // characteristics
    return d;
}

// Qt resource data record: [be32 comp+4][be32 usize][zlib stream]
QByteArray qtZlibRecord(const QByteArray &raw)
{
    const QByteArray comp = ZlibUtils::compress(raw);
    QByteArray rec(8, '\0');
    putBe32(rec, 0, quint32(comp.size()) + 4);
    putBe32(rec, 4, quint32(raw.size()));
    return rec + comp;
}

// Highly-compressible UiThemeObject.qml with 50 color entries.
QString makeThemeQml(const QStringList &colors)
{
    static const QStringList names = [] {
        QStringList n;
        for (int i = 100; i >= 10; i -= 10)
            n << QStringLiteral("primary%1").arg(i);
        n << "accentNormal" << "accentHover" << "accentClick" << "accentClickTransparent"
          << "secondaryNormal" << "secondaryHover" << "secondaryClick"
          << "tertiaryNormal" << "tertiaryHover" << "tertiaryClick"
          << "alternateNormal" << "alternateHover" << "alternateClick"
          << "dangerNormal" << "dangerHover" << "dangerClick" << "dangerDark"
          << "notice" << "success" << "warning" << "error"
          << "overlayPrimary" << "overlayBlack" << "overlayGray" << "overlayBright"
          << "cloudGradientPrimary" << "cloudGradientSecondary"
          << "notificationGradientPrimary" << "notificationGradientSecondary"
          << "subWindowHeader" << "subWindowBg" << "separator" << "black" << "white";
        return n;
    }();

    QString q = QStringLiteral("import QtQuick\nQtObject {\n    property QtObject colors: QtObject {\n");
    for (int i = 0; i < names.size() && i < colors.size(); ++i)
        q += QStringLiteral("        property color %1: \"%2\"\n").arg(names.at(i), colors.at(i));
    // Compressible filler (also pushes size over the 1000-byte threshold).
    for (int i = 0; i < 30; ++i)
        q += QStringLiteral("        // padding padding padding padding padding %1\n").arg(i);
    q += QStringLiteral("    }\n}\n");
    return q;
}

// Bare qCompress record: [be32 usize][zlib stream] with NO outer length field.
// This is the layout the reference tool actually scans for - it reads only the
// 4-byte uncompressed size and never looks for an outer length.
QByteArray bareQCompressRecord(const QByteArray &raw)
{
    const QByteArray comp = ZlibUtils::compress(raw);
    QByteArray rec(4, '\0');
    putBe32(rec, 0, quint32(raw.size()));
    return rec + comp;
}

// The ASCII name marker the reference tool anchors its 8192-byte scan on.
QByteArray themeMarker()
{
    return QByteArray("UiThemeObject.qml") + QByteArray(1, '\0');
}

QStringList colorList(const QString &c, int n = 50)
{
    QStringList out;
    for (int i = 0; i < n; ++i)
        out << c;
    return out;
}

QStringList randomColors(int n = 50)
{
    QStringList out;
    quint32 seed = 0xB51C0DE;
    for (int i = 0; i < n; ++i) {
        seed = seed * 1664525u + 1013904223u;
        out << QStringLiteral("#%1").arg(seed >> 8, 6, 16, QLatin1Char('0')).toUpper();
    }
    return out;
}

quint32 qrcNameHash(const QString &name)
{
    quint32 hash = 0;
    for (const auto c : name) {
        hash = (hash << 4) + c.unicode();
        hash ^= (hash & 0xf0000000) >> 23;
        hash &= 0x0fffffff;
    }
    return hash;
}

// Writes a qrc name record + file node that point at a data record, using the
// identity RVA mapping of makeFakePe (base 0x1000).
void writeQrcNode(QByteArray &d, qint64 entryOff, qint64 nameRec, qint64 nodeOff)
{
    const QString name = QStringLiteral("UiThemeObject.qml");
    putBe16(d, nameRec, quint16(name.size()));
    putBe32(d, nameRec + 2, qrcNameHash(name));
    d.replace(int(nameRec + 6), utf16be(name).size(), utf16be(name));

    const qint64 nameBase = nameRec;
    const qint64 dataBase = 0x1000;
    // A real root + one file, not an isolated flag pattern.
    putBe32(d, nodeOff, 0);
    putBe16(d, nodeOff + 4, 2);
    putBe32(d, nodeOff + 6, 1);
    putBe32(d, nodeOff + 10, 1);
    nodeOff += 14;
    putBe32(d, nodeOff, quint32(nameRec - nameBase));  // name offset
    putBe16(d, nodeOff + 4, 1);                        // flags: file, compressed
    putBe16(d, nodeOff + 6, 0);                        // country
    putBe16(d, nodeOff + 8, 0);                        // language
    putBe32(d, nodeOff + 10, quint32(entryOff - dataBase)); // data offset
}

} // namespace

// ===========================================================================

int runSelfTest()
{
    // Separate settings namespace so the test never touches real user settings.
    QCoreApplication::setOrganizationName(QStringLiteral("BSICustomizerSelftest"));
    QCoreApplication::setApplicationName(QStringLiteral("BSICustomizerSelftest"));
    QSettings().clear();
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    std::printf("BSI Customizer self-test\n");
    std::printf("========================\n");

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        std::printf("FATAL: cannot create temp dir\n");
        return 1;
    }

    // Background patch facade: event delivery, serialization and error cleanup.
    {
        std::printf("[worker] responsive GUI event loop during patch operations\n");
        SettingsManager settings(nullptr, false);
        settings.setFpsAutoDetect(false);
        settings.setExePath(tmp.filePath(QStringLiteral("missing-player.exe")));
        Backend backend(&settings);
        const QString originalPath = backend.exePath();
        bool deliveredWhileBusy = false;
        bool overlapRejected = false;
        int transitions = 0;
        QObject::connect(&backend, &Backend::busyChanged, &backend, [&] {
            ++transitions;
            if (!backend.busy()) return;
            // Enqueued before the worker starts, so this runs before its
            // finished notification even on a very fast machine.
            QMetaObject::invokeMethod(&backend, [&] {
                deliveredWhileBusy = backend.busy();
                overlapRejected = !backend.integrityPatch().value("ok").toBool();
                backend.setExePath(tmp.filePath(QStringLiteral("wrong-player.exe")));
            }, Qt::QueuedConnection);
        });
        const QVariantMap failed = backend.integrityPatch();
        check(deliveredWhileBusy, "GUI receives events while a background patch is active");
        check(overlapRejected, "overlapping patches are refused");
        check(backend.exePath() == originalPath, "target cannot change during a patch");
        check(!failed.value("ok").toBool(), "worker preserves patch failure result");
        check(!backend.busy() && transitions == 2, "failed patch clears busy state exactly once");
        backend.integrityPatch();
        check(!backend.busy() && transitions == 4, "another operation can run after failure");
    }

    // ------------------------------------------------------------------
    // 1) App UI theme chain (C++ NOTIFY -> QML bindings)
    // ------------------------------------------------------------------
    {
        std::printf("[ui] C++ NOTIFY -> context property -> Theme singleton -> QML bindings\n");

        SettingsManager settings;
        ThemeManager themes;
        Backend backend(&settings);
        SysInfo sysinfo;
        AuthManager auth;
        backend.setAuthManager(&auth);

        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("themes"), &themes);
        engine.rootContext()->setContextProperty(QStringLiteral("settings"), &settings);
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("sysinfo"), &sysinfo);
        engine.rootContext()->setContextProperty(QStringLiteral("auth"), &auth);
        engine.load(QUrl(QStringLiteral("qrc:/qt/qml/BSICustomizer/src/qml/Main.qml")));

        QObject *root = engine.rootObjects().value(0, nullptr);
        check(root != nullptr, "Main.qml loaded without errors");

        if (root) {
            QObject *stack = root->findChild<QObject *>(QStringLiteral("studioPageStack"));
            QObject *dashboard = root->findChild<QObject *>(QStringLiteral("studioDashboard"));
            check(stack && stack->property("count").toInt() == 12,
                  "studio shell retains all twelve real application pages");
            check(root->findChild<QObject *>(QStringLiteral("studioSidebar")) != nullptr,
                  "studio navigation sidebar is present");
            const auto resourceCount = backend.resourceInfos().size();
            const bool navigated = dashboard && QMetaObject::invokeMethod(dashboard, "navigate", Q_ARG(int, 2));
            check(navigated && stack && stack->property("currentIndex").toInt() == 2,
                  "dashboard toolkit navigation opens the real Resources page");
            QCoreApplication::processEvents();
            check(backend.resourceInfos().size() == resourceCount,
                  "opening Resources through studio navigation preserves resource results");
            if (stack) stack->setProperty("currentIndex", 0);
            const QColor bgDefault = root->property("testProbe").value<QColor>();
            themes.setTheme(ThemeManager::Light);
            const QColor bgLight = root->property("testProbe").value<QColor>();
            check(bgLight.isValid() && bgLight != bgDefault,
                  "QML re-bound instantly after setTheme(Light)");
            themes.setTheme(ThemeManager::Dark);
            check(root->property("testProbe").value<QColor>() == bgDefault,
                  "switching back to Dark restores the original colors");
        }
    }

    // ------------------------------------------------------------------
    // 2) QML theme (zlib) — in-place patch
    // ------------------------------------------------------------------
    {
        std::printf("[theme-zlib] detect + in-place patch of compressed UiThemeObject.qml\n");

        const QString origQml = makeThemeQml(colorList(QStringLiteral("#000000")));
        const QByteArray rec = qtZlibRecord(origQml.toLatin1());

        QByteArray pe = makeFakePe(0, 0x20000);
        const qint64 entryOff = 0x2000;
        pe.replace(int(entryOff), rec.size(), rec);
        const qint64 nameRec = ((entryOff + rec.size() + 8192) + 3) & ~qint64(3);
        const qint64 nodeOff = ((nameRec + 6 + 34) + 3) & ~qint64(3);
        writeQrcNode(pe, entryOff, nameRec, nodeOff);

        const QString exePath = tmp.path() + QStringLiteral("/inplace.exe");
        {
            QFile f(exePath);
            f.open(QIODevice::WriteOnly);
            f.write(pe);
        }

        ThemeInfo info = ThemeEngine::detectTheme(pe, QStringLiteral("qml"));
        check(info.found && info.mode == QLatin1String("zlib"), "zlib theme detected");
        check(info.hasQrcNode, "qrc node for UiThemeObject.qml located");
        check(info.entryOff == entryOff, "data record offset matches");

        const auto entries = ThemeEngine::parseQmlEntries(info.qml);
        check(entries.size() >= 30, "color entries parsed");

        QMap<QString, QString> edits;
        const QStringList newColors = randomColors();
        for (int i = 0; i < entries.size() && i < 40; ++i)
            edits[entries.at(i).name] = newColors.at(i);

        const auto r = ThemeEngine::patchTheme(exePath, info, entries, edits);
        check(r.ok, "in-place patch succeeded");

        QFile f(exePath);
        f.open(QIODevice::ReadOnly);
        const QByteArray patched = f.readAll();
        f.close();

        const ThemeInfo again = ThemeEngine::detectTheme(patched, QStringLiteral("qml"));
        check(again.found && again.entryOff == entryOff, "patched file re-detected in place");
        bool colorsOk = true;
        const auto entries2 = ThemeEngine::parseQmlEntries(again.qml);
        for (int i = 0; i < entries2.size() && i < 40; ++i) {
            if (entries2.at(i).value.compare(newColors.at(i), Qt::CaseInsensitive) != 0)
                colorsOk = false;
        }
        check(colorsOk, "all edited colors persisted (qrc still valid)");
    }

    // ------------------------------------------------------------------
    // 2b) Stale snapshot: the EXE changed between Load and Patch (another
    //     theme tool patched it while this app was open)
    // ------------------------------------------------------------------
    {
        std::printf("[theme-zlib] patch with a stale ThemeInfo re-detects the live record\n");

        const QString origQml = makeThemeQml(colorList(QStringLiteral("#111111")));
        const QByteArray rec = qtZlibRecord(origQml.toLatin1());

        QByteArray pe = makeFakePe(0, 0x40000);
        const qint64 entryOff = 0x2000;
        pe.replace(int(entryOff), rec.size(), rec);
        const qint64 nameRec = ((entryOff + rec.size() + 8192) + 3) & ~qint64(3);
        const qint64 nodeOff = ((nameRec + 6 + 34) + 3) & ~qint64(3);
        writeQrcNode(pe, entryOff, nameRec, nodeOff);

        const QString exePath = tmp.path() + QStringLiteral("/stale.exe");
        {
            QFile f(exePath);
            f.open(QIODevice::WriteOnly);
            f.write(pe);
        }

        // 1) "Load": the UI holds this snapshot from here on.
        const ThemeInfo snapshot = ThemeEngine::detectTheme(pe, QStringLiteral("qml"));
        check(snapshot.found && snapshot.mode == QLatin1String("zlib"),
              "stale test: theme detected on load");
        const auto snapshotEntries = ThemeEngine::parseQmlEntries(snapshot.qml);
        check(!snapshotEntries.isEmpty(), "stale test: entries parsed");

        // 2) An external tool repaints every color to #222222 while the UI is open.
        {
            QFile f(exePath);
            f.open(QIODevice::ReadOnly);
            const QByteArray now = f.readAll();
            f.close();

            const ThemeInfo fresh = ThemeEngine::detectTheme(now, QStringLiteral("qml"));
            const auto freshEntries = ThemeEngine::parseQmlEntries(fresh.qml);
            QMap<QString, QString> externalEdits;
            for (const auto &e : freshEntries)
                externalEdits[e.name] = QStringLiteral("#222222");
            const auto r = ThemeEngine::patchTheme(exePath, fresh, freshEntries, externalEdits);
            check(r.ok, "stale test: external tool patch succeeded");
        }

        // 3) Patch through the STALE snapshot, changing just one color.
        //    The engine must re-read the file and rebuild the QML from the live
        //    text: our edit lands, the external tool's colors survive, and the
        //    stale pre-change theme must not be written back.
        QMap<QString, QString> edits;
        edits[snapshotEntries.at(0).name] = QStringLiteral("#333333");
        const auto r = ThemeEngine::patchTheme(exePath, snapshot, snapshotEntries, edits);
        check(r.ok, "stale test: patch with an out-of-date snapshot succeeded");

        QFile f(exePath);
        f.open(QIODevice::ReadOnly);
        const QByteArray patched = f.readAll();
        f.close();

        const ThemeInfo live = ThemeEngine::detectTheme(patched, QStringLiteral("qml"));
        check(live.found && live.mode == QLatin1String("zlib"),
              "stale test: theme still detected after patch");
        check(live.qml.contains(QStringLiteral("#333333")),
              "stale test: our edit is live in the EXE");
        check(live.qml.contains(QStringLiteral("#222222")),
              "stale test: external tool's colors kept (stale text not used as base)");
        check(!live.qml.contains(QStringLiteral("#111111")),
              "stale test: stale pre-change theme was not resurrected");
    }

    // ------------------------------------------------------------------
    // 2c) Orphaned copy: an external tool relocated the theme and left the
    //     old record behind.  Both decompress into a perfect theme, so only
    //     the qresource tree can say which one BlueStacks loads.
    // ------------------------------------------------------------------
    {
        std::printf("[theme-zlib] orphaned record: detection follows the qrc-linked live copy\n");

        QByteArray pe = makeFakePe(0, 0x20000);

        // Stale copy left behind at a LOW offset (so "first match wins" would
        // pick it) by an external relocation.
        const QByteArray orphan =
            qtZlibRecord(makeThemeQml(colorList(QStringLiteral("#111111"))).toLatin1());
        const qint64 orphanOff = 0x1100;
        pe.replace(int(orphanOff), orphan.size(), orphan);

        // qt_resource_data array: the live theme plus other compressed
        // resources around it, at deliberately irregular spacing so a shifted
        // data base cannot accidentally land on another valid header.
        const qint64 dataBase = 0x2000;
        const QList<qint64> payloadOff = { 0x2000, 0x20A0, 0x2200, 0x2600, 0x3400, 0x3480 };
        const int themeIdx = 3;
        for (int i = 0; i < payloadOff.size(); ++i) {
            const QByteArray rec =
                (i == themeIdx)
                    ? qtZlibRecord(makeThemeQml(colorList(QStringLiteral("#444444"))).toLatin1())
                    : qtZlibRecord(QByteArray("property int filler") + QByteArray(40, char('a' + i)));
            pe.replace(int(payloadOff.at(i)), rec.size(), rec);
        }

        // qt_resource_name array.
        const qint64 nameBase = 0x4000;
        const QStringList names = { QStringLiteral("a.qml"), QStringLiteral("bb.qml"),
                                    QStringLiteral("ccc.qml"),
                                    QStringLiteral("UiThemeObject.qml"),
                                    QStringLiteral("dddd.qml"), QStringLiteral("eeeee.qml") };
        QList<qint64> nameRecs;
        for (int i = 0; i < names.size(); ++i) {
            const qint64 rec = nameBase + i * 0x80;
            putBe16(pe, rec, quint16(names.at(i).size()));
            putBe32(pe, rec + 2, qrcNameHash(names.at(i)));
            pe.replace(int(rec + 6), utf16be(names.at(i)).size(), utf16be(names.at(i)));
            nameRecs << rec;
        }

        // qt_resource_tree: one 14-byte entry per payload.
        const qint64 nodeBase = 0x6000;
        putBe32(pe, nodeBase - 14, 0);
        putBe16(pe, nodeBase - 10, 2);
        putBe32(pe, nodeBase - 8, quint32(payloadOff.size()));
        putBe32(pe, nodeBase - 4, 1);
        for (int i = 0; i < payloadOff.size(); ++i) {
            const qint64 node = nodeBase + i * 14;
            putBe32(pe, node, quint32(nameRecs.at(i) - nameBase));         // name offset
            putBe16(pe, node + 4, 1);                                      // compressed file
            putBe16(pe, node + 6, 0);
            putBe16(pe, node + 8, 0);
            putBe32(pe, node + 10, quint32(payloadOff.at(i) - dataBase));  // data offset
        }

        const ThemeInfo t = ThemeEngine::detectTheme(pe, QStringLiteral("qml"));
        check(t.found && t.mode == QLatin1String("zlib"), "orphan test: theme detected");
        check(t.candidateCount >= 2, "orphan test: both copies seen as candidates");
        check(t.entryOff == payloadOff.at(themeIdx),
              "orphan test: live qrc-linked record chosen, not the stale orphan");
        check(t.hasQrcNode && t.qrcNodeOff == nodeBase + themeIdx * 14,
              "orphan test: the theme's own tree entry was identified");
        check(t.qrcSupport > 0,
              "orphan test: data base corroborated by neighbouring tree entries");
        check(t.qml.contains(QStringLiteral("#444444")), "orphan test: live colors reported");
        check(!t.qml.contains(QStringLiteral("#111111")),
              "orphan test: orphan colors not reported");
    }

    // ------------------------------------------------------------------
    // 2d) buildQml: a longer color must be applied to a compressed theme,
    //     and anything refused must be reported instead of dropped silently
    // ------------------------------------------------------------------
    {
        std::printf("[theme-qml] buildQml grows compressed slots and reports refused edits\n");

        const QString qml = QStringLiteral("QtObject {\n"
                                          "    property color primary100: \"#111111\"\n"
                                          "    property color overlayBlack: \"#80222222\"\n"
                                          "    property color separator: \"#333333\"\n"
                                          "}\n");
        const auto entries = ThemeEngine::parseQmlEntries(qml);
        check(entries.size() == 3, "buildQml test: three color entries parsed");

        // 6-digit slot -> 8-digit value grows the expression by two characters.
        QMap<QString, QString> grow;
        grow[QStringLiteral("primary100")] = QStringLiteral("#aabbccdd");

        QStringList skippedGrow;
        const QString grown = ThemeEngine::buildQml(qml, entries, grow, true, &skippedGrow);
        check(grown.contains(QStringLiteral("\"#aabbccdd\"")),
              "buildQml test: longer hex applied when the record is recompressed");
        check(skippedGrow.isEmpty(), "buildQml test: nothing refused in grow mode");
        check(grown.contains(QStringLiteral("\"#333333\"")),
              "buildQml test: untouched entries survive a growing edit");

        QStringList skippedFit;
        const QString unchanged = ThemeEngine::buildQml(qml, entries, grow, false, &skippedFit);
        check(unchanged == qml,
              "buildQml test: in-place text mode still refuses to grow the slot");
        check(skippedFit.size() == 1 && skippedFit.first().contains(QStringLiteral("primary100")),
              "buildQml test: the refusal is reported, not silent");

        // An existing 8-digit alpha is kept when a 6-digit color is chosen.
        QMap<QString, QString> alphaEdit;
        alphaEdit[QStringLiteral("overlayBlack")] = QStringLiteral("#123456");
        const QString alpha = ThemeEngine::buildQml(qml, entries, alphaEdit, true, nullptr);
        check(alpha.contains(QStringLiteral("\"#80123456\"")),
              "buildQml test: existing alpha preserved");
        check(!alpha.contains(QStringLiteral("\"#123456\"")),
              "buildQml test: no alpha-less value written over an ARGB slot");

        // An unquoted expression slot gets a valid quoted literal, not a bare
        // "#rrggbb" that would make the QML fail to parse.
        const QString exprQml = QStringLiteral(
            "QtObject {\n    property color notice: Qt.rgba(0.5, 0.5, 0.5, 1)\n}\n");
        const auto exprEntries = ThemeEngine::parseQmlEntries(exprQml);
        check(exprEntries.size() == 1 && !exprEntries.first().quoted,
              "buildQml test: unquoted expression parsed");
        QMap<QString, QString> exprEdit;
        exprEdit[QStringLiteral("notice")] = QStringLiteral("#00ff00");
        QStringList skippedExpr;
        const QString exprOut =
            ThemeEngine::buildQml(exprQml, exprEntries, exprEdit, true, &skippedExpr);
        check(exprOut.contains(QStringLiteral("\"#00ff00\"")),
              "buildQml test: expression slot rewritten as a quoted literal");
        check(skippedExpr.isEmpty(), "buildQml test: expression rewrite not refused");
    }

    // ------------------------------------------------------------------
    // 2e) Loose content tier: a build that names the theme object
    //     differently must still be found instead of "theme not found"
    // ------------------------------------------------------------------
    {
        std::printf("[theme-zlib] loose content tier finds a renamed theme object\n");

        QString qml = QStringLiteral("import QtQuick 2.0\nSkinPalette {\n");
        for (int i = 0; i < 12; ++i)
            qml += QStringLiteral("    property color tint%1: \"#1A2B3C\"\n").arg(i);
        qml += QStringLiteral("}\n");

        const QByteArray rec = qtZlibRecord(qml.toLatin1());
        QByteArray pe = makeFakePe(0, 0x20000);
        const qint64 entryOff = 0x2000;
        pe.replace(int(entryOff), rec.size(), rec);

        const ThemeInfo t = ThemeEngine::detectTheme(pe, QStringLiteral("qml"));
        check(t.found && t.mode == QLatin1String("zlib"), "loose tier: theme still detected");
        check(t.contentQuality == 1, "loose tier: reported as a fallback match");
        check(t.entryCount == 12, "loose tier: all color entries parsed");
        check(t.entryOff == entryOff, "loose tier: correct record offset");
    }

    // ------------------------------------------------------------------
    // 2f) Presets: name variants, 6-digit values, full table coverage
    // ------------------------------------------------------------------
    {
        std::printf("[presets] palettes match every spelling of the theme table\n");

        const QList<ThemePreset> presets = ThemePresets::all();
        check(presets.size() >= 10, "presets: built-in list present");
        const ThemePreset *black = ThemePresets::byId(QStringLiteral("pure_black_minimal"));
        check(black && black->toggleOn == QLatin1String("#1A90FF"),
              "presets: pure black minimal ported");
        const ThemePreset *amethyst = ThemePresets::byId(QStringLiteral("amethyst"));
        check(amethyst && amethyst->toggleOff == QLatin1String("#3D2463"),
              "presets: Amethyst ported");
        check(ThemePresets::byId(QStringLiteral("nope")) == nullptr, "presets: unknown id is null");

        check(ThemePresets::normalize(QStringLiteral("seperator")) == QLatin1String("separator"),
              "presets: seperator -> separator");
        check(ThemePresets::normalize(QStringLiteral("overLayGrey")) == QLatin1String("overlaygray"),
              "presets: overLayGrey -> overlaygray");
        check(ThemePresets::normalize(QStringLiteral("accentClickTransparent"))
                  == QLatin1String("accentclicktransparent"),
              "presets: case and punctuation folded");

        static const QRegularExpression hex6(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
        const auto entries = ThemeEngine::parseQmlEntries(makeThemeQml(colorList("#010203")));
        for (const ThemePreset &p : presets) {
            const QMap<QString, QString> m = ThemePresets::matchEntries(p, entries);
            check(m.size() >= 30, "presets: covers the theme table");
            bool allHex = true;
            for (auto it = m.constBegin(); it != m.constEnd(); ++it) {
                if (!hex6.match(it.value()).hasMatch())
                    allHex = false;
            }
            check(allHex, "presets: every value is a 6-digit hex");
        }

        // A color the EXE already has is not staged as an edit.
        const ThemePreset *nord = ThemePresets::byId(QStringLiteral("nord"));
        QList<QmlEntry> oneEntry;
        QmlEntry e;
        e.name = QStringLiteral("primary100");
        e.value = nord ? nord->colors.value(QStringLiteral("primary100")) : QString();
        oneEntry << e;
        check(nord && ThemePresets::matchEntries(*nord, oneEntry).isEmpty(),
              "presets: an already-applied color is not staged");

        check(ThemePresets::toVariantList().size() == presets.size(),
              "presets: QML list matches the C++ list");

        // ---- Hex (old) method: the same palettes reach the legacy editor ----
        QList<HexColorItem> hexRows;
        for (const QString &name : ThemeEngine::qmlColorNames()) {
            HexColorItem item;
            item.name = name;
            item.currentHex = QStringLiteral("#010203");
            hexRows << item;
        }
        for (const ThemePreset &p : presets) {
            const QMap<QString, QString> m = ThemePresets::matchHexColors(p, hexRows);
            check(m.size() == hexRows.size(),
                  "presets: hex method covers every palette color name");
            bool allHex = true;
            for (auto it = m.constBegin(); it != m.constEnd(); ++it) {
                if (!hex6.match(it.value()).hasMatch())
                    allHex = false;
            }
            check(allHex, "presets: hex values are 6-digit hex");
        }

        // An already-applied legacy value is not staged again.
        QList<HexColorItem> sameRow;
        HexColorItem sameItem;
        sameItem.name = QStringLiteral("accentNormal");
        sameItem.currentHex = nord ? nord->colors.value(QStringLiteral("accentnormal")) : QString();
        sameRow << sameItem;
        check(nord && ThemePresets::matchHexColors(*nord, sameRow).isEmpty(),
              "presets: an already-applied hex color is not staged");

        // An 8-digit ARGB slot keeps its alpha, so an equal RGB is unchanged.
        const QString nordRgb = nord ? nord->colors.value(QStringLiteral("accentnormal")) : QString();
        QList<HexColorItem> alphaRow;
        HexColorItem alphaItem;
        alphaItem.name = QStringLiteral("accentNormal");
        alphaItem.currentHex = nordRgb.isEmpty() ? QString()
                                                 : QStringLiteral("#FF") + nordRgb.mid(1);
        alphaRow << alphaItem;
        check(nord && ThemePresets::matchHexColors(*nord, alphaRow).isEmpty(),
              "presets: alpha-only difference is not a staged hex change");

        // Unknown legacy slots keep their value.
        QList<HexColorItem> unknownRow;
        HexColorItem unknown;
        unknown.name = QStringLiteral("notAThemeSlot");
        unknown.currentHex = QStringLiteral("#123456");
        unknownRow << unknown;
        check(ThemePresets::matchHexColors(presets.constFirst(), unknownRow).isEmpty(),
              "presets: unknown hex color names are left alone");

        // A preset staged through the hex matcher really becomes a replacement.
        QByteArray legacy;
        legacy += utf16le(QStringLiteral("accentNormal"));
        legacy += utf16le(QStringLiteral(": \"#1A90FF\","));
        HexColorItem legacyRow;
        legacyRow.name = QStringLiteral("accentNormal");
        legacyRow.currentHex = QStringLiteral("#1A90FF");
        const QMap<QString, QString> staged =
            ThemePresets::matchHexColors(*nord, QList<HexColorItem> { legacyRow });
        legacyRow.color = staged.value(QStringLiteral("accentNormal"));
        legacyRow.enabled = true;
        const auto repls = ThemeEngine::collectThemeReplacements(
            legacy, QStringLiteral("NXT"), {}, QList<HexColorItem> { legacyRow });
        check(repls.size() == 1 && repls.first().rep == utf16le(QStringLiteral("#88C0D0")),
              "presets: staged hex color replaces the EXE value");
    }
        // ------------------------------------------------------------------
    // 2g) Toggle SVG recolor (PatchToggleSvgs): length-safe, knob preserved
    // ------------------------------------------------------------------
    {
        std::printf("[svg] toggle recolor keeps the blob size and the white knob\n");

        const QByteArray onSvg = QByteArray("<svg id=\"toggle_on\" width=\"24\" height=\"14\">"
                                            "<rect width=\"24\" height=\"14\" rx=\"7\" "
                                            "fill=\"#1A90FF\"/>"
                                            "<circle cx=\"17\" cy=\"7\" r=\"5\" fill=\"#FFFFFF\"/>"
                                            "</svg>");
        const QByteArray offSvg = QByteArray("<svg id=\"toggle_off\" width=\"24\" height=\"14\">"
                                             "<rect width=\"24\" height=\"14\" rx=\"7\" "
                                             "fill=\"#808080\"/>"
                                             "<circle cx=\"17\" cy=\"7\" r=\"5\" fill=\"#FFFFFF\"/>"
                                             "</svg>");

        int n = 0;
        const QByteArray out = SvgEngine::recolor(onSvg, QStringLiteral("#8B5CF6"), true, &n);
        check(n == 1, "svg recolor: exactly one color replaced");
        check(out.size() == onSvg.size(), "svg recolor: blob length unchanged");
        check(out.contains("#8B5CF6"), "svg recolor: new color written");
        check(out.contains("#FFFFFF"), "svg recolor: white knob preserved");
        check(!out.contains("#1A90FF"), "svg recolor: old color gone");

        int none = 99;
        SvgEngine::recolor(onSvg, QStringLiteral("#8B5CF6"), true, &none);
        check(none == 1, "svg recolor: repeatable");
        int already = 99;
        const QByteArray same = SvgEngine::recolor(out, QStringLiteral("#8B5CF6"), true, &already);
        check(already == 0 && same == out, "svg recolor: already-colored blob is a no-op");

        // 8-digit keeps its alpha, 3-digit stays 3 digits.
        const QByteArray argbIn("<svg><stop stop-color=\"#1A90FF80\"/></svg>");
        int n8 = 0;
        const QByteArray argb = SvgEngine::recolor(argbIn, QStringLiteral("#00FF41"), true, &n8);
        check(n8 == 1 && argb.contains("#00FF4180") && argb.size() == argbIn.size(),
              "svg recolor: 8-digit alpha preserved, length kept");
        const QByteArray rgb3In("<svg><rect fill=\"#ABC\"/></svg>");
        int n3 = 0;
        const QByteArray rgb3 = SvgEngine::recolor(rgb3In, QStringLiteral("#00FF41"), true, &n3);
        check(n3 == 1 && rgb3.contains("#0F4") && rgb3.size() == rgb3In.size(),
              "svg recolor: 3-digit stays 3 digits");

        // colorsIn / preview / guessToggles
        const QStringList cols = SvgEngine::colorsIn(onSvg);
        check(cols.size() == 2, "svg recolor: colors listed");
        check(SvgEngine::preview(onSvg).startsWith(QLatin1String("<svg id=\"toggle_on\"")),
              "svg recolor: preview text");

        QList<SvgBlob> blobs;
        SvgBlob a;
        a.index = 0;
        a.offset = 0x1500;
        a.data = offSvg;
        a.size = offSvg.size();
        SvgBlob b;
        b.index = 1;
        b.offset = 0x1800;
        b.data = onSvg;
        b.size = onSvg.size();
        blobs << a << b;
        const SvgEngine::ToggleGuess guess = SvgEngine::guessToggles(blobs);
        check(guess.off == 0 && guess.on == 1, "svg recolor: off/on guessed from the content");

        // End to end on a fake EXE.
        QByteArray pe = makeFakePe(0, 0x4000);
        pe.replace(int(a.offset), a.data.size(), a.data);
        pe.replace(int(b.offset), b.data.size(), b.data);
        const QString exePath = tmp.path() + QStringLiteral("/toggles.exe");
        {
            QFile f(exePath);
            f.open(QIODevice::WriteOnly);
            f.write(pe);
        }
        const SvgEngine::RecolorResult r =
            SvgEngine::recolorToggles(exePath, 0, 1, QStringLiteral("#111111"),
                                      QStringLiteral("#222222"));
        check(r.ok, "svg recolor: patch of both states succeeded");

        QFile f(exePath);
        f.open(QIODevice::ReadOnly);
        const QByteArray patched = f.readAll();
        f.close();
        const QList<SvgBlob> after = SvgEngine::findSvgs(patched);
        check(after.size() == 2, "svg recolor: both blobs still present and in place");
        check(after.at(0).data.contains("#111111"), "svg recolor: off state color written");
        check(after.at(1).data.contains("#222222"), "svg recolor: on state color written");
        check(patched.size() == pe.size(), "svg recolor: file size unchanged");
        check(QFileInfo::exists(exePath + QStringLiteral(".bak")), "svg recolor: backup created");
    }

    // ------------------------------------------------------------------
    // 2h) Bare qCompress record: [be32 usize][zlib] with no outer length
    //     field.  The reference tool detects this layout; requiring an outer
    //     length made us skip it and report "theme not found".
    // ------------------------------------------------------------------
    {
        std::printf("[theme-zlib] bare qCompress record (no outer length field)\n");

        const QString origQml = makeThemeQml(colorList(QStringLiteral("#0A0A0A")));
        const QByteArray rec = bareQCompressRecord(origQml.toLatin1());

        QByteArray pe = makeFakePe(0, 0x20000);
        const qint64 markOff = 0x1800;
        pe.replace(int(markOff), themeMarker().size(), themeMarker());
        // inside the 8192-byte marker window, with the preceding bytes zero so
        // there is nothing that could be mistaken for an outer length field
        const qint64 entryOff = markOff + themeMarker().size() + 16;
        pe.replace(int(entryOff), rec.size(), rec);

        const QString exePath = tmp.path() + QStringLiteral("/bare.exe");
        {
            QFile f(exePath);
            f.open(QIODevice::WriteOnly);
            f.write(pe);
        }

        const ThemeInfo t = ThemeEngine::detectTheme(pe, QStringLiteral("qml"));
        check(t.found && t.mode == QLatin1String("zlib"), "bare qCompress theme detected");
        check(t.headerBytes == 4, "bare qCompress: reported as a 4-byte header");
        check(t.markerAnchored, "bare qCompress: found through the marker window");
        check(t.entryOff == entryOff, "bare qCompress: entry offset is the usize field");
        check(t.zoff == entryOff + 4, "bare qCompress: stream offset correct");
        check(t.compSize == quint32(rec.size() - 4),
              "bare qCompress: compressed length measured exactly");
        check(t.qml.contains(QStringLiteral("#0A0A0A")), "bare qCompress: live colors reported");

        const auto entries = ThemeEngine::parseQmlEntries(t.qml);
        check(entries.size() >= 30, "bare qCompress: color entries parsed");

        const QByteArray before = pe.mid(int(entryOff - 4), 4);
        QMap<QString, QString> edits;
        edits[entries.at(0).name] = QStringLiteral("#0B0B0B");
        const auto r = ThemeEngine::patchTheme(exePath, t, entries, edits);
        check(r.ok, "bare qCompress: in-place patch succeeded");

        QFile f(exePath);
        f.open(QIODevice::ReadOnly);
        const QByteArray patched = f.readAll();
        f.close();
        check(patched.size() == pe.size(), "bare qCompress: file size unchanged");
        check(patched.mid(int(entryOff - 4), 4) == before,
              "bare qCompress: bytes in front of the record were NOT rewritten");
        const ThemeInfo again = ThemeEngine::detectTheme(patched, QStringLiteral("qml"));
        check(again.found && again.qml.contains(QStringLiteral("#0B0B0B"), Qt::CaseInsensitive),
              "bare qCompress: the edit is live after patching");
        check(again.headerBytes == 4, "bare qCompress: still detected as a 4-byte header");
    }

    // ------------------------------------------------------------------
    // 2i) Headerless stream: no size field at all, so neither the outer
    //     length nor the uncompressed size can be read from the file.
    // ------------------------------------------------------------------
    {
        std::printf("[theme-zlib] headerless compressed stream (no size field)\n");

        const QString origQml = makeThemeQml(colorList(QStringLiteral("#0C0C0C")));
        const QByteArray comp = ZlibUtils::compress(origQml.toLatin1());

        QByteArray pe = makeFakePe(0, 0x20000);
        const qint64 markOff = 0x1800;
        pe.replace(int(markOff), themeMarker().size(), themeMarker());
        const qint64 zoff = markOff + themeMarker().size() + 16;
        pe.replace(int(zoff), comp.size(), comp);

        const QString exePath = tmp.path() + QStringLiteral("/headerless.exe");
        {
            QFile f(exePath);
            f.open(QIODevice::WriteOnly);
            f.write(pe);
        }

        const ThemeInfo t = ThemeEngine::detectTheme(pe, QStringLiteral("qml"));
        check(t.found && t.mode == QLatin1String("zlib"), "headerless theme detected");
        check(t.headerBytes == 0, "headerless: reported as a 0-byte header");
        check(t.zoff == zoff, "headerless: stream offset correct");
        check(t.compSize == quint32(comp.size()),
              "headerless: compressed length measured exactly");
        check(t.qml.contains(QStringLiteral("#0C0C0C")), "headerless: live colors reported");

        const auto entries = ThemeEngine::parseQmlEntries(t.qml);
        const QByteArray before = pe.mid(int(zoff - 8), 8);
        QMap<QString, QString> edits;
        edits[entries.at(0).name] = QStringLiteral("#0D0D0D");
        const auto r = ThemeEngine::patchTheme(exePath, t, entries, edits);
        check(r.ok, "headerless: in-place patch succeeded");

        QFile f(exePath);
        f.open(QIODevice::ReadOnly);
        const QByteArray patched = f.readAll();
        f.close();
        check(patched.mid(int(zoff - 8), 8) == before,
              "headerless: the 8 bytes in front of the stream were NOT rewritten");
        const ThemeInfo again = ThemeEngine::detectTheme(patched, QStringLiteral("qml"));
        check(again.found && again.qml.contains(QStringLiteral("#0D0D0D"), Qt::CaseInsensitive),
              "headerless: the edit is live after patching");
    }

    // ------------------------------------------------------------------
    // 2j) A record with no usable marker: the loose second pass must still
    //     find it instead of reporting "theme not found in this file"
    // ------------------------------------------------------------------
    {
        std::printf("[theme-zlib] no marker present: loose fallback pass finds the theme\n");

        const QString origQml = makeThemeQml(colorList(QStringLiteral("#0E0E0E")));
        const QByteArray rec = bareQCompressRecord(origQml.toLatin1());

        QByteArray pe = makeFakePe(0, 0x20000);
        const qint64 entryOff = 0x3000;
        pe.replace(int(entryOff), rec.size(), rec);

        check(!pe.contains(QByteArray("UiThemeObject.qml")),
              "no-marker test: the file really has no marker");

        const ThemeInfo t = ThemeEngine::detectTheme(pe, QStringLiteral("qml"));
        check(t.found && t.mode == QLatin1String("zlib"),
              "no-marker test: theme still detected by the loose pass");
        check(!t.markerAnchored, "no-marker test: correctly reported as not marker-anchored");
        check(t.headerBytes == 4, "no-marker test: header layout still measured correctly");
        check(t.qml.contains(QStringLiteral("#0E0E0E")), "no-marker test: live colors reported");
    }

    // ------------------------------------------------------------------
    // 2k) Colon-less color declarations: the reference tool's regex does not
    //     require the ':' a normal QML binding has, so neither may we
    // ------------------------------------------------------------------
    {
        std::printf("[theme-qml] colon-less color declarations still parse\n");

        QString qml = QStringLiteral("QtObject {\n");
        for (int i = 0; i < 8; ++i)
            qml += QStringLiteral("    property color tint%1 #1A2B3C\n").arg(i);
        qml += QStringLiteral("}\n");

        const auto e = ThemeEngine::parseQmlEntries(qml);
        check(e.size() == 8, "colon-less: all eight entries parsed");
        if (e.size() == 8) {
            check(e.at(0).name == QLatin1String("tint0"), "colon-less: name captured");
            check(e.at(0).value == QLatin1String("#1A2B3C"), "colon-less: value captured");
            check(!e.at(0).quoted, "colon-less: recorded as unquoted");
        }

        QMap<QString, QString> edits;
        edits[QStringLiteral("tint0")] = QStringLiteral("#00FF41");
        const QString out = ThemeEngine::buildQml(qml, e, edits, true, nullptr);
        check(out.contains(QStringLiteral("\"#00FF41\"")),
              "colon-less: edit applied in grow mode");
        check(out.contains(QStringLiteral("#1A2B3C")),
              "colon-less: untouched entries survive");

        // Mixed file: the colon spelling must win where both could match.
        const QString mixed = QStringLiteral("QtObject {\n"
                                             "    property color a: \"#111111\"\n"
                                             "    property color b #222222\n"
                                             "}\n");
        const auto me = ThemeEngine::parseQmlEntries(mixed);
        check(me.size() == 2, "colon-less: mixed file yields both entries");
        bool aQuoted = false, bPlain = false;
        for (const QmlEntry &x : me) {
            if (x.name == QLatin1String("a"))
                aQuoted = x.quoted && x.value == QLatin1String("#111111");
            if (x.name == QLatin1String("b"))
                bPlain = !x.quoted && x.value == QLatin1String("#222222");
        }
        check(aQuoted, "colon-less: the colon entry kept its quoted spelling");
        check(bPlain, "colon-less: the colon-less entry parsed alongside it");
    }

    // ------------------------------------------------------------------
    // 2l) Stock-palette signature.  BS_THEME_CHANGER carries BlueStacks' own
    //     default color table and falls back on it when parsing finds nothing.
    //     Those hex values (#080A1F, #1B1E38, #1A90FF, ...) are distinctive
    //     enough to identify a theme object that has been reshaped or renamed
    //     by a build we have never seen - which the name-based tiers reject.
    // ------------------------------------------------------------------
    {
        std::printf("[theme-zlib] stock BlueStacks palette identifies a reshaped theme\n");

        // Colon-less, and deliberately free of the words "property" and "color"
        // so neither the strict tier nor the loose tier can accept it.  Only the
        // palette signature can.
        QString qml = QStringLiteral("import QtQuick 2.0\nItem {\n");
        const QStringList stock = {
            QStringLiteral("surfaceDeep #080A1F"), QStringLiteral("surfaceMid #1B1E38"),
            QStringLiteral("surfaceLite #232642"), QStringLiteral("brandMain #1A90FF"),
            QStringLiteral("brandHover #2EA4FF"),  QStringLiteral("alertMain #EB2C43"),
            QStringLiteral("goodMain #7AC143"),
        };
        for (const QString &line : stock)
            qml += QStringLiteral("    %1\n").arg(line);
        for (int i = 0; i < 6; ++i)
            qml += QStringLiteral("    // padding padding padding padding %1\n").arg(i);
        qml += QStringLiteral("}\n");

        check(!qml.contains(QLatin1String("property")),
              "stock palette: fixture really has no 'property' keyword");
        check(!qml.contains(QLatin1String("color")),
              "stock palette: fixture really has no 'color' keyword");

        const QByteArray rec = qtZlibRecord(qml.toLatin1());
        QByteArray pe = makeFakePe(0, 0x20000);
        pe.replace(0x2000, rec.size(), rec);

        const ThemeInfo t = ThemeEngine::detectTheme(pe, QStringLiteral("qml"));
        check(t.found && t.mode == QLatin1String("zlib"),
              "stock palette: reshaped theme found via the palette signature");
        check(t.stockHits >= 3, "stock palette: stock color hits recorded");
        check(t.entryCount >= 7, "stock palette: every entry is editable");
        check(t.qml.contains(QStringLiteral("#1A90FF")),
              "stock palette: live colors reported back to the UI");

        // The same colors in a form no parser can read must NOT be claimed.
        // Reporting "found" with nothing editable would also suppress the
        // fall-through to the plain and hex methods in detectTheme.
        const QStringList hexes = {
            QStringLiteral("#080A1F"), QStringLiteral("#0B0C0E"), QStringLiteral("#1B1E38"),
            QStringLiteral("#17181C"), QStringLiteral("#232642"), QStringLiteral("#21242C"),
            QStringLiteral("#2B2E4C"), QStringLiteral("#2E3038"), QStringLiteral("#131621"),
            QStringLiteral("#555E6B"),
        };
        QString opaque = QStringLiteral("TOKENS");
        for (int i = 0; i < hexes.size(); ++i)
            opaque += QStringLiteral(" k%1=\"%2\"").arg(i).arg(hexes.at(i));
        opaque += QStringLiteral(" ") + QString(200, QLatin1Char('z'));
        check(ThemeEngine::parseQmlEntries(opaque).isEmpty(),
              "stock palette: the opaque fixture yields no editable entries");

        const QByteArray rec2 = qtZlibRecord(opaque.toLatin1());
        QByteArray pe2 = makeFakePe(0, 0x20000);
        pe2.replace(0x2000, rec2.size(), rec2);
        const ThemeInfo t2 = ThemeEngine::detectTheme(pe2, QStringLiteral("qml"));
        check(!t2.found,
              "stock palette: an unparseable blob is not claimed as a theme");
    }

    // ------------------------------------------------------------------
    // 2m) GradientStop.  BlueStacks' UiThemeObject.qml carries gradient
    //     definitions, and a multi-line stop holds a bare "color: #hex"
    //     line that is NOT a theme slot.  buildQml maps edits by name, so
    //     an entry called "color" would let one UI row rewrite every stop
    //     in the file at once.
    // ------------------------------------------------------------------
    {
        std::printf("[theme-qml] GradientStop colors are not theme slots\n");

        const QString qml = QStringLiteral(
            "import QtQuick\n"
            "QtObject {\n"
            "    id: root\n"
            "    property QtObject colors: QtObject {\n"
            "        property color primary100: \"#080A1F\"\n"
            "        property color cloudGradientPrimary: \"#1A90FF\"\n"
            "        property color cloudGradientSecondary: \"#057CEB\"\n"
            "    }\n"
            "    property Gradient cloudGradient: Gradient {\n"
            "        GradientStop {\n"
            "            position: 0.0\n"
            "            color: \"#1A90FF\"\n"
            "        }\n"
            "        GradientStop {\n"
            "            position: 1.0\n"
            "            color: \"#057CEB\"\n"
            "        }\n"
            "    }\n"
            "}\n");

        const auto e = ThemeEngine::parseQmlEntries(qml);
        check(e.size() == 3, "gradient: only the three real theme slots parse");
        bool sawGeneric = false;
        for (const QmlEntry &x : e) {
            const QString n = x.name.toLower();
            if (n == QLatin1String("color") || n == QLatin1String("position"))
                sawGeneric = true;
        }
        check(!sawGeneric, "gradient: no entry is named 'color' or 'position'");

        // Two occurrences of #1A90FF before the edit: the theme slot and the
        // gradient stop.  Editing the slot must change exactly one of them.
        check(qml.count(QStringLiteral("#1A90FF")) == 2,
              "gradient: fixture really has the value in both places");
        QMap<QString, QString> edits;
        edits[QStringLiteral("cloudGradientPrimary")] = QStringLiteral("#00FF41");
        const QString out = ThemeEngine::buildQml(qml, e, edits, true, nullptr);
        check(out.contains(QStringLiteral("#00FF41")), "gradient: the slot was edited");
        check(out.count(QStringLiteral("#1A90FF")) == 1,
              "gradient: the GradientStop literal was left alone");

        // The filter must stay narrow: these are real BlueStacks slot names
        // recovered from BS_THEME_CHANGER's own table.
        const QString real = QStringLiteral("QtObject {\n"
                                            "    background: \"#101010\"\n"
                                            "    border: \"#202020\"\n"
                                            "    text: \"#303030\"\n"
                                            "    accent: \"#404040\"\n"
                                            "    selection: \"#505050\"\n"
                                            "}\n");
        const auto re = ThemeEngine::parseQmlEntries(real);
        check(re.size() == 5, "gradient filter: real slot names are NOT filtered");

        // SVG presentation attributes belong to SvgEngine, not the QML table.
        const QString svgish = QStringLiteral("Item {\n"
                                              "    fill: \"#111111\"\n"
                                              "    stroke: \"#222222\"\n"
                                              "    property color accentNormal: \"#333333\"\n"
                                              "}\n");
        const auto se = ThemeEngine::parseQmlEntries(svgish);
        check(se.size() == 1 && se.at(0).name == QLatin1String("accentNormal"),
              "gradient filter: bare fill/stroke lines are skipped");
    }

    // ------------------------------------------------------------------
    // 3) QML theme (zlib) — relocation when the slot is too small
    // ------------------------------------------------------------------
    {
        std::printf("[theme-zlib] relocation into zero cave when compressed theme overflows\n");

        const QString origQml = makeThemeQml(colorList(QStringLiteral("#000000")));
        const QByteArray rec = qtZlibRecord(origQml.toLatin1());

        QByteArray pe = makeFakePe(0, 0x20000);
        const qint64 entryOff = 0x2000;
        pe.replace(int(entryOff), rec.size(), rec);
        // A resource relocation fixture needs a data section, not executable
        // .text padding. The production writer must never reuse code as a cave.
        pe.replace(0x188, 8, QByteArray(".rdata\0\0", 8));
        putLe32(pe, 0x1ac, 0x40000040);
        pe[int(entryOff + rec.size())] = char(0xFF); // no zero slack -> forces relocation
        const qint64 nameRec = 0x6000;
        const qint64 nodeOff = 0x7000;
        writeQrcNode(pe, entryOff, nameRec, nodeOff);
        // Anchor the pool's real bases so the retained original payload cannot
        // be mistaken for a second live theme after its node is repointed.
        const qint64 header = 0x1100;
        pe.replace(header, 4, "qres");
        putBe32(pe, header + 4, 1);
        putBe32(pe, header + 8, quint32(nodeOff - header));
        putBe32(pe, header + 12, quint32(entryOff - header));
        putBe32(pe, header + 16, quint32(nameRec - header));
        putBe32(pe, nodeOff + 14 + 10, 0); // File data offset from entryOff.


        const QString exePath = tmp.path() + QStringLiteral("/relocate.exe");
        {
            QFile f(exePath);
            f.open(QIODevice::WriteOnly);
            f.write(pe);
        }

        ThemeInfo info = ThemeEngine::detectTheme(pe, QStringLiteral("qml"));
        check(info.found && info.hasQrcNode, "zlib theme + qrc node detected");

        const auto entries = ThemeEngine::parseQmlEntries(info.qml);
        QMap<QString, QString> edits;
        const QStringList newColors = randomColors();
        for (int i = 0; i < entries.size() && i < 45; ++i)
            edits[entries.at(i).name] = newColors.at(i);

        const auto r = ThemeEngine::patchTheme(exePath, info, entries, edits);
        if (!r.ok)
            std::printf("  DEBUG relocation msg: %s\n", qPrintable(r.msg));
        check(r.ok, "relocation patch succeeded");
        check(r.msg.contains(QLatin1String("relocated")), "reported as relocated");

        QFile f(exePath);
        f.open(QIODevice::ReadOnly);
        const QByteArray patched = f.readAll();
        f.close();

        const ThemeInfo again = ThemeEngine::detectTheme(patched, QStringLiteral("qml"));
        check(again.found, "patched file re-detected after relocation");
        check(again.entryOff != entryOff, "theme record moved to the cave");
        bool colorsOk = true;
        const auto entries2 = ThemeEngine::parseQmlEntries(again.qml);
        for (int i = 0; i < entries2.size() && i < 45; ++i) {
            if (entries2.at(i).value.compare(newColors.at(i), Qt::CaseInsensitive) != 0)
                colorsOk = false;
        }
        check(colorsOk, "all edited colors persisted after relocation");
    }

    // ------------------------------------------------------------------
    // 4) QML theme (plain, uncompressed UTF-16LE)
    // ------------------------------------------------------------------
    {
        std::printf("[theme-plain] uncompressed UTF-16LE QML edit in place\n");

        QString qml = QStringLiteral("import QtQuick\nQtObject {\n  property QtObject colors: QtObject {\n");
        qml += QStringLiteral("    property color primary100 : \"#112233\"\n");
        qml += QStringLiteral("    property color primary90 : \"#445566\"\n");
        qml += QStringLiteral("    property color primary80 : \"#778899\"\n");
        for (int i = 0; i < 60; ++i)
            qml += QStringLiteral("    // filler filler filler filler %1\n").arg(i);
        qml += QStringLiteral("  }\n}\n");

        QByteArray data(128, 'J');
        data += utf16le(qml);
        data += QByteArray(128, 'K');

        const QString exePath = tmp.path() + QStringLiteral("/plain.exe");
        {
            QFile f(exePath);
            f.open(QIODevice::WriteOnly);
            f.write(data);
        }

        const ThemeInfo info = ThemeEngine::detectTheme(data, QStringLiteral("qml"));
        check(info.found && info.mode == QLatin1String("plain"), "plain QML theme detected");
        check(info.mult == 2, "detected as UTF-16LE");

        const auto entries = ThemeEngine::parseQmlEntries(info.qml);
        QMap<QString, QString> edits;
        edits[QStringLiteral("primary100")] = QStringLiteral("#654321");

        const auto r = ThemeEngine::patchTheme(exePath, info, entries, edits);
        check(r.ok, "plain patch succeeded");

        QFile f(exePath);
        f.open(QIODevice::ReadOnly);
        const QByteArray patched = f.readAll();
        f.close();
        check(patched.contains(utf16le(QStringLiteral("#654321"))), "new color written in place");
        check(!patched.contains(utf16le(QStringLiteral("#112233"))), "old color replaced");
    }

    // ------------------------------------------------------------------
    // 5) Hex theme (old builds) + QML color names in EXE strings
    // ------------------------------------------------------------------
    {
        std::printf("[theme-hex] literal color values + camelCase QML color names\n");

        QByteArray data(256, 'A');
        data += utf16le(QStringLiteral("#232642")); // NXT Sidebar/Topbar
        data += QByteArray(64, 'B');
        data += utf16le(QStringLiteral("accentNormal"));
        data += utf16le(QStringLiteral(": \"#1A90FF\","));
        data += QByteArray(64, 'C');

        check(ThemeEngine::findQmlColor(data, QStringLiteral("accentNormal"))
                  == QLatin1String("#1A90FF"),
              "findQmlColor reads current value (utf-16le)");

        const QString exePath = tmp.path() + QStringLiteral("/hex.exe");
        {
            QFile f(exePath);
            f.open(QIODevice::WriteOnly);
            f.write(data);
        }

        HexColorItem sidebar;
        sidebar.name = QStringLiteral("Sidebar/Topbar");
        sidebar.currentHex = QStringLiteral("#232642");
        sidebar.color = QStringLiteral("#ff0000");
        sidebar.enabled = true;

        HexColorItem accent;
        accent.name = QStringLiteral("accentNormal");
        accent.currentHex = QStringLiteral("#1A90FF");
        accent.color = QStringLiteral("#00FF00");
        accent.enabled = true;

        const auto r = ThemeEngine::applyHexTheme(exePath, QStringLiteral("NXT"),
                                                  { sidebar }, { accent });
        check(r.ok, "hex theme applied");

        QFile f(exePath);
        f.open(QIODevice::ReadOnly);
        const QByteArray patched = f.readAll();
        f.close();
        check(patched.contains(utf16le(QStringLiteral("#ff0000"))), "hex target replaced");
        check(patched.contains(utf16le(QStringLiteral("#00FF00"))), "QML color name value replaced");
        check(!patched.contains(utf16le(QStringLiteral("#232642"))), "old hex value gone");
    }

    // ------------------------------------------------------------------
    // 6) FPS patcher (export table detour)
    // ------------------------------------------------------------------
    {
        std::printf("[fps] QQuickSlider::setTo export detour + restore\n");

        QByteArray d = makeFakePe(0x1000, 0x4000);
        const quint32 e = 0x1000;
        putLe32(d, e + 24, 1);      // NumberOfNames
        putLe32(d, e + 28, 0x1200); // AddressOfFunctions
        putLe32(d, e + 32, 0x1204); // AddressOfNames
        putLe32(d, e + 36, 0x1208); // AddressOfOrdinals
        const QByteArray nameStr = "?setTo@QQuickSlider@@QEAAXN@Z";
        d.replace(0x1100, nameStr.size() + 1, nameStr + '\0');
        putLe32(d, 0x1200, 0x1300); // func rva
        putLe32(d, 0x1204, 0x1100); // name rva
        putLe16(d, 0x1208, 0);      // ordinal
        d.replace(0x1300, 6, QByteArray::fromHex("48895C2408C3"));

        const QString dllPath = tmp.path() + QStringLiteral("/Qt5QuickTemplates2.dll");
        {
            QFile f(dllPath);
            f.open(QIODevice::WriteOnly);
            f.write(d);
        }
        const QByteArray original = d;

        const auto a = FpsPatcher::analyze(d);
        check(a.ok && a.targets.size() == 1, "setTo export found, state clean");

        const auto r = FpsPatcher::patch(dllPath, 360.0);
        check(r.ok, "fps patch applied");

        QFile f(dllPath);
        f.open(QIODevice::ReadOnly);
        const QByteArray patched = f.readAll();
        f.close();

        check(uchar(patched[0x1300]) == 0xE9, "jmp written at function start");
        const qint64 cave = a.caveOff;
        check(patched.mid(int(cave) + 20, 5) == QByteArray::fromHex("48895C2408"),
              "original prologue preserved in slot");
        check(readDoubleLE(patched, cave + 48) == 240.0, "slot matches only the 240.0 high-FPS maximum");
        check(readDoubleLE(patched, cave + 56) == 360.0, "slot high FPS = 360.0");
        check(patched.mid(cave, 64).count(QByteArray::fromHex("F20F100D")) == 1,
              "detour has one replacement path, no normal-mode rewrite");
        check(patched.mid(cave + 44, 4) == QByteArray::fromHex("46500105"),
              "slot records complete original instruction length");
        check(patched.mid(cave + 64, 16) == QByteArray(16, '\0'),
              "no legacy normal-mode constants written beyond the 64-byte slot");

        const auto rr = FpsPatcher::restore(dllPath);
        QFile f2(dllPath);
        f2.open(QIODevice::ReadOnly);
        check(rr.ok && f2.readAll() == original, "restore returns the original DLL");
        f2.close();
    }

    // ------------------------------------------------------------------
    // 7) Integrity (tamper) bypass
    // ------------------------------------------------------------------
    {
        std::printf("[integrity] test al,al / jz patch site\n");

        QByteArray d = makeFakePe(0, 0x2000);
        d.replace(0x1500, 9, QByteArray::fromHex("E80000000084C07405"));

        const QString exePath = tmp.path() + QStringLiteral("/HD-Player.exe");
        {
            QFile f(exePath);
            f.open(QIODevice::WriteOnly);
            f.write(d);
        }

        const auto a = IntegrityPatcher::analyze(exePath);
        check(a.state == QLatin1String("clean") && a.site == 0x1505, "patch site found (clean)");

        const auto r = IntegrityPatcher::patch(exePath);
        check(r.ok, "integrity bypass patched");

        QFile f(exePath);
        f.open(QIODevice::ReadOnly);
        const QByteArray patched = f.readAll();
        f.close();
        check(uchar(patched[0x1505 + 2]) == 0x90 && uchar(patched[0x1505 + 3]) == 0x90,
              "jz neutralized with NOPs");

        const auto a2 = IntegrityPatcher::analyze(exePath);
        check(a2.state == QLatin1String("patched"), "re-analysis reports patched");
    }

    // ------------------------------------------------------------------
    // 8) SVG find / replace
    // ------------------------------------------------------------------
    {
        std::printf("[svg] embedded SVG find + replace\n");

        QByteArray d("JUNKDATA");
        d += "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24'>"
             "<rect x='0' y='0' width='24' height='24' fill='#ff0000'/></svg>";
        const int firstSize = 111; // exact size of the svg above
        d += QByteArray(64, 'X');
        d += "<svg xmlns='http://www.w3.org/2000/svg'><circle r='5'/></svg>";

        const auto svgs = SvgEngine::findSvgs(d);
        check(svgs.size() == 2, "found 2 embedded SVGs");

        const QString exePath = tmp.path() + QStringLiteral("/svg.exe");
        {
            QFile f(exePath);
            f.open(QIODevice::WriteOnly);
            f.write(d);
        }
        const QString newSvg = tmp.path() + QStringLiteral("/new.svg");
        {
            QFile f(newSvg);
            f.open(QIODevice::WriteOnly);
            f.write("<svg xmlns='http://www.w3.org/2000/svg'><rect/></svg>");
        }

        const auto r = SvgEngine::replaceSvg(exePath, 0, newSvg);
        check(r.ok, "SVG #0 replaced");

        QFile f(exePath);
        f.open(QIODevice::ReadOnly);
        const QByteArray patched = f.readAll();
        f.close();
        check(patched.size() == d.size(), "file size unchanged (null padded)");
        check(patched.contains("<svg xmlns='http://www.w3.org/2000/svg'><rect/></svg>"),
              "new SVG present");
        Q_UNUSED(firstSize)
    }

    // ------------------------------------------------------------------
    // 9) Ad/UI string removals
    // ------------------------------------------------------------------
    {
        std::printf("[extras] ad/UI string removal\n");

        QByteArray d(32, 'Z');
        d += "5.21.151.6303";
        d += QByteArray(32, 'W');
        d += utf16le(QStringLiteral("SidebarMacro.svg"));
        d += QByteArray(32, 'V');

        const QString exePath = tmp.path() + QStringLiteral("/extras.exe");
        {
            QFile f(exePath);
            f.open(QIODevice::WriteOnly);
            f.write(d);
        }

        const auto rr = ExtrasPatcher::applyRemovals(exePath,
                                                     { QStringLiteral("Sidebar Macro") });
        check(rr.ok, "removals applied");

        QFile f(exePath);
        f.open(QIODevice::ReadOnly);
        const QByteArray patched = f.readAll();
        f.close();
        check(!patched.contains(utf16le(QStringLiteral("SidebarMacro.svg"))),
              "sidebar macro string nulled");

        // The controls that keep the window usable are no longer offered as
        // removals: Back, Home, Recent, the logo and fullscreen stay.
        const QStringList names = ExtrasPatcher::removalNames();
        check(!names.contains(QStringLiteral("Titlebar Back"))
                  && !names.contains(QStringLiteral("Titlebar Home"))
                  && !names.contains(QStringLiteral("Titlebar Recents"))
                  && !names.contains(QStringLiteral("Titlebar Logo"))
                  && !names.contains(QStringLiteral("Sidebar Fullscreen")),
              "Back/Home/Recent, the logo and fullscreen are not removal rows");
    }

    // ------------------------------------------------------------------
    // 10) Boot video patcher
    // ------------------------------------------------------------------
    {
        std::printf("[boot] compressed boot QML rewrite\n");

        QString plr = QStringLiteral("ApplicationWindow{id:iAppwin;Loader{id:iBootLoader}}"
                                     "iBootProgressComponent{}");
        QString boot = QStringLiteral("Item {\r\n    Image{id: iBootImage}\r\n"
                                      "    // bootBackgroundImage fSetBootPromotion\r\n");
        boot += QStringLiteral("    Loader{id:pv;source:\"qrc:/qml/BootVideo.qml\";"
                               "anchors.fill:parent;asynchronous:true;active:true;"
                               "onStatusChanged:{if(status===Component.Ready&&item)"
                               "{item.anchors.fill=pv;item.z=-2;item.play()}};"
                               "onLoadedChanged:{}}\r\n");
        boot += QStringLiteral("    Text {\r\n        id: iBootLoadingText\r\n"
                               "        visible: uiBackend.oem === \"msi5\" ? false : true\r\n"
                               "    }\r\n"
                               "    function f(){iBootImage.visible = true;"
                               "pBootImageButtonVisible = true;pBootGridVisible = true;"
                               "iBootLoadingText.visible = false}\r\n");
        // Incompressible padding so both records pass the size gates and the
        // size budget behaves like a real build.
        quint32 seed = 0xB007B00B;
        auto noise = [&seed](int n) {
            QString s;
            static const char alpha[] = "abcdefghijklmnopqrstuvwxyz0123456789";
            for (int i = 0; i < n; ++i) {
                seed = seed * 1664525u + 1013904223u;
                s += QLatin1Char(alpha[(seed >> 16) % 36]);
            }
            return s;
        };
        plr += QStringLiteral("//") + noise(2000);
        boot += QStringLiteral("//") + noise(2000);

        QByteArray data(64, '\0');
        const QByteArray plrRec = qtZlibRecord(plr.toUtf8());
        data += plrRec;
        data += QByteArray(16, '\0');
        const qint64 bootEntry = data.size();
        const QByteArray bootRec = qtZlibRecord(boot.toUtf8());
        data += bootRec;
        data += QByteArray(512, '\0');

        const QString exePath = tmp.path() + QStringLiteral("/boot.exe");
        {
            QFile f(exePath);
            f.open(QIODevice::WriteOnly);
            f.write(data);
        }

        const auto r = BootVideoPatcher::patch(exePath);
        check(!r.ok && r.msg.contains(QLatin1String("not a player executable")),
              "a blob that is not a PE is refused before anything is written");
        QFile f(exePath); f.open(QIODevice::ReadOnly);
        check(f.readAll() == data, "the refused file is left unchanged");
    }

    // ------------------------------------------------------------------
    // N) Discord sign-in gate (the Premium-Optimizer login system): session
    //    envelope, PKCE math, config parsing and the Backend gate authority.
    // ------------------------------------------------------------------
    {
        std::printf("[auth] Discord gate: session, PKCE, config, gate authority\n");

        // ---- session envelope: sign, verify, tamper, clear ----
        QTemporaryDir authTmp;
        check(authTmp.isValid(), "temp dir for the session store");
        SessionStore store(authTmp.path());
        check(!store.hasSaved(), "fresh store starts signed out");

        QVariantMap identity;
        identity.insert(QStringLiteral("Id"), QStringLiteral("42"));
        identity.insert(QStringLiteral("Username"), QStringLiteral("mrpcgamer"));
        identity.insert(QStringLiteral("DisplayName"), QStringLiteral("MR.PC GAMER"));
        QString saveError;
        check(store.saveIdentity(identity, &saveError), "session saved");
        {
            SessionStore reloaded(authTmp.path());
            check(reloaded.hasSaved(), "session verifies after reload");
            check(reloaded.identity().value(QStringLiteral("Id")).toString()
                      == QStringLiteral("42"),
                  "identity round-trips");
        }
        {
            QFile f(authTmp.path() + QStringLiteral("/session.json"));
            f.open(QIODevice::ReadOnly);
            QByteArray raw = f.readAll();
            f.close();
            raw.replace("mrpcgamer", "attacker");
            f.open(QIODevice::WriteOnly | QIODevice::Truncate);
            f.write(raw);
            f.close();
            SessionStore tampered(authTmp.path());
            check(!tampered.hasSaved(), "edited session file is rejected");
        }

        // ---- token: purpose-bound, tamper-evident ----
        const QString token = store.issueToken(QByteArrayLiteral("payload"),
                                               QStringLiteral("test"));
        check(store.verifyToken(token, QStringLiteral("test")) == QByteArrayLiteral("payload"),
              "token verifies for its purpose");
        check(store.verifyToken(token, QStringLiteral("other")).isEmpty(),
              "token is bound to its purpose");
        check(store.verifyToken(token + QLatin1Char('x'), QStringLiteral("test")).isEmpty(),
              "tampered token is rejected");

        // ---- PKCE: verifier -> S256 challenge ----
        const QString verifier = AuthManager::pkceVerifier();
        check(verifier.length() >= 43, "PKCE verifier is base64url of 32 bytes");
        const QByteArray expected =
            QCryptographicHash::hash(verifier.toUtf8(), QCryptographicHash::Sha256)
                .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
        check(AuthManager::pkceChallenge(verifier) == QString::fromLatin1(expected),
              "PKCE challenge is S256 of the verifier");
        check(AuthManager::randomHex(16).length() == 32, "state is 16 hex bytes");

        // ---- readCode (callback URL and bare code) ----
        QString code, state;
        AuthManager::readCode(QStringLiteral("http://127.0.0.1:43713/auth/discord/callback"
                                             "?code=abc123&state=deadbeef"),
                              code, state);
        check(code == QLatin1String("abc123") && state == QLatin1String("deadbeef"),
              "callback URL parses into code + state");
        AuthManager::readCode(QStringLiteral("only-a-code"), code, state);
        check(code == QLatin1String("only-a-code") && state.isEmpty(), "bare code accepted");

        // ---- config from env (restored afterwards) ----
        const QByteArray oldClient = qgetenv("BSIC_DISCORD_CLIENT_ID");
        const QByteArray oldAllow = qgetenv("BSIC_DISCORD_ALLOW_USERS");
        const QByteArray oldPort = qgetenv("BSIC_DISCORD_REDIRECT_PORT");
        const QByteArray oldBlob = qgetenv("ZLP_AUTH_JSON");
        qputenv("BSIC_DISCORD_CLIENT_ID", QByteArrayLiteral("fake-client-id"));
        qputenv("BSIC_DISCORD_ALLOW_USERS", QByteArrayLiteral("111, 222;333"));
        qputenv("BSIC_DISCORD_REDIRECT_PORT", QByteArrayLiteral("43713"));
        {
            const AuthManager::Config cfg = AuthManager::loadConfig();
            check(cfg.configured, "client id configures Discord sign-in");
            check(cfg.allowUsers.size() == 3 && cfg.allowUsers.contains(QStringLiteral("222")),
                  "dev allowlist parses comma/semicolon lists");
            check(cfg.redirectUri
                      == QLatin1String("http://127.0.0.1:43713/auth/discord/callback"),
                  "redirect URI is the portal string (127.0.0.1, not localhost)");
            check(!cfg.setupHint.contains(QStringLiteral("not configured")),
                  "configured builds get a source line instead of a setup hint");
        }

        // ---- the whole auth.json as one blob: $ZLP_AUTH_JSON ----
        // (the name the CI secret carries; wins over any file/embedded config,
        // so this stays deterministic even when the build embeds a real blob.
        // Per-field BSIC_DISCORD_* env vars still override it, so clear those
        // for the duration of this block.)
        qunsetenv("BSIC_DISCORD_REDIRECT_PORT");
        qunsetenv("BSIC_DISCORD_CLIENT_ID");
        qunsetenv("BSIC_DISCORD_ALLOW_USERS");
        qputenv("ZLP_AUTH_JSON",
                QByteArrayLiteral("{\"discord\":{\"clientId\":\"zip-client\","
                                  "\"allowUsers\":[\"777\"]}}"));
        {
            const AuthManager::Config cfg = AuthManager::loadConfig();
            check(cfg.configured && cfg.clientId == QLatin1String("zip-client"),
                  "the ZLP_AUTH_JSON blob is the whole auth.json config");
            check(cfg.allowUsers == QStringList { QStringLiteral("777") },
                  "the blob's allowlist wins over files and other sources");
            check(cfg.redirectUri
                      == QLatin1String("http://127.0.0.1:43713/auth/discord/callback"),
                  "a blob without redirectPort falls back to the registered 43713 default");
            check(cfg.sourceFile.contains(QLatin1String("ZLP_AUTH_JSON")),
                  "the setup line names the blob as the config source");
        }
        {
            // Build-time embed: the release workflow bakes the same blob into
            // the EXE from the ZLP_AUTH_JSON secret. Whatever was baked must
            // parse as an auth.json.
            const QByteArray embedded = AuthManager::embeddedAuthJson();
            if (!embedded.isEmpty()) {
                const AuthManager::Config embeddedCfg = AuthManager::loadConfig();
                check(embeddedCfg.configured && !embeddedCfg.clientId.isEmpty(),
                      "the build-time embedded ZLP_AUTH_JSON blob parses and configures sign-in");
            }
        }

        // ---- server role gate (Premium-Optimizer auth.json shape) ----
        // guildId + roles + requiredTier must grant access without any
        // allowUsers entry, and a scope mangled into markdown by a chat app
        // must be repaired before it reaches Discord.
        qputenv("ZLP_AUTH_JSON",
                QByteArrayLiteral("{\"discord\":{"
                                  "\"clientId\":\"role-client\","
                                  "\"scope\":\"identify [guilds.members.read]"
                                  "(http://guilds.members.read)\","
                                  "\"guildId\":\"1380048834876674108\","
                                  "\"requiredTier\":\"Dev\","
                                  "\"roles\":{\"1488215210010611932\":"
                                  "{\"tier\":\"Dev\",\"label\":\"Dev\"}},"
                                  "\"inviteUrl\":\"[https://discord.gg/5qkKPRZkWa]"
                                  "(https://discord.gg/5qkKPRZkWa)\""
                                  "}}"));
        {
            const AuthManager::Config cfg = AuthManager::loadConfig();
            check(cfg.configured, "a role-gated blob configures sign-in with no allowlist");
            check(cfg.roleGateConfigured(), "guildId + roles enable the role gate");
            check(cfg.guildId == QLatin1String("1380048834876674108"),
                  "discord.guildId parses");
            check(cfg.requiredTier == QLatin1String("Dev"),
                  "discord.requiredTier parses");
            check(cfg.roles.value(QStringLiteral("1488215210010611932")) == QLatin1String("Dev"),
                  "discord.roles parses the {tier,label} object form");
            check(cfg.scope == QLatin1String("identify guilds.members.read"),
                  "a markdown-mangled scope is repaired to space-separated scopes");
            check(cfg.inviteUrl == QLatin1String("https://discord.gg/5qkKPRZkWa"),
                  "a markdown-mangled invite URL is repaired");
            check(!cfg.setupHint.contains(QStringLiteral("No dev access gate")),
                  "the role gate suppresses the no-gate setup hint");

            QString tier;
            QString roleId;
            check(AuthManager::tierForRoles(cfg,
                                            QStringList { QStringLiteral("1488215210010611932") },
                                            &tier, &roleId)
                      && tier == QLatin1String("Dev")
                      && roleId == QLatin1String("1488215210010611932"),
                  "holding a listed role grants its tier");
            check(!AuthManager::tierForRoles(cfg,
                                             QStringList { QStringLiteral("999999999999999999") }),
                  "a role outside discord.roles is rejected");

            AuthManager::Config wrongTier = cfg;
            wrongTier.requiredTier = QStringLiteral("Mod");
            check(!AuthManager::tierForRoles(
                      wrongTier, QStringList { QStringLiteral("1488215210010611932") }),
                  "a role below discord.requiredTier is rejected");

            AuthManager::Config anyRole = cfg;
            anyRole.requiredTier.clear();
            check(AuthManager::tierForRoles(
                      anyRole, QStringList { QStringLiteral("1488215210010611932") }),
                  "an empty requiredTier accepts any listed role");

            AuthManager::Config idOnly = cfg;
            idOnly.roles.clear();
            idOnly.roles.insert(QStringLiteral("1488215210010611932"), QString());
            check(AuthManager::tierForRoles(
                      idOnly, QStringList { QStringLiteral("1488215210010611932") }),
                  "a role listed without a tier is an explicit grant");
        }

        // A secret pasted as a ```json fence (and a numeric client id) still
        // has to parse - both are common copy/paste accidents.
        qputenv("ZLP_AUTH_JSON",
                QByteArrayLiteral("```json\n{\"discord\":{\"clientId\":123456789,"
                                  "\"allowUsers\":[\"555\"]}}\n```"));
        {
            const AuthManager::Config cfg = AuthManager::loadConfig();
            check(cfg.configured && cfg.clientId == QLatin1String("123456789"),
                  "a code-fenced blob with a numeric clientId still parses");
        }
        qunsetenv("ZLP_AUTH_JSON");
        // The gate blocks below were written against the env config set above
        // (the staged "111" session must stay on the allowlist for the
        // one-click continue check), so put that env back.
        qputenv("BSIC_DISCORD_CLIENT_ID", QByteArrayLiteral("fake-client-id"));
        qputenv("BSIC_DISCORD_ALLOW_USERS", QByteArrayLiteral("111, 222;333"));
        qputenv("BSIC_DISCORD_REDIRECT_PORT", QByteArrayLiteral("43713"));

        // ---- gate authority: Backend refuses while the gate is closed ----
        {
            SettingsManager gateSettings;
            Backend gated(&gateSettings);
            AuthManager gate;
            gated.setAuthManager(&gate);
            check(!gate.isAuthorized(), "saved sign-in alone never opens the gate");
            const QVariantMap refused = gated.patchThemeQml();
            check(!refused.value(QStringLiteral("ok")).toBool()
                      && refused.value(QStringLiteral("error"))
                             .toString()
                             .contains(QStringLiteral("Sign in with Discord")),
                  "Backend refuses patching while the Discord gate is closed");
            Backend ungated(&gateSettings);
            check(ungated.patchThemeQml().value(QStringLiteral("error")).toString()
                      != QStringLiteral("Sign in with Discord to unlock BSI Customizer."),
                  "Backend without an AuthManager stays ungated (tests/scripts)");
        }

        // ---- one-click continue over a saved, allowlisted session ----
        {
            SessionStore staged; // the store the gate itself reads
            QVariantMap mine;
            mine.insert(QStringLiteral("Id"), QStringLiteral("111"));
            mine.insert(QStringLiteral("Username"), QStringLiteral("mrpcgamer"));
            mine.insert(QStringLiteral("DisplayName"), QStringLiteral("MR.PC GAMER"));
            check(staged.saveIdentity(mine, &saveError), "gate session staged");
            AuthManager gate;
            check(gate.hasSavedSession(), "gate offers the one-click continue");
            check(gate.continueSaved(), "allowlisted saved sign-in opens the gate");
            check(gate.isAuthorized(), "gate is open after continue");
            gate.signOut();
            check(!gate.hasSavedSession(), "sign-out drops the session");
        }

        if (oldClient.isNull())
            qunsetenv("BSIC_DISCORD_CLIENT_ID");
        else
            qputenv("BSIC_DISCORD_CLIENT_ID", oldClient);
        if (oldAllow.isNull())
            qunsetenv("BSIC_DISCORD_ALLOW_USERS");
        else
            qputenv("BSIC_DISCORD_ALLOW_USERS", oldAllow);
        if (oldPort.isNull())
            qunsetenv("BSIC_DISCORD_REDIRECT_PORT");
        else
            qputenv("BSIC_DISCORD_REDIRECT_PORT", oldPort);
        if (oldBlob.isNull())
            qunsetenv("ZLP_AUTH_JSON");
        else
            qputenv("ZLP_AUTH_JSON", oldBlob);
    }

    g_failures += runPatchRegressionTests();

    std::printf("========================\n");
    std::printf("%s (%d failure(s))\n",
                g_failures == 0 ? "ALL TESTS PASSED" : "SELF-TEST FAILED",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
