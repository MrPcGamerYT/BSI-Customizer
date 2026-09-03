#include "BootVideoPatcher.h"

#include "ByteUtils.h"
#include "ThemeEngine.h"
#include "ZlibUtils.h"

#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>

using namespace ByteUtils;

namespace BootVideoPatcher {

namespace {

QString bootKindOf(const QString &text)
{
    // Token-only matching: real builds ship the QML minified ("id:iAppwin"),
    // some builds pretty-printed ("id: iAppwin") — accept both.
    if (text.contains(QLatin1String("ApplicationWindow"))
        && text.contains(QLatin1String("iAppwin"))
        && text.contains(QLatin1String("iBootLoader"))
        && text.contains(QLatin1String("iBootProgressComponent")))
        return QStringLiteral("plr");
    if (text.contains(QLatin1String("iBootImage"))
        && text.contains(QLatin1String("bootBackgroundImage"))
        && text.contains(QLatin1String("fSetBootPromotion")))
        return QStringLiteral("boot");
    if (text.contains(QLatin1String("fSetBootPromotion")))
        return QStringLiteral("boot_stub");
    return {};
}

struct BootStream
{
    qint64 entry = 0;
    qint64 zoff = 0;
    quint32 payload = 0;
    quint32 usize = 0;
    QString text;
    QString kind;
};

QList<BootStream> findStreams(const QByteArray &data)
{
    QList<BootStream> streams;
    const QList<qint64> magics = ZlibUtils::findMagicOffsets(data);
    for (const qint64 pos : magics) {
        if (pos < 8 || !inRange(data, pos - 8, 8))
            continue;
        const quint32 payload = be32(data, pos - 8);
        const quint32 usize = be32(data, pos - 4);
        if (payload < 64 || payload > 5000000u || usize < 100 || usize > 5000000u)
            continue;
        if (pos + payload > data.size())
            continue;
        const QByteArray qb = ZlibUtils::tryDecompress(data.mid(pos, payload), qint64(usize));
        if (qb.isEmpty())
            continue;
        const QString text = QString::fromUtf8(qb);
        const QString kind = bootKindOf(text);
        if (!kind.isEmpty())
            streams << BootStream { pos - 8, pos, payload, usize, text, kind };
    }
    return streams;
}

} // namespace

Result patch(const QString &exePath)
{
    if (!QFileInfo::exists(exePath))
        return { false, QStringLiteral("HD-Player.exe not found.") };

    QFile f(exePath);
    if (!f.open(QIODevice::ReadOnly))
        return { false, QStringLiteral("Cannot read EXE.") };
    QByteArray data = f.readAll();
    f.close();

    const auto streams = findStreams(data);
    QMap<QString, BootStream> kinds;
    for (const BootStream &s : streams) {
        if (!kinds.contains(s.kind))
            kinds.insert(s.kind, s);
    }
    if (!kinds.contains(QLatin1String("plr")))
        return { false, QStringLiteral("plr.qml not found.") };
    if (!kinds.contains(QLatin1String("boot")))
        return { false, QStringLiteral("boot qml not found.") };

    const BootStream st = kinds.value(QLatin1String("boot"));
    QString text = st.text;

    // Swap the stock BootVideo.qml loader for our file-loader, hide the boot
    // image / grid / button, keep the loading text visible.
    static const QRegularExpression loaderRe(
        QStringLiteral("\\s*Loader\\{id:pv;[^\n]*BootVideo\\.qml[^\n]*\\}\\s*"));
    text.replace(loaderRe, QStringLiteral("\r\n"));

    const QString bootQmlUrl = QStringLiteral("file:///Promo.mp4");
    const QString loader = QStringLiteral("    Loader{id:pv;z:-1;anchors.fill:parent;source:\"%1\"}\r\n\r\n")
                               .arg(bootQmlUrl);

    const QString marker1 = QStringLiteral("    Text {\r\n        id: iBootLoadingText");
    const QString marker2 = QStringLiteral("    Text {\n        id: iBootLoadingText");
    int idx = text.indexOf(marker1);
    if (idx >= 0) {
        text.insert(idx, loader);
    } else {
        idx = text.indexOf(marker2);
        if (idx >= 0) {
            text.insert(idx, loader);
        } else {
            idx = text.indexOf(QStringLiteral("Item {"));
            if (idx >= 0) {
                text.replace(idx, 6, QStringLiteral("Item {\r\n") + loader);
            }
        }
    }

    text.replace(QStringLiteral("visible: uiBackend.oem === \"msi5\" ? false : true"),
                 QStringLiteral("visible: false"));
    text.replace(QStringLiteral("iBootImage.visible = true"),
                 QStringLiteral("iBootImage.visible = false"));
    text.replace(QStringLiteral("pBootImageButtonVisible = true"),
                 QStringLiteral("pBootImageButtonVisible = false"));
    text.replace(QStringLiteral("pBootGridVisible = true"),
                 QStringLiteral("pBootGridVisible = false"));
    text.replace(QStringLiteral("iBootLoadingText.visible = false"),
                 QStringLiteral("iBootLoadingText.visible = true"));

    const QByteArray textBytes = text.toUtf8();
    const QByteArray comp = ZlibUtils::compress(textBytes);
    if (comp.isEmpty())
        return { false, QStringLiteral("Compression failed.") };

    const quint32 newPayload = quint32(comp.size()) + 4;
    if (newPayload > st.payload)
        return { false, QStringLiteral("boot QML too large.") };

    putBe32(data, st.entry, newPayload);
    putBe32(data, st.entry + 4, quint32(textBytes.size()));
    data.replace(int(st.zoff), comp.size(), comp);

    ThemeEngine::backup(exePath);
    QFile out(exePath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return { false, QStringLiteral("Write error (run as administrator?).") };
    out.write(data);
    out.close();
    return { true, QStringLiteral("Boot video patched.") };
}

} // namespace BootVideoPatcher
