#include "ShaderSettingsImage.h"
#include "ShaderSettingsSource.h"
#include "QtResources.h"
#include "PeUtils.h"
#include "ByteUtils.h"
#include "ZlibUtils.h"
#include "settings_image_fixture.h"
#include "native_ui_resources.h"
#include <QCoreApplication>
#include <QFile>
#include <QMap>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

using namespace ByteUtils;
namespace {
void require(bool condition, const QString &message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", qPrintable(message)); std::exit(1); }
}
using ShaderSettingsImageFixture::fixture;
QMap<QString, QByteArray> sources(const QByteArray &image)
{
    QMap<QString, QByteArray> out;
    for (const auto &file : QtResources::registeredFiles(image))
        out.insert(file.path.isEmpty() ? file.name : file.path, QtResources::readFile(image, file));
    return out;
}
QByteArray read(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly) && file.size() < 256 * 1024 * 1024, QStringLiteral("read reference %1").arg(path));
    return file.readAll();
}
bool sameImports(const QByteArray &a, const QByteArray &b)
{
    const auto first = PeUtils::importedModules(a), second = PeUtils::importedModules(b);
    // Compare every module in order, including size and parse success. Qt
    // 5.15.2's QList::operator== needs stdext helpers removed by modern MSVC.
    return first.ok && second.ok && first.modules.size() == second.modules.size()
        && std::equal(first.modules.cbegin(), first.modules.cend(), second.modules.cbegin());
}
void cycle(const QByteArray &base)
{
    const auto before = sources(base);
    const auto installed = ShaderSettingsImage::prepare(base, true);
    require(installed.ok && installed.changed && installed.installed && installed.tabIndex == 12 && installed.output.size() == base.size(), QStringLiteral("bounded image plan: ") + installed.error);
    const auto repeated = ShaderSettingsImage::prepare(installed.output, true);
    require(repeated.ok && !repeated.changed && repeated.output == installed.output, QStringLiteral("image idempotence, without timestamp churn: ") + repeated.error);
    const auto restored = ShaderSettingsImage::prepare(installed.output, false);
    require(restored.ok && restored.changed && !restored.installed && restored.output.size() == base.size(), QStringLiteral("bounded image removal: ") + restored.error);
    require(sources(restored.output) == before, QStringLiteral("every original source/resource byte survives the add/remove cycle"));
    require(sameImports(installed.output, base), QStringLiteral("no startup/system-proxy DLL import may be added"));
    const auto after = sources(installed.output);
    for (auto it = before.cbegin(); it != before.cend(); ++it)
        if (!it.key().endsWith(QLatin1String("plr.qml"), Qt::CaseInsensitive) && !it.key().endsWith(QLatin1String("UiSettingsWindow.qml"), Qt::CaseInsensitive))
            require(after.value(it.key()) == it.value(), QStringLiteral("unrelated resource changed: ") + it.key());
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() == 4 && app.arguments().at(1) == QLatin1String("--export-native-ui"))
        return NativeUiResources::exportReadOnly(app.arguments().at(2), app.arguments().at(3));
    const auto base = fixture();
    require(QtResources::registeredFiles(base).size() == 15 + ShaderSettingsImageFixture::nativeControlNames().size(),
            QStringLiteral("fixture must be a real registered resource tree, including stock-control dependencies"));
    {
        const auto missing = fixture(false, false);
        const auto refused = ShaderSettingsImage::prepare(missing, true);
        require(!refused.ok && refused.output.isEmpty() && refused.error.contains(QStringLiteral("qrc:/UiThemeObject.qml")),
                QStringLiteral("missing native control resources cannot install a broken or imitation page: ") + refused.error);
        const auto absentRemoval = ShaderSettingsImage::prepare(missing, false);
        require(absentRemoval.ok && absentRemoval.output == missing && !absentRemoval.changed,
                QStringLiteral("missing native controls do not block safe no-op removal"));
        for (const auto &version : {QStringLiteral("17"), QStringLiteral("18"), QStringLiteral("19"), QStringLiteral("20")}) {
            auto unsafe = base;
            auto oldWindow = ShaderSettingsFixture::window();
            oldWindow.replace(QStringLiteral("//BSI v17"), QStringLiteral("//BSI v") + version);
            QString error;
            require(ShaderSettingsImage::replaceSource(unsafe, QStringLiteral("UiSettingsWindow.qml"), oldWindow.toUtf8(), &error),
                    QStringLiteral("stage an old material fixture: ") + error);
            const auto blocked = ShaderSettingsImage::prepare(unsafe, true);
            require(!blocked.ok && blocked.output.isEmpty() && blocked.error.contains(QStringLiteral("Update the rebuilt Settings panel")),
                    QStringLiteral("a module-only update must refuse unsafe older shell decoration: ") + version + blocked.error);
            // Older owned overlays must still be removable; requiring v21 to
            // remove them would strand the very installations being repaired.
            const auto old = ShaderSettingsSource::prepare(ShaderSettingsFixture::player(), oldWindow, ShaderSettingsFixture::pageCounts());
            require(old.ok, QStringLiteral("build historical owned source overlay: ") + old.error);
            require(ShaderSettingsImage::replaceSource(unsafe, QStringLiteral("plr.qml"), old.player.toUtf8(), &error)
                        && ShaderSettingsImage::replaceSource(unsafe, QStringLiteral("UiSettingsWindow.qml"), old.window.toUtf8(), &error),
                    QStringLiteral("stage historical installed integration: ") + error);
            const auto removed = ShaderSettingsImage::prepare(unsafe, false);
            require(removed.ok && removed.changed && !removed.installed
                        && sources(removed.output).value(QStringLiteral("UiSettingsWindow.qml")) == oldWindow.toUtf8(),
                    QStringLiteral("exact removal remains available on older Settings designs: ") + removed.error);
        }
    }
    for (const auto &control : {QStringLiteral("UiMessageDialog.qml"), QStringLiteral("UiDialog.qml"),
                                 QStringLiteral("UiShadowContainer.qml"), QStringLiteral("UiDropShadowEffect.qml"),
                                 QStringLiteral("UiImageButton.qml")}) {
        const auto missingDialog = fixture(false, true, control);
        const auto blocked = ShaderSettingsImage::prepare(missingDialog, true);
        require(!blocked.ok && blocked.output.isEmpty() && blocked.error.contains(QStringLiteral("qrc:/") + control),
                QStringLiteral("missing stock restart-dialog dependency is refused before any installation, never substituted: ") + blocked.error);
        const auto removal = ShaderSettingsImage::prepare(missingDialog, false);
        require(removal.ok && removal.output == missingDialog && !removal.changed,
                QStringLiteral("a missing stock dialog must not block safe no-op removal"));
    }
    const auto absent = ShaderSettingsImage::prepare(base, false);
    require(absent.ok && !absent.changed && !absent.installed && absent.output == base && absent.sources.isEmpty(),
            QStringLiteral("removing absent shaders is a pure no-op, not a native source/cache rewrite: ") + absent.error);
    cycle(base);
    const auto dense = fixture(true);
    {
        QtResources::File root;
        for (const auto &file : QtResources::registeredFiles(dense)) if (file.name == QLatin1String("plr.qml")) root = file;
        QString error;
        const auto budget = QtResources::inPlaceCapacity(dense, root.entryOff, root.name, &error);
        const auto replacement = ShaderSettingsFixture::player().toUtf8() + QByteArray("\n//A\n");
        require(root.entryOff >= 0 && qCompress(replacement, 9).size() > budget
                    && ZlibUtils::compressForCapacity(replacement, budget).size() <= budget,
                QStringLiteral("fixture must genuinely need tighter lossless encoding in its fixed slot"));
        auto rewritten = dense;
        require(ShaderSettingsImage::replaceSource(rewritten, root.name, replacement, &error)
                    && rewritten.size() == dense.size(), QStringLiteral("no-cave fixed-slot fallback must write/read back without growth: ") + error);
        const auto link = QtResources::findLink(rewritten, root.entryOff, root.name);
        require(link.found && !link.ambiguous && QtResources::readFile(rewritten, root) == replacement,
                QStringLiteral("tighter payload must stay in the original mapped record"));
        auto wanted = sources(dense);
        wanted[root.path.isEmpty() ? root.name : root.path] = replacement;
        require(sources(rewritten) == wanted, QStringLiteral("tighter encoding cannot change any unrelated registered resource"));
    }
    const auto refused = ShaderSettingsImage::prepare(dense, true);
    require(!refused.ok && !refused.changed && !refused.installed && refused.output.isEmpty()
                && refused.error.contains(QStringLiteral("Growing the EXE is forbidden")),
            QStringLiteral("no-slot/no-cave image must fail without growth or a false installed status: ") + refused.error);
    auto x86 = base; putLe16(x86, 0x84, 0x14c); putLe16(x86, 0x98, 0x10b);
    require(!ShaderSettingsImage::prepare(x86, true).ok, QStringLiteral("unsupported x86 cannot receive an x64 plugin plan"));
    auto unknown = base; unknown.replace(0x1200, 11, "NotQt00.dll");
    require(!ShaderSettingsImage::prepare(unknown, true).ok, QStringLiteral("a Qt ABI cannot be guessed from arbitrary strings"));
    QString error;
    auto untouched = base;
    require(!ShaderSettingsImage::replaceSource(untouched, QStringLiteral("Missing.qml"), QByteArray("Item{}"), &error) && untouched == base, QStringLiteral("missing-resource plan is atomic in memory"));
    auto orphan = base;
    require(ShaderSettingsImage::replaceSource(orphan, QStringLiteral("plr.qml"),
                ShaderSettingsFixture::player().toUtf8() + QByteArray("\n/*BSI-SHADERS-UNKNOWN:1*/"), &error),
            QStringLiteral("stage orphan-fragment fixture: ") + error);
    const auto unknownRemoval = ShaderSettingsImage::prepare(orphan, false);
    require(!unknownRemoval.ok && unknownRemoval.output.isEmpty(), QStringLiteral("unknown orphan shader fragments cannot report an already-removed success"));
    auto partial = base;
    const auto installed = ShaderSettingsImage::prepare(base, true);
    require(installed.ok && ShaderSettingsImage::replaceSource(partial, QStringLiteral("plr.qml"), installed.sources.first().after, &error),
            QStringLiteral("stage partial-install fixture: ") + error);
    require(!ShaderSettingsImage::prepare(partial, true).ok && !ShaderSettingsImage::prepare(partial, false).ok,
            QStringLiteral("partial source integration cannot install or remove without a complete owned pair"));
    auto qt5 = base;
    qt5.replace(0x1200, 11, QByteArray("Qt5Core.dll"));
    const auto qt5Plan = ShaderSettingsImage::prepare(qt5, true);
    require(qt5Plan.ok && qt5Plan.qtMajor == 5 && sameImports(qt5Plan.output, qt5),
            QStringLiteral("the actual Qt5 import selects only a Qt5 package plan, without changing imports: ") + qt5Plan.error);
    auto bom = base;
    const auto prefix = QByteArray::fromHex("efbbbf");
    require(ShaderSettingsImage::replaceSource(bom, QStringLiteral("plr.qml"), prefix + ShaderSettingsFixture::player().toUtf8(), &error),
            QStringLiteral("stage native UTF-8 BOM fixture: ") + error);
    cycle(bom);
    if (app.arguments().size() > 2) {
        const auto window = read(app.arguments().at(1));
        for (int i = 2; i < app.arguments().size(); ++i) {
            const auto original = read(app.arguments().at(i));
            auto prepared = original;
            require(ShaderSettingsImage::replaceSource(prepared, QStringLiteral("UiSettingsWindow.qml"), window, &error), QStringLiteral("stage reference window without writing the EXE: ") + error);
            cycle(prepared);
            require(read(app.arguments().at(i)) == original, QStringLiteral("read-only reference EXE changed"));
            std::printf("PASS: read-only %s native source/image add/remove plan; reference never executed or written\n", qPrintable(app.arguments().at(i)));
        }
    }
    std::puts("PASS: registered PE resource/source plans, actual Qt import identification, exact source/resource restoration, unchanged EXE size/imports/unrelated pages, idempotence and atomic refusal of ambiguous/unsupported/no-space images");
    std::puts("LIMIT: byte planning only; this is not an installed player, package transaction or game-frame/hardware result");
    return 0;
}
