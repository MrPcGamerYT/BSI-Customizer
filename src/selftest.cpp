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
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdio>

#include "core/Backend.h"
#include "core/BootVideoPatcher.h"
#include "core/ByteUtils.h"
#include "core/ExtrasPatcher.h"
#include "core/FpsPatcher.h"
#include "core/IntegrityPatcher.h"
#include "core/SettingsManager.h"
#include "core/SvgEngine.h"
#include "core/SysInfo.h"
#include "core/ThemeEngine.h"
#include "core/ThemeManager.h"
#include "core/ZlibUtils.h"

using namespace ByteUtils;

namespace {

int g_failures = 0;

void check(bool cond, const char *what)
{
    if (cond) {
        std::printf("  PASS  %s\n", what);
    } else {
        std::printf("  FAIL  %s\n", what);
        ++g_failures;
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

// Writes a qrc name record + file node that point at a data record, using the
// identity RVA mapping of makeFakePe (base 0x1000).
void writeQrcNode(QByteArray &d, qint64 entryOff, qint64 nameRec, qint64 nodeOff)
{
    const QString name = QStringLiteral("UiThemeObject.qml");
    putBe16(d, nameRec, quint16(name.size()));
    putBe32(d, nameRec + 2, 0xDEADBEEF);
    d.replace(int(nameRec + 6), utf16be(name).size(), utf16be(name));

    const qint64 nameBase = 0x1040;
    const qint64 dataBase = 0x1000;
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

    // ------------------------------------------------------------------
    // 1) App UI theme chain (C++ NOTIFY -> QML bindings)
    // ------------------------------------------------------------------
    {
        std::printf("[ui] C++ NOTIFY -> context property -> Theme singleton -> QML bindings\n");

        SettingsManager settings;
        ThemeManager themes;
        Backend backend(&settings);
        SysInfo sysinfo;

        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("themes"), &themes);
        engine.rootContext()->setContextProperty(QStringLiteral("settings"), &settings);
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("sysinfo"), &sysinfo);
        engine.load(QUrl(QStringLiteral("qrc:/qt/qml/BSICustomizer/src/qml/Main.qml")));

        QObject *root = engine.rootObjects().value(0, nullptr);
        check(root != nullptr, "Main.qml loaded without errors");

        if (root) {
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
    // 3) QML theme (zlib) — relocation when the slot is too small
    // ------------------------------------------------------------------
    {
        std::printf("[theme-zlib] relocation into zero cave when compressed theme overflows\n");

        const QString origQml = makeThemeQml(colorList(QStringLiteral("#000000")));
        const QByteArray rec = qtZlibRecord(origQml.toLatin1());

        QByteArray pe = makeFakePe(0, 0x20000);
        const qint64 entryOff = 0x2000;
        pe.replace(int(entryOff), rec.size(), rec);
        pe[int(entryOff + rec.size())] = char(0xFF); // no zero slack -> forces relocation
        const qint64 nameRec = ((entryOff + rec.size() + 1) + 3) & ~qint64(3);
        const qint64 nodeOff = ((nameRec + 6 + 34) + 3) & ~qint64(3);
        writeQrcNode(pe, entryOff, nameRec, nodeOff);

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

        const QString dllPath = tmp.path() + QStringLiteral("/libOpenglRender.dll");
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
        check(patched.mid(int(cave) + 38, 5) == QByteArray::fromHex("48895C2408"),
              "original prologue preserved in slot");
        check(readDoubleLE(patched, cave + 48) == 240.0, "slot max constant = 240.0");
        check(readDoubleLE(patched, cave + 64) == 360.0, "slot high FPS = 360.0");
        check(readDoubleLE(patched, cave + 72) == 60.0, "slot low FPS = 60.0");

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
    // 9) Version spoof + string removals
    // ------------------------------------------------------------------
    {
        std::printf("[extras] version spoof + ad/string removal\n");

        QByteArray d(32, 'Z');
        d += "5.21.151.6303";
        d += QByteArray(32, 'Y');
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

        const auto vers = ExtrasPatcher::findVersions(d);
        bool hasVersion = false;
        for (const QVariantMap &m : vers)
            hasVersion |= (m.value(QStringLiteral("version")).toString()
                           == QLatin1String("5.21.151.6303"));
        check(hasVersion, "version string found");

        const auto rv = ExtrasPatcher::applyVersion(exePath, QStringLiteral("5.21.151.6303"),
                                                    QStringLiteral("5.21.151.9999"));
        check(rv.ok, "version spoofed");

        const auto rr = ExtrasPatcher::applyRemovals(exePath,
                                                     { QStringLiteral("Sidebar Macro") });
        check(rr.ok, "removals applied");

        QFile f(exePath);
        f.open(QIODevice::ReadOnly);
        const QByteArray patched = f.readAll();
        f.close();
        check(patched.contains("5.21.151.9999"), "new version present");
        check(!patched.contains("5.21.151.6303"), "old version gone");
        check(!patched.contains(utf16le(QStringLiteral("SidebarMacro.svg"))),
              "sidebar macro string nulled");
    }

    // ------------------------------------------------------------------
    // 10) Boot video patcher
    // ------------------------------------------------------------------
    {
        std::printf("[boot] compressed boot QML rewrite\n");

        QString plr = QStringLiteral("ApplicationWindow{id:iAppwin;Loader{id:iBootLoader}"
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
        check(r.ok, "boot video patched");

        QFile f(exePath);
        f.open(QIODevice::ReadOnly);
        const QByteArray patched = f.readAll();
        f.close();

        const quint32 stored = be32(patched, bootEntry);
        const quint32 usize = be32(patched, bootEntry + 4);
        const QByteArray newQml =
            ZlibUtils::tryDecompress(patched.mid(bootEntry + 8, stored - 4), usize);
        const QString text = QString::fromUtf8(newQml);
        check(text.contains(QLatin1String("file:///Promo.mp4")), "Promo.mp4 loader injected");
        check(text.contains(QLatin1String("iBootImage.visible = false")), "boot image hidden");
        check(!text.contains(QLatin1String("BootVideo.qml")), "stock BootVideo.qml removed");
    }

    std::printf("========================\n");
    std::printf("%s (%d failure(s))\n",
                g_failures == 0 ? "ALL TESTS PASSED" : "SELF-TEST FAILED",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
