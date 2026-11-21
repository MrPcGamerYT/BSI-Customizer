#pragma once
// Read-only resource adapter for an opt-in, pinned-player UI test. The test
// sources contain no OEM QML or artwork. Only a fresh temporary directory may
// receive resources. This exporter never executes an original EXE/DLL; a
// separate Qt fixture interprets only the exported QML control closure.
#include "QtResources.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QXmlStreamWriter>
#include <cstdio>

namespace NativeUiResources {
inline int exportReadOnly(const QString &reference, const QString &destination)
{
    const auto fail = [](const char *message) {
        std::fprintf(stderr, "FAIL: native UI read-only export: %s\n", message);
        return 1;
    };
    const QFileInfo input(reference), target(destination);
    if (!input.isFile() || input.isSymLink() || input.size() <= 0 || input.size() > 256LL * 1024 * 1024)
        return fail("a bounded regular, non-symlink reference EXE is required");
    if (target.exists() || target.isSymLink()) return fail("destination must not already exist");
    QFile file(input.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) return fail("cannot read reference");
    const auto image = file.readAll();
    if (image.size() != input.size()) return fail("incomplete reference read");
    QDir directory(target.absoluteFilePath());
    if (!directory.mkpath(QStringLiteral("."))) return fail("cannot create temporary destination");
    const auto hash = [](const QByteArray &bytes) {
        return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    };
    QSet<QString> paths;
    QJsonArray inventory;
    QByteArray qrc;
    QXmlStreamWriter xml(&qrc);
    xml.writeStartElement(QStringLiteral("RCC"));
    xml.writeStartElement(QStringLiteral("qresource"));
    xml.writeAttribute(QStringLiteral("prefix"), QStringLiteral("/"));
    qint64 total = 0;
    for (const auto &entry : QtResources::registeredFiles(image)) {
        const auto &path = entry.path;
        const bool ui = !path.contains(QLatin1Char('/')) && path.startsWith(QLatin1String("Ui"))
            && path.endsWith(QLatin1String(".qml"));
        const bool asset = path.startsWith(QLatin1String("qrcAssets/"))
            && (path.endsWith(QLatin1String(".svg")) || path.endsWith(QLatin1String(".png")));
        if (!ui && !asset) continue;
        if (paths.contains(path) || path.contains(QLatin1Char('\\')) || path.contains(QLatin1Char(':'))
            || QDir::cleanPath(path) != path || path.startsWith(QLatin1Char('/'))
            || path.startsWith(QLatin1String(".."))) return fail("ambiguous or unsafe resource path");
        const auto bytes = QtResources::readFile(image, entry, 5 * 1024 * 1024);
        total += bytes.size();
        if (bytes.isEmpty() || total > 32LL * 1024 * 1024) return fail("empty or oversized resource closure");
        const auto output = directory.absoluteFilePath(path);
        if (!QDir().mkpath(QFileInfo(output).absolutePath())) return fail("cannot create resource subdirectory");
        QFile resource(output);
        if (!resource.open(QIODevice::WriteOnly | QIODevice::NewOnly)
            || resource.write(bytes) != bytes.size()) return fail("cannot exclusively write temporary resource");
        resource.close();
        if (!resource.open(QIODevice::ReadOnly) || resource.readAll() != bytes) return fail("resource read-back mismatch");
        paths.insert(path);
        xml.writeStartElement(QStringLiteral("file"));
        xml.writeAttribute(QStringLiteral("alias"), path);
        xml.writeCharacters(output);
        xml.writeEndElement();
        inventory.append(QJsonObject{{QStringLiteral("path"), path},
                                     {QStringLiteral("bytes"), bytes.size()},
                                     {QStringLiteral("sha256"), hash(bytes)}});
    }
    xml.writeEndElement();
    xml.writeEndElement();
    for (const auto &required : {QStringLiteral("UiThemeObject.qml"), QStringLiteral("UiButton.qml"),
                                QStringLiteral("UiColumnLayout.qml"), QStringLiteral("UiRowLayout.qml"),
                                QStringLiteral("UiCheckBox.qml"), QStringLiteral("UiComboBox.qml"),
                                QStringLiteral("UiSlider.qml"), QStringLiteral("UiScrollView.qml"),
                                QStringLiteral("UiScrollBar.qml"), QStringLiteral("UiImage.qml"),
                                QStringLiteral("UiTextField.qml"), QStringLiteral("UiMessageDialog.qml"),
                                QStringLiteral("UiDialog.qml"), QStringLiteral("UiShadowContainer.qml"),
                                QStringLiteral("UiDropShadowEffect.qml"), QStringLiteral("UiImageButton.qml")})
        if (!paths.contains(required)) return fail("required stock control is absent from qrc root");
    QFile qrcFile(directory.filePath(QStringLiteral("native.qrc")));
    if (!qrcFile.open(QIODevice::WriteOnly | QIODevice::NewOnly) || qrcFile.write(qrc) != qrc.size())
        return fail("cannot write temporary resource manifest");
    QFile metadata(directory.filePath(QStringLiteral("inventory.json")));
    const auto json = QJsonDocument(QJsonObject{{QStringLiteral("reference"), input.fileName()},
                                               {QStringLiteral("referenceSha256"), hash(image)},
                                               {QStringLiteral("resources"), inventory}}).toJson();
    if (!metadata.open(QIODevice::WriteOnly | QIODevice::NewOnly) || metadata.write(json) != json.size())
        return fail("cannot write resource evidence");
    file.seek(0);
    if (file.readAll() != image) return fail("reference changed during read-only export");
    std::printf("PASS: read-only native UI resources from %s sha256=%s (%d resources, %lld bytes); original not executed or written\n",
                qPrintable(input.fileName()), qPrintable(hash(image)), int(paths.size()), static_cast<long long>(total));
    return 0;
}
} // namespace NativeUiResources
