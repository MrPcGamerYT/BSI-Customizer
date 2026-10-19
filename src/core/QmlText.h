#pragma once

#include <QList>
#include <QString>

// Minimal, literal-safe QML text surgery shared by the resource patchers.
// BlueStacks writes its QML with CRLF endings and one member per line, but
// these scanners only rely on brace structure: string literals and comments
// are blanked in a mask copy first, so a brace, colon or keyword inside a
// literal can never open a block or end a member.
namespace QmlText {

// Strings ('/"), // and /* */ comments replaced 1:1 with spaces. The mask has
// the exact length of the input, so offsets map back onto the source text.
QString mask(const QString &qml);

struct Object
{
    int nameBegin = -1; // first character of the type name (may be qualified)
    int open = -1;      // '{'
    int end = -1;       // one past the matching '}'
};

// Every `type {` literal, in source order. A longer identifier that merely
// ends in the type name is rejected; a qualified `Foo.type {` is accepted.
QList<Object> objects(const QString &mask, const QString &type);

struct Binding
{
    int nameBegin = -1;
    int nameEnd = -1;   // exclusive end of the member name (dots included)
    int valueBegin = -1;
    int valueEnd = -1;  // exclusive end of the value expression
};

// First member named `name` inside [begin, end) of the masked text. The value
// runs to the end of its statement: a line end at balanced (), [], {} depth,
// or a ';' - long calls wrap across lines while their parentheses stay open.
// The line ending itself is never part of the value, so replacing values keeps
// the file's CRLF layout byte-stable. Property reads (`foo.name`) and longer
// identifiers are not mistaken for member declarations.
bool binding(const QString &mask, int begin, int end, const QString &name, Binding *out);

} // namespace QmlText
