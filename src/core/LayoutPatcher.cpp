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
const QString kResponsiveSidebar = QStringLiteral("responsive-sidebar");
// The responsive fix can be selected for the stock sidebar on its own. When
// moving the controls into the title bar, it is installed automatically; that
// copy is removed on restore, while a separately selected fix is preserved.
const QString kResponsiveBegin = QStringLiteral("// BSI CUSTOMIZER: responsive sidebar begin");
const QString kResponsiveEnd = QStringLiteral("// BSI CUSTOMIZER: responsive sidebar end");
const QString kResponsiveAuto = QStringLiteral(" [titlebar]");
// Distinguish this helper from earlier responsive patches, which did not keep
// unmodelled controls such as Discord accessible when space became tight.
const QString kResponsiveCurrent = QStringLiteral(" [v2]");
const QString kResponsiveCall = QStringLiteral("fBSIUpdateOverflow() /* BSI CUSTOMIZER: smart overflow */");
const QString kPopupOriginal = QStringLiteral(" // BSI CUSTOMIZER: popup original:");
const QString kMenuOriginal = QStringLiteral(" /* BSI CUSTOMIZER: overflow menu original:");
const QString kLayoutId = QStringLiteral("iBSISidebarLayout");
const QString kRepeaterId = QStringLiteral("iBSISidebarButtons");

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
const QString kCollapseHidden = QStringLiteral("\nvisible: false // BSI CUSTOMIZER: no sidebar to collapse in the title bar");
// Finish the comment before an inline `id:` or `}`. Older patches did not
// include this line break, so keep accepting their marker on restore.
const QString kCollapseHiddenCurrent = kCollapseHidden + QStringLiteral(" [v2]");

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

bool hasCurrentResponsiveHelper(const QString &qml)
{
    const int at = qml.indexOf(kResponsiveBegin);
    if (at < 0) return false;
    const QString suffix = qml.mid(at + kResponsiveBegin.size());
    return suffix.startsWith(kResponsiveCurrent)
        || suffix.startsWith(kResponsiveAuto + kResponsiveCurrent);
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
    auto files = QtResources::registeredFiles(image);
    // Both 5.10 and newer players register these QML resources. The theme
    // fallback probes every zlib stream in the EXE; avoid that expensive scan
    // when the requested file is already present in the validated registry.
    // Retain it for players whose resource tree is not registered.
    const bool registered = std::any_of(files.cbegin(), files.cend(),
                                        [&](const QtResources::File &file) { return file.name == name; });
    if (!registered) {
        const auto theme = ThemeEngine::findThemeZlib(image);
        if (theme.hasQrcNode)
            for (const auto &file : QtResources::filesInPool(image, theme.resource)) {
                bool present = false;
                for (const auto &old : files) if (old.entryOff == file.entryOff && old.name == file.name) present = true;
                if (!present) files << file;
            }
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

// The stock sidebar's overflow math can leave the kebab visible even when all
// enabled buttons fit, and only counting the other dimension when it is moved
// to a title bar makes this worse. Keep the player's original reset (it also
// owns other state), then correct the visible buttons using the *actual* slot
// budget. This is shared by the stock column and the relocated row.
struct SidebarControls
{
    struct Fixed { QString id, allowed, elementId; };
    QmlText::Object root, layout, repeater;
    QString layoutId, repeaterId, itemsName, allItemsName, repeatFlag;
    QList<Fixed> fixedButtons; // budgeted and represented in pAllSidebarItems
    QList<Fixed> unmodelledButtons; // budgeted but without a menu entry (e.g. Discord in 5.22)
    QStringList pinnedButtons; // always-present controls (e.g. Collapse in 5.10)
    bool horizontal = false;
    int resetEnd = -1;
};

bool sidebarControls(const QString &qml, const QString &masked, SidebarControls *out)
{
    SidebarControls controls;
    for (const auto &object : QmlText::objects(masked, QStringLiteral("Rectangle"))) {
        QmlText::Binding id;
        if (!QmlText::binding(masked, object.open, object.end, QStringLiteral("id"), &id)
            || !directChild(masked, object.open, id.nameBegin)
            || qml.mid(id.valueBegin, id.valueEnd - id.valueBegin) != QLatin1String("iSidebar"))
            continue;
        int begin = 0, end = 0;
        if (!functionSpan(masked, QStringLiteral("fResetSidebar"), &begin, &end)
            || begin <= object.open || end >= object.end
            || !directChild(masked, object.open,
                            masked.lastIndexOf(QLatin1String("function fResetSidebar"), begin)))
            return false;
        controls.root = object;
        controls.resetEnd = end;
        break;
    }
    if (controls.resetEnd < 0) return false;

    const auto member = [&](const QString &name) {
        QmlText::Binding b;
        if (QmlText::binding(masked, controls.root.open, controls.root.end, name, &b)
            && directChild(masked, controls.root.open, b.nameBegin)) return true;
        // pSidebarItems is declared as `property list<QtObject> pSidebarItems`
        // (no colon/initializer) in both real 5.10 and 5.22 players.
        const QRegularExpression declaration(QStringLiteral("\\bproperty\\s+list\\s*<\\s*QtObject\\s*>\\s*")
                                             + QRegularExpression::escape(name) + QStringLiteral("\\b"));
        auto declarations = declaration.globalMatch(masked);
        while (declarations.hasNext()) {
            const int at = declarations.next().capturedStart();
            if (at > controls.root.open && at < controls.root.end
                && directChild(masked, controls.root.open, at)) return true;
        }
        return false;
    };
    if (!member(QStringLiteral("pSidebarElementSize"))
        || !member(QStringLiteral("pVisibleSidebarElementsCount"))) return false;

    // Do not confuse a popup's nested layout/repeater with the button strip.
    int layouts = 0;
    for (const auto &type : { QStringLiteral("UiColumnLayout"), QStringLiteral("UiRowLayout") }) {
        for (const auto &object : QmlText::objects(masked, type)) {
            if (object.open <= controls.root.open || object.end > controls.root.end
                || !directChild(masked, controls.root.open, object.nameBegin)
                || !qml.mid(object.open, object.end - object.open).contains(QLatin1String("iKebabMenuBtn")))
                continue;
            controls.layout = object;
            controls.horizontal = type == QLatin1String("UiRowLayout");
            ++layouts;
        }
    }
    if (layouts != 1) return false;

    int repeaters = 0;
    for (const auto &object : QmlText::objects(masked, QStringLiteral("Repeater"))) {
        if (object.open <= controls.layout.open || object.end > controls.layout.end
            || !directChild(masked, controls.layout.open, object.nameBegin)) continue;
        QmlText::Binding model;
        if (!QmlText::binding(masked, object.open, object.end, QStringLiteral("model"), &model)
            || !directChild(masked, object.open, model.nameBegin)) continue;
        const QString name = qml.mid(model.valueBegin, model.valueEnd - model.valueBegin);
        if ((name == QLatin1String("pAllSidebarItems") || name == QLatin1String("iAllSidebarItems")
             || name == QLatin1String("pSidebarItems")) && member(name)) {
            controls.itemsName = name;
            controls.repeater = object;
            ++repeaters;
        }
    }
    if (repeaters != 1) return false;
    // BlueStacks 5.10/5.22 spell this Repeater flag correctly; older sidebar
    // components spell it "Repater". Use the player's actual property.
    controls.repeatFlag = qml.contains(QLatin1String("pShowViaRepeaterOnly"))
        ? QStringLiteral("pShowViaRepeaterOnly")
        : qml.contains(QLatin1String("pShowViaRepaterOnly"))
            ? QStringLiteral("pShowViaRepaterOnly") : QString();
    if (controls.repeatFlag.isEmpty()) return false;
    if (controls.itemsName == QLatin1String("pSidebarItems") && member(QStringLiteral("pAllSidebarItems"))) {
        int bodyBegin = 0, bodyEnd = 0;
        if (!functionSpan(masked, QStringLiteral("fResetSidebar"), &bodyBegin, &bodyEnd)
            || !qml.mid(bodyBegin, bodyEnd - bodyBegin).contains(
                   QLatin1String("pSidebarItems.push(pAllSidebarItems[i])"))) return false;
        controls.allItemsName = QStringLiteral("pAllSidebarItems");
    }

    QMap<QString, QmlText::Object> buttons;
    bool kebab = false;
    for (const auto &type : { QStringLiteral("UiImageButton"), QStringLiteral("Item") }) {
        for (const auto &object : QmlText::objects(masked, type)) {
            if (object.open <= controls.layout.open || object.end > controls.layout.end)
                continue;
            QmlText::Binding id;
            if (!QmlText::binding(masked, object.open, object.end, QStringLiteral("id"), &id)
                || !directChild(masked, object.open, id.nameBegin)) continue;
            const QString name = qml.mid(id.valueBegin, id.valueEnd - id.valueBegin);
            if (name == QLatin1String("iKebabMenuBtn")) { kebab = true; continue; }
            if (!QRegularExpression(QStringLiteral("^[_a-zA-Z][_a-zA-Z0-9]*$")).match(name).hasMatch()
                || buttons.contains(name)) return false;
            buttons.insert(name, object);
        }
    }
    if (!kebab) return false;
    // Only the controls that stock fResetSidebar itself budgets may have
    // their visibility reassigned. This includes the decorated Settings Item
    // in 5.22 and optional Discord/AI buttons (which have feature bindings).
    // Other direct buttons, like 5.10's Collapse, remain pinned and take one
    // slot only while they are visible.
    int bodyBegin = 0, bodyEnd = 0;
    if (!functionSpan(masked, QStringLiteral("fResetSidebar"), &bodyBegin, &bodyEnd)) return false;
    QSet<QString> managed;
    // A fixed button can be moved to the menu only if the player's model
    // actually contains an item for it. The latest player's Discord button
    // has no such item: hiding it would silently lose its action. Find the
    // model relationship from its stock !button.visible menu binding rather
    // than assuming every button in fResetSidebar has a matching model.
    QmlText::Binding allItems;
    const bool hasAllItems = QmlText::binding(masked, controls.root.open, controls.root.end,
                                              QStringLiteral("pAllSidebarItems"), &allItems)
        && directChild(masked, controls.root.open, allItems.nameBegin);
    const auto elements = QmlText::objects(masked, QStringLiteral("PlrSidebarElement"));
    const auto menuModelId = [&](const QString &button) -> QString {
        if (!hasAllItems) return {};
        QString found;
        for (const auto &element : elements) {
            if (element.nameBegin < allItems.valueBegin || element.end > allItems.valueEnd) continue;
            QmlText::Binding menu, id, repeat;
            if (!QmlText::binding(masked, element.open, element.end,
                                  QStringLiteral("pShowOnKebabMenu"), &menu)
                || !directChild(masked, element.open, menu.nameBegin)
                || qml.mid(menu.valueBegin, menu.valueEnd - menu.valueBegin)
                       != QStringLiteral("!") + button + QStringLiteral(".visible")
                || !QmlText::binding(masked, element.open, element.end,
                                     QStringLiteral("pElementId"), &id)
                || !directChild(masked, element.open, id.nameBegin)
                || !QmlText::binding(masked, element.open, element.end, controls.repeatFlag, &repeat)
                || !directChild(masked, element.open, repeat.nameBegin)
                || qml.mid(repeat.valueBegin, repeat.valueEnd - repeat.valueBegin) != QLatin1String("false"))
                continue;
            const QString modelId = qml.mid(id.valueBegin, id.valueEnd - id.valueBegin);
            if (!QRegularExpression(QStringLiteral("^[_a-zA-Z][_a-zA-Z0-9]*$"))
                     .match(modelId).hasMatch() || !found.isEmpty()) return {};
            found = modelId;
        }
        return found;
    };
    const QRegularExpression fixedRx(QStringLiteral(R"(\b(i[_a-zA-Z0-9]+)\.visible\s*=)"));
    auto assignments = fixedRx.globalMatch(masked.mid(bodyBegin, bodyEnd - bodyBegin));
    while (assignments.hasNext()) {
        const QString name = assignments.next().captured(1);
        if (!buttons.contains(name) || managed.contains(name)) continue;
        managed.insert(name);
        const auto object = buttons.value(name);
        QmlText::Binding visible;
        QString allowed = QStringLiteral("true");
        if (QmlText::binding(masked, object.open, object.end, QStringLiteral("visible"), &visible)
            && directChild(masked, object.open, visible.nameBegin)) {
            allowed = qml.mid(visible.valueBegin, visible.valueEnd - visible.valueBegin);
            if (allowed.contains(QLatin1String("parent.")) || allowed.contains(QLatin1String("this.")))
                return false; // not necessarily in scope in the root's helper
        }
        const QString modelId = menuModelId(name);
        if (modelId.isEmpty())
            controls.unmodelledButtons << SidebarControls::Fixed { name, allowed, {} };
        else
            controls.fixedButtons << SidebarControls::Fixed { name, allowed, modelId };
    }
    for (const auto &type : { QStringLiteral("UiImageButton"), QStringLiteral("Item") }) {
        for (const auto &object : QmlText::objects(masked, type)) {
            if (object.open <= controls.layout.open || object.end > controls.layout.end
                || !directChild(masked, controls.layout.open, object.nameBegin)) continue;
            QmlText::Binding id;
            if (!QmlText::binding(masked, object.open, object.end, QStringLiteral("id"), &id)
                || !directChild(masked, object.open, id.nameBegin)) continue;
            const QString name = qml.mid(id.valueBegin, id.valueEnd - id.valueBegin);
            if (name != QLatin1String("iKebabMenuBtn") && !managed.contains(name))
                controls.pinnedButtons << name;
        }
    }

    const auto objectId = [&](const QmlText::Object &object, const QString &fallback) {
        QmlText::Binding b;
        if (QmlText::binding(masked, object.open, object.end, QStringLiteral("id"), &b)
            && directChild(masked, object.open, b.nameBegin)) {
            const QString name = qml.mid(b.valueBegin, b.valueEnd - b.valueBegin);
            if (QRegularExpression(QStringLiteral("^[_a-zA-Z][_a-zA-Z0-9]*$")).match(name).hasMatch())
                return name;
            return QString();
        }
        if (qml.contains(fallback)) return QString(); // avoid an id collision
        return fallback;
    };
    controls.layoutId = objectId(controls.layout, kLayoutId);
    controls.repeaterId = objectId(controls.repeater, kRepeaterId);
    if (controls.layoutId.isEmpty() || controls.repeaterId.isEmpty()) return false;
    *out = controls;
    return true;
}

// Some player builds leave disabled entries in the Repeater's model. Count
// only enabled buttons for the space budget but keep the Repeater's raw index
// as the visibility cutoff; otherwise a later enabled button disappears with
// no three-dot menu to reach it. The menu must also admit *all* overflowing
// repeatable buttons, including ones whose stock pShowOnKebabMenu is false.
bool menuEdits(const QString &qml, const QString &masked, const SidebarControls &controls,
               QList<Edit> &edits)
{
    int found = 0;
    for (const auto &repeater : QmlText::objects(masked, QStringLiteral("Repeater"))) {
        if (repeater.open == controls.repeater.open || repeater.open <= controls.root.open
            || repeater.end >= controls.root.end) continue;
        bool windowed = false;
        for (const auto &popup : QmlText::objects(masked, QStringLiteral("UiWindowedPopup"))) {
            if (repeater.open > popup.open && repeater.end < popup.end) windowed = true;
        }
        if (!windowed) continue;
        QmlText::Binding model;
        if (!QmlText::binding(masked, repeater.open, repeater.end, QStringLiteral("model"), &model)
            || !directChild(masked, repeater.open, model.nameBegin)) continue;
        const QString modelName = qml.mid(model.valueBegin, model.valueEnd - model.valueBegin);
        if (modelName != controls.itemsName && modelName != controls.allItemsName) continue;
        for (const auto &button : QmlText::objects(masked, QStringLiteral("UiImageButton"))) {
            if (button.open <= repeater.open || button.end >= repeater.end) continue;
            QmlText::Binding visible;
            if (!QmlText::binding(masked, button.open, button.end,
                                  QStringLiteral("visible"), &visible)
                || !directChild(masked, button.open, visible.nameBegin)) continue;
            const QString value = qml.mid(visible.valueBegin, visible.valueEnd - visible.valueBegin);
            if (!value.contains(QLatin1String("pVisibleSidebarElementsCount"))
                || !value.contains(QLatin1String("pIsFeatureEnable"))
                || !value.contains(QLatin1String("pShowOnKebabMenu"))
                || !value.contains(QLatin1String("index"))) continue;
            const QString item = value.contains(QLatin1String("model.modelData."))
                ? QStringLiteral("model.modelData") : QStringLiteral("modelData");
            const QString replacement = QStringLiteral("fBSIInOverflow(") + item
                + QLatin1Char(')') + kMenuOriginal
                + QString::fromLatin1(value.toUtf8().toBase64()) + QStringLiteral(" */");
            addEdit(edits, visible.valueBegin, visible.valueEnd - visible.valueBegin, replacement);
            ++found;
        }
    }
    return found == 1; // never leave a hidden button unreachable
}

// The original popup placement in the sidebar uses the sidebar's global X
// and an index-based Y. After reparenting, neither is the trigger's position:
// use the real Repeater delegate (or the kebab when that button is hidden).
// Preserve each original assignment in a tagged comment so every supported
// player's exact source and line endings can be restored without guessing.
struct PopupLine
{
    int begin = 0, length = 0;
    QString object, axis, expression, original;
};

QString popupReplacement(const PopupLine &line, const QString &anchor)
{
    int indentLength = 0;
    while (indentLength < line.original.size() && line.original.at(indentLength).isSpace())
        ++indentLength;
    const QString replacement = line.original.left(indentLength) + line.object + QLatin1Char('.') + line.axis
        + QStringLiteral(" = fBSIPopupPoint(") + line.object + QStringLiteral(", ") + anchor
        + QStringLiteral(").") + line.axis;
    return replacement + kPopupOriginal + QString::fromLatin1(line.original.toUtf8().toBase64());
}

bool popupEdits(const QString &qml, const QString &masked, const SidebarControls &controls,
                QList<Edit> &edits)
{
    const QRegularExpression lineRx(QStringLiteral(
        R"((?m)^([ \t]*)([_a-zA-Z][_a-zA-Z0-9]*)\.(x|y)[ \t]*=[ \t]*([^\r\n]+))"));
    QList<PopupLine> lines;
    auto it = lineRx.globalMatch(masked);
    while (it.hasNext()) {
        const auto match = it.next();
        if (match.capturedStart() <= controls.root.open || match.capturedEnd() >= controls.root.end)
            continue;
        // capturedStart()/capturedLength() are qsizetype; braced initialization
        // would make the implicit conversion to int an MSVC C2397 error.
        lines << PopupLine { int(match.capturedStart()), int(match.capturedLength()),
                             match.captured(2), match.captured(3),
                             qml.mid(match.capturedStart(4), match.capturedLength(4)).trimmed(),
                             qml.mid(match.capturedStart(), match.capturedLength()) };
    }
    const QRegularExpression pointRx(QStringLiteral(
        R"(\bfGetPopupTopPoint\s*\(\s*([_a-zA-Z][_a-zA-Z0-9]*)\s*\))"));
    const QRegularExpression buttonRx(QStringLiteral(
        R"(\b(i[_a-zA-Z0-9]*Btn)\.mapToGlobal\s*\()"));
    QSet<int> used;
    int paired = 0;
    for (int j = 0; j < lines.size(); ++j) {
        const auto &y = lines.at(j);
        if (y.axis != QLatin1String("y")) continue;
        const auto point = pointRx.match(y.expression);
        const auto button = buttonRx.match(y.expression);
        if (!point.hasMatch() && !button.hasMatch()) continue;
        const QString anchor = point.hasMatch() ? point.captured(1)
            : button.captured(1) == QLatin1String("iKebabMenuBtn")
                  ? QStringLiteral("null") : button.captured(1);
        int xIndex = -1;
        for (int i = j - 1; i >= 0; --i) {
            if (y.begin - lines.at(i).begin > 700) break;
            if (lines.at(i).axis == QLatin1String("x") && lines.at(i).object == y.object
                && !used.contains(i)) { xIndex = i; break; }
        }
        if (xIndex < 0 || !lines.at(xIndex).expression.contains(QLatin1String("mapToGlobal")))
            return false; // unknown popup geometry: don't leave it half-moved
        for (const int index : { xIndex, j }) {
            const auto &line = lines.at(index);
            addEdit(edits, line.begin, line.length, popupReplacement(line, anchor));
            used.insert(index);
        }
        ++paired;
    }
    // Popup content sometimes resizes after creation. Its own onWidthChanged
    // must follow the same anchor, not snap back to the old right-hand strip.
    for (int i = 0; i < lines.size(); ++i) {
        const auto &line = lines.at(i);
        if (used.contains(i) || line.axis != QLatin1String("x")
            || !line.expression.contains(QLatin1String("iSidebar.mapToGlobal"))) continue;
        bool widthHandler = false;
        for (const auto &popup : QmlText::objects(masked, QStringLiteral("UiWindowedPopup"))) {
            if (popup.open >= line.begin || popup.end <= line.begin) continue;
            QmlText::Binding id, handler;
            if (!QmlText::binding(masked, popup.open, popup.end, QStringLiteral("id"), &id)
                || !directChild(masked, popup.open, id.nameBegin)
                || qml.mid(id.valueBegin, id.valueEnd - id.valueBegin) != line.object
                || !QmlText::binding(masked, popup.open, popup.end,
                                     QStringLiteral("onWidthChanged"), &handler)
                || !directChild(masked, popup.open, handler.nameBegin)) continue;
            widthHandler = line.begin >= handler.valueBegin && line.begin < handler.valueEnd;
            if (widthHandler) break;
        }
        if (!widthHandler) return false;
        addEdit(edits, line.begin, line.length,
                popupReplacement(line, line.object + QStringLiteral(".pBSIAnchor")));
        used.insert(i);
    }
    // A different form of fGetPopupTopPoint call could otherwise leave a
    // window stranded beside the old sidebar while the button has moved.
    if (qml.count(QLatin1String("fGetPopupTopPoint(")) > paired + 1) return false;
    if (used.isEmpty()) return true; // players with no windowed button panels
    for (const auto &popup : QmlText::objects(masked, QStringLiteral("UiWindowedPopup"))) {
        if (popup.open < controls.root.open || popup.end > controls.root.end) continue;
        QmlText::Binding existing;
        if (QmlText::binding(masked, popup.open, popup.end,
                             QStringLiteral("pBSIAnchor"), &existing)) return false;
        addEdit(edits, popup.open + 1, 0,
                lineEnding(qml) + QStringLiteral("            property var pBSIAnchor: undefined // BSI CUSTOMIZER: popup anchor"));
    }
    return true;
}

QString responsiveHelper(const SidebarControls &controls, bool autoInstalled, const QString &nl)
{
    const QString axis = controls.horizontal ? QStringLiteral("width") : QStringLiteral("height");
    const QString startPad = controls.horizontal ? QStringLiteral("leftPadding") : QStringLiteral("topPadding");
    const QString endPad = controls.horizontal ? QStringLiteral("rightPadding") : QStringLiteral("bottomPadding");
    QStringList fixed, unmodelled;
    for (const auto &button : controls.fixedButtons)
        fixed << QStringLiteral("{ button: ") + button.id + QStringLiteral(", allowed: (")
                 + button.allowed + QStringLiteral("), elementId: ") + button.elementId
                 + QStringLiteral(" }");
    for (const auto &button : controls.unmodelledButtons)
        unmodelled << QStringLiteral("{ button: ") + button.id + QStringLiteral(", allowed: (")
                      + button.allowed + QStringLiteral(") }");
    const QString rebuild = controls.allItemsName.isEmpty() ? QString() :
        QStringLiteral("        var enabledItems = []") + nl
        + QStringLiteral("        for (var i = 0; i < ") + controls.allItemsName + QStringLiteral(".length; ++i)") + nl
        + QStringLiteral("            if (") + controls.allItemsName + QStringLiteral("[i].pIsFeatureEnable)") + nl
        + QStringLiteral("                enabledItems.push(") + controls.allItemsName + QStringLiteral("[i])") + nl
        + QStringLiteral("        var changed = enabledItems.length !== ") + controls.itemsName
        + QStringLiteral(".length") + nl
        + QStringLiteral("        for (var i = 0; !changed && i < enabledItems.length; ++i)") + nl
        + QStringLiteral("            if (enabledItems[i] !== ") + controls.itemsName + QStringLiteral("[i]) changed = true") + nl
        + QStringLiteral("        if (changed) ") + controls.itemsName + QStringLiteral(" = enabledItems") + nl;
    const QStringList lines {
        QStringLiteral("    ") + kResponsiveBegin + (autoInstalled ? kResponsiveAuto : QString())
            + kResponsiveCurrent,
        QStringLiteral("    function fBSIUpdateOverflow() {"),
        QStringLiteral("        var slot = pSidebarElementSize"),
        QStringLiteral("        if (slot <= 0) return"),
        rebuild + QStringLiteral("        var items = ") + controls.itemsName,
        QStringLiteral("        var count = 0"),
        QStringLiteral("        for (var i = 0; i < items.length; ++i)"),
        QStringLiteral("            if (items[i].pIsFeatureEnable && items[i].")
            + controls.repeatFlag + QStringLiteral(") ++count"),
        QStringLiteral("        var gap = Math.max(0, ") + controls.layoutId + QStringLiteral(".spacing || 0)"),
        QStringLiteral("        var available = iSidebar.") + axis + QStringLiteral(" - (")
            + controls.layoutId + QLatin1Char('.') + startPad + QStringLiteral(" || 0) - (")
            + controls.layoutId + QLatin1Char('.') + endPad + QStringLiteral(" || 0)"),
        QStringLiteral("        var capacity = Math.max(0, Math.floor((available + gap) / (slot + gap)))"),
        QStringLiteral("        var pinned = [") + controls.pinnedButtons.join(QStringLiteral(", ")) + QLatin1Char(']'),
        QStringLiteral("        for (var n = 0; n < pinned.length; ++n) if (pinned[n].visible) --capacity"),
        QStringLiteral("        var unmodelled = [") + unmodelled.join(QStringLiteral(", ")) + QLatin1Char(']'),
        QStringLiteral("        for (var n = 0; n < unmodelled.length; ++n) {"),
        QStringLiteral("            unmodelled[n].button.visible = unmodelled[n].allowed"),
        QStringLiteral("            if (unmodelled[n].button.visible) --capacity"),
        QStringLiteral("        }"),
        QStringLiteral("        capacity = Math.max(0, capacity)"),
        QStringLiteral("        var fixed = [") + fixed.join(QStringLiteral(", ")) + QLatin1Char(']'),
        QStringLiteral("        var enabledFixed = 0"),
        QStringLiteral("        for (var n = 0; n < fixed.length; ++n) if (fixed[n].allowed) ++enabledFixed"),
        QStringLiteral("        var overflow = count + enabledFixed > capacity"),
        QStringLiteral("        var remaining = Math.max(0, capacity - (overflow ? 1 : 0))"),
        QStringLiteral("        for (var n = 0; n < fixed.length; ++n) {"),
        QStringLiteral("            fixed[n].button.visible = fixed[n].allowed && remaining > 0"),
        QStringLiteral("            if (fixed[n].button.visible) --remaining"),
        QStringLiteral("        }"),
        QStringLiteral("        var toShow = Math.min(count, remaining)"),
        QStringLiteral("        var cutoff = -1"),
        QStringLiteral("        for (var i = 0; i < items.length && toShow > 0; ++i) {"),
        QStringLiteral("            if (items[i].pIsFeatureEnable && items[i].")
            + controls.repeatFlag + QStringLiteral(") {"),
        QStringLiteral("                cutoff = i"),
        QStringLiteral("                --toShow"),
        QStringLiteral("            }"),
        QStringLiteral("        }"),
        QStringLiteral("        pVisibleSidebarElementsCount = cutoff + 1"),
        QStringLiteral("        iKebabMenuBtn.visible = overflow && capacity > 0"),
        QStringLiteral("    }"),
        QStringLiteral("    function fBSIInOverflow(item) {"),
        QStringLiteral("        if (!item.pIsFeatureEnable) return false"),
        QStringLiteral("        if (!item.") + controls.repeatFlag + QStringLiteral(") {"),
        QStringLiteral("            var fixed = [") + fixed.join(QStringLiteral(", ")) + QLatin1Char(']'),
        QStringLiteral("            for (var n = 0; n < fixed.length; ++n)"),
        QStringLiteral("                if (fixed[n].elementId === item.pElementId)"),
        QStringLiteral("                    return fixed[n].allowed && !fixed[n].button.visible"),
        QStringLiteral("            return false"),
        QStringLiteral("        }"),
        QStringLiteral("        var items = ") + controls.itemsName,
        QStringLiteral("        for (var i = 0; i < items.length; ++i) {"),
        QStringLiteral("            if (items[i].pElementId === item.pElementId && items[i].")
            + controls.repeatFlag + QStringLiteral(") {"),
        QStringLiteral("                var delegate = ") + controls.repeaterId + QStringLiteral(".itemAt(i)"),
        QStringLiteral("                return !delegate || !delegate.visible"),
        QStringLiteral("            }"),
        QStringLiteral("        }"),
        QStringLiteral("        return true"),
        QStringLiteral("    }"),
        QStringLiteral("    function fBSIPopupPoint(popup, elementId) {"),
        QStringLiteral("        if (elementId === undefined) return Qt.point(popup.x, popup.y)"),
        QStringLiteral("        popup.pBSIAnchor = elementId"),
        QStringLiteral("        var button = null"),
        QStringLiteral("        if (elementId && typeof elementId === 'object') button = elementId"),
        QStringLiteral("        else if (elementId !== null) {"),
        QStringLiteral("            var items = ") + controls.itemsName,
        QStringLiteral("            for (var i = 0; i < items.length; ++i) {"),
        QStringLiteral("                if (items[i].pIsFeatureEnable && items[i].pElementId === elementId) {"),
        QStringLiteral("                    var delegate = ") + controls.repeaterId + QStringLiteral(".itemAt(i)"),
        QStringLiteral("                    if (delegate && delegate.visible) button = delegate"),
        QStringLiteral("                    break"),
        QStringLiteral("                }"),
        QStringLiteral("            }"),
        QStringLiteral("        }"),
        QStringLiteral("        if (!button || !button.visible) button = iKebabMenuBtn"),
        QStringLiteral("        var bottom = button.mapToGlobal(button.width / 2, button.height)"),
        QStringLiteral("        var top = button.mapToGlobal(button.width / 2, 0)"),
        QStringLiteral("        var shadow = popup.shadowThickness || 0"),
        QStringLiteral("        var bounds = popup.screen ? popup.screen.availableGeometry : null"),
        QStringLiteral("        if (!bounds || bounds.width <= 0)"),
        QStringLiteral("            bounds = Qt.rect(iAppwin.x, iAppwin.y, iAppwin.width, iAppwin.height)"),
        QStringLiteral("        var left = bounds.x, right = bounds.x + bounds.width"),
        QStringLiteral("        var upper = bounds.y, lower = bounds.y + bounds.height"),
        QStringLiteral("        var x = bottom.x - popup.width / 2"),
        QStringLiteral("        var y = bottom.y + 6 - shadow"),
        QStringLiteral("        if (y + popup.height > lower && top.y - 6 + shadow - popup.height >= upper)"),
        QStringLiteral("            y = top.y - 6 + shadow - popup.height"),
        QStringLiteral("        return Qt.point(Math.max(left, Math.min(x, right - popup.width)),"),
        QStringLiteral("                        Math.max(upper, Math.min(y, lower - popup.height)))"),
        QStringLiteral("    }"),
        QStringLiteral("    ") + kResponsiveEnd
    };
    return nl + lines.join(nl) + nl;
}

bool responsiveEdits(QString &qml, bool enabled, bool autoInstalled, QList<Edit> &edits)
{
    const QString masked = QmlText::mask(qml);
    SidebarControls controls;
    if (!sidebarControls(qml, masked, &controls)) return false;
    const QString nl = lineEnding(qml);
    const bool applied = qml.contains(kResponsiveBegin);
    if (enabled) {
        if (applied || qml.contains(kResponsiveEnd) || qml.contains(kPopupOriginal)
            || qml.contains(kLayoutId) || qml.contains(kRepeaterId)
            || qml.contains(QLatin1String("pBSIAnchor"))) return false;
        if (!menuEdits(qml, masked, controls, edits)
            || !popupEdits(qml, masked, controls, edits)) return false;
        // Updating an *already moved* single-owner layout does not pass
        // through sidebarEdits(true). Earlier releases left 5.10's Collapse
        // button live; hide it here as well so clicking it cannot remove the
        // entire new title-bar row. The normal move already inserted this.
        if (autoInstalled && controls.horizontal && !qml.contains(kCollapseHidden)) {
            for (const auto &button : QmlText::objects(masked, QStringLiteral("UiImageButton"))) {
                if (button.open <= controls.layout.open || button.end > controls.layout.end
                    || !directChild(masked, controls.layout.open, button.nameBegin)) continue;
                QmlText::Binding id, visible;
                if (!QmlText::binding(masked, button.open, button.end, QStringLiteral("id"), &id)
                    || !directChild(masked, button.open, id.nameBegin)
                    || qml.mid(id.valueBegin, id.valueEnd - id.valueBegin) != QLatin1String("iCollapseBtn"))
                    continue;
                if (QmlText::binding(masked, button.open, button.end, QStringLiteral("visible"), &visible)
                    && directChild(masked, button.open, visible.nameBegin)) return false;
                addEdit(edits, button.open + 1, 0, kCollapseHiddenCurrent + nl);
            }
        }
        if (controls.layoutId == kLayoutId)
            addEdit(edits, controls.layout.open + 1, 0,
                    nl + QStringLiteral("        id: ") + kLayoutId
                       + QStringLiteral(" // BSI CUSTOMIZER: layout id"));
        if (controls.repeaterId == kRepeaterId)
            addEdit(edits, controls.repeater.open + 1, 0,
                    nl + QStringLiteral("        id: ") + kRepeaterId
                       + QStringLiteral(" // BSI CUSTOMIZER: repeater id"));
        addEdit(edits, controls.resetEnd, 0,
                nl + QStringLiteral("        ") + kResponsiveCall + nl + QStringLiteral("    "));
        addEdit(edits, controls.root.end - 1, 0, responsiveHelper(controls, autoInstalled, nl));
    } else {
        if (!applied || !qml.contains(kResponsiveEnd)) return false;
        const int begin = qml.indexOf(nl + QStringLiteral("    ") + kResponsiveBegin, controls.root.open);
        const int end = qml.indexOf(QStringLiteral("    ") + kResponsiveEnd, begin);
        if (begin < 0 || end < begin || end >= controls.root.end
            || qml.count(kResponsiveBegin) != 1 || qml.count(kResponsiveEnd) != 1) return false;
        addEdit(edits, begin, end + 4 + kResponsiveEnd.size() + nl.size() - begin, QString());
        const QString call = nl + QStringLiteral("        ") + kResponsiveCall + nl + QStringLiteral("    ");
        const int at = qml.indexOf(call, controls.root.open);
        if (at < 0 || at + call.size() > controls.resetEnd || qml.count(kResponsiveCall) != 1)
            return false;
        addEdit(edits, at, call.size(), QString());
        for (const auto &tag : { QStringLiteral("        id: ") + kLayoutId
                                    + QStringLiteral(" // BSI CUSTOMIZER: layout id"),
                                 QStringLiteral("        id: ") + kRepeaterId
                                    + QStringLiteral(" // BSI CUSTOMIZER: repeater id"),
                                 QStringLiteral("            property var pBSIAnchor: undefined // BSI CUSTOMIZER: popup anchor") }) {
            int from = 0;
            while ((from = qml.indexOf(nl + tag, from)) >= 0) {
                addEdit(edits, from, nl.size() + tag.size(), QString());
                from += nl.size() + tag.size();
            }
        }
        const QRegularExpression originalRx(QStringLiteral(
            R"((?m)^([^\r\n]*?) // BSI CUSTOMIZER: popup original:([A-Za-z0-9+/=]+))"));
        auto iter = originalRx.globalMatch(qml);
        int count = 0;
        while (iter.hasNext()) {
            const auto match = iter.next();
            const QByteArray encoded = match.captured(2).toLatin1();
            const QByteArray bytes = QByteArray::fromBase64(encoded);
            if (bytes.toBase64() != encoded || QString::fromUtf8(bytes).toUtf8() != bytes
                || !match.captured(1).contains(QLatin1String("fBSIPopupPoint("))) return false;
            const QString original = QString::fromUtf8(bytes);
            if (!original.contains(QLatin1String(".x ="))
                && !original.contains(QLatin1String(".y ="))) return false;
            addEdit(edits, int(match.capturedStart()), int(match.capturedLength()), original);
            ++count;
        }
        if (count != qml.count(kPopupOriginal)) return false;
        const QRegularExpression menuRx(QStringLiteral(
            R"(fBSIInOverflow\((?:model\.modelData|modelData)\) /\* BSI CUSTOMIZER: overflow menu original:([A-Za-z0-9+/=]+) \*/)"));
        auto menu = menuRx.globalMatch(qml);
        if (!menu.hasNext() || qml.count(kMenuOriginal) != 1) return false;
        const auto match = menu.next();
        if (menu.hasNext()) return false;
        const QByteArray encoded = match.captured(1).toLatin1();
        const QByteArray bytes = QByteArray::fromBase64(encoded);
        const QString value = QString::fromUtf8(bytes);
        if (bytes.toBase64() != encoded || value.toUtf8() != bytes
            || !value.contains(QLatin1String("pVisibleSidebarElementsCount"))) return false;
        addEdit(edits, int(match.capturedStart()), int(match.capturedLength()), value);
    }
    return true;
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
    // 5.10 puts a Collapse button at the start of the sidebar. It has no
    // meaning once the side strip is removed (and could hide the moved row).
    for (const auto &button : QmlText::objects(masked, QStringLiteral("UiImageButton"))) {
        if (button.open <= column.open || button.end > column.end
            || !directChild(masked, column.open, button.nameBegin)) continue;
        QmlText::Binding id;
        if (!QmlText::binding(masked, button.open, button.end, QStringLiteral("id"), &id)
            || !directChild(masked, button.open, id.nameBegin)
            || qml.mid(id.valueBegin, id.valueEnd - id.valueBegin) != QLatin1String("iCollapseBtn"))
            continue;
        QmlText::Binding visible;
        // The marker is a line comment: it must end before an inline `id:`
        // or `}`. Distinguish the current versioned insertion from earlier
        // releases so their original line breaks are restored byte-for-byte.
        const QString nl = lineEnding(qml);
        const QString currentMarker = kCollapseHiddenCurrent + nl;
        const QString earlierMarker = kCollapseHidden + nl;
        if (enabled) {
            if (QmlText::binding(masked, button.open, button.end, QStringLiteral("visible"), &visible)
                && directChild(masked, button.open, visible.nameBegin)) return false;
            addEdit(edits, button.open + 1, 0, currentMarker);
        } else if (qml.mid(button.open + 1, kCollapseHiddenCurrent.size()) == kCollapseHiddenCurrent) {
            if (qml.mid(button.open + 1, currentMarker.size()) != currentMarker) return false;
            addEdit(edits, button.open + 1, currentMarker.size(), QString());
        } else if (qml.mid(button.open + 1, earlierMarker.size()) == earlierMarker) {
            // The intermediate release inserted a terminating line break;
            // the first release did not. Two consecutive breaks (or a
            // one-line original button) identify the inserted one.
            const int bodyAt = button.open + 1 + earlierMarker.size();
            const bool inserted = qml.mid(bodyAt, nl.size()) == nl
                || !qml.mid(bodyAt, button.end - bodyAt).contains(nl);
            addEdit(edits, button.open + 1,
                    inserted ? earlierMarker.size() : kCollapseHidden.size(), QString());
        } else if (qml.mid(button.open + 1, kCollapseHidden.size()) == kCollapseHidden) {
            addEdit(edits, button.open + 1, kCollapseHidden.size(), QString());
        } // older patches did not hide Collapse; normalize them on upgrade
    }
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
    // If the separate responsive option is already on, keep its overflow
    // budget on the new axis as the original sidebar is moved/restored.
    if (qml.contains(kResponsiveBegin)) {
        const int start = qml.indexOf(kResponsiveBegin);
        const int end = qml.indexOf(kResponsiveEnd, start);
        const QString oldBudget = QStringLiteral("var available = ") + from;
        const int budgetAt = qml.indexOf(oldBudget, start);
        if (end <= start || budgetAt < start || budgetAt + oldBudget.size() > end)
            return false;
        addEdit(edits, budgetAt, oldBudget.size(), QStringLiteral("var available = ") + to);
        // An independently selected stock-sidebar helper survives moving the
        // row and later restoring it. Its padding must follow the new axis as
        // well as `available`; older moved helpers sometimes still have the
        // vertical padding, so accept either before normalizing it.
        SidebarControls responsiveControls;
        if (!sidebarControls(qml, masked, &responsiveControls)) return false;
        for (const auto &pair : { QPair<QString, QString> {
                                     enabled ? QStringLiteral("topPadding") : QStringLiteral("leftPadding"),
                                     enabled ? QStringLiteral("leftPadding") : QStringLiteral("topPadding") },
                                  QPair<QString, QString> {
                                     enabled ? QStringLiteral("bottomPadding") : QStringLiteral("rightPadding"),
                                     enabled ? QStringLiteral("rightPadding") : QStringLiteral("bottomPadding") } }) {
            const QString oldPad = responsiveControls.layoutId + QLatin1Char('.') + pair.first;
            const QString newPad = responsiveControls.layoutId + QLatin1Char('.') + pair.second;
            const int padAt = qml.indexOf(oldPad, start);
            if (padAt >= start && padAt + oldPad.size() <= end)
                addEdit(edits, padAt, oldPad.size(), newPad);
            else {
                const int already = qml.indexOf(newPad, start);
                if (already < start || already + newPad.size() > end) return false;
            }
        }
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
    const bool responsive = qml.contains(kResponsiveBegin);
    const bool installedByTitlebar = qml.contains(kResponsiveBegin + kResponsiveAuto);
    if (enabled && responsive && !hasCurrentResponsiveHelper(qml)) {
        // If the stock sidebar had the earlier independently selected fix,
        // refresh it before moving the row. Keep it independent so restoring
        // the title-bar layout will not silently turn that option off.
        if (!responsiveEdits(qml, false, installedByTitlebar, edits)
            || !applyEdits(qml, edits)) return false;
        edits.clear();
        if (!responsiveEdits(qml, true, installedByTitlebar, edits)
            || !applyEdits(qml, edits)) return false;
        edits.clear();
    }
    if (!enabled && installedByTitlebar) {
        // Remove only the fix we installed on behalf of this layout option.
        if (!responsiveEdits(qml, false, true, edits) || !applyEdits(qml, edits)) return false;
        edits.clear();
    }
    if (!sidebarEdits(qml, enabled, edits)) return false;
    if (enabled && !responsive) {
        if (!applyEdits(qml, edits)) return false;
        edits.clear();
        return responsiveEdits(qml, true, true, edits);
    }
    return true;
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
    bool responsive = false;
    bool responsiveCurrent = false;
    bool installedByTitlebar = false;
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

    snapshot.shape.responsive = snapshot.sidebar.qml.contains(kResponsiveBegin);
    snapshot.shape.responsiveCurrent = hasCurrentResponsiveHelper(snapshot.sidebar.qml);
    snapshot.shape.installedByTitlebar = snapshot.sidebar.qml.contains(kResponsiveBegin + kResponsiveAuto);
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
    if (!collectSidebarEdits(sidebar, enabled, snapshot.shape.layout, sidebarPlan)) {
        snapshot.shape.detail = QStringLiteral(
            "PlrSidebar.qml does not have the supported button strip, overflow logic and "
            "popup anchors; the tweak would be unsafe to write.");
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
    if (snapshot.shape.patched && !snapshot.shape.responsiveCurrent) {
        // An earlier layout (with or without the older responsive helper)
        // can be upgraded in place, but only after proving the whole change
        // against its QML. Never overwrite an unfamiliar patch speculatively.
        QString oldSidebar = snapshot.sidebar.qml;
        QList<Edit> repair;
        if (snapshot.shape.responsive) {
            if (!responsiveEdits(oldSidebar, false, snapshot.shape.installedByTitlebar, repair)
                || !applyEdits(oldSidebar, repair)) {
                snapshot.shape.detail = QStringLiteral("The older responsive layout cannot be upgraded safely.");
                return snapshot;
            }
            repair.clear();
        }
        const bool autoInstalled = !snapshot.shape.responsive || snapshot.shape.installedByTitlebar;
        if (!responsiveEdits(oldSidebar, true, autoInstalled, repair)
            || !applyEdits(oldSidebar, repair)) {
            snapshot.shape.detail = QStringLiteral(
                "The older title-bar layout cannot be upgraded safely on this player.");
            return snapshot;
        }
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
          QStringLiteral("Moves the original buttons into a centered title-bar row. All visible "
                         "buttons stay out until they no longer fit; only then do the extras go "
                         "into the three-dot menu. Panels open directly beneath their buttons. "
                         "Turn this off and apply to restore the sidebar.") },
        { kResponsiveSidebar,
          QStringLiteral("Responsive sidebar buttons and aligned panels"),
          QStringLiteral("Keep the three-dot menu hidden while all enabled sidebar buttons fit. "
                         "Overflow only the buttons that need it, and open sound, controls and "
                         "other panels below their own buttons. Included automatically when "
                         "moving the buttons to the title bar; use this option for the stock sidebar.") },
    };
}

State analyze(const QByteArray &image, const QString &id)
{
    State state;
    if (id != kSidebarToTitlebar && id != kResponsiveSidebar) {
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
    if (id == kResponsiveSidebar) {
        state.applied = snapshot.shape.responsive;
        if (snapshot.shape.patched) {
            state.detail = QStringLiteral("Included with the title-bar layout. Restore the sidebar "
                                          "first to change this separately.");
            return state;
        }
        QString sidebar = snapshot.sidebar.qml;
        QList<Edit> plan;
        if (!responsiveEdits(sidebar, !state.applied, false, plan) || !applyEdits(sidebar, plan)) {
            state.detail = QStringLiteral("This sidebar does not have supported overflow and popup anchors.");
            return state;
        }
        state.supported = true;
        state.needsUpdate = state.applied && !snapshot.shape.responsiveCurrent;
        state.detail = state.needsUpdate ? QStringLiteral("Earlier responsive layout applied. Press Update.")
                     : state.applied ? QStringLiteral("Applied.") : QStringLiteral("Ready.");
        return state;
    }
    state.supported = true;
    state.applied = snapshot.shape.patched;
    state.needsUpdate = state.applied && (snapshot.shape.legacyOverlay || !snapshot.shape.responsiveCurrent);
    state.detail = snapshot.shape.legacyOverlay ? QStringLiteral("Older layout applied. Press Update to repair it.")
                  : state.needsUpdate ? QStringLiteral("Press Update for smart overflow and panels beneath buttons.")
                  : state.applied ? QStringLiteral("Applied.") : QStringLiteral("Ready.");
    return state;
}

Result apply(const QString &exePath, const QString &id, bool enabled)
{
    const auto refuse = [&](const QString &message) { return Result { false, message }; };
    if (id != kSidebarToTitlebar && id != kResponsiveSidebar)
        return refuse(QStringLiteral("Unknown layout tweak."));
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

    QByteArray changed = original;
    if (id == kResponsiveSidebar) {
        if (before.shape.patched)
            return refuse(QStringLiteral("The responsive fix is included with the title-bar layout. "
                                         "Restore the sidebar first to change it separately."));
        const bool update = enabled && before.shape.responsive && !before.shape.responsiveCurrent;
        if (before.shape.responsive == enabled && !update)
            return { true, enabled ? QStringLiteral("Responsive sidebar is already applied.")
                                   : QStringLiteral("Stock sidebar overflow is already in place.") };
        Doc doc;
        QString error;
        if (!findDoc(changed, QStringLiteral("PlrSidebar.qml"), &doc, &error)) return refuse(error);
        QList<Edit> edits;
        if (update) {
            if (!responsiveEdits(doc.qml, false, false, edits) || !applyEdits(doc.qml, edits))
                return refuse(QStringLiteral("Cannot safely update the previous responsive layout; no file was written."));
            edits.clear();
        }
        if (!responsiveEdits(doc.qml, enabled, false, edits) || !applyEdits(doc.qml, edits))
            return refuse(QStringLiteral("The sidebar's overflow/popup QML is not a supported shape; "
                                         "no file was written."));
        const QByteArray header = doc.bom ? QByteArray::fromHex("efbbbf") : QByteArray();
        if (!QtResources::replaceResource(changed, doc.file.entryOff, doc.link,
                                          header + doc.qml.toUtf8(), &error)) return refuse(error);
    } else {
        const bool upgrade = enabled && before.shape.patched && before.shape.legacyOverlay;
        const bool repair = enabled && before.shape.patched && !before.shape.responsiveCurrent && !upgrade;
        if (before.shape.patched == enabled && !upgrade && !repair)
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
        for (const auto &role : roles) {
            if (repair && role.name != QLatin1String("PlrSidebar.qml")) continue;
            Doc doc;
            QString error;
            if (!findDoc(changed, role.name, &doc, &error)) return refuse(error);
            QList<Edit> edits;
            if (upgrade) {
                // Normalize old copies in memory, then apply the new version.
                // Only the verified final image is ever written to disk.
                if (!role.collect(doc.qml, false, before.shape.layout, edits)
                    || !applyEdits(doc.qml, edits))
                    return refuse(QStringLiteral("Cannot safely upgrade the old layout; no file was written."));
                edits.clear();
            }
            if (repair && before.shape.responsive) {
                if (!responsiveEdits(doc.qml, false, before.shape.installedByTitlebar, edits)
                    || !applyEdits(doc.qml, edits))
                    return refuse(QStringLiteral("Cannot safely update the older responsive layout; no file was written."));
                edits.clear();
            }
            const bool autoInstalled = !before.shape.responsive || before.shape.installedByTitlebar;
            if (repair ? !responsiveEdits(doc.qml, true, autoInstalled, edits)
                       : !role.collect(doc.qml, enabled, before.shape.layout, edits))
                return refuse(QStringLiteral("%1 does not match the expected layout; no file was written.")
                                  .arg(role.name));
            if (!applyEdits(doc.qml, edits))
                return refuse(QStringLiteral("Layout edits in %1 overlap; no file was written.").arg(role.name));
            const QByteArray header = doc.bom ? QByteArray::fromHex("efbbbf") : QByteArray();
            if (!QtResources::replaceResource(changed, doc.file.entryOff, doc.link,
                                              header + doc.qml.toUtf8(), &error)) return refuse(error);
        }
    }
    const Snapshot after = inspect(changed);
    const bool verified = after.shape.layout != SidebarLayout::None && after.shape.editable
        && (id == kResponsiveSidebar
            ? !after.shape.patched && after.shape.responsive == enabled
                  && (!enabled || after.shape.responsiveCurrent)
            : after.shape.patched == enabled && (!enabled ||
                (after.shape.responsiveCurrent && !after.shape.legacyOverlay)));
    if (!verified)
        return refuse(QStringLiteral("The written layout did not pass resource/read-back "
                                     "verification; no file was written."));
    if (!ThemeEngine::backup(exePath))
        return refuse(QStringLiteral("Could not preserve the original backup; no EXE was written."));
    QString error;
    if (!PatchFile::writeVerified(exePath, original, changed, &error)) return refuse(error);
    const int caches = QtResources::clearPlayerQmlCache(exePath);
    QString msg = id == kResponsiveSidebar
        ? (enabled ? QStringLiteral("Sidebar buttons now overflow only when needed; panels open "
                                    "beneath their buttons. Restart BlueStacks.")
                   : QStringLiteral("Stock sidebar overflow and panel placement restored. "
                                    "Restart BlueStacks."))
        : (enabled ? QStringLiteral("Sidebar buttons now sit in the title bar; extras overflow "
                                    "only when needed and panels open beneath their buttons. "
                                    "Restart BlueStacks.")
                   : QStringLiteral("Sidebar layout restored. Restart BlueStacks."));
    if (caches > 0)
        msg += QStringLiteral(" Cleared the player QML disk cache so BlueStacks reloads the source.");
    return { true, msg };
}

} // namespace LayoutPatcher
