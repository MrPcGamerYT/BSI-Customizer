#include "QtResources.h"

#include "ByteUtils.h"
#include "PeUtils.h"

#include <QDateTime>
#include <QSet>

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
QList<Node> readTree(const QByteArray &data, qint64 root, int stride)
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
            if (count > kMaxNodes || first >= kMaxNodes || quint64(first) + count > kMaxNodes
                || (count && first <= index))
                return {};
            if (!inRange(data, root, qint64(first + count) * stride))
                return {};
            for (quint32 i = 0; i < count; ++i)
                pending << first + i;
        }
    }
    if (seen.size() != last + 1)
        return {}; // gaps are not emitted by rcc
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

} // namespace

Link findLink(const QByteArray &data, qint64 entryOff, const QString &name)
{
    Link best;
    const auto sections = PeUtils::sections(data);
    const qint64 entryRva = PeUtils::rawToRva(sections, entryOff);
    if (entryRva < 0)
        return best;

    QList<qint64> names;
    const QByteArray needle = nameBytes(name);
    for (qsizetype p = data.indexOf(needle); p >= 0; p = data.indexOf(needle, p + 1)) {
        if (p >= 6 && be16(data, p - 6) == name.size())
            names << p - 6;
    }
    if (names.isEmpty())
        return best;

    // Root: name offset 0, Directory, child count, first child index 1.
    const QByteArray rootPrefix = QByteArray::fromHex("000000000002");
    for (qsizetype root = data.indexOf(rootPrefix); root >= 0;
         root = data.indexOf(rootPrefix, root + 1)) {
        if (!inRange(data, root, 14) || be32(data, root + 10) != 1
            || be32(data, root + 6) == 0 || be32(data, root + 6) >= kMaxNodes)
            continue;
        for (const int stride : { 22, 14 }) {
            const auto tree = readTree(data, root, stride);
            if (tree.isEmpty())
                continue;
            const qint64 treeEnd = root + qint64(tree.size()) * stride;
            const PeSection *treeSection = PeUtils::sectionForRaw(sections, root);
            if (!treeSection || treeEnd > qint64(treeSection->rawPtr) + treeSection->rawSize)
                continue;
            for (const Node &target : tree) {
                if (target.flags != 1)
                    continue;
                for (const qint64 nameRecord : names) {
                    const qint64 nameBase = nameRecord - target.name;
                    const qint64 dataBaseRva = entryRva - target.value;
                    if (nameBase < 0 || dataBaseRva < 0 || dataBaseRva > 0xFFFFFFFFLL)
                        continue;
                    const qint64 dataBaseRaw = PeUtils::rvaToRaw(sections, quint32(dataBaseRva));
                    if (dataBaseRaw < 0)
                        continue;
                    Link link;
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
                    if (!valid || readName(data, nameRecord) != name)
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
                        if (node.flags == 1) {
                            // Qt's compressed data is qCompress: usize + zlib.
                            if (size < 6 || be32(data, raw + 4) == 0
                                || uchar(data[int(raw + 8)]) != 0x78
                                || be16(data, raw + 8) % 31 != 0) {
                                valid = false;
                                break;
                            }
                            if (raw == entryOff) {
                                const quint32 usize = be32(data, raw + 4);
                                if (usize > 50000000u
                                    || qUncompress(data.mid(raw + 4, size)).size() != usize) {
                                    valid = false;
                                    break;
                                }
                            }
                        } else if (node.flags == 4) {
                            if (size < 4 || be32(data, raw + 4) != 0x28B52FFD) {
                                valid = false;
                                break;
                            }
                        }
                        if (readName(data, nameBase + node.name) == name + QLatin1Char('c'))
                            link.themeCaches << qMakePair(raw, node.flags);
                        if (raw == entryOff)
                            link.nodes << node.off;
                        else
                            otherPayloads.insert(raw);
                        link.reserved << qMakePair(raw, end);
                    }
                    if (!valid || link.nodes.isEmpty())
                        continue;
                    link.found = true;
                    link.support = otherPayloads.size();
                    link.strict = dataBaseRaw < nameBase && nameBase < root;
                    if (!best.found || link.support > best.support) {
                        best = link;
                    } else if (link.support == best.support
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
    return best;
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
        if (table < 248 || quint64(table) + quint64(count) * 4 > size || source >= count)
            continue;
        const quint32 str = le32(data, pos + table + source * 4);
        const int header = qt >> 16 == 5 ? 24 : 4;
        if (str < 248 || quint64(str) + header > size)
            continue;
        const quint32 len = le32(data, pos + str + (header == 24 ? 4 : 0));
        if (len == 0 || len > 4096 || quint64(str) + header + quint64(len) * 2 > size)
            continue;
        QString path;
        for (quint32 i = 0; i < len; ++i)
            path += QChar(quint16(le16(data, pos + str + header + i * 2)));
        path.replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (path.section(QLatin1Char('/'), -1) != QLatin1String("UiThemeObject.qml"))
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
        if (unit.size() < 28 || !unit.startsWith(QByteArray("qv4cdata\0", 8))
            || le32(unit, 24) > quint32(unit.size())) {
            *error = QStringLiteral("Cannot validate the embedded theme cache. No file was written.");
            return false;
        }
        // The tree itself proves this is UiThemeObject.qmlc. This also handles
        // older Qt Unit layouts without relying on sourceFileIndex offsets.
        if (le32(unit, 8) == 0)
            continue;
        putLe32(unit, 8, 0);
        const QByteArray replacement = cache.second == 1 ? qCompress(unit, 9) : unit;
        if (replacement.size() > size) {
            *error = QStringLiteral("The embedded theme cache cannot be invalidated in its "
                                    "current slot. No file was written.");
            return false;
        }
        putBe32(data, cache.first, replacement.size());
        data.replace(cache.first + 4, size,
                     replacement + QByteArray(size - replacement.size(), '\0'));
    }
    return true;
}

} // namespace QtResources
