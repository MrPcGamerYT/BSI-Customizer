#include "RpcIdScan.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <set>
#include <unordered_map>

namespace RpcIdScan {
namespace {

// Documented IDs, shown as a friendly label in the UI. Detection itself stays
// value-agnostic - an ID that is not in this table is still found by the same
// generic evidence (code immediates, text, repeated stored constants) - but a
// documented ID IS a confirmed match the moment it is seen, the same way the
// reference scanner treats it, so it carries a decisive scoring bonus and is
// always preferred over an unconfirmed guess instead of risking a refusal or
// a coin-flip tie against unrelated noise in the file.
struct KnownId
{
    const char *id;
    const char *name;
};
constexpr KnownId kKnown[] = {
    { "821361671729709086", "BlueStacks 5 / MSI App Player" },
};

inline bool isDigit(unsigned char c) { return c >= '0' && c <= '9'; }

inline unsigned char byteAt(std::string_view d, std::int64_t off)
{
    return static_cast<unsigned char>(d[static_cast<std::size_t>(off)]);
}

inline std::uint16_t le16(std::string_view d, std::int64_t off)
{
    return std::uint16_t(byteAt(d, off) | (byteAt(d, off + 1) << 8));
}

inline std::uint32_t le32(std::string_view d, std::int64_t off)
{
    return std::uint32_t(byteAt(d, off)) | (std::uint32_t(byteAt(d, off + 1)) << 8)
         | (std::uint32_t(byteAt(d, off + 2)) << 16) | (std::uint32_t(byteAt(d, off + 3)) << 24);
}

inline std::int32_t sle32(std::string_view d, std::int64_t off)
{
    return static_cast<std::int32_t>(le32(d, off));
}

inline std::uint64_t le64(std::string_view d, std::int64_t off)
{
    return std::uint64_t(le32(d, off)) | (std::uint64_t(le32(d, off + 4)) << 32);
}

inline void putLe64(char *out, std::uint64_t value)
{
    for (int i = 0; i < 8; ++i)
        out[i] = static_cast<char>((value >> (8 * i)) & 0xFF);
}

std::uint64_t nowMs()
{
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

// Decimal text -> value. False on empty input, a non-digit or 64-bit overflow
// (the reference tool works with big integers; everything that does not fit is
// far outside the snowflake range anyway).
bool parseId(const std::string &text, std::uint64_t *out)
{
    if (text.empty() || text.size() > 20)
        return false;
    std::uint64_t value = 0;
    for (const char c : text) {
        if (!isDigit(static_cast<unsigned char>(c)))
            return false;
        const std::uint64_t digit = std::uint64_t(c - '0');
        if (value > (UINT64_MAX - digit) / 10)
            return false;
        value = value * 10 + digit;
    }
    if (out)
        *out = value;
    return true;
}

bool isRipModrm(unsigned char c)
{
    // mod=00, rm=101 -> [rip+disp32], for every destination register.
    return c == 0x05 || c == 0x0D || c == 0x15 || c == 0x1D
        || c == 0x25 || c == 0x2D || c == 0x35 || c == 0x3D;
}

} // namespace

// ---------------------------------------------------------------------- kinds

const char *kindName(Kind kind)
{
    switch (kind) {
    case Kind::Imm64:   return "imm64";
    case Kind::U64le:   return "u64le";
    case Kind::Ascii:   return "ascii";
    case Kind::Utf16le: return "utf16le";
    }
    return "?";
}

const char *kindLabel(Kind kind)
{
    switch (kind) {
    case Kind::Imm64:   return "code constant";
    case Kind::U64le:   return "integer";
    case Kind::Ascii:   return "text";
    case Kind::Utf16le: return "wide text";
    }
    return "?";
}

// ------------------------------------------------------------------- PE image

Pe::Pe(std::string_view data)
    : m_data(data)
{
    if (data.size() < 64 || data[0] != 'M' || data[1] != 'Z')
        return;
    const std::uint32_t pe = le32(data, 0x3C);
    if (std::uint64_t(pe) + 4 > data.size() || std::memcmp(data.data() + pe, "PE\0\0", 4) != 0)
        return;
    if (std::uint64_t(pe) + 26 > data.size())
        return;
    m_pe = pe;
    m_machine = le16(data, pe + 4);
    const std::uint16_t count = le16(data, pe + 6);
    const std::uint16_t optionalSize = le16(data, pe + 20);
    const bool plus = le16(data, pe + 24) == 0x20B;
    m_dd = pe + 24 + (plus ? 112 : 96);
    for (std::uint16_t i = 0; i < count; ++i) {
        const std::uint64_t off = std::uint64_t(pe) + 24 + optionalSize + std::uint64_t(i) * 40;
        if (off + 40 > data.size())
            return;  // truncated header: treat the image as unparsable
        Section section;
        const char *name = data.data() + off;
        std::size_t length = 0;
        while (length < 8 && name[length] != '\0')
            ++length;
        section.name.assign(name, length);
        section.vsize = le32(data, std::int64_t(off) + 8);
        section.va = le32(data, std::int64_t(off) + 12);
        section.rawSize = le32(data, std::int64_t(off) + 16);
        section.raw = le32(data, std::int64_t(off) + 20);
        section.chars = le32(data, std::int64_t(off) + 36);
        m_sections.push_back(std::move(section));
    }
    m_ok = true;
}

const char *Pe::arch() const
{
    switch (m_machine) {
    case 0x8664: return "x64";
    case 0x014C: return "x86";
    case 0xAA64: return "arm64";
    default:     return "?";
    }
}

std::pair<std::uint32_t, std::uint32_t> Pe::directory(int index) const
{
    if (!m_dd)
        return { 0, 0 };
    const std::uint64_t off = std::uint64_t(m_dd) + std::uint64_t(index) * 8;
    if (off + 8 > m_data.size())
        return { 0, 0 };
    return { le32(m_data, std::int64_t(off)), le32(m_data, std::int64_t(off) + 4) };
}

bool Pe::isSigned() const
{
    return m_ok && directory(4).second != 0;
}

std::pair<std::string, bool> Pe::sectionOf(std::int64_t off) const
{
    for (const Section &s : m_sections) {
        if (off >= std::int64_t(s.raw) && off < std::int64_t(s.raw) + std::int64_t(s.rawSize))
            return { s.name, (s.chars & 0x20000000u) != 0 };
    }
    return { std::string("headers"), false };
}

std::int64_t Pe::rvaToOffset(std::int64_t rva) const
{
    for (const Section &s : m_sections) {
        const std::int64_t span = std::int64_t(std::max(s.vsize, s.rawSize));
        if (rva >= std::int64_t(s.va) && rva < std::int64_t(s.va) + span)
            return std::int64_t(s.raw) + rva - std::int64_t(s.va);
    }
    return -1;
}

std::int64_t Pe::offsetToRva(std::int64_t off) const
{
    for (const Section &s : m_sections) {
        if (off >= std::int64_t(s.raw) && off < std::int64_t(s.raw) + std::int64_t(s.rawSize))
            return std::int64_t(s.va) + off - std::int64_t(s.raw);
    }
    return -1;
}

std::vector<Pe::Span> Pe::spans() const
{
    std::vector<Span> spans;
    spans.reserve(m_sections.size());
    for (const Section &s : m_sections)
        spans.push_back({ std::int64_t(s.raw), std::int64_t(s.rawSize),
                          std::int64_t(s.va), (s.chars & 0x20000000u) != 0 });
    return spans;
}

// ------------------------------------------------------------------ shape tests

bool plausible(std::uint64_t value)
{
    if (!(value > (1ULL << 56) && value < (1ULL << 61)))
        return false;
    const std::uint64_t stamp = (value >> 22) + kDiscordEpoch;
    return stamp > kDiscordEpoch && stamp < nowMs() + 86400000ULL;
}

bool compilerConstant(std::uint64_t value)
{
    if ((value & (value + 1)) == 0 || (value & (value - 1)) == 0)
        return true;
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
    const std::string hex(buffer, 16);
    if (hex.find("00000000") != std::string::npos || hex.find("ffffffff") != std::string::npos)
        return true;
    if (std::set<char>(hex.begin(), hex.end()).size() <= 3)
        return true;
    for (const int width : { 2, 4, 8 }) {
        bool uniform = true;
        for (int i = width; i < 16 && uniform; i += width)
            uniform = hex.compare(std::size_t(i), std::size_t(width), hex, 0, std::size_t(width)) == 0;
        if (uniform)
            return true;
    }
    return (value & 0x3FFFFFULL) == 0;
}

bool isAppIdText(const std::string &id)
{
    if (id.size() < 17 || id.size() > 20)
        return false;
    std::uint64_t value = 0;
    if (!parseId(id, &value))
        return false;
    return plausible(value) && !compilerConstant(value);
}

std::string snowflakeDate(const std::string &id)
{
    std::uint64_t value = 0;
    if (!parseId(id, &value))
        return std::string("?");
    const std::time_t stamp = static_cast<std::time_t>(((value >> 22) + kDiscordEpoch) / 1000);
    std::tm utc {};
#if defined(_WIN32)
    if (gmtime_s(&utc, &stamp) != 0)
        return std::string("?");
#else
    if (gmtime_r(&stamp, &utc) == nullptr)
        return std::string("?");
#endif
    char text[16];
    if (std::strftime(text, sizeof(text), "%Y-%m-%d", &utc) == 0)
        return std::string("?");
    return std::string(text);
}

std::string knownName(const std::string &id)
{
    for (const KnownId &entry : kKnown) {
        if (id == entry.id)
            return std::string(entry.name);
    }
    return std::string();
}

// -------------------------------------------------------------------- harvest

std::vector<Candidate> harvest(std::string_view data, const Pe *pe)
{
    std::vector<Candidate> records;
    const std::int64_t limit = static_cast<std::int64_t>(data.size());
    if (limit < 8)
        return records;

    std::unordered_map<std::string, std::size_t> index;
    std::set<std::pair<int, std::int64_t>> added;

    const auto add = [&](std::uint64_t value, Kind kind, std::int64_t off,
                         std::string text = std::string(), int slack = 0) {
        if (!added.insert({ int(kind), off }).second)
            return;
        const std::string id = std::to_string(value);
        auto hit = index.find(id);
        if (hit == index.end()) {
            Candidate record;
            record.id = id;
            records.push_back(std::move(record));
            hit = index.emplace(id, records.size() - 1).first;
        }
        Site site;
        site.kind = kind;
        site.off = off;
        site.text = std::move(text);
        site.slack = slack;
        records[hit->second].sites.push_back(std::move(site));
    };

    const bool usePe = pe != nullptr && pe->ok();

    // Addresses the code actually references (RIP-relative). Used both to admit
    // bare-qword candidates and as ranking evidence.
    std::vector<std::int64_t> rip;
    if (usePe) {
        for (std::int64_t i = 0; i + 3 <= limit;) {               // REX.W mov/lea r64,[rip+d]
            const unsigned char b0 = byteAt(data, i);
            const unsigned char b1 = byteAt(data, i + 1);
            if (b0 >= 0x48 && b0 <= 0x4F && (b1 == 0x8B || b1 == 0x8D)
                && isRipModrm(byteAt(data, i + 2))) {
                if (i + 7 <= limit) {
                    const std::int64_t rva = pe->offsetToRva(i);
                    if (rva >= 0)
                        rip.push_back(rva + 7 + sle32(data, i + 3));
                }
                i += 3;
            } else {
                ++i;
            }
        }
        for (std::int64_t i = 0; i + 2 <= limit;) {               // 32-bit operand form
            const unsigned char b0 = byteAt(data, i);
            const unsigned char previous = i > 0 ? byteAt(data, i - 1) : 0;
            if ((b0 == 0x8B || b0 == 0x8D) && isRipModrm(byteAt(data, i + 1))
                && !(i > 0 && previous >= 0x40 && previous <= 0x4F)) {
                if (i + 6 <= limit) {
                    const std::int64_t rva = pe->offsetToRva(i);
                    if (rva >= 0)
                        rip.push_back(rva + 6 + sle32(data, i + 2));
                }
                i += 2;
            } else {
                ++i;
            }
        }
        std::sort(rip.begin(), rip.end());
        rip.erase(std::unique(rip.begin(), rip.end()), rip.end());
    }
    const auto referenced = [&rip](std::int64_t rva) {
        return std::binary_search(rip.begin(), rip.end(), rva);
    };

    // x64 mov r64, imm64 (48/49 B8..BF), including non-RAX registers.
    for (std::int64_t i = 0; i + 2 <= limit;) {
        const unsigned char b0 = byteAt(data, i);
        const unsigned char b1 = byteAt(data, i + 1);
        if ((b0 == 0x48 || b0 == 0x49) && b1 >= 0xB8 && b1 <= 0xBF) {
            if (i + 10 <= limit) {
                const std::uint64_t value = le64(data, i + 2);
                if (plausible(value) && !compilerConstant(value))
                    add(value, Kind::Imm64, i + 2);
            }
            i += 2;
        } else {
            ++i;
        }
    }

    // Where Discord-related data lives; bare qwords are only interesting here,
    // where code references them, or when a value already proved itself as a
    // deliberate stored constant (handled further below).
    std::vector<std::int64_t> discord;
    for (std::int64_t i = 0; i + 7 <= limit;) {
        bool hit = true;
        static const char needle[] = "discord";
        for (int k = 0; k < 7 && hit; ++k) {
            unsigned char c = byteAt(data, i + k);
            if (c >= 'A' && c <= 'Z')
                c = static_cast<unsigned char>(c - 'A' + 'a');
            hit = c == static_cast<unsigned char>(needle[k]);
        }
        if (hit) {
            discord.push_back(i);
            i += 7;
        } else {
            ++i;
        }
    }
    std::vector<std::pair<std::int64_t, std::int64_t>> windows;
    for (const std::int64_t p : discord) {
        const std::int64_t low = std::max<std::int64_t>(0, p - 4096);
        const std::int64_t high = std::min<std::int64_t>(limit, p + 4096);
        if (!windows.empty() && low <= windows.back().second)
            windows.back().second = std::max(windows.back().second, high);
        else
            windows.emplace_back(low, high);
    }

    std::set<std::int64_t> claimed;
    for (const Candidate &record : records) {
        for (const Site &site : record.sites) {
            if (site.kind != Kind::Imm64)
                continue;
            for (std::int64_t x = site.off - 7; x <= site.off + 7; ++x)
                claimed.insert(x);
        }
    }

    std::int64_t budget = 4096;                     // hard cap on noise
    const auto qwordOk = [&](std::int64_t off) {
        if (off < 0 || off + 8 > limit || claimed.count(off) != 0)
            return false;
        const std::uint64_t value = le64(data, off);
        if (!plausible(value) || compilerConstant(value))
            return false;
        bool printable = true;
        for (int k = 0; k < 8 && printable; ++k) {
            const unsigned char c = byteAt(data, off + k);
            printable = c >= 32 && c <= 126;
        }
        if (printable)
            return false;
        add(value, Kind::U64le, off);
        return true;
    };

    const std::uint64_t maxPlausible = (nowMs() + 86400000ULL - kDiscordEpoch) << 22;
    const int highestMsb = static_cast<int>(std::min<std::uint64_t>(0x1F, maxPlausible >> 56));
    if (highestMsb >= 1) {
        for (const auto &window : windows) {                     // near Discord strings
            for (std::int64_t p = window.first; p < window.second; ++p) {
                if (budget <= 0)
                    break;
                const unsigned char msb = byteAt(data, p);
                if (msb >= 1 && msb <= static_cast<unsigned char>(highestMsb) && qwordOk(p - 7))
                    --budget;
            }
        }
        if (usePe) {
            for (const std::int64_t rva : rip) {                 // referenced by code
                const std::int64_t off = pe->rvaToOffset(rva);
                if (off >= 0 && qwordOk(off))
                    --budget;
            }
        }
    }
    // Decimal text forms. Junk-shaped numbers (round values, repeated digit
    // patterns) are rejected here too, not just for the integer forms. Only a
    // complete 17-20 digit run counts: part of a longer number is not an ID.
    for (std::int64_t i = 0; i < limit;) {
        if (!isDigit(byteAt(data, i))) {
            ++i;
            continue;
        }
        std::int64_t end = i;
        while (end < limit && isDigit(byteAt(data, end)))
            ++end;
        const std::int64_t length = end - i;
        if (length >= 17 && length <= 20) {
            const std::string text(data.substr(std::size_t(i), std::size_t(length)));
            std::uint64_t value = 0;
            if (parseId(text, &value) && plausible(value) && !compilerConstant(value)) {
                std::int64_t tail = end;
                int slack = 0;
                while (tail < limit && byteAt(data, tail) == 0) {
                    ++tail;
                    ++slack;
                }
                add(value, Kind::Ascii, i, text, slack);
            }
        }
        i = end;
    }

    // UTF-16: measure the WHOLE digit run, then reject runs longer than 20
    // digits instead of silently reading their first 20 pairs as an ID. A
    // rejected run is retried two bytes later, and "part of a longer number"
    // needs TWO preceding digit pairs: a single pair can be a phantom, e.g.
    // the last digit + NUL of an ASCII string in front of a UTF-16 one.
    const auto digitPair = [&](std::int64_t p) {
        return p >= 0 && p + 1 < limit && isDigit(byteAt(data, p)) && byteAt(data, p + 1) == 0;
    };
    for (std::int64_t pos = 0; pos < limit;) {
        std::int64_t start = -1;
        std::int64_t pairs = 0;
        for (std::int64_t s = pos; s + 34 <= limit; ++s) {
            if (!digitPair(s))
                continue;
            std::int64_t count = 0;
            for (std::int64_t t = s; count < 21 && digitPair(t); t += 2)
                ++count;
            if (count >= 17) {
                start = s;
                pairs = count;                     // 21 means "longer than 20 digits"
                break;
            }
        }
        if (start < 0)
            break;
        const bool boundary = start >= 4 && digitPair(start - 2) && digitPair(start - 4);
        bool accepted = false;
        if (!boundary && pairs <= 20) {
            std::string text;
            text.reserve(std::size_t(pairs));
            for (std::int64_t k = 0; k < pairs; ++k)
                text.push_back(static_cast<char>(byteAt(data, start + 2 * k)));
            std::uint64_t value = 0;
            if (parseId(text, &value) && plausible(value) && !compilerConstant(value)) {
                std::int64_t tail = start + 2 * pairs;
                int slack = 0;
                while (tail + 1 < limit && byteAt(data, tail) == 0 && byteAt(data, tail + 1) == 0) {
                    tail += 2;
                    ++slack;
                }
                add(value, Kind::Utf16le, start, text, slack);
                pos = start + 2 * pairs;
                accepted = true;
            }
        }
        if (!accepted)
            pos = start + 2;
    }

    // Deliberate forms: any ID already seen as a digit string or a repeated
    // code immediate is corroborated by EVERY byte-level copy of its 64-bit
    // form, wherever that copy sits. This used to be done for one hardcoded
    // ID only, which is why other IDs looked "undetectable": now every value
    // gets the same whole-file search, based on evidence instead of a table.
    // Lone one-shot constants stay un-corroborated, so a file full of junk
    // immediates cannot turn the whole-file search into a scan per constant.
    // A hard budget protects scan time against adversarial junk either way.
    std::size_t corroborated = 0;
    constexpr std::size_t kCorroborationBudget = 64;
    const auto findEverywhere = [&](std::uint64_t value) {
        if (corroborated >= kCorroborationBudget)
            return;
        ++corroborated;
        char packed[8];
        putLe64(packed, value);
        const std::string_view needle(packed, 8);
        for (std::size_t pos = data.find(needle); pos != std::string_view::npos;
             pos = data.find(needle, pos + 1)) {
            if (claimed.count(static_cast<std::int64_t>(pos)) == 0)
                add(value, Kind::U64le, static_cast<std::int64_t>(pos));
        }
    };
    {
        std::set<std::uint64_t> seeded;
        for (const Candidate &record : records) {
            if (record.sites.size() < 2)
                continue;
            for (const Site &site : record.sites) {
                if (site.kind == Kind::Ascii || site.kind == Kind::Utf16le
                    || site.kind == Kind::Imm64) {
                    std::uint64_t value = 0;
                    if (parseId(record.id, &value))
                        seeded.insert(value);
                    break;
                }
            }
        }
        for (const std::uint64_t value : seeded)
            findEverywhere(value);
    }

    // Stored constants: an application ID that only exists as data (Discord
    // Game SDK structs, configuration blobs) cannot rely on nearby "discord"
    // strings or code references, so naturally aligned qwords in data are
    // admitted when the same value repeats - a single snowflake-shaped qword
    // can be anything, but two identical copies in stored data are deliberate.
    {
        std::unordered_map<std::uint64_t, int> stored;
        const auto probe = [&](std::int64_t off) {
            if (off < 0 || off + 8 > limit || claimed.count(off) != 0)
                return;
            const std::uint64_t value = le64(data, off);
            if (!plausible(value) || compilerConstant(value))
                return;
            bool printable = true;
            for (int k = 0; k < 8 && printable; ++k) {
                const unsigned char c = byteAt(data, off + k);
                printable = c >= 32 && c <= 126;
            }
            if (printable)
                return;
            // Only the first two sightings matter: one copy is ambient, the
            // second proves a stored constant.
            auto &count = stored[value];
            if (count < 2)
                ++count;
        };
        const std::vector<Pe::Span> spans = usePe ? pe->spans() : std::vector<Pe::Span>();
        if (spans.empty()) {
            for (std::int64_t off = 0; off + 8 <= limit; off += 8)
                probe(off);
        } else {
            for (const Pe::Span &span : spans) {
                if (span.exec)
                    continue;
                const std::int64_t last = std::min<std::int64_t>(span.off + span.size, limit);
                // Keep addresses naturally aligned: compilers store u64
                // fields at 8-byte boundaries, junk straddlers do not.
                std::int64_t off = span.off + ((8 - (span.rva & 7)) & 7);
                for (; off + 8 <= last; off += 8)
                    probe(off);
            }
        }
        for (const auto &entry : stored) {
            if (entry.second >= 2)
                findEverywhere(entry.first);
        }
    }

    const auto nearDiscord = [&](std::int64_t off) {
        const auto it = std::lower_bound(discord.begin(), discord.end(), off);
        const std::int64_t i = it - discord.begin();
        for (const std::int64_t j : { i - 1, i }) {
            if (j < 0 || j >= std::int64_t(discord.size()))
                continue;
            if (std::abs(discord[std::size_t(j)] - off) > 4096)
                continue;
            if (usePe)
                return pe->sectionOf(off).first == pe->sectionOf(discord[std::size_t(j)]).first;
            return true;
        }
        return false;
    };

    // Ranking. Deliberate storage forms - a standalone digit string, an
    // immediate load in executable code - outweigh ambient integers, and no
    // weight ever names a specific ID, so an unknown-but-real ID can beat a
    // documented-looking junk constant instead of being buried under it.
    // Corroboration (the same value stored in several ways) ranks highest.
    for (Candidate &record : records) {
        bool text = false, immediate = false, immExec = false, qword = false;
        int qwords = 0;
        bool near = false, ripRef = false;
        std::set<int> kinds;
        for (Site &site : record.sites) {
            kinds.insert(int(site.kind));
            switch (site.kind) {
            case Kind::Ascii:
            case Kind::Utf16le: text = true; break;
            case Kind::Imm64:   immediate = true; break;
            case Kind::U64le:   qword = true; ++qwords; break;
            }
            if (usePe) {
                const auto section = pe->sectionOf(site.off);
                site.section = section.first;
                if (site.kind == Kind::Imm64 && section.second)
                    immExec = true;
                if (site.kind == Kind::U64le) {
                    const std::int64_t rva = pe->offsetToRva(site.off);
                    if (rva >= 0 && referenced(rva)) {
                        site.rip = true;
                        ripRef = true;
                    }
                }
            }
            // A digit string right next to a Discord mention is just as
            // telling as an integer near one.
            if (!near && nearDiscord(site.off))
                near = true;
        }
        record.known = knownName(record.id);
        const bool isKnown = !record.known.empty();
        // An immediate only counts strongly when it really sits in code;
        // outside an executable section the "instruction" is misread data.
        const bool immStrong = immediate && (immExec || !usePe);
        const bool multiKind = kinds.size() >= 2;
        // A repeated stored constant is deliberate; it still needs one more
        // hint (a third copy, Discord-related neighbourhood or a code
        // reference) before it may be acted on automatically.
        const bool array = qwords >= 2;
        const bool arrayStrong = qwords >= 3 || (array && (near || ripRef));
        // Neighbourhood is supporting evidence, never a verdict: a digit
        // string on its own must still outrank a well-placed junk constant,
        // because a wrong pick is exactly what bricks executables. A
        // documented Application ID (e.g. the stock BlueStacks one) is a
        // confirmed match on its own - exactly like the reference tool,
        // which always recognizes it - so it gets a decisive bonus instead
        // of competing on generic heuristics alone and losing a coin-flip
        // tie against unrelated noise.
        const int score = (text ? 7 : 0) + (immStrong ? 5 : immediate ? 1 : 0)
                        + (multiKind ? 4 : 0) + (array ? 3 : 0)
                        + (near ? 2 : 0) + (ripRef ? 2 : 0) + (isKnown ? 10 : 0);
        const int signals = int(text) + int(immStrong) + int(multiKind) + int(arrayStrong)
                          + int(near) + int(ripRef) + int(isKnown);
        record.score = score;
        record.refs = static_cast<int>(record.sites.size());
        record.date = snowflakeDate(record.id);
        record.confident = signals >= 2 || isKnown;
        // A bare qword may only be chosen automatically when it sits in
        // Discord-related data or is a repeated stored constant; a mere code
        // reference is not enough on its own, because referenced timestamps
        // and hashes are everywhere. A documented ID is always eligible.
        record.eligible = text || immStrong || multiKind || arrayStrong || signals >= 2
                       || (near && (immediate || qword)) || isKnown;
    }

    // Two bare-qword candidates whose bytes overlap cannot both be real
    // constants; one is usually the other shifted by a few bytes. The one with
    // less evidence is demoted, and a dead tie demotes both - guessing here is
    // what used to make the detector show a wrong ID.
    {
        std::vector<std::pair<std::int64_t, std::size_t>> bare;
        for (std::size_t r = 0; r < records.size(); ++r) {
            const Candidate &record = records[r];
            if (record.score < 3 || record.sites.empty() || !record.known.empty())
                continue;
            bool onlyQwords = true;
            for (const Site &site : record.sites)
                onlyQwords = onlyQwords && site.kind == Kind::U64le;
            if (!onlyQwords)
                continue;
            for (const Site &site : record.sites)
                bare.emplace_back(site.off, r);
        }
        std::stable_sort(bare.begin(), bare.end(),
                         [](const auto &a, const auto &b) { return a.first < b.first; });
        for (std::size_t i = 0; i + 1 < bare.size(); ++i) {
            if (bare[i + 1].first - bare[i].first >= 8 || bare[i].second == bare[i + 1].second)
                continue;
            Candidate &first = records[bare[i].second];
            Candidate &second = records[bare[i + 1].second];
            if (first.score > second.score)
                second.score = std::min(second.score, 2);
            else if (second.score > first.score)
                first.score = std::min(first.score, 2);
            else
                first.score = second.score = 2;
        }
    }

    std::sort(records.begin(), records.end(), [](const Candidate &a, const Candidate &b) {
        if (a.score != b.score)
            return a.score > b.score;
        if (a.refs != b.refs)
            return a.refs < b.refs;
        return a.id < b.id;
    });
    return records;
}

Pick pickCandidate(const std::vector<Candidate> &records)
{
    Pick pick;
    for (int i = 0; i < static_cast<int>(records.size()); ++i) {
        if (records[std::size_t(i)].score >= 3)
            pick.strong.push_back(i);
    }
    if (pick.strong.empty())
        return pick;
    // A documented Application ID (e.g. the stock "821361671729709086" used
    // by BlueStacks 5 / MSI App Player) is a confirmed match by definition,
    // exactly as in the reference tool: whenever one shows up among the
    // strong candidates it is used outright, instead of being held back by a
    // tie against an undocumented guess the way an unconfirmed candidate is.
    for (const int index : pick.strong) {
        if (!records[std::size_t(index)].known.empty()) {
            pick.index = index;
            return pick;
        }
    }
    const Candidate &top = records[std::size_t(pick.strong.front())];
    if (!top.eligible)
        return pick;
    if (pick.strong.size() > 1 && records[std::size_t(pick.strong[1])].score >= top.score)
        return pick;                    // two equally good guesses: refuse to guess
    pick.index = pick.strong.front();
    return pick;
}

// ------------------------------------------------------------- stored copies

std::vector<Site> sitesForId(std::string_view data, const std::string &id)
{
    std::vector<Site> sites;
    std::set<std::pair<int, std::int64_t>> seen;
    const std::int64_t limit = static_cast<std::int64_t>(data.size());

    const auto add = [&](Kind kind, std::int64_t off, std::string text = std::string(),
                         int slack = 0) {
        if (!seen.insert({ int(kind), off }).second)
            return;
        Site site;
        site.kind = kind;
        site.off = off;
        site.text = std::move(text);
        site.slack = slack;
        sites.push_back(std::move(site));
    };

    std::uint64_t value = 0;
    if (parseId(id, &value)) {
        char packed[8];
        putLe64(packed, value);
        const std::string_view needle(packed, 8);
        for (std::size_t pos = data.find(needle); pos != std::string_view::npos;
             pos = data.find(needle, pos + needle.size()))
            add(Kind::U64le, static_cast<std::int64_t>(pos));
    }

    std::string wide;
    wide.reserve(id.size() * 2);
    for (const char c : id) {
        wide.push_back(c);
        wide.push_back('\0');
    }
    const std::pair<Kind, const std::string *> forms[] = {
        { Kind::Ascii, &id },
        { Kind::Utf16le, &wide },
    };
    for (const auto &form : forms) {
        const Kind kind = form.first;
        const std::string &needle = *form.second;
        if (needle.empty())
            continue;
        const std::int64_t width = kind == Kind::Ascii ? 1 : 2;
        const std::int64_t length = static_cast<std::int64_t>(needle.size());
        for (std::size_t found = data.find(needle); found != std::string::npos;
             found = data.find(needle, found + needle.size())) {
            const std::int64_t pos = static_cast<std::int64_t>(found);
            // "Embedded in a longer number" needs two full characters of
            // context on the relevant side: a single digit character can be a
            // phantom, e.g. the last byte pair of a neighbouring ASCII ID
            // string ("9\0") sitting directly in front of a UTF-16 copy.
            bool digitBefore = false;
            bool digitAfter = false;
            if (width == 1) {
                digitBefore = pos >= 2 && isDigit(byteAt(data, pos - 1));
                digitAfter = pos + length < limit && isDigit(byteAt(data, pos + length));
            } else {
                digitBefore = pos >= 4 && isDigit(byteAt(data, pos - 2)) && byteAt(data, pos - 1) == 0
                           && isDigit(byteAt(data, pos - 4)) && byteAt(data, pos - 3) == 0;
                digitAfter = pos + length + 1 < limit && isDigit(byteAt(data, pos + length))
                          && byteAt(data, pos + length + 1) == 0;
            }
            if (digitBefore || digitAfter)
                continue;
            std::int64_t end = pos + length;
            int slack = 0;
            while (end + width <= limit) {
                bool blank = true;
                for (std::int64_t k = 0; k < width && blank; ++k)
                    blank = byteAt(data, end + k) == 0;
                if (!blank)
                    break;
                end += width;
                ++slack;
            }
            add(kind, pos, id, slack);
        }
    }
    return sites;
}

std::string siteValue(std::string_view data, const Site &site)
{
    const std::int64_t limit = static_cast<std::int64_t>(data.size());
    if (site.off < 0 || site.off >= limit)
        return std::string();
    if (site.kind == Kind::Imm64 || site.kind == Kind::U64le) {
        if (site.off + 8 > limit)
            return std::string();
        return std::to_string(le64(data, site.off));
    }
    const std::int64_t width = site.kind == Kind::Ascii ? 1 : 2;
    std::string text;
    for (std::int64_t p = site.off; p + width <= limit; p += width) {
        if (!isDigit(byteAt(data, p)))
            break;
        if (width == 2 && byteAt(data, p + 1) != 0)
            break;
        text.push_back(static_cast<char>(byteAt(data, p)));
    }
    return text;
}

// -------------------------------------------------------------------- writing

bool fits(const Site &site, const std::string &newId, std::string *why)
{
    if (site.kind == Kind::Imm64 || site.kind == Kind::U64le) {
        if (why)
            *why = "8-byte integer, any ID fits";
        return true;
    }
    const int growth = static_cast<int>(newId.size()) - static_cast<int>(site.text.size());
    if (growth <= 0) {
        if (why)
            *why = "same length or shorter";
        return true;
    }
    if (site.slack >= growth + 1) {
        if (why)
            *why = std::to_string(site.slack) + " spare NULs";
        return true;
    }
    if (why)
        *why = std::to_string(growth) + " digit(s) longer, " + std::to_string(site.slack)
             + " spare NULs";
    return false;
}

WriteReport writeSites(char *buf, std::size_t size, const std::vector<Site> &sites,
                       const std::string &newId)
{
    WriteReport report;
    std::uint64_t value = 0;
    const bool numeric = parseId(newId, &value);
    for (const Site &site : sites) {
        std::string why;
        if (!fits(site, newId, &why)) {
            report.skipped.emplace_back(site, why);
            continue;
        }
        if (site.kind == Kind::Imm64 || site.kind == Kind::U64le) {
            if (!numeric || site.off < 0 || std::uint64_t(site.off) + 8 > size) {
                report.skipped.emplace_back(site, std::string("outside the file"));
                continue;
            }
            putLe64(buf + site.off, value);
        } else {
            const std::size_t width = site.kind == Kind::Ascii ? 1u : 2u;
            const std::size_t span = (site.text.size() + std::size_t(site.slack)) * width;
            if (site.off < 0 || std::uint64_t(site.off) + span > size) {
                report.skipped.emplace_back(site, std::string("outside the file"));
                continue;
            }
            std::string blob;
            blob.reserve((newId.size() + 1) * width);
            for (const char c : newId) {
                blob.push_back(c);
                if (width == 2)
                    blob.push_back('\0');
            }
            blob.append(width, '\0');          // terminator
            blob.resize(span, '\0');           // pad with NULs / drop what does not fit
            std::memcpy(buf + site.off, blob.data(), span);
        }
        report.done.push_back(site);
    }
    return report;
}

} // namespace RpcIdScan
