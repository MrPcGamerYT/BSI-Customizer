#include "VersionResource.h"

#include "RpcIdScan.h"

#include <algorithm>

namespace VersionResource {
namespace {

inline unsigned char byteAt(std::string_view d, std::int64_t off)
{
    return static_cast<unsigned char>(d[static_cast<std::size_t>(off)]);
}

inline bool has(std::string_view d, std::int64_t off, std::int64_t count)
{
    return off >= 0 && count >= 0
        && std::uint64_t(off) + std::uint64_t(count) <= std::uint64_t(d.size());
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

inline std::int64_t align4(std::int64_t n) { return (n + 3) & ~std::int64_t(3); }

// ---------------------------------------------------------------------------
// Resource directory: find the first RT_VERSION (type 16) data entry.
// Only the structure is walked; nothing is executed or relocated.
// ---------------------------------------------------------------------------
struct Found
{
    bool ok = false;
    std::int64_t off = 0;
    std::int64_t size = 0;
};

void walkDirectory(std::string_view data, const RpcIdScan::Pe &pe, std::int64_t base,
                   std::int64_t node, int depth, std::uint32_t type, Found *result)
{
    if (result->ok || depth > 3 || !has(data, node, 16))
        return;
    const std::int64_t count = std::int64_t(le16(data, node + 12)) + std::int64_t(le16(data, node + 14));
    for (std::int64_t i = 0; i < count; ++i) {
        const std::int64_t entry = node + 16 + i * 8;
        if (!has(data, entry, 8))
            return;
        const std::uint32_t name = le32(data, entry);
        const std::uint32_t child = le32(data, entry + 4);
        const std::uint32_t kind = (depth == 0) ? name : type;
        if (child & 0x80000000u) {
            walkDirectory(data, pe, base, base + std::int64_t(child & 0x7FFFFFFFu),
                          depth + 1, kind, result);
            if (result->ok)
                return;
            continue;
        }
        // Leaf: a RESOURCE_DATA_ENTRY, only interesting below type 16.
        if (depth == 0 || kind != 16)
            continue;
        const std::int64_t leaf = base + std::int64_t(child);
        if (!has(data, leaf, 8))
            continue;
        const std::int64_t dataOff = pe.rvaToOffset(std::int64_t(le32(data, leaf)));
        const std::int64_t dataSize = std::int64_t(le32(data, leaf + 4));
        if (dataOff < 0 || dataSize <= 0 || !has(data, dataOff, dataSize))
            continue;
        result->ok = true;
        result->off = dataOff;
        result->size = dataSize;
        return;
    }
}

// ---------------------------------------------------------------------------
// VS_VERSION_INFO node tree: {wLength, wValueLength, wType, szKey, Value, kids}
// every node and value aligned to 4 bytes, relative to the resource base.
// ---------------------------------------------------------------------------
void walkStrings(std::string_view data, std::int64_t base, std::int64_t off, std::int64_t limit,
                 bool inStringTable, int depth, std::vector<StringEntry> *out)
{
    if (depth > 4)
        return;
    while (off + 6 <= limit) {
        if (!has(data, off, 6))
            return;
        const std::int64_t wlen = le16(data, off);
        const std::int64_t vlen = le16(data, off + 2);
        const std::uint16_t type = le16(data, off + 4);
        // The last sibling may be padded past the parent's rounded length.
        if (wlen < 6 || off + wlen > limit + 3)
            return;

        std::int64_t k = off + 6;
        while (k + 1 < limit && has(data, k, 2) && !(byteAt(data, k) == 0 && byteAt(data, k + 1) == 0))
            k += 2;
        if (!has(data, k, 2))
            return;
        const std::string key = utf16leToUtf8(data.substr(static_cast<std::size_t>(off + 6),
                                                          static_cast<std::size_t>(k - (off + 6))));
        const std::int64_t value = base + align4(k + 2 - base);
        const std::int64_t valueBytes = (type == 1) ? vlen * 2 : vlen;
        if (!has(data, value, valueBytes))
            return;

        if (inStringTable && type == 1 && vlen > 0 && !key.empty()) {
            StringEntry entry;
            entry.key = key;
            entry.off = value;
            entry.cap = int(vlen);
            std::string text = utf16leToUtf8(data.substr(static_cast<std::size_t>(value),
                                                         static_cast<std::size_t>(valueBytes)));
            while (!text.empty() && text.back() == '\0')
                text.pop_back();
            entry.value = text;
            out->push_back(std::move(entry));
        }

        const std::int64_t child = base + align4(value + valueBytes - base);
        if (child < off + wlen) {
            walkStrings(data, base, child, off + wlen,
                        inStringTable || key == "StringFileInfo", depth + 1, out);
        }
        const std::int64_t next = base + align4(off + wlen - base);
        if (next <= off)
            return;  // malformed: never loop forever on a zero-length node
        off = next;
    }
}

} // namespace

// ------------------------------------------------------------------ encodings

std::string utf16leToUtf8(std::string_view utf16le)
{
    std::string out;
    out.reserve(utf16le.size());
    for (std::size_t i = 0; i + 1 < utf16le.size(); i += 2) {
        std::uint32_t cp = std::uint32_t(static_cast<unsigned char>(utf16le[i]))
                         | (std::uint32_t(static_cast<unsigned char>(utf16le[i + 1])) << 8);
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 3 < utf16le.size()) {
            const std::uint32_t low = std::uint32_t(static_cast<unsigned char>(utf16le[i + 2]))
                                    | (std::uint32_t(static_cast<unsigned char>(utf16le[i + 3])) << 8);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                i += 2;
            }
        }
        if (cp >= 0xD800 && cp <= 0xDFFF)
            cp = 0xFFFD;  // lone surrogate: display as the replacement char
        if (cp < 0x80) {
            out.push_back(char(cp));
        } else if (cp < 0x800) {
            out.push_back(char(0xC0 | (cp >> 6)));
            out.push_back(char(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(char(0xE0 | (cp >> 12)));
            out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(char(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(char(0xF0 | (cp >> 18)));
            out.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(char(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

std::string utf8ToUtf16le(std::string_view utf8)
{
    std::string out;
    out.reserve(utf8.size() * 2);
    const auto push = [&out](std::uint32_t unit) {
        out.push_back(char(unit & 0xFF));
        out.push_back(char((unit >> 8) & 0xFF));
    };
    for (std::size_t i = 0; i < utf8.size();) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        std::uint32_t cp = 0xFFFD;
        std::size_t len = 1;
        if (c < 0x80) { cp = c; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1Fu; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0Fu; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07u; len = 4; }
        if (len > 1) {
            if (i + len > utf8.size()) { cp = 0xFFFD; len = 1; }
            else {
                for (std::size_t j = 1; j < len; ++j) {
                    const unsigned char cont = static_cast<unsigned char>(utf8[i + j]);
                    if ((cont & 0xC0) != 0x80) { cp = 0xFFFD; len = 1; break; }
                    cp = (cp << 6) | (cont & 0x3Fu);
                }
            }
        }
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            cp = 0xFFFD;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            push(0xD800 + (cp >> 10));
            push(0xDC00 + (cp & 0x3FF));
        } else {
            push(cp);
        }
        i += len;
    }
    return out;
}

// ---------------------------------------------------------------------- read

Info read(std::string_view data)
{
    Info info;
    const RpcIdScan::Pe pe(data);
    if (!pe.ok() || !pe.hasSections()) {
        info.error = "this file is not a readable PE image";
        return info;
    }
    const auto directory = pe.dataDirectory(2);
    if (directory.first == 0 || directory.second == 0) {
        info.error = "this file has no resource directory";
        return info;
    }
    const std::int64_t base = pe.rvaToOffset(std::int64_t(directory.first));
    if (base < 0 || !has(data, base, 16)) {
        info.error = "the resource directory is not mapped in this file";
        return info;
    }
    Found found;
    walkDirectory(data, pe, base, base, 0, 0, &found);
    if (!found.ok) {
        info.error = "this file has no readable version resource";
        return info;
    }
    info.base = found.off;
    info.size = found.size;
    walkStrings(data, found.off, found.off,
                std::min<std::int64_t>(std::int64_t(data.size()), found.off + found.size),
                false, 0, &info.strings);
    if (info.strings.empty()) {
        info.error = "the version resource has no editable strings";
        return info;
    }
    info.ok = true;
    return info;
}

std::vector<const StringEntry *> withKey(const Info &info, const std::string &key)
{
    std::vector<const StringEntry *> out;
    for (const StringEntry &entry : info.strings)
        if (entry.key == key)
            out.push_back(&entry);
    return out;
}

bool setString(char *buf, std::size_t size, const StringEntry &entry,
               const std::string &utf8, std::string *why)
{
    const std::string wide = utf8ToUtf16le(utf8);
    const std::size_t units = wide.size() / 2;
    if (entry.cap <= 0 || entry.off < 0
        || std::uint64_t(entry.off) + std::uint64_t(entry.cap) * 2 > std::uint64_t(size)) {
        if (why) *why = "the stored value is outside this file";
        return false;
    }
    if (units + 1 > std::size_t(entry.cap)) {
        if (why) {
            *why = "\"" + utf8 + "\" needs " + std::to_string(units)
                 + " characters; the slot holds " + std::to_string(entry.cap - 1);
        }
        return false;
    }
    // Fixed slot: the new text plus NUL padding, so the node length, every
    // parent length and the file size all stay exactly as they were.
    char *out = buf + entry.off;
    std::copy(wide.begin(), wide.end(), out);
    std::fill(out + wide.size(), out + std::size_t(entry.cap) * 2, '\0');
    if (why) *why = "ok";
    return true;
}

} // namespace VersionResource
