#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// PE parsing shared by the FPS patcher, integrity patcher and the QML
// theme relocation logic (ports of _pe_parse / _qmlfix_pe_sections).
struct PeSection
{
    QString name;
    quint32 va = 0;
    quint32 vsize = 0;
    quint32 rawPtr = 0;
    quint32 rawSize = 0;
    quint32 chars = 0;
};

namespace PeUtils {

int peHeaderOffset(const QByteArray &d);             // -1 if not a PE
QList<PeSection> sections(const QByteArray &d);      // empty if not a PE
bool isPe32Plus(const QByteArray &d);                // PE32+ (x64)
quint16 machine(const QByteArray &d);                // 0 if not a PE
quint32 exportRva(const QByteArray &d);              // 0 if none / not a PE
quint32 optionalHeaderSize(const QByteArray &d);

// RVA -> file offset using max(VSz, RawSz) span (FPS patcher semantics).
qint64 rvaToRaw(const QList<PeSection> &secs, quint32 rva);   // -1 if unmapped
// file offset -> RVA using [RawPtr, RawPtr+RawSize) (theme/integrity semantics).
qint64 rawToRva(const QList<PeSection> &secs, qint64 raw);    // -1 if unmapped
const PeSection *sectionForRaw(const QList<PeSection> &secs, qint64 raw);

} // namespace PeUtils
