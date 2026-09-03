#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QVariantList>

// FPS patcher — unlocks the high-FPS slider by detouring
// QQuickSlider::setTo / QQuickRangeSlider::setTo in libOpenglRender.dll.
// Exact port of the Python fps_find_targets / fps_patch / fps_restore.
namespace FpsPatcher {

struct Target
{
    QString name;   // mangled export name
    QString cls;    // QQuickSlider / QQuickRangeSlider
    quint32 rva = 0;
    qint64 off = 0; // file offset
    QString state;  // "clean" | "patched" | "unknown"
};

struct Analysis
{
    bool ok = false;
    QString state;      // "clean" | "patched" | "unsupported"
    QString msg;
    QList<Target> targets;
    qint64 caveOff = -1;
    quint32 caveRva = 0;
};

struct Result { bool ok = false; QString msg; };

Analysis analyze(const QByteArray &dll);
Result patch(const QString &dllPath, double hi);
Result restore(const QString &dllPath);

} // namespace FpsPatcher
