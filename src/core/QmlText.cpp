#include "QmlText.h"

namespace QmlText {

QString mask(const QString &qml)
{
    QString out = qml;
    const int n = out.size();
    int i = 0;
    while (i < n) {
        const QChar c = out.at(i);
        if (c == QLatin1Char('/') && i + 1 < n && out.at(i + 1) == QLatin1Char('/')) {
            while (i < n && out.at(i) != QLatin1Char('\n')) out[i++] = QLatin1Char(' ');
        } else if (c == QLatin1Char('/') && i + 1 < n && out.at(i + 1) == QLatin1Char('*')) {
            out[i++] = QLatin1Char(' ');
            out[i++] = QLatin1Char(' ');
            while (i + 1 < n && !(out.at(i) == QLatin1Char('*') && out.at(i + 1) == QLatin1Char('/'))) out[i++] = QLatin1Char(' ');
            if (i < n) out[i++] = QLatin1Char(' ');
            if (i < n) out[i++] = QLatin1Char(' ');
        } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            const QChar quote = c;
            out[i++] = QLatin1Char(' ');
            while (i < n && out.at(i) != quote) {
                if (out.at(i) == QLatin1Char('\\') && i + 1 < n) out[i++] = QLatin1Char(' ');
                out[i++] = QLatin1Char(' ');
            }
            if (i < n) out[i++] = QLatin1Char(' ');
        } else {
            ++i;
        }
    }
    return out;
}

static int closeBrace(const QString &m, int open)
{
    int depth = 0;
    for (int i = open; i < m.size(); ++i) {
        if (m.at(i) == QLatin1Char('{')) ++depth;
        else if (m.at(i) == QLatin1Char('}') && --depth == 0) return i + 1;
    }
    return -1;
}

QList<Object> objects(const QString &m, const QString &type)
{
    QList<Object> out;
    for (int at = m.indexOf(type); at >= 0; at = m.indexOf(type, at + 1)) {
        if (at > 0 && (m.at(at - 1).isLetterOrNumber() || m.at(at - 1) == QLatin1Char('_')))
            continue; // a longer identifier merely ends in `type`
        int open = at + type.size();
        while (open < m.size() && m.at(open).isSpace()) ++open;
        if (open >= m.size() || m.at(open) != QLatin1Char('{')) continue;
        const int end = closeBrace(m, open);
        if (end < 0) continue;
        out << Object { at, open, end };
    }
    return out;
}

bool binding(const QString &m, int begin, int end, const QString &name, Binding *out)
{
    for (int at = begin; at >= 0 && at < end;) {
        const int found = m.indexOf(name, at);
        if (found < 0 || found >= end) return false;
        const bool boundary = found == begin
            || !(m.at(found - 1).isLetterOrNumber() || m.at(found - 1) == QLatin1Char('_')
                 || m.at(found - 1) == QLatin1Char('.'));
        int colon = found + name.size();
        while (colon < end && m.at(colon).isSpace()) ++colon;
        if (boundary && colon < end && m.at(colon) == QLatin1Char(':')
            && !(colon + 1 < end && m.at(colon + 1) == QLatin1Char('='))) {
            int start = colon + 1;
            while (start < end && m.at(start).isSpace()) ++start;
            int depth = 0;
            int stop = start;
            for (; stop < end; ++stop) {
                const QChar c = m.at(stop);
                if (c == QLatin1Char('(') || c == QLatin1Char('[') || c == QLatin1Char('{')) ++depth;
                else if (c == QLatin1Char(')') || c == QLatin1Char(']') || c == QLatin1Char('}')) {
                    if (depth == 0) break;
                    --depth;
                } else if (depth == 0 && (c == QLatin1Char('\n') || c == QLatin1Char(';'))) break;
            }
            // Never swallow the line ending itself: replacements keep the
            // file's CRLF layout byte-stable around the edited member.
            while (stop > start && (m.at(stop - 1) == QLatin1Char(' ')
                                    || m.at(stop - 1) == QLatin1Char('\t')
                                    || m.at(stop - 1) == QLatin1Char('\r'))) --stop;
            if (out) {
                out->nameBegin = found;
                out->nameEnd = found + name.size();
                out->valueBegin = start;
                out->valueEnd = stop;
            }
            return true;
        }
        at = found + name.size();
    }
    return false;
}

} // namespace QmlText
