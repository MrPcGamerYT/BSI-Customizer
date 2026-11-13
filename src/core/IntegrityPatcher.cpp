#include "IntegrityPatcher.h"

#include "ByteUtils.h"
#include "BsDetector.h"
#include "PatchFile.h"
#include "PeUtils.h"
#include "ThemeEngine.h"

#include <QFile>
#include <QFileInfo>
#include <QSet>

using namespace ByteUtils;

namespace IntegrityPatcher {
namespace {
QByteArray read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
QSet<qint64> stringRvas(const QByteArray &bytes, const QList<PeSection> &sections)
{
    QSet<qint64> result;
    for (const QByteArray &text : { QByteArray("Verified the disk integrity!"),
                                   QByteArray("Failed to verify the disk integrity!") }) {
        QByteArray wide;
        for (char c : text) { wide += c; wide += '\0'; }
        const QList<QByteArray> needles { text + '\0', wide + QByteArray(2, '\0') };
        for (const auto &needle : needles) {
            for (qsizetype at = bytes.indexOf(needle); at >= 0; at = bytes.indexOf(needle, at + 1)) {
                const qint64 rva = PeUtils::rawToRva(sections, at);
                if (rva >= 0) result.insert(rva);
            }
        }
    }
    return result;
}
bool nearAnchor(const QByteArray &bytes, const PeSection &text, qint64 site, const QSet<qint64> &anchors)
{
    const qint64 start = qMax<qint64>(text.rawPtr, site - 0xc8);
    const qint64 end = qMin<qint64>(bytes.size(), qMin<qint64>(qint64(text.rawPtr) + text.rawSize, site + 0xc8));
    for (qint64 p = start; p + 7 <= end; ++p) {
        const uchar rex = uchar(bytes[int(p)]);
        if ((rex != 0x48 && rex != 0x49 && rex != 0x4c && rex != 0x4d)
            || uchar(bytes[int(p + 1)]) != 0x8d || (uchar(bytes[int(p + 2)]) & 0xc7) != 0x05)
            continue;
        const qint64 target = qint64(text.va) + (p + 7 - text.rawPtr) + qint32(le32(bytes, p + 3));
        if (target >= 0 && anchors.contains(target)) return true;
    }
    return false;
}
bool backupProvesPatch(const QByteArray &bytes, const QByteArray &backup, const PeSection &text, qint64 site)
{
    if (!inRange(backup, text.rawPtr, text.rawSize) || !inRange(backup, site - 5, 9)
        || !PeUtils::isPe32Plus(backup) || PeUtils::machine(backup) != PeUtils::machine(bytes))
        return false;
    bool sameText = false;
    for (const auto &section : PeUtils::sections(backup)) {
        if (section.name == text.name && section.va == text.va && section.rawPtr == text.rawPtr
            && section.rawSize == text.rawSize) sameText = true;
    }
    if (!sameText || backup.mid(site - 5, 7) != bytes.mid(site - 5, 7)
        || uchar(backup[int(site + 2)]) != 0x74) return false;
    QByteArray normalized = bytes.mid(text.rawPtr, text.rawSize);
    normalized.replace(site + 2 - text.rawPtr, 2, backup.mid(site + 2, 2));
    return normalized == backup.mid(text.rawPtr, text.rawSize);
}
} // namespace

Analysis analyze(const QString &exePath)
{
    Analysis out;
    if (!QFileInfo::exists(exePath)) {
        out.state = QStringLiteral("nofile"); out.msg = QStringLiteral("HD-Player.exe not found."); return out;
    }
    const QByteArray bytes = read(exePath);
    if (!PeUtils::isPe32Plus(bytes) || PeUtils::machine(bytes) != 0x8664) {
        out.state = QStringLiteral("bad"); out.msg = QStringLiteral("A readable Windows x64 PE32+ HD-Player.exe is required."); return out;
    }
    const auto sections = PeUtils::sections(bytes);
    const PeSection *text = nullptr;
    for (const auto &section : sections) {
        if (section.name == QLatin1String(".text") && (section.chars & 0x20000000)) { text = &section; break; }
    }
    if (!text || !inRange(bytes, text->rawPtr, text->rawSize)) {
        out.state = QStringLiteral("bad"); out.msg = QStringLiteral("Missing or truncated executable .text section."); return out;
    }
    const auto anchors = stringRvas(bytes, sections);
    QList<qint64> clean, nopped, validated;
    const qint64 end = qint64(text->rawPtr) + text->rawSize;
    for (qint64 t = qint64(text->rawPtr) + 5; t + 4 <= end; ++t) {
        if (uchar(bytes[int(t - 5)]) != 0xe8 || uchar(bytes[int(t)]) != 0x84
            || uchar(bytes[int(t + 1)]) != 0xc0) continue;
        if (uchar(bytes[int(t + 2)]) == 0x74) clean << t;
        else if (bytes.mid(t + 2, 2) == QByteArray::fromHex("9090")) nopped << t;
        else continue;
        if (nearAnchor(bytes, *text, t, anchors)) validated << t;
    }
    // The reference's candidate count is for CALL; TEST; JZ, not arbitrary
    // CALL; TEST; NOP; NOP sequences elsewhere in the executable.
    out.candidates = clean.size();
    qint64 site = -1;
    QString how;
    if (validated.size() == 1) {
        site = validated.first(); how = QStringLiteral("verify/fail string reference");
    } else if (validated.size() > 1) {
        out.state = QStringLiteral("ambig");
        out.msg = QStringLiteral("%1 integrity candidates reference verify/fail messages. No bytes changed; refusing to choose an arbitrary site.").arg(validated.size());
        return out;
    } else {
        const auto backup = read(exePath + QStringLiteral(".bak"));
        QList<qint64> proven;
        for (qint64 t : nopped) if (backupProvesPatch(bytes, backup, *text, t)) proven << t;
        if (proven.size() == 1) {
            site = proven.first(); how = QStringLiteral("original backup and unchanged .text");
        } else if (clean.size() == 1) {
            site = clean.first(); how = QStringLiteral("unique CALL; TEST AL,AL; JZ match");
        } else {
            out.state = clean.size() > 1 ? QStringLiteral("ambig") : QStringLiteral("notfound");
            out.msg = QStringLiteral("Found %1 clean integrity candidate(s), but no unique verified site. No bytes changed. This does not prove protection is absent.").arg(clean.size());
            return out;
        }
    }
    const bool patched = bytes.mid(site + 2, 2) == QByteArray::fromHex("9090");
    out.ok = true;
    out.state = patched ? QStringLiteral("patched") : QStringLiteral("clean");
    out.site = site;
    out.how = how;
    out.bytes = bytes;
    out.msg = QStringLiteral("%1 at file 0x%2 (%3).")
                  .arg(patched ? QStringLiteral("Integrity bypass already applied") : QStringLiteral("Integrity patch site ready"),
                       QString::number(site + 2, 16), how);
    return out;
}

Result patch(const QString &exePath, bool dry)
{
    const auto analysis = analyze(exePath);
    if (!analysis.ok) return { false, analysis.msg };
    if (analysis.state == QLatin1String("patched")) return { true, analysis.msg + QStringLiteral(" Nothing changed.") };
    if (dry) return { true, QStringLiteral("Dry run: replace only the two JZ bytes at 0x%1 with 90 90. No backup or file was written.").arg(analysis.site + 2, 0, 16) };
    if (BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe")))
        return { false, QStringLiteral("Close all BlueStacks instances before patching integrity. Running processes were not forcibly terminated.") };
    QByteArray modified = analysis.bytes;
    modified.replace(analysis.site + 2, 2, QByteArray::fromHex("9090"));
    if (!ThemeEngine::backup(exePath)) return { false, QStringLiteral("Could not preserve the original .bak; no EXE was written.") };
    QString error;
    if (!PatchFile::writeVerified(exePath, analysis.bytes, modified, &error)) return { false, error };
    return { true, QStringLiteral("Integrity bypass written and verified at 0x%1. Only the two JZ bytes were changed.").arg(analysis.site + 2, 0, 16) };
}

Result restore(const QString &exePath)
{
    if (BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe")))
        return { false, QStringLiteral("Close all BlueStacks instances before restoring the EXE.") };
    const auto backup = read(exePath + QStringLiteral(".bak"));
    if (!PeUtils::isPe32Plus(backup) || PeUtils::machine(backup) != 0x8664)
        return { false, QStringLiteral("A readable original Windows x64 .bak is required.") };
    QFile current(exePath);
    if (!current.open(QIODevice::ReadOnly)) return { false, QStringLiteral("Cannot read the current EXE.") };
    const auto original = current.readAll(); current.close();
    QString error;
    if (!PatchFile::writeVerified(exePath, original, backup, &error)) return { false, error };
    return { true, QStringLiteral("Full original EXE restored and verified. Theme/boot changes made after that backup are also restored.") };
}
} // namespace IntegrityPatcher
