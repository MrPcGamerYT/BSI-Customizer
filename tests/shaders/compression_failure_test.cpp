#include "BoundedZopfli.h"
#include "ZlibUtils.h"
#include <QCoreApplication>
#include <cstdio>
#include <cstdlib>
namespace { int attempts = 0; }
// A disposable linker stub simulates the guard's clean failure result at the
// actual production C++ call site. Never linked into the application/encoder.
extern "C" int bsiBoundedZopfli(const unsigned char *, size_t, unsigned char *, size_t, size_t *size)
{
    ++attempts; *size = 0; return 0;
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QByteArray text("import QtQuick 2.15\nItem { property string name: 'bounded allocation failure' }\n");
    const auto ordinary = qCompress(text, 9);
    if (ZlibUtils::compressForCapacity(text, 0) != ordinary || attempts != 1
        || ZlibUtils::compressForCapacity(text, ordinary.size()) != ordinary || attempts != 1) {
        std::fputs("FAIL: a failed encoder must retain the ordinary bytes without falsely publishing a tighter result\n", stderr); return 1;
    }
    std::puts("PASS: real production compression call site safely falls back on a failed/OOM encoder and leaves capacity refusal to the resource planner");
    return 0;
}
