#include "core/TitlebarDetails.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStringList>

#include <cstdio>
#include <stdexcept>

namespace {
void check(bool ok, const char *message)
{
    if (!ok) throw std::runtime_error(message);
}
QString read(const QString &path)
{
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "cannot open titlebar fixture");
    return QString::fromUtf8(file.readAll());
}
void write(const QString &path, const QString &text)
{
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "cannot write generated titlebar fixture");
    const auto bytes = text.toUtf8();
    check(file.write(bytes) == bytes.size(), "generated fixture was not completely written");
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try {
        check(argc >= 2, "usage: titlebar-details-test OUTPUT_DIR [NATIVE_PLRTOPBAR]");
        const QDir output(QString::fromLocal8Bit(argv[1]));
        check(QDir().mkpath(output.absolutePath()), "cannot create fixture output directory");
        const QString stock = read(QStringLiteral("tests/fixtures/titlebar-details.qml"));
        QString flat = stock;
        flat.replace(QStringLiteral("                RowLayout { // DETAILS ROW OPEN\n                    id: iVersionImgLayout\n"), QString());
        flat.replace(QStringLiteral("                } // DETAILS ROW CLOSE\n"), QString());
        flat.replace(QStringLiteral("iVersionImgLayout.visible = true"),
                     QStringLiteral("iVersionTxt.visible = true; iImageTxt.visible = true"));
        QString callback = stock;
        callback.replace(QStringLiteral("id: iVersionImgLayout"), QStringLiteral(
            "id: iVersionImgLayout\n                    onVisibleChanged: { fixture.callbackCount++ }"));
        QString formerlyBlank = stock;
        formerlyBlank.replace(QStringLiteral("text: plrBackend.version"), QStringLiteral("text: \"\""));
        const QList<QPair<QString, QString>> variants {
            { QStringLiteral("grouped"), stock }, { QStringLiteral("flat"), flat },
            { QStringLiteral("callback"), callback }, { QStringLiteral("formerly-blank"), formerlyBlank }
        };
        for (const auto &variant : variants) {
            QString error;
            const QString hidden = TitlebarDetails::hide(variant.second, &error);
            check(!hidden.isEmpty(), qPrintable(error));
            check(TitlebarDetails::hide(hidden, &error) == hidden, "title-details rewrite is not idempotent");
            const QString marker = QStringLiteral("    // SETTINGS PROTECTION:");
            check(hidden.mid(hidden.indexOf(marker)) == variant.second.mid(variant.second.indexOf(marker)),
                  "title-details rewrite altered the Settings subtree");
            check(hidden.contains(QStringLiteral("plrBackend.title")) && hidden.contains(QStringLiteral("\"N32\"")),
                  "title text/instance string was deleted instead of hiding only its owner");
            if (variant.first != QLatin1String("formerly-blank"))
                check(hidden.contains(QStringLiteral("plrBackend.version")), "version binding was erased");
            check(hidden.contains(QStringLiteral("onVisibleChanged:if(visible)visible=false"))
                      || hidden.contains(QStringLiteral("onVisibleChanged:{if(visible)visible=false;")),
                  "imperative native reload could re-show details");
            write(output.filePath(variant.first + QStringLiteral("-stock.qml")), variant.second);
            write(output.filePath(variant.first + QStringLiteral("-hidden.qml")), hidden);
        }
        QString error;
        QString missing = stock;
        missing.replace(QStringLiteral("id: iImageTxt"), QStringLiteral("id: anotherLabel"));
        check(TitlebarDetails::hide(missing, &error).isEmpty(), "partial shapes must be refused atomically");
        check(!error.isEmpty(), "unsupported shape needs an actionable diagnostic");
        QString ambiguous = stock;
        ambiguous.insert(ambiguous.lastIndexOf(QLatin1Char('}')), QStringLiteral("Text { id: iVersionTxt; text: 'do not touch' }"));
        check(TitlebarDetails::hide(ambiguous, &error).isEmpty(), "duplicate title ids must be refused");
        for (const QString &id : { QStringLiteral("iVersionImgLayout"), QStringLiteral("iTitleLayout") }) {
            QString duplicate = stock;
            duplicate.insert(duplicate.lastIndexOf(QLatin1Char('}')), QStringLiteral("Item { id: ") + id + QStringLiteral(" }"));
            check(TitlebarDetails::hide(duplicate, &error).isEmpty(), "ambiguous title/detail containers must be refused");
        }
        QString unrelatedChild = stock;
        unrelatedChild.replace(QStringLiteral("id: iVersionImgLayout"), QStringLiteral("id: iVersionImgLayout\n                    Text { text: 'Settings must survive' }"));
        check(TitlebarDetails::hide(unrelatedChild, &error).isEmpty(), "a details row containing another control must never be hidden");
        if (argc > 2) {
            const QString native = read(QString::fromLocal8Bit(argv[2]));
            const QString hidden = TitlebarDetails::hide(native, &error);
            check(!hidden.isEmpty(), qPrintable(error));
            check(TitlebarDetails::hide(hidden, &error) == hidden, "native source rewrite is not idempotent");
            check(hidden.contains(QStringLiteral("fOpenHamburgerPopup")), "native Settings/menu callback was lost");
            write(output.filePath(QStringLiteral("native-PlrTopbar-hidden.qml")), hidden);
        }
        std::puts("PASS: combined title details, grouped/flat/legacy-hidden/callback variants, idempotence, Settings subtree isolation and unsafe-shape refusal");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
