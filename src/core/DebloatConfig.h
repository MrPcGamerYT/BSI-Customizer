#pragma once

#include <QByteArray>
#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>

/**
 * DebloatConfig — byte-preserving reader/writer for the player's
 * `bluestacks.conf`.
 *
 * The file is a flat `key=value` list that BlueStacks rewrites from its own
 * in-memory copy when it exits, so it is never regenerated here: only the
 * bytes of a value that actually changes are replaced. Key order, quoting,
 * spacing, comments, blank lines and line endings stay exactly as the player
 * wrote them, which is what makes Restore exact.
 *
 * Keys this file does not declare are never created (BlueStacks would only
 * delete an invented key again on the next exit, and a missing setting is the
 * player saying "this build does not have that feature").
 */
namespace DebloatConfig {

class File
{
public:
    // Where a key's value lives in `bytes()`: the span points at the value
    // itself, quotes excluded, so a quoted value keeps its quotes.
    struct Span
    {
        int begin = 0;
        int length = 0;
        bool quoted = false;
    };

    // Never throws and never writes: a malformed file comes back invalid with
    // a reason, so a caller can refuse instead of guessing.
    static File parse(const QByteArray &bytes);

    bool isValid() const { return m_valid; }
    QString error() const { return m_error; }
    const QByteArray &bytes() const { return m_bytes; }

    bool contains(const QString &key) const { return m_spans.contains(key); }
    QString value(const QString &key) const;
    bool isQuoted(const QString &key) const;
    // Declaration order, duplicates refused at parse time.
    QStringList keys() const { return m_order; }

    // Instance names of `bst.instance.<name>.<setting>` keys, as declared.
    QStringList instances() const;

    // Replace the value of existing keys only. `out` receives a copy of the
    // original bytes with those spans replaced; every other byte is untouched.
    bool applyEdits(const QMap<QString, QString> &changes, QByteArray *out,
                    QString *error = nullptr) const;

private:
    QByteArray m_bytes;
    QHash<QString, Span> m_spans;
    QStringList m_order;
    bool m_valid = true;
    QString m_error;
};

} // namespace DebloatConfig
