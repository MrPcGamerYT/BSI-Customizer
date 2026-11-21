#include "QtResources.h"
#include "ByteUtils.h"
#include "PeUtils.h"
#include "QtResourceRegistration.h"
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
    const qint64 pe = PeUtils::peHeaderOffset(data), opt = pe + 24;
    // Growth has to work for the older 32-bit players too, so the image is
    // accepted with either machine/header pairing (x64 + PE32+ or i386 + PE32).
    // Every optional-header field used below sits at the same offset in both
    // layouts; only the certificate directory moves, because a PE32 header has
    // one 4-byte BaseOfData field where a PE32+ header has an 8-byte ImageBase.
    const bool plus = PeUtils::isPe32Plus(data);
    if (size <= 0 || size > 50000008 || pe < 0
        || (plus ? PeUtils::machine(data) != 0x8664u : PeUtils::machine(data) != 0x14cu))
        return refuse(QStringLiteral("No safe mapped resource space: the image is neither a Windows x64 nor a Windows x86 PE."));
    const qint64 certDir = plus ? 144 : 128;
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
        const qint64 cert = le32(data, opt + certDir), certSize = le32(data, opt + certDir + 4);
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

namespace {

// Where the entry's own space ends: its record plus the zero padding behind it,
// bounded by whatever the writer must not overwrite. Shared by the writer and
// by inPlaceCapacity() so the budget shown in the UI is the exact one enforced.
qint64 entrySpaceEnd(const QByteArray &data, const Link &link, qint64 entry, QString *error)
{
    if (!inRange(data, entry, 4)) {
        if (error) *error = QStringLiteral("The resource record is outside the file.");
        return -1;
    }
    if (!PeUtils::sectionForRaw(PeUtils::sections(data), entry)) {
        if (error) *error = QStringLiteral("The resource is not mapped by a PE section.");
        return -1;
    }
    const quint32 stored = be32(data, entry);
    const auto sections = PeUtils::sections(data);
    const auto *section = PeUtils::sectionForRaw(sections, entry);
    qint64 limit = qMin<qint64>(data.size(), qint64(section->rawPtr) + section->rawSize);
    const qint64 oldEnd = entry + 4 + stored;
    for (const auto &range : link.reserved) {
        if (range.first >= oldEnd) limit = qMin(limit, range.first);
        else if (range.second > oldEnd) limit = oldEnd;
    }
    // Other registered pools can begin with zero-prefixed headers too.
    // Their first bytes are metadata, never slack belonging to this resource.
    for (const auto &pool : registeredPools(data)) {
        if (pool.tree >= oldEnd) limit = qMin(limit, pool.tree);
        if (pool.names >= oldEnd) limit = qMin(limit, pool.names);
    }
    for (const auto &file : registeredFiles(data))
        if (file.entryOff >= oldEnd) limit = qMin(limit, file.entryOff);
    qint64 end = oldEnd;
    while (end < limit && data[int(end)] == '\0') ++end;
    return end;
}

} // namespace

qint64 inPlaceCapacity(const QByteArray &data, qint64 entry, const QString &name,
                       QString *error)
{
    if (entry < 0 || !inRange(data, entry, 4)) {
        if (error) *error = QStringLiteral("The resource record is outside the file.");
        return -1;
    }
    const Link link = findLink(data, entry, name);
    if (!link.found || link.ambiguous) {
        if (error) *error = link.found ? QStringLiteral("The resource mapping is ambiguous.")
                                       : QStringLiteral("The resource mapping was not found.");
        return -1;
    }
    const qint64 end = entrySpaceEnd(data, link, entry, error);
    if (end < 0) return -1;
    return end - entry - 4;
}

qint64 findFreeSpace(const QByteArray &data, const Link &link, qint64 need, QString *error)
{
    const auto fail = [&](const QString &message) {
        if (error) *error = message;
        return qint64(-1);
    };
    if (!link.found || link.ambiguous || link.nodes.isEmpty() || link.dataBaseRva < 0)
        return fail(QStringLiteral("The resource mapping is not unique, so the record cannot be "
                                   "moved anywhere else."));
    if (need <= 0 || need > 50000000)
        return fail(QStringLiteral("That replacement size cannot be placed."));
    const auto sections = PeUtils::sections(data);
    if (sections.isEmpty())
        return fail(QStringLiteral("The PE section table could not be read."));

    // Data sections only: that is where rcc puts resource payloads, and a zero
    // run inside .text may be alignment padding between functions that some
    // later build fetches into. A build with no free data section gets a clear
    // refusal instead.
    QList<const PeSection *> order;
    for (const QString &wanted : { QStringLiteral(".rdata"), QStringLiteral(".data"),
                                   QStringLiteral(".rsrc") })
        for (const PeSection &section : sections)
            if (section.name == wanted && !order.contains(&section)) order << &section;
    for (const PeSection &section : sections)
        if (section.name != QLatin1String(".text") && !order.contains(&section)) order << &section;

    // link.reserved covers the target's own pool (payloads, names, tree). A cave
    // in another pool's payload could hold a long run of zero bytes too, so every
    // payload the EXE registers is blocked as well: only bytes no resource owns
    // may be reused.
    QList<QPair<qint64, qint64>> blocked = link.reserved;
    for (const auto &file : registeredFiles(data)) {
        if (file.entryOff < 0 || !inRange(data, file.entryOff, 4)) continue;
        blocked << qMakePair(file.entryOff, file.entryOff + 4 + qint64(be32(data, file.entryOff)));
    }
    // The pool the link itself points into, walked through the tree rather than
    // through the registrations: a build whose pool is registered in a shape
    // this tool cannot enumerate still owns every payload in that pool, and a
    // long run of zeros inside one of them (a .wav full of silence, a padded
    // blob) is data, not free space.
    for (const auto &file : filesInPool(data, link)) {
        if (file.entryOff < 0 || !inRange(data, file.entryOff, 4)) continue;
        blocked << qMakePair(file.entryOff, file.entryOff + 4 + qint64(be32(data, file.entryOff)));
    }

    const QByteArray probe(int(qMin<qint64>(need, 64)), '\0');
    const QByteArray wanted(int(need), '\0');
    // Protect PE directory storage (imports, TLS, Windows resources, etc.)
    // independently of the selected Qt pool. A zero directory field is not a cave.
    const int pe = PeUtils::peHeaderOffset(data), opt = pe + 24;
    const bool plus = PeUtils::isPe32Plus(data);
    const int dirStart = plus ? 112 : 96;
    if (pe < 0 || !inRange(data, opt, dirStart)) return fail(QStringLiteral("Invalid PE directories."));
    const quint32 directoryCount = qMin<quint32>(le32(data, opt + (plus ? 108 : 92)), 16);
    for (quint32 i = 0; i < directoryCount; ++i) {
        const qint64 at = opt + dirStart + i * 8;
        if (!inRange(data, at, 8) || at + 8 > opt + PeUtils::optionalHeaderSize(data)) return fail(QStringLiteral("Truncated PE directories."));
        const quint32 address = le32(data, at), size = le32(data, at + 4);
        if (!address || !size) continue;
        const qint64 raw = i == 4 ? address : PeUtils::rvaToRaw(sections, address);
        if (raw < 0 || !inRange(data, raw, size)) return fail(QStringLiteral("An existing PE directory cannot be bounded safely."));
        blocked << qMakePair(raw, raw + qint64(size));
    }
    for (const auto &pool : registeredPools(data)) {
        // The selected pool already has fully validated, exact metadata bounds
        // in link.reserved. Reserving its tree through the section end would
        // also hide legitimate free storage after that tree.
        if (pool.tree == link.treeOff && pool.names == link.nameBaseRaw
            && pool.dataRva == link.dataBaseRva) continue;
        // Conservatively reserve other pools whose exact metadata bounds were
        // not validated by this link. Their zero-prefixed words are not caves.
        const auto *treeSection = PeUtils::sectionForRaw(sections, pool.tree);
        const auto *namesSection = PeUtils::sectionForRaw(sections, pool.names);
        const qint64 poolData = PeUtils::rvaToRaw(sections, quint32(pool.dataRva));
        if (treeSection) {
            qint64 end = qint64(treeSection->rawPtr) + treeSection->rawSize;
            if (pool.names > pool.tree) end = qMin(end, pool.names);
            if (poolData > pool.tree) end = qMin(end, poolData);
            blocked << qMakePair(pool.tree, end);
        }
        if (namesSection) {
            qint64 end = qint64(namesSection->rawPtr) + namesSection->rawSize;
            if (pool.tree > pool.names) end = qMin(end, pool.tree);
            if (poolData > pool.names) end = qMin(end, poolData);
            blocked << qMakePair(pool.names, end);
        }
    }
    for (const PeSection *section : order) {
        if (section->chars & 0x20000000u) continue;
        const qint64 begin = section->rawPtr;
        const qint64 end = qMin<qint64>(data.size(), qint64(section->rawPtr) + section->rawSize);
        if (end - begin < need) continue;
        qsizetype pos = data.indexOf(probe, qsizetype(begin));
        while (pos >= 0 && pos + need <= end) {
            const qint64 at = qMax<qint64>(qint64(pos), (begin + 3) & ~qint64(3));
            if (at >= pos && at + need <= end && data.mid(at, int(need)) == wanted) {
                bool usable = true;
                for (const auto &range : blocked) {
                    if (at < range.second && at + need > range.first) { usable = false; break; }
                }
                if (usable) {
                    const qint64 rva = PeUtils::rawToRva(sections, at);
                    if (rva >= 0) {
                        // The tree node stores an unsigned distance from the pool's
                        // data base, so a gap in front of that base cannot be
                        // addressed at all - relocation would go negative.
                        const qint64 delta = rva - link.dataBaseRva;
                        if (delta >= 0 && delta <= 0xffffffffLL) return at;
                    }
                }
            }
            pos = data.indexOf(probe, pos + 1);
        }
    }
    return fail(QStringLiteral("The EXE has no free space left for a record of %1 bytes. Nothing was "
                               "written.").arg(need));
}

bool replaceResource(QByteArray &data, qint64 entry, const Link &snapshot,
                     const QByteArray &source, QString *error, qint64 *newEntry,
                     bool allowGrowth, qint64 forcedDestination, const QStringList &preciseIdentifiers)
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
    if (!PeUtils::sectionForRaw(PeUtils::sections(changed), entry))
        return refuse(QStringLiteral("Resource is not mapped by a PE section."));
    const qint64 end = entrySpaceEnd(changed, link, entry, error);
    if (end < 0) return false;
    qint64 destination = entry;
    if (record.size() > end - entry) {
        if (forcedDestination >= 0) {
            // Relocation into space the file already has: the record is built
            // in a verified zero run and the tree node points at it. The file
            // size never changes, so nothing needs to move.
            if (!inRange(changed, forcedDestination, record.size()))
                return refuse(QStringLiteral("The chosen free space does not fit the replacement."));
            if (changed.mid(forcedDestination, record.size()) != QByteArray(record.size(), '\0'))
                return refuse(QStringLiteral("The chosen free space is not empty any more."));
            destination = forcedDestination;
        } else if (!allowGrowth) {
            return refuse(QStringLiteral("The replacement needs %1 bytes, but this entry only has %2 bytes "
                                         "of room. Growing the file is disabled, so nothing was written - "
                                         "make the replacement smaller.")
                              .arg(record.size())
                              .arg(end - entry));
        } else {
            // Prefer verified existing data-section storage. Growing first
            // needlessly rejects otherwise supported images whose section
            // headers cannot be extended, and changes their size unnecessarily.
            destination = findFreeSpace(changed, link, record.size(), error);
            if (destination < 0)
                destination = appendStorage(changed, record.size(), error);
            if (destination < 0) return false;
            if (error) error->clear();
        }
        const qint64 rva = PeUtils::rawToRva(PeUtils::sections(changed), destination);
        if (rva < 0) return refuse(QStringLiteral("New resource offset is not mapped by a PE section."));
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
    if (preciseIdentifiers.isEmpty()) invalidateUnits(changed, link.sourceNames);
    else sourceUnits(changed, link.sourceNames, preciseIdentifiers, true);
    if (!invalidateThemeCacheResources(changed, link, error)) return false;
    // A source write is not enough when the player still has a matching
    // compiled QV4 unit. Refuse the replacement unless every unit we can
    // positively associate with this resource has been disabled. This keeps
    // generic editors and feature patchers from reporting success for a QML
    // change that the emulator will ignore.
    const int compiledMatches = preciseIdentifiers.isEmpty() ? matchingUnits(changed, link.sourceNames) : sourceUnits(changed,link.sourceNames,preciseIdentifiers);
    const int compiledStillActive = preciseIdentifiers.isEmpty() ? activeUnits(changed, link.sourceNames) : sourceUnits(changed,link.sourceNames,preciseIdentifiers,false,true);
    if (compiledMatches > 0 && compiledStillActive > 0)
        return refuse(QStringLiteral("The resource source was prepared, but %1 matching compiled QML unit(s) remain active; the player may ignore this edit. Nothing was written. Retry on a supported player build or restore its backup.")
                          .arg(compiledStillActive));
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
