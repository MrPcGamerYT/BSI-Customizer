#include "LayoutPatcher.h"

#include "BsDetector.h"
#include "PatchFile.h"
#include "PeUtils.h"
#include "QmlText.h"
#include "QtResources.h"
#include "ThemeEngine.h"

#include <QFile>
#include <QMap>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace LayoutPatcher {

namespace {

const QString kSidebarToTitlebar = QStringLiteral("sidebar-to-titlebar");

// The stock shapes this tweak reads and the patched shapes it writes. Every
// edit is a value/name swap between the two (plus one inserted visibility
// member), so applying the tweak again with `enabled = false` restores the
// player byte-for-byte.
const QString kColorStock = QStringLiteral(
    "uiBackend.oem === \"msi5\" ? UiTheme.colors.primary70 : UiTheme.colors.primary80");
const QString kColorPatched = QStringLiteral("\"transparent\"");
const QString kVisibleStock = QStringLiteral("uiBackend.confGetInstanceBool(\"show_sidebar\")");
const QString kVisiblePatched = QStringLiteral("true");
const QString kInsetStock = QStringLiteral(
    "pBorderWidth + (sidebarVisible ? iAppwin.pSidebarWidth : 0)");
const QString kInsetPatched = QStringLiteral("pBorderWidth + 0");
const QString kHidden = QStringLiteral("\nvisible: false");

struct Edit
{
    int begin = 0;
    int length = 0;
    QString replacement;
};

void addEdit(QList<Edit> &edits, int begin, int length, const QString &replacement)
{
    edits << Edit { begin, length, replacement };
}

// Swap one member's value expression, refusing when the current text is
// neither the expected stock nor the expected patched form.
bool swapValue(const QString &qml, const QString &masked, int begin, int end, const QString &name,
               bool enabled, const QString &stock, const QString &patched, QList<Edit> &edits)
{
    QmlText::Binding binding;
    if (!QmlText::binding(masked, begin, end, name, &binding)) return false;
    const QString current = qml.mid(binding.valueBegin, binding.valueEnd - binding.valueBegin);
    const QString from = enabled ? stock : patched;
    const QString to = enabled ? patched : stock;
    if (current != from) return false;
    addEdit(edits, binding.valueBegin, binding.valueEnd - binding.valueBegin, to);
    return true;
}

// Swap a whole member (`name: value`) for different member text.
bool swapMember(const QString &qml, const QString &masked, int begin, int end, const QString &name,
                const QString &fromValue, const QString &toMember, QList<Edit> &edits)
{
    QmlText::Binding binding;
    if (!QmlText::binding(masked, begin, end, name, &binding)) return false;
    if (qml.mid(binding.valueBegin, binding.valueEnd - binding.valueBegin) != fromValue) return false;
    addEdit(edits, binding.nameBegin, binding.valueEnd - binding.nameBegin, toMember);
    return true;
}

// Swap only the member name (`onHeightChanged` <-> `onWidthChanged`).
bool swapName(const QString &masked, int begin, int end, const QString &from, const QString &to,
              QList<Edit> &edits)
{
    QmlText::Binding binding;
    if (!QmlText::binding(masked, begin, end, from, &binding)) return false;
    addEdit(edits, binding.nameBegin, binding.nameEnd - binding.nameBegin, to);
    return true;
}

// Span of `function <name>(...) { ... }` in the masked text: [bodyBegin, bodyEnd).
bool functionSpan(const QString &masked, const QString &name, int *bodyBegin, int *bodyEnd)
{
    const QString head = QStringLiteral("function ") + name;
    const int at = masked.indexOf(head);
    if (at < 0) return false;
    int open = masked.indexOf(QLatin1Char('{'), at + head.size());
    if (open < 0) return false;
    int depth = 0;
    for (int i = open; i < masked.size(); ++i) {
        if (masked.at(i) == QLatin1Char('{')) ++depth;
        else if (masked.at(i) == QLatin1Char('}') && --depth == 0) {
            *bodyBegin = open + 1;
            *bodyEnd = i;
            return true;
        }
    }
    return false;
}

struct Doc
{
    QtResources::File file;
    QtResources::Link link;
    QString qml;
    bool bom = false;
};

bool findDoc(const QByteArray &image, const QString &name, Doc *out, QString *error)
{
    const auto theme = ThemeEngine::findThemeZlib(image);
    auto files = QtResources::registeredFiles(image);
    if (theme.hasQrcNode)
        for (const auto &file : QtResources::filesInPool(image, theme.resource)) {
            bool present = false;
            for (const auto &old : files) if (old.entryOff == file.entryOff && old.name == file.name) present = true;
            if (!present) files << file;
        }
    for (const auto &file : files) {
        if (file.name != name) continue;
        const auto bytes = QtResources::readFile(image, file);
        if (bytes.isEmpty()) {
            if (error) *error = QStringLiteral("%1 could not be read; no file was written.").arg(name);
            return false;
        }
        const bool bom = bytes.startsWith(QByteArray::fromHex("efbbbf"));
        const QByteArray body = bom ? bytes.mid(3) : bytes;
        QString qml = QString::fromUtf8(body);
        if (qml.toUtf8() != body) {
            if (error) *error = QStringLiteral("%1 is not UTF-8; no file was written.").arg(name);
            return false;
        }
        const auto link = QtResources::findLink(image, file.entryOff, file.name);
        if (!link.found || link.ambiguous) {
            if (error) *error = QStringLiteral("The %1 resource mapping is ambiguous; no file was written.").arg(name);
            return false;
        }
        *out = Doc { file, link, qml, bom };
        return true;
    }
    if (error) *error = QStringLiteral("%1 is not embedded in this player.").arg(name);
    return false;
}

bool applyEdits(QString &qml, QList<Edit> edits)
{
    std::sort(edits.begin(), edits.end(), [](const Edit &a, const Edit &b) { return a.begin > b.begin; });
    int boundary = qml.size();
    for (const auto &edit : edits) {
        if (edit.begin < 0 || edit.begin + edit.length > boundary) return false;
        qml.replace(edit.begin, edit.length, edit.replacement);
        boundary = edit.begin;
    }
    return true;
}

// ---------------------------------------------------------------- plr.qml

// The sidebar instance in the player window: stock sits in the right-hand
// strip below the top bar; patched overlays the top bar full-width with a
// transparent background so its button row centers inside the title bar.
bool plrEdits(QString &qml, bool enabled, QList<Edit> &edits)
{
    const QString masked = QmlText::mask(qml);
    const auto instances = QmlText::objects(masked, QStringLiteral("PlrSidebar"));
    if (instances.size() != 1) return false;
    const auto &instance = instances.first();
    if (!swapValue(qml, masked, instance.open, instance.end, QStringLiteral("anchors.top"),
                   enabled, QStringLiteral("iTopbar.bottom"), QStringLiteral("iTopbar.top"), edits))
        return false;
    if (!swapValue(qml, masked, instance.open, instance.end, QStringLiteral("anchors.bottom"),
                   enabled, QStringLiteral("parent.bottom"), QStringLiteral("iTopbar.bottom"), edits))
        return false;
    if (!swapValue(qml, masked, instance.open, instance.end, QStringLiteral("anchors.right"),
                   enabled, QStringLiteral("parent.right"), QStringLiteral("iTopbar.right"), edits))
        return false;
    // `width: pSidebarWidth` becomes `anchors.left: iTopbar.left` so the
    // instance spans the whole title bar without a width/anchors conflict.
    if (enabled) {
        if (!swapMember(qml, masked, instance.open, instance.end, QStringLiteral("width"),
                        QStringLiteral("pSidebarWidth"), QStringLiteral("anchors.left: iTopbar.left"), edits))
            return false;
    } else {
        if (!swapMember(qml, masked, instance.open, instance.end, QStringLiteral("anchors.left"),
                        QStringLiteral("iTopbar.left"), QStringLiteral("width: pSidebarWidth"), edits))
            return false;
    }
    if (!swapValue(qml, masked, instance.open, instance.end, QStringLiteral("color"),
                   enabled, kColorStock, kColorPatched, edits))
        return false;
    if (!swapValue(qml, masked, instance.open, instance.end, QStringLiteral("visible"),
                   enabled, kVisibleStock, kVisiblePatched, edits))
        return false;
    // The Android frame must span the full window width now that no side strip
    // exists; fSetUiParams keeps receiving `sidebarVisible` from its callers.
    int bodyBegin = 0, bodyEnd = 0;
    if (!functionSpan(masked, QStringLiteral("fSetUiParams"), &bodyBegin, &bodyEnd)) return false;
    const QString from = enabled ? kInsetStock : kInsetPatched;
    const int at = qml.indexOf(from, bodyBegin);
    if (at < 0 || at + from.size() > bodyEnd) return false;
    addEdit(edits, at, from.size(), enabled ? kInsetPatched : kInsetStock);
    return true;
}

// --------------------------------------------------------- PlrSidebar.qml

bool sidebarEdits(QString &qml, bool enabled, QList<Edit> &edits)
{
    QString masked = QmlText::mask(qml);
    // The button column becomes a centered row: the same elements, the same
    // overflow logic, one dimension over. The vertical spacer's Layout.fillHeight
    // is inert in a row layout, so it costs no horizontal space.
    QmlText::Object column;
    bool found = false;
    for (const auto &object : QmlText::objects(masked, QStringLiteral("UiColumnLayout"))) {
        if (qml.mid(object.open, object.end - object.open).contains(QLatin1String("iKebabMenuBtn"))) {
            column = object;
            found = true;
            break;
        }
    }
    if (!found) {
        if (enabled) return false;
        for (const auto &object : QmlText::objects(masked, QStringLiteral("UiRowLayout"))) {
            if (qml.mid(object.open, object.end - object.open).contains(QLatin1String("iKebabMenuBtn"))) {
                column = object;
                found = true;
                break;
            }
        }
    }
    if (!found) return false;
    if (enabled) {
        addEdit(edits, column.nameBegin, int(QStringLiteral("UiColumnLayout").size()),
                QStringLiteral("UiRowLayout"));
        if (!swapMember(qml, masked, column.open, column.end, QStringLiteral("anchors.fill"),
                        QStringLiteral("parent"), QStringLiteral("anchors.centerIn: parent"), edits))
            return false;
    } else {
        addEdit(edits, column.nameBegin, int(QStringLiteral("UiRowLayout").size()),
                QStringLiteral("UiColumnLayout"));
        if (!swapMember(qml, masked, column.open, column.end, QStringLiteral("anchors.centerIn"),
                        QStringLiteral("parent"), QStringLiteral("anchors.fill: parent"), edits))
            return false;
    }
    // The overflow budget is measured in buttons of 32px; in a horizontal
    // strip that budget is the width, and the row re-lays out when it changes.
    const auto roots = QmlText::objects(masked, QStringLiteral("Rectangle"));
    QmlText::Object root;
    bool rooted = false;
    for (const auto &object : roots) {
        if (qml.mid(object.open, object.end - object.open).contains(QLatin1String("function fResetSidebar"))) {
            root = object;
            rooted = true;
            break;
        }
    }
    if (!rooted) return false;
    if (!swapName(masked, root.open, root.end,
                  enabled ? QStringLiteral("onHeightChanged") : QStringLiteral("onWidthChanged"),
                  enabled ? QStringLiteral("onWidthChanged") : QStringLiteral("onHeightChanged"),
                  edits))
        return false;
    int bodyBegin = 0, bodyEnd = 0;
    if (!functionSpan(masked, QStringLiteral("fResetSidebar"), &bodyBegin, &bodyEnd)) return false;
    const QString from = enabled ? QStringLiteral("iSidebar.height") : QStringLiteral("iSidebar.width");
    const QString to = enabled ? QStringLiteral("iSidebar.width") : QStringLiteral("iSidebar.height");
    int at = bodyBegin;
    while ((at = qml.indexOf(from, at)) >= 0 && at < bodyEnd) {
        addEdit(edits, at, from.size(), to);
        at += from.size();
    }
    return !edits.isEmpty();
}

// ---------------------------------------------------------- PlrTopbar.qml

bool topbarEdits(QString &qml, bool enabled, QList<Edit> &edits)
{
    const QString masked = QmlText::mask(qml);
    // The sidebar toggle has nothing left to toggle; hiding it keeps the
    // title bar honest. It is not part of the width-managed item lists, so a
    // plain visibility member sticks.
    QmlText::Object toggle;
    bool found = false;
    for (const auto &object : QmlText::objects(masked, QStringLiteral("UiImageButton"))) {
        if (qml.mid(object.open, object.end - object.open).contains(QLatin1String("id: iSidebarShowBtn"))) {
            toggle = object;
            found = true;
            break;
        }
    }
    if (!found) return false;
    if (enabled) {
        if (qml.mid(toggle.open, toggle.end - toggle.open).contains(QLatin1String("visible: false")))
            return false;
        addEdit(edits, toggle.open + 1, 0, kHidden);
    } else {
        const int at = qml.indexOf(kHidden, toggle.open);
        if (at < 0 || at + kHidden.size() > toggle.end) return false;
        addEdit(edits, at, kHidden.size(), QString());
    }
    // The title bar's own Back/Home/Recent live in pNonFixedTopbarItems and
    // are toggled imperatively; switching their element's pVisible off is the
    // one hook that survives the width manager. The moved sidebar row carries
    // its own copies centered, so the duplicates step aside.
    const QList<QPair<QString, QString>> duplicates {
        { QStringLiteral("iBackBtn.visible"), QStringLiteral("pVisible") },
        { QStringLiteral("iHomeBtn.visible"), QStringLiteral("pVisible") },
        { QStringLiteral("iRecentBtn.visible"), QStringLiteral("pVisible") },
    };
    QSet<QString> handled;
    for (const auto &object : QmlText::objects(masked, QStringLiteral("PlrTopbarElement"))) {
        const QString text = qml.mid(object.open, object.end - object.open);
        for (const auto &wanted : duplicates) {
            if (!text.contains(wanted.first) || handled.contains(wanted.first)) continue;
            QmlText::Binding visible;
            if (!QmlText::binding(masked, object.open, object.end, wanted.second, &visible)) return false;
            const QString current = qml.mid(visible.valueBegin, visible.valueEnd - visible.valueBegin);
            const QString from = enabled ? QStringLiteral("true") : QStringLiteral("false");
            if (current != from) return false;
            addEdit(edits, visible.valueBegin, visible.valueEnd - visible.valueBegin,
                    enabled ? QStringLiteral("false") : QStringLiteral("true"));
            handled.insert(wanted.first);
        }
    }
    return handled.size() == duplicates.size();
}

struct Shape { bool stock = false; bool patched = false; };

Shape inspect(const QByteArray &image)
{
    Shape shape;
    Doc plr, sidebar, topbar;
    if (!findDoc(image, QStringLiteral("plr.qml"), &plr, nullptr)
        || !findDoc(image, QStringLiteral("PlrSidebar.qml"), &sidebar, nullptr)
        || !findDoc(image, QStringLiteral("PlrTopbar.qml"), &topbar, nullptr))
        return shape;
    const auto instance = QmlText::objects(QmlText::mask(plr.qml), QStringLiteral("PlrSidebar"));
    if (instance.size() != 1) return shape;
    const auto &block = instance.first();
    const QString maskedPlr = QmlText::mask(plr.qml);
    QmlText::Binding top, color, width, left, visible;
    const bool haveTop = QmlText::binding(maskedPlr, block.open, block.end, QStringLiteral("anchors.top"), &top);
    const bool haveColor = QmlText::binding(maskedPlr, block.open, block.end, QStringLiteral("color"), &color);
    const bool haveVisible = QmlText::binding(maskedPlr, block.open, block.end, QStringLiteral("visible"), &visible);
    const auto value = [&](const QmlText::Binding &b) {
        return plr.qml.mid(b.valueBegin, b.valueEnd - b.valueBegin);
    };
    shape.stock = haveTop && haveColor && haveVisible
        && value(top) == QLatin1String("iTopbar.bottom")
        && value(color) == kColorStock
        && value(visible) == kVisibleStock
        && QmlText::binding(maskedPlr, block.open, block.end, QStringLiteral("width"), &width)
        && value(width) == QLatin1String("pSidebarWidth")
        && plr.qml.contains(kInsetStock);
    shape.patched = haveTop && haveColor && haveVisible
        && value(top) == QLatin1String("iTopbar.top")
        && value(color) == kColorPatched
        && value(visible) == kVisiblePatched
        && QmlText::binding(maskedPlr, block.open, block.end, QStringLiteral("anchors.left"), &left)
        && value(left) == QLatin1String("iTopbar.left")
        && plr.qml.contains(kInsetPatched);
    return shape;
}

} // namespace

QList<Tweak> tweaks()
{
    return {
        { kSidebarToTitlebar,
          QStringLiteral("Remove sidebar · buttons to the title bar (centered)"),
          QStringLiteral("Lays every sidebar button out as one centered row inside the title "
                         "bar and removes the side strip (latest HD-Player). The title bar's own "
                         "duplicate Back/Home/Recent buttons step aside so nothing shows twice. "
                         "Turn the option off and apply again to restore the stock layout.") },
    };
}

State analyze(const QByteArray &image, const QString &id)
{
    State state;
    if (id != kSidebarToTitlebar) {
        state.detail = QStringLiteral("Unknown layout tweak.");
        return state;
    }
    const Shape shape = inspect(image);
    if (shape.stock || shape.patched) {
        state.supported = true;
        state.applied = shape.patched;
        state.detail = shape.patched ? QStringLiteral("Applied.")
                                     : QStringLiteral("Ready.");
        return state;
    }
    state.detail = QStringLiteral("This player's sidebar/title-bar layout is not the expected "
                                  "latest HD-Player shape; the tweak would be unsafe to write.");
    return state;
}

Result apply(const QString &exePath, const QString &id, bool enabled)
{
    const auto refuse = [&](const QString &message) { return Result { false, message }; };
    if (id != kSidebarToTitlebar) return refuse(QStringLiteral("Unknown layout tweak."));
    if (BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe")))
        return refuse(QStringLiteral("Close all BlueStacks instances before changing the layout."));
    QFile file(exePath);
    if (!file.open(QIODevice::ReadOnly))
        return refuse(QStringLiteral("Cannot read the selected EXE."));
    const QByteArray original = file.readAll();
    file.close();
    const State before = analyze(original, id);
    if (!before.supported) return refuse(before.detail);
    if (before.applied == enabled)
        return { true, enabled ? QStringLiteral("This layout tweak is already applied.")
                              : QStringLiteral("The stock sidebar layout is already in place.") };

    struct Role { QString name; bool (*collect)(QString &, bool, QList<Edit> &); };
    const QList<Role> roles {
        { QStringLiteral("plr.qml"), &plrEdits },
        { QStringLiteral("PlrSidebar.qml"), &sidebarEdits },
        { QStringLiteral("PlrTopbar.qml"), &topbarEdits },
    };
    QByteArray changed = original;
    for (const auto &role : roles) {
        Doc doc;
        QString error;
        if (!findDoc(changed, role.name, &doc, &error)) return refuse(error);
        QList<Edit> edits;
        if (!role.collect(doc.qml, enabled, edits))
            return refuse(QStringLiteral("%1 does not match the expected layout; no file was written.")
                              .arg(role.name));
        if (!applyEdits(doc.qml, edits))
            return refuse(QStringLiteral("Layout edits in %1 overlap; no file was written.").arg(role.name));
        const QByteArray header = doc.bom ? QByteArray::fromHex("efbbbf") : QByteArray();
        const QByteArray source = header + doc.qml.toUtf8();
        if (!QtResources::replaceResource(changed, doc.file.entryOff, doc.link, source, &error))
            return refuse(error);
    }
    const State after = analyze(changed, id);
    if (!after.supported || after.applied != enabled)
        return refuse(QStringLiteral("The written layout did not pass resource/read-back "
                                     "verification; no file was written."));
    if (!ThemeEngine::backup(exePath))
        return refuse(QStringLiteral("Could not preserve the original backup; no EXE was written."));
    QString error;
    if (!PatchFile::writeVerified(exePath, original, changed, &error)) return refuse(error);
    const int caches = QtResources::clearPlayerQmlCache(exePath);
    QString msg = enabled
        ? QStringLiteral("Sidebar removed: its buttons now sit as one centered row in the title "
                         "bar. Restart BlueStacks.")
        : QStringLiteral("Stock sidebar layout restored. Restart BlueStacks.");
    if (caches > 0)
        msg += QStringLiteral(" Cleared the player QML disk cache so BlueStacks reloads the source.");
    return { true, msg };
}

} // namespace LayoutPatcher
