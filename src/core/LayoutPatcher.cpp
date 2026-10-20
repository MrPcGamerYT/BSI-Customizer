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
#include <QStringList>

#include <algorithm>

namespace LayoutPatcher {

namespace {

const QString kSidebarToTitlebar = QStringLiteral("sidebar-to-titlebar");

// The stock shapes this tweak reads and the patched shapes it writes. Every
// edit is a value/name swap between the two (plus one inserted title-bar
// instance and one inserted visibility member), so applying the tweak again
// with `enabled = false` restores the player byte-for-byte.
const QString kColorStock = QStringLiteral(
    "uiBackend.oem === \"msi5\" ? UiTheme.colors.primary70 : UiTheme.colors.primary80");
const QString kColorPatched = QStringLiteral("\"transparent\"");
const QString kVisibleStock = QStringLiteral("uiBackend.confGetInstanceBool(\"show_sidebar\")");
const QString kInsetStock = QStringLiteral(
    "pBorderWidth + (sidebarVisible ? iAppwin.pSidebarWidth : 0)");
const QString kInsetPatched = QStringLiteral("pBorderWidth + 0");
const QString kHidden = QStringLiteral("\nvisible: false");
// Prefix used when the toggle already binds `visible` (a second member would
// be a duplicate binding). The original expression is kept verbatim inside the
// parentheses so restoring is an exact text unwrap.
const QString kHiddenPrefix = QStringLiteral("false && ");
const QString kOverlayId = QStringLiteral("iSidebarTitlebar");
const QString kOverlayMarker = QStringLiteral("// BSI CUSTOMIZER: sidebar buttons over the title bar");
// The old (5.x) player lays the sidebar out as a row-layout item; the new one
// anchors it to the right-hand strip. Both are zero-width after the patch so
// the game content can use the freed strip.
const QString kSidebarWidthName = QStringLiteral("width");
const QString kSidebarLayoutWidthName = QStringLiteral("Layout.preferredWidth");
// Hides the button strip inside the *original* (now zero-width) sidebar
// instance before it is mounted. The mounted sidebar has a real width.
const QString kRowHidden = QStringLiteral("\nvisible: iSidebar.width > 0");

// Which shape the selected player uses. The tweak writes the same title-bar
// overlay for both; only the width member of the sidebar instance differs.
enum class SidebarLayout { None, Anchored, RowItem };

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

QString lineEnding(const QString &qml)
{
    return qml.contains(QLatin1String("\r\n")) ? QStringLiteral("\r\n") : QStringLiteral("\n");
}

// Exact old overlay retained only for restoring/upgrading already-patched
// players. New patches must not create another live sidebar component.
QString legacyTitlebarOverlay(const QString &nl, bool settingsHandler)
{
    QString text = nl
        + QStringLiteral("        PlrSidebar {") + nl
        + QStringLiteral("            ") + kOverlayMarker + nl
        + QStringLiteral("            id: ") + kOverlayId + nl
        + QStringLiteral("            anchors.fill: parent") + nl
        + QStringLiteral("            color: ") + kColorPatched + nl
        + QStringLiteral("            visible: true");
    if (settingsHandler)
        text += nl + QStringLiteral("            onSidebarSettingClicked: fOpenSettingsWindow()");
    return text + nl + QStringLiteral("        }");
}

// Move the *existing* sidebar, rather than constructing another one. Backend
// Connections, popup owners and button animations must have exactly one owner.
// The mount runs after the whole player is constructed (the sidebar can be
// declared after the top bar). Clearing old anchors is essential on Qt 5 too.
QString titlebarOverlay(const QString &nl)
{
    const QStringList lines {
        QStringLiteral("        Item {"),
        QStringLiteral("            ") + kOverlayMarker,
        QStringLiteral("            id: ") + kOverlayId,
        QStringLiteral("            anchors.fill: parent"),
        QStringLiteral("            anchors.leftMargin: Math.min(144, parent.width / 4)"),
        QStringLiteral("            anchors.rightMargin: Math.min(144, parent.width / 4)"),
        QStringLiteral("            function fMountSidebar() {"),
        QStringLiteral("                iSidebar.anchors.top = undefined"),
        QStringLiteral("                iSidebar.anchors.bottom = undefined"),
        QStringLiteral("                iSidebar.anchors.left = undefined"),
        QStringLiteral("                iSidebar.anchors.right = undefined"),
        QStringLiteral("                iSidebar.anchors.centerIn = undefined"),
        QStringLiteral("                iSidebar.parent = iSidebarTitlebar"),
        QStringLiteral("                iSidebar.anchors.fill = iSidebarTitlebar"),
        QStringLiteral("                iSidebar.visible = true"),
        QStringLiteral("                Qt.callLater(function() { iSidebar.fResetSidebar() })"),
        QStringLiteral("            }"),
        QStringLiteral("            Component.onCompleted: Qt.callLater(fMountSidebar)"),
        QStringLiteral("        }")
    };
    return nl + lines.join(nl);
}

bool hasOverlay(const QString &qml)
{
    return qml.contains(kOverlayId);
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

bool directChild(const QString &masked, int parentOpen, int childBegin);

bool findTitlebarOverlay(const QString &qml, const QString &masked,
                         const QList<QmlText::Object> &instances, QmlText::Object *overlay)
{
    for (const auto &instance : instances) {
        QmlText::Binding id;
        if (QmlText::binding(masked, instance.open, instance.end, QStringLiteral("id"), &id)
            && directChild(masked, instance.open, id.nameBegin)
            && qml.mid(id.valueBegin, id.valueEnd - id.valueBegin) == kOverlayId) {
            *overlay = instance;
            return true;
        }
    }
    return false;
}

bool findOriginalSidebar(const QString &qml, const QList<QmlText::Object> &instances,
                         QmlText::Object *original)
{
    bool found = false;
    for (const auto &instance : instances) {
        if (qml.mid(instance.open, instance.end - instance.open).contains(kOverlayId)) continue;
        if (found) return false; // more than one stock instance: not our shape
        *original = instance;
        found = true;
    }
    return found;
}

// plr.qml: shrink the player-window sidebar to zero width and host its
// buttons in a single-owner host inside the title bar. Works for the anchored
// strip (5.21/5.22) and for the old row-layout item (5.10/5.12): only the
// member that carries the strip width changes.
bool plrEdits(QString &qml, bool enabled, SidebarLayout layout, QList<Edit> &edits)
{
    const QString masked = QmlText::mask(qml);
    const auto instances = QmlText::objects(masked, QStringLiteral("PlrSidebar"));
    const auto topbars = QmlText::objects(masked, QStringLiteral("PlrTopbar"));
    if (topbars.size() != 1) return false;

    const QString widthName = layout == SidebarLayout::RowItem ? kSidebarLayoutWidthName
                                                               : kSidebarWidthName;
    if (enabled) {
        if (instances.size() != 1) return false;
        const auto &instance = instances.first();
        if (!swapValue(qml, masked, instance.open, instance.end, widthName,
                       true, QStringLiteral("pSidebarWidth"), QStringLiteral("0"), edits))
            return false;
        if (!swapValue(qml, masked, instance.open, instance.end, QStringLiteral("color"),
                       true, kColorStock, kColorPatched, edits))
            return false;
        QmlText::Binding id;
        if (!QmlText::binding(masked, instance.open, instance.end, QStringLiteral("id"), &id)
            || qml.mid(id.valueBegin, id.valueEnd - id.valueBegin) != QLatin1String("iSidebar"))
            return false;
        const QString overlay = titlebarOverlay(lineEnding(qml));
        addEdit(edits, topbars.first().end - 1, 0, overlay);
    } else {
        if (instances.size() != 1 && instances.size() != 2) return false;
        QmlText::Object original;
        QmlText::Object overlayInstance;
        if (!findOriginalSidebar(qml, instances, &original)) return false;
        const bool legacy = instances.size() == 2;
        if (!findTitlebarOverlay(qml, masked,
                                legacy ? instances : QmlText::objects(masked, QStringLiteral("Item")),
                                &overlayInstance)) return false;
        const QString instanceText = qml.mid(original.open, original.end - original.open);
        const QString overlay = legacy
            ? legacyTitlebarOverlay(lineEnding(qml), instanceText.contains(QLatin1String("onSidebarSettingClicked")))
            : titlebarOverlay(lineEnding(qml));
        const int at = topbars.first().end - 1 - overlay.size();
        if (at < 0 || qml.mid(at, overlay.size()) != overlay) return false;
        addEdit(edits, at, overlay.size(), QString());
        if (!swapValue(qml, masked, original.open, original.end, widthName,
                       false, QStringLiteral("pSidebarWidth"), QStringLiteral("0"), edits))
            return false;
        if (!swapValue(qml, masked, original.open, original.end, QStringLiteral("color"),
                       false, kColorStock, kColorPatched, edits))
            return false;
    }

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

// True when `childBegin` is a direct member of the object opened at
// `parentOpen` (the only braces between them are balanced member blocks,
// e.g. a Component or a signal handler).
bool directChild(const QString &masked, int parentOpen, int childBegin)
{
    int depth = 0;
    for (int i = parentOpen + 1; i < childBegin; ++i) {
        const QChar c = masked.at(i);
        if (c == QLatin1Char('{')) ++depth;
        else if (c == QLatin1Char('}')) --depth;
    }
    return depth == 0;
}

bool sidebarEdits(QString &qml, bool enabled, QList<Edit> &edits)
{
    const QString masked = QmlText::mask(qml);
    // The component's root Rectangle owns fResetSidebar. Locate it first so
    // the button strip below can only be the root's direct child layout, not
    // some popup's column that happens to mention the kebab button.
    QmlText::Object root;
    bool rooted = false;
    for (const auto &object : QmlText::objects(masked, QStringLiteral("Rectangle"))) {
        if (qml.mid(object.open, object.end - object.open).contains(QLatin1String("function fResetSidebar"))) {
            root = object;
            rooted = true;
            break;
        }
    }
    if (!rooted) return false;

    // The button column becomes a centered row: the same elements, the same
    // overflow logic, one dimension over. The vertical spacer's Layout.fillHeight
    // is inert in a row layout, so it costs no horizontal space.
    QmlText::Object column;
    bool found = false;
    const auto pickColumn = [&](const QString &type) {
        for (const auto &object : QmlText::objects(masked, type)) {
            if (object.nameBegin <= root.open || object.end > root.end) continue;
            if (!directChild(masked, root.open, object.nameBegin)) continue;
            if (qml.mid(object.open, object.end - object.open).contains(QLatin1String("iKebabMenuBtn"))) {
                column = object;
                return true;
            }
        }
        return false;
    };
    found = pickColumn(QStringLiteral("UiColumnLayout"));
    if (!found && !enabled) found = pickColumn(QStringLiteral("UiRowLayout"));
    if (!found) return false;
    if (enabled) {
        addEdit(edits, column.nameBegin, int(QStringLiteral("UiColumnLayout").size()),
                QStringLiteral("UiRowLayout"));
        if (!swapMember(qml, masked, column.open, column.end, QStringLiteral("anchors.fill"),
                        QStringLiteral("parent"), QStringLiteral("anchors.centerIn: parent"), edits))
            return false;
        // Suppress the row before the original sidebar is mounted into its
        // title-bar host and receives the actual available width.
        addEdit(edits, column.open + 1, 0, kRowHidden);
    } else {
        addEdit(edits, column.nameBegin, int(QStringLiteral("UiRowLayout").size()),
                QStringLiteral("UiColumnLayout"));
        if (!swapMember(qml, masked, column.open, column.end, QStringLiteral("anchors.centerIn"),
                        QStringLiteral("parent"), QStringLiteral("anchors.fill: parent"), edits))
            return false;
        const int at = qml.indexOf(kRowHidden, column.open);
        if (at < 0 || at + kRowHidden.size() > column.end) return false;
        addEdit(edits, at, kRowHidden.size(), QString());
    }
    // The overflow budget is measured in buttons of 32px; in a horizontal
    // strip that budget is the width, and the row re-lays out when it changes.
    // Only the root's own handler may be renamed - a child object's
    // onHeightChanged must stay untouched.
    QmlText::Binding handler;
    const QString handlerFrom = enabled ? QStringLiteral("onHeightChanged")
                                        : QStringLiteral("onWidthChanged");
    if (!QmlText::binding(masked, root.open, root.end, handlerFrom, &handler)) return false;
    if (!directChild(masked, root.open, handler.nameBegin)) return false;
    addEdit(edits, handler.nameBegin, handler.nameEnd - handler.nameBegin,
            enabled ? QStringLiteral("onWidthChanged") : QStringLiteral("onHeightChanged"));
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
    // The sidebar toggle has nothing left to toggle, and leaving it live would
    // still resize the window by pSidebarWidth on click, so it is hidden.
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
    // Some builds already bind the toggle's visibility (5.10: `visible:
    // !iSidebar.visible`). A second `visible:` member would be a duplicate
    // binding, so wrap the existing expression in `false && (...)` instead and
    // unwrap it verbatim when restoring. Builds without one get a plain member.
    QmlText::Binding toggleVisible;
    const bool hasVisible =
        QmlText::binding(masked, toggle.open, toggle.end, QStringLiteral("visible"), &toggleVisible)
        && directChild(masked, toggle.open, toggleVisible.nameBegin);
    const auto toggleValue = [&] {
        return qml.mid(toggleVisible.valueBegin, toggleVisible.valueEnd - toggleVisible.valueBegin);
    };
    if (enabled) {
        if (hasVisible) {
            if (toggleValue().startsWith(kHiddenPrefix)) return false;
            addEdit(edits, toggleVisible.valueBegin, toggleVisible.valueEnd - toggleVisible.valueBegin,
                    kHiddenPrefix + QLatin1Char('(') + toggleValue() + QLatin1Char(')'));
        } else {
            addEdit(edits, toggle.open + 1, 0, kHidden);
        }
    } else if (qml.mid(toggle.open + 1, kHidden.size()) == kHidden) {
        addEdit(edits, toggle.open + 1, kHidden.size(), QString());
    } else if (hasVisible) {
        const QString current = toggleValue();
        if (!current.startsWith(kHiddenPrefix + QLatin1Char('('))
            || !current.endsWith(QLatin1Char(')')))
            return false;
        addEdit(edits, toggleVisible.valueBegin, toggleVisible.valueEnd - toggleVisible.valueBegin,
                current.mid(int(kHiddenPrefix.size()) + 1,
                            current.size() - int(kHiddenPrefix.size()) - 2));
    } else {
        return false;
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
            if (!QmlText::binding(masked, object.open, object.end, wanted.second, &visible)
                || !directChild(masked, object.open, visible.nameBegin))
                return false;
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

// Adapters so the write loop can treat the two non-plr collectors uniformly.
bool collectSidebarEdits(QString &qml, bool enabled, SidebarLayout, QList<Edit> &edits)
{
    return sidebarEdits(qml, enabled, edits);
}

bool collectTopbarEdits(QString &qml, bool enabled, SidebarLayout, QList<Edit> &edits)
{
    return topbarEdits(qml, enabled, edits);
}

// ------------------------------------------------------------- inspection

struct Shape
{
    SidebarLayout layout = SidebarLayout::None;
    bool stock = false;
    bool patched = false;
    bool legacyOverlay = false;
    bool editable = false;
    QString detail;
};

struct Snapshot
{
    Shape shape;
    Doc plr;
    Doc sidebar;
    Doc topbar;
};

Snapshot inspect(const QByteArray &image)
{
    Snapshot snapshot;
    QString error;
    if (!findDoc(image, QStringLiteral("plr.qml"), &snapshot.plr, &error)
        || !findDoc(image, QStringLiteral("PlrSidebar.qml"), &snapshot.sidebar, &error)
        || !findDoc(image, QStringLiteral("PlrTopbar.qml"), &snapshot.topbar, &error)) {
        snapshot.shape.detail = error;
        return snapshot;
    }

    const QString &qml = snapshot.plr.qml;
    const QString masked = QmlText::mask(qml);
    const auto instances = QmlText::objects(masked, QStringLiteral("PlrSidebar"));
    const auto topbars = QmlText::objects(masked, QStringLiteral("PlrTopbar"));
    if (topbars.size() != 1) {
        snapshot.shape.detail = QStringLiteral("plr.qml does not declare exactly one title bar; "
                                               "the tweak would be unsafe to write.");
        return snapshot;
    }

    const auto value = [&](const QmlText::Object &object, const QString &name, QString *out) {
        QmlText::Binding binding;
        if (!QmlText::binding(masked, object.open, object.end, name, &binding)) return false;
        *out = qml.mid(binding.valueBegin, binding.valueEnd - binding.valueBegin);
        return true;
    };

    const bool overlay = hasOverlay(qml);
    if (!overlay && instances.size() == 1) {
        // Stock: either the anchored side strip (5.21/5.22) or the old
        // row-layout item (5.10/5.12).
        const auto &instance = instances.first();
        QString color, visible, width, layoutWidth, top;
        const bool haveColor = value(instance, QStringLiteral("color"), &color);
        const bool haveVisible = value(instance, QStringLiteral("visible"), &visible);
        const bool haveWidth = value(instance, kSidebarWidthName, &width);
        const bool haveLayoutWidth = value(instance, kSidebarLayoutWidthName, &layoutWidth);
        const bool haveTop = value(instance, QStringLiteral("anchors.top"), &top);
        if (haveColor && color == kColorStock && haveVisible && visible == kVisibleStock
            && qml.contains(kInsetStock)) {
            if (haveWidth && width == QLatin1String("pSidebarWidth") && haveTop
                && top == QLatin1String("iTopbar.bottom")) {
                snapshot.shape.layout = SidebarLayout::Anchored;
                snapshot.shape.stock = true;
            } else if (haveLayoutWidth && layoutWidth == QLatin1String("pSidebarWidth")) {
                snapshot.shape.layout = SidebarLayout::RowItem;
                snapshot.shape.stock = true;
            }
        }
    } else if (overlay && (instances.size() == 1 || instances.size() == 2)) {
        // Recognize both the old duplicate and the new single-owner host.
        snapshot.shape.legacyOverlay = instances.size() == 2;
        QmlText::Object original;
        QmlText::Object overlayInstance;
        if (findOriginalSidebar(qml, instances, &original)
            && findTitlebarOverlay(qml, masked,
                                   snapshot.shape.legacyOverlay ? instances
                                       : QmlText::objects(masked, QStringLiteral("Item")),
                                   &overlayInstance)) {
            QString color, width, layoutWidth, fill, overlayColor, overlayVisible;
            const bool haveColor = value(original, QStringLiteral("color"), &color);
            const bool haveWidth = value(original, kSidebarWidthName, &width);
            const bool haveLayoutWidth = value(original, kSidebarLayoutWidthName, &layoutWidth);
            const bool haveFill = value(overlayInstance, QStringLiteral("anchors.fill"), &fill);
            const bool haveOverlayColor = value(overlayInstance, QStringLiteral("color"), &overlayColor);
            const bool haveOverlayVisible = value(overlayInstance, QStringLiteral("visible"), &overlayVisible);
            if (haveColor && color == kColorPatched && haveFill && fill == QLatin1String("parent")
                && (!snapshot.shape.legacyOverlay
                    || (haveOverlayColor && overlayColor == kColorPatched
                        && haveOverlayVisible && overlayVisible == QLatin1String("true")))
                && qml.contains(kInsetPatched)) {
                if (haveWidth && width == QLatin1String("0")) {
                    snapshot.shape.layout = SidebarLayout::Anchored;
                    snapshot.shape.patched = true;
                } else if (haveLayoutWidth && layoutWidth == QLatin1String("0")) {
                    snapshot.shape.layout = SidebarLayout::RowItem;
                    snapshot.shape.patched = true;
                }
            }
        }
    }

    if (snapshot.shape.layout == SidebarLayout::None) {
        snapshot.shape.detail = QStringLiteral(
            "This player's sidebar is not one of the supported shapes (anchored side strip or "
            "row-layout item); the tweak would be unsafe to write.");
        return snapshot;
    }

    // Prove the whole edit set can actually be built before offering the
    // tweak: the UI enables the checkbox/Apply from this result, so a player
    // whose title bar or sidebar component differs is refused here instead of
    // failing halfway through a write.
    const bool enabled = !snapshot.shape.patched;
    QList<Edit> plrPlan, sidebarPlan, topbarPlan;
    QString plr = snapshot.plr.qml;
    QString sidebar = snapshot.sidebar.qml;
    QString topbar = snapshot.topbar.qml;
    if (!plrEdits(plr, enabled, snapshot.shape.layout, plrPlan)) {
        snapshot.shape.detail = QStringLiteral(
            "plr.qml does not match the supported sidebar/title-bar layout; the tweak would be "
            "unsafe to write.");
        return snapshot;
    }
    if (!sidebarEdits(sidebar, enabled, sidebarPlan)) {
        snapshot.shape.detail = QStringLiteral(
            "PlrSidebar.qml does not match the supported button-column layout; the tweak would "
            "be unsafe to write.");
        return snapshot;
    }
    if (!topbarEdits(topbar, enabled, topbarPlan)) {
        snapshot.shape.detail = QStringLiteral(
            "PlrTopbar.qml does not match the supported title-bar layout; the tweak would be "
            "unsafe to write.");
        return snapshot;
    }
    if (!applyEdits(plr, plrPlan) || !applyEdits(sidebar, sidebarPlan)
        || !applyEdits(topbar, topbarPlan)) {
        snapshot.shape.detail = QStringLiteral(
            "The layout edits overlap in this player; no file would be written.");
        return snapshot;
    }
    snapshot.shape.editable = true;
    return snapshot;
}

} // namespace

QList<Tweak> tweaks()
{
    return {
        { kSidebarToTitlebar,
          QStringLiteral("Remove sidebar · buttons to the title bar (centered)"),
          QStringLiteral("Lays every sidebar button out as one centered row inside the title "
                         "bar and removes the side strip. Moves the original controls, keeping "
                         "popup and animation ownership intact. Works with both the current player "
                         "and the older 5.x layout. The title bar's own duplicate "
                         "Back/Home/Recent buttons step aside so nothing shows twice. "
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
    if (image.isEmpty()) {
        state.detail = QStringLiteral("Select the HD-Player.exe path first.");
        return state;
    }
    const Snapshot snapshot = inspect(image);
    if (snapshot.shape.layout == SidebarLayout::None || !snapshot.shape.editable) {
        state.detail = snapshot.shape.detail;
        return state;
    }
    state.supported = true;
    state.applied = snapshot.shape.patched;
    state.detail = snapshot.shape.legacyOverlay ? QStringLiteral("Older layout applied. Apply again to repair popup ownership.")
                  : state.applied ? QStringLiteral("Applied.")
                                 : QStringLiteral("Ready.");
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
    const Snapshot before = inspect(original);
    if (before.shape.layout == SidebarLayout::None || !before.shape.editable)
        return refuse(before.shape.detail);
    const bool upgrade = enabled && before.shape.patched && before.shape.legacyOverlay;
    if (before.shape.patched == enabled && !upgrade)
        return { true, enabled ? QStringLiteral("This layout tweak is already applied.")
                              : QStringLiteral("The stock sidebar layout is already in place.") };

    struct Role
    {
        QString name;
        bool (*collect)(QString &, bool, SidebarLayout, QList<Edit> &);
    };
    const QList<Role> roles {
        { QStringLiteral("plr.qml"), &plrEdits },
        { QStringLiteral("PlrSidebar.qml"), &collectSidebarEdits },
        { QStringLiteral("PlrTopbar.qml"), &collectTopbarEdits },
    };
    QByteArray changed = original;
    for (const auto &role : roles) {
        Doc doc;
        QString error;
        if (!findDoc(changed, role.name, &doc, &error)) return refuse(error);
        QList<Edit> edits;
        if (upgrade) {
            // Normalize old copies in memory, then apply the new version. Only
            // the verified final image is written; never expose a half-upgrade.
            if (!role.collect(doc.qml, false, before.shape.layout, edits)
                || !applyEdits(doc.qml, edits))
                return refuse(QStringLiteral("Cannot safely upgrade the old layout; no file was written."));
            edits.clear();
        }
        if (!role.collect(doc.qml, enabled, before.shape.layout, edits))
            return refuse(QStringLiteral("%1 does not match the expected layout; no file was written.")
                              .arg(role.name));
        if (!applyEdits(doc.qml, edits))
            return refuse(QStringLiteral("Layout edits in %1 overlap; no file was written.").arg(role.name));
        const QByteArray header = doc.bom ? QByteArray::fromHex("efbbbf") : QByteArray();
        const QByteArray source = header + doc.qml.toUtf8();
        if (!QtResources::replaceResource(changed, doc.file.entryOff, doc.link, source, &error))
            return refuse(error);
    }
    const Snapshot after = inspect(changed);
    if (after.shape.layout == SidebarLayout::None || !after.shape.editable
        || after.shape.patched != enabled || (enabled && after.shape.legacyOverlay))
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
