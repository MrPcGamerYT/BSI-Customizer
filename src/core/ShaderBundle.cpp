#include "ShaderBundle.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <stdexcept>

namespace ShaderBundle {
namespace {
void need(bool yes, const char *message) { if (!yes) throw std::runtime_error(message); }
bool hash(const QString &value)
{
    static const QRegularExpression rx(QStringLiteral("^[0-9a-f]{64}$"));
    return rx.match(value).hasMatch();
}
QByteArray resource(const QString &name)
{
    QFile f(QStringLiteral(":/bsi-shader-bundle/") + name);
    if (!f.open(QIODevice::ReadOnly) || f.size() < 1 || f.size() > 128LL * 1024 * 1024) return {};
    return f.readAll();
}
}
Catalog decode(const QByteArray &index, const std::function<QByteArray(const QString &)> &read)
{
    Catalog result;
    try {
        need(!index.isEmpty() && index.size() <= 8192, "This build has no embedded shader bundle, or its index is unreadable.");
        QJsonParseError parse;
        const auto document = QJsonDocument::fromJson(index, &parse);
        need(parse.error == QJsonParseError::NoError && document.isObject(), "Invalid embedded shader bundle index.");
        const auto root = document.object();
        need(root.size() == 2 && root.value(QStringLiteral("schema")).toString() == QLatin1String("bsi.shader-bundle.v1")
             && root.value(QStringLiteral("entries")).isArray(), "Unknown embedded shader bundle schema.");
        const auto rows = root.value(QStringLiteral("entries")).toArray();
        need(!rows.isEmpty() && rows.size() <= 4, "The embedded shader bundle must contain one to four version-contract modules.");
        QSet<QString> versions;
        qint64 total = 0;
        static const QRegularExpression version(QStringLiteral("^[56]\\.[0-9]{1,3}\\.[0-9]{1,3}$"));
        for (const auto &row : rows) {
            need(row.isObject(), "Invalid shader bundle entry.");
            const auto object = row.toObject();
            Entry entry;
            entry.qtVersion = object.value(QStringLiteral("qtVersion")).toString();
            entry.id = object.value(QStringLiteral("id")).toString();
            entry.packageSha256 = object.value(QStringLiteral("packageSha256")).toString();
            const auto dllHash = object.value(QStringLiteral("dllSha256")).toString();
            const auto qmlHash = object.value(QStringLiteral("qmldirSha256")).toString();
            need(object.size() == 5 && version.match(entry.qtVersion).hasMatch()
                 && entry.id == QLatin1String("qt-") + entry.qtVersion && !versions.contains(entry.qtVersion)
                 && hash(entry.packageSha256) && hash(dllHash) && hash(qmlHash), "Ambiguous/unsafe shader bundle identity or hash.");
            versions.insert(entry.qtVersion);
            entry.payload.dll = read(entry.id + QStringLiteral("/BSIShaders.dll"));
            entry.payload.qmldir = read(entry.id + QStringLiteral("/qmldir"));
            total += entry.payload.dll.size() + entry.payload.qmldir.size();
            need(!entry.payload.dll.isEmpty() && entry.payload.dll.size() <= 128LL * 1024 * 1024
                 && entry.payload.qmldir.size() <= 1024 && total <= 256LL * 1024 * 1024, "Embedded shader package exceeds its bounds.");
            need(ShaderPackage::sha256(entry.payload.dll) == dllHash && ShaderPackage::sha256(entry.payload.qmldir) == qmlHash
                 && ShaderPackage::payloadHash(entry.payload) == entry.packageSha256, "Embedded shader package hash mismatch; installation is disabled.");
            const auto description = ShaderPackage::describe(entry.payload);
            need(description.ok && description.qtVersion == entry.qtVersion, "Embedded shader descriptor/entry points do not match the bundle index.");
            result.entries << entry;
        }
        result.ok = true;
    } catch (const std::exception &e) { result.error = QString::fromUtf8(e.what()); result.entries.clear(); }
    return result;
}
Catalog load()
{
    return decode(resource(QStringLiteral("index.json")), resource);
}
} // namespace ShaderBundle
