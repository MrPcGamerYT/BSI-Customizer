// Qt-free regression for the Player Appearance Studio token catalog.
// g++ -std=c++17 -O2 tests/player_appearance_scan_test.cpp src/core/PlayerAppearanceScan.cpp -o /tmp/player-appearance-test
#include "core/PlayerAppearanceScan.h"

#include <cassert>
#include <iostream>
#include <string>

namespace {
const char *kTheme = R"QML(import QtQuick 2.15
QtObject {
    property string pFontFamily: "Rubik"
    property QtObject fonts: QtObject {
        property font bodyMedium: Qt.font({
            "family": pFontFamily,
            "pixelSize": 13
        })
        property font expressionFont: Qt.font({
            "family": pFontFamily,
            "pixelSize": 10 + 3
        })
    }
    property QtObject buttons: QtObject {
        property QtObject sizes: QtObject {
            property QtObject large: QtObject {
                property real height: 40
                property real radius: 0
                property real horizontalPadding: 20
                property real spacing: 12
                property real iconWidth: 20
                property real iconHeight: 20
                property real computed: 4 * 5
                property string decoy: "radius: 999 } property real height: 999"
            }
        }
    }
    property QtObject sliders: QtObject {
        property QtObject sizes: QtObject {
            property QtObject medium: QtObject {
                property real handleWidth: 20
                property real handleHeight: 20
                property real sliderHeight: 2
            }
        }
    }
    // property real radius: 123
    /* property real height: 456 */
}
)QML";
const char *kChrome = "import QtQuick\r\nimport QtQuick.Window\r\nApplicationWindow {\r\n    id: iAppwin\r\n    property int pBorderWidth: 1\r\n    property int pSidebarWidth: 32\r\n    property int pX: 0\r\n    property real pAutoZoom: Math.max(0.35, 1.0)\r\n}\r\n";

const PlayerAppearanceScan::Token *find(const PlayerAppearanceScan::Result &r, const std::string &path)
{
    for (const auto &token : r.tokens)
        if (token.path == path) return &token;
    return nullptr;
}
}

int main()
{
    using namespace PlayerAppearanceScan;
    const auto theme = scan(kTheme, "qml/UiThemeObject.qml");
    assert(theme.ok);
    assert(theme.ambiguous.empty());
    assert(find(theme, "root.fonts.bodyMedium.pixelSize"));
    assert(find(theme, "root.fonts.bodyMedium.pixelSize")->value == 13);
    assert(!find(theme, "root.fonts.expressionFont.pixelSize"));
    assert(find(theme, "root.buttons.sizes.large.radius"));
    assert(find(theme, "root.buttons.sizes.large.height")->value == 40);
    assert(find(theme, "root.sliders.sizes.medium.sliderHeight")->value == 2);
    assert(!find(theme, "root.buttons.sizes.large.computed"));
    assert(!find(theme, "root.buttons.sizes.large.decoy"));
    assert(!find(theme, "root.pFontFamily"));

    const auto chrome = scan(kChrome, "plr.qml");
    assert(chrome.ok);
    assert(chrome.tokens.size() == 2);
    assert(find(chrome, "root.pSidebarWidth")->value == 32);
    assert(find(chrome, "root.pBorderWidth")->value == 1);
    assert(!find(chrome, "root.pX"));
    assert(!find(chrome, "root.pAutoZoom"));

    std::string edited = kTheme;
    std::string error = "stale";
    assert(apply(edited, "UiThemeObject.qml",
                 {{"root.fonts.bodyMedium.pixelSize", 16}, {"root.buttons.sizes.large.radius", 8}}, &error));
    assert(error.empty());
    const auto rewritten = scan(edited, "UiThemeObject.qml");
    assert(rewritten.ok);
    assert(find(rewritten, "root.fonts.bodyMedium.pixelSize")->value == 16);
    assert(find(rewritten, "root.buttons.sizes.large.radius")->value == 8);
    assert(edited.find("\"pixelSize\": 16") != std::string::npos);
    assert(edited.find("property real radius: 8") != std::string::npos);
    assert(edited.find("computed: 4 * 5") != std::string::npos);

    std::string chromeEdited = kChrome;
    assert(apply(chromeEdited, "plr.qml", {{"root.pSidebarWidth", 48}}, &error));
    assert(chromeEdited.find("property int pSidebarWidth: 48\r\n") != std::string::npos);
    assert(chromeEdited.find("property int pBorderWidth: 1\r\n") != std::string::npos);

    std::string unchanged = kTheme;
    assert(!apply(unchanged, "UiThemeObject.qml", {{"root.buttons.sizes.large.radius", 33}}, &error));
    assert(unchanged == kTheme && !error.empty());
    assert(!apply(unchanged, "UiThemeObject.qml", {{"root.buttons.sizes.large.height", 15}}, &error));
    assert(unchanged == kTheme);
    assert(!apply(unchanged, "UiThemeObject.qml", {{"root.unknown", 1}}, &error));
    assert(unchanged == kTheme);
    assert(!apply(unchanged, "UiThemeObject.qml",
                  {{"root.buttons.sizes.large.radius", 4}, {"root.buttons.sizes.large.radius", 5}}, &error));
    assert(unchanged == kTheme);
    assert(!apply(unchanged, "UiThemeObject.qml", {}, &error));
    assert(unchanged == kTheme);

    const std::string duplicate = R"QML(import QtQuick
QtObject {
    property QtObject buttons: QtObject {
        property QtObject sizes: QtObject {
            property QtObject large: QtObject { property real radius: 1 }
            property QtObject large: QtObject { property real radius: 2 }
        }
    }
}
)QML";
    const auto ambiguous = scan(duplicate, "UiThemeObject.qml");
    assert(ambiguous.ok);
    assert(!ambiguous.ambiguous.empty());
    assert(!find(ambiguous, "root.buttons.sizes.large.radius"));
    std::string ambiguousEdit = duplicate;
    assert(!apply(ambiguousEdit, "UiThemeObject.qml", {{"root.buttons.sizes.large.radius", 3}}, &error));
    assert(ambiguousEdit == duplicate);

    assert(!scan("import QtQuick\nQtObject { property real radius: 4 }", "unknown.qml").ok);
    assert(!scan("QtObject { property real radius: 4 }", "UiThemeObject.qml").ok);
    assert(!scan("", "UiThemeObject.qml").ok);
    const auto wrongRoot = scan("import QtQuick\nItem { property int pSidebarWidth: 32 }", "plr.qml");
    assert(wrongRoot.ok && wrongRoot.tokens.empty());

    std::cout << "PASS: Player Appearance Studio catalog, literal-only edits, CRLF preservation, ranges and ambiguity refusal\n";
    return 0;
}
