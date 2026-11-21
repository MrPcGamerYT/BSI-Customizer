#include "ZlibUtils.h"
#include "ByteUtils.h"
#include "settings_fixture.h"
#include <QCoreApplication>
#include <QThread>
#include <cstdio>
#include <cstdlib>

namespace {
class Worker final : public QThread {
public:
    QByteArray input, output;
    void run() override { output = ZlibUtils::compressForCapacity(input, 0); }
};
void require(bool ok, const char *message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QByteArray text = ShaderSettingsFixture::player().toUtf8();
    const QByteArray ordinary = qCompress(text, 9);
    require(ZlibUtils::compressForCapacity(text, ordinary.size()) == ordinary,
            "an ordinary fitting payload must remain byte-identical");
    require(ZlibUtils::compressForCapacity(text, -1) == ordinary,
            "an unknown budget must not request extra compression work");
    const QByteArray tight = ZlibUtils::compressForCapacity(text, ordinary.size() - 1);
    require(tight.size() < ordinary.size() && qUncompress(tight) == text,
            "a bounded oversized payload must get smaller without changing a source byte");
    require(ByteUtils::be32(tight, 0) == quint32(text.size()), "Qt decoded-size header must remain exact");
    require(ZlibUtils::compressForCapacity(text, ordinary.size() - 1) == tight,
            "the same bytes/encoding options must be deterministic");
    const QByteArray binary = QByteArray::fromHex("efbbbf") + text + QByteArray("\r\n\0\xff", 4);
    require(qUncompress(ZlibUtils::compressForCapacity(binary, 0)) == binary,
            "BOM, CRLF, binary zero and high bytes cannot be normalized");
    const QByteArray large(256 * 1024 + 1, 'a');
    require(ZlibUtils::compressForCapacity(large, 0) == qCompress(large, 9),
            "the 256 KiB fallback input limit must be enforced");
    require(ZlibUtils::compressForCapacity({}, 0) == qCompress(QByteArray{}, 9),
            "empty input keeps the ordinary Qt behavior");
    Worker workers[4];
    for (auto &worker : workers) { worker.input = binary; worker.start(); }
    for (auto &worker : workers) {
        require(worker.wait(30000) && worker.output == ZlibUtils::compressForCapacity(binary, 0),
                "concurrent encoder calls must retain independent TLS allocation/cleanup contexts");
    }
    std::puts("PASS: bounded lossless Qt-resource compression, unchanged normal fast path, exact BOM/binary source bytes, deterministic output and fixed input cap");
    return 0;
}
