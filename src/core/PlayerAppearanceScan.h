#pragma once

#include <map>
#include <string>
#include <vector>

// Qt-free structural scanner for the first Player Appearance Studio catalog.
// It only recognizes literal integer design tokens in known BlueStacks QML
// shapes. Expressions, strings and comments are never edited. The Qt facade
// adds validated resource ownership, storage checks and recovery; this layer
// stays compilable and testable with nothing but a C++17 compiler.
namespace PlayerAppearanceScan {

struct Token
{
    std::string path;      // stable logical path, e.g. root.buttons.sizes.large.radius
    std::string label;     // human-readable control label
    std::string group;     // Typography / Buttons / Controls / Player chrome
    std::string property;  // leaf property name
    int value = 0;
    int minimum = 0;
    int maximum = 0;
    int step = 1;
    std::size_t begin = 0; // UTF-8 byte offset of the literal in the source
    std::size_t end = 0;
};

struct Result
{
    bool ok = false;
    std::string error;
    std::vector<Token> tokens;
    std::vector<std::string> ambiguous; // logical paths seen more than once
};

// Scan one UTF-8 QML document. `resource` is a logical resource path, not a
// file offset; it is supplied by the caller so diagnostics can name the page.
Result scan(const std::string &qml, const std::string &resource);

bool inRange(const Token &token, int value);

struct Edit
{
    std::string path;
    int value = 0;
};

// Apply edits to one document. Every path must resolve to exactly one scanned
// token and every value must be in that token's range. On failure the input
// string is unchanged. The rewrite is verified by scanning the result again.
bool apply(std::string &qml, const std::string &resource,
           const std::vector<Edit> &edits, std::string *error);

} // namespace PlayerAppearanceScan
