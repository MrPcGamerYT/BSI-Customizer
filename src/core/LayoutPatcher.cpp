#include "LayoutPatcher.h"

#include "BsDetector.h"
#include "PatchFile.h"
#include "PeUtils.h"
#include "QmlText.h"
#include "QtResources.h"
#include "ThemeEngine.h"

#include <QFile>
#include <QHash>
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
const QString kInsetStock = QStringLiteral(
    "pBorderWidth + (sidebarVisible ? iAppwin.pSidebarWidth : 0)");
const QString kInsetPatched = QStringLiteral("pBorderWidth + 0");
// BlueStacks changes these spellings between builds (palette names, a
// qualified sidebar id, the ad/inset arithmetic). A tweak that only accepted
// one exact spelling disabled both layout options on every other build, so a
// value that does not match the known stock text is replaced by *our* value
// followed by the original, base64-tagged. Restoring decodes that tag, which
// keeps the byte-for-byte undo promise for shapes this file has never seen.
const QString kColorOriginal = QStringLiteral(" /* BSI CUSTOMIZER: color original:");
const QString kInsetOriginal = QStringLiteral(" /* BSI CUSTOMIZER: inset original:");
const QString kWidthOriginal = QStringLiteral(" /* BSI CUSTOMIZER: width original:");
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
// Added only when a player build has no stock resize handler to rename.
const QString kAxisHandler = QStringLiteral(" // BSI CUSTOMIZER: sidebar axis handler");
// Marks a sidebar whose own overflow code was kept because it does not carry
// the kebab/Repeater/popup model the smart helper needs. The title-bar move
// itself does not depend on that model, so the button can still be offered.
// Tags appended to a binding this tool added or changed. They are block
// comments so they can sit inside a single-line declaration without eating
// the rest of it, and they tell a later run which value is ours.
const QString kButtonHiddenTag = QStringLiteral(" /* BSI CUSTOMIZER: hidden button */");
const QString kSettingsFlipTag = QStringLiteral(" /* BSI CUSTOMIZER: settings in the row */");
const QString kNoSmartMarker = QStringLiteral(" // BSI CUSTOMIZER: stock overflow only");

// ---------------------------------------------------------- clean player UI
// One option that hides every extra top-bar button in one apply. Back/Home/
// Recent, the product logo, the window title/version and the minimize/
// maximize/close controls stay. The sidebar is deliberately left alone: it is
// only removed by moving it into the title bar ("Sidebar buttons to the top
// bar"). The stock top bar re-asserts `visible` from fReloadTopbarElements(),
// so the option switches the owning PlrTopbarElement's `pVisible` off as well;
// a plain `visible: false` binding would be overwritten the next time the
// window is resized.
const QString kCleanUi = QStringLiteral("clean-player-ui");
const QString kCleanMarker = QStringLiteral("BSI CUSTOMIZER: clean player UI");
const QString kCleanHidePrefix = QStringLiteral(
    "false /* BSI CUSTOMIZER: clean player UI */ && (");
const QString kCleanHiddenLine = QStringLiteral(
    "visible: false // BSI CUSTOMIZER: clean player UI");
const QString kCleanVisibleTag = QStringLiteral(
    " /* BSI CUSTOMIZER: clean player UI visible:");
// The player assigns iSidebar.visible imperatively when fullscreen is left
// (restoring the pre-fullscreen value), which would destroy the false binding
// above and paint the empty strip again. Collapsing the instance width to zero
// keeps the content area full width even then; the width is never assigned at
// runtime.
const QString kCleanWidthTag = QStringLiteral(
    " /* BSI CUSTOMIZER: clean player UI width:");
// Sidebar buttons are hidden through `pIsFeatureEnable`, the switch the sidebar
// itself uses to build pSidebarItems: the element leaves the column and the
// kebab overflow together, so no 32px gap is left behind.
const QString kSidebarCleanTag = QStringLiteral(
    " /* BSI CUSTOMIZER: sidebar clean:");
const QString kSidebarCleanLine = QStringLiteral(
    "pIsFeatureEnable: false // BSI CUSTOMIZER: sidebar clean");

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

// `value` followed by an inert QML comment carrying the original expression.
// The comment is read back from the source text (taggedAfter), not from the
// binding value, so the exact original text stays recoverable.
QString withOriginal(const QString &value, const QString &original, const QString &marker)
{
    return value + marker + QString::fromLatin1(original.toUtf8().toBase64())
        + QStringLiteral(" */");
}

bool originalFrom(const QString &value, const QString &marker, QString *original)
{
    const int at = value.indexOf(marker);
    if (at < 0) return false;
    const int begin = at + marker.size();
    const int end = value.indexOf(QLatin1String("*/"), begin);
    if (end < 0) return false;
    const QByteArray encoded = value.mid(begin, end - begin).trimmed().toLatin1();
    const QByteArray bytes = QByteArray::fromBase64(encoded);
    // Reject anything that is not the exact encoding we write, so a foreign
    // comment cannot make the restore path write arbitrary text.
    if (bytes.isEmpty() || bytes.toBase64() != encoded
        || QString::fromUtf8(bytes).toUtf8() != bytes) return false;
    *original = QString::fromUtf8(bytes);
    return true;
}

// The trailing comment that follows a patched value on the same line.
// QmlText::mask() blanks comments and binding() trims the resulting spaces,
// so the tag is not part of the binding value: read it from the source text
// right after the value instead.
bool taggedAfter(const QString &qml, int valueBegin, const QString &marker,
                 QString *original, int *tagEnd)
{
    const int at = qml.indexOf(marker, valueBegin);
    if (at < 0) return false;
    if (qml.mid(valueBegin, at - valueBegin).contains(QLatin1Char('\n'))) return false;
    const int close = qml.indexOf(QLatin1String("*/"), at + marker.size());
    if (close < 0) return false;
    if (!originalFrom(qml.mid(at, close + 2 - at), marker, original)) return false;
    *tagEnd = close + 2;
    return true;
}

// True for the zero width this tweak writes. A tagged original is a comment,
// which the binding scanner trims, so the value itself is always exactly `0`.
bool zeroWidthValue(const QString &value)
{
    return value == QLatin1String("0");
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
    // The host starts where the title bar's own Back/Home/Recent end, so the
    // moved buttons continue that row instead of floating in its middle. The
    // position comes from PlrTopbar.qml (pBSISidebarX) because those buttons
    // live in that component.
    const QStringList lines {
        QStringLiteral("        Item {"),
        QStringLiteral("            ") + kOverlayMarker,
        QStringLiteral("            id: ") + kOverlayId,
        QStringLiteral("            anchors.fill: parent"),
        QStringLiteral("            anchors.leftMargin: parent.pBSISidebarX"),
        // Keep clear of the window controls and icons that live on the right.
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

// First member `name` that belongs to the object itself (not to a nested
// child that happens to declare a member with the same name).
bool directBinding(const QString &masked, int open, int end, const QString &name,
                   QmlText::Binding *out)
{
    int from = open;
    while (from < end) {
        QmlText::Binding binding;
        if (!QmlText::binding(masked, from, end, name, &binding)) return false;
        if (directChild(masked, open, binding.nameBegin)) {
            *out = binding;
            return true;
        }
        from = binding.nameBegin + name.size();
    }
    return false;
}

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
        // The width member is the one thing every supported build spells the
        // same way: it carries the stock sidebar width. A build that qualifies
        // it (`iAppwin.pSidebarWidth`) or wraps it in arithmetic still gets the
        // same zero-width strip, and the original expression is kept in a tag.
        QmlText::Binding width;
        if (!directBinding(masked, instance.open, instance.end, widthName, &width)
            || !directChild(masked, instance.open, width.nameBegin))
            return false;
        const QString widthValue = qml.mid(width.valueBegin, width.valueEnd - width.valueBegin);
        if (zeroWidthValue(widthValue)) return false; // already moved
        if (!widthValue.contains(QLatin1String("pSidebarWidth"))) return false;
        addEdit(edits, width.valueBegin, width.valueEnd - width.valueBegin,
                widthValue == QLatin1String("pSidebarWidth")
                    ? QStringLiteral("0")
                    : withOriginal(QStringLiteral("0"), widthValue, kWidthOriginal));
        // Palette expressions differ between builds (and the panel colour is
        // irrelevant once the strip is zero width), so keep the exact stock
        // text when it is the known one and tag anything else. A build that
        // paints the colour in the component itself simply has no member.
        QmlText::Binding color;
        if (directBinding(masked, instance.open, instance.end, QStringLiteral("color"), &color)
            && directChild(masked, instance.open, color.nameBegin)) {
            const QString colorValue = qml.mid(color.valueBegin, color.valueEnd - color.valueBegin);
            if (colorValue == kColorStock) {
                addEdit(edits, color.valueBegin, color.valueEnd - color.valueBegin, kColorPatched);
            } else if (colorValue != kColorPatched && !colorValue.contains(kColorOriginal)) {
                addEdit(edits, color.valueBegin, color.valueEnd - color.valueBegin,
                        withOriginal(kColorPatched, colorValue, kColorOriginal));
            } else {
                return false; // already patched by us
            }
        }
        QmlText::Binding id;
        if (!directBinding(masked, instance.open, instance.end, QStringLiteral("id"), &id)
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
        QmlText::Binding width;
        if (!directBinding(masked, original.open, original.end, widthName, &width)) return false;
        if (qml.mid(width.valueBegin, width.valueEnd - width.valueBegin) != QLatin1String("0"))
            return false;
        QString restoredWidth = QStringLiteral("pSidebarWidth");
        int widthEnd = width.valueEnd;
        QString widthOriginal;
        if (taggedAfter(qml, width.valueBegin, kWidthOriginal, &widthOriginal, &widthEnd))
            restoredWidth = widthOriginal;
        addEdit(edits, width.valueBegin, widthEnd - width.valueBegin, restoredWidth);
        QmlText::Binding color;
        if (directBinding(masked, original.open, original.end, QStringLiteral("color"), &color)) {
            const QString colorValue = qml.mid(color.valueBegin, color.valueEnd - color.valueBegin);
            if (colorValue != kColorPatched) return false;
            QString restoredColor = kColorStock;
            int colorEnd = color.valueEnd;
            QString colorOriginal;
            if (taggedAfter(qml, color.valueBegin, kColorOriginal, &colorOriginal, &colorEnd))
                restoredColor = colorOriginal;
            addEdit(edits, color.valueBegin, colorEnd - color.valueBegin, restoredColor);
        }
    }

    // The Android frame must span the full window width now that no side strip
    // exists; fSetUiParams keeps receiving `sidebarVisible` from its callers.
    // The known spelling is restored from the original constant; anything else
    // is matched structurally and stored in a tag so unknown indentation and
    // identifier qualification still round-trip byte-for-byte.
    int bodyBegin = 0, bodyEnd = 0;
    if (!functionSpan(masked, QStringLiteral("fSetUiParams"), &bodyBegin, &bodyEnd)) return false;
    if (enabled) {
        const int exact = qml.indexOf(kInsetStock, bodyBegin);
        if (exact >= 0 && exact + kInsetStock.size() <= bodyEnd) {
            addEdit(edits, exact, kInsetStock.size(), kInsetPatched);
        } else {
            const QRegularExpression insetRx(QStringLiteral(
                "sidebarVisible\\s*\\?\\s*[^:?\\r\\n]*pSidebarWidth[^:?\\r\\n]*:\\s*0\\b"));
            const auto match = insetRx.match(masked.mid(bodyBegin, bodyEnd - bodyBegin));
            if (!match.hasMatch()) return false;
            const int start = bodyBegin + int(match.capturedStart());
            const int length = int(match.capturedLength());
            if (start < bodyBegin || start + length > bodyEnd) return false;
            addEdit(edits, start, length,
                    withOriginal(QStringLiteral("0"), qml.mid(start, length), kInsetOriginal));
        }
    } else {
        const QString tagged = QStringLiteral("0") + kInsetOriginal;
        const int taggedAt = qml.indexOf(tagged, bodyBegin);
        if (taggedAt >= 0 && taggedAt + tagged.size() <= bodyEnd) {
            const int close = qml.indexOf(QLatin1String("*/"), taggedAt);
            if (close < 0 || close + 2 > bodyEnd) return false;
            QString original;
            if (!originalFrom(qml.mid(taggedAt + 1, close + 2 - taggedAt - 1),
                              kInsetOriginal, &original)) return false;
            addEdit(edits, taggedAt, close + 2 - taggedAt, original);
        } else {
            const int exact = qml.indexOf(kInsetPatched, bodyBegin);
            if (exact < 0 || exact + kInsetPatched.size() > bodyEnd) return false;
            addEdit(edits, exact, kInsetPatched.size(), kInsetStock);
        }
    }
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
    int resetBegin = -1;
    int resetEnd = -1;
};

// The innermost object that opens the block containing `pos`.
bool enclosingObject(const QString &masked, int pos, QmlText::Object *object)
{
    // Walk backwards to the nearest brace that is still open at `pos`. Scanning
    // forwards and remembering the last '{' returns a sibling that already
    // closed, which breaks parent lookup for objects declared after a nested
    // child (for example a text field under a Text item). Backward scanning also
    // steps over the document's own function bodies, whose braces do not open a
    // QML object.
    int depth = 0;
    int open = -1;
    for (int i = qMin(pos, int(masked.size())) - 1; i >= 0; --i) {
        const QChar c = masked.at(i);
        if (c == QLatin1Char('}')) ++depth;
        else if (c == QLatin1Char('{')) {
            if (depth == 0) { open = i; break; }
            --depth;
        }
    }
    if (open < 0) return false;
    int nameEnd = open;
    while (nameEnd > 0 && masked.at(nameEnd - 1).isSpace()) --nameEnd;
    int nameBegin = nameEnd;
    while (nameBegin > 0 && (masked.at(nameBegin - 1).isLetterOrNumber()
                             || masked.at(nameBegin - 1) == QLatin1Char('_')
                             || masked.at(nameBegin - 1) == QLatin1Char('.')))
        --nameBegin;
    if (nameBegin == nameEnd) return false;
    int close = 0;
    // closeBrace() is private to QmlText; the object list below has the span.
    for (const auto &candidate : QmlText::objects(masked, masked.mid(nameBegin, nameEnd - nameBegin))) {
        if (candidate.open == open) { close = candidate.end; break; }
    }
    if (close <= open) return false;
    *object = QmlText::Object { nameBegin, open, close };
    return true;
}

// The sidebar component's root, whatever object type the player uses. The id
// (`iSidebar`) is the only part the inserted helpers depend on; fResetSidebar
// is required when the caller needs the stock overflow logic.
bool findSidebarRoot(const QString &qml, const QString &masked, QmlText::Object *root,
                     bool requireReset = true)
{
    const auto accepts = [&](const QmlText::Object &object) {
        QmlText::Binding id;
        if (!directBinding(masked, object.open, object.end, QStringLiteral("id"), &id)
            || qml.mid(id.valueBegin, id.valueEnd - id.valueBegin) != QLatin1String("iSidebar"))
            return false;
        if (!requireReset) return true;
        int begin = 0, end = 0;
        return functionSpan(masked, QStringLiteral("fResetSidebar"), &begin, &end)
            && begin > object.open && end < object.end;
    };
    for (const auto &type : { QStringLiteral("Rectangle"), QStringLiteral("Item"),
                              QStringLiteral("UiRectangle"), QStringLiteral("UiItem"),
                              QStringLiteral("FocusScope") }) {
        for (const auto &object : QmlText::objects(masked, type)) {
            if (!accepts(object)) continue;
            *root = object;
            return true;
        }
    }
    // Unknown root type: locate the id binding and use its enclosing object.
    const QRegularExpression idRx(QStringLiteral(R"(\bid\s*:\s*iSidebar\b)"));
    const auto match = idRx.match(masked);
    if (match.hasMatch()) {
        QmlText::Object object;
        if (enclosingObject(masked, int(match.capturedStart()), &object) && accepts(object)) {
            *root = object;
            return true;
        }
    }
    return false;
}

bool sidebarControls(const QString &qml, const QString &masked, SidebarControls *out)
{
    SidebarControls controls;
    if (!findSidebarRoot(qml, masked, &controls.root)) return false;
    int resetBegin = 0, resetEnd = 0;
    if (!functionSpan(masked, QStringLiteral("fResetSidebar"), &resetBegin, &resetEnd)
        || resetBegin <= controls.root.open || resetEnd >= controls.root.end) return false;
    controls.resetBegin = resetBegin;
    controls.resetEnd = resetEnd;

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
    // A build may declare more than one direct layout; prefer the one that
    // also hosts the Repeater, and stay ambiguous rather than guessing.
    QList<QmlText::Object> candidates;
    QList<bool> candidateHorizontal;
    for (const auto &type : { QStringLiteral("UiColumnLayout"), QStringLiteral("UiRowLayout"),
                              QStringLiteral("UiFlow"), QStringLiteral("Flow"),
                              QStringLiteral("Row"), QStringLiteral("Column") }) {
        for (const auto &object : QmlText::objects(masked, type)) {
            if (object.open <= controls.root.open || object.end > controls.root.end
                || !directChild(masked, controls.root.open, object.nameBegin)
                || !qml.mid(object.open, object.end - object.open).contains(QLatin1String("iKebabMenuBtn")))
                continue;
            candidates << object;
            candidateHorizontal << (type == QLatin1String("UiRowLayout")
                                    || type == QLatin1String("Row")
                                    || qml.mid(object.open, object.end - object.open).contains(kRowHidden));
        }
    }
    if (candidates.isEmpty()) return false;
    int chosen = 0;
    if (candidates.size() > 1) {
        int withRepeater = -1;
        for (int i = 0; i < candidates.size(); ++i) {
            if (!qml.mid(candidates.at(i).open, candidates.at(i).end - candidates.at(i).open)
                     .contains(QLatin1String("Repeater"))) continue;
            if (withRepeater >= 0) return false;
            withRepeater = i;
        }
        if (withRepeater < 0) return false;
        chosen = withRepeater;
    }
    controls.layout = candidates.at(chosen);
    controls.horizontal = candidateHorizontal.at(chosen);

    int repeaters = 0;
    for (const auto &object : QmlText::objects(masked, QStringLiteral("Repeater"))) {
        if (object.open <= controls.layout.open || object.end > controls.layout.end
            || !directChild(masked, controls.layout.open, object.nameBegin)) continue;
        QmlText::Binding model;
        if (!QmlText::binding(masked, object.open, object.end, QStringLiteral("model"), &model)
            || !directChild(masked, object.open, model.nameBegin)) continue;
        const QString name = qml.mid(model.valueBegin, model.valueEnd - model.valueBegin);
        // Player builds name this model property differently (pAllSidebarItems,
        // iAllSidebarItems, pSidebarItems, ...). Any model that is one of the
        // root's own list members is usable by the generated helper.
        if (QRegularExpression(QStringLiteral("^[_a-zA-Z][_a-zA-Z0-9]*$")).match(name).hasMatch()
            && member(name)) {
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
    // Moving Settings into the Repeater is this tool's own edit. It must not
    // change how the player's buttons are classified, or an Update would write
    // a different overflow helper than a fresh apply.
    const auto stockRepeatFlag = [&](const QmlText::Binding &repeat) {
        const QString value = qml.mid(repeat.valueBegin, repeat.valueEnd - repeat.valueBegin);
        if (value == QLatin1String("false")) return true;
        return value == QLatin1String("true")
            && qml.mid(repeat.valueEnd, kSettingsFlipTag.size()) == kSettingsFlipTag;
    };
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
                || !stockRepeatFlag(repeat))
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
    // Popups for volume, game controls, macros, screenshot, location,
    // shake, fullscreen, settings, etc. all anchor to a sidebar button via
    // `<btn>.mapToGlobal(...)`. Older/newer player builds don't always name
    // those buttons with a "Btn" suffix (e.g. iSound, iGameControls,
    // pScreenshot), so build the set of valid button ids from the sidebar
    // we actually parsed instead of hard-coding an i*Btn regex — that way
    // every panel drops beneath its real button on every supported build.
    QSet<QString> buttonIds;
    for (const auto &b : controls.fixedButtons) buttonIds.insert(b.id);
    for (const auto &b : controls.unmodelledButtons) buttonIds.insert(b.id);
    for (const auto &b : controls.pinnedButtons) buttonIds.insert(b);
    buttonIds.insert(QStringLiteral("iKebabMenuBtn"));
    // Also pick up any other direct UiImageButton/Item ids inside the strip
    // that weren't already listed (builds that don't model a button in
    // pAllSidebarItems / fResetSidebar still have panels that anchor to it).
    for (const auto &type : { QStringLiteral("UiImageButton"), QStringLiteral("Item") }) {
        for (const auto &object : QmlText::objects(masked, type)) {
            if (object.open <= controls.layout.open || object.end > controls.layout.end
                || !directChild(masked, controls.layout.open, object.nameBegin)) continue;
            QmlText::Binding id;
            if (!QmlText::binding(masked, object.open, object.end, QStringLiteral("id"), &id)
                || !directChild(masked, object.open, id.nameBegin)) continue;
            const QString name = qml.mid(id.valueBegin, id.valueEnd - id.valueBegin);
            if (QRegularExpression(QStringLiteral("^[_a-zA-Z][_a-zA-Z0-9]*$")).match(name).hasMatch())
                buttonIds.insert(name);
        }
    }
    const QRegularExpression anyButtonRx(QStringLiteral(
        R"(\b([_a-zA-Z][_a-zA-Z0-9]*)\.mapToGlobal\s*\()"));
    QSet<int> used;
    int paired = 0;
    for (int j = 0; j < lines.size(); ++j) {
        const auto &y = lines.at(j);
        if (y.axis != QLatin1String("y")) continue;
        const auto point = pointRx.match(y.expression);
        QString anchor;
        if (point.hasMatch()) {
            anchor = point.captured(1);
        } else {
            // Walk every .mapToGlobal( on the line and pick the first one that
            // actually names a sidebar button; the kebab is mapped to null so
            // its popup opens under the three dots.
            auto btns = anyButtonRx.globalMatch(y.expression);
            while (btns.hasNext()) {
                const auto m = btns.next();
                const QString name = m.captured(1);
                if (!buttonIds.contains(name)) continue;
                anchor = (name == QLatin1String("iKebabMenuBtn"))
                    ? QStringLiteral("null") : name;
                break;
            }
            if (anchor.isEmpty()) continue;
        }
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
        if (used.contains(i) || line.axis != QLatin1String("x")) continue;
        // Old/new builds use different coordinate sources for the x anchor:
        // iSidebar.mapToGlobal, the popup's parent.mapToGlobal, or a named
        // button's .mapToGlobal. If the line doesn't reference global coords
        // at all it's just a static offset — leave it untouched.
        if (!line.expression.contains(QLatin1String(".mapToGlobal"))) continue;
        bool sizeHandler = false;
        const QStringList sizeHandlers {
            QStringLiteral("onWidthChanged"), QStringLiteral("onHeightChanged"),
            QStringLiteral("onVisibleChanged"), QStringLiteral("onVisibleToParentChanged")
        };
        for (const auto &popup : QmlText::objects(masked, QStringLiteral("UiWindowedPopup"))) {
            if (popup.open >= line.begin || popup.end <= line.begin) continue;
            QmlText::Binding id;
            if (!QmlText::binding(masked, popup.open, popup.end, QStringLiteral("id"), &id)
                || !directChild(masked, popup.open, id.nameBegin)) continue;
            if (qml.mid(id.valueBegin, id.valueEnd - id.valueBegin) != line.object) continue;
            for (const auto &handlerName : sizeHandlers) {
                QmlText::Binding handler;
                if (!QmlText::binding(masked, popup.open, popup.end, handlerName, &handler)
                    || !directChild(masked, popup.open, handler.nameBegin)) continue;
                if (line.begin >= handler.valueBegin && line.begin < handler.valueEnd) {
                    sizeHandler = true;
                    break;
                }
            }
            if (sizeHandler) break;
        }
        // Any mapToGlobal-using X assignment inside a popup's size/visibility
        // handler must be re-anchored, otherwise that panel would snap back
        // to the old sidebar strip the moment it resizes/appears.
        if (!sizeHandler) return false;
        addEdit(edits, line.begin, line.length,
                popupReplacement(line, line.object + QStringLiteral(".pBSIAnchor")));
        used.insert(i);
    }
    // Any remaining fGetPopupTopPoint() call we didn't match is one whose
    // anchor button we don't know about; refuse the patch instead of
    // stranding that panel at the old sidebar X. We accept one extra
    // (paired+1) because older builds declare the helper function itself
    // whose name contains the substring — definition lines don't count.
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
        // Some Qt 5/Qt 6 builds pass the sidebar model object (for example
        // pSound), not its string pElementId and not the visual button. Only
        // treat an object as the trigger when it really exposes mapToGlobal;
        // otherwise resolve its element id through the fixed/model delegates.
        QStringLiteral("        var lookupId = elementId"),
        QStringLiteral("        if (elementId && typeof elementId === 'object') {"),
        QStringLiteral("            if (typeof elementId.mapToGlobal === 'function') button = elementId"),
        QStringLiteral("            else if (elementId.pElementId !== undefined) lookupId = elementId.pElementId"),
        QStringLiteral("        }"),
        QStringLiteral("        if (!button && lookupId !== null && lookupId !== undefined) {"),
        // Search the fixed (non-Repeater) buttons first — Settings, Back,
        // Home, Recent, etc. — so their own panels open beneath them instead
        // of snapping to the three dots.
        QStringLiteral("            var fixed = [") + fixed.join(QStringLiteral(", ")) + QStringLiteral("]"),
        QStringLiteral("            for (var n = 0; n < fixed.length; ++n) {"),
        QStringLiteral("                if (fixed[n].elementId === lookupId || fixed[n].elementId === elementId) {"),
        QStringLiteral("                    if (fixed[n].button && fixed[n].button.visible)"),
        QStringLiteral("                        button = fixed[n].button"),
        QStringLiteral("                    break"),
        QStringLiteral("                }"),
        QStringLiteral("            }"),
        // Then unmodelled buttons (Discord/AI on builds that ship them) and
        // any fixed button passed by direct id instead of pElementId.
        QStringLiteral("            var unmodelled = [") + unmodelled.join(QStringLiteral(", ")) + QStringLiteral("]"),
        QStringLiteral("            for (var n = 0; !button && n < unmodelled.length; ++n) {"),
        QStringLiteral("                if (unmodelled[n].button && unmodelled[n].allowed) {"),
        QStringLiteral("                    if (unmodelled[n].button === elementId || unmodelled[n].button === lookupId")
            + QStringLiteral(") button = unmodelled[n].button"),
        QStringLiteral("                }"),
        QStringLiteral("            }"),
        // Fallback: some builds pass the button's own id as a string (e.g.
        // the literal "iGameControlsBtn"). Try resolving it as a property
        // of the sidebar component scope before giving up.
        QStringLiteral("            if (!button && typeof lookupId === 'string' && typeof this[lookupId] !== 'undefined'")
        + QStringLiteral(" && this[lookupId] && this[lookupId].mapToGlobal) {"),
        QStringLiteral("                button = this[lookupId]"),
        QStringLiteral("            }"),
        // Finally the Repeater model (volume, game controls, macros,
        // screenshot, location, shake, fullscreen, … — whichever the build
        // exposes).
        QStringLiteral("            if (!button) {"),
        QStringLiteral("                var items = ") + controls.itemsName,
        QStringLiteral("                for (var i = 0; i < items.length; ++i) {"),
        QStringLiteral("                    if (items[i].pIsFeatureEnable && (items[i] === elementId || items[i].pElementId === lookupId)) {"),
        QStringLiteral("                        var delegate = ") + controls.repeaterId + QStringLiteral(".itemAt(i)"),
        QStringLiteral("                        if (delegate && delegate.visible) button = delegate"),
        QStringLiteral("                        break"),
        QStringLiteral("                    }"),
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
        // Place the popup's visible top edge flush with the bottom of its
        // trigger. Account for the popup shadow so the panel opens directly
        // below the button rather than floating away from it.
        QStringLiteral("        var y = bottom.y - shadow"),
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
        // One line of its own, just before the function's closing brace, so
        // the stock indentation stays exactly as it was and taking the line
        // away again restores the document byte for byte.
        const int braceLine = qml.lastIndexOf(QLatin1Char('\n'), controls.resetEnd - 1) + 1;
        if (braceLine <= controls.resetBegin || braceLine > controls.resetEnd) return false;
        addEdit(edits, braceLine, 0, QStringLiteral("        ") + kResponsiveCall + nl);
        addEdit(edits, controls.root.end - 1, 0, responsiveHelper(controls, autoInstalled, nl));
    } else {
        if (!applied || !qml.contains(kResponsiveEnd)) return false;
        const int begin = qml.indexOf(nl + QStringLiteral("    ") + kResponsiveBegin, controls.root.open);
        const int end = qml.indexOf(QStringLiteral("    ") + kResponsiveEnd, begin);
        if (begin < 0 || end < begin || end >= controls.root.end
            || qml.count(kResponsiveBegin) != 1 || qml.count(kResponsiveEnd) != 1) return false;
        addEdit(edits, begin, end + 4 + kResponsiveEnd.size() + nl.size() - begin, QString());
        // The whole line the call sits on. An earlier release added it in
        // front of the closing brace and left a whitespace-only line above
        // it; that line came from the same edit and goes with it.
        const int callAt = qml.indexOf(kResponsiveCall, controls.resetBegin);
        if (callAt < 0 || callAt > controls.resetEnd || qml.count(kResponsiveCall) != 1)
            return false;
        const int eol = qml.indexOf(QLatin1Char('\n'), callAt);
        if (eol < 0) return false;
        int callLine = qml.lastIndexOf(QLatin1Char('\n'), callAt) + 1;
        const int callEnd = eol + 1;
        const int prevNl = qml.lastIndexOf(QLatin1Char('\n'), callLine - 1);
        if (prevNl > controls.resetBegin) {
            const int prevLine = qml.lastIndexOf(QLatin1Char('\n'), prevNl - 1) + 1;
            if (qml.mid(prevLine, prevNl - prevLine).trimmed().isEmpty()) callLine = prevLine;
        }
        addEdit(edits, callLine, callEnd - callLine, QString());
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

// The button strip inside a PlrSidebar component. Every generation keeps its
// buttons in one direct child layout; some add a three-dot button, a Repeater
// model and windowed popups, others only the strip itself. The title-bar move
// needs nothing but the strip, so it must not require those extras.
bool findButtonStrip(const QString &qml, const QString &masked, const QmlText::Object &root,
                     QmlText::Object *strip)
{
    // Buttons are `UiImageButton` in the known builds; a build that draws its
    // own controls still names them `iSomethingBtn`.
    const QRegularExpression buttonRx(QStringLiteral(R"(\bid\s*:\s*i[_a-zA-Z0-9]*Btn\b)"));
    const auto holdsButtons = [&](const QmlText::Object &object) {
        const QString body = qml.mid(object.open, object.end - object.open);
        return body.contains(QLatin1String("UiImageButton"))
            || buttonRx.match(QmlText::mask(body)).hasMatch();
    };
    QList<QmlText::Object> candidates;
    for (const auto &type : { QStringLiteral("UiColumnLayout"), QStringLiteral("UiRowLayout"),
                              QStringLiteral("UiFlow"), QStringLiteral("Flow"),
                              QStringLiteral("Row"), QStringLiteral("Column") }) {
        for (const auto &object : QmlText::objects(masked, type)) {
            if (object.nameBegin <= root.open || object.end > root.end) continue;
            if (!directChild(masked, root.open, object.nameBegin)) continue;
            if (!holdsButtons(object)) continue;
            candidates << object;
        }
    }
    if (candidates.isEmpty()) return false;
    // Prefer the strip that owns the overflow button, then the one with the
    // Repeater, then the first strip with buttons.
    int best = 0;
    int bestScore = 0;
    for (int i = 0; i < candidates.size(); ++i) {
        const QString body = qml.mid(candidates.at(i).open,
                                     candidates.at(i).end - candidates.at(i).open);
        int score = 1;
        if (body.contains(QLatin1String("Repeater"))) score = 2;
        if (body.contains(QLatin1String("iKebabMenuBtn"))) score = 3;
        if (score > bestScore) {
            bestScore = score;
            best = i;
        }
    }
    *strip = candidates.at(best);
    return true;
}

// ---------------------------------------------------------------------------
// Button order and per-button visibility
// ---------------------------------------------------------------------------
//
// The moved row *is* the sidebar's own button strip: its direct children are
// the buttons in display order, and the Repeater among them renders the
// pAllSidebarItems entries (Fullscreen, Sound, Files / APK, ...). So "move a
// button" means moving one declaration - never rewriting its contents - and
// "hide a button" means switching the member that decides its visibility. The
// stock order is recorded in a comment before the first move, so turning the
// option off puts every declaration back where it was.

const QString kOrderMarkerPrefix = QStringLiteral("// BSI CUSTOMIZER: order ");
const QString kHiddenRowMarker = QStringLiteral("// BSI CUSTOMIZER: hidden row buttons=");
// Block comments, not line comments: a build may put several members on one
// line, and a line comment would swallow everything after the value.

// What the UI asks for: one key per button of the row, and the ones to hide.
struct RowRequest
{
    QStringList order;
    QSet<QString> hidden;
};

QString lineIndentAt(const QString &qml, int pos)
{
    int start = qml.lastIndexOf(QLatin1Char('\n'), qMax(0, pos - 1)) + 1;
    if (start > pos) start = 0;
    QString indent;
    for (int i = start; i < qml.size() && (qml.at(i) == QLatin1Char(' ') || qml.at(i) == QLatin1Char('\t')); ++i)
        indent += qml.at(i);
    return indent;
}

// The `// BSI CUSTOMIZER: order <scope>=a,b,c` line this tool writes.
bool orderMarker(const QString &qml, const QString &scope, QStringList *keys, int *lineBegin, int *lineEnd)
{
    const QString prefix = kOrderMarkerPrefix + scope + QLatin1Char('=');
    const int at = qml.indexOf(prefix);
    if (at < 0) return false;
    int stop = qml.indexOf(QLatin1Char('\n'), at);
    if (stop < 0) stop = qml.size();
    const QString list = qml.mid(at + prefix.size(), stop - at - prefix.size()).trimmed();
    if (keys) *keys = list.isEmpty() ? QStringList() : list.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (lineBegin) *lineBegin = qml.lastIndexOf(QLatin1Char('\n'), at) + 1;
    if (lineEnd) *lineEnd = qMin(stop + 1, qml.size());
    return true;
}

QString orderMarkerLine(const QString &scope, const QStringList &keys, const QString &nl, const QString &indent)
{
    return indent + kOrderMarkerPrefix + scope + QLatin1Char('=') + keys.join(QLatin1Char(',')) + nl;
}

// Every direct child object of `parent`, in source order, whatever its type.
QList<QmlText::Object> childrenOf(const QString &masked, const QmlText::Object &parent)
{
    QList<QmlText::Object> children;
    int i = parent.open + 1;
    const int limit = parent.end - 1;
    while (i < limit) {
        const QChar c = masked.at(i);
        if (!c.isLetter() && c != QLatin1Char('_')) { ++i; continue; }
        const QChar previous = masked.at(i - 1);
        if (previous.isLetterOrNumber() || previous == QLatin1Char('_') || previous == QLatin1Char('.')) {
            ++i;
            continue;
        }
        int nameEnd = i;
        while (nameEnd < limit && (masked.at(nameEnd).isLetterOrNumber()
                                   || masked.at(nameEnd) == QLatin1Char('_')
                                   || masked.at(nameEnd) == QLatin1Char('.'))) ++nameEnd;
        int open = nameEnd;
        while (open < limit && masked.at(open).isSpace()) ++open;
        if (open >= limit || masked.at(open) != QLatin1Char('{')) { i = nameEnd; continue; }
        // A handler such as `onClicked: { ... }` also opens a block; only a
        // type name followed by `{` declares a child object.
        if (masked.mid(i, open - i).contains(QLatin1Char(':'))) { ++i; continue; }
        int depth = 0;
        int end = open;
        for (; end < limit + 1; ++end) {
            if (masked.at(end) == QLatin1Char('{')) ++depth;
            else if (masked.at(end) == QLatin1Char('}') && --depth == 0) { ++end; break; }
        }
        if (depth != 0) return children;
        children << QmlText::Object { i, open, end };
        i = end;
    }
    return children;
}

QString objectType(const QString &masked, const QmlText::Object &object)
{
    return masked.mid(object.nameBegin, object.open - object.nameBegin).trimmed()
        .section(QLatin1Char('.'), -1);
}

bool objectId(const QString &qml, const QString &masked, const QmlText::Object &object, QString *id)
{
    QmlText::Binding binding;
    if (!directBinding(masked, object.open, object.end, QStringLiteral("id"), &binding)) return false;
    *id = qml.mid(binding.valueBegin, binding.valueEnd - binding.valueBegin).trimmed();
    return true;
}

struct ButtonSlot
{
    QString key;
    QString id;        // the object's own id, empty for a Repeater
    int begin = -1;    // first character of the object's type name
    int end = -1;      // one past its closing '}'
    QmlText::Object object;
};

// Display keys of the buttons the strip names explicitly.
const QHash<QString, QString> &fixedButtonKeys()
{
    static const QHash<QString, QString> keys {
        { QStringLiteral("iBackBtn"), QStringLiteral("back") },
        { QStringLiteral("iHomeBtn"), QStringLiteral("home") },
        { QStringLiteral("iRecentBtn"), QStringLiteral("recent") },
        { QStringLiteral("iSettingsBtn"), QStringLiteral("settings") },
        { QStringLiteral("iDiscordBtn"), QStringLiteral("discord") },
        { QStringLiteral("iChatBtn"), QStringLiteral("chat") },
        { QStringLiteral("iKebabMenuBtn"), QStringLiteral("more") },
    };
    return keys;
}

// pFullscreen -> fullscreen, pInstallApk -> installapk, "screen" -> screen.
QString elementKey(const QString &qml, const QString &masked, const QmlText::Object &element)
{
    QmlText::Binding binding;
    if (!directBinding(masked, element.open, element.end, QStringLiteral("pElementId"), &binding))
        return QString();
    QString value = qml.mid(binding.valueBegin, binding.valueEnd - binding.valueBegin).trimmed();
    if (value.startsWith(QLatin1Char('"')) || value.startsWith(QLatin1Char('\'')))
        value = value.mid(1);
    if (value.endsWith(QLatin1Char('"')) || value.endsWith(QLatin1Char('\'')))
        value.chop(1);
    if (value.startsWith(QLatin1Char('p')) && value.size() > 1) value = value.mid(1);
    return value.toLower();
}

const QHash<QString, QString> &buttonLabels()
{
    static const QHash<QString, QString> labels {
        { QStringLiteral("back"), QStringLiteral("Back") },
        { QStringLiteral("home"), QStringLiteral("Home") },
        { QStringLiteral("recent"), QStringLiteral("Recent apps") },
        { QStringLiteral("fullscreen"), QStringLiteral("Fullscreen") },
        { QStringLiteral("volume"), QStringLiteral("Sound") },
        { QStringLiteral("installapk"), QStringLiteral("Files / APK") },
        { QStringLiteral("settings"), QStringLiteral("Settings") },
        { QStringLiteral("lockcursor"), QStringLiteral("Lock mouse cursor") },
        { QStringLiteral("gamecontrol"), QStringLiteral("Game controls") },
        { QStringLiteral("imapabort"), QStringLiteral("Gamepad (R3)") },
        { QStringLiteral("macro"), QStringLiteral("Macro") },
        { QStringLiteral("sync"), QStringLiteral("Sync operations") },
        { QStringLiteral("memorytrim"), QStringLiteral("Trim memory") },
        { QStringLiteral("screenshot"), QStringLiteral("Screenshot") },
        { QStringLiteral("screenrecord"), QStringLiteral("Screen recording") },
        { QStringLiteral("livestream"), QStringLiteral("Play together") },
        { QStringLiteral("mediafolder"), QStringLiteral("Media gallery") },
        { QStringLiteral("airplanemode"), QStringLiteral("Airplane mode") },
        { QStringLiteral("rotate"), QStringLiteral("Rotate") },
        { QStringLiteral("shake"), QStringLiteral("Shake") },
        { QStringLiteral("setlocation"), QStringLiteral("Set location") },
        { QStringLiteral("mim"), QStringLiteral("Multi-instance manager") },
        { QStringLiteral("ecomode"), QStringLiteral("Eco mode") },
        { QStringLiteral("sidepanel"), QStringLiteral("Side panel") },
        { QStringLiteral("cltdatamode"), QStringLiteral("AIGames Collect Data Mode") },
        { QStringLiteral("chat"), QStringLiteral("Ana AI") },
        { QStringLiteral("discord"), QStringLiteral("Discord") },
        { QStringLiteral("more"), QStringLiteral("More (overflow menu)") },
    };
    return labels;
}

QString buttonLabel(const QString &key)
{
    if (buttonLabels().contains(key)) return buttonLabels().value(key);
    QString pretty = key;
    pretty.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!pretty.isEmpty()) pretty[0] = pretty.at(0).toUpper();
    return pretty;
}

// The strip's children that are buttons. The Repeater stands for every entry
// of pAllSidebarItems it renders; spacers and anything else are left alone.
QList<ButtonSlot> stripSlots(const QString &qml, const QString &masked, const QmlText::Object &strip,
                             const QStringList &skip = QStringList())
{
    QList<ButtonSlot> entries;
    for (const auto &child : childrenOf(masked, strip)) {
        ButtonSlot slot;
        slot.begin = child.nameBegin;
        slot.end = child.end;
        slot.object = child;
        objectId(qml, masked, child, &slot.id);
        const QString type = objectType(masked, child);
        if (fixedButtonKeys().contains(slot.id)) slot.key = fixedButtonKeys().value(slot.id);
        else if (type == QLatin1String("Repeater")) slot.key = QStringLiteral("repeater");
        if (slot.key.isEmpty() || skip.contains(slot.key)) continue;
        entries << slot;
    }
    return entries;
}

// The pAllSidebarItems entries the Repeater renders, in declaration order.
QList<ButtonSlot> itemSlots(const QString &qml, const QString &masked)
{
    QList<ButtonSlot> entries;
    for (const auto &element : QmlText::objects(masked, QStringLiteral("PlrSidebarElement"))) {
        ButtonSlot slot;
        slot.begin = element.nameBegin;
        slot.end = element.end;
        slot.object = element;
        slot.key = elementKey(qml, masked, element);
        if (slot.key.isEmpty()) continue;
        // Back/Home/Recent/Settings have their own buttons in the strip, so
        // stock keeps them out of the Repeater. A build that spells the member
        // with the older typo is read the same way.
        QmlText::Binding repeaterOnly;
        bool bound = false;
        for (const auto &member : { QStringLiteral("pShowViaRepeaterOnly"),
                                    QStringLiteral("pShowViaRepaterOnly") }) {
            bound = directBinding(masked, element.open, element.end, member, &repeaterOnly);
            if (bound) break;
        }
        if (bound
            && qml.mid(repeaterOnly.valueBegin, repeaterOnly.valueEnd - repeaterOnly.valueBegin)
                   .trimmed().startsWith(QLatin1String("false")))
            continue;
        entries << slot;
    }
    return entries;
}

QStringList slotKeys(const QList<ButtonSlot> &entries)
{
    QStringList keys;
    for (const auto &slot : entries) keys << slot.key;
    return keys;
}

// Give every slot the text of the slot that belongs in its place. Only whole
// objects move, so commas, line breaks and indentation stay untouched.
bool permuteSlots(const QString &qml, const QList<ButtonSlot> &entries, const QStringList &wanted,
                  QList<Edit> &edits)
{
    if (wanted.size() != entries.size()) return false;
    const QStringList current = slotKeys(entries);
    QList<QString> texts;
    for (const auto &slot : entries) texts << qml.mid(slot.begin, slot.end - slot.begin);
    for (int i = 0; i < entries.size(); ++i) {
        const int source = int(current.indexOf(wanted.at(i)));
        if (source < 0) return false;
        if (source == i) continue;
        addEdit(edits, entries.at(i).begin, entries.at(i).end - entries.at(i).begin, texts.at(source));
    }
    return true;
}

// Show or hide one object through `member`. A member the file already binds is
// wrapped, so no duplicate binding is introduced and the original expression
// comes back verbatim; a missing one is added on its own line, tagged so a
// later run knows it is ours and can take it away again.
bool setObjectVisible(const QString &qml, bool visible, const QmlText::Object &object,
                      const QString &member, QList<Edit> &edits)
{
    const QString masked = QmlText::mask(qml);
    QmlText::Binding binding;
    if (directBinding(masked, object.open, object.end, member, &binding)) {
        const QString value = qml.mid(binding.valueBegin, binding.valueEnd - binding.valueBegin);
        const bool owned = qml.mid(binding.valueEnd, kButtonHiddenTag.size()) == kButtonHiddenTag;
        if (visible) {
            if (owned) {
                // The member took a line of its own: take the indentation and
                // the line break with it, wherever the object put them.
                int begin = binding.nameBegin;
                while (begin > 0 && (qml.at(begin - 1) == QLatin1Char(' ')
                                     || qml.at(begin - 1) == QLatin1Char('\t'))) --begin;
                int end = binding.valueEnd + kButtonHiddenTag.size();
                if (qml.mid(end, 2) == QLatin1String("\r\n")) end += 2;
                else if (end < qml.size() && qml.at(end) == QLatin1Char('\n')) end += 1;
                addEdit(edits, begin, end - begin, QString());
                return true;
            }
            const QString prefix = kHiddenPrefix + QLatin1Char('(');
            if (value.startsWith(prefix) && value.endsWith(QLatin1Char(')')))
                addEdit(edits, binding.valueBegin, binding.valueEnd - binding.valueBegin,
                        value.mid(int(prefix.size()), value.size() - int(prefix.size()) - 1));
            return true;
        }
        if (owned || value.startsWith(kHiddenPrefix)) return true; // already off
        addEdit(edits, binding.valueBegin, binding.valueEnd - binding.valueBegin,
                kHiddenPrefix + QLatin1Char('(') + value + QLatin1Char(')'));
        return true;
    }
    if (!visible) {
        // A member with no value terminator would swallow the rest of a
        // single-line declaration, so it always gets a line of its own.
        int at = object.open + 1;
        if (qml.mid(at, 2) == QLatin1String("\r\n")) at += 2;
        else if (at < qml.size() && qml.at(at) == QLatin1Char('\n')) at += 1;
        else if (at < qml.size() && qml.at(at) == QLatin1Char('\r')) at += 1;
        const int nlAt = qml.indexOf(QLatin1Char('\n'), object.open);
        const QString indent = (nlAt > 0 && nlAt < object.end) ? lineIndentAt(qml, nlAt + 1)
                                                              : QStringLiteral("    ");
        addEdit(edits, at, 0, indent + member + QStringLiteral(": false") + kButtonHiddenTag
                                + lineEnding(qml));
    }
    return true;
}

// Whether `member` is currently switched off for this object.
bool objectHidden(const QString &qml, const QString &masked, const QmlText::Object &object,
                  const QString &member)
{
    QmlText::Binding binding;
    if (!directBinding(masked, object.open, object.end, member, &binding)) return false;
    return qml.mid(binding.valueBegin, binding.valueEnd - binding.valueBegin).trimmed()
        .startsWith(QLatin1String("false"));
}

void removeOrderMarker(const QString &qml, const QString &scope, QList<Edit> &edits)
{
    int begin = -1, end = -1;
    if (!orderMarker(qml, scope, nullptr, &begin, &end)) return;
    addEdit(edits, begin, end - begin, QString());
}

// The order the first tap on "apply" produces: the title bar's own navigation
// first (it is already there), then the buttons the user reaches for, then
// everything else in the stock order.
QStringList defaultButtonOrder()
{
    return { QStringLiteral("back"), QStringLiteral("home"), QStringLiteral("recent"),
             QStringLiteral("fullscreen"), QStringLiteral("volume"), QStringLiteral("installapk"),
             QStringLiteral("settings") };
}

// A request names one key per button of the whole row. The strip's own
// children and the Repeater's entries move independently, so it is split into
// those two sequences: the Repeater takes the place of its first entry, and
// every key the request leaves out keeps its stock place behind the rest.
void splitOrder(const QList<ButtonSlot> &strip, const QList<ButtonSlot> &items,
                const QStringList &requested, QStringList *stripOrder, QStringList *itemOrder)
{
    const QStringList stripKeys = slotKeys(strip);
    const QStringList itemKeys = slotKeys(items);
    QStringList wantedStrip;
    QStringList wantedItems;
    bool repeaterPlaced = !stripKeys.contains(QStringLiteral("repeater"));
    for (const QString &key : requested) {
        if (itemKeys.contains(key)) {
            if (!wantedItems.contains(key)) wantedItems << key;
            if (!repeaterPlaced) {
                wantedStrip << QStringLiteral("repeater");
                repeaterPlaced = true;
            }
        } else if (key != QLatin1String("repeater") && stripKeys.contains(key)) {
            if (!wantedStrip.contains(key)) wantedStrip << key;
        }
    }
    for (const QString &key : stripKeys)
        if (!wantedStrip.contains(key)) wantedStrip << key;
    for (const QString &key : itemKeys)
        if (!wantedItems.contains(key)) wantedItems << key;
    *stripOrder = wantedStrip;
    *itemOrder = wantedItems;
}

// fResetSidebar() gives the sidebar's own buttons their visibility on every
// resize, so a `visible:` binding declared on them would not survive. The ones
// that have to step aside are switched off at the end of that function, in one
// generated block that a later run replaces or removes verbatim.
bool hiddenRowEdits(const QString &qml, const QString &masked, const QStringList &ids, QList<Edit> &edits)
{
    const QString nl = lineEnding(qml);
    const int existing = qml.indexOf(kHiddenRowMarker);
    int lineStart = -1, lineEnd = -1;
    if (existing >= 0) {
        lineStart = qml.lastIndexOf(QLatin1Char('\n'), existing) + 1;
        const int firstNl = qml.indexOf(QLatin1Char('\n'), existing);
        const int secondNl = firstNl < 0 ? -1 : qml.indexOf(QLatin1Char('\n'), firstNl + 1);
        if (firstNl < 0 || secondNl < 0) return false;
        lineEnd = secondNl + 1;
    }
    QStringList unique;
    for (const QString &id : ids)
        if (!id.isEmpty() && !unique.contains(id)) unique << id;
    if (unique.isEmpty()) {
        if (lineStart >= 0) addEdit(edits, lineStart, lineEnd - lineStart, QString());
        return true;
    }
    int bodyBegin = 0, bodyEnd = 0;
    if (!functionSpan(masked, QStringLiteral("fResetSidebar"), &bodyBegin, &bodyEnd)) return true;
    const int bodyNl = qml.indexOf(QLatin1Char('\n'), bodyBegin);
    QString indent = QStringLiteral("        ");
    if (bodyNl > 0 && bodyNl < bodyEnd) indent = lineIndentAt(qml, bodyNl + 1);
    QString statements;
    for (const QString &id : unique) {
        if (!statements.isEmpty()) statements += QStringLiteral("; ");
        statements += id + QStringLiteral(".visible = false");
    }
    const QString block = indent + kHiddenRowMarker + unique.join(QLatin1Char(',')) + nl
        + indent + statements + nl;
    if (lineStart >= 0) {
        addEdit(edits, lineStart, lineEnd - lineStart, block);
        return true;
    }
    // Before the responsive call when the file has one: the overflow budget
    // has to be recomputed after the buttons this block switches off, and a
    // repaired helper then writes the same text as a fresh one.
    int insertAt = qml.lastIndexOf(QLatin1Char('\n'), bodyEnd - 1) + 1;
    const int callAt = qml.indexOf(kResponsiveCall, bodyBegin);
    if (callAt > 0 && callAt < bodyEnd)
        insertAt = qml.lastIndexOf(QLatin1Char('\n'), callAt) + 1;
    addEdit(edits, insertAt, 0, block);
    return true;
}

// pSettings already carries a Repeater entry that stock keeps out of the row
// (`pShowViaRepeaterOnly: false`) because the strip has a dedicated Settings
// button. That button can only sit before or after the whole Repeater, so the
// entry is switched on instead and Settings can take any place among the
// other buttons; the dedicated button steps aside while this is active.
// Whether the Settings entry is currently rendered through the Repeater.
bool settingsInRow(const QString &qml, const QString &masked)
{
    for (const auto &element : QmlText::objects(masked, QStringLiteral("PlrSidebarElement"))) {
        if (elementKey(qml, masked, element) != QLatin1String("settings")) continue;
        QmlText::Binding binding;
        bool bound = false;
        for (const auto &member : { QStringLiteral("pShowViaRepeaterOnly"),
                                    QStringLiteral("pShowViaRepaterOnly") }) {
            bound = directBinding(masked, element.open, element.end, member, &binding);
            if (bound) break;
        }
        if (!bound) return true; // no member: stock already renders it through the Repeater
        return qml.mid(binding.valueBegin, binding.valueEnd - binding.valueBegin).trimmed()
            .startsWith(QLatin1String("true"));
    }
    return false;
}

bool settingsFlipEdits(const QString &qml, const QString &masked, bool enabled, QList<Edit> &edits)
{
    for (const auto &element : QmlText::objects(masked, QStringLiteral("PlrSidebarElement"))) {
        if (elementKey(qml, masked, element) != QLatin1String("settings")) continue;
        QmlText::Binding binding;
        bool bound = false;
        for (const auto &member : { QStringLiteral("pShowViaRepeaterOnly"),
                                    QStringLiteral("pShowViaRepaterOnly") }) {
            bound = directBinding(masked, element.open, element.end, member, &binding);
            if (bound) break;
        }
        if (!bound) return true; // no member: stock already renders it through the Repeater
        const QString value = qml.mid(binding.valueBegin, binding.valueEnd - binding.valueBegin).trimmed();
        const bool owned = qml.mid(binding.valueEnd, kSettingsFlipTag.size() + 2)
                               .contains(kSettingsFlipTag);
        const bool flipped = value.startsWith(QLatin1String("true"));
        if (enabled && !flipped) {
            addEdit(edits, binding.valueBegin, binding.valueEnd - binding.valueBegin,
                    QStringLiteral("true") + kSettingsFlipTag);
        } else if (!enabled && owned) {
            addEdit(edits, binding.valueBegin, binding.valueEnd - binding.valueBegin
                        + kSettingsFlipTag.size(), QStringLiteral("false"));
        }
        return true;
    }
    return true;
}

// PlrSidebar.qml: the strip's own buttons and the Repeater entries in the
// requested order, with the requested ones hidden.
bool sidebarOrderEdits(const QString &qml, const QString &masked, bool enabled, bool flipped,
                       const RowRequest &request, QList<Edit> &edits)
{
    QmlText::Object root;
    if (!findSidebarRoot(qml, masked, &root, false)) return false;
    QmlText::Object stripObject;
    if (!findButtonStrip(qml, masked, root, &stripObject)) return false;

    const QList<ButtonSlot> allStrip = stripSlots(qml, masked, stripObject);
    const QList<ButtonSlot> items = itemSlots(qml, masked);

    // The dedicated Settings button is hidden while the entry is in the row,
    // so it takes no place in the order.
    QList<ButtonSlot> strip;
    for (const auto &slot : allStrip)
        if (!(flipped && slot.key == QLatin1String("settings"))) strip << slot;
    if (strip.isEmpty()) return false;

    QStringList stockStrip, stockItems;
    const bool haveStripMarker = orderMarker(qml, QStringLiteral("strip"), &stockStrip, nullptr, nullptr);
    const bool haveItemsMarker = orderMarker(qml, QStringLiteral("items"), &stockItems, nullptr, nullptr);

    if (!enabled) {
        if (haveStripMarker && stockStrip.size() == strip.size())
            permuteSlots(qml, strip, stockStrip, edits);
        if (haveItemsMarker && stockItems.size() == items.size())
            permuteSlots(qml, items, stockItems, edits);
        removeOrderMarker(qml, QStringLiteral("strip"), edits);
        removeOrderMarker(qml, QStringLiteral("items"), edits);
        return true;
    }

    // An older patch centred the row inside the title bar. The update lands on
    // the current shape instead of leaving the old anchor behind.
    const auto centre = [&](QList<Edit> &list) {
        QmlText::Binding binding;
        if (!directBinding(masked, stripObject.open, stripObject.end,
                           QStringLiteral("anchors.centerIn"), &binding)) return true;
        if (qml.mid(binding.valueBegin, binding.valueEnd - binding.valueBegin) != QLatin1String("parent"))
            return true;
        addEdit(list, binding.nameBegin, binding.valueEnd - binding.nameBegin,
                QStringLiteral("anchors.left: parent.left"));
        return true;
    };
    if (!centre(edits)) return false;

    const QString nl = lineEnding(qml);
    if (!haveStripMarker) {
        addEdit(edits, qml.lastIndexOf(QLatin1Char('\n'), strip.first().begin) + 1, 0,
                orderMarkerLine(QStringLiteral("strip"), slotKeys(strip), nl,
                                lineIndentAt(qml, strip.first().begin)));
    }
    if (!haveItemsMarker && !items.isEmpty()) {
        int at = qml.indexOf(QStringLiteral("pAllSidebarItems"));
        at = at < 0 ? items.first().begin : qml.lastIndexOf(QLatin1Char('\n'), at) + 1;
        addEdit(edits, at, 0, orderMarkerLine(QStringLiteral("items"), slotKeys(items), nl,
                                              lineIndentAt(qml, at)));
    }

    QStringList wantedStrip, wantedItems;
    splitOrder(strip, items, request.order.isEmpty() ? defaultButtonOrder() : request.order,
               &wantedStrip, &wantedItems);
    if (wantedStrip != slotKeys(strip) && !permuteSlots(qml, strip, wantedStrip, edits)) return false;
    if (wantedItems != slotKeys(items) && !permuteSlots(qml, items, wantedItems, edits)) return false;
    return true;
}

// PlrSidebar.qml, second pass: which buttons the row does not show. Runs after
// the order, so the declarations hidden here are the ones that ended up in the
// row and not the ones that were there before the move.
bool sidebarVisibilityEdits(const QString &qml, const QString &masked, bool enabled, bool flipped,
                            const RowRequest &request, QList<Edit> &edits)
{
    QmlText::Object root;
    if (!findSidebarRoot(qml, masked, &root, false)) return false;
    QmlText::Object strip;
    if (!findButtonStrip(qml, masked, root, &strip)) return false;
    const QList<ButtonSlot> allStrip = stripSlots(qml, masked, strip);
    const QList<ButtonSlot> items = itemSlots(qml, masked);

    if (!enabled) {
        hiddenRowEdits(qml, masked, QStringList(), edits);
        for (const auto &slot : items)
            if (!setObjectVisible(qml, true, slot.object, QStringLiteral("pVisible"), edits))
                return false;
        return true;
    }

    // The title bar already shows Back/Home/Recent, so the strip's own copies
    // would be duplicates of the same three buttons.
    QStringList hiddenIds;
    for (const auto &slot : allStrip) {
        const bool duplicate = slot.key == QLatin1String("back") || slot.key == QLatin1String("home")
            || slot.key == QLatin1String("recent");
        const bool wanted = request.hidden.contains(slot.key)
            || (flipped && slot.key == QLatin1String("settings"));
        if (duplicate || wanted) hiddenIds << slot.id;
    }
    if (!hiddenRowEdits(qml, masked, hiddenIds, edits)) return false;
    for (const auto &slot : items)
        if (!setObjectVisible(qml, !request.hidden.contains(slot.key), slot.object,
                              QStringLiteral("pVisible"), edits))
            return false;
    return true;
}

// The title bar's own Back/Home/Recent, in the order they are declared.
QList<ButtonSlot> topbarSlots(const QString &qml, const QString &masked)
{
    QList<ButtonSlot> entries;
    QmlText::Object row;
    for (const auto &object : QmlText::objects(masked, QStringLiteral("Rectangle"))) {
        if (row.nameBegin < 0 || object.nameBegin < row.nameBegin) row = object;
    }
    if (row.nameBegin < 0) return entries;
    for (const auto &object : QmlText::objects(masked, QStringLiteral("UiRowLayout"))) {
        QString id;
        if (objectId(qml, masked, object, &id) && id == QLatin1String("iTopbarRow")) row = object;
    }
    for (const auto &object : QmlText::objects(masked, QStringLiteral("UiImageButton"))) {
        QString id;
        if (!objectId(qml, masked, object, &id) || !fixedButtonKeys().contains(id)) continue;
        const QString key = fixedButtonKeys().value(id);
        if (key != QLatin1String("back") && key != QLatin1String("home")
            && key != QLatin1String("recent")) continue;
        if (object.nameBegin <= row.open || object.end > row.end) continue;
        entries << ButtonSlot { key, id, object.nameBegin, object.end, object };
    }
    std::sort(entries.begin(), entries.end(), [](const ButtonSlot &a, const ButtonSlot &b) {
        return a.begin < b.begin;
    });
    return entries;
}

// PlrTopbar.qml: the title bar's own Back/Home/Recent in the requested order.
// They stay in the row they already live in; the width manager above them
// re-lays that row out on its own.
bool topbarOrderEdits(const QString &qml, const QString &masked, bool enabled,
                      const QStringList &requested, QList<Edit> &edits)
{
    const QList<ButtonSlot> entries = topbarSlots(qml, masked);
    if (entries.size() < 2) {
        if (!enabled) removeOrderMarker(qml, QStringLiteral("topbar"), edits);
        return true;
    }

    QStringList stock;
    const bool haveMarker = orderMarker(qml, QStringLiteral("topbar"), &stock, nullptr, nullptr);
    if (!enabled) {
        if (haveMarker && stock.size() == entries.size()) permuteSlots(qml, entries, stock, edits);
        removeOrderMarker(qml, QStringLiteral("topbar"), edits);
        return true;
    }
    if (!haveMarker) {
        addEdit(edits, qml.lastIndexOf(QLatin1Char('\n'), entries.first().begin) + 1, 0,
                orderMarkerLine(QStringLiteral("topbar"), slotKeys(entries), lineEnding(qml),
                                lineIndentAt(qml, entries.first().begin)));
    }
    QStringList wanted;
    for (const QString &key : requested.isEmpty() ? defaultButtonOrder() : requested)
        if (slotKeys(entries).contains(key) && !wanted.contains(key)) wanted << key;
    for (const QString &key : slotKeys(entries))
        if (!wanted.contains(key)) wanted << key;
    if (wanted != slotKeys(entries) && !permuteSlots(qml, entries, wanted, edits)) return false;
    return true;
}

// The title bar has to know where its own Back/Home/Recent end, so the moved
// row can start right there. The expression only names the buttons this build
// has.
bool topbarPositionEdits(const QString &qml, const QString &masked, bool enabled, QList<Edit> &edits)
{
    const QString nl = lineEnding(qml);
    if (!enabled) {
        const QRegularExpression marker(QStringLiteral(
            "(\\r?\\n)[ \\t]*property real pBSISidebarX:[^\\r\\n]*// BSI CUSTOMIZER: sidebar row position"));
        const auto match = marker.match(qml);
        if (!match.hasMatch()) return true; // never added
        addEdit(edits, int(match.capturedStart()), int(match.capturedLength()), QString());
        return true;
    }
    if (qml.contains(QLatin1String("pBSISidebarX"))) return true; // already there
    QmlText::Object root;
    for (const auto &object : QmlText::objects(masked, QStringLiteral("Rectangle"))) {
        if (root.nameBegin < 0 || object.nameBegin < root.nameBegin) root = object;
    }
    if (root.nameBegin < 0) {
        for (const auto &object : QmlText::objects(masked, QStringLiteral("Item"))) {
            if (root.nameBegin < 0 || object.nameBegin < root.nameBegin) root = object;
        }
    }
    if (root.nameBegin < 0) return false;
    QStringList terms;
    for (const auto &object : QmlText::objects(masked, QStringLiteral("UiImageButton"))) {
        QString id;
        if (!objectId(qml, masked, object, &id)) continue;
        if (id != QLatin1String("iBackBtn") && id != QLatin1String("iHomeBtn")
            && id != QLatin1String("iRecentBtn")) continue;
        terms << QStringLiteral("%1.x + %1.width").arg(id);
    }
    const QString expression = terms.isEmpty()
        ? QStringLiteral("44")
        : QStringLiteral("Math.max(%1) + 10").arg(terms.join(QStringLiteral(", ")));
    addEdit(edits, root.open + 1, 0,
            nl + QStringLiteral("    property real pBSISidebarX: ") + expression
                + QStringLiteral(" // BSI CUSTOMIZER: sidebar row position"));
    return true;
}

// Turn the vertical button strip into a centered horizontal row (and back).
// This is the whole title-bar move: only the strip's own layout is rewritten.
// `stockOverflowOnly` records that the build keeps its own overflow code.
bool sidebarRowEdits(QString &qml, bool enabled, QList<Edit> &edits, bool stockOverflowOnly = false)
{
    const QString masked = QmlText::mask(qml);
    QmlText::Object root;
    if (!findSidebarRoot(qml, masked, &root, false)) return false;
    QmlText::Object strip;
    if (!findButtonStrip(qml, masked, root, &strip)) return false;
    const QString type = qml.mid(strip.nameBegin, strip.open - strip.nameBegin).trimmed();
    // 5.10 puts a Collapse button at the start of the sidebar. It has no
    // meaning once the side strip is removed (and could hide the moved row).
    for (const auto &button : QmlText::objects(masked, QStringLiteral("UiImageButton"))) {
        if (button.open <= strip.open || button.end > strip.end
            || !directChild(masked, strip.open, button.nameBegin)) continue;
        QmlText::Binding id;
        if (!directBinding(masked, button.open, button.end, QStringLiteral("id"), &id)
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
            if (directBinding(masked, button.open, button.end, QStringLiteral("visible"), &visible))
                return false;
            addEdit(edits, button.open + 1, 0, currentMarker);
        } else if (qml.mid(button.open + 1, kCollapseHiddenCurrent.size()) == kCollapseHiddenCurrent) {
            if (qml.mid(button.open + 1, currentMarker.size()) != currentMarker) return false;
            addEdit(edits, button.open + 1, currentMarker.size(), QString());
        } else if (qml.mid(button.open + 1, earlierMarker.size()) == earlierMarker) {
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
        if (type == QLatin1String("UiColumnLayout")) {
            addEdit(edits, strip.nameBegin, int(type.size()), QStringLiteral("UiRowLayout"));
        } else if (type == QLatin1String("Column")) {
            addEdit(edits, strip.nameBegin, int(type.size()), QStringLiteral("Row"));
        }
        // The row starts at the left edge of its title-bar host (the host itself
        // begins after Back/Home/Recent), not in the middle of the bar. An older
        // patch centred it; that shape is accepted and moved to the same place.
        QmlText::Binding fill;
        if (directBinding(masked, strip.open, strip.end, QStringLiteral("anchors.fill"), &fill)
            && qml.mid(fill.valueBegin, fill.valueEnd - fill.valueBegin) == QLatin1String("parent")) {
            addEdit(edits, fill.nameBegin, fill.valueEnd - fill.nameBegin,
                    QStringLiteral("anchors.left: parent.left"));
        } else {
            QmlText::Binding centre;
            if (directBinding(masked, strip.open, strip.end, QStringLiteral("anchors.centerIn"), &centre)
                && qml.mid(centre.valueBegin, centre.valueEnd - centre.valueBegin) == QLatin1String("parent")) {
                addEdit(edits, centre.nameBegin, centre.valueEnd - centre.nameBegin,
                        QStringLiteral("anchors.left: parent.left"));
            }
        }
        // Suppress the row inside the original (now zero-width) sidebar until
        // it is mounted into the title-bar host, which has a real width.
        addEdit(edits, strip.open + 1, 0,
                kRowHidden + (stockOverflowOnly ? kNoSmartMarker : QString()));
    } else {
        if (type == QLatin1String("UiRowLayout")) {
            addEdit(edits, strip.nameBegin, int(type.size()), QStringLiteral("UiColumnLayout"));
        } else if (type == QLatin1String("Row")) {
            addEdit(edits, strip.nameBegin, int(type.size()), QStringLiteral("Column"));
        }
        QmlText::Binding left;
        if (directBinding(masked, strip.open, strip.end, QStringLiteral("anchors.left"), &left)
            && qml.mid(left.valueBegin, left.valueEnd - left.valueBegin) == QLatin1String("parent.left")) {
            addEdit(edits, left.nameBegin, left.valueEnd - left.nameBegin,
                    QStringLiteral("anchors.fill: parent"));
        } else {
            // Older patches centred the strip instead of aligning it left.
            QmlText::Binding centre;
            if (directBinding(masked, strip.open, strip.end, QStringLiteral("anchors.centerIn"), &centre)
                && qml.mid(centre.valueBegin, centre.valueEnd - centre.valueBegin) == QLatin1String("parent")) {
                addEdit(edits, centre.nameBegin, centre.valueEnd - centre.nameBegin,
                        QStringLiteral("anchors.fill: parent"));
            }
        }
        int at = qml.indexOf(kRowHidden + kNoSmartMarker, strip.open);
        int length = kRowHidden.size() + kNoSmartMarker.size();
        if (at < 0) {
            at = qml.indexOf(kRowHidden, strip.open);
            length = kRowHidden.size();
        }
        if (at < 0 || at + length > strip.end) return false;
        addEdit(edits, at, length, QString());
    }
    // The player's own budget helper follows the axis too: after the move the
    // strip is as wide as the title bar and only 32px tall, so a stock budget
    // measured in height would hide every button. Optional: a strip without
    // that helper has nothing to convert.
    int bodyBegin = 0, bodyEnd = 0;
    if (functionSpan(masked, QStringLiteral("fResetSidebar"), &bodyBegin, &bodyEnd)) {
        const QString from = enabled ? QStringLiteral("iSidebar.height")
                                     : QStringLiteral("iSidebar.width");
        const QString to = enabled ? QStringLiteral("iSidebar.width")
                                   : QStringLiteral("iSidebar.height");
        int at = bodyBegin;
        while ((at = qml.indexOf(from, at)) >= 0 && at < bodyEnd) {
            addEdit(edits, at, from.size(), to);
            at += from.size();
        }
    }
    // The overflow budget is measured in buttons of 32px; in a horizontal
    // strip that budget is the width, and the row re-lays out when it changes.
    // Only the root's own handler may be renamed - a child object's
    // onHeightChanged must stay untouched. A build that has no stock handler
    // gets a tagged one, so the restore still removes exactly what was added.
    const QString axisNl = lineEnding(qml);
    QmlText::Binding handler;
    if (enabled) {
        if (directBinding(masked, root.open, root.end, QStringLiteral("onHeightChanged"), &handler)) {
            addEdit(edits, handler.nameBegin, handler.nameEnd - handler.nameBegin,
                    QStringLiteral("onWidthChanged"));
        } else if (!directBinding(masked, root.open, root.end,
                                  QStringLiteral("onWidthChanged"), &handler)) {
            addEdit(edits, root.open + 1, 0,
                    axisNl + QStringLiteral("    onWidthChanged: fResetSidebar()") + kAxisHandler);
        }
    } else {
        // Prefer removing the handler this file added over renaming it back:
        // the inserted marker line includes the member, so renaming would
        // leave a duplicate stock-looking handler behind.
        const QString inserted = axisNl + QStringLiteral("    onWidthChanged: fResetSidebar()")
            + kAxisHandler;
        const int at = qml.indexOf(inserted, root.open);
        if (at >= 0 && at + inserted.size() <= root.end) {
            addEdit(edits, at, inserted.size(), QString());
        } else if (directBinding(masked, root.open, root.end,
                                 QStringLiteral("onWidthChanged"), &handler)) {
            addEdit(edits, handler.nameBegin, handler.nameEnd - handler.nameBegin,
                    QStringLiteral("onHeightChanged"));
        } else if (!directBinding(masked, root.open, root.end,
                                  QStringLiteral("onHeightChanged"), &handler)) {
            return false;
        }
    }
    return true;
}

bool sidebarEdits(QString &qml, bool enabled, QList<Edit> &edits)
{
    const QString masked = QmlText::mask(qml);
    if (!sidebarRowEdits(qml, enabled, edits)) return false;
    // If the separate responsive option is already on, keep its overflow
    // budget on the new axis as the original sidebar is moved/restored.
    const QString from = enabled ? QStringLiteral("iSidebar.height")
                                 : QStringLiteral("iSidebar.width");
    const QString to = enabled ? QStringLiteral("iSidebar.width")
                               : QStringLiteral("iSidebar.height");
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
    // Where the moved row starts: right after the title bar's own buttons.
    if (!topbarPositionEdits(qml, masked, enabled, edits)) return false;
    // Their order follows the same request as the rest of the row.
    if (enabled && !topbarOrderEdits(qml, masked, enabled, QStringList(), edits)) return false;
    // The sidebar toggle has nothing left to toggle, and leaving it live would
    // still resize the window by pSidebarWidth on click, so it is hidden.
    // Builds that do not carry the toggle (or name its id differently) are not
    // refused any more: there is simply nothing to hide.
    QmlText::Object toggle;
    bool found = false;
    for (const auto &object : QmlText::objects(masked, QStringLiteral("UiImageButton"))) {
        QmlText::Binding id;
        if (directBinding(masked, object.open, object.end, QStringLiteral("id"), &id)
            && qml.mid(id.valueBegin, id.valueEnd - id.valueBegin)
                   == QLatin1String("iSidebarShowBtn")) {
            toggle = object;
            found = true;
            break;
        }
        if (qml.mid(object.open, object.end - object.open)
                .contains(QLatin1String("id: iSidebarShowBtn"))) {
            toggle = object;
            found = true;
            break;
        }
    }
    if (found) {
        // Some builds already bind the toggle's visibility (5.10: `visible:
        // !iSidebar.visible`). A second `visible:` member would be a duplicate
        // binding, so wrap the existing expression in `false && (...)` instead
        // and unwrap it verbatim when restoring. Builds without one get a
        // plain member.
        QmlText::Binding toggleVisible;
        const bool hasVisible =
            directBinding(masked, toggle.open, toggle.end, QStringLiteral("visible"), &toggleVisible);
        const auto toggleValue = [&] {
            return qml.mid(toggleVisible.valueBegin, toggleVisible.valueEnd - toggleVisible.valueBegin);
        };
        if (enabled) {
            if (hasVisible) {
                if (toggleValue().startsWith(kHiddenPrefix)) return false;
                addEdit(edits, toggleVisible.valueBegin,
                        toggleVisible.valueEnd - toggleVisible.valueBegin,
                        kHiddenPrefix + QLatin1Char('(') + toggleValue() + QLatin1Char(')'));
            } else {
                addEdit(edits, toggle.open + 1, 0, kHidden);
            }
        } else if (qml.mid(toggle.open + 1, kHidden.size()) == kHidden) {
            addEdit(edits, toggle.open + 1, kHidden.size(), QString());
        } else if (hasVisible) {
            const QString current = toggleValue();
            if (current.startsWith(kHiddenPrefix + QLatin1Char('('))
                && current.endsWith(QLatin1Char(')'))) {
                addEdit(edits, toggleVisible.valueBegin,
                        toggleVisible.valueEnd - toggleVisible.valueBegin,
                        current.mid(int(kHiddenPrefix.size()) + 1,
                                    current.size() - int(kHiddenPrefix.size()) - 2));
            } else if (current == QLatin1String("false") || current == QLatin1String("true")) {
                // Stock visibility: nothing this tweak changed.
            } else {
                return false; // unknown binding: do not leave it half-hidden
            }
        }
    }
    // The title bar's own Back/Home/Recent live in pNonFixedTopbarItems and
    // are toggled imperatively; switching their element's pVisible off is the
    // one hook that survives the width manager. The moved sidebar row carries
    // its own copies centered, so the duplicates step aside. A build that has
    // renamed, removed or re-bound one of these entries keeps working: only
    // the entries that are still in the expected stock state are edited.
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
            if (!directBinding(masked, object.open, object.end, wanted.second, &visible)) {
                handled.insert(wanted.first);
                continue;
            }
            // The title bar keeps its own Back/Home/Recent: they are the first
            // three buttons of the row the sidebar continues. An older patch
            // (which centred the moved copies over them) switched these off,
            // and that is undone here in either direction.
            const QString current = qml.mid(visible.valueBegin, visible.valueEnd - visible.valueBegin);
            if (current == QLatin1String("false"))
                addEdit(edits, visible.valueBegin, visible.valueEnd - visible.valueBegin,
                        QStringLiteral("true"));
            handled.insert(wanted.first);
        }
    }
    return true;
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

    // The smart overflow helper is an upgrade, not a precondition. Probe it on
    // a copy: builds without the kebab button, Repeater model or popup anchors
    // still get the title-bar move and keep their own overflow code.
    bool smart = responsive;
    if (enabled && !responsive) {
        QString probe = qml;
        QList<Edit> row, helper;
        smart = sidebarRowEdits(probe, true, row) && applyEdits(probe, row)
            && responsiveEdits(probe, true, true, helper);
    }
    if (!smart) {
        edits.clear();
        return sidebarRowEdits(qml, enabled, edits, true);
    }
    if (!sidebarEdits(qml, enabled, edits)) return false;
    if (enabled && !responsive) {
        // Move the row in the caller's working text, then plan the helper
        // against the moved strip (the caller re-applies both halves).
        if (!applyEdits(qml, edits)) return false;
        edits.clear();
        if (!responsiveEdits(qml, true, true, edits)) return false;
    }
    return true;
}

bool collectTopbarEdits(QString &qml, bool enabled, SidebarLayout, QList<Edit> &edits)
{
    return topbarEdits(qml, enabled, edits);
}

// The row's order and hidden buttons. Three passes over one document, each on
// the text the previous one produced: Settings joins (or leaves) the row, the
// declarations move, and only then are the buttons the player's own code shows
// again switched off - a moved button must be the one that gets hidden.
bool rowOrderDoc(QString &qml, const QString &name, bool enabled, const RowRequest &request)
{
    if (name == QLatin1String("PlrTopbar.qml")) {
        QList<Edit> edits;
        if (!topbarOrderEdits(qml, QmlText::mask(qml), enabled, request.order, edits)) return false;
        return applyEdits(qml, edits);
    }
    if (name != QLatin1String("PlrSidebar.qml")) return true;
    const auto pass = [&](QList<Edit> &edits) { return applyEdits(qml, edits); };
    QList<Edit> edits;
    if (!enabled) {
        // Restore the declarations first - the recorded order still has the
        // Settings entry the row was using - then the flip that put it there.
        const bool flipped = settingsInRow(qml, QmlText::mask(qml));
        if (!sidebarOrderEdits(qml, QmlText::mask(qml), false, flipped, request, edits)) return false;
        if (!pass(edits)) return false;
        edits.clear();
        if (!sidebarVisibilityEdits(qml, QmlText::mask(qml), false, false, request, edits)) return false;
        if (!pass(edits)) return false;
        edits.clear();
        if (!settingsFlipEdits(qml, QmlText::mask(qml), false, edits)) return false;
        return pass(edits);
    }
    if (!settingsFlipEdits(qml, QmlText::mask(qml), true, edits)) return false;
    if (!pass(edits)) return false;
    edits.clear();
    const bool flipped = settingsInRow(qml, QmlText::mask(qml));
    if (!sidebarOrderEdits(qml, QmlText::mask(qml), true, flipped, request, edits)) return false;
    if (!pass(edits)) return false;
    edits.clear();
    if (!sidebarVisibilityEdits(qml, QmlText::mask(qml), true, flipped, request, edits)) return false;
    return pass(edits);
}

// Both files are read from `changed`, so this always works on the current text.
// `strict` is for the explicit order editor: a player whose declarations are
// not in the expected shape is then refused instead of silently left alone,
// while the layout pass itself keeps working on such a build.
bool writeRowOrder(QByteArray &changed, bool enabled, const RowRequest &request, QString *error,
                   bool strict = false)
{
    for (const auto &name : { QStringLiteral("PlrSidebar.qml"), QStringLiteral("PlrTopbar.qml") }) {
        Doc doc;
        if (!findDoc(changed, name, &doc, error)) return false;
        if (!rowOrderDoc(doc.qml, name, enabled, request)) {
            if (!strict) continue;
            if (error)
                *error = QStringLiteral("%1 does not declare the buttons of the row in the "
                                        "expected shape; no file was written.").arg(name);
            return false;
        }
        const QByteArray header = doc.bom ? QByteArray::fromHex("efbbbf") : QByteArray();
        if (!QtResources::replaceResource(changed, doc.file.entryOff, doc.link,
                                          header + doc.qml.toUtf8(), error)) return false;
    }
    return true;
}

// ------------------------------------------------- clean player UI helpers

// Buttons that must survive the clean-up: player navigation, the product
// logo and title, and the window controls (without them a frameless window
// cannot be closed or resized).
const QStringList &cleanKeepIds()
{
    static const QStringList ids {
        QStringLiteral("iProductLogo"), QStringLiteral("iBstTxt"),
        QStringLiteral("iVersionImgLayout"), QStringLiteral("iTitleLayout"),
        QStringLiteral("iBackBtn"), QStringLiteral("iHomeBtn"), QStringLiteral("iRecentBtn"),
        QStringLiteral("iMinmizeBtn"), QStringLiteral("iMinimizeBtn"),
        QStringLiteral("iMaximizeBtn"), QStringLiteral("iFixedResizeBtn"),
        QStringLiteral("iRestoreBtn"), QStringLiteral("iCloseBtn")
    };
    return ids;
}

// Everything else the two known player generations draw in the title bar or
// the sidebar strip. Ids absent from a build are simply skipped.
const QStringList &cleanHideIds()
{
    static const QStringList ids {
        QStringLiteral("iSidebarShowBtn"), QStringLiteral("iHelpBtn"),
        // MSI 5.9 spells the quest button `iQuestbtn`.
        QStringLiteral("iCfgBtn"), QStringLiteral("iQuestBtn"), QStringLiteral("iQuestbtn"),
        QStringLiteral("iRewardsCenterBtn"), QStringLiteral("iRawModeWarningBtn"),
        QStringLiteral("iNowbuxBtn"), QStringLiteral("iNowggAccountBtn"),
        QStringLiteral("iDisablePinToTopBtn"), QStringLiteral("iTopbarIconsList"),
        QStringLiteral("iSyncTopbarControl"), QStringLiteral("iMacroTopbarControl"),
        QStringLiteral("iMacroRowLayout"), QStringLiteral("iMacroDetail"),
        QStringLiteral("iScreenRecordingTopbarControl")
    };
    return ids;
}

// The object that directly declares `id: <id>`.
bool findObjectById(const QString &qml, const QString &masked, const QString &id,
                    QmlText::Object *out)
{
    const QRegularExpression rx(QStringLiteral("\\bid\\s*:\\s*")
                                + QRegularExpression::escape(id) + QStringLiteral("\\b"));
    auto iter = rx.globalMatch(masked);
    while (iter.hasNext()) {
        const auto match = iter.next();
        QmlText::Object object;
        if (!enclosingObject(masked, int(match.capturedStart()), &object)) continue;
        QmlText::Binding binding;
        if (!directBinding(masked, object.open, object.end, QStringLiteral("id"), &binding))
            continue;
        if (qml.mid(binding.valueBegin, binding.valueEnd - binding.valueBegin) != id) continue;
        *out = object;
        return true;
    }
    return false;
}

// Where a `visible: false` member can be inserted, and the exact text written
// there. A multi-line object gets the line at its first member's indentation
// (no extra blank line); a one-line object gets it wrapped in its own line so
// the line comment cannot swallow the members that follow it.
bool cleanHiddenSite(const QString &qml, const QmlText::Object &object, int *at, QString *text)
{
    const QString nl = lineEnding(qml);
    int probe = object.open + 1;
    while (probe < qml.size()
           && (qml.at(probe) == QLatin1Char(' ') || qml.at(probe) == QLatin1Char('\t')))
        ++probe;
    const bool firstMemberOnNewLine = probe < object.end
        && (qml.at(probe) == QLatin1Char('\n') || qml.at(probe) == QLatin1Char('\r'));
    if (firstMemberOnNewLine) {
        const int newline = qml.indexOf(QLatin1Char('\n'), probe);
        if (newline < 0 || newline >= object.end) return false;
        const int lineStart = newline + 1;
        QString indent;
        int i = lineStart;
        while (i < qml.size()
               && (qml.at(i) == QLatin1Char(' ') || qml.at(i) == QLatin1Char('\t')))
            indent += qml.at(i++);
        *at = lineStart;
        *text = indent + kCleanHiddenLine + nl;
        return true;
    }
    // The object shares its opening line with its members (`UiButton { id: …`),
    // so a newline insertion would split that line. Add the member inline with
    // a separator and a block comment instead; cleanShowObject() removes it by
    // the same byte-exact site match.
    *at = object.open + 1;
    *text = QStringLiteral(" visible: false /* BSI CUSTOMIZER: clean player UI */;");
    return true;
}

bool cleanHideObject(const QString &qml, const QmlText::Object &object, QList<Edit> &edits)
{
    const QString masked = QmlText::mask(qml);
    QmlText::Binding visible;
    if (directBinding(masked, object.open, object.end, QStringLiteral("visible"), &visible)) {
        const QString value = qml.mid(visible.valueBegin, visible.valueEnd - visible.valueBegin);
        if (value.startsWith(kCleanHidePrefix)) return false; // already hidden by us
        addEdit(edits, visible.valueBegin, visible.valueEnd - visible.valueBegin,
                kCleanHidePrefix + value + QStringLiteral(")"));
    } else {
        int at = 0;
        QString text;
        if (!cleanHiddenSite(qml, object, &at, &text)) return false;
        addEdit(edits, at, 0, text);
    }
    return true;
}

bool cleanShowObject(const QString &qml, const QmlText::Object &object, QList<Edit> &edits)
{
    const QString masked = QmlText::mask(qml);
    int at = 0;
    QString inserted;
    if (!cleanHiddenSite(qml, object, &at, &inserted)) return false;
    // Only the line this tweak wrote itself counts: searching further would
    // find the same marker inserted into a nested object (iMacroDetail inside
    // iMacroRowLayout) and remove it twice.
    if (qml.mid(at, inserted.size()) == inserted) {
        addEdit(edits, at, inserted.size(), QString());
        return true;
    }
    QmlText::Binding visible;
    if (directBinding(masked, object.open, object.end, QStringLiteral("visible"), &visible)) {
        const QString value = qml.mid(visible.valueBegin, visible.valueEnd - visible.valueBegin);
        if (value.startsWith(kCleanHidePrefix) && value.endsWith(QLatin1Char(')'))) {
            addEdit(edits, visible.valueBegin, visible.valueEnd - visible.valueBegin,
                    value.mid(kCleanHidePrefix.size(),
                              value.size() - kCleanHidePrefix.size() - 1));
            return true;
        }
    }
    return false;
}

// The clean player UI leaves the sidebar alone: the strip is only removed by
// moving it into the title bar ("Sidebar buttons to the top bar"), which is a
// separate option. An earlier build of this tool did hide `iSidebar` and
// collapsed its width from here, so both directions undo those edits: updating
// such a patch must not leave an empty column behind.
bool cleanPlrEdits(QString &qml, bool enabled, QList<Edit> &edits)
{
    Q_UNUSED(enabled); // only ever undoes the old strip hiding, in both directions

    const QString masked = QmlText::mask(qml);
    QmlText::Object target;
    for (const auto &instance : QmlText::objects(masked, QStringLiteral("PlrSidebar"))) {
        QmlText::Binding id;
        if (directBinding(masked, instance.open, instance.end, QStringLiteral("id"), &id)
            && qml.mid(id.valueBegin, id.valueEnd - id.valueBegin) == QLatin1String("iSidebar")) {
            target = instance;
            break;
        }
    }
    // No sidebar instance is fine: nothing was hidden, nothing to restore.
    if (target.open < 0) return true;

    // 5.10 / MSI 5.9 declare the sidebar instance as a layout child and size it
    // with Layout.preferredWidth instead of width; newer builds use width.
    QList<QPair<QString, QmlText::Binding>> sizes;
    for (const auto &name : { QStringLiteral("width"), QStringLiteral("Layout.preferredWidth") }) {
        QmlText::Binding binding;
        if (directBinding(masked, target.open, target.end, name, &binding))
            sizes << qMakePair(name, binding);
    }

    bool changed = false;
    for (const auto &size : sizes) {
        const auto &binding = size.second;
        const QString value = qml.mid(binding.valueBegin, binding.valueEnd - binding.valueBegin);
        if (value != QLatin1String("0")) continue;
        QString original;
        int tagEnd = -1;
        if (!taggedAfter(qml, binding.valueBegin, kCleanWidthTag, &original, &tagEnd)) continue;
        addEdit(edits, binding.valueBegin, tagEnd - binding.valueBegin, original);
        changed = true;
    }
    if (cleanShowObject(qml, target, edits)) changed = true;
    // Nothing of ours in plr.qml is the normal case: the clean-up only edits the
    // sidebar and the title bar, and this function exists to undo an old
    // strip-hiding patch. It must therefore report success, not "changed".
    return true;
}

// PlrSidebar.qml is likewise only touched to undo the earlier strip hiding.
// The sidebar buttons the clean keeps: the five the player is actually used
// with. Everything else in the strip (screenshot, screen record, media folder,
// rotate, location, shake, lock cursor, macro, sync, eco mode, instance
// manager, side panel, live stream, chat, settings and the strip's own
// Back / Home / Recent) is switched off.
const QStringList &cleanKeepSidebarIds()
{
    static const QStringList ids {
        QStringLiteral("pFullscreen"), QStringLiteral("pVolume"),
        QStringLiteral("pGameControl"), QStringLiteral("pMemoryTrim"),
        QStringLiteral("pInstallApk")
    };
    return ids;
}

// The `pElementId: pVolume` value of a PlrSidebarElement, read from its body.
QString sidebarElementId(const QString &qml, const QmlText::Object &object)
{
    static const QRegularExpression rx(
        QStringLiteral("\\bpElementId\\s*:\\s*([A-Za-z_][A-Za-z0-9_]*)"));
    const auto match = rx.match(qml.mid(object.open, object.end - object.open));
    return match.hasMatch() ? match.captured(1) : QString();
}

// Where an inserted `pIsFeatureEnable: false` line goes: directly under the
// element's opening line, indented one level deeper. The site stays put while
// the line is there, so the undo can match it byte-for-byte.
bool sidebarMemberSite(const QString &qml, const QmlText::Object &object, int *at, QString *text)
{
    const int openLineEnd = qml.indexOf(QLatin1Char('\n'), object.open);
    if (openLineEnd < 0 || openLineEnd >= object.end) return false;
    const int typeLineStart = qml.lastIndexOf(QLatin1Char('\n'), object.open) + 1;
    QString indent;
    int i = typeLineStart;
    while (i < qml.size() && (qml.at(i) == QLatin1Char(' ') || qml.at(i) == QLatin1Char('\t')))
        indent += qml.at(i++);
    indent += QStringLiteral("    ");
    *at = openLineEnd + 1;
    *text = indent + kSidebarCleanLine + lineEnding(qml);
    return true;
}

// Hide (or bring back) every sidebar button outside the keep list.
bool cleanSidebarButtonEdits(const QString &qml, bool enabled, QList<Edit> &edits)
{
    const QString masked = QmlText::mask(qml);
    bool changed = false;
    for (const auto &element : QmlText::objects(masked, QStringLiteral("PlrSidebarElement"))) {
        const QString id = sidebarElementId(qml, element);
        if (id.isEmpty() || cleanKeepSidebarIds().contains(id)) continue;
        QmlText::Binding feature;
        const bool hasFeature = directBinding(masked, element.open, element.end,
                                              QStringLiteral("pIsFeatureEnable"), &feature);
        if (enabled) {
            if (hasFeature) {
                const QString value = qml.mid(feature.valueBegin,
                                              feature.valueEnd - feature.valueBegin);
                // `false` is either our own edit or a removal the user applied
                // through the Ads/UI list: leave it alone either way.
                if (value.startsWith(QLatin1String("false"))) continue;
                addEdit(edits, feature.valueBegin, feature.valueEnd - feature.valueBegin,
                        withOriginal(QStringLiteral("false"), value, kSidebarCleanTag));
                changed = true;
                continue;
            }
            int at = 0;
            QString text;
            if (!sidebarMemberSite(qml, element, &at, &text)) continue;
            if (qml.mid(at, text.size()) == text) continue; // already inserted
            addEdit(edits, at, 0, text);
            changed = true;
        } else {
            // A line this tool inserted reads back as a plain `pIsFeatureEnable`
            // binding, so the inserted line is matched before the tag: it is
            // the only trace of that edit.
            int at = 0;
            QString text;
            const bool site = sidebarMemberSite(qml, element, &at, &text);
            if (site && qml.mid(at, text.size()) == text) {
                addEdit(edits, at, text.size(), QString());
                changed = true;
                continue;
            }
            if (!hasFeature) continue;
            const QString value = qml.mid(feature.valueBegin,
                                          feature.valueEnd - feature.valueBegin);
            if (!value.startsWith(QLatin1String("false"))) continue;
            QString original;
            int tagEnd = -1;
            if (!taggedAfter(qml, feature.valueBegin, kSidebarCleanTag, &original, &tagEnd))
                continue; // a stock/removal false, not ours
            addEdit(edits, feature.valueBegin, tagEnd - feature.valueBegin, original);
            changed = true;
        }
    }
    return changed;
}

bool sidebarButtonsCleaned(const QString &qml)
{
    return qml.contains(kSidebarCleanTag) || qml.contains(kSidebarCleanLine);
}

bool cleanSidebarEdits(QString &qml, bool enabled, QList<Edit> &edits)
{
    // The strip itself is never removed any more: an older version did that, so
    // the strip is brought back in both directions (updating a clean written by
    // that version must not leave an empty column behind).
    const QString masked = QmlText::mask(qml);
    QmlText::Object root;
    if (findSidebarRoot(qml, masked, &root, false)) {
        QmlText::Object strip;
        if (findButtonStrip(qml, masked, root, &strip)) cleanShowObject(qml, strip, edits);
    }
    cleanSidebarButtonEdits(qml, enabled, edits);
    return true;
}

// True while an already-patched player still has the sidebar itself hidden by
// the old clean-up. The title-bar move needs those buttons, so it is refused
// until the old patch is restored.
bool cleanHidesSidebar(const QByteArray &image)
{
    Doc plr;
    QString error;
    if (findDoc(image, QStringLiteral("plr.qml"), &plr, &error)
        && (plr.qml.contains(kCleanWidthTag) || plr.qml.contains(kCleanMarker)))
        return true;
    Doc sidebar;
    if (findDoc(image, QStringLiteral("PlrSidebar.qml"), &sidebar, &error)
        && sidebar.qml.contains(kCleanHiddenLine))
        return true;
    return false;
}

// MSI 5.9 does not have PlrTopbarElement objects at all: its stock
// fReloadTopbarElements() re-asserts `id.visible` from a ListModel whose entries
// are `{ "id" : iCfgBtn, "visible" : true, "width" : ... }`. Those entries are
// found here (raw text, because the member names are string literals) so the
// model itself says `false` and the reload can never show the button again.
bool modelEntryVisible(const QString &qml, const QString &id, int *valueBegin, int *valueEnd)
{
    const QString idToken = QStringLiteral("\"id\"");
    for (int at = qml.indexOf(idToken); at >= 0; at = qml.indexOf(idToken, at + idToken.size())) {
        int i = at + idToken.size();
        while (i < qml.size() && qml.at(i).isSpace()) ++i;
        if (i >= qml.size() || qml.at(i) != QLatin1Char(':')) continue;
        ++i;
        while (i < qml.size() && qml.at(i).isSpace()) ++i;
        int idEnd = i;
        while (idEnd < qml.size() && (qml.at(idEnd).isLetterOrNumber()
                                      || qml.at(idEnd) == QLatin1Char('_')))
            ++idEnd;
        if (qml.mid(i, idEnd - i) != id) continue;
        const int visibleToken = qml.indexOf(QStringLiteral("\"visible\""), idEnd);
        if (visibleToken < 0 || visibleToken - idEnd > 200) continue;
        int v = visibleToken + int(QStringLiteral("\"visible\"").size());
        while (v < qml.size() && qml.at(v).isSpace()) ++v;
        if (v >= qml.size() || qml.at(v) != QLatin1Char(':')) continue;
        ++v;
        while (v < qml.size() && qml.at(v).isSpace()) ++v;
        int depth = 0;
        int stop = v;
        for (; stop < qml.size(); ++stop) {
            const QChar c = qml.at(stop);
            if (c == QLatin1Char('(') || c == QLatin1Char('[') || c == QLatin1Char('{')) ++depth;
            else if (c == QLatin1Char(')') || c == QLatin1Char(']')) --depth;
            else if (c == QLatin1Char('}') && depth == 0) break;
            else if (c == QLatin1Char(',') && depth == 0) break;
        }
        while (stop > v && qml.at(stop - 1).isSpace()) --stop;
        if (stop <= v) continue;
        *valueBegin = v;
        *valueEnd = stop;
        return true;
    }
    return false;
}

// PlrTopbar.qml: switch every PlrTopbarElement that owns a removable button
// off (so fReloadTopbarElements() stops re-showing it), then hide the buttons
// themselves for the first frame and for the ones the player does not drive
// from that list.
bool cleanTopbarEdits(QString &qml, bool enabled, QList<Edit> &edits)
{
    const QString masked = QmlText::mask(qml);
    int changed = 0;
    for (const auto &element : QmlText::objects(masked, QStringLiteral("PlrTopbarElement"))) {
        const QString body = qml.mid(element.open, element.end - element.open);
        bool keep = false;
        for (const auto &id : cleanKeepIds())
            if (body.contains(id)) { keep = true; break; }
        if (keep) continue;
        bool hides = false;
        for (const auto &id : cleanHideIds())
            if (body.contains(id)) { hides = true; break; }
        if (!hides) continue;
        QmlText::Binding visible;
        if (!directBinding(masked, element.open, element.end,
                           QStringLiteral("pVisible"), &visible))
            continue;
        const QString value = qml.mid(visible.valueBegin, visible.valueEnd - visible.valueBegin);
        if (enabled) {
            if (value == QLatin1String("false")) continue;
            addEdit(edits, visible.valueBegin, visible.valueEnd - visible.valueBegin,
                    withOriginal(QStringLiteral("false"), value, kCleanVisibleTag));
            ++changed;
        } else {
            if (value != QLatin1String("false")) continue;
            QString original;
            int tagEnd = -1;
            if (!taggedAfter(qml, visible.valueBegin, kCleanVisibleTag, &original, &tagEnd))
                continue; // a stock false, not one of ours
            addEdit(edits, visible.valueBegin, tagEnd - visible.valueBegin, original);
            ++changed;
        }
    }

    for (const auto &id : cleanHideIds()) {
        int valueBegin = 0;
        int valueEnd = 0;
        if (modelEntryVisible(qml, id, &valueBegin, &valueEnd)) {
            // The model value spans to the following comma, so a patched entry
            // reads back as `false <tag>` rather than as a bare `false`; the
            // tag is what tells our edit apart from a stock `false`.
            const QString value = qml.mid(valueBegin, valueEnd - valueBegin);
            QString original;
            int tagEnd = -1;
            const bool tagged = taggedAfter(qml, valueBegin, kCleanVisibleTag, &original, &tagEnd);
            if (enabled) {
                if (value != QLatin1String("false") && !tagged) {
                    addEdit(edits, valueBegin, valueEnd - valueBegin,
                            withOriginal(QStringLiteral("false"), value, kCleanVisibleTag));
                    ++changed;
                }
            } else if (tagged) {
                addEdit(edits, valueBegin, tagEnd - valueBegin, original);
                ++changed;
            }
        }
        QmlText::Object object;
        if (!findObjectById(qml, masked, id, &object)) continue;
        if (enabled) {
            if (!cleanHideObject(qml, object, edits)) continue;
            ++changed;
        } else if (cleanShowObject(qml, object, edits)) {
            ++changed;
        }
    }
    return changed > 0;
}

State analyzeCleanUi(const QByteArray &image)
{
    State state;
    if (image.isEmpty()) {
        state.detail = QStringLiteral("Select the HD-Player.exe path first.");
        return state;
    }
    Doc plr;
    Doc sidebar;
    Doc topbar;
    QString error;
    // Only the title bar is edited; the other two documents are read to
    // detect and undo an old clean-up, so a build without them still works.
    if (!findDoc(image, QStringLiteral("PlrTopbar.qml"), &topbar, &error)) {
        state.detail = error;
        return state;
    }
    findDoc(image, QStringLiteral("plr.qml"), &plr, &error);
    findDoc(image, QStringLiteral("PlrSidebar.qml"), &sidebar, &error);
    // The sidebar may already live in the title bar: this option only edits the
    // buttons themselves, so both layouts are fine.
    const bool topbarClean = topbar.qml.contains(kCleanMarker);
    const bool sidebarClean = sidebarButtonsCleaned(sidebar.qml);
    const bool legacy = plr.qml.contains(kCleanMarker) || sidebar.qml.contains(kCleanMarker);
    state.applied = topbarClean || sidebarClean || legacy;
    // A clean-up written by an older version only hid the title-bar buttons (and
    // sometimes the whole strip). It is reported as an update so the missing
    // half can be written without restoring first.
    state.needsUpdate = state.applied && !(topbarClean && sidebarClean);
    QString p = plr.qml;
    QString s = sidebar.qml;
    QString t = topbar.qml;
    QList<Edit> plrPlan;
    QList<Edit> sidebarPlan;
    QList<Edit> topbarPlan;
    // The title bar is the only document that says whether the player has the
    // expected shape: enabling must find something to hide. Restoring a patch
    // that never touched the title bar (an old build only hid the strip) is
    // fine and leaves `topbarPlan` empty.
    const bool topbarOk = cleanTopbarEdits(t, !state.applied, topbarPlan);
    if (!cleanPlrEdits(p, !state.applied, plrPlan)
        || !cleanSidebarEdits(s, !state.applied, sidebarPlan)
        || (!topbarOk && !state.applied)) {
        state.detail = state.applied
            ? QStringLiteral("The applied clean UI could not be read back safely.")
            : QStringLiteral("This player's sidebar/top-bar QML is not a supported shape; "
                             "nothing would be written.");
        return state;
    }
    // Prove every edit set applies without overlap before offering the option.
    if (!applyEdits(p, plrPlan) || !applyEdits(s, sidebarPlan) || !applyEdits(t, topbarPlan)) {
        state.detail = QStringLiteral("The clean-UI edits overlap in this player; nothing would be written.");
        return state;
    }
    state.supported = true;
    state.detail = state.needsUpdate
        ? QStringLiteral("An older clean-up is applied. Press Update to hide the extra sidebar "
                         "buttons too.")
        : state.applied
          ? QStringLiteral("Applied: the extra title-bar buttons are hidden and the sidebar keeps "
                           "only fullscreen, volume, game controls, install APK and trim memory.")
          : QStringLiteral("Ready: one apply hides every extra top-bar button and every sidebar "
                           "button except fullscreen, volume, game controls, install APK and trim "
                           "memory.");
    return state;
}

Result applyCleanUi(const QString &exePath, bool enabled)
{
    const auto refuse = [&](const QString &message) { return Result { false, message }; };
    if (BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe")))
        return refuse(QStringLiteral("Close all BlueStacks instances before changing the player UI."));
    QFile file(exePath);
    if (!file.open(QIODevice::ReadOnly))
        return refuse(QStringLiteral("Cannot read the selected EXE."));
    const QByteArray original = file.readAll();
    file.close();

    const State before = analyzeCleanUi(original);
    if (!before.supported) return refuse(before.detail);
    if (before.applied == enabled && !before.needsUpdate)
        return { true, enabled ? QStringLiteral("The extra top-bar buttons and the extra sidebar "
                                                "buttons are already hidden.")
                               : QStringLiteral("The stock title bar and sidebar are already in place.") };

    QByteArray changed = original;
    struct Role
    {
        QString name;
        bool (*collect)(QString &, bool, QList<Edit> &);
    };
    const QList<Role> roles {
        { QStringLiteral("plr.qml"), &cleanPlrEdits },
        { QStringLiteral("PlrSidebar.qml"), &cleanSidebarEdits },
        { QStringLiteral("PlrTopbar.qml"), &cleanTopbarEdits },
    };
    bool changedAny = false;
    for (const auto &role : roles) {
        Doc doc;
        QString error;
        if (!findDoc(changed, role.name, &doc, &error)) {
            if (role.name == QLatin1String("PlrTopbar.qml")) return refuse(error);
            continue; // no such document: nothing to restore there
        }
        // analyzeCleanUi() has already vetted the shape; here a document that
        // has nothing of ours (for example the title bar of a player cleaned by
        // an older version) is simply skipped so the missing half can be
        // written.
        QList<Edit> edits;
        role.collect(doc.qml, enabled, edits);
        if (edits.isEmpty()) continue;
        if (!applyEdits(doc.qml, edits))
            return refuse(QStringLiteral("Clean-UI edits in %1 overlap; no file was written.")
                              .arg(role.name));
        const QByteArray header = doc.bom ? QByteArray::fromHex("efbbbf") : QByteArray();
        if (!QtResources::replaceResource(changed, doc.file.entryOff, doc.link,
                                          header + doc.qml.toUtf8(), &error))
            return refuse(error);
        changedAny = true;
    }
    if (!changedAny)
        return { true, enabled ? QStringLiteral("The extra buttons are already hidden.")
                               : QStringLiteral("The stock player UI is already in place.") };
    const State after = analyzeCleanUi(changed);
    if (!after.supported || after.applied != enabled || (enabled && after.needsUpdate))
        return refuse(QStringLiteral("The written player UI did not pass read-back verification; "
                                     "no file was written."));
    if (!ThemeEngine::backup(exePath))
        return refuse(QStringLiteral("Could not preserve the original backup; no EXE was written."));
    QString error;
    if (!PatchFile::writeVerified(exePath, original, changed, &error)) return refuse(error);
    const int caches = QtResources::clearPlayerQmlCache(exePath);
    QString msg = enabled
        ? QStringLiteral("Clean UI applied: the extra title-bar buttons are gone and the sidebar "
                         "keeps only fullscreen, volume, game controls, install APK and trim "
                         "memory. Back, Home, Recent, the logo, the title/version and the window "
                         "controls stay. Restart BlueStacks.")
        : QStringLiteral("Stock title-bar and sidebar buttons restored (including any sidebar "
                         "hidden by the old clean-up). Restart BlueStacks.");
    if (caches > 0)
        msg += QStringLiteral(" Cleared the player QML disk cache so BlueStacks reloads the source.");
    return { true, msg };
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
    bool stockOverflowOnly = false; // moved strip keeps the player's own overflow code
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
    snapshot.shape.stockOverflowOnly = snapshot.sidebar.qml.contains(kNoSmartMarker);
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
        if (!directBinding(masked, object.open, object.end, name, &binding)) return false;
        *out = qml.mid(binding.valueBegin, binding.valueEnd - binding.valueBegin);
        return true;
    };

    const bool overlay = hasOverlay(qml);
    if (!overlay && instances.size() == 1) {
        // Stock: either the anchored side strip (5.21/5.22) or the old
        // row-layout item (5.10/5.12). Recognize the shape structurally - the
        // member that carries the stock sidebar width - instead of matching a
        // palette expression or a visibility call that changes between builds
        // (which used to disable both layout options on those builds). The
        // dry run below still proves every edit against the real text.
        const auto &instance = instances.first();
        QString width, layoutWidth;
        const bool haveWidth = value(instance, kSidebarWidthName, &width);
        const bool haveLayoutWidth = value(instance, kSidebarLayoutWidthName, &layoutWidth);
        // Prefer the layout-member shape when both are present: inside a
        // UiRowLayout the layout owns the width, so zeroing `width` alone
        // would leave the strip in place.
        if (haveLayoutWidth && layoutWidth.contains(QLatin1String("pSidebarWidth"))) {
            snapshot.shape.layout = SidebarLayout::RowItem;
            snapshot.shape.stock = true;
        } else if (haveWidth && width.contains(QLatin1String("pSidebarWidth"))) {
            snapshot.shape.layout = SidebarLayout::Anchored;
            snapshot.shape.stock = true;
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
            // Ours and older patches both write the same transparent colour.
            const bool colorOk = !haveColor || color == kColorPatched;
            const bool insetOk = qml.contains(kInsetPatched) || qml.contains(kInsetOriginal);
            if (colorOk && haveFill && fill == QLatin1String("parent")
                && (!snapshot.shape.legacyOverlay
                    || (haveOverlayColor && overlayColor == kColorPatched
                        && haveOverlayVisible && overlayVisible == QLatin1String("true")))
                && insetOk) {
                if (haveLayoutWidth && zeroWidthValue(layoutWidth)) {
                    snapshot.shape.layout = SidebarLayout::RowItem;
                    snapshot.shape.patched = true;
                } else if (haveWidth && zeroWidthValue(width)) {
                    snapshot.shape.layout = SidebarLayout::Anchored;
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
            "PlrSidebar.qml does not declare a movable button strip (a direct child "
            "layout holding the sidebar buttons); the tweak would be unsafe to write.");
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
    if (snapshot.shape.patched && !snapshot.shape.responsiveCurrent
        && !snapshot.shape.stockOverflowOnly) {
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

RowState buttonRows(const QByteArray &image)
{
    RowState state;
    if (image.isEmpty()) {
        state.detail = QStringLiteral("Select the HD-Player.exe path first.");
        return state;
    }
    Doc topbar;
    Doc sidebar;
    QString error;
    if (!findDoc(image, QStringLiteral("PlrTopbar.qml"), &topbar, &error)
        || !findDoc(image, QStringLiteral("PlrSidebar.qml"), &sidebar, &error)) {
        state.detail = error;
        return state;
    }
    const QString topMasked = QmlText::mask(topbar.qml);
    const QString sideMasked = QmlText::mask(sidebar.qml);
    QmlText::Object root;
    QmlText::Object strip;
    if (!findSidebarRoot(sidebar.qml, sideMasked, &root, false)
        || !findButtonStrip(sidebar.qml, sideMasked, root, &strip)) {
        state.detail = QStringLiteral("This player's sidebar does not declare a movable button strip.");
        return state;
    }

    // The buttons fResetSidebar() switches off are listed in the block the
    // move writes; a missing block means nothing has been hidden there.
    QStringList hiddenIds;
    const int markerAt = sidebar.qml.indexOf(kHiddenRowMarker);
    if (markerAt >= 0) {
        int stop = sidebar.qml.indexOf(QLatin1Char('\n'), markerAt);
        if (stop < 0) stop = sidebar.qml.size();
        const QString list = sidebar.qml.mid(markerAt + kHiddenRowMarker.size(),
                                             stop - markerAt - kHiddenRowMarker.size()).trimmed();
        if (!list.isEmpty()) hiddenIds = list.split(QLatin1Char(','), Qt::SkipEmptyParts);
    }

    for (const auto &slot : topbarSlots(topbar.qml, topMasked)) {
        RowButton button;
        button.key = slot.key;
        button.label = buttonLabel(slot.key);
        button.canHide = false; // the title bar keeps its own navigation
        state.buttons << button;
    }
    const bool flipped = settingsInRow(sidebar.qml, sideMasked);
    const QList<ButtonSlot> items = itemSlots(sidebar.qml, sideMasked);
    for (const auto &slot : stripSlots(sidebar.qml, sideMasked, strip)) {
        if (slot.key == QLatin1String("repeater")) {
            for (const auto &item : items) {
                RowButton button;
                button.key = item.key;
                button.label = buttonLabel(item.key);
                button.hidden = objectHidden(sidebar.qml, sideMasked, item.object,
                                             QStringLiteral("pVisible"));
                state.buttons << button;
            }
            continue;
        }
        // The strip's own Back/Home/Recent are the title bar's duplicates.
        if (slot.key == QLatin1String("back") || slot.key == QLatin1String("home")
            || slot.key == QLatin1String("recent")) continue;
        if (flipped && slot.key == QLatin1String("settings")) continue; // the entry is in the row
        RowButton button;
        button.key = slot.key;
        button.label = buttonLabel(slot.key);
        button.hidden = hiddenIds.contains(slot.id);
        state.buttons << button;
    }

    const State shape = analyze(image, kSidebarToTitlebar);
    state.supported = shape.supported;
    state.applied = shape.applied;
    if (!state.supported) state.detail = shape.detail;
    else if (!state.applied)
        state.detail = QStringLiteral("Apply the title-bar layout first; the order belongs to it.");
    return state;
}

Result applyOrder(const QString &exePath, const QStringList &order, const QStringList &hidden)
{
    const auto refuse = [&](const QString &message) { return Result { false, message }; };
    if (BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe")))
        return refuse(QStringLiteral("Close all BlueStacks instances before changing the layout."));
    QFile file(exePath);
    if (!file.open(QIODevice::ReadOnly)) return refuse(QStringLiteral("Cannot read the selected EXE."));
    const QByteArray original = file.readAll();
    file.close();
    if (!analyze(original, kSidebarToTitlebar).applied)
        return refuse(QStringLiteral("Apply the title-bar layout first; the button order belongs to it."));

    RowRequest request;
    request.order = order;
    for (const QString &key : hidden) request.hidden.insert(key);
    QByteArray changed = original;
    QString error;
    if (!writeRowOrder(changed, true, request, &error, true)) return refuse(error);
    if (changed == original)
        return { true, QStringLiteral("The row already has that order.") };
    const Snapshot after = inspect(changed);
    if (after.shape.layout == SidebarLayout::None || !after.shape.patched
        || !after.shape.editable)
        return refuse(QStringLiteral("The reordered row did not pass verification; no file was written."));
    if (!ThemeEngine::backup(exePath))
        return refuse(QStringLiteral("Could not preserve the original backup; no EXE was written."));
    if (!PatchFile::writeVerified(exePath, original, changed, &error)) return refuse(error);
    const int caches = QtResources::clearPlayerQmlCache(exePath);
    QString msg = QStringLiteral("Button order saved. Restart BlueStacks to see the new row.");
    if (caches > 0) msg += QStringLiteral(" Cleared the player QML disk cache.");
    return { true, msg };
}

QList<Tweak> tweaks()
{
    return {
        { kSidebarToTitlebar,
          QStringLiteral("Sidebar buttons to the top bar"),
          QStringLiteral("Moves the original sidebar buttons into a top-bar row that starts "
                         "right after the title bar's own Back, Home and Recent buttons. The "
                         "row opens with Fullscreen, Sound, Files / APK and Settings; the order "
                         "and the buttons it shows can be changed in \"Top-bar button order\". "
                         "Buttons stay shown until they no longer fit; extras then go into the "
                         "three-dot menu, and every popup opens beneath its own button. Turn "
                         "this off and apply to restore the stock sidebar.") },
        { kCleanUi,
          QStringLiteral("Clean player UI (one click)"),
          QStringLiteral("Hides every extra button in one apply. Title bar: help, menu/settings, "
                         "quests, rewards, account, nowbuX, pin-to-top, top-bar icons, macro, "
                         "sync and screen recording. Sidebar: screenshot, screen record, media "
                         "folder, rotate, location, shake, lock cursor, macro, sync, eco mode, "
                         "instance manager, side panel and its own settings/back/home/recent. "
                         "Back, Home, Recent, the BlueStacks logo, the title/version and the "
                         "window controls stay, and the sidebar keeps fullscreen, volume, game "
                         "controls, install APK and trim memory. Untick and apply to restore.") },
        // kResponsiveSidebar is kept internally for byte-for-byte restore of
        // any player that had the old standalone option applied, and for the
        // regression tests. It is NOT surfaced in the UI: the helper is
        // installed automatically as part of the title-bar move above.
        { kResponsiveSidebar,
          QStringLiteral("Responsive sidebar buttons and aligned panels"),
          QStringLiteral("Internal: included automatically with the top-bar layout.") },
    };
}

State analyze(const QByteArray &image, const QString &id)
{
    State state;
    if (id != kSidebarToTitlebar && id != kResponsiveSidebar && id != kCleanUi) {
        state.detail = QStringLiteral("Unknown layout tweak.");
        return state;
    }
    if (image.isEmpty()) {
        state.detail = QStringLiteral("Select the HD-Player.exe path first.");
        return state;
    }
    if (id == kCleanUi) return analyzeCleanUi(image);
    if (id == kResponsiveSidebar) {
        // This option edits PlrSidebar.qml only. It used to be gated by the
        // whole plr.qml/title-bar dry run, so a build whose plr.qml spelling
        // differed disabled it even when its sidebar was perfectly supported.
        QString error;
        Doc sidebarDoc;
        if (!findDoc(image, QStringLiteral("PlrSidebar.qml"), &sidebarDoc, &error)) {
            state.detail = error;
            return state;
        }
        state.applied = sidebarDoc.qml.contains(kResponsiveBegin);
        Doc plrDoc;
        if (findDoc(image, QStringLiteral("plr.qml"), &plrDoc, &error)
            && hasOverlay(plrDoc.qml)) {
            state.detail = QStringLiteral("Included with the title-bar layout. Restore the sidebar "
                                          "first to change this separately.");
            return state;
        }
        QString sidebar = sidebarDoc.qml;
        QList<Edit> plan;
        if (!responsiveEdits(sidebar, !state.applied, false, plan) || !applyEdits(sidebar, plan)) {
            state.detail = QStringLiteral("This sidebar does not have supported overflow and popup anchors.");
            return state;
        }
        state.supported = true;
        state.needsUpdate = state.applied && !hasCurrentResponsiveHelper(sidebarDoc.qml);
        state.detail = state.needsUpdate ? QStringLiteral("Earlier responsive layout applied. Press Update.")
                     : state.applied ? QStringLiteral("Applied.") : QStringLiteral("Ready.");
        return state;
    }
    const Snapshot snapshot = inspect(image);
    if (snapshot.shape.layout == SidebarLayout::None || !snapshot.shape.editable) {
        state.detail = snapshot.shape.detail;
        return state;
    }
    // An older clean-up could have hidden the very strip this option moves;
    // the current one leaves the sidebar alone, so the two compose.
    if (cleanHidesSidebar(image)) {
        state.detail = QStringLiteral("Restore the clean player UI first; an earlier version hid "
                                      "the sidebar buttons this tweak would move.");
        return state;
    }
    state.supported = true;
    state.applied = snapshot.shape.patched;
    // A build without the kebab/overflow model is not an older patch: its
    // stock overflow stays in charge, so it must never offer Update forever.
    state.needsUpdate = state.applied
        && (snapshot.shape.legacyOverlay
            || (!snapshot.shape.responsiveCurrent && !snapshot.shape.stockOverflowOnly));
    state.detail = snapshot.shape.legacyOverlay ? QStringLiteral("Older layout applied. Press Update to repair it.")
                  : state.needsUpdate ? QStringLiteral("Press Update for smart overflow and panels beneath buttons.")
                  : state.applied ? (snapshot.shape.stockOverflowOnly
                                         ? QStringLiteral("Applied. This build keeps its own overflow menu.")
                                         : QStringLiteral("Applied."))
                  : QStringLiteral("Ready.");
    return state;
}

Result apply(const QString &exePath, const QString &id, bool enabled)
{
    const auto refuse = [&](const QString &message) { return Result { false, message }; };
    if (id != kSidebarToTitlebar && id != kResponsiveSidebar && id != kCleanUi)
        return refuse(QStringLiteral("Unknown layout tweak."));
    if (id == kCleanUi) return applyCleanUi(exePath, enabled);
    if (BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe")))
        return refuse(QStringLiteral("Close all BlueStacks instances before changing the layout."));
    QFile file(exePath);
    if (!file.open(QIODevice::ReadOnly))
        return refuse(QStringLiteral("Cannot read the selected EXE."));
    const QByteArray original = file.readAll();
    file.close();

    if (id == kSidebarToTitlebar && cleanHidesSidebar(original))
        return refuse(QStringLiteral("Restore the clean player UI first; an earlier version hid "
                                     "the sidebar buttons this tweak would move."));

    QByteArray changed = original;
    if (id == kResponsiveSidebar) {
        // The sidebar option never edits plr.qml, so it must not depend on the
        // title-bar shape. Only our own overlay marker hides it.
        Doc plrDoc;
        QString plrError;
        if (findDoc(original, QStringLiteral("plr.qml"), &plrDoc, &plrError)
            && hasOverlay(plrDoc.qml))
            return refuse(QStringLiteral("The responsive fix is included with the title-bar layout. "
                                         "Restore the sidebar first to change it separately."));
        Doc doc;
        QString error;
        if (!findDoc(changed, QStringLiteral("PlrSidebar.qml"), &doc, &error)) return refuse(error);
        const bool applied = doc.qml.contains(kResponsiveBegin);
        const bool update = enabled && applied && !hasCurrentResponsiveHelper(doc.qml);
        if (applied == enabled && !update)
            return { true, enabled ? QStringLiteral("Responsive sidebar is already applied.")
                                   : QStringLiteral("Stock sidebar overflow is already in place.") };
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
        const Snapshot before = inspect(original);
        if (before.shape.layout == SidebarLayout::None || !before.shape.editable)
            return refuse(before.shape.detail);
        const bool upgrade = enabled && before.shape.patched && before.shape.legacyOverlay;
        const bool repair = enabled && before.shape.patched && !before.shape.responsiveCurrent
            && !before.shape.stockOverflowOnly && !upgrade;
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
                return refuse(role.name == QLatin1String("PlrSidebar.qml")
                    ? QStringLiteral("PlrSidebar.qml does not declare a movable button strip "
                                     "(a direct child layout holding the sidebar buttons); "
                                     "no file was written.")
                    : QStringLiteral("%1 does not match the expected layout; no file was written.")
                          .arg(role.name));
            if (!applyEdits(doc.qml, edits))
                return refuse(QStringLiteral("Layout edits in %1 overlap; no file was written.").arg(role.name));
            const QByteArray header = doc.bom ? QByteArray::fromHex("efbbbf") : QByteArray();
            if (!QtResources::replaceResource(changed, doc.file.entryOff, doc.link,
                                              header + doc.qml.toUtf8(), &error)) return refuse(error);
        }
        // The row gets its default order in the same write: the title bar's own
        // Back/Home/Recent, then Fullscreen, Sound, Files / APK and Settings.
        QString orderError;
        if (!writeRowOrder(changed, enabled, RowRequest(), &orderError)) return refuse(orderError);
    }
    // The sidebar option is verified on PlrSidebar.qml alone; the title-bar
    // option still has to prove the complete layout on all three files.
    bool verified = false;
    if (id == kResponsiveSidebar) {
        Doc after;
        QString error;
        verified = findDoc(changed, QStringLiteral("PlrSidebar.qml"), &after, &error)
            && after.qml.contains(kResponsiveBegin) == enabled
            && (!enabled || hasCurrentResponsiveHelper(after.qml));
    } else {
        const Snapshot after = inspect(changed);
        verified = after.shape.layout != SidebarLayout::None && after.shape.editable
            && after.shape.patched == enabled
            && (!enabled || (!after.shape.legacyOverlay
                             && (after.shape.responsiveCurrent || after.shape.stockOverflowOnly)));
    }
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
