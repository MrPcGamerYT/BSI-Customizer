// A separate process for every load is intentional: QQmlEngine's in-memory
// cache must not hide the disk-cache behaviour seen when HD-Player restarts.
#include "core/ByteUtils.h"
#include "core/PeUtils.h"

#include <QColor>
#include <QCryptographicHash>
#include <QtQml/qqmlprivate.h>
#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QResource>

#include <cstdio>
#include <memory>

QT_BEGIN_NAMESPACE
Q_CORE_EXPORT bool qRegisterResourceData(int, const unsigned char *, const unsigned char *,
                                         const unsigned char *);
Q_CORE_EXPORT bool qUnregisterResourceData(int, const unsigned char *, const unsigned char *,
                                           const unsigned char *);
QT_END_NAMESPACE

namespace {
QQmlPrivate::CachedQmlUnit cachedUnit {};
const QQmlPrivate::CachedQmlUnit *lookupUnit(const QUrl &url)
{
    return url == QUrl(QStringLiteral("qrc:/UiThemeObject.qml")) && cachedUnit.qmlData
               ? &cachedUnit : nullptr;
}
} // namespace

// args: exe, --theme-runtime-test, fixture, resource-version, tree-RVA,
//       name-RVA, data-RVA, expected-primary100
int runThemeRuntimeTest(const QStringList &args)
{
    if (args.size() != 8)
        return 2;
    QFile file(args.at(2));
    if (!file.open(QIODevice::ReadOnly))
        return 2;
    const QByteArray pe = file.readAll();
    const auto sections = PeUtils::sections(pe);
    qint64 imageSize = 0;
    for (const auto &section : sections)
        imageSize = qMax(imageSize, qint64(section.va) + section.rawSize);
    if (imageSize <= 0 || imageSize > 16 * 1024 * 1024)
        return 2;
    QByteArray image(imageSize, '\0');
    for (const auto &section : sections) {
        if (!ByteUtils::inRange(pe, section.rawPtr, section.rawSize))
            return 2;
        image.replace(section.va, section.rawSize, pe.mid(section.rawPtr, section.rawSize));
    }
    const int version = args.at(3).toInt();
    if (version < 1 || version > 3)
        return 2;
    for (int i = 4; i <= 6; ++i) {
        bool valid = false;
        const qint64 offset = args.at(i).toLongLong(&valid);
        if (!valid || offset < 0 || offset + 22 > image.size())
            return 2;
    }
    const auto *tree = reinterpret_cast<const uchar *>(image.constData()) + args.at(4).toLongLong();
    const auto *names = reinterpret_cast<const uchar *>(image.constData()) + args.at(5).toLongLong();
    const auto *payloads = reinterpret_cast<const uchar *>(image.constData()) + args.at(6).toLongLong();
    if (!qRegisterResourceData(version, tree, names, payloads))
        return 2;
    // Register the fixture unit exactly as qmlcachegen's compiled-in loader
    // does. QFile::map cannot load an embedded .qmlc resource on every Qt/OS,
    // so a companion resource alone is not a faithful AOT regression test.
    QByteArray compiled = QResource(QStringLiteral(":/UiThemeObject.qmlc")).uncompressedData();
    if (compiled.size() >= 248) {
        const quint32 flags = ByteUtils::le32(compiled, 108);
        if (!(flags & 2)) {
            ByteUtils::putLe32(compiled, 108, flags | 2); // StaticData: owned here
            compiled.replace(76, 16, QCryptographicHash::hash(compiled.mid(92), QCryptographicHash::Md5));
        }
        cachedUnit.qmlData = reinterpret_cast<const QV4::CompiledData::Unit *>(compiled.constData());
        QQmlPrivate::RegisterQmlUnitCacheHook hook { 0, lookupUnit };
        QQmlPrivate::qmlregister(QQmlPrivate::QmlUnitCacheHookRegistration, &hook);
    }
    bool ok = false;
    {
        QResource resource(QStringLiteral(":/UiThemeObject.qml"));
        if (!resource.isValid()) {
            std::fprintf(stderr, "Qt could not resolve the theme resource\n");
        } else {
            QQmlEngine engine;
            QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/UiThemeObject.qml")));
            std::unique_ptr<QObject> object(component.create());
            if (!object) {
                std::fprintf(stderr, "%s\n", qPrintable(component.errorString()));
            } else {
                QObject *colors = object->property("colors").value<QObject *>();
                const QColor actual = colors ? colors->property("primary100").value<QColor>() : QColor();
                ok = actual == QColor(args.at(7));
                std::printf("Qt loaded primary100=%s (expected %s)\n",
                            qPrintable(actual.name()), qPrintable(args.at(7)));
            }
        }
    }
    qUnregisterResourceData(version, tree, names, payloads);
    cachedUnit.qmlData = nullptr;
    return ok ? 0 : 1;
}
