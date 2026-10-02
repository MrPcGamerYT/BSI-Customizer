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

inline std::uint64_t be64(std::string_view d, std::int64_t off)
{
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v = (v << 8) | byteAt(d, off + i);
    return v;
}

inline void putLe64(char *out, std::uint64_t value)
{
    for (int i = 0; i < 8; ++i)
        out[i] = static_cast<char>((value >> (8 * i)) & 0xFF);
}

inline void putBe64(char *out, std::uint64_t value)
{
    for (int i = 0; i < 8; ++i)
        out[7 - i] = static_cast<char>((value >> (8 * i)) & 0xFF);
}

inline void putLe32(char *out, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        out[i] = static_cast<char>((value >> (8 * i)) & 0xFF);
}

std::string hexEncode(std::string_view bytes)
{
    static const char *digits = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const unsigned char c : bytes) {
        out.push_back(digits[c >> 4]);
        out.push_back(digits[c & 0xF]);
    }
    return out;
}

std::string hexDecode(const std::string &hex)
{
    const auto value = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        const int hi = value(hex[i]);
        const int lo = value(hex[i + 1]);
        if (hi < 0 || lo < 0)
            break;
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return out;
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

// --------------------------------------------------------------- gathering
//
// Every scan* helper below only appends sites to a Harvester; scoring and
// ranking are entirely separate (finalizeAndRank), so harvest() (binary
// evidence) and harvestText() (decompressed text blobs) can share one
// implementation of "how good is this candidate" without sharing how its
// sites were found.
struct Harvester
{
    std::vector<Candidate> records;
    std::unordered_map<std::string, std::size_t> index;
    std::set<std::pair<int, std::int64_t>> added;

    void add(std::uint64_t value, Kind kind, std::int64_t off, std::string text = {},
            int slack = 0, std::int64_t hiOff = -1)
    {
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
        site.hiOff = hiOff;
        site.text = std::move(text);
        site.slack = slack;
        records[hit->second].sites.push_back(std::move(site));
    }
};

// x64 mov r64, imm64 (48/49 B8..BF), including non-RAX registers. The Game
// SDK's client_id is an int64, so this is the form a 64-bit Game SDK build
// uses; nothing below depends on it also being the ONLY form present.
void scanImm64(std::string_view data, Harvester &h)
{
    const std::int64_t limit = static_cast<std::int64_t>(data.size());
    for (std::int64_t i = 0; i + 2 <= limit;) {
        const unsigned char b0 = byteAt(data, i);
        const unsigned char b1 = byteAt(data, i + 1);
        if ((b0 == 0x48 || b0 == 0x49) && b1 >= 0xB8 && b1 <= 0xBF) {
            if (i + 10 <= limit) {
                const std::uint64_t value = le64(data, i + 2);
                if (plausible(value) && !compilerConstant(value))
                    h.add(value, Kind::Imm64, i + 2);
            }
            i += 2;
        } else {
            ++i;
        }
    }
}

// Every 32-bit immediate load/store/push cheap enough to decode without a
// full disassembler - the building blocks an x86 build uses to construct a
// 64-bit id from two 32-bit halves.
std::vector<std::pair<std::int64_t, std::uint32_t>>
imm32Slots(std::string_view data, std::int64_t lo, std::int64_t hi)
{
    std::vector<std::pair<std::int64_t, std::uint32_t>> out;
    std::int64_t i = lo;
    while (i + 5 <= hi) {
        const unsigned char b = byteAt(data, i);
        if (b == 0x68) {                             // push imm32
            out.emplace_back(i + 1, le32(data, i + 1));
            i += 5;
            continue;
        }
        if (b >= 0xB8 && b <= 0xBF) {                 // mov r32, imm32
            out.emplace_back(i + 1, le32(data, i + 1));
            i += 5;
            continue;
        }
        if (b == 0xC7 && i + 8 <= hi) {
            const unsigned char modrm = byteAt(data, i + 1);
            if (modrm == 0x44) {                      // mov [base+idx*s+disp8], imm32 (SIB)
                out.emplace_back(i + 4, le32(data, i + 4));
                i += 8;
                continue;
            }
            if ((modrm == 0x45 || modrm == 0x40 || modrm == 0x41 || modrm == 0x42
                 || modrm == 0x43 || modrm == 0x46 || modrm == 0x47) && i + 7 <= hi) {
                out.emplace_back(i + 3, le32(data, i + 3));
                i += 7;
                continue;
            }
        }
        ++i;
    }
    return out;
}

// A 64-bit id built from two 32-bit immediates close together (32-bit
// builds). Both orderings are tried since either half may be set first.
void scanSplit32(std::string_view data, Harvester &h,
                 const std::vector<std::pair<std::int64_t, std::int64_t>> &ranges,
                 int window = 28, int cap = 4000)
{
    int hits = 0;
    for (const auto &range : ranges) {
        const auto slots = imm32Slots(data, range.first, range.second);
        for (std::size_t a = 0; a + 1 < slots.size(); ++a) {
            const std::size_t bEnd = std::min(slots.size(), a + 4);
            for (std::size_t b = a + 1; b < bEnd; ++b) {
                if (slots[b].first - slots[a].first > window)
                    break;
                const struct { std::uint32_t low, high; std::int64_t loOff, hiOff; } combos[2] = {
                    { slots[a].second, slots[b].second, slots[a].first, slots[b].first },
                    { slots[b].second, slots[a].second, slots[b].first, slots[a].first },
                };
                for (const auto &combo : combos) {
                    if (combo.high == 0 || combo.low == 0)
                        continue;
                    const std::uint64_t value = (std::uint64_t(combo.high) << 32) | combo.low;
                    if (plausible(value) && !compilerConstant(value)) {
                        h.add(value, Kind::Split32, combo.loOff, {}, 0, combo.hiOff);
                        ++hits;
                    }
                }
                if (hits > cap)
                    return;
            }
        }
    }
}

// Plain 8-byte integers sitting in data, either byte order, at any 8-byte
// alignment. A window that is entirely printable ASCII is text, handled by
// scanText instead; one already covered by an imm64/split32 instruction is
// skipped so the same bytes are not reported twice.
void scanU64(std::string_view data, Harvester &h, const std::set<std::int64_t> &claimed)
{
    const std::int64_t limit = static_cast<std::int64_t>(data.size());
    for (std::int64_t off = 0; off + 8 <= limit; off += 8) {
        if (claimed.count(off))
            continue;
        bool printable = true;
        for (int k = 0; k < 8 && printable; ++k) {
            const unsigned char ch = byteAt(data, off + k);
            printable = ch >= 0x20 && ch <= 0x7E;
        }
        if (printable)
            continue;
        const std::uint64_t vLe = le64(data, off);
        if (plausible(vLe) && !compilerConstant(vLe))
            h.add(vLe, Kind::U64le, off);
        const std::uint64_t vBe = be64(data, off);
        if (plausible(vBe) && !compilerConstant(vBe))
            h.add(vBe, Kind::U64be, off);
    }
}

// Decimal text, UTF-16 in either byte order. A complete 17-20 digit run is
// required so a longer or shorter number can never be misread as an id, and
// junk-shaped values are rejected here too, exactly like the integer forms.
void scanAscii(std::string_view data, Harvester &h)
{
    const std::int64_t limit = static_cast<std::int64_t>(data.size());
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
                h.add(value, Kind::Ascii, i, text, slack);
            }
        }
        i = end;
    }
}

// UTF-16, either byte order: measure the WHOLE digit run, reject runs longer
// than 20 digits instead of silently reading their first 20 pairs as an id,
// and require TWO preceding digit pairs before calling a run "embedded in a
// longer number" (one pair alone can be a phantom, e.g. the last digit+NUL of
// a neighbouring ASCII id sitting directly in front of this one).
void scanUtf16(std::string_view data, Harvester &h, bool bigEndian)
{
    const std::int64_t limit = static_cast<std::int64_t>(data.size());
    const int hiIdx = bigEndian ? 0 : 1;   // index of the always-zero byte in the pair
    const int loIdx = bigEndian ? 1 : 0;   // index of the digit byte in the pair
    const auto digitPair = [&](std::int64_t p) {
        return p >= 0 && p + 1 < limit && byteAt(data, p + hiIdx) == 0
            && isDigit(byteAt(data, p + loIdx));
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
                text.push_back(static_cast<char>(byteAt(data, start + 2 * k + loIdx)));
            std::uint64_t value = 0;
            if (parseId(text, &value) && plausible(value) && !compilerConstant(value)) {
                std::int64_t tail = start + 2 * pairs;
                int slack = 0;
                while (tail + 1 < limit && byteAt(data, tail) == 0 && byteAt(data, tail + 1) == 0) {
                    tail += 2;
                    ++slack;
                }
                h.add(value, bigEndian ? Kind::Utf16be : Kind::Utf16le, start, text, slack);
                pos = start + 2 * pairs;
                accepted = true;
            }
        }
        if (!accepted)
            pos = start + 2;
    }
}

bool isImmediateKind(Kind kind) { return kind == Kind::Imm64 || kind == Kind::Split32; }
bool isBareQwordKind(Kind kind) { return kind == Kind::U64le || kind == Kind::U64be; }
bool isTextKind(Kind kind)
{
    return kind == Kind::Ascii || kind == Kind::Utf16le || kind == Kind::Utf16be;
}

// Scores and ranks whatever scan* already found; shared by harvest() (full
// binary evidence) and harvestText() (decompressed text blobs, usePe=false).
std::vector<Candidate> finalizeAndRank(std::vector<Candidate> records, std::string_view data,
                                       const Pe *pe)
{
    const bool usePe = pe != nullptr && pe->ok();
    const std::int64_t limit = static_cast<std::int64_t>(data.size());

    // Addresses the code actually references (RIP-relative). Used only as a
    // ranking bonus for bare qwords: a referenced timestamp or hash is
    // everywhere, so this alone never admits a candidate on its own.
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

    // Where Discord-related data lives; used only as a ranking bonus.
    std::vector<std::int64_t> discord;
    for (std::int64_t i = 0; i + 7 <= limit;) {
        bool hit = true;
        static const char needle[] = "discord";
        for (int k = 0; k < 7 && hit; ++k) {
            unsigned char ch = byteAt(data, i + k);
            if (ch >= 'A' && ch <= 'Z')
                ch = static_cast<unsigned char>(ch - 'A' + 'a');
            hit = ch == static_cast<unsigned char>(needle[k]);
        }
        if (hit) {
            discord.push_back(i);
            i += 7;
        } else {
            ++i;
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
    // immediate load in executable code - outweigh ambient integers, and a
    // documented Application ID is a confirmed match on its own, exactly the
    // way the reference scanner treats it, so it carries a decisive bonus
    // instead of competing on generic heuristics alone and losing a
    // coin-flip tie against unrelated noise in the file.
    for (Candidate &record : records) {
        bool text = false, immediate = false, immExec = false, qword = false;
        int qwords = 0;
        bool near = false, ripRef = false;
        std::set<int> kinds;
        for (Site &site : record.sites) {
            kinds.insert(int(site.kind));
            if (isTextKind(site.kind))
                text = true;
            else if (isImmediateKind(site.kind))
                immediate = true;
            else if (isBareQwordKind(site.kind)) {
                qword = true;
                ++qwords;
            }
            if (usePe) {
                const auto section = pe->sectionOf(site.off);
                site.section = section.first;
                if (isImmediateKind(site.kind) && section.second)
                    immExec = true;
                if (isBareQwordKind(site.kind)) {
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
    // constants; one is usually the other shifted by a few bytes, or the same
    // bytes read in the other byte order. The one with less evidence is
    // demoted, and a dead tie demotes both - guessing here is what used to
    // make the detector show a wrong ID. A documented ID is never demoted.
    {
        std::vector<std::pair<std::int64_t, std::size_t>> bare;
        for (std::size_t r = 0; r < records.size(); ++r) {
            const Candidate &record = records[r];
            if (record.score < 3 || record.sites.empty() || !record.known.empty())
                continue;
            bool onlyQwords = true;
            for (const Site &site : record.sites)
                onlyQwords = onlyQwords && isBareQwordKind(site.kind);
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

} // namespace

// ---------------------------------------------------------------------- kinds

const char *kindName(Kind kind)
{
    switch (kind) {
    case Kind::Imm64:   return "imm64";
    case Kind::Split32: return "split32";
    case Kind::U64le:   return "u64le";
    case Kind::U64be:   return "u64be";
    case Kind::Ascii:   return "ascii";
    case Kind::Utf16le: return "utf16le";
    case Kind::Utf16be: return "utf16be";
    }
    return "?";
}

const char *kindLabel(Kind kind)
{
    switch (kind) {
    case Kind::Imm64:   return "code constant";
    case Kind::Split32: return "code constant (two halves)";
    case Kind::U64le:   return "integer";
    case Kind::U64be:   return "integer (big-endian)";
    case Kind::Ascii:   return "text";
    case Kind::Utf16le: return "wide text";
    case Kind::Utf16be: return "wide text (big-endian)";
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

std::vector<std::pair<std::int64_t, std::int64_t>> Pe::codeRanges() const
{
    std::vector<std::pair<std::int64_t, std::int64_t>> ranges;
    for (const Section &s : m_sections) {
        if ((s.chars & 0x20000000u) != 0 && s.rawSize)
            ranges.emplace_back(std::int64_t(s.raw), std::int64_t(s.raw) + std::int64_t(s.rawSize));
    }
    return ranges;
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

std::vector<Candidate> harvest(std::string_view data, const Pe *pe, SplitMode split)
{
    if (data.size() < 8)
        return {};

    Harvester h;
    scanImm64(data, h);

    const bool usePe = pe != nullptr && pe->ok();
    const bool doSplit = split == SplitMode::On
                       || (split == SplitMode::Auto && usePe && pe->is32());
    if (doSplit) {
        std::vector<std::pair<std::int64_t, std::int64_t>> ranges;
        if (usePe)
            ranges = pe->codeRanges();
        if (ranges.empty())
            ranges.emplace_back(0, static_cast<std::int64_t>(data.size()));
        scanSplit32(data, h, ranges);
    }

    // Bytes already explained by a decoded instruction are not reconsidered
    // as a bare integer, so the same value is not reported twice.
    std::set<std::int64_t> claimed;
    for (const Candidate &record : h.records) {
        for (const Site &site : record.sites) {
            if (!isImmediateKind(site.kind))
                continue;
            for (std::int64_t x = site.off - 7; x <= site.off + 7; ++x)
                claimed.insert(x);
        }
    }
    scanU64(data, h, claimed);
    scanAscii(data, h);
    scanUtf16(data, h, false);
    scanUtf16(data, h, true);

    // A real client_id is referenced once or twice. Division reciprocals and
    // other compiler constants show up all over the code, so drop any value
    // that appears as an immediate more than three times with no text form
    // to back it up.
    std::vector<Candidate> records;
    records.reserve(h.records.size());
    for (Candidate &record : h.records) {
        int immCount = 0;
        bool hasText = false;
        for (const Site &site : record.sites) {
            if (isImmediateKind(site.kind))
                ++immCount;
            else if (isTextKind(site.kind))
                hasText = true;
        }
        if (immCount > 3 && !hasText)
            continue;
        records.push_back(std::move(record));
    }

    return finalizeAndRank(std::move(records), data, pe);
}

std::vector<Candidate> harvestText(std::string_view data)
{
    Harvester h;
    scanAscii(data, h);
    scanUtf16(data, h, false);
    scanUtf16(data, h, true);
    return finalizeAndRank(std::move(h.records), data, nullptr);
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
        char packedLe[8];
        putLe64(packedLe, value);
        const std::string_view needleLe(packedLe, 8);
        for (std::size_t pos = data.find(needleLe); pos != std::string_view::npos;
             pos = data.find(needleLe, pos + 1)) {
            const std::int64_t off = static_cast<std::int64_t>(pos);
            const bool isImm = off >= 2 && (byteAt(data, off - 2) == 0x48 || byteAt(data, off - 2) == 0x49)
                             && byteAt(data, off - 1) >= 0xB8 && byteAt(data, off - 1) <= 0xBF;
            add(isImm ? Kind::Imm64 : Kind::U64le, off);
        }
        char packedBe[8];
        putBe64(packedBe, value);
        const std::string_view needleBe(packedBe, 8);
        for (std::size_t pos = data.find(needleBe); pos != std::string_view::npos;
             pos = data.find(needleBe, pos + 1))
            add(Kind::U64be, static_cast<std::int64_t>(pos));
    }

    std::string wideLe;
    wideLe.reserve(id.size() * 2);
    for (const char c : id) {
        wideLe.push_back(c);
        wideLe.push_back('\0');
    }
    std::string wideBe;
    wideBe.reserve(id.size() * 2);
    for (const char c : id) {
        wideBe.push_back('\0');
        wideBe.push_back(c);
    }
    const std::pair<Kind, const std::string *> forms[] = {
        { Kind::Ascii, &id },
        { Kind::Utf16le, &wideLe },
        { Kind::Utf16be, &wideBe },
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
            // phantom, e.g. the last byte pair of a neighbouring ASCII id
            // string ("9\0") sitting directly in front of a UTF-16 copy.
            bool digitBefore = false;
            bool digitAfter = false;
            if (width == 1) {
                digitBefore = pos >= 2 && isDigit(byteAt(data, pos - 1));
                digitAfter = pos + length < limit && isDigit(byteAt(data, pos + length));
            } else if (kind == Kind::Utf16le) {
                digitBefore = pos >= 4 && isDigit(byteAt(data, pos - 2)) && byteAt(data, pos - 1) == 0
                           && isDigit(byteAt(data, pos - 4)) && byteAt(data, pos - 3) == 0;
                digitAfter = pos + length + 1 < limit && isDigit(byteAt(data, pos + length))
                          && byteAt(data, pos + length + 1) == 0;
            } else {    // Utf16be
                digitBefore = pos >= 4 && byteAt(data, pos - 2) == 0 && isDigit(byteAt(data, pos - 1))
                           && byteAt(data, pos - 4) == 0 && isDigit(byteAt(data, pos - 3));
                digitAfter = pos + length + 1 < limit && byteAt(data, pos + length) == 0
                          && isDigit(byteAt(data, pos + length + 1));
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
    if (site.kind == Kind::U64be) {
        if (site.off + 8 > limit)
            return std::string();
        return std::to_string(be64(data, site.off));
    }
    if (site.kind == Kind::Split32) {
        if (site.off + 4 > limit || site.hiOff < 0 || site.hiOff + 4 > limit)
            return std::string();
        const std::uint64_t low = le32(data, site.off);
        const std::uint64_t high = le32(data, site.hiOff);
        return std::to_string((high << 32) | low);
    }
    const std::int64_t width = site.kind == Kind::Ascii ? 1 : 2;
    const int digitIdx = site.kind == Kind::Utf16be ? 1 : 0;
    const int zeroIdx = site.kind == Kind::Utf16be ? 0 : 1;
    std::string text;
    for (std::int64_t p = site.off; p + width <= limit; p += width) {
        if (width == 1) {
            if (!isDigit(byteAt(data, p)))
                break;
            text.push_back(static_cast<char>(byteAt(data, p)));
        } else {
            if (!isDigit(byteAt(data, p + digitIdx)) || byteAt(data, p + zeroIdx) != 0)
                break;
            text.push_back(static_cast<char>(byteAt(data, p + digitIdx)));
        }
    }
    return text;
}

// --------------------------------------------------------- slot relocation

Signature makeSignature(std::string_view data, std::int64_t off, int gap, int contextBytes,
                        std::string name)
{
    Signature sig;
    sig.name = std::move(name);
    sig.gap = gap;
    const std::int64_t limit = static_cast<std::int64_t>(data.size());
    const std::int64_t preStart = std::max<std::int64_t>(0, off - contextBytes);
    const std::int64_t preLen = off - preStart;
    const std::int64_t postStart = off + gap;
    const std::int64_t postEnd = std::min<std::int64_t>(limit, postStart + contextBytes);
    const std::int64_t postLen = std::max<std::int64_t>(0, postEnd - postStart);
    if (preLen > 0)
        sig.pre = hexEncode(data.substr(std::size_t(preStart), std::size_t(preLen)));
    if (postLen > 0 && postStart >= 0 && postStart <= limit)
        sig.post = hexEncode(data.substr(std::size_t(postStart), std::size_t(postLen)));
    return sig;
}

SignatureMatch findSignature(std::string_view data, const Signature &sig, int minContext)
{
    SignatureMatch result;
    const std::string pre = hexDecode(sig.pre);
    const std::string post = hexDecode(sig.post);
    const int gap = sig.gap > 0 ? sig.gap : 8;
    const std::int64_t limit = static_cast<std::int64_t>(data.size());
    int n = static_cast<int>(std::min(pre.size(), post.size()));
    for (; n >= minContext; n -= 2) {
        const std::string_view preTail(pre.data() + pre.size() - std::size_t(n), std::size_t(n));
        const std::string_view postHead(post.data(), std::size_t(n));
        std::vector<std::int64_t> hits;
        for (std::int64_t pos = n; pos + gap + n <= limit; ++pos) {
            if (data.compare(std::size_t(pos - n), std::size_t(n), preTail) != 0)
                continue;
            if (data.compare(std::size_t(pos + gap), std::size_t(n), postHead) != 0)
                continue;
            hits.push_back(pos);
            if (hits.size() > 64)      // ambiguous enough already, stop looking
                break;
        }
        if (!hits.empty()) {
            result.offsets = std::move(hits);
            result.contextUsed = n;
            return result;
        }
    }
    return result;
}

const std::vector<Signature> &builtinSignatures()
{
    static const std::vector<Signature> table = { {
        "BlueStacks 5 / MSI App Player - HD-Player.exe (Game SDK)",
        "c0743c48c70000000000488d4b1848b8",
        "c6431000ba0201000048894308e84c19",
        8,
    } };
    return table;
}

std::vector<std::int64_t> diffRuns(std::string_view current, std::string_view backup,
                                   int maxRun, int limit)
{
    std::vector<std::int64_t> runs;
    if (current.size() != backup.size())
        return runs;
    const std::int64_t n = static_cast<std::int64_t>(current.size());
    std::int64_t start = -1;
    for (std::int64_t i = 0; i < n; ++i) {
        if (current[std::size_t(i)] != backup[std::size_t(i)]) {
            if (start < 0)
                start = i;
        } else if (start >= 0) {
            if (i - start <= maxRun)
                runs.push_back(start);
            start = -1;
        }
        if (static_cast<int>(runs.size()) > limit)
            break;
    }
    return runs;
}

// -------------------------------------------------------------------- writing

bool fits(const Site &site, const std::string &newId, std::string *why)
{
    if (site.packed) {
        if (why)
            *why = "inside a compressed blob";
        return false;
    }
    if (site.kind == Kind::Imm64 || site.kind == Kind::U64le || site.kind == Kind::U64be
        || site.kind == Kind::Split32) {
        if (why)
            *why = "fixed 8 bytes, any ID fits";
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
        } else if (site.kind == Kind::U64be) {
            if (!numeric || site.off < 0 || std::uint64_t(site.off) + 8 > size) {
                report.skipped.emplace_back(site, std::string("outside the file"));
                continue;
            }
            putBe64(buf + site.off, value);
        } else if (site.kind == Kind::Split32) {
            if (!numeric || site.off < 0 || std::uint64_t(site.off) + 4 > size
                || site.hiOff < 0 || std::uint64_t(site.hiOff) + 4 > size) {
                report.skipped.emplace_back(site, std::string("outside the file"));
                continue;
            }
            putLe32(buf + site.off, static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));
            putLe32(buf + site.hiOff, static_cast<std::uint32_t>((value >> 32) & 0xFFFFFFFFULL));
        } else {
            const bool bigEndian = site.kind == Kind::Utf16be;
            const std::size_t width = site.kind == Kind::Ascii ? 1u : 2u;
            const std::size_t span = (site.text.size() + std::size_t(site.slack)) * width;
            if (site.off < 0 || std::uint64_t(site.off) + span > size) {
                report.skipped.emplace_back(site, std::string("outside the file"));
                continue;
            }
            std::string blob;
            blob.reserve((newId.size() + 1) * width);
            for (const char c : newId) {
                if (width == 1) {
                    blob.push_back(c);
                } else if (bigEndian) {
                    blob.push_back('\0');
                    blob.push_back(c);
                } else {
                    blob.push_back(c);
                    blob.push_back('\0');
                }
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
