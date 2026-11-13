#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QVariantList>

// FPS patcher — unlocks the high-FPS slider by detouring
// QQuickSlider::setTo / QQuickRangeSlider::setTo in Qt5QuickTemplates2.dll or Qt6QuickTemplates2.dll.
// Only 240.0 (the high-frame-rate maximum) is replaced. Normal mode
// (60.0) and all other values pass through the original setter unchanged.
namespace FpsPatcher {

struct Target
{
    QString name;   // mangled export name
    QString cls;    // QQuickSlider / QQuickRangeSlider
    quint32 entryRva = 0; // exported entry (may be a thunk or ENDBR64 entry)
    quint32 rva = 0;      // actual detour site
    qint64 off = 0; // file offset
    QString state;  // "clean" | "patched" | "unknown"
    qint64 slotOff = -1; // verified detour; used when removing an existing patch
    int slotSize = 0;    // 64 = current, 48 = previous max-only, 80 = legacy high/normal
    QByteArray originalBytes; // complete instructions, not an assumed five-byte prologue
    QByteArray entryBytes;    // read-only diagnostic bytes from the detour site
    QString detail;
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

bool isSupportedPath(const QString &dllPath);
// Pick the Qt major used by HD-Player when both versions are installed.
// Returns empty when the corresponding QuickTemplates2 DLL is missing.
QString findDll(const QString &installDir);
struct DllDetection {
    bool ok = false;
    int qtMajor = 0;
    // 32 or 64 for the selected HD-Player.exe, 0 when it is not a readable PE.
    // Detection is bitness-neutral so that older x86 installations are still
    // recognised; only the slider patch itself requires a 64-bit Qt DLL.
    int playerBits = 0;
    QString path;
    QString msg;
};
DllDetection detectDll(const QString &playerExe);
Analysis analyze(const QByteArray &dll);
Result patch(const QString &dllPath, double hi);
Result restore(const QString &dllPath);

} // namespace FpsPatcher
