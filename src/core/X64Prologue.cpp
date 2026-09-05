#include "X64Prologue.h"

#include "ByteUtils.h"
#include "../third_party/hde64/hde64.h"

#include <cstring>
#include <limits>

namespace X64Prologue {
namespace {
bool allowedEntryInstruction(const hde64s &h)
{
    // Reject calls, jumps, returns, traps, immediate-address trampolines and
    // anything outside this small set of ordinary compiler entry operations.
    // In particular, successfully decoding arbitrary bytes is not enough.
    if ((h.flags & (F_ERROR | F_RELATIVE | F_IMM64)) || h.p_lock || h.p_67)
        return false; // HDE64's address-size override support is not suitable here
    const auto op = h.opcode;
    if ((op >= 0x50 && op <= 0x57) || op == 0x90) // push register / nop
        return true;
    if (op == 0x81 || op == 0x83) // sub/and rsp, immediate
        return h.rex_w && !h.rex_b && h.modrm_mod == 3 && h.modrm_rm == 4
               && (h.modrm_reg == 5 || h.modrm_reg == 4);
    if (op == 0x8b || op == 0x89 || op == 0x8a || op == 0x88 || op == 0x8d || op == 0x63
        || op == 0x31 || op == 0x33 || op == 0x84 || op == 0x85 || op == 0x39 || op == 0x3b)
        return true; // register/memory moves, lea, xor, test, cmp
    if (op == 0x0f) {
        switch (h.opcode2) {
        case 0x1f: // multi-byte nop
        case 0x10: case 0x11: case 0x28: case 0x29: // scalar/SIMD moves
        case 0x2e: case 0x2f: // floating-point comparisons
        case 0x57: case 0x6f: case 0x7f: // xorps / aligned/unaligned moves
            return true;
        }
    }
    return false;
}
} // namespace

Decoded decode(const QByteArray &code)
{
    Decoded out;
#if defined(Q_PROCESSOR_X86_64)
    int offset = 0;
    while (offset < 5) {
        const int available = int(qMin<qsizetype>(15, code.size() - offset));
        if (available <= 0) {
            out.error = QStringLiteral("truncated entry instruction");
            return out;
        }
        // HDE has no length argument and may look past the current instruction.
        // Decode from a padded private buffer; never let it read beyond a DLL.
        unsigned char padded[64] = {};
        std::memcpy(padded, code.constData() + offset, size_t(available));
        hde64s decoded = {};
        hde64_disasm(padded, &decoded);
        if (!decoded.len || decoded.len > available || offset + decoded.len > MaxSize
            || !allowedEntryInstruction(decoded)) {
            out.error = QStringLiteral("unsupported entry instruction at +%1 (branch, detour, or unsupported encoding)")
                            .arg(offset);
            return out;
        }
        Instruction instruction { offset, decoded.len, -1 };
        if ((decoded.flags & F_MODRM) && decoded.modrm_mod == 0 && decoded.modrm_rm == 5) {
            // RIP-relative displacement precedes any immediate operand.
            const int immediateSize = ((decoded.flags & F_IMM8) ? 1 : 0)
                                      + ((decoded.flags & F_IMM16) ? 2 : 0)
                                      + ((decoded.flags & F_IMM32) ? 4 : 0);
            instruction.ripDisplacement = decoded.len - immediateSize - 4;
            if (!(decoded.flags & F_DISP32) || instruction.ripDisplacement < 1) {
                out.error = QStringLiteral("unsupported RIP-relative instruction");
                return out;
            }
        }
        out.instructions << instruction;
        offset += decoded.len;
    }
    out.bytes = code.left(offset);
#else
    Q_UNUSED(code);
    out.error = QStringLiteral("FPS instruction decoding requires an x64 build of BSI Customizer");
#endif
    return out;
}

bool relocate(const QByteArray &code, quint32 fromRva, quint32 toRva, QByteArray *relocated, QString *error)
{
    const auto decoded = decode(code);
    if (!decoded.ok() || decoded.bytes.size() != code.size()) {
        if (error)
            *error = decoded.error.isEmpty() ? QStringLiteral("entry does not end on a complete instruction") : decoded.error;
        return false;
    }
    QByteArray result = code;
    for (const auto &instruction : decoded.instructions) {
        if (instruction.ripDisplacement < 0)
            continue;
        const int displacementOffset = instruction.offset + instruction.ripDisplacement;
        const qint64 target = qint64(fromRva) + instruction.offset + instruction.size
                              + qint32(ByteUtils::le32(code, displacementOffset));
        if (target >= fromRva && target < qint64(fromRva) + code.size()) {
            if (error)
                *error = QStringLiteral("RIP-relative operand refers to the entry being overwritten");
            return false;
        }
        const qint64 displacement = target - (qint64(toRva) + instruction.offset + instruction.size);
        if (target < 0 || target > std::numeric_limits<quint32>::max()
            || displacement < std::numeric_limits<qint32>::min()
            || displacement > std::numeric_limits<qint32>::max()) {
            if (error)
                *error = QStringLiteral("RIP-relative operand is out of range for this code cave");
            return false;
        }
        ByteUtils::putLe32(result, displacementOffset, quint32(qint32(displacement)));
    }
    *relocated = result;
    return true;
}
} // namespace X64Prologue
