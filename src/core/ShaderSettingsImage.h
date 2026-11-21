#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

// Read-only, bounded preparation for the native shader category. No PE import
// is added: QQmlEngine loads a regular matching-Qt plugin outside DllMain.
// The EXE's size never changes. A caller must separately transact and verify
// the executable + owned plugin package before claiming an installation.
namespace ShaderSettingsImage {
struct Change {
    QString path;
    QString leaf;
    QByteArray before;
    QByteArray after;
};
struct Plan {
    bool ok = false;
    bool changed = false;
    bool installed = false;
    int qtMajor = 0;
    int tabIndex = -1;
    QByteArray output;
    QList<Change> sources;
    QString error;
};
Plan prepare(const QByteArray &image, bool enabled);
// Also used by tests/other planners to stage a known resource. The input is
// untouched on failure; registered aliases and source-specific caches are
// verified by the shared resource writer. Never appends storage/PE sections.
bool replaceSource(QByteArray &image, const QString &leaf, const QByteArray &source,
                    QString *error);
} // namespace ShaderSettingsImage
