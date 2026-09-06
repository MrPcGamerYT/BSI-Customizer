#include "QtResources.h"
#include "ByteUtils.h"
#include "PeUtils.h"
#include <algorithm>
#include <limits>

using namespace ByteUtils;
namespace QtResources {
namespace {
qint64 align(qint64 value, quint32 alignment) { return (value + alignment - 1) & ~qint64(alignment - 1); }
bool powerOfTwo(quint32 value) { return value && !(value & (value - 1)); }
bool zeros(const QByteArray &b, qint64 from, qint64 to)
{
    if (to < from || !inRange(b, from, to - from)) return false;
    for (qint64 at = from; at < to; ++at) if (b[int(at)] != '\0') return false;
    return true;
}
}
qint64 appendStorage(QByteArray &data, qint64 size, QString *error)
{
    const auto refuse = [&](const QString &why) { if (error) *error = why; return qint64(-1); };
    if (size <= 0 || size > 50000008 || !PeUtils::isPe32Plus(data) || PeUtils::machine(data) != 0x8664)
        return refuse(QStringLiteral("No safe mapped resource space: a supported Windows x64 PE is required."));
    const qint64 pe = PeUtils::peHeaderOffset(data), opt = pe + 24;
    if (PeUtils::optionalHeaderSize(data) < 152 || !inRange(data, opt, 152))
        return refuse(QStringLiteral("Truncated PE optional header."));
    const quint32 sectionAlign = le32(data, opt + 32), fileAlign = le32(data, opt + 36);
    if (!powerOfTwo(fileAlign) || !powerOfTwo(sectionAlign) || fileAlign < 0x200
        || fileAlign > 0x10000 || sectionAlign < fileAlign)
        return refuse(QStringLiteral("Unsupported PE file/section alignment; no file was written."));
    const auto sections = PeUtils::sections(data);
    const int count = int(le16(data, pe + 6));
    if (sections.size() != count || count == 0 || count >= 96)
        return refuse(QStringLiteral("Invalid/full PE section table."));
    qint64 rawEnd = 0, imageEnd = 0, firstRaw = data.size();
    for (const auto &s : sections) {
        if (s.rawSize && !inRange(data, s.rawPtr, s.rawSize)) return refuse(QStringLiteral("Truncated PE section."));
        if (s.rawSize) firstRaw = qMin<qint64>(firstRaw, s.rawPtr);
        rawEnd = qMax(rawEnd, qint64(s.rawPtr) + s.rawSize);
        imageEnd = qMax(imageEnd, qint64(s.va) + qMax(s.rawSize, s.vsize));
    }
    // Keep all original bytes at their original offsets. Unknown overlays may
    // be located relative to EOF, so only zero padding or a certificate table
    // may precede a new mapped section.
    if (!zeros(data, rawEnd, data.size())) {
        const qint64 cert = le32(data, opt + 144), certSize = le32(data, opt + 148);
        if (cert < rawEnd || certSize < 8 || !inRange(data, cert, certSize)
            || !zeros(data, rawEnd, cert) || !zeros(data, cert + certSize, data.size())
            || le32(data, cert) < 8 || le32(data, cert) > certSize || le16(data, cert + 6) != 2)
            return refuse(QStringLiteral("Unknown PE overlay prevents safe resource growth; original bytes were retained."));
    }
    const qint64 table = opt + PeUtils::optionalHeaderSize(data);
    int reuse = -1;
    for (int i = 0; i < sections.size(); ++i) {
        const auto &s = sections[i];
        if (s.name == QLatin1String(".bsirc") && s.chars == 0x40000040
            && qint64(s.rawPtr) + s.rawSize == data.size()
            && qint64(s.va) + qMax(s.rawSize, s.vsize) == imageEnd) reuse = i;
    }
    const qint64 at = align(data.size(), reuse >= 0 ? 8 : fileAlign);
    const qint64 end = align(at + size, fileAlign);
    const qint64 rva = reuse >= 0 ? qint64(sections[reuse].va) + at - sections[reuse].rawPtr : align(imageEnd, sectionAlign);
    const qint64 newImageEnd = align(rva + size, sectionAlign);
    if (end > std::numeric_limits<int>::max() || newImageEnd > 0xffffffffLL)
        return refuse(QStringLiteral("Resource growth exceeds PE address limits."));
    qint64 header = table + qint64(count) * 40;
    if (reuse < 0 && (header + 40 > firstRaw || header + 40 > le32(data, opt + 60)
                     || !zeros(data, header, header + 40)))
        return refuse(QStringLiteral("No unused PE section-header slot is available for resource growth."));
    QByteArray result = data;
    result.append(QByteArray(end - result.size(), '\0'));
    quint32 added = quint32(end - at);
    if (reuse >= 0) {
        header = table + qint64(reuse) * 40;
        const auto &s = sections[reuse];
        added = quint32(end - s.rawPtr - s.rawSize);
        putLe32(result, header + 8, quint32(at + size - s.rawPtr));
        putLe32(result, header + 16, quint32(end - s.rawPtr));
    } else {
        result.replace(header, 8, QByteArray(".bsirc\0\0", 8));
        putLe32(result, header + 8, quint32(size));
        putLe32(result, header + 12, quint32(rva));
        putLe32(result, header + 16, quint32(end - at));
        putLe32(result, header + 20, quint32(at));
        putLe32(result, header + 36, 0x40000040); // initialized, readable, never executable/discardable
        putLe16(result, pe + 6, quint16(count + 1));
    }
    if (quint64(le32(result, opt + 8)) + added > 0xffffffffULL) return refuse(QStringLiteral("PE initialized-data size overflow."));
    putLe32(result, opt + 8, le32(result, opt + 8) + added);
    putLe32(result, opt + 56, quint32(newImageEnd));
    putLe32(result, opt + 64, 0); // checksum must not describe the old image
    data = result;
    return at;
}

bool replaceCompressed(QByteArray &data, qint64 entry, const Link &snapshot,
                       const QByteArray &source, QString *error, qint64 *newEntry)
{
    if (snapshot.compression != 1) {
        if (error) *error = QStringLiteral("The selected resource is not zlib-compressed.");
        return false;
    }
    return replaceResource(data, entry, snapshot, source, error, newEntry);
}

bool replaceResource(QByteArray &data, qint64 entry, const Link &snapshot,
                     const QByteArray &source, QString *error, qint64 *newEntry)
{
    const auto refuse = [&](const QString &message) { if (error) *error = message; return false; };
    if (!snapshot.found || snapshot.ambiguous || source.isEmpty() || source.size() > 50000000)
        return refuse(QStringLiteral("A unique validated resource and bounded replacement are required."));
    const auto link = findLink(data, entry, snapshot.sourceName);
    if (!link.found || link.ambiguous || link.treeOff != snapshot.treeOff || link.nodeOff != snapshot.nodeOff)
        return refuse(QStringLiteral("The resource mapping changed while preparing the patch."));
    const quint32 stored = be32(data, entry);
    const auto oldPayload = data.mid(entry + 4, stored);
    if (link.compression != 0 && link.compression != 1) return refuse(QStringLiteral("Unsupported resource compression."));
    const auto oldSource = link.compression == 1 ? qUncompress(oldPayload) : oldPayload;
    const QByteArray payload = oldSource == source ? oldPayload : (link.compression == 1 ? qCompress(source, 9) : source);
    if ((link.compression == 1 && payload.size() < 6)
        || (link.compression == 1 ? qUncompress(payload) : payload) != source)
        return refuse(QStringLiteral("Resource compression did not verify."));
    QByteArray record(4, '\0');
    putBe32(record, 0, payload.size()); record += payload;
    QByteArray changed = data;
    const auto sections = PeUtils::sections(changed);
    const auto *section = PeUtils::sectionForRaw(sections, entry);
    if (!section) return refuse(QStringLiteral("Resource is not mapped by a PE section."));
    qint64 limit = qMin<qint64>(changed.size(), qint64(section->rawPtr) + section->rawSize);
    const qint64 oldEnd = entry + 4 + stored;
    for (const auto &range : link.reserved) {
        if (range.first >= oldEnd) limit = qMin(limit, range.first);
        else if (range.second > oldEnd) limit = oldEnd;
    }
    qint64 end = oldEnd;
    while (end < limit && changed[int(end)] == '\0') ++end;
    qint64 destination = entry;
    if (record.size() > end - entry) {
        destination = appendStorage(changed, record.size(), error);
        if (destination < 0) return false;
        const qint64 rva = PeUtils::rawToRva(PeUtils::sections(changed), destination);
        const qint64 delta = rva - link.dataBaseRva;
        if (delta < 0 || delta > 0xffffffffLL) return refuse(QStringLiteral("New resource offset is out of range."));
        for (qint64 node : link.nodes) putBe32(changed, node + 10, quint32(delta));
        // Do not erase the old record: another independently registered pool
        // may still own it. Detection must follow the validated node, not orphans.
    }
    changed.replace(destination, record.size(), record);
    if (destination == entry && record.size() < 4 + stored)
        changed.replace(entry + record.size(), 4 + stored - record.size(), QByteArray(4 + stored - record.size(), '\0'));
    refreshTimestamp(changed, link);
    invalidateUnits(changed, link.sourceNames);
    if (!invalidateThemeCacheResources(changed, link, error)) return false;
    const auto verified = findLink(changed, destination, link.sourceName);
    if (!verified.found || verified.ambiguous || verified.nodes != link.nodes
        || verified.compression != link.compression
        || readFile(changed, { link.sourceName, destination, link.compression }, 50000000) != source)
        return refuse(QStringLiteral("Rewritten resource did not pass verification; no file was written."));
    data = changed;
    if (newEntry) *newEntry = destination;
    return true;
}
} // namespace QtResources
