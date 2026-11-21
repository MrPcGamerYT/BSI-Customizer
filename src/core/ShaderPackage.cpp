#include "ShaderPackage.h"
#include "ByteUtils.h"

#include <QCryptographicHash>
#include <QRegularExpression>
#include <QSet>
#include <QVersionNumber>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

namespace ShaderPackage {
namespace {
using U = quint64;
constexpr U fileLimit = 128ULL * 1024 * 1024;
constexpr U totalLimit = 512ULL * 1024 * 1024;
constexpr int moduleLimit = 64;
[[noreturn]] void refuse(const QString &s) { throw std::runtime_error(s.toUtf8().constData()); }
void need(bool yes, const QString &s) { if (!yes) refuse(s); }
void need(bool yes, const char *s) { if (!yes) refuse(QString::fromLatin1(s)); }
bool range(U offset, U length, U size) { return offset <= size && length <= size - offset; }
bool basename(const QString &s)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9][a-z0-9_.-]{0,119}\\.dll$"));
    return pattern.match(s).hasMatch() && !s.contains(QLatin1String(".."));
}
struct Section { U va, size, raw, length, flags; };
struct Symbol {
    QString module;
    QByteArray name;
    quint16 ordinal = 0;
    bool byOrdinal = false;
};
struct Export { U rva = 0; QByteArray forward; };

// Independent of the more permissive resource/FPS RVA helpers: a package must
// never resolve a header, overlapping section or zero-fill RVA as native data.
class Image {
public:
    const QByteArray data; // implicitly shared ownership, including temporary/map-value inputs
    U opt = 0, headerSize = 0;
    std::vector<Section> sections;
    QList<Symbol> imports;
    QStringList modules;
    QMap<QByteArray, Export> exports;
    QMap<quint32, Export> ordinals;
    U stringWork = 0;

    U read(U at, unsigned width) const
    {
        need(range(at, width, U(data.size())), "Truncated PE field.");
        U value = 0;
        for (unsigned i = 0; i < width; ++i) value |= U(uchar(data.at(int(at + i)))) << (8 * i);
        return value;
    }
    U raw(U rva, U length) const
    {
        need(rva && range(rva, length, 0x100000000ULL), "Invalid PE RVA.");
        for (const auto &s : sections)
            if (rva >= s.va && range(rva - s.va, length, s.length)) {
                need((s.flags & 0x40000000) && range(s.raw + rva - s.va, length, U(data.size())),
                     "PE data is not in a readable file-backed section.");
                return s.raw + rva - s.va;
            }
        refuse(QStringLiteral("PE RVA is outside file-backed sections."));
    }
    U flags(U rva) const
    {
        for (const auto &s : sections)
            if (rva >= s.va && rva - s.va < qMax(s.size, s.length)) return s.flags;
        return 0;
    }
    QByteArray string(U rva, U maximum = 4096)
    {
        QByteArray value;
        for (U i = 0; i < maximum; ++i) {
            need(++stringWork <= 16 * 1024 * 1024, "PE strings exceed the parser work budget.");
            const uchar c = uchar(data.at(int(raw(rva + i, 1))));
            if (!c) { need(!value.isEmpty(), "Empty PE string."); return value; }
            need(c >= 32 && c < 127, "Non-ASCII PE string.");
            value += char(c);
        }
        refuse(QStringLiteral("Unterminated PE string."));
    }
    std::pair<U, U> directory(int index) const
    {
        const U at = opt + 112 + U(index) * 8;
        const U rva = read(at, 4), size = read(at + 4, 4);
        need(bool(rva) == bool(size), "Incomplete PE data directory.");
        if (rva) raw(rva, size);
        return {rva, size};
    }
    explicit Image(const QByteArray &bytes, bool dll) : data(bytes)
    {
        need(U(data.size()) >= 0x100 && U(data.size()) <= fileLimit && data.startsWith("MZ"),
             "Expected a PE image of at most 128 MiB.");
        const U pe = read(0x3c, 4);
        need(pe >= 0x40 && read(pe, 4) == 0x4550, "Missing PE signature.");
        const U count = read(pe + 6, 2), os = read(pe + 20, 2), characteristics = read(pe + 22, 2);
        opt = pe + 24;
        need(read(pe + 4, 2) == 0x8664 && (characteristics & 2) && bool(characteristics & 0x2000) == dll,
             "A native AMD64 player and DLL are required; not PE32, ARM64 or a renamed EXE.");
        need(count && count <= 96 && os >= 240 && range(opt, os + count * 40, U(data.size()))
                 && read(opt, 2) == 0x20b && read(opt + 108, 4) == 16,
             "Invalid PE32+ headers or section table.");
        headerSize = read(opt + 60, 4);
        const U imageSize = read(opt + 56, 4), sa = read(opt + 32, 4), fa = read(opt + 36, 4);
        need(fa >= 512 && fa <= 65536 && !(fa & (fa - 1)) && sa >= 4096 && sa >= fa
                 && sa <= 0x1000000 && !(sa & (sa - 1)) && headerSize % fa == 0
                 && headerSize >= opt + os + count * 40 && headerSize <= U(data.size())
                 && imageSize && imageSize % sa == 0, "Invalid PE alignment or image bounds.");
        for (U i = 0; i < count; ++i) {
            const U at = opt + os + i * 40;
            Section s{read(at + 12, 4), read(at + 8, 4), read(at + 20, 4),
                      read(at + 16, 4), read(at + 36, 4)};
            const U span = qMax(s.size, s.length);
            need(s.va >= ((headerSize + sa - 1) / sa) * sa && s.va % sa == 0
                     && range(s.va, span, imageSize), "Invalid PE virtual section bounds.");
            need(!s.length || (s.raw >= headerSize && s.raw % fa == 0 && s.length % fa == 0
                                  && range(s.raw, s.length, U(data.size()))), "Invalid PE raw section bounds.");
            for (const auto &p : sections) {
                need(s.va >= p.va + qMax(p.size, p.length) || p.va >= s.va + span,
                     "Overlapping PE virtual sections.");
                need(!s.length || !p.length || s.raw >= p.raw + p.length || p.raw >= s.raw + s.length,
                     "Overlapping PE raw sections.");
            }
            sections.push_back(s);
        }
        const U entry = read(opt + 16, 4);
        if (entry || !dll) { raw(entry, 1); need(flags(entry) & 0x20000000, "Invalid PE entry point."); }
        readImports();
    }
    void readImports()
    {
        U work = 0;
        const U base = read(opt + 24, 8);
        for (int index : {1, 13}) {
            const auto d = directory(index);
            if (!d.first) continue;
            const U width = index == 1 ? 20 : 32;
            need(d.second >= width && d.second <= 4096 * width, "Invalid import directory size.");
            bool ended = false;
            for (U i = 0; i < d.second / width; ++i) {
                const U at = raw(d.first + i * width, width);
                bool zero = true;
                for (U j = 0; j < width; j += 4) zero &= read(at + j, 4) == 0;
                if (zero) { ended = true; break; }
                const bool rvas = index == 1 || (read(at, 4) & 1);
                if (index == 13) need((read(at, 4) & ~U(1)) == 0, "Unknown delay-import attributes.");
                auto address = [&](U value) {
                    if (!rvas && value) { need(value >= base, "Invalid VA-based delay import."); value -= base; }
                    return value;
                };
                const U name = address(read(at + (index == 1 ? 12 : 4), 4));
                const U iat = address(read(at + (index == 1 ? 16 : 12), 4));
                U table = address(read(at + (index == 1 ? 0 : 16), 4));
                if (!table) table = iat;
                const QString module = QString::fromLatin1(string(name, 260)).toLower();
                need((basename(module) || systemModule(module)) && iat && table, "Invalid import module or thunk address.");
                if (!modules.contains(module)) modules << module;
                bool terminated = false;
                for (U j = 0; j < 65536; ++j) {
                    need(++work <= 131072, "PE import thunks exceed the parser work budget.");
                    U thunk = read(raw(table + j * 8, 8), 8);
                    raw(iat + j * 8, 8);
                    if (!thunk) { terminated = true; break; }
                    Symbol symbol; symbol.module = module;
                    if (thunk & (1ULL << 63)) {
                        need((thunk & 0x7fffffffffff0000ULL) == 0, "Invalid ordinal import.");
                        symbol.byOrdinal = true; symbol.ordinal = quint16(thunk);
                    } else {
                        thunk = address(thunk);
                        raw(thunk, 2); symbol.name = string(thunk + 2);
                    }
                    imports << symbol;
                }
                need(terminated, "Unterminated PE import thunk table.");
            }
            need(ended, "Unterminated PE import directory.");
        }
    }
    void readExports()
    {
        const auto d = directory(0);
        need(d.first && d.second >= 40, "Missing PE export directory.");
        const U at = raw(d.first, 40);
        const U base = read(at + 16, 4), functions = read(at + 20, 4), names = read(at + 24, 4);
        need(functions && functions <= 65536 && names <= 65536 && base + functions <= 0x100000000ULL,
             "Invalid PE export counts.");
        const U ft = raw(read(at + 28, 4), functions * 4);
        U nt = 0, ot = 0;
        if (names) { nt = raw(read(at + 32, 4), names * 4); ot = raw(read(at + 36, 4), names * 2); }
        for (U i = 0; i < functions; ++i) {
            Export value; value.rva = read(ft + i * 4, 4);
            if (value.rva >= d.first && value.rva - d.first < d.second) {
                value.forward = string(value.rva, qMin<U>(4096, d.first + d.second - value.rva));
            } else if (value.rva) {
                const U f = flags(value.rva);
                need(f & 0x40000000, "Export address is outside readable mapped sections.");
                // A runtime may legitimately export zero-initialized data.
                // Executable exports still require real file-backed code.
                if (f & 0x20000000) raw(value.rva, 1);
            }
            ordinals.insert(quint32(base + i), value);
        }
        QByteArray previous;
        for (U i = 0; i < names; ++i) {
            const auto name = string(read(nt + i * 4, 4));
            const U ordinal = read(ot + i * 2, 2);
            need((i == 0 || previous < name) && ordinal < functions, "Unsorted/duplicate export name or invalid ordinal.");
            previous = name; exports.insert(name, ordinals.value(quint32(base + ordinal)));
        }
    }
    Export exported(const Symbol &s) const
    {
        const auto value = s.byOrdinal ? ordinals.value(s.ordinal) : exports.value(s.name);
        need(value.rva, QStringLiteral("Missing import %1!%2.").arg(s.module,
             s.byOrdinal ? QStringLiteral("#%1").arg(s.ordinal) : QString::fromLatin1(s.name)));
        return value;
    }
    Export nativeExport(const QByteArray &name, bool executable) const
    {
        Symbol s; s.name = name;
        const auto value = exported(s);
        need(value.forward.isEmpty(), "A required plugin export is forwarded.");
        raw(value.rva, 1);
        const U f = flags(value.rva);
        need(bool(f & 0x20000000) == executable && (executable || !(f & 0x80000000)),
             "The descriptor must be read-only data and Qt entry points must be native code.");
        return value;
    }
    QString version()
    {
        const auto d = directory(2);
        need(d.first && d.second >= 16 && d.second <= 16 * 1024 * 1024, "Missing/bad Qt version resource directory.");
        auto at = [&](U offset, U size) { need(range(offset, size, d.second), "Version resource escapes its directory."); return raw(d.first + offset, size); };
        auto entries = [&](U offset) {
            const U p = at(offset, 16), count = read(p + 12, 2) + read(p + 14, 2);
            need(count && count <= 1024, "Invalid version resource entry count.");
            at(offset + 16, count * 8); return count;
        };
        U type = 0;
        const U roots = entries(0);
        for (U i = 0; i < roots; ++i) {
            const U p = at(16 + i * 8, 8);
            if (read(p, 4) != 16) continue;
            need(!type && (read(p + 4, 4) & 0x80000000), "Ambiguous Qt RT_VERSION resource.");
            type = read(p + 4, 4) & 0x7fffffff;
        }
        need(type, "Qt DLL lacks RT_VERSION; editable display strings cannot prove its version.");
        QString result;
        U leaves = 0;
        const U ids = entries(type);
        for (U i = 0; i < ids; ++i) {
            const U p = at(type + 16 + i * 8, 8), child = read(p + 4, 4);
            need(read(p, 4) < 0x80000000 && (child & 0x80000000), "Unsupported version resource identity.");
            const U langs = child & 0x7fffffff, count = entries(langs);
            for (U j = 0; j < count; ++j) {
                need(++leaves <= 64, "Too many Qt version resource languages.");
                const U e = at(langs + 16 + j * 8, 8), leaf = read(e + 4, 4);
                need(read(e, 4) < 0x80000000 && !(leaf & 0x80000000), "Invalid version resource leaf.");
                const U info = at(leaf, 16), size = read(info + 4, 4), v = raw(read(info, 4), size);
                need(size >= 92 && size <= 1024 * 1024 && read(v, 2) <= size && read(v, 2) >= 92
                         && read(v + 2, 2) == 52 && read(v + 4, 2) == 0, "Invalid VS_VERSION_INFO header.");
                const QByteArray key = QByteArray::fromHex("560053005f00560045005200530049004f004e005f0049004e0046004f000000");
                need(data.mid(int(v + 6), key.size()) == key && read(v + 40, 4) == 0xfeef04bd
                         && read(v + 44, 4) == 0x10000, "Invalid VS_FIXEDFILEINFO signature.");
                const U ms = read(v + 48, 4), ls = read(v + 52, 4);
                need(ms == read(v + 56, 4) && ls == read(v + 60, 4) && (ls & 0xffff) == 0
                         && read(v + 68, 4) == 0 && read(v + 76, 4) == 2,
                     "Qt must be a release DLL with matching fixed file/product versions, not a debug/private build.");
                const QString version = QStringLiteral("%1.%2.%3").arg(ms >> 16).arg(ms & 0xffff).arg(ls >> 16);
                need(result.isEmpty() || result == version, "Conflicting Qt fixed-version resources.");
                result = version;
            }
        }
        need(!result.isEmpty(), "Qt has no fixed version value.");
        return result;
    }
};

struct Contract { QString version; bool publicApi = false; };
QVersionNumber qtVersion(const QString &text)
{
    static const QRegularExpression format(QStringLiteral("^[56]\\.(?:0|[1-9][0-9]{0,2})\\.(?:0|[1-9][0-9]{0,2})$"));
    if (!format.match(text).hasMatch()) return {};
    const auto version = QVersionNumber::fromString(text);
    if (version.minorVersion() > 255 || version.microVersion() > 255) return {};
    return version;
}
bool targets(const Contract &contract, const QString &runtime)
{
    const auto built = qtVersion(contract.version), host = qtVersion(runtime);
    if (built.isNull() || host.isNull() || built.majorVersion() != host.majorVersion()) return false;
    return contract.publicApi ? QVersionNumber::compare(host, built) >= 0 : host == built;
}
Contract describePlugin(Image &plugin, const Payload &payload)
{
    need(payload.qmldir.size() <= 1024 && QByteArray(payload.qmldir).replace("\r\n", "\n") == moduleDefinition(),
         "The shader qmldir is not the exact owned regular-plugin definition.");
    plugin.readExports();
    const auto descriptor = plugin.nativeExport(QByteArray("BSIShadersDescriptor"), false);
    const QString contract = QString::fromLatin1(plugin.string(descriptor.rva, 192));
    plugin.raw(descriptor.rva, U(contract.size()) + 1);
    static const QRegularExpression exact(QStringLiteral("^BSI_SHADERS_V2;QT=([56]\\.[0-9]{1,3}\\.[0-9]{1,3});ARCH=x64;ABI=msvc;BUILD=release;NS=default;END;$"));
    static const QRegularExpression publicApi(QStringLiteral("^BSI_SHADERS_V3;QT=([56]\\.[0-9]{1,3}\\.[0-9]{1,3});ARCH=x64;ABI=msvc;BUILD=release;NS=default;API=public-v1;END;$"));
    auto matched = exact.match(contract);
    Contract result;
    if (!matched.hasMatch()) { matched = publicApi.match(contract); result.publicApi = matched.hasMatch(); }
    need(matched.hasMatch(), "Unknown shader descriptor, Qt API policy, ABI, architecture, debug build or namespace.");
    result.version = matched.captured(1);
    const auto built = qtVersion(result.version);
    need(!built.isNull(), "Invalid compiled Qt baseline.");
    if (result.publicApi) {
        const QVersionNumber minimum = built.majorVersion() == 5 ? QVersionNumber(5, 15, 2) : QVersionNumber(6, 2, 4);
        need(QVersionNumber::compare(built, minimum) >= 0, "The public Qt bridge baseline predates its audited API floor.");
    }
    plugin.nativeExport(QByteArray("qt_plugin_instance"), true);
    // Qt 6.0-6.2 used the original query export too; moc switched to the
    // v2 metadata entry in Qt 6.3. Requiring only _v2 would reject a real 6.2
    // baseline even though newer Qt loaders explicitly support that format.
    const bool metadataV2 = built.majorVersion() == 6 && built.minorVersion() >= 3;
    plugin.nativeExport(metadataV2 ? QByteArray("qt_plugin_query_metadata_v2") : QByteArray("qt_plugin_query_metadata"), true);
    return result;
}

bool qtName(const QString &name, int *major = nullptr)
{
    static const QRegularExpression rx(QStringLiteral("^qt([56])[a-z0-9]+\\.dll$"));
    const auto match = rx.match(name);
    if (!match.hasMatch()) return false;
    if (major) *major = match.captured(1).toInt();
    return true;
}
} // namespace

QString sha256(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QByteArray moduleDefinition()
{
    return QByteArray("module BSIShaders\nplugin BSIShaders\nclassname BSIShadersPlugin\n");
}
QByteArray licenseNotices()
{
    return QByteArray(
#include "../assets/shader-notices.inc"
    );
}
QString payloadHash(const Payload &payload)
{
    return sha256(QByteArray("bsi.shader-payload.v1\nBSIShaders.dll:") + sha256(payload.dll).toLatin1()
                  + "\nqmldir:" + sha256(payload.qmldir).toLatin1() + '\n');
}
bool systemModule(const QString &name)
{
    static const QSet<QString> fixed = {
        QStringLiteral("kernel32.dll"), QStringLiteral("kernelbase.dll"), QStringLiteral("ntdll.dll"),
        QStringLiteral("user32.dll"), QStringLiteral("gdi32.dll"), QStringLiteral("gdi32full.dll"),
        QStringLiteral("advapi32.dll"), QStringLiteral("shell32.dll"), QStringLiteral("shlwapi.dll"),
        QStringLiteral("ole32.dll"), QStringLiteral("oleaut32.dll"), QStringLiteral("combase.dll"),
        QStringLiteral("psapi.dll"), QStringLiteral("bcrypt.dll"), QStringLiteral("bcryptprimitives.dll"),
        QStringLiteral("crypt32.dll"), QStringLiteral("winmm.dll"), QStringLiteral("ws2_32.dll"),
        QStringLiteral("iphlpapi.dll"), QStringLiteral("dnsapi.dll"), QStringLiteral("netapi32.dll"),
        QStringLiteral("userenv.dll"), QStringLiteral("version.dll"), QStringLiteral("dwmapi.dll"),
        QStringLiteral("uxtheme.dll"), QStringLiteral("imm32.dll"), QStringLiteral("wtsapi32.dll"),
        QStringLiteral("wintrust.dll"), QStringLiteral("normaliz.dll"), QStringLiteral("mpr.dll"),
        QStringLiteral("d3d11.dll"), QStringLiteral("d3d12.dll"), QStringLiteral("dxgi.dll"),
        QStringLiteral("d3d9.dll"), QStringLiteral("dxguid.dll"), QStringLiteral("d2d1.dll"),
        QStringLiteral("dwrite.dll"), QStringLiteral("opengl32.dll"), QStringLiteral("glu32.dll"),
        QStringLiteral("comdlg32.dll"), QStringLiteral("comctl32.dll"), QStringLiteral("setupapi.dll"),
        QStringLiteral("authz.dll"), QStringLiteral("secur32.dll"), QStringLiteral("sspicli.dll"),
        QStringLiteral("msvcrt.dll"), QStringLiteral("ucrtbase.dll"), QStringLiteral("winspool.drv"),
        QStringLiteral("shcore.dll"), QStringLiteral("dcomp.dll"), QStringLiteral("windowscodecs.dll"),
        QStringLiteral("winhttp.dll"), QStringLiteral("wininet.dll"), QStringLiteral("wldap32.dll"),
        QStringLiteral("dhcpcsvc.dll"), QStringLiteral("propsys.dll"), QStringLiteral("powrprof.dll"),
        QStringLiteral("rpcrt4.dll"), QStringLiteral("dbghelp.dll"), QStringLiteral("d3dcompiler_47.dll")
    };
    // An API-set name is an OS loader contract, not a redistributable package
    // dependency. A similarly named arbitrary DLL must not be exempted.
    static const QRegularExpression api(QStringLiteral("^(?:api|ext)-ms-win-[a-z0-9]+(?:-[a-z0-9]+)*-l[0-9]+-[0-9]+-[0-9]+\\.dll$"));
    return name == name.toLower() && (fixed.contains(name) || api.match(name).hasMatch());
}
Dependencies dependencies(const QByteArray &image, bool dll)
{
    Dependencies result;
    try {
        Image parsed(image, dll); result.modules = parsed.modules;
        if (dll) {
            parsed.readExports();
            for (auto i = parsed.ordinals.cbegin(); i != parsed.ordinals.cend(); ++i) {
                if (i.value().forward.isEmpty()) continue;
                const int dot = i.value().forward.lastIndexOf('.');
                need(dot > 0, "Malformed export forwarder.");
                QString module = QString::fromLatin1(i.value().forward.left(dot)).toLower();
                if (!module.endsWith(QLatin1String(".dll"))) module += QStringLiteral(".dll");
                need(basename(module), "Invalid forwarded dependency basename.");
                if (!result.modules.contains(module)) result.modules << module;
            }
        }
        result.ok = true;
    }
    catch (const std::exception &e) { result.error = QString::fromUtf8(e.what()); }
    return result;
}

Result describe(const Payload &payload)
{
    Result result;
    try {
        Image plugin(payload.dll, true);
        const auto contract = describePlugin(plugin, payload);
        result.qtVersion = result.moduleQtVersion = contract.version;
        result.publicQtApi = contract.publicApi;
        result.packageSha256 = payloadHash(payload);
        result.ok = true;
    } catch (const std::exception &e) { result.error = QString::fromUtf8(e.what()); }
    return result;
}
Result runtimeVersion(const QByteArray &dll)
{
    Result result;
    try {
        Image runtime(dll, true);
        result.qtVersion = runtime.version();
        result.ok = true;
    } catch (const std::exception &e) { result.error = QString::fromUtf8(e.what()); }
    return result;
}

bool canTarget(const Result &description, const QString &runtimeQtVersion)
{
    return description.ok && targets({description.moduleQtVersion, description.publicQtApi}, runtimeQtVersion);
}

Result validate(const QByteArray &player, const Payload &payload, const QMap<QString, QByteArray> &runtime)
{
    Result result;
    try {
        need(payload.qmldir.size() <= 1024 && QByteArray(payload.qmldir).replace("\r\n", "\n") == moduleDefinition(),
             "The shader qmldir is not the exact owned regular-plugin definition.");
        need(runtime.size() <= moduleLimit, "Too many supplied runtime dependencies.");
        U total = U(player.size()) + U(payload.dll.size());
        for (auto i = runtime.cbegin(); i != runtime.cend(); ++i) {
            need(basename(i.key()) && !systemModule(i.key()) && i.key() != QLatin1String("bsishaders.dll"),
                 "Runtime keys must be lowercase DLL basenames, never package/system replacements.");
            total += U(i.value().size());
        }
        need(total <= totalLimit, "Shader validation input exceeds 512 MiB.");
        Image host(player, false), plugin(payload.dll, true);
        const auto contract = describePlugin(plugin, payload);
        const QString version = contract.version;
        const int major = version.left(1).toInt();
        const QString core = QStringLiteral("qt%1core.dll").arg(major);
        const QString gui = QStringLiteral("qt%1gui.dll").arg(major);
        const QString qml = QStringLiteral("qt%1qml.dll").arg(major);
        for (const auto &module : {core, gui, qml})
            need(host.modules.contains(module) && plugin.modules.contains(module),
                 QStringLiteral("Player and plugin must import the same Qt Core/Gui/Qml family: %1.").arg(module));
        auto checkQt = [&](const QStringList &modules) {
            for (const auto &module : modules) {
                int found = 0;
                if (module.startsWith(QLatin1String("qt")))
                    need(qtName(module, &found) && found == major && !module.endsWith(QLatin1String("d.dll")),
                         "Mixed, debug, suffixed or unknown Qt import family.");
            }
        };
        checkQt(host.modules); checkQt(plugin.modules);
        need(runtime.contains(core), QStringLiteral("Missing verified player runtime: %1.").arg(core));
        const auto coreVersion = runtimeVersion(runtime.value(core));
        need(coreVersion.ok, coreVersion.error);
        const QString runtimeQtVersion = coreVersion.qtVersion;
        need(targets(contract, runtimeQtVersion), contract.publicApi
            ? QStringLiteral("The public Qt bridge built with %1 requires that version or newer in the SAME Qt major; player: %2.").arg(version, runtimeQtVersion)
            : QStringLiteral("This legacy exact-version module needs Qt %1; player: %2. No compatibility override was applied.").arg(version, runtimeQtVersion));
        QMap<QString, std::shared_ptr<Image>> providers;
        QSet<QString> visiting, checked;
        auto provider = [&](const QString &module) -> std::shared_ptr<Image> {
            need(basename(module), "Invalid dependency basename.");
            static const QRegularExpression debugCrt(QStringLiteral("^(?:(?:msvcp|vcruntime|concrt)[0-9_]+d|ucrtbased)\\.dll$"));
            need(!debugCrt.match(module).hasMatch(), "Debug VC runtime dependencies are refused.");
            if (providers.contains(module)) return providers.value(module);
            need(providers.size() < moduleLimit && runtime.contains(module),
                 QStringLiteral("Missing verified runtime dependency: %1. No DLL will be installed/overwritten to supply it.").arg(module));
            auto p = std::make_shared<Image>(runtime.constFind(module).value(), true);
            p->readExports(); checkQt(p->modules);
            int m = 0;
            if (qtName(module, &m)) {
                need(m == major && p->version() == runtimeQtVersion,
                     QStringLiteral("Mixed player Qt release versions: %1 must match Core %2 exactly.").arg(module, runtimeQtVersion));
            }
            result.runtimeSha256.insert(module, sha256(runtime[module]));
            providers.insert(module, p); return p;
        };
        // Resolve forwarders too, with bounded depth and cycle detection. The
        // Windows-owned system provider is trusted, not read from a local DLL.
        auto resolve = [&](auto &&self, Symbol symbol, QSet<QString> seen) -> void {
            if (systemModule(symbol.module)) return;
            const QString key = symbol.module + QLatin1Char('!') + (symbol.byOrdinal
                ? QStringLiteral("#%1").arg(symbol.ordinal) : QString::fromLatin1(symbol.name));
            need(seen.size() < 32 && !seen.contains(key), "Cyclic or excessive dependency export forwarding.");
            seen.insert(key);
            auto p = provider(symbol.module);
            const auto value = p->exported(symbol);
            if (value.forward.isEmpty()) return;
            const int dot = value.forward.lastIndexOf('.');
            need(dot > 0 && dot < value.forward.size() - 1, "Malformed export forwarder.");
            symbol.module = QString::fromLatin1(value.forward.left(dot)).toLower();
            if (!symbol.module.endsWith(QLatin1String(".dll"))) symbol.module += QStringLiteral(".dll");
            symbol.name = value.forward.mid(dot + 1); symbol.byOrdinal = symbol.name.startsWith('#');
            if (symbol.byOrdinal) {
                bool ok = false; const uint number = symbol.name.mid(1).toUInt(&ok);
                need(ok && number <= 65535, "Invalid forwarded ordinal."); symbol.ordinal = quint16(number);
            }
            self(self, symbol, seen);
        };
        auto closure = [&](auto &&self, const QString &module) -> void {
            if (systemModule(module) || checked.contains(module) || visiting.contains(module)) return;
            visiting.insert(module);
            auto p = provider(module);
            for (const auto &s : p->imports) resolve(resolve, s, {});
            for (const auto &dependency : p->modules) self(self, dependency);
            visiting.remove(module); checked.insert(module);
        };
        for (const auto &s : plugin.imports) resolve(resolve, s, {});
        for (const auto &module : plugin.modules) closure(closure, module);
        // A forwarder may introduce a provider not named by an import table.
        for (;;) {
            const auto keys = providers.keys();
            const int count = checked.size();
            for (const auto &module : keys) closure(closure, module);
            if (checked.size() == count) break;
        }
        // MSVC's Qt ABI must be exercised by actual name imports, not inferred
        // from arbitrary DLL-name strings or the self-declared descriptor.
        for (const auto &module : {core, gui, qml}) {
            bool mangled = false;
            for (const auto &s : plugin.imports) if (s.module == module && s.name.startsWith('?')) mangled = true;
            need(mangled, "The plugin does not establish the required MSVC Qt import contract.");
        }
        result.ok = true; result.qtVersion = runtimeQtVersion; result.moduleQtVersion = version;
        result.publicQtApi = contract.publicApi; result.packageSha256 = payloadHash(payload);
    } catch (const std::exception &e) {
        result.error = QString::fromUtf8(e.what()); result.runtimeSha256.clear();
    }
    return result;
}
} // namespace ShaderPackage
