#pragma once

#include "ThemeEngine.h"

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantList>

/**
 * ThemePresets — one-click palettes for both theme methods.
 *
 * Ported from the presets in BS_THEME_CHANGER ("pure black minimal",
 * "Amethyst", high contrast purple) plus extra built-ins.  A preset is a map of
 * canonical color names to #RRGGBB; names are matched case-insensitively and
 * against the spelling variants BlueStacks uses across builds (seperator /
 * separator, overlayGrey / overlayGray, line_ / line, ...), so the same preset
 * works on 5.x and NXT.
 *
 * The same table drives the legacy Hex (old) method: that editor rewrites the
 * camelCase palette names stored in the EXE ("accentNormal", "primary70", ...),
 * which are exactly the names the preset table is built from.
 *
 * Values are always 6 digits: ThemeEngine::buildQml keeps the alpha of an
 * 8-digit (ARGB) slot, so translucent overlays stay translucent.
 */
struct ThemePreset
{
    QString id;                       // "pure_black_minimal"
    QString name;                     // "pure black minimal"
    QString desc;                     // "OLED black, blue accent"
    QMap<QString, QString> colors;    // normalized name -> "#RRGGBB"
    QString toggleOff;                // SVG switch, off state fill
    QString toggleOn;                 // SVG switch, on state fill
    bool light = false;               // light-on-dark vs dark-on-light
    QStringList preview;              // swatches for the preset button
};

namespace ThemePresets {

QList<ThemePreset> all();

// For QML: [{id, name, desc, light, toggleOff, toggleOn, colorCount, preview}]
// `preview` is the five swatches the preset button shows (darkest surface,
// mid surface, accent, text, danger).
QVariantList toVariantList();

const ThemePreset *byId(const QString &id);

// Canonical key for a property name as spelled in the EXE.
QString normalize(const QString &qmlName);

// Preset colors for the entries that actually exist in this EXE:
// exact QML property name -> "#RRGGBB".  Names the preset does not cover are
// left alone, so a preset never blanks out part of the UI.
QMap<QString, QString> matchEntries(const ThemePreset &preset,
                                    const QList<QmlEntry> &entries);

// ---- Hex (old) method -----------------------------------------------------
// The legacy editor stages HexColorItem rows for the palette names it found in
// the EXE.  Returns the colors a preset would change, keyed by the exact name
// stored in the file; slots that are missing, not covered by the preset, or
// already hold the preset color are left out.  The caller copies each value
// into HexColorItem::color and enables that row for Apply Theme.
QMap<QString, QString> matchHexColors(const ThemePreset &preset,
                                      const QList<HexColorItem> &colors);

// Palette math (also used by the presets themselves to derive hover/click).
QString lighten(const QString &hex, double amount);   // amount 0..1 toward white
QString darken(const QString &hex, double amount);    // amount 0..1 toward black

} // namespace ThemePresets
