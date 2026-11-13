#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

/**
 * Windows VS_VERSION_INFO (RT_VERSION) string reader/writer.
 *
 * The emulator's product/description strings live in the PE version resource,
 * which is what Explorer, Task Manager and most overlays display. Editing one
 * of those strings must never move anything: a version resource is a tree of
 * length-prefixed, 4-byte aligned nodes, so a longer value would invalidate
 * every parent length. This engine therefore only overwrites a value inside
 * the fixed slot the compiler already reserved for it (value capacity, in
 * UTF-16 code units, including the terminator) and keeps the file size
 * identical — the same rule the rest of this project's patchers follow.
 *
 * Like RpcIdScan it is deliberately Qt-free (byte views in, plain structs
 * out) so the delicate parsing can be compiled and tested anywhere.
 */
namespace VersionResource {

struct StringEntry
{
    std::string key;         // "FileDescription", "ProductName", ...
    std::string value;       // current text, UTF-8
    std::int64_t off = 0;    // file offset of the UTF-16LE value
    int cap = 0;             // slot size in UTF-16 units, terminator included
};

struct Info
{
    bool ok = false;
    std::int64_t base = 0;             // file offset of the version resource
    std::int64_t size = 0;             // its size in bytes
    std::vector<StringEntry> strings;  // StringFileInfo entries, in file order
    std::string error;                 // why ok is false
};

/** Locate RT_VERSION in `data` and read its StringFileInfo entries. */
Info read(std::string_view data);

/** Every entry whose key matches (a file can carry several translations). */
std::vector<const StringEntry *> withKey(const Info &info, const std::string &key);

/**
 * Overwrite one entry's text in place. Fails (without touching `buf`) when
 * the text does not fit the reserved slot, which keeps the resource tree and
 * the file size valid.
 */
bool setString(char *buf, std::size_t size, const StringEntry &entry,
               const std::string &utf8, std::string *why = nullptr);

// Narrow helpers shared with the tests; invalid units are replaced, never
// rejected, because version resources are read for display only.
std::string utf16leToUtf8(std::string_view utf16le);
std::string utf8ToUtf16le(std::string_view utf8);

} // namespace VersionResource
