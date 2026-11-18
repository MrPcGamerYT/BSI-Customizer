#pragma once
#include "core/DllStartupImport.h"
#include <algorithm>
#include <cstdint>
#include <string>
namespace DllStartupFixture
{
using DllStartupImport::Bytes;
inline void put(Bytes &b, std::size_t o, std::uint64_t v, unsigned n = 4)
{
    for (unsigned i = 0; i < n; ++i)
        b.at(o + i) = std::uint8_t(v >> (8 * i));
}
inline Bytes image(bool dll = false)
{
    Bytes b(0xa00, 0);
    b[0] = 'M';
    b[1] = 'Z';
    put(b, 0x3c, 0x80);
    b[0x80] = 'P';
    b[0x81] = 'E';
    put(b, 0x84, 0x8664, 2);
    put(b, 0x86, 2, 2);
    put(b, 0x94, 0xf0, 2);
    put(b, 0x96, dll ? 0x2022 : 0x22, 2);
    const std::size_t o = 0x98;
    put(b, o, 0x20b, 2);
    put(b, o + 4, 0x200);
    put(b, o + 8, 0x400);
    put(b, o + 16, 0x1000);
    put(b, o + 24, dll ? 0x180000000ULL : 0x140000000ULL, 8);
    put(b, o + 32, 0x1000);
    put(b, o + 36, 0x200);
    put(b, o + 40, 6, 2);
    put(b, o + 48, 6, 2);
    put(b, o + 56, 0x3000);
    put(b, o + 60, 0x400);
    put(b, o + 68, 3, 2);
    put(b, o + 72, 0x100000, 8);
    put(b, o + 80, 0x1000, 8);
    put(b, o + 88, 0x100000, 8);
    put(b, o + 96, 0x1000, 8);
    put(b, o + 108, 16);
    for (std::size_t i = 0; i < 2; ++i) {
        const auto s = 0x188 + i * 40;
        const std::string name = i ? ".idata" : ".text";
        std::copy(name.begin(), name.end(), b.begin() + std::ptrdiff_t(s));
        put(b, s + 8, i ? 0x400 : 0x200);
        put(b, s + 12, i ? 0x2000 : 0x1000);
        put(b, s + 16, i ? 0x400 : 0x200);
        put(b, s + 20, i ? 0x600 : 0x400);
        put(b, s + 36, i ? 0xc0000040 : 0x60000020);
    }
    b[0x400] = 0xc3;
    if (!dll) {
        put(b, o + 120, 0x2000);
        put(b, o + 124, 40);
        put(b, 0x600, 0x2040);
        put(b, 0x60c, 0x20c0);
        put(b, 0x610, 0x2060);
        put(b, 0x640, 0x20e0, 8);
        put(b, 0x660, 0x20e0, 8);
        const std::string module = "KERNEL32.dll", function = "ExitProcess";
        std::copy(module.begin(), module.end(), b.begin() + 0x6c0);
        std::copy(function.begin(), function.end(), b.begin() + 0x6e2);
    }
    return b;
}
} // namespace DllStartupFixture
