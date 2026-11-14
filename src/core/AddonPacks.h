#pragma once
#include "ThemePresets.h"
#include "PlayerAppearancePatcher.h"

namespace AddonPacks {
struct Pack { const char *id; const char *preset; int radius; int height; int padding; };
inline const Pack *find(const QString &id) {
    static const Pack packs[] = {
        {"carbon", "oled_neutral", 8, 32, 12},
        {"amethyst", "amethyst", 12, 36, 18},
        {"arctic", "dark_blue", 8, 34, 16},
        {"ember", "red_dark", 6, 32, 12},
        {"matrix", "green_matrix", 4, 30, 10},
        {"nord", "nord", 10, 38, 20}
    };
    for (const auto &pack : packs) if (id == QLatin1String(pack.id)) return &pack;
    return nullptr;
}
inline QVariantMap designEdits(const Pack &pack, const PlayerAppearancePatcher::Analysis &analysis) {
    QVariantMap edits;
    for (const auto &token : analysis.tokens) {
        if (!token.path.startsWith(QLatin1String("root.buttons.sizes."))) continue;
        int value = token.value;
        if (token.property == QLatin1String("radius")) value = pack.radius;
        else if (token.property == QLatin1String("height")) value = pack.height;
        else if (token.property == QLatin1String("horizontalPadding")) value = pack.padding;
        else continue;
        if (value >= token.minimum && value <= token.maximum) edits[token.key] = value;
    }
    return edits;
}
} // namespace AddonPacks
