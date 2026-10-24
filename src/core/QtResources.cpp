#include "QtResources.h"

#include "ByteUtils.h"
#include "PeUtils.h"
#include "QtResourceRegistration.h"

#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <functional>

using namespace ByteUtils;

namespace QtResources {
namespace {

constexpr quint32 kMaxNodes = 65536;

struct Node
{
    qint64 off;
    quint32 name;
    quint16 flags;
    quint32 value;
};

// Parse a connected tree from its root, rather than guessing a node from any
// occurrence of the compressed flag. Child indices give the exact tree size.
QList<Node> readTree(const QByteArray &data, qint64 root, int stride, bool explicitBases, qint64 *span)
{
    QList<Node> nodes;
    QSet<quint32> seen;
    QList<quint32> pending { 0 };
    quint32 last = 0;
    while (!pending.isEmpty()) {
        const quint32 index = pending.takeLast();
        if (index >= kMaxNodes || seen.contains(index))
            return {};
        seen.insert(index);
        last = qMax(last, index);
        const qint64 off = root + qint64(index) * stride;
        if (!inRange(data, off, stride))
            return {};
        const quint16 flags = be16(data, off + 4);
        if (flags != 0 && flags != 1 && flags != 2 && flags != 4)
            return {};
        nodes << Node { off, be32(data, off), flags, be32(data, off + 10) };
        if (flags == 2) {
            const quint32 count = be32(data, off + 6);
            const quint32 first = be32(data, off + 10);
            if (count > kMaxNodes || (count && (first >= kMaxNodes || quint64(first) + count > kMaxNodes
                || (!explicitBases && first <= index))))
                return {};
            if (count && !inRange(data, root, qint64(first + count) * stride))
                return {};
            for (quint32 i = 0; i < count; ++i)
                pending << first + i;
        }
    }
    if (!explicitBases && seen.size() != last + 1)
        return {}; // Canonical fallback; explicit registrations can contain unused nodes.
    *span = qint64(last + 1) * stride;
    return nodes;
}

QString readName(const QByteArray &data, qint64 off)
{
    if (!inRange(data, off, 6))
        return {};
    const quint16 len = be16(data, off);
    if (len == 0 || len > 1024 || !inRange(data, off + 6, qint64(len) * 2))
        return {};
    QString name;
    quint32 hash = 0;
    for (int i = 0; i < len; ++i) {
        const QChar c(be16(data, off + 6 + i * 2));
        if (c.isNull() || c == QLatin1Char('/') || c == QLatin1Char('\\') || c.unicode() < 32)
            return {};
        name += c;
        hash = (hash << 4) + c.unicode();
        hash ^= (hash & 0xf0000000) >> 23;
        hash &= 0x0fffffff;
    }
    return hash == be32(data, off + 2) ? name : QString();
}

QByteArray nameBytes(const QString &name)
{
    QByteArray bytes(name.size() * 2, '\0');
    for (int i = 0; i < name.size(); ++i)
        putBe16(bytes, i * 2, name.at(i).unicode());
    return bytes;
}

bool overlaps(qint64 start, qint64 end, const QPair<qint64, qint64> &range)
{
    return start < range.second && end > range.first;
}

QString unitString(const QByteArray &data, qsizetype pos, quint32 size, quint32 table,
                   quint32 index, int header)
{
    if (quint64(table) + (quint64(index) + 1) * 4 > size)
        return {};
    const quint32 str = le32(data, pos + table + index * 4);
    if (str < 248 || quint64(str) + header > size)
        return {};
    const quint32 len = le32(data, pos + str + (header == 24 ? 4 : 0));
    if (len == 0 || len > 4096 || quint64(str) + header + quint64(len) * 2 > size)
        return {};
    QString path;
    for (quint32 i = 0; i < len; ++i)
        path += QChar(quint16(le16(data, pos + str + header + i * 2)));
    return path;
}

QStringList unitNeedles(const QStringList &sourceNames)
{
    QStringList needles;
    for (const QString &n : sourceNames) {
        const QString file = n.section(QLatin1Char('/'), -1).section(QLatin1Char('\\'), -1);
        if (file.isEmpty())
            continue;
        if (!needles.contains(file))
            needles << file;
        const QString stem = QFileInfo(file).completeBaseName();
        // Short stems like "plr" match unrelated identifiers (plrBackend).
        if (stem.size() >= 6 && !needles.contains(stem))
            needles << stem;
        if (file.compare(QLatin1String("plr.qml"), Qt::CaseInsensitive) == 0) {
            for (const QString &extra : { QStringLiteral("iSidebar"), QStringLiteral("iTopbar"),
                                          QStringLiteral("PlrSidebar"), QStringLiteral("PlrTopbar") }) {
                if (!needles.contains(extra))
                    needles << extra;
            }
        }
    }
    return needles;
}

} // namespace

Link findLink(const QByteArray &data, qint64 entryOff, const QString &name)
{
    Link best;
    const auto sections = PeUtils::sections(data);
    const qint64 entryRva = PeUtils::rawToRva(sections, entryOff);
    if (entryRva < 0) {
        best.detail = QStringLiteral("Resource record at file 0x%1 is not mapped by a PE section; an appended orphan cannot be used as a loaded resource.").arg(entryOff, 0, 16);
        return best;
    }

    const auto pools = registeredPools(data);
    QList<qint64> names;
    if (!name.isEmpty()) {
        const QByteArray needle = nameBytes(name);
        for (qsizetype p = data.indexOf(needle); p >= 0; p = data.indexOf(needle, p + 1)) {
            if (p >= 6 && be16(data, p - 6) == name.size()) names << p - 6;
        }
    }
    struct Root { qint64 off; int pool; };
    QList<Root> roots;
    for (int i = 0; i < pools.size(); ++i) roots << Root { pools[i].tree, i };
    const QByteArray rootPrefix = QByteArray::fromHex("000000000002");
    if (!names.isEmpty()) {
        for (qsizetype root = data.indexOf(rootPrefix); root >= 0; root = data.indexOf(rootPrefix, root + 1)) {
            if (inRange(data, root, 14) && be32(data, root + 10) == 1
                && be32(data, root + 6) > 0 && be32(data, root + 6) < kMaxNodes)
                roots << Root { root, -1 };
        }
    }
    for (const auto &candidate : roots) {
        const qint64 root = candidate.off;
        const bool explicitBases = candidate.pool >= 0;
        const auto pool = explicitBases ? pools[candidate.pool] : Pool();
        const QList<int> strides = explicitBases ? QList<int> { pool.version == 1 ? 14 : 22 } : QList<int> { 22, 14 };
        for (const int stride : strides) {
            qint64 span = 0;
            const auto tree = readTree(data, root, stride, explicitBases, &span);
            if (tree.isEmpty()) continue;
            const qint64 treeEnd = root + span;
            const PeSection *treeSection = PeUtils::sectionForRaw(sections, root);
            if (!treeSection || treeEnd > qint64(treeSection->rawPtr) + treeSection->rawSize)
                continue;
            for (const Node &target : tree) {
                if (target.flags != 0 && target.flags != 1)
                    continue;
                if (explicitBases && pool.dataRva + target.value != entryRva) continue;
                const QList<qint64> targetNames = explicitBases ? QList<qint64> { pool.names + target.name } : names;
                for (const qint64 nameRecord : targetNames) {
                    const QString actualName = readName(data, nameRecord);
                    if (actualName.isEmpty() || (!name.isEmpty() && actualName != name)) continue;
                    const qint64 nameBase = explicitBases ? pool.names : nameRecord - target.name;
                    const qint64 dataBaseRva = explicitBases ? pool.dataRva : entryRva - target.value;
                    if (nameBase < 0 || dataBaseRva < 0 || dataBaseRva > 0xFFFFFFFFLL)
                        continue;
                    const qint64 dataBaseRaw = PeUtils::rvaToRaw(sections, quint32(dataBaseRva));
                    if (dataBaseRaw < 0)
                        continue;
                    Link link;
                    link.explicitBases = explicitBases;
                    link.registered = pool.registered;
                    link.sourceName = actualName;
                    link.compression = target.flags;
                    link.nodeSize = stride;
                    link.treeOff = root;
                    link.nodeOff = target.off;
                    link.nameRecordOff = nameRecord;
                    link.nameBaseRaw = nameBase;
                    link.dataBaseRaw = dataBaseRaw;
                    link.dataBaseRva = dataBaseRva;
                    link.dataOffset = target.value;
                    link.reserved << qMakePair(qint64(root), treeEnd);
                    bool valid = true;
                    for (const Node &node : tree) {
                        if (node.off == root)
                            continue; // root has no name record
                        const qint64 nr = nameBase + node.name;
                        if (readName(data, nr).isEmpty()) {
                            valid = false;
                            break;
                        }
                        const qint64 end = nr + 6 + qint64(be16(data, nr)) * 2;
                        if (overlaps(nr, end, { root, treeEnd })) {
                            valid = false;
                            break;
                        }
                        link.reserved << qMakePair(nr, end);
                    }
                    if (!valid)
                        continue;
                    const auto metadata = link.reserved;
                    QSet<qint64> otherPayloads;
                    for (const Node &node : tree) {
                        if (node.flags == 2)
                            continue;
                        const qint64 rva = dataBaseRva + node.value;
                        if (rva > 0xFFFFFFFFLL) {
                            valid = false;
                            break;
                        }
                        const qint64 raw = PeUtils::rvaToRaw(sections, quint32(rva));
                        if (!inRange(data, raw, 4)) {
                            valid = false;
                            break;
                        }
                        const quint32 size = be32(data, raw);
                        const qint64 end = raw + 4 + qint64(size);
                        const PeSection *section = PeUtils::sectionForRaw(sections, raw);
                        if (!section || !inRange(data, raw + 4, size)
                            || end > qint64(section->rawPtr) + section->rawSize) {
                            valid = false;
                            break;
                        }
                        for (const auto &range : metadata) {
                            if (overlaps(raw, end, range))
                                valid = false;
                        }
                        if (!valid)
                            break;
                        // Codec validation belongs to the resource being edited.
                        // An unrelated precompiled/opaque resource must not make
                        // a structurally valid theme tree appear unlinked.
                        if (raw == entryOff) {
                            if (node.flags != target.flags || size > 50000004u
                                || (node.flags == 1 && (size < 6 || be32(data, raw + 4) == 0
                                    || be32(data, raw + 4) > 50000000u
                                    || qUncompress(data.mid(raw + 4, size)).size() != be32(data, raw + 4)))) {
                                valid = false;
                                break;
                            }
                            link.nodes << node.off;
                            const QString alias = readName(data, nameBase + node.name);
                            if (!link.sourceNames.contains(alias)) link.sourceNames << alias;
                        } else {
                            otherPayloads.insert(raw);
                        }
                        link.reserved << qMakePair(raw, end);
                    }
                    if (!valid || link.nodes.isEmpty())
                        continue;
                    for (const auto &node : tree) {
                        if (node.flags == 2) continue;
                        const auto cacheName = readName(data, nameBase + node.name);
                        for (const auto &alias : link.sourceNames) {
                            if (cacheName == alias + QLatin1Char('c')) {
                                const auto raw = PeUtils::rvaToRaw(sections, quint32(dataBaseRva + node.value));
                                if (!link.themeCaches.contains(qMakePair(raw, node.flags)))
                                    link.themeCaches << qMakePair(raw, node.flags);
                            }
                        }
                    }
                    link.found = true;
                    link.detail = QStringLiteral("%1; %2 connected nodes, %3-byte records")
                                      .arg(link.registered ? QStringLiteral("qRegisterResourceData mapping")
                                           : explicitBases ? QStringLiteral("binary QRC mapping") : QStringLiteral("validated resource tree"))
                                      .arg(tree.size()).arg(stride);
                    link.support = otherPayloads.size();
                    link.strict = dataBaseRaw < nameBase && nameBase < root;
                    const int confidence = int(link.explicitBases) + int(link.registered);
                    const int bestConfidence = int(best.explicitBases) + int(best.registered);
                    if (!best.found || confidence > bestConfidence || (confidence == bestConfidence && link.support > best.support)) {
                        best = link;
                    } else if (confidence == bestConfidence && link.support == best.support
                               && (link.treeOff != best.treeOff || link.nodeOff != best.nodeOff
                                   || link.nodeSize != best.nodeSize)) {
                        // Aliases of the same payload in the same tree are fine.
                        if (link.treeOff != best.treeOff || link.nodeSize != best.nodeSize
                            || link.dataBaseRva != best.dataBaseRva)
                            best.ambiguous = true;
                    }
                }
            }
        }
    }
    if (!best.found)
        best.detail = QStringLiteral("No verified mapping: %1 filename record(s), %2 possible root(s), %3 explicit resource pool(s).")
                          .arg(names.size()).arg(roots.size()).arg(pools.size());
    return best;
}

QList<File> filesInPool(const QByteArray &data, const Link &pool)
{
    if (!pool.found || pool.ambiguous || (pool.nodeSize != 14 && pool.nodeSize != 22)
        || pool.treeOff < 0 || pool.nameBaseRaw < 0 || pool.dataBaseRva < 0)
        return {};
    qint64 span = 0;
    const auto tree = readTree(data, pool.treeOff, pool.nodeSize, pool.explicitBases, &span);
    const auto sections = PeUtils::sections(data);
    const auto *treeSection = PeUtils::sectionForRaw(sections, pool.treeOff);
    if (tree.isEmpty() || !treeSection || pool.treeOff + span > qint64(treeSection->rawPtr) + treeSection->rawSize)
        return {};

    // rcc stores one node per path component: "images" is a directory node and
    // "logo.png" is its child, so the node name alone is not the resource name.
    // Walk the tree to rebuild the full path; `name` stays the leaf.
    QHash<qint64, const Node *> byOffset;
    byOffset.reserve(tree.size());
    for (const auto &node : tree)
        byOffset.insert(node.off, &node);

    QList<File> files;
    QSet<QString> seen;
    std::function<bool(qint64, const QString &)> walk = [&](qint64 off, const QString &prefix) -> bool {
        const auto found = byOffset.constFind(off);
        if (found == byOffset.cend())
            return false;
        const Node &node = *found.value();
        if (node.flags == 2) {
            QString childPrefix = prefix;
            // The root's name word is unused and may hold anything (Qt leaves it
            // zero, some builds write garbage), so it is never read.
            if (off != pool.treeOff) {
                const QString directory = readName(data, pool.nameBaseRaw + node.name);
                if (directory.isEmpty())
                    return false;
                childPrefix += directory + QLatin1Char('/');
            }
            const quint32 count = be32(data, node.off + 6);
            const quint32 first = be32(data, node.off + 10);
            for (quint32 i = 0; i < count; ++i) {
                if (!walk(pool.treeOff + qint64(first + i) * pool.nodeSize, childPrefix))
                    return false;
            }
            return true;
        }
        const QString name = readName(data, pool.nameBaseRaw + node.name);
        const qint64 rva = pool.dataBaseRva + node.value;
        if (name.isEmpty() || rva < 0 || rva > 0xffffffffLL) return false;
        const qint64 raw = PeUtils::rvaToRaw(sections, quint32(rva));
        if (!inRange(data, raw, 4)) return false;
        const quint32 size = be32(data, raw);
        const auto *section = PeUtils::sectionForRaw(sections, raw);
        if (!section || !inRange(data, raw + 4, size)
            || raw + 4 + qint64(size) > qint64(section->rawPtr) + section->rawSize) return false;
        const QString key = name + QLatin1Char(':') + QString::number(raw);
        if (!seen.contains(key)) {
            seen.insert(key);
            files << File { name, raw, node.flags, prefix + name };
        }
        return true;
    };
    if (!walk(pool.treeOff, QString()))
        return {};
    return files;
}

QList<File> registeredFiles(const QByteArray &data)
{
    QList<File> files;
    QSet<QString> seen;
    for (const auto &registration : registeredPools(data)) {
        Link pool;
        pool.found = true; pool.explicitBases = true; pool.registered = registration.registered;
        pool.treeOff = registration.tree; pool.nameBaseRaw = registration.names;
        pool.dataBaseRva = registration.dataRva; pool.nodeSize = registration.version == 1 ? 14 : 22;
        for (const auto &file : filesInPool(data, pool)) {
            const auto key = file.name + QLatin1Char(':') + QString::number(file.entryOff);
            if (seen.contains(key)) continue;
            seen.insert(key); files << file;
        }
    }
    return files;
}

QByteArray readFile(const QByteArray &data, const File &file, qint64 limit)
{
    if (limit <= 0 || !inRange(data, file.entryOff, 4)) return {};
    const quint32 size = be32(data, file.entryOff);
    if (!inRange(data, file.entryOff + 4, size)) return {};
    if (file.flags == 0) return size <= limit ? data.mid(file.entryOff + 4, size) : QByteArray();
    if (file.flags != 1 || size < 6 || size > limit + 65536) return {};
    const quint32 expected = be32(data, file.entryOff + 4);
    if (expected > limit) return {};
    const auto bytes = qUncompress(data.mid(file.entryOff + 4, size));
    return bytes.size() == expected ? bytes : QByteArray();
}

bool refreshTimestamp(QByteArray &data, const Link &link)
{
    if (!link.found || link.ambiguous || link.nodeSize != 22)
        return false;
    for (const qint64 node : link.nodes) {
        if (!inRange(data, node + 14, 8))
            return false;
        const quint64 old = be64(data, node + 14);
        // Millisecond granularity, strictly different even for rapid repatches.
        const quint64 now = quint64(QDateTime::currentMSecsSinceEpoch());
        putBe64(data, node + 14, qMax(now, old + 1));
    }
    return true;
}

int invalidateThemeUnits(QByteArray &data)
{
    return invalidateUnits(data, { QStringLiteral("UiThemeObject.qml") });
}

int invalidateUnits(QByteArray &data, const QStringList &sourceNames)
{
    const QStringList needles = unitNeedles(sourceNames);
    int changed = 0;
    const QByteArray magic("qv4cdata\0", 8);
    for (qsizetype pos = data.indexOf(magic); pos >= 0; pos = data.indexOf(magic, pos + 8)) {
        // Qt 5.15 and Qt 6 use the same Unit header offsets. Check every bound
        // and the actual source string, not nearby occurrences of the filename.
        if (!inRange(data, pos, 248))
            continue;
        const quint32 size = le32(data, pos + 24);
        const quint32 qt = le32(data, pos + 12);
        if (size < 248 || !inRange(data, pos, size) || (qt >> 16 != 5 && qt >> 16 != 6))
            continue;
        const quint32 count = le32(data, pos + 112);
        const quint32 table = le32(data, pos + 116);
        const quint32 source = le32(data, pos + 236);
        if (table < 248 || quint64(table) + quint64(count) * 4 > size)
            continue;
        const int header = qt >> 16 == 5 ? 24 : 4;
        bool match = false;
        if (source < count) {
            QString path = unitString(data, pos, size, table, source, header);
            path.replace(QLatin1Char('\\'), QLatin1Char('/'));
            const QString file = path.section(QLatin1Char('/'), -1);
            if (!file.isEmpty() && sourceNames.contains(file))
                match = true;
        }
        // Qt 5.15.4 BlueStacks units often leave sourceFileIndex at 0 (empty
        // string). Scan the unit string table for distinctive identifiers so
        // compiled PlrSidebar/plr.qml cannot keep painting the old chrome.
        if (!match && !needles.isEmpty() && count > 0 && count < 4096) {
            for (quint32 i = 0; i < count && !match; ++i) {
                const QString s = unitString(data, pos, size, table, i, header);
                if (s.isEmpty())
                    continue;
                for (const QString &needle : needles) {
                    if (s.contains(needle, Qt::CaseInsensitive)) {
                        match = true;
                        break;
                    }
                }
            }
        }
        if (!match)
            continue;
        // The format version is outside the payload checksum. A mismatch
        // makes Qt fall back to the source without touching other cached units.
        if (le32(data, pos + 8) != 0) {
            putLe32(data, pos + 8, 0);
            ++changed;
        }
    }
    return changed;
}

namespace {

// Deletes the compiled units inside one cache directory, and the directory
// itself when nothing else lives there. Only qmlcache-named folders are ever
// touched, so nothing a user cares about can be reached from here.
bool clearCacheDir(const QString &path)
{
    QDir cache(path);
    if (!cache.exists())
        return false;
    bool removedSomething = false;
    const QStringList units = cache.entryList({ QStringLiteral("*.qmlc"), QStringLiteral("*.jsc"),
                                                QStringLiteral("*.mjsc") },
                                              QDir::Files | QDir::Hidden | QDir::System);
    for (const QString &unit : units) {
        if (cache.remove(unit))
            removedSomething = true;
    }
    const QStringList folders = cache.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QString &folder : folders) {
        QDir nested(cache.filePath(folder));
        nested.removeRecursively();
        removedSomething = true;
    }
    // A cache folder that is now empty is removed as a whole; one that still
    // holds anything else (a log, a lock) is left in place.
    if (cache.isEmpty(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden))
        QDir().rmdir(path);
    return removedSomething;
}

QStringList playerCacheRoots(const QString &exePath, bool includeSystemCaches)
{
    QStringList roots;
    const QFileInfo fi(exePath);
    const QDir dir(fi.absolutePath());
    // Next to the player itself, where BlueStacks 5 has always kept them.
    roots << QDir::cleanPath(dir.filePath(QStringLiteral("cache/qmlcache")))
          << QDir::cleanPath(dir.filePath(QStringLiteral("qmlcache")))
          << QDir::cleanPath(dir.filePath(QStringLiteral("cache/qml")))
          << QDir::cleanPath(dir.filePath(QStringLiteral("../cache/qmlcache")));
    if (!includeSystemCaches) {
        roots.removeDuplicates();
        return roots;
    }
    // Qt's own location: <cache>/<org>/<app>/qmlcache, i.e.
    // %LOCALAPPDATA%\BlueStacks_nxt\HD-Player\cache\qmlcache on Windows,
    // and the AppData / ProgramData trees BlueStacks keeps its data in.
    QStringList bases = {
        QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation),
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation),
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation),
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation),
        qEnvironmentVariable("ProgramData"),
        qEnvironmentVariable("LOCALAPPDATA"),
        qEnvironmentVariable("APPDATA"),
    };
    bases.removeAll(QString()); // an unset variable is not a root
    bases.removeDuplicates();
    // Matched token by token: "Microsoft" must not look like "MSI".
    const QStringList owners = { QStringLiteral("bluestacks"), QStringLiteral("msi"),
                                 QStringLiteral("now.gg") };
    const auto belongsToPlayer = [&owners](const QString &folderName) {
        const QString lower = folderName.toLower();
        if (lower.startsWith(QLatin1String("bluestacks")))
            return true;
        // Qt's own location is <org>/<app>, so the app folder is often plain
        // "HD-Player": matched as a whole name, since splitting on '-' would
        // leave only "hd" and "player".
        if (lower.contains(QLatin1String("hd-player")) || lower.contains(QLatin1String("hdplayer")))
            return true;
        const QStringList tokens = lower.split(QRegularExpression(QStringLiteral("[^a-z0-9.]+")),
                                               Qt::SkipEmptyParts);
        for (const QString &token : tokens)
            if (owners.contains(token))
                return true;
        return false;
    };
    // Bounded walk: BlueStacks keeps instances and disk images in the same
    // tree, so the search stops after a few levels and a fixed budget instead
    // of walking a 100 GB folder to find a cache directory.
    int visited = 0;
    std::function<void(const QString &, int)> visit = [&](const QString &folder, int depth) {
        if (depth > 3 || visited > 512)
            return;
        ++visited;
        const QDir current(folder);
        const QStringList entries = current.entryList(QDir::Dirs | QDir::NoDotAndDotDot
                                                      | QDir::Hidden);
        for (const QString &entry : entries) {
            const QString full = current.filePath(entry);
            if (entry.compare(QLatin1String("qmlcache"), Qt::CaseInsensitive) == 0) {
                roots << full;
                continue;
            }
            const QString lower = entry.toLower();
            if (lower == QLatin1String("engine") || lower == QLatin1String("logs")
                || lower == QLatin1String("sharedfolder") || lower == QLatin1String("bstsharedfolder"))
                continue; // the big, player-unrelated halves of the data folder
            visit(full, depth + 1);
        }
    };
    for (const QString &base : bases) {
        if (base.isEmpty() || !QDir(base).exists())
            continue;
        const QDir root(base);
        const QStringList children = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &child : children) {
            if (!belongsToPlayer(child))
                continue;
            visit(root.filePath(child), 1);
        }
    }
    roots.removeDuplicates();
    return roots;
}

} // namespace

int clearPlayerQmlCache(const QString &exePath)
{
    const QFileInfo fi(exePath);
    if (!fi.exists())
        return 0;
    // A file under the system temp folder is a fixture: clear only what sits
    // next to it, never the real player caches of the machine running a test.
    const bool fixture = fi.absolutePath().startsWith(QDir::cleanPath(QDir::tempPath()),
                                                      Qt::CaseInsensitive);
    int removed = 0;
    for (const QString &path : playerCacheRoots(exePath, !fixture)) {
        QDir cache(path);
        if (!cache.exists())
            continue;
        const QString name = cache.dirName();
        if (!name.contains(QLatin1String("qml"), Qt::CaseInsensitive)
            && !path.contains(QLatin1String("qmlcache"), Qt::CaseInsensitive))
            continue;
        if (clearCacheDir(path))
            ++removed;
    }
    return removed;
}

bool invalidateThemeCacheResources(QByteArray &data, const Link &link, QString *error)
{
    for (const auto &cache : link.themeCaches) {
        if (cache.second != 0 && cache.second != 1) {
            *error = QStringLiteral("Unsupported compression in the embedded theme cache. "
                                    "No file was written.");
            return false;
        }
        const quint32 size = be32(data, cache.first);
        QByteArray unit = data.mid(cache.first + 4, size);
        if (cache.second == 1)
            unit = qUncompress(unit);
        // A resource whose leaf is "<name>c" only counts as a compiled unit when
        // it really carries the qv4cdata header: the naming rule alone pairs any
        // file that ends in c. Anything else is left alone instead of failing the
        // edit - Qt rejects a unit it cannot parse by itself and compiles the
        // edited source, so there is nothing to invalidate here.
        if (unit.size() < 28 || !unit.startsWith(QByteArray("qv4cdata\0", 8))
            || le32(unit, 24) > quint32(unit.size()))
            continue;
        // The tree itself proves this is UiThemeObject.qmlc. This also handles
        // older Qt Unit layouts without relying on sourceFileIndex offsets.
        if (le32(unit, 8) == 0)
            continue;
        putLe32(unit, 8, 0);
        const QByteArray replacement = cache.second == 1 ? qCompress(unit, 9) : unit;
        if (replacement.size() > size) {
            Link cacheLink;
            for (const auto &name : link.sourceNames) {
                cacheLink = findLink(data, cache.first, name + QLatin1Char('c'));
                if (cacheLink.found && !cacheLink.ambiguous) break;
            }
            if (cache.second != 1 || !cacheLink.found || cacheLink.ambiguous
                || !replaceCompressed(data, cache.first, cacheLink, unit, error)) {
                if (error && error->isEmpty()) *error = QStringLiteral("The compiled resource cache cannot be safely relocated.");
                return false;
            }
            continue;
        }
        putBe32(data, cache.first, replacement.size());
        data.replace(cache.first + 4, size,
                     replacement + QByteArray(size - replacement.size(), '\0'));
    }
    return true;
}

} // namespace QtResources
