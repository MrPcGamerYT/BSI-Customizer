#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/**
 * Discord Application ID scanner for emulator binaries (HD-Player.exe & co).
 *
 * The Discord Game SDK embeds the application ID as a little-endian 64-bit
 * integer (as a mov r64,imm64 immediate or as plain data), while classic
 * discord-rpc builds keep it as ASCII or UTF-16 decimal text. This engine
 * finds every stored form, scores the evidence, and rewrites them in place so
 * the file size never changes.
 *
 * It is deliberately free of Qt (plain C++17, byte views in / plain structs
 * out). The detection heuristics are the delicate part of the feature, so they
 * stay compilable and testable with nothing but a compiler. RpcIdPatcher is
 * the Qt facade that adds file I/O, backups and the remembered patch state on
 * top, and cli.cpp exposes both to scripts.
 */
namespace RpcIdScan {

inline constexpr std::uint64_t kDiscordEpoch = 1420070400000ULL;

// How one copy of an ID is stored in the file.
enum class Kind {
    Imm64,    // mov r64, imm64 immediate
    U64le,    // bare little-endian qword (any alignment)
    Ascii,    // decimal digits, one byte per digit
    Utf16le,  // decimal digits, UTF-16LE
};

const char *kindName(Kind kind);   // "imm64" | "u64le" | "ascii" | "utf16le"
const char *kindLabel(Kind kind);  // UI wording: "integer" | "text (ASCII)" | ...

struct Site
{
    Kind kind = Kind::U64le;
    std::int64_t off = 0;    // file offset of the stored value
    std::string text;        // ascii/utf16le: the digits actually stored there
    int slack = 0;           // ascii/utf16le: spare NUL units right behind it
    std::string section;     // PE section name, empty when unknown
    bool rip = false;        // a RIP-relative instruction points at this qword
};

struct Candidate
{
    std::string id;             // decimal Application ID
    std::vector<Site> sites;    // every place it is stored
    int score = 0;              // ranking evidence
    int refs = 0;               // number of sites
    std::string date;           // snowflake creation date (YYYY-MM-DD)
    bool confident = false;     // at least two independent signals
    bool eligible = false;      // may be acted on without the user naming it
    std::string known;          // documented ID ("BlueStacks 5 / MSI App Player")
};

/** Minimal PE reader: sections, RVA mapping and the signature directory. */
class Pe
{
public:
    explicit Pe(std::string_view data);

    bool ok() const { return m_ok; }
    const char *arch() const;           // "x64" | "x86" | "arm64" | "?"
    bool isSigned() const;
    bool hasSections() const { return !m_sections.empty(); }

    // Data directory entry (RVA, size); 2 = resources, 4 = signature.
    std::pair<std::uint32_t, std::uint32_t> dataDirectory(int index) const { return directory(index); }

    // Section name for a file offset plus whether it is executable code.
    std::pair<std::string, bool> sectionOf(std::int64_t off) const;
    std::int64_t rvaToOffset(std::int64_t rva) const;   // -1 when unmapped
    std::int64_t offsetToRva(std::int64_t off) const;   // -1 when unmapped

    struct Span
    {
        std::int64_t off = 0;      // first file offset of the section data
        std::int64_t size = 0;     // bytes of raw data
        std::int64_t rva = 0;      // virtual address the raw data maps to
        bool exec = false;         // executable code section
    };

    // File spans of every section, in header order.
    std::vector<Span> spans() const;

private:
    struct Section
    {
        std::string name;
        std::uint32_t va = 0;
        std::uint32_t vsize = 0;
        std::uint32_t raw = 0;
        std::uint32_t rawSize = 0;
        std::uint32_t chars = 0;
    };

    std::pair<std::uint32_t, std::uint32_t> directory(int index) const;

    std::string_view m_data;
    std::vector<Section> m_sections;
    bool m_ok = false;
    std::uint32_t m_pe = 0;
    std::uint32_t m_dd = 0;
    std::uint16_t m_machine = 0;
};

// ---------------------------------------------------------------- shape tests

bool plausible(std::uint64_t value);          // snowflake-shaped at all?
bool compilerConstant(std::uint64_t value);   // round/patterned junk number
bool isAppIdText(const std::string &id);      // 17-20 digits, snowflake-shaped
std::string snowflakeDate(const std::string &id);
std::string knownName(const std::string &id); // "" when not a documented ID

// ------------------------------------------------------------------ discovery

/** Every Application-ID-shaped value in `data`, best candidate first. */
std::vector<Candidate> harvest(std::string_view data, const Pe *pe = nullptr);

struct Pick
{
    int index = -1;            // into the harvest() result, -1 = refuse to guess
    std::vector<int> strong;   // indexes of all candidates worth showing
};

/** The candidate automatic actions may act on: strong, eligible and un-tied. */
Pick pickCandidate(const std::vector<Candidate> &records);

/**
 * Every place `id` is stored right now, with freshly measured spare room.
 * Re-derived from the bytes on disk (never from an older scan), which is what
 * makes re-patching an already patched file reliable.
 */
std::vector<Site> sitesForId(std::string_view data, const std::string &id);

/** The ID currently stored at `site`, or "" when it no longer holds one. */
std::string siteValue(std::string_view data, const Site &site);

// ------------------------------------------------------------------- patching

/** Can `newId` be written over `site` without changing the file size? */
bool fits(const Site &site, const std::string &newId, std::string *why = nullptr);

struct WriteReport
{
    std::vector<Site> done;
    std::vector<std::pair<Site, std::string>> skipped;  // site + reason
};

/** Overwrite every site that fits, in place. `buf` keeps its size. */
WriteReport writeSites(char *buf, std::size_t size, const std::vector<Site> &sites,
                       const std::string &newId);

} // namespace RpcIdScan
