#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

// SVG blobs embedded in the EXE: <svg ...>...</svg> (ports find_svgs /
// extract_svgs / replace_svg).
struct SvgBlob
{
    int index = 0;
    qint64 offset = 0;
    int size = 0;
    QByteArray data;
};

namespace SvgEngine {

QList<SvgBlob> findSvgs(const QByteArray &data);
QStringList extractSvgs(const QString &exePath, const QString &outDir, int *countOut = nullptr);

struct Result { bool ok = false; QString msg; };
Result replaceSvg(const QString &exePath, int index, const QString &newSvgPath);

} // namespace SvgEngine
