#include "PlayerAppearanceScan.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <map>
#include <set>

namespace PlayerAppearanceScan {
namespace {

bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }
bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool ident(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// A 1:1 structural mask. Comments become spaces; string/template contents and
// delimiters become '@' so they cannot invent braces, members or numbers while
// still terminating an expression. Quoted Qt.font keys are recovered from the
// original text only after the surrounding call has been structurally located.
std::string maskCode(const std::string &s)
{
    std::string out = s;
    for (std::size_t i = 0; i < s.size();) {
        const char c = s[i], n = i + 1 < s.size() ? s[i + 1] : '\0';
        if (c == '/' && n == '/') {
            while (i < s.size() && s[i] != '\n') out[i++] = ' ';
        } else if (c == '/' && n == '*') {
            out[i++] = ' '; out[i++] = ' ';
            while (i < s.size()) {
                if (s[i] == '*' && i + 1 < s.size() && s[i + 1] == '/') { out[i++] = ' '; out[i++] = ' '; break; }
                if (s[i] != '\n') out[i] = ' ';
                ++i;
            }
        } else if (c == '"' || c == '\'' || c == '`') {
            const char quote = c;
            out[i++] = '@';
            bool escaped = false;
            while (i < s.size()) {
                const char d = s[i];
                out[i++] = '@';
                if (escaped) { escaped = false; continue; }
                if (d == '\\') { escaped = true; continue; }
                if (d == quote) break;
                if (d == '\n' && quote != '`') break; // unterminated literal: stay conservative
            }
        } else ++i;
    }
    return out;
}

std::size_t skipSpace(const std::string &m, std::size_t i, std::size_t end)
{
    while (i < end && space(m[i])) ++i;
    return i;
}

std::string readIdent(const std::string &m, std::size_t &i, std::size_t end)
{
    i = skipSpace(m, i, end);
    if (i >= end || !identStart(m[i])) return {};
    const auto start = i++;
    while (i < end && (ident(m[i]) || m[i] == '.')) ++i;
    return m.substr(start, i - start);
}

std::size_t expressionEnd(const std::string &m, std::size_t start, std::size_t end)
{
    int depth = 0;
    char previous = '\0';
    int ternaries = 0;
    for (std::size_t i = start; i < end; ++i) {
        const char c = m[i];
        if (c == '(' || c == '[' || c == '{') ++depth;
        else if (c == ')' || c == ']' || c == '}') { if (!depth) return i; --depth; }
        else if (!depth && c == '?') ++ternaries;
        else if (!depth && c == ':' && ternaries) --ternaries;
        else if (!depth && c == ';') return i;
        else if (!depth && (c == '\n' || c == '\r')) {
            std::size_t next = i + 1;
            while (next < end && space(m[next])) ++next;
            const std::string continuation = "?:,+-*/&|=.";
            const std::string nextContinuation = "?:.&|";
            if (!ternaries && continuation.find(previous) == std::string::npos
                && (next == end || nextContinuation.find(m[next]) == std::string::npos)) return i;
        }
        if (!space(c)) previous = c;
    }
    return end;
}

struct Binding
{
    std::string name, type;
    std::size_t begin = 0, value = 0, end = 0;
    int owner = -1;
};
struct Object
{
    std::string type, path, id;
    std::size_t begin = 0, body = 0, end = 0;
    int parent = -1;
};
struct Document
{
    std::string mask;
    std::vector<Object> objects;
    std::vector<Binding> bindings;
};

bool parse(const std::string &qml, Document &doc)
{
    doc.mask = maskCode(qml);
    const auto &m = doc.mask;
    // Require a real QtQuick import, but accept version-less Qt 6 imports and
    // CRLF line endings. This is a catalog for Qt Quick design tokens, not a
    // generic JavaScript numeric editor.
    bool quick = false;
    for (std::size_t i = 0; i < m.size();) {
        i = m.find("import", i);
        if (i == std::string::npos) break;
        if ((i == 0 || space(m[i - 1])) && m.compare(i, 6, "import") == 0) {
            std::size_t p = i + 6;
            const auto name = readIdent(m, p, m.size());
            if (name == "QtQuick") { quick = true; break; }
        }
        i += 6;
    }
    if (!quick) return false;
    std::map<std::size_t, std::size_t> ends;
    std::vector<int> depths(m.size() + 1, 0);
    {
        std::vector<std::size_t> stack;
        for (std::size_t i = 0; i < m.size(); ++i) {
            depths[i] = int(stack.size());
            if (m[i] == '{') stack.push_back(i);
            else if (m[i] == '}') { if (stack.empty()) return false; ends[stack.back()] = i + 1; stack.pop_back(); }
        }
        if (!stack.empty()) return false;
    }
    std::vector<int> parents;
    for (std::size_t i = 0; i < m.size(); ++i) {
        if (m[i] != '{') continue;
        // The type immediately before the brace must be a capitalized QML type,
        // optionally qualified. Object literals inside calls are not objects.
        std::size_t p = i;
        while (p > 0 && space(m[p - 1])) --p;
        const auto typeEnd = p;
        while (p > 0 && (ident(m[p - 1]) || m[p - 1] == '.')) --p;
        if (p == typeEnd) continue;
        const auto type = m.substr(p, typeEnd - p);
        const auto dot = type.rfind('.');
        const auto leaf = dot == std::string::npos ? type : type.substr(dot + 1);
        if (leaf.empty() || !std::isupper(static_cast<unsigned char>(leaf[0]))) continue;
        const auto end = ends[i];
        while (!parents.empty() && doc.objects[parents.back()].end <= p) parents.pop_back();
        Object object;
        object.type = leaf; object.begin = p; object.body = i + 1; object.end = end;
        object.parent = parents.empty() ? -1 : parents.back();
        parents.push_back(int(doc.objects.size()));
        doc.objects.push_back(object);
    }
    if (doc.objects.empty()) return false;
    std::map<int, std::size_t> previousEnd;
    for (std::size_t i = 0; i < m.size(); ++i) {
        if (!identStart(m[i]) || (i > 0 && (ident(m[i - 1]) || m[i - 1] == '.'))) continue;
        std::size_t p = i;
        std::string first = readIdent(m, p, m.size());
        std::string type, name;
        if (first == "property" || first == "readonly") {
            if (first == "readonly") {
                const auto next = readIdent(m, p, m.size());
                if (next != "property") { i = p > i ? p - 1 : i; continue; }
            }
            type = readIdent(m, p, m.size());
            name = readIdent(m, p, m.size());
            if (type.empty() || name.empty()) { i = p > i ? p - 1 : i; continue; }
        } else {
            name = first;
        }
        p = skipSpace(m, p, m.size());
        if (p >= m.size() || m[p] != ':' || (p + 1 < m.size() && m[p + 1] == '=')) { i = p > i ? p - 1 : i; continue; }
        int owner = -1;
        for (int o = 0; o < int(doc.objects.size()); ++o) {
            const auto &obj = doc.objects[o];
            if (obj.body <= i && obj.end > p && depths[i] == depths[obj.body]) owner = o;
        }
        if (owner < 0) { i = p > i ? p - 1 : i; continue; }
        const auto previous = previousEnd.find(owner);
        if (previous != previousEnd.end() && i < previous->second) { i = p > i ? p - 1 : i; continue; }
        const auto start = skipSpace(m, p + 1, doc.objects[owner].end);
        auto end = expressionEnd(m, start, doc.objects[owner].end - 1);
        for (const auto &object : doc.objects) {
            if (object.parent == owner && object.begin >= start) {
                bool onlySpace = true;
                for (std::size_t k = start; k < object.begin; ++k) if (!space(m[k])) { onlySpace = false; break; }
                if (onlySpace) { end = object.end; break; }
            }
        }
        while (end > start && space(m[end - 1])) --end;
        if (end <= start) { i = p > i ? p - 1 : i; continue; }
        previousEnd[owner] = end;
        doc.bindings.push_back({name, type, i, start, end, owner});
        // Continue after the colon, not after the value: object-valued bindings
        // contain the nested members the catalog needs. previousEnd prevents a
        // second binding from being invented inside this owner's value.
        i = p;
    }
    for (int i = 0; i < int(doc.objects.size()); ++i) {
        auto &object = doc.objects[i];
        for (const auto &b : doc.bindings) {
            if (b.owner == i && b.name == "id") {
                const auto value = m.substr(b.value, b.end - b.value);
                if (!value.empty() && std::all_of(value.begin(), value.end(), [](char c) { return ident(c); })) object.id = value;
            }
        }
        if (object.parent < 0) { object.path = "root"; continue; }
        std::string name;
        for (const auto &b : doc.bindings) {
            if (b.owner == object.parent && b.value <= object.begin && b.end >= object.end) {
                bool onlySpace = true;
                for (std::size_t k = b.value; k < object.begin; ++k) if (!space(m[k])) { onlySpace = false; break; }
                if (onlySpace) { name = b.name; break; }
            }
        }
        if (name.empty()) name = object.id;
        if (name.empty()) {
            int ordinal = 0;
            for (int j = 0; j < i; ++j) if (doc.objects[j].parent == object.parent && doc.objects[j].type == object.type) ++ordinal;
            name = object.type + "[" + std::to_string(ordinal) + "]";
        }
        object.path = doc.objects[object.parent].path + "." + name;
    }
    return true;
}

bool integerLiteral(const std::string &m, std::size_t begin, std::size_t end, int *value)
{
    if (begin >= end || end > m.size()) return false;
    const auto text = m.substr(begin, end - begin);
    std::size_t i = 0;
    if (text[i] == '-' || text[i] == '+') ++i;
    if (i == text.size()) return false;
    for (; i < text.size(); ++i) if (!std::isdigit(static_cast<unsigned char>(text[i]))) return false;
    errno = 0;
    char *stop = nullptr;
    const long parsed = std::strtol(text.c_str(), &stop, 10);
    if (errno || !stop || *stop || parsed < -1000000 || parsed > 1000000) return false;
    *value = int(parsed);
    return true;
}

struct Rule { int minimum, maximum; const char *group; const char *label; };
const std::map<std::string, Rule> &themeRules()
{
    static const std::map<std::string, Rule> rules {
        {"height", {16, 96, "Buttons", "Height"}},
        {"radius", {0, 32, "Buttons", "Corner radius"}},
        {"horizontalPadding", {0, 64, "Buttons", "Horizontal padding"}},
        {"spacing", {0, 32, "Buttons", "Spacing"}},
        {"iconWidth", {8, 64, "Buttons", "Icon width"}},
        {"iconHeight", {8, 64, "Buttons", "Icon height"}},
    };
    return rules;
}
const std::map<std::string, Rule> &controlRules()
{
    static const std::map<std::string, Rule> rules {
        {"height", {16, 96, "Controls", "Height"}},
        {"horizontalPadding", {0, 64, "Controls", "Horizontal padding"}},
        {"spacing", {0, 32, "Controls", "Spacing"}},
        {"iconWidth", {8, 64, "Controls", "Icon width"}},
        {"iconHeight", {8, 64, "Controls", "Icon height"}},
        {"indicatorWidth", {8, 64, "Controls", "Indicator width"}},
        {"indicatorImageWidth", {8, 64, "Controls", "Indicator image width"}},
        {"indicatorImageHeight", {8, 64, "Controls", "Indicator image height"}},
        {"maxDropDownHeight", {80, 600, "Controls", "Maximum dropdown height"}},
        {"handleWidth", {8, 48, "Controls", "Slider handle width"}},
        {"handleHeight", {8, 48, "Controls", "Slider handle height"}},
        {"sliderHeight", {1, 16, "Controls", "Slider track height"}},
    };
    return rules;
}

std::string leafName(const std::string &resource)
{
    const auto slash = resource.find_last_of("/\\");
    return slash == std::string::npos ? resource : resource.substr(slash + 1);
}

void addToken(Result &out, const std::string &path, const std::string &property,
              const Rule &rule, int value, std::size_t begin, std::size_t end,
              const std::string &suffix = {})
{
    Token token;
    token.path = path; token.property = property; token.label = rule.label;
    token.group = rule.group; token.value = value; token.minimum = rule.minimum;
    token.maximum = rule.maximum; token.step = 1; token.begin = begin; token.end = end;
    if (!suffix.empty()) token.label += " · " + suffix;
    out.tokens.push_back(token);
}

bool fontPixelSize(const std::string &qml, const Document &doc, const Binding &binding,
                   std::size_t *begin, std::size_t *end, int *value)
{
    // Only `Qt.font({ ... })` (or an import alias ending in .font) is accepted.
    const auto &m = doc.mask;
    auto p = skipSpace(m, binding.value, binding.end);
    std::size_t call = p;
    while (call < binding.end && (ident(m[call]) || m[call] == '.')) ++call;
    const auto callee = m.substr(p, call - p);
    if (callee != "Qt.font" && !(callee.size() > 5 && callee.compare(callee.size() - 5, 5, ".font") == 0)) return false;
    p = skipSpace(m, call, binding.end);
    if (p >= binding.end || m[p] != '(') return false;
    p = skipSpace(m, p + 1, binding.end);
    if (p >= binding.end || m[p] != '{') return false;
    const auto open = p++;
    int depth = 1;
    std::size_t close = open;
    for (; p < binding.end; ++p) {
        if (m[p] == '{') ++depth;
        else if (m[p] == '}') { if (--depth == 0) { close = p; break; } }
    }
    if (depth != 0) return false;
    // Recover the quoted key from the original source. The mask proves the
    // surrounding braces/call are code; the key itself is a fixed string, never
    // user-supplied text. Only a bare integer immediately after the colon counts.
    const std::string needle = "\"pixelSize\"";
    std::size_t at = open + 1;
    std::size_t found = std::string::npos;
    while ((at = qml.find(needle, at)) != std::string::npos && at < close) {
        // The fixed key must be a real masked string literal, not text inside a
        // comment or an escaped fragment that merely spells the same words.
        if (m.compare(at, needle.size(), std::string(needle.size(), '@')) != 0) { at += needle.size(); continue; }
        if (found != std::string::npos) return false;
        found = at; at += needle.size();
    }
    if (found == std::string::npos) return false;
    p = skipSpace(m, found + needle.size(), close);
    if (p >= close || m[p] != ':') return false;
    const auto start = skipSpace(m, p + 1, close);
    auto stop = start;
    while (stop < close && (std::isdigit(static_cast<unsigned char>(m[stop])) || m[stop] == '-' || m[stop] == '+')) ++stop;
    if (!integerLiteral(m, start, stop, value)) return false;
    auto after = skipSpace(m, stop, close);
    if (after < close && m[after] != ',' && m[after] != '}') return false;
    *begin = start; *end = stop;
    return true;
}

} // namespace

Result scan(const std::string &qml, const std::string &resource)
{
    Result out;
    if (qml.empty() || qml.size() > 8 * 1024 * 1024) { out.error = "QML source is empty or too large."; return out; }
    Document doc;
    if (!parse(qml, doc)) { out.error = "The resource is not a structurally readable QtQuick document."; return out; }
    const auto leaf = leafName(resource);
    std::set<std::string> seen;
    const auto add = [&](const std::string &path, const std::string &property, const Rule &rule,
                         int value, std::size_t begin, std::size_t end, const std::string &suffix = {}) {
        if (!seen.insert(path).second) {
            if (std::find(out.ambiguous.begin(), out.ambiguous.end(), path) == out.ambiguous.end())
                out.ambiguous.push_back(path);
            out.tokens.erase(std::remove_if(out.tokens.begin(), out.tokens.end(), [&](const Token &t) { return t.path == path; }), out.tokens.end());
            return;
        }
        if (std::find(out.ambiguous.begin(), out.ambiguous.end(), path) != out.ambiguous.end()) return;
        addToken(out, path, property, rule, value, begin, end, suffix);
    };
    if (leaf == "UiThemeObject.qml") {
        for (const auto &binding : doc.bindings) {
            if (binding.owner < 0) continue;
            const auto &owner = doc.objects[binding.owner];
            int value = 0;
            if (owner.path == "root.fonts" && binding.type == "font") {
                std::size_t begin = 0, end = 0;
                if (fontPixelSize(qml, doc, binding, &begin, &end, &value))
                    add(owner.path + "." + binding.name + ".pixelSize", "pixelSize", {6, 72, "Typography", "Font size"}, value, begin, end, binding.name);
                continue;
            }
            if (owner.path.rfind("root.buttons.sizes.", 0) == 0 && owner.path.find('.', 19) == std::string::npos) {
                const auto rule = themeRules().find(binding.name);
                if (rule != themeRules().end() && integerLiteral(doc.mask, binding.value, binding.end, &value))
                    add(owner.path + "." + binding.name, binding.name, rule->second, value, binding.value, binding.end, owner.path.substr(19));
            } else if ((owner.path.rfind("root.sliders.sizes.", 0) == 0 || owner.path.rfind("root.toggleButtons.sizes.", 0) == 0
                        || owner.path.rfind("root.checkBoxes.sizes.", 0) == 0 || owner.path.rfind("root.radioButtons.sizes.", 0) == 0
                        || owner.path.rfind("root.comboBoxes.sizes.", 0) == 0 || owner.path.rfind("root.textFields.sizes.", 0) == 0)
                       && owner.path.find('.', owner.path.rfind("sizes.") + 6) == std::string::npos) {
                const auto rule = controlRules().find(binding.name);
                if (rule != controlRules().end() && integerLiteral(doc.mask, binding.value, binding.end, &value))
                    add(owner.path + "." + binding.name, binding.name, rule->second, value, binding.value, binding.end, owner.path.substr(owner.path.rfind('.') + 1));
            }
        }
    } else if (leaf == "plr.qml") {
        for (const auto &binding : doc.bindings) {
            if (binding.owner < 0 || doc.objects[binding.owner].path != "root" || doc.objects[binding.owner].type != "ApplicationWindow") continue;
            int value = 0;
            if (!integerLiteral(doc.mask, binding.value, binding.end, &value)) continue;
            if (binding.name == "pSidebarWidth") add("root.pSidebarWidth", binding.name, {24, 96, "Player chrome", "Sidebar width"}, value, binding.value, binding.end);
            else if (binding.name == "pBorderWidth") add("root.pBorderWidth", binding.name, {0, 8, "Player chrome", "Window border width"}, value, binding.value, binding.end);
        }
    } else {
        out.error = "This resource is outside the Player Appearance Studio catalog.";
        return out;
    }
    std::sort(out.tokens.begin(), out.tokens.end(), [](const Token &a, const Token &b) { return a.path < b.path; });
    out.ok = true;
    if (out.tokens.empty()) out.error = "No supported literal appearance tokens were found in this resource.";
    return out;
}

bool inRange(const Token &token, int value) { return value >= token.minimum && value <= token.maximum; }

bool apply(std::string &qml, const std::string &resource, const std::vector<Edit> &edits, std::string *error)
{
    const auto fail = [&](const std::string &message) { if (error) *error = message; return false; };
    if (error) error->clear();
    if (edits.empty()) return fail("No appearance edits were supplied.");
    if (edits.size() > 512) return fail("Too many appearance edits in one operation.");
    const auto before = scan(qml, resource);
    if (!before.ok || !before.ambiguous.empty()) return fail(before.error.empty() ? "Ambiguous appearance token paths." : before.error);
    std::map<std::string, const Token *> byPath;
    for (const auto &token : before.tokens) byPath[token.path] = &token;
    std::set<std::string> paths;
    std::vector<std::pair<std::size_t, Edit>> located;
    for (const auto &edit : edits) {
        if (!paths.insert(edit.path).second) return fail("Duplicate appearance token: " + edit.path);
        const auto found = byPath.find(edit.path);
        if (found == byPath.end()) return fail("Unknown or unsupported appearance token: " + edit.path);
        if (!inRange(*found->second, edit.value)) return fail("Value out of range for " + edit.path);
        located.push_back({found->second->begin, edit});
    }
    std::sort(located.begin(), located.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    std::string changed = qml;
    for (const auto &item : located) {
        const auto token = byPath[item.second.path];
        changed.replace(token->begin, token->end - token->begin, std::to_string(item.second.value));
    }
    const auto after = scan(changed, resource);
    if (!after.ok || !after.ambiguous.empty() || after.tokens.size() != before.tokens.size()) return fail("The appearance rewrite did not preserve the document structure.");
    for (const auto &edit : edits) {
        const auto found = std::find_if(after.tokens.begin(), after.tokens.end(), [&](const Token &t) { return t.path == edit.path; });
        if (found == after.tokens.end() || found->value != edit.value) return fail("The appearance rewrite did not read back correctly.");
        const auto old = byPath[edit.path];
        if (found->minimum != old->minimum || found->maximum != old->maximum) return fail("The appearance token catalog changed during verification.");
    }
    qml.swap(changed);
    return true;
}

} // namespace PlayerAppearanceScan
