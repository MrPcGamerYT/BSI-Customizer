#pragma once
#include <QByteArray>
#include <QVariantMap>

// Read-only PE analysis. Never loads a DLL or executes inspected content.
namespace BinaryInspector {
constexpr qint64 FileLimit = 256LL * 1024 * 1024;
QVariantMap inspect(const QByteArray &data);
QVariantMap strings(const QByteArray &data, const QString &query);
QVariantMap hex(const QByteArray &data, qint64 offset);
}
