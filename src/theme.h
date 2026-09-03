#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>

// Exact same strings from original binary
#define STR_THEME_CHANGER "THEME CHANGER"
#define STR_SUBTITLE "BlueStacks / MSI 5.22  |  preset or manual"
#define STR_THEME_PRESETS "THEME PRESETS"
#define STR_UITHEME_QML "UiThemeObject.qml"
#define STR_PROP_COLORS "property QtObject colors"
#define STR_PROP_COLOR "property color primary100"

#define STR_ERR_CANNOT_READ "Cannot read file."
#define STR_ERR_THEME_NOT_FOUND "Theme was not found in this file."
#define STR_MSG_LOADED "Loaded. Choose preset or edit manually."
#define STR_MSG_PATCH_RELOCATED "Patch complete. Full theme relocated and applied."
#define STR_MSG_PATCH_APPLIED "Patch complete. Full theme applied."
#define STR_ERR_COMPRESS_FAIL "Theme compression failed."
#define STR_ERR_NO_ROOM "No room for this custom theme in this EXE."
#define STR_ERR_CANNOT_WRITE "Cannot write file."
#define STR_ERR_VERIFY_FAIL "Patched theme verify failed."
#define STR_MSG_SELECT_THEN_EDIT "Select HD-Player.exe, then choose a preset or edit a color."
#define STR_MSG_DONE_REPATCH "Done. Full theme table patched. You can repatch again without backup."
#define STR_MSG_NO_BACKUP "No backup found."
#define STR_MSG_RESTORED_BACKUP "Restored from backup."
#define STR_MSG_PATCH_FAILED "Patch failed"
#define STR_MSG_RESTORE_FAILED "Restore failed"
#define STR_MSG_SELECT_BEGIN "Select HD-Player.exe to begin."

// Original color list from binary dump
struct ColorEntry {
    std::string name;          // e.g. primary100, gradientprimary, dangerDark
    std::string originalHex;   // #080A1F
    std::string currentHex;    // editable
    std::string qmlLine;       // full line
    bool isCustom = false;
};

struct ThemeRes {
    size_t fileOffset = 0;              // offset of compressed theme in exe
    size_t compressedSize = 0;
    size_t decompressedSize = 0;
    size_t originalCompressedSize = 0;
    std::vector<uint8_t> rawCompressed;
    std::vector<uint8_t> rawDecompressed;
    std::string qmlText;                // decompressed QML
    std::vector<ColorEntry> colors;     // parsed colors
    std::map<std::string, std::string> svgPatches; // toggle -> color
    bool found = false;
    std::string exePath;
    // For relocation
    bool needsRelocation = false;
    size_t newOffset = 0;
};

struct AutoPreset {
    std::string id;
    std::string name; // pure black minimal, Amethyst
    std::string desc; // high contrast purple
    std::map<std::string, std::string> colorMap; // name(lower) -> hex
    std::map<std::string, std::string> svgOverrides;
};

// Default colors from original binary
static const std::vector<std::pair<std::string, std::string>> kDefaultQmlColors = {
    {"primary100", "#080A1F"},
    {"primary90", "#0B0C0E"},
    {"primary80", "#1B1E38"},
    {"primary70", "#17181C"},
    {"primary60", "#232642"},
    {"primary50", "#21242C"},
    {"primary40", "#808080"},
    {"primary30", "#B0B0B0"},
    {"primary20", "#DADADA"},
    {"primary10", "#FFFFFF"},
    {"accentnormal", "#1A90FF"},
    {"accenthover", "#E8E8E8"},
    {"accentclick", "#CFCFCF"},
    {"accentclicktransparent", "#66FFFFFF"},
    {"secondarynormal", "#2B2E4C"},
    {"secondaryhover", "#454954"},
    {"secondaryclick", "#373A58"},
    {"tertiarynormal", "#2E3038"},
    {"tertiaryhover", "#5C6170"},
    {"tertiaryclick", "#434664"},
    {"alternatenormal", "#080808"},
    {"alternatehover", "#131621"},
    {"alternateclick", "#555E6B"},
    {"dangernormal", "#EB2C43"},
    {"dangerhover", "#ED1C24"},
    {"dangerclick", "#C61017"},
    {"dangerdark", "#33000000"},
    {"dangerTranslucent", "#CCFFFFFF"},
    {"dangerTransparent", "#00000000"},
    {"overlayPrimary", "#D9000000"},
    {"overlayblack", "#CC000000"},
    {"overLayBlack", "#CC000000"},
    {"overlaygray", "#99000000"},
    {"overLayGray", "#99000000"},
    {"overlaybright", "#99FFFFFF"},
    {"overLayBright", "#99FFFFFF"},
    {"overlaytint", "#33000000"},
    {"overLayTint", "#33000000"},
    {"gradientprimary", "#080A1F"},
    {"cloudGradientPrimary", "#232642"},
    {"gradientsecondary", "#21242C"},
    {"cloudGradientSecondary", "#2B2E4C"},
    {"notificationGradientPrimary", "#080A1F"},
    {"notificationGradientSecondary", "#1B1E38"},
};

// Extended UI color names from binary
static const std::vector<std::string> kExtendedColorNames = {
    "primary40", "primary30", "primary20", "primary10",
    "accentnormal", "accenthover", "accentclick", "accentclicktransparent",
    "secondarynormal", "secondaryclick", "tertiarynormal", "tertiaryclick",
    "secondaryhover", "tertiaryhover", "alternatenormal", "alternatehover", "alternateclick",
    "dangernormal", "dangerhover", "dangerclick", "notice", "success", "warning", "error",
    "dangerdark", "dangertranslucent", "dangertransparent", "dark", "dangerDark",
    "overlayPrimary", "overLayBlack", "overLayGray", "overLayBright", "overLayTint",
    "gradientprimary", "cloudGradientPrimary", "gradientsecondary", "cloudGradientSecondary",
    "colors", "sliders", "handlebackground", "accentNormal", "handlebackgrounddisabled",
    "handleborder", "sliderbackground", "sliderbackgrounddisabled", "sliderfill",
    "sliderfilldisabled", "rangevalue", "selectedvalue", "recommendedvalue",
    "buttons", "secondary", "tertiary", "danger", "alternate", "cloudbutton", "nowbuxbutton",
    "linkbutton", "background", "bordercolor", "border", "textcolor", "subtextcolor",
    "accent", "textfields", "comboboxes", "dropdown", "dialogs", "subwindow", "tooltips",
    "selectiontext", "text", "selection", "dropdownselection", "dropdownhighlight",
    "scrollbars", "backgroundhover", "backgroundactive", "backgrounddisabled",
    "bghover", "accentClick", "title", "subtitle", "description", "primary",
    "subwindowheader", "subwindowheaderclick", "subwindowbg", "seperator", "separator",
    "accentClickTransparent", "press", "secondaryHover", "secondaryClick", "secondaryNormal",
    "tertiaryHover", "tertiaryClick", "tertiaryNormal", "translucent", "dangerTranslucent",
    "dangerTransparent", "alternateHover", "alternateClick", "alternateNormal",
    "overlayPrimary", "black", "white", "transparent"
};
