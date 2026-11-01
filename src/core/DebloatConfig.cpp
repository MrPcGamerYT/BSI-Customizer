#include "DebloatConfig.h"

#include <algorithm>

namespace DebloatConfig {

namespace {

const char kBom[] = "\xEF\xBB\xBF";

bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\v' || c == '\f';
}

} // namespace

File File::parse(const QByteArray &bytes)
{
    File file;
    file.m_bytes = bytes;
    if (bytes.contains('\0')) {
        file.m_valid = false;
        file.m_error = QStringLiteral("The configuration contains NUL bytes; nothing was read.");
        return file;
    }

    int offset = 0;
    const int size = bytes.size();
    while (offset < size) {
        int lineEnd = bytes.indexOf('\n', offset);
        if (lineEnd < 0) lineEnd = size;
        int bodyEnd = lineEnd;
        if (bodyEnd > offset && bytes.at(bodyEnd - 1) == '\r') --bodyEnd;

        int lineStart = offset;
        if (lineStart == 0 && bytes.startsWith(kBom)) lineStart += int(sizeof(kBom)) - 1;

        const int equals = bytes.indexOf('=', lineStart);
        if (equals >= 0 && equals < bodyEnd) {
            const QByteArray left = bytes.mid(lineStart, equals - lineStart);
            const QString key = QString::fromUtf8(left).trimmed();
            if (!key.isEmpty() && !key.startsWith(QLatin1Char('#'))) {
                int valueBegin = equals + 1;
                while (valueBegin < bodyEnd && isSpace(bytes.at(valueBegin))) ++valueBegin;
                int valueEnd = bodyEnd;
                while (valueEnd > valueBegin && isSpace(bytes.at(valueEnd - 1))) --valueEnd;

                Span span;
                if (valueEnd - valueBegin >= 2 && bytes.at(valueBegin) == '"') {
                    if (bytes.at(valueEnd - 1) != '"') {
                        file.m_valid = false;
                        file.m_error = QStringLiteral("Unterminated quoted value for %1; nothing was read.").arg(key);
                        return file;
                    }
                    span.quoted = true;
                    span.begin = valueBegin + 1;
                    span.length = valueEnd - valueBegin - 2;
                } else {
                    span.begin = valueBegin;
                    span.length = valueEnd - valueBegin;
                }
                const QByteArray value = bytes.mid(span.begin, span.length);
                if (value.contains('"')) {
                    file.m_valid = false;
                    file.m_error = QStringLiteral("Malformed value for %1; nothing was read.").arg(key);
                    return file;
                }
                if (file.m_spans.contains(key)) {
                    file.m_valid = false;
                    file.m_error = QStringLiteral("Duplicate setting %1; nothing was read.").arg(key);
                    return file;
                }
                file.m_spans.insert(key, span);
                file.m_order << key;
            }
        }
        offset = lineEnd + 1;
    }
    return file;
}

QString File::value(const QString &key) const
{
    const auto it = m_spans.constFind(key);
    if (it == m_spans.constEnd()) return QString();
    return QString::fromUtf8(m_bytes.mid(it->begin, it->length));
}

bool File::isQuoted(const QString &key) const
{
    const auto it = m_spans.constFind(key);
    return it != m_spans.constEnd() && it->quoted;
}

QStringList File::instances() const
{
    QStringList names;
    const QString prefix = QStringLiteral("bst.instance.");
    for (const QString &key : m_order) {
        if (!key.startsWith(prefix)) continue;
        const QString rest = key.mid(prefix.size());
        const int dot = rest.indexOf(QLatin1Char('.'));
        if (dot <= 0) continue;
        const QString name = rest.left(dot);
        if (!names.contains(name)) names << name;
    }
    return names;
}

bool File::applyEdits(const QMap<QString, QString> &changes, QByteArray *out, QString *error) const
{
    const auto fail = [&](const QString &why) {
        if (error) *error = why;
        return false;
    };
    if (!m_valid) return fail(m_error);

    struct Replacement
    {
        int begin = 0;
        int length = 0;
        QByteArray value;
    };
    QList<Replacement> replacements;
    for (auto it = changes.constBegin(); it != changes.constEnd(); ++it) {
        const auto span = m_spans.constFind(it.key());
        if (span == m_spans.constEnd())
            return fail(QStringLiteral("This player does not declare %1; no setting was written.")
                            .arg(it.key()));
        if (it.value().contains(QLatin1Char('"')) || it.value().contains(QLatin1Char('\n'))
            || it.value().contains(QLatin1Char('\r')) || it.value().contains(QChar(0)))
            return fail(QStringLiteral("Unsupported value for %1.").arg(it.key()));
        if (value(it.key()) == it.value()) continue;
        replacements << Replacement { span->begin, span->length, it.value().toUtf8() };
    }
    QByteArray result = m_bytes;
    std::sort(replacements.begin(), replacements.end(),
              [](const Replacement &a, const Replacement &b) { return a.begin > b.begin; });
    for (const auto &replacement : replacements)
        result.replace(replacement.begin, replacement.length, replacement.value);
    if (out) *out = result;
    return true;
}

} // namespace DebloatConfig
