#pragma once

#include "ThemeEngine.h" // Logger, backup

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

// SVG blobs embedded in the EXE: <svg ...>...</svg> (ports find_svgs /
// extract_svgs / replace_svg, plus the toggle recolor of BS_THEME_CHANGER's
// PatchToggleSvgs).
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

// ---- toggle / switch recolor -----------------------------------------

// Hex colors used inside one blob, in document order, duplicates removed.
QStringList colorsIn(const QByteArray &svg);

// First 60 characters of the blob with newlines collapsed, for a list view.
QString preview(const QByteArray &svg);

// Rewrite every fill/stroke/stop-color hex value in one SVG to `hex`.
// The byte length never changes (#RGB stays 3 digits, #RRGGBBAA keeps its
// alpha) so the blob can be written back in place - a Qt resource blob has no
// room to grow.  keepKnob leaves pure black and pure white alone, which is what
// the moving knob of a switch usually is; recoloring it would make it invisible
// against the track.
QByteArray recolor(const QByteArray &svg, const QString &hex, bool keepKnob, int *countOut);

// Best guess at which blobs are the switch off/on assets: content keywords
// ("toggle", "switch", "checked", rect+circle) plus whether the blob carries a
// saturated color (the "on" state is accent-colored, "off" is gray).
struct ToggleGuess
{
    int off = -1;
    int on = -1;
};
ToggleGuess guessToggles(const QList<SvgBlob> &svgs);

struct RecolorResult
{
    bool ok = false;
    QString msg;
    int offColors = 0;
    int onColors = 0;
};

// Recolor the switch SVGs in one pass: read, rewrite both blobs, back up,
// write.  An index of -1 (or an empty hex) leaves that state untouched, so
// "off only" and "on only" work too.
RecolorResult recolorToggles(const QString &exePath, int offIndex, int onIndex,
                             const QString &offHex, const QString &onHex,
                             ThemeEngine::Logger logger = {});

} // namespace SvgEngine
