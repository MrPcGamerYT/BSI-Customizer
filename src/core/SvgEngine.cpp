#include "SvgEngine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace SvgEngine {

QList<SvgBlob> findSvgs(const QByteArray &data)
{
    QList<SvgBlob> svgs;
    qsizetype pos = 0;
    while (true) {
        const qsizetype start = data.indexOf("<svg", pos);
        if (start < 0)
            break;
        qsizetype end = data.indexOf("</svg>", start);
        if (end < 0) {
            pos = start + 1;
            continue;
        }
        end += 6;
        SvgBlob b;
        b.index = svgs.size();
        b.offset = start;
        b.size = int(end - start);
        b.data = data.mid(start, int(end - start));
        svgs << b;
        pos = end;
    }
    return svgs;
}

QStringList extractSvgs(const QString &exePath, const QString &outDir, int *countOut)
{
    QStringList saved;
    QFile file(exePath);
    if (!file.open(QIODevice::ReadOnly))
        return saved;
    const QByteArray data = file.readAll();
    file.close();

    const auto svgs = findSvgs(data);
    QDir().mkpath(outDir);
    for (const SvgBlob &b : svgs) {
        const QString path = outDir + QStringLiteral("/svg_%1.svg")
                                       .arg(b.index, 3, 10, QLatin1Char('0'));
        QFile out(path);
        if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            out.write(b.data);
            out.close();
            saved << path;
        }
    }
    if (countOut)
        *countOut = saved.size();
    return saved;
}

Result replaceSvg(const QString &exePath, int index, const QString &newSvgPath)
{
    QFile nf(newSvgPath);
    if (!nf.open(QIODevice::ReadOnly))
        return { false, QStringLiteral("Cannot read the new SVG file.") };
    const QByteArray newData = nf.readAll();
    nf.close();

    QFile file(exePath);
    if (!file.open(QIODevice::ReadOnly))
        return { false, QStringLiteral("Cannot open EXE.") };
    QByteArray data = file.readAll();
    file.close();

    const auto svgs = findSvgs(data);
    if (index < 0 || index >= svgs.size())
        return { false, QStringLiteral("Invalid SVG index.") };

    const SvgBlob &sv = svgs.at(index);
    QByteArray payload = newData;
    if (payload.size() <= sv.size) {
        payload += QByteArray(sv.size - payload.size(), '\0'); // pad to old size
        data.replace(int(sv.offset), sv.size, payload);
    } else {
        // Larger SVGs grow the blob; safe here because these live in the
        // Qt resource data region which tolerates trailing shifts only if
        // nothing references later offsets by absolute position. The Python
        // tool does exactly this, so we keep the behavior.
        data.replace(int(sv.offset), sv.size, payload);
    }

    QFile out(exePath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return { false, QStringLiteral("Write error (run as administrator?).") };
    out.write(data);
    out.close();
    return { true, QStringLiteral("Replaced SVG #%1.").arg(index) };
}

} // namespace SvgEngine
