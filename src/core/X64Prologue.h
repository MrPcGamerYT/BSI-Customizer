#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// Restricted decoder for complete, relocatable x64 function-entry instructions.
// Never assume a detour's five bytes end on an instruction boundary.
namespace X64Prologue {
constexpr int MaxSize = 19; // four bytes before the last, at-most-15-byte instruction
struct Instruction
{
    int offset = 0;
    int size = 0;
    int ripDisplacement = -1; // offset within this instruction, or -1
};
struct Decoded
{
    QByteArray bytes;
    QList<Instruction> instructions;
    QString error;
    bool ok() const { return error.isEmpty() && bytes.size() >= 5; }
};
Decoded decode(const QByteArray &code);
bool relocate(const QByteArray &code, quint32 fromRva, quint32 toRva,
              QByteArray *relocated, QString *error = nullptr);
} // namespace X64Prologue
