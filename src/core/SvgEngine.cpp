#include "SvgEngine.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>

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

// ---------------------------------------------------------------------------
// Toggle / switch recolor (BS_THEME_CHANGER's PatchToggleSvgs, length safe)
// ---------------------------------------------------------------------------

namespace {

// Hex color literal inside SVG markup.  Latin-1 (never UTF-8) is used for every
// text round trip in here so the bytes that are not part of a color survive
// untouched: a resource blob has no room to grow or shrink.
const QRegularExpression &hexRx()
{
    static const QRegularExpression rx(
        QStringLiteral("((?:fill|stroke|stop-color|flood-color|color)\\s*[:=]\\s*[\"']?)"
                       "(#[0-9A-Fa-f]{8}|#[0-9A-Fa-f]{6}|#[0-9A-Fa-f]{3})(?![0-9A-Fa-f])"),
        QRegularExpression::CaseInsensitiveOption);
    return rx;
}

QColor parseSvgColor(const QString &src)
{
    if (src.size() == 9)
        return QColor(src.left(7)); // SVG is #RRGGBBAA, Qt is #AARRGGBB
    if (src.size() == 4)
        return QColor(QStringLiteral("#") + src.mid(1, 1) + src.mid(1, 1) + src.mid(2, 1)
                      + src.mid(2, 1) + src.mid(3, 1) + src.mid(3, 1));
    return QColor(src);
}

} // namespace

QStringList colorsIn(const QByteArray &svg)
{
    QStringList out;
    auto it = hexRx().globalMatch(QString::fromLatin1(svg));
    while (it.hasNext()) {
        const QString c = it.next().captured(2);
        if (!out.contains(c, Qt::CaseInsensitive))
            out << c;
    }
    return out;
}

QString preview(const QByteArray &svg)
{
    QString t = QString::fromLatin1(svg);
    t.replace(QLatin1Char('\n'), QLatin1Char(' '));
    t.replace(QLatin1Char('\r'), QLatin1Char(' '));
    t = t.simplified();
    return t.left(60);
}

QByteArray recolor(const QByteArray &svg, const QString &hex, bool keepKnob, int *countOut)
{
    if (countOut)
        *countOut = 0;
    const QColor target(hex);
    if (!target.isValid() || svg.isEmpty())
        return svg;

    const QString t6 = QStringLiteral("#%1%2%3")
                           .arg(target.red(), 2, 16, QLatin1Char('0'))
                           .arg(target.green(), 2, 16, QLatin1Char('0'))
                           .arg(target.blue(), 2, 16, QLatin1Char('0'))
                           .toUpper();

    const QString text = QString::fromLatin1(svg);
    QString out;
    out.reserve(text.size());
    qsizetype last = 0;
    int count = 0;

    auto it = hexRx().globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString src = m.captured(2);
        const QColor c = parseSvgColor(src);
        if (!c.isValid())
            continue;
        // The knob of a switch is usually pure white or black; recoloring it
        // would hide it against the track.
        const bool knob = (c.red() == 255 && c.green() == 255 && c.blue() == 255)
                          || (c.red() == 0 && c.green() == 0 && c.blue() == 0);
        if (keepKnob && knob)
            continue;

        QString rep;
        if (src.size() == 9)
            rep = t6 + src.right(2); // keep the SVG alpha pair
        else if (src.size() == 7)
            rep = t6;
        else
            rep = QStringLiteral("#") + t6.mid(1, 1) + t6.mid(3, 1) + t6.mid(5, 1);
        if (rep.compare(src, Qt::CaseInsensitive) == 0)
            continue; // already this color

        out += text.mid(last, m.capturedStart(2) - last);
        out += rep;
        last = m.capturedEnd(2);
        ++count;
    }
    out += text.mid(last);

    if (countOut)
        *countOut = count;
    const QByteArray bytes = out.toLatin1();
    return bytes.size() == svg.size() ? bytes : svg; // never change the blob size
}

ToggleGuess guessToggles(const QList<SvgBlob> &svgs)
{
    ToggleGuess g;
    int bestOff = -1;
    int bestOn = -1;
    for (const SvgBlob &b : svgs) {
        const QString t = QString::fromLatin1(b.data).toLower();
        int score = 0;
        if (t.contains(QLatin1String("toggle")))
            score += 3;
        if (t.contains(QLatin1String("switch")))
            score += 2;
        if (t.contains(QLatin1String("checked")))
            score += 1;
        if (t.contains(QLatin1String("<rect")) && t.contains(QLatin1String("<circle")))
            score += 1;
        if (b.size > 6000)
            score -= 2; // switches are tiny
        if (score <= 0)
            continue;

        // The "on" asset carries the accent color, the "off" asset is gray.
        bool saturated = false;
        const QStringList cols = colorsIn(b.data);
        for (const QString &c : cols) {
            const QColor col = parseSvgColor(c);
            if (!col.isValid())
                continue;
            const int mx = qMax(col.red(), qMax(col.green(), col.blue()));
            const int mn = qMin(col.red(), qMin(col.green(), col.blue()));
            if (mx - mn > 40) {
                saturated = true;
                break;
            }
        }
        if (saturated) {
            if (score > bestOn) {
                bestOn = score;
                g.on = b.index;
            }
        } else if (score > bestOff) {
            bestOff = score;
            g.off = b.index;
        }
    }
    return g;
}

RecolorResult recolorToggles(const QString &exePath, int offIndex, int onIndex,
                             const QString &offHex, const QString &onHex,
                             ThemeEngine::Logger logger)
{
    auto log = [&logger](const QString &m, const QString &c) {
        if (logger)
            logger(m, c);
    };

    const bool wantOff = offIndex >= 0 && !offHex.trimmed().isEmpty();
    const bool wantOn = onIndex >= 0 && !onHex.trimmed().isEmpty();
    if (!wantOff && !wantOn)
        return { false, QStringLiteral("Choose a toggle SVG and a color first."), 0, 0 };

    QFile file(exePath);
    if (!file.open(QIODevice::ReadOnly))
        return { false, QStringLiteral("Cannot read file."), 0, 0 };
    QByteArray data = file.readAll();
    file.close();

    const QList<SvgBlob> svgs = findSvgs(data);
    if ((wantOff && offIndex >= svgs.size()) || (wantOn && onIndex >= svgs.size()))
        return { false, QStringLiteral("Invalid SVG index - scan the SVGs again."), 0, 0 };

    struct Job
    {
        int index;
        QString hex;
        int count = 0;
    };
    QList<Job> jobs;
    if (wantOff)
        jobs << Job { offIndex, offHex.trimmed(), 0 };
    if (wantOn)
        jobs << Job { onIndex, onHex.trimmed(), 0 };
    // Highest offset first, so no replacement can move one still to do.
    std::sort(jobs.begin(), jobs.end(), [&svgs](const Job &a, const Job &b) {
        return svgs.at(a.index).offset > svgs.at(b.index).offset;
    });

    for (Job &j : jobs) {
        const SvgBlob &b = svgs.at(j.index);
        const QByteArray patched = recolor(b.data, j.hex, true, &j.count);
        if (j.count > 0 && patched.size() == b.size) {
            data.replace(int(b.offset), b.size, patched);
            log(QStringLiteral("SVG #%1: %2 color(s) -> %3").arg(b.index).arg(j.count).arg(j.hex),
                QStringLiteral("blue"));
        }
    }

    int offCount = 0;
    int onCount = 0;
    int total = 0;
    for (const Job &j : jobs) {
        total += j.count;
        if (wantOff && j.index == offIndex)
            offCount = j.count;
        if (wantOn && j.index == onIndex)
            onCount = j.count;
    }
    if (total == 0) {
        return { false,
                 QStringLiteral("Nothing to change - those SVG(s) already use these colors, or "
                                "they have no fill/stroke color to recolor."),
                 0, 0 };
    }

    if (!ThemeEngine::backup(exePath, logger))
        return { false, QStringLiteral("Could not create backup."), 0, 0 };
    QFile out(exePath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return { false,
                 QStringLiteral("Cannot write file. Close BlueStacks (HD-Player.exe) and run this "
                                "tool as administrator."),
                 0, 0 };
    out.write(data);
    out.close();

    return { true, QStringLiteral("Toggle SVG colors patched (%1 color(s)).").arg(total), offCount,
             onCount };
}

} // namespace SvgEngine
