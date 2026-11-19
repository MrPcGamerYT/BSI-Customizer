#include "DllStartupImport.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace DllStartupImport
{
namespace
{
using U = std::uint64_t;
constexpr U fileLimit = 128ULL * 1024 * 1024;
constexpr U stringBudget = 8 * 1024 * 1024;
constexpr U thunkBudget = 131072;
constexpr char marker[] = "BSIDLLSTARTUPv2";
constexpr U prefix = sizeof(marker);
static_assert(prefix == 16, "marker format");

[[noreturn]] void refuse(const char *s) { throw std::runtime_error(s); }
void need(bool b, const char *s)
{
    if (!b)
        refuse(s);
}
std::string lower(std::string s)
{
    for (auto &c : s)
        if (c >= 'A' && c <= 'Z')
            c = char(c + ('a' - 'A'));
    return s;
}
bool range(U o, U n, U size) { return o <= size && n <= size - o; }
U align(U v, U a)
{
    need(a && !(a & (a - 1)), "Invalid PE alignment.");
    need(v <= 0xffffffffULL - (a - 1), "PE alignment overflow.");
    return (v + a - 1) & ~(a - 1);
}
U read(const Bytes &b, U o, unsigned n)
{
    need(range(o, n, b.size()), "Truncated PE field.");
    U v = 0;
    for (unsigned i = 0; i < n; ++i)
        v |= U(b[std::size_t(o + i)]) << (8 * i);
    return v;
}
void put(Bytes &b, U o, U v, unsigned n = 4)
{
    need(range(o, n, b.size()) && (n == 8 || v < (1ULL << (8 * n))), "PE field overflow.");
    for (unsigned i = 0; i < n; ++i)
        b[std::size_t(o + i)] = std::uint8_t(v >> (8 * i));
}

struct Section {
    std::string name;
    U va, vs, raw, size, flags;
};
struct Image {
    const Bytes &b;
    U coff, opt, table, end, count, headers, imageSize, sa, fa;
    std::vector<Section> sections;
    std::vector<Bytes> descriptors;
    std::vector<std::string> modules;
    U importRva = 0;
    mutable U stringBytes = 0;

    std::pair<U, U> span(U rva) const
    {
        need(rva <= 0xffffffffULL, "RVA overflow.");
        if (rva < headers)
            return {rva, headers - rva};
        for (const auto &s : sections) {
            if (rva >= s.va && rva - s.va < s.size)
                return {s.raw + rva - s.va, s.size - (rva - s.va)};
        }
        refuse("RVA outside file-backed data.");
    }
    U raw(U rva, U n) const
    {
        const auto s = span(rva);
        need(n <= s.second && range(s.first, n, b.size()), "RVA outside file-backed data.");
        return s.first;
    }
    std::string string(U rva, U max) const
    {
        const auto s = span(rva);
        const U n = std::min(max, s.second);
        std::string out;
        for (U i = 0; i < n; ++i) {
            need(++stringBytes <= stringBudget, "PE import strings exceed the parser work budget.");
            const auto c = b[std::size_t(s.first + i)];
            if (!c) {
                need(!out.empty(), "Empty PE string.");
                return out;
            }
            need(c >= 32 && c < 127, "Invalid PE ASCII string.");
            out += char(c);
        }
        refuse("Unterminated PE string.");
    }

    Image(const Bytes &bytes, bool dll) : b(bytes)
    {
        need(b.size() >= 0x100 && b.size() <= fileLimit && b[0] == 'M' && b[1] == 'Z',
             "Invalid PE file or file larger than 128 MiB.");
        const U pe = read(b, 0x3c, 4);
        need(pe >= 0x40 && read(b, pe, 4) == 0x4550, "Missing PE signature.");
        coff = pe + 4;
        opt = coff + 20;
        count = read(b, coff + 2, 2);
        const U os = read(b, coff + 16, 2), flags = read(b, coff + 18, 2);
        need(read(b, coff, 2) == 0x8664 && (flags & 2) && bool(flags & 0x2000) == dll,
             "An AMD64 executable and an AMD64 DLL are required (not ARM64, PE32 or an EXE renamed to DLL).");
        need(count && count < 96 && os >= 240 && range(opt, os, b.size()) && read(b, opt, 2) == 0x20b,
             "Invalid PE32+ optional header or section count.");
        need(read(b, opt + 108, 4) == 16, "Unsupported PE data-directory layout.");
        table = opt + os;
        end = table + count * 40;
        headers = read(b, opt + 60, 4);
        imageSize = read(b, opt + 56, 4);
        sa = read(b, opt + 32, 4);
        fa = read(b, opt + 36, 4);
        need(fa >= 512 && fa <= 65536 && sa >= 4096 && sa >= fa && sa <= 0x1000000,
             "Unsupported PE alignment.");
        need(align(fa, fa) == fa && align(sa, sa) == sa && headers >= end && headers <= b.size() &&
                 headers % fa == 0 && imageSize && imageSize % sa == 0,
             "Invalid PE header/image bounds.");
        for (U i = 0; i < count; ++i) {
            const U o = table + i * 40;
            need(range(o, 40, b.size()), "Truncated section table.");
            std::string name;
            for (U j = 0; j < 8 && b[std::size_t(o + j)]; ++j)
                name += char(b[std::size_t(o + j)]);
            Section s{name,
                      read(b, o + 12, 4),
                      read(b, o + 8, 4),
                      read(b, o + 20, 4),
                      read(b, o + 16, 4),
                      read(b, o + 36, 4)};
            need(s.va >= align(headers, sa) && s.va % sa == 0 && s.va + std::max(s.vs, s.size) <= imageSize,
                 "Invalid section virtual bounds.");
            need(!s.size || (s.raw >= headers && s.raw % fa == 0 && s.size % fa == 0 &&
                             range(s.raw, s.size, b.size())),
                 "Invalid section raw bounds.");
            for (const auto &p : sections) {
                need(s.va >= p.va + std::max(p.vs, p.size) || p.va >= s.va + std::max(s.vs, s.size),
                     "Overlapping PE virtual sections.");
                need(!s.size || !p.size || s.raw >= p.raw + p.size || p.raw >= s.raw + s.size,
                     "Overlapping PE file sections.");
            }
            sections.push_back(s);
        }
        const U ep = read(b, opt + 16, 4);
        if (ep || !dll) {
            raw(ep, 1);
            need(std::any_of(sections.begin(), sections.end(),
                             [&](const Section &s) {
                                 return ep >= s.va && ep - s.va < std::max(s.vs, s.size) &&
                                        (s.flags & 0x20000000);
                             }),
                 "Invalid executable entry point.");
        }
        importRva = read(b, opt + 120, 4);
        const U importSize = read(b, opt + 124, 4);
        if (!importRva && !importSize)
            return;
        need(importRva && importSize >= 20 && importSize <= 2048 * 20, "Invalid import directory bounds.");
        bool terminated = false;
        U thunks = 0;
        for (U i = 0; i < importSize / 20; ++i) {
            const U o = raw(importRva + i * 20, 20);
            if (std::all_of(b.begin() + std::ptrdiff_t(o), b.begin() + std::ptrdiff_t(o + 20),
                            [](std::uint8_t c) { return c == 0; })) {
                terminated = true;
                break;
            }
            const U ilt = read(b, o, 4), iat = read(b, o + 16, 4);
            need(iat && read(b, o + 12, 4), "Invalid import descriptor.");
            modules.push_back(string(read(b, o + 12, 4), 260));
            const U source = ilt ? ilt : iat;
            bool endThunk = false;
            for (U j = 0; j < 65536; ++j) {
                need(++thunks <= thunkBudget, "PE import thunks exceed the parser work budget.");
                const U thunk = read(b, raw(source + j * 8, 8), 8);
                raw(iat + j * 8, 8);
                if (!thunk) {
                    endThunk = true;
                    break;
                }
                if (thunk & (1ULL << 63))
                    need((thunk & 0x7fffffffffff0000ULL) == 0, "Invalid ordinal import.");
                else {
                    raw(thunk, 2);
                    string(thunk + 2, 4096);
                }
            }
            need(endThunk, "Unterminated import thunks.");
            descriptors.emplace_back(b.begin() + std::ptrdiff_t(o), b.begin() + std::ptrdiff_t(o + 20));
        }
        need(terminated, "Unterminated import directory.");
    }
};

// Validate exactly what Windows will resolve. No export is ever invoked here.
// Reject forwarders/data exports rather than guessing at a DLL's dependency
// contract. Name tables must be sorted as required for import-by-name lookup.
void requireExport(const Image &dll, const std::string &requested)
{
    const U rva = read(dll.b, dll.opt + 112, 4), size = read(dll.b, dll.opt + 116, 4);
    need(rva && size >= 40 && size <= fileLimit,
         "The DLL needs the selected named function export; it has no valid export directory.");
    const U dir = dll.raw(rva, 40);
    dll.raw(rva, size);
    const U functions = read(dll.b, dir + 20, 4), names = read(dll.b, dir + 24, 4);
    need(functions && functions <= 1000000 && names && names <= 1000000,
         "Invalid or empty DLL export tables.");
    const U ft = dll.raw(read(dll.b, dir + 28, 4), functions * 4);
    const U nt = dll.raw(read(dll.b, dir + 32, 4), names * 4);
    const U ot = dll.raw(read(dll.b, dir + 36, 4), names * 2);
    U address = 0;
    bool found = false;
    std::string previous;
    for (U i = 0; i < names; ++i) {
        const auto name = dll.string(read(dll.b, nt + i * 4, 4), 4096);
        need(i == 0 || previous < name, "Unsorted or duplicate DLL export names.");
        previous = name;
        const U ordinal = read(dll.b, ot + i * 2, 2);
        need(ordinal < functions, "DLL export ordinal is outside the function table.");
        if (name == requested) {
            found = true;
            address = read(dll.b, ft + ordinal * 4, 4);
        }
    }
    need(found, "Selected export is absent from the DLL (names are case-sensitive).");
    need(address != 0, "Selected DLL export has a null function address.");
    need(address < rva || address >= rva + size,
         "Selected export is a forwarder; choose a real native function export.");
    dll.raw(address, 1);
    need(std::any_of(dll.sections.begin(), dll.sections.end(),
                     [&](const Section &s) {
                         return address >= s.va && address - s.va < std::max(s.vs, s.size) &&
                                (s.flags & 0x20000000);
                     }),
         "Selected DLL export does not point to executable, file-backed native code.");
}

std::uint32_t checksum(const Bytes &b, U at)
{
    U sum = 0;
    for (U i = 0; i < b.size(); i += 2) {
        if (i == at || i == at + 2)
            continue;
        sum += b[std::size_t(i)] | (i + 1 < b.size() ? U(b[std::size_t(i + 1)]) << 8 : 0);
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return std::uint32_t(sum + b.size());
}

Bytes descriptor(U va, U ilt, U filename, U iat)
{
    Bytes d(20, 0);
    put(d, 0, va + ilt);
    put(d, 12, va + filename);
    put(d, 16, va + iat);
    return d;
}
} // namespace

bool validName(const std::string &s)
{
    if (s.size() < 5 || s.size() > 120 || lower(s.substr(s.size() - 4)) != ".dll" || s.front() == '.')
        return false;
    for (const auto c : s) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
              c == '-' || c == '.'))
            return false;
    }
    const auto stem = lower(s.substr(0, s.find('.')));
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul")
        return false;
    if (stem.size() == 4 && (stem.substr(0, 3) == "com" || stem.substr(0, 3) == "lpt") && stem[3] >= '1' &&
        stem[3] <= '9')
        return false;
    return true;
}

bool validExportName(const std::string &name)
{
    return !name.empty() && name.size() <= 255 &&
           std::all_of(name.begin(), name.end(), [](char c) { return c >= 33 && c <= 126; });
}

Plan prepare(const Bytes &exe, const Bytes &dll, const std::string &name, const std::string &exportName)
{
    Plan p;
    try {
        need(validName(name), "Use an ASCII DLL filename (letters, digits, dot, dash or underscore; no "
                              "paths, spaces or reserved device names).");
        need(validExportName(exportName),
             "Enter an exact ASCII function export name (1-255 non-space characters).");
        const Image target(exe, false), payload(dll, true);
        requireExport(payload, exportName);
        p.dependencies = payload.modules;
        p.importsBefore = target.modules.size();
        const auto own = std::find_if(target.sections.begin(), target.sections.end(),
                                      [](const Section &s) { return s.name == ".bsidll"; });
        if (own != target.sections.end()) {
            need(std::count_if(target.sections.begin(), target.sections.end(),
                               [](const Section &s) { return s.name == ".bsidll"; }) == 1 &&
                     &*own == &target.sections.back() && own->size >= prefix &&
                     own->raw + own->size == exe.size() &&
                     std::memcmp(exe.data() + own->raw, marker, sizeof(marker)) == 0 &&
                     target.importRva == own->va + prefix && !target.modules.empty(),
                 "Unrecognized .bsidll section; nothing was changed.");
            need(lower(target.modules.back()) == lower(name),
                 "A different startup DLL is already installed. Restore it before choosing another.");
            const U ds = (target.modules.size() + 1) * 20;
            const U ilt = align(prefix + ds, 8), iat = ilt + 16, filename = iat + 16;
            const U hint = align(filename + target.modules.back().size() + 1, 2);
            const U vs = hint + 2 + exportName.size() + 1;
            need(own->vs == vs && own->size == align(vs, target.fa) && own->flags == 0xc0000040 &&
                     read(exe, target.opt + 124, 4) == ds &&
                     target.descriptors.back() == descriptor(own->va, ilt, filename, iat) &&
                     target.string(own->va + filename, 121) == target.modules.back() &&
                     read(exe, own->raw + ilt, 8) == own->va + hint &&
                     read(exe, own->raw + iat, 8) == own->va + hint &&
                     read(exe, own->raw + ilt + 8, 8) == 0 && read(exe, own->raw + iat + 8, 8) == 0 &&
                     read(exe, own->raw + hint, 2) == 0 &&
                     target.string(own->va + hint + 2, 256) == exportName,
                 "The startup import no longer matches this patcher's format; nothing was changed.");
            p.ok = p.alreadyApplied = true;
            p.output = exe;
            p.importsAfter = p.importsBefore;
            return p;
        }
        for (const auto &s : target.modules)
            need(lower(s) != lower(name),
                 "This DLL is already an original dependency; no duplicate import will be added.");
        // Bound import tables may cache old descriptor/timestamp relationships.
        // Do not silently rewrite or invalidate a layout that is not supported.
        need(!read(exe, target.opt + 200, 4) && !read(exe, target.opt + 204, 4),
             "Bound import tables are not supported; nothing was changed.");
        need(target.end + 40 <= target.headers, "No room for a new PE section header.");
        for (U i = target.end; i < target.end + 40; ++i)
            need(exe[std::size_t(i)] == 0,
                 "The new section-header slot contains data; refusing to overwrite it.");
        const U ds = (target.descriptors.size() + 2) * 20;
        need(ds <= 2048 * 20, "No room within the import-descriptor safety limit.");
        const U ilt = align(prefix + ds, 8), iat = ilt + 16, filename = iat + 16;
        const U hint = align(filename + name.size() + 1, 2);
        const U size = hint + 2 + exportName.size() + 1, rawSize = align(size, target.fa);
        const U va = align(target.imageSize, target.sa), offset = align(exe.size(), target.fa);
        need(offset + rawSize <= fileLimit, "Patched EXE would exceed 128 MiB.");
        p.output = exe;
        p.output.resize(std::size_t(offset + rawSize), 0);
        std::copy(marker, marker + sizeof(marker), p.output.begin() + std::ptrdiff_t(offset));
        U cursor = offset + prefix;
        for (const auto &d : target.descriptors) {
            std::copy(d.begin(), d.end(), p.output.begin() + std::ptrdiff_t(cursor));
            cursor += 20;
        }
        const auto added = descriptor(va, ilt, filename, iat);
        std::copy(added.begin(), added.end(), p.output.begin() + std::ptrdiff_t(cursor));
        // A real symbol is essential: Windows skipped DLLs with empty thunk
        // lists in the actual startup test. Resolving this symbol never calls it.
        put(p.output, offset + ilt, va + hint, 8);
        put(p.output, offset + iat, va + hint, 8);
        std::copy(name.begin(), name.end(), p.output.begin() + std::ptrdiff_t(offset + filename));
        std::copy(exportName.begin(), exportName.end(), p.output.begin() + std::ptrdiff_t(offset + hint + 2));
        const char section[] = ".bsidll";
        std::copy(section, section + sizeof(section), p.output.begin() + std::ptrdiff_t(target.end));
        put(p.output, target.end + 8, size);
        put(p.output, target.end + 12, va);
        put(p.output, target.end + 16, rawSize);
        put(p.output, target.end + 20, offset);
        put(p.output, target.end + 36, 0xc0000040);
        put(p.output, target.coff + 2, target.count + 1, 2);
        put(p.output, target.opt + 56, align(va + size, target.sa));
        put(p.output, target.opt + 8, read(exe, target.opt + 8, 4) + rawSize);
        put(p.output, target.opt + 120, va + prefix);
        put(p.output, target.opt + 124, ds);
        put(p.output, target.opt + 64, 0);
        put(p.output, target.opt + 64, checksum(p.output, target.opt + 64));
        const Image verified(p.output, false);
        need(verified.modules.size() == target.modules.size() + 1 && verified.modules.back() == name,
             "Prepared import failed verification.");
        for (std::size_t i = 0; i < target.descriptors.size(); ++i)
            need(verified.descriptors[i] == target.descriptors[i], "Original import descriptor changed.");
        p.importsAfter = verified.modules.size();
        p.ok = true;
    } catch (const std::exception &e) {
        p.output.clear();
        p.error = e.what();
    }
    return p;
}
} // namespace DllStartupImport
