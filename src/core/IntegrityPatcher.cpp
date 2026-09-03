#include "IntegrityPatcher.h"

#include "ByteUtils.h"
#include "PeUtils.h"
#include "ThemeEngine.h"

#include <QFile>
#include <QFileInfo>

using namespace ByteUtils;

namespace IntegrityPatcher {

namespace {

qint64 stringRva(const QByteArray &b, const QString &text, const QList<PeSection> &secs)
{
    const QByteArray needle = text.toLatin1() + '\0';
    const qsizetype i = b.indexOf(needle);
    if (i < 0)
        return -1;
    return PeUtils::rawToRva(secs, i);
}

} // namespace

Analysis analyze(const QString &exePath)
{
    Analysis out;
    if (!QFileInfo::exists(exePath)) {
        out.state = QStringLiteral("nofile");
        out.msg = QStringLiteral("HD-Player.exe not found");
        return out;
    }
    QFile f(exePath);
    if (!f.open(QIODevice::ReadOnly)) {
        out.state = QStringLiteral("bad");
        out.msg = QStringLiteral("cannot read file");
        return out;
    }
    const QByteArray b = f.readAll();
    f.close();

    if (b.size() < 0x200 || PeUtils::peHeaderOffset(b) < 0) {
        out.state = QStringLiteral("bad");
        out.msg = QStringLiteral("invalid PE header");
        return out;
    }

    const QList<PeSection> secs = PeUtils::sections(b);
    const PeSection *text = nullptr;
    for (const PeSection &s : secs) {
        if (s.name == QLatin1String(".text")) {
            text = &s;
            break;
        }
    }
    if (!text) {
        out.state = QStringLiteral("bad");
        out.msg = QStringLiteral(".text section not found");
        return out;
    }

    const qint64 verifyRva = stringRva(b, QStringLiteral("Verified the disk integrity!"), secs);
    const qint64 failRva = stringRva(b, QStringLiteral("Failed to verify the disk integrity!"), secs);

    // Candidate sites: call rel32 ; test al,al ; jz rel8 (or already NOPed)
    QList<qint64> candidates;
    const qint64 textRaw = text->rawPtr;
    const qint64 textEnd = qMin<qint64>(b.size(), textRaw + text->rawSize);
    for (qint64 t = textRaw + 5; t < textEnd - 3; ++t) {
        if (uchar(b[int(t)]) != 0x84 || uchar(b[int(t) + 1]) != 0xC0
            || uchar(b[int(t) - 5]) != 0xE8)
            continue;
        const uchar b2 = uchar(b[int(t) + 2]);
        if (b2 == 0x74 || (b2 == 0x90 && uchar(b[int(t) + 3]) == 0x90))
            candidates << t;
    }
    out.candidates = candidates.size();

    if (candidates.isEmpty()) {
        if (verifyRva < 0 && failRva < 0) {
            out.state = QStringLiteral("absent");
            out.msg = QStringLiteral("no tamper protection in this build");
        } else {
            out.state = QStringLiteral("notfound");
            out.msg = QStringLiteral("patch site not found (version may differ)");
        }
        return out;
    }

    qint64 site = -1;
    QString how;
    if (candidates.size() == 1) {
        site = candidates.first();
        how = QStringLiteral("unique match");
    } else {
        for (const qint64 c : candidates) {
            const qint64 lo = qMax(textRaw, c - 0xC8);
            const qint64 hi = c + 0xC8;
            bool found = false;
            for (qint64 p = lo; p < hi - 6; ++p) {
                const uchar rex = uchar(b[int(p)]);
                if (rex != 0x48 && rex != 0x4C && rex != 0x49 && rex != 0x4D)
                    continue;
                if (uchar(b[int(p) + 1]) != 0x8D || (uchar(b[int(p) + 2]) & 0xC7) != 0x05)
                    continue;
                const qint32 disp = qint32(le32(b, p + 3));
                const qint64 insnRva = text->va + ((p + 7) - textRaw);
                const qint64 target = insnRva + disp;
                if (target == verifyRva || target == failRva) {
                    found = true;
                    break;
                }
            }
            if (found) {
                site = c;
                how = QStringLiteral("validated");
                break;
            }
        }
        if (site < 0) {
            out.state = QStringLiteral("ambig");
            out.msg = QStringLiteral("multiple sites, none validated");
            return out;
        }
    }

    const bool already = uchar(b[int(site) + 2]) == 0x90 && uchar(b[int(site) + 3]) == 0x90;
    out.ok = true;
    out.state = already ? QStringLiteral("patched") : QStringLiteral("clean");
    out.site = site;
    out.how = how;
    out.bytes = b;
    out.msg = already ? QStringLiteral("Already patched (site %1)").arg(how)
                      : QStringLiteral("Clean install - patch site ready (%1)").arg(how);
    return out;
}

Result patch(const QString &exePath, bool dry)
{
    const Analysis r = analyze(exePath);
    if (r.state == QLatin1String("patched"))
        return { false, QStringLiteral("Already patched.") };
    if (r.state == QLatin1String("absent"))
        return { false, QStringLiteral("No tamper protection in this build.") };
    if (r.state != QLatin1String("clean"))
        return { false, QStringLiteral("Cannot patch: %1").arg(r.msg) };
    if (dry)
        return { true, QStringLiteral("Dry run - site at 0x%1 (no file written).")
                           .arg(r.site, 0, 16) };

    QByteArray b = r.bytes;
    ThemeEngine::backup(exePath);
    b[int(r.site) + 2] = char(0x90);
    b[int(r.site) + 3] = char(0x90);

    QFile out(exePath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return { false, QStringLiteral("Write error (run as administrator?).") };
    out.write(b);
    out.close();
    return { true, QStringLiteral("Tamper-check bypass patched successfully!") };
}

Result restore(const QString &exePath)
{
    const QString bak = exePath + QStringLiteral(".bak");
    if (!QFileInfo::exists(bak))
        return { false, QStringLiteral("No backup to restore.") };
    QFile::remove(exePath);
    if (!QFile::copy(bak, exePath))
        return { false, QStringLiteral("Restore failed.") };
    return { true, QStringLiteral("Restored HD-Player.exe from backup.") };
}

} // namespace IntegrityPatcher
