#include "ThemePresets.h"

#include <QColor>

namespace ThemePresets {

QString lighten(const QString &hex, double amount)
{
    const QColor c(hex);
    if (!c.isValid())
        return hex;
    const double a = qBound(0.0, amount, 1.0);
    return QColor(int(c.red() + (255 - c.red()) * a),
                  int(c.green() + (255 - c.green()) * a),
                  int(c.blue() + (255 - c.blue()) * a))
        .name()
        .toUpper();
}

QString darken(const QString &hex, double amount)
{
    const QColor c(hex);
    if (!c.isValid())
        return hex;
    const double a = qBound(0.0, amount, 1.0);
    return QColor(int(c.red() * (1.0 - a)), int(c.green() * (1.0 - a)), int(c.blue() * (1.0 - a)))
        .name()
        .toUpper();
}

QString normalize(const QString &qmlName)
{
    // BlueStacks spells the same property several ways between builds
    // (seperator/separator, overlayGrey/overlayGray, line_/line), and presets
    // have to match all of them.
    QString out;
    out.reserve(qmlName.size());
    for (const QChar c : qmlName) {
        if (c.isLetterOrNumber())
            out += c.toLower();
    }
    if (out == QLatin1String("seperator"))
        return QStringLiteral("separator");
    if (out == QLatin1String("overlaygrey"))
        return QStringLiteral("overlaygray");
    if (out.endsWith(QLatin1String("grey")))
        return out.left(out.size() - 4) + QStringLiteral("gray");
    return out;
}

} // namespace ThemePresets

namespace {

using ThemePresets::darken;
using ThemePresets::lighten;
using ThemePresets::normalize;

// A preset only has to name the ramp, the accent and the state colors; every
// other key is derived from those so all eleven presets stay internally
// consistent (hover is always a little lighter, click a little darker).
struct Palette
{
    QString p100, p90, p80, p70, p60, p50, p40, p30, p20, p10;
    QString accent, accentHover, accentClick;
    QString secondary, tertiary, alternate;
    QString danger, notice, success, warning;
    QString overlay, separator, subHeader, subBg;
    QString text, textDim;
    QString toggleOff, toggleOn;
    bool light = false;
};

QMap<QString, QString> expand(const Palette &p)
{
    const QString text = p.text.isEmpty() ? p.p10 : p.text;
    const QString textDim = p.textDim.isEmpty() ? p.p40 : p.textDim;
    const QString accentHover = p.accentHover.isEmpty() ? lighten(p.accent, 0.15) : p.accentHover;
    const QString accentClick = p.accentClick.isEmpty() ? darken(p.accent, 0.15) : p.accentClick;
    const QString secondary = p.secondary.isEmpty() ? p.p60 : p.secondary;
    const QString tertiary = p.tertiary.isEmpty() ? p.p70 : p.tertiary;
    const QString alternate = p.alternate.isEmpty() ? p.p60 : p.alternate;
    const QString danger = p.danger.isEmpty() ? QStringLiteral("#E5484D") : p.danger;
    const QString notice = p.notice.isEmpty() ? p.accent : p.notice;
    const QString success = p.success.isEmpty() ? QStringLiteral("#38D996") : p.success;
    const QString warning = p.warning.isEmpty() ? QStringLiteral("#FFB454") : p.warning;
    const QString overlay = p.overlay.isEmpty() ? p.p40 : p.overlay;
    const QString separator = p.separator.isEmpty() ? p.p60 : p.separator;
    const QString subHeader = p.subHeader.isEmpty() ? p.p90 : p.subHeader;
    const QString subBg = p.subBg.isEmpty() ? p.p100 : p.subBg;
    const double step = 0.12;

    QMap<QString, QString> m;
    auto put = [&m](const QStringList &keys, const QString &v) {
        if (v.isEmpty())
            return;
        for (const QString &k : keys)
            m.insert(k, v);
    };

    // Background ramp.  primary100 is the darkest surface, primary10 the
    // lightest, which is how BlueStacks numbers them.
    put({ QStringLiteral("primary100"), QStringLiteral("background") }, p.p100);
    put({ QStringLiteral("primary90") }, p.p90);
    put({ QStringLiteral("primary80") }, p.p80);
    put({ QStringLiteral("primary70") }, p.p70);
    put({ QStringLiteral("primary60") }, p.p60);
    put({ QStringLiteral("primary50") }, p.p50);
    put({ QStringLiteral("primary40") }, p.p40);
    put({ QStringLiteral("primary30") }, p.p30);
    put({ QStringLiteral("primary20") }, p.p20);
    put({ QStringLiteral("primary10"), QStringLiteral("primary") }, p.p10);

    put({ QStringLiteral("textcolor"), QStringLiteral("text"), QStringLiteral("title"),
          QStringLiteral("selectiontext") },
        text);
    put({ QStringLiteral("subtextcolor"), QStringLiteral("subtitle"),
          QStringLiteral("description") },
        textDim);

    put({ QStringLiteral("accentnormal"), QStringLiteral("accent") }, p.accent);
    put({ QStringLiteral("accenthover") }, accentHover);
    put({ QStringLiteral("accentclick") }, accentClick);
    put({ QStringLiteral("accentclicktransparent") }, p.accent);

    put({ QStringLiteral("secondarynormal"), QStringLiteral("secondary") }, secondary);
    put({ QStringLiteral("secondaryhover") }, lighten(secondary, step));
    put({ QStringLiteral("secondaryclick") }, darken(secondary, step));
    put({ QStringLiteral("tertiarynormal"), QStringLiteral("tertiary") }, tertiary);
    put({ QStringLiteral("tertiaryhover") }, lighten(tertiary, step));
    put({ QStringLiteral("tertiaryclick") }, darken(tertiary, step));
    put({ QStringLiteral("alternatenormal"), QStringLiteral("alternate") }, alternate);
    put({ QStringLiteral("alternatehover") }, lighten(alternate, step));
    put({ QStringLiteral("alternateclick") }, darken(alternate, step));

    put({ QStringLiteral("dangernormal"), QStringLiteral("danger") }, danger);
    put({ QStringLiteral("dangerhover") }, lighten(danger, step));
    put({ QStringLiteral("dangerclick") }, darken(danger, step));
    put({ QStringLiteral("dangerdark") }, darken(danger, 0.35));
    put({ QStringLiteral("dangertranslucent"), QStringLiteral("dangertransparent") }, danger);
    put({ QStringLiteral("error") }, danger);
    put({ QStringLiteral("notice") }, notice);
    put({ QStringLiteral("success") }, success);
    put({ QStringLiteral("warning") }, warning);

    // Overlays keep whatever alpha the slot already has - buildQml preserves an
    // 8-digit ARGB value when it is handed a 6-digit color.
    put({ QStringLiteral("overlayprimary") }, p.p100);
    put({ QStringLiteral("overlayblack"), QStringLiteral("black"), QStringLiteral("dark") },
        QStringLiteral("#000000"));
    put({ QStringLiteral("overlaygray"), QStringLiteral("overlaygrey") }, overlay);
    put({ QStringLiteral("overlaybright"), QStringLiteral("white") }, QStringLiteral("#FFFFFF"));
    put({ QStringLiteral("overlaytint") }, p.accent);
    put({ QStringLiteral("transparent"), QStringLiteral("translucent") },
        QStringLiteral("#000000"));

    put({ QStringLiteral("gradientprimary"), QStringLiteral("cloudgradientprimary"),
          QStringLiteral("notificationgradientprimary") },
        p.p100);
    put({ QStringLiteral("gradientsecondary"), QStringLiteral("cloudgradientsecondary"),
          QStringLiteral("notificationgradientsecondary") },
        p.p60);

    put({ QStringLiteral("subwindowheader") }, subHeader);
    put({ QStringLiteral("subwindowheaderclick") },
        p.light ? darken(subHeader, step) : lighten(subHeader, step));
    put({ QStringLiteral("subwindowbg") }, subBg);
    put({ QStringLiteral("separator"), QStringLiteral("seperator"), QStringLiteral("line"),
          QStringLiteral("border"), QStringLiteral("bordercolor") },
        separator);

    put({ QStringLiteral("handlebackground") }, p.p70);
    put({ QStringLiteral("handlebackgrounddisabled") }, p.p80);
    put({ QStringLiteral("handleborder") }, p.p60);
    put({ QStringLiteral("sliderbackground") }, p.p80);
    put({ QStringLiteral("sliderbackgrounddisabled") }, p.p90);
    put({ QStringLiteral("sliderfill"), QStringLiteral("rangevalue"),
          QStringLiteral("selectedvalue") },
        p.accent);
    put({ QStringLiteral("sliderfilldisabled") }, p.p60);
    put({ QStringLiteral("recommendedvalue") }, p.p40);

    put({ QStringLiteral("cloudbutton"), QStringLiteral("nowbuxbutton") }, secondary);
    put({ QStringLiteral("linkbutton") }, p.accent);
    put({ QStringLiteral("backgroundhover"), QStringLiteral("bghover") }, p.p80);
    put({ QStringLiteral("backgroundactive"), QStringLiteral("press") }, p.p70);
    put({ QStringLiteral("backgrounddisabled") }, p.p90);
    put({ QStringLiteral("dropdownselection"), QStringLiteral("dropdownhighlight"),
          QStringLiteral("selection") },
        p.accent);
    put({ QStringLiteral("tooltips") }, p.p80);

    return m;
}

ThemePreset make(const QString &id, const QString &name, const QString &desc, const Palette &p)
{
    ThemePreset t;
    t.id = id;
    t.name = name;
    t.desc = desc;
    t.colors = expand(p);
    t.light = p.light;
    t.toggleOff = p.toggleOff.isEmpty() ? p.p60 : p.toggleOff;
    t.toggleOn = p.toggleOn.isEmpty() ? p.accent : p.toggleOn;
    t.preview = { p.p100, p.p70, p.accent, p.text.isEmpty() ? p.p10 : p.text,
                  p.danger.isEmpty() ? QStringLiteral("#E5484D") : p.danger };
    return t;
}

} // namespace

namespace ThemePresets {

QList<ThemePreset> all()
{
    static const QList<ThemePreset> presets = [] {
        QList<ThemePreset> list;

        // ---- the two that ship with BS_THEME_CHANGER -------------------
        {
            Palette p;
            p.p100 = "#000000"; p.p90 = "#050505"; p.p80 = "#080808"; p.p70 = "#0B0C0E";
            p.p60 = "#111111"; p.p50 = "#1A1A1A"; p.p40 = "#808080"; p.p30 = "#B0B0B0";
            p.p20 = "#DADADA"; p.p10 = "#FFFFFF";
            p.accent = "#1A90FF"; p.accentHover = "#2EA4FF"; p.accentClick = "#057CEB";
            p.secondary = "#1A1A1A"; p.tertiary = "#141414"; p.alternate = "#1A1A1A";
            p.danger = "#E5484D"; p.overlay = "#808080"; p.separator = "#1A1A1A";
            p.subHeader = "#050505"; p.subBg = "#000000";
            p.toggleOff = "#1A1A1A"; p.toggleOn = "#1A90FF";
            list << make(QStringLiteral("pure_black_minimal"),
                         QStringLiteral("pure black minimal"),
                         QStringLiteral("OLED black, blue accent"), p);
        }
        {
            Palette p;
            p.p100 = "#1A0A2E"; p.p90 = "#2D1B4E"; p.p80 = "#3A2363"; p.p70 = "#4A2C7A";
            p.p60 = "#6B3FA0"; p.p50 = "#8B5CF6"; p.p40 = "#A78BFA"; p.p30 = "#C4B5FD";
            p.p20 = "#E9E3FF"; p.p10 = "#F0F1F4";
            p.accent = "#8B5CF6"; p.accentHover = "#A78BFA"; p.accentClick = "#6D28D9";
            p.secondary = "#2D1B4E"; p.tertiary = "#3A2363"; p.alternate = "#4A2C7A";
            p.overlay = "#2D1B4E"; p.separator = "#4A2C7A";
            p.subHeader = "#2D1B4E"; p.subBg = "#1A0A2E"; p.text = "#F0F1F4";
            p.toggleOff = "#3D2463"; p.toggleOn = "#8B5CF6";
            list << make(QStringLiteral("amethyst"), QStringLiteral("Amethyst"),
                         QStringLiteral("high contrast purple"), p);
        }
        {
            Palette p;
            p.p100 = "#12002B"; p.p90 = "#1B0340"; p.p80 = "#250A56"; p.p70 = "#32126B";
            p.p60 = "#431C86"; p.p50 = "#5B27B0"; p.p40 = "#8B5CF6"; p.p30 = "#C77DFF";
            p.p20 = "#E9D5FF"; p.p10 = "#FFFFFF";
            p.accent = "#C77DFF"; p.accentHover = "#D8A3FF"; p.accentClick = "#9B4DDB";
            p.secondary = "#250A56"; p.tertiary = "#32126B"; p.alternate = "#431C86";
            p.overlay = "#1B0340"; p.separator = "#431C86";
            p.subHeader = "#1B0340"; p.subBg = "#12002B"; p.text = "#FFFFFF";
            p.toggleOff = "#32126B"; p.toggleOn = "#C77DFF";
            list << make(QStringLiteral("high_contrast_purple"),
                         QStringLiteral("high contrast purple"),
                         QStringLiteral("deep purple, maximum contrast"), p);
        }

        // ---- extra built-ins -------------------------------------------
        {
            Palette p;
            p.p100 = "#000000"; p.p90 = "#0A0A0A"; p.p80 = "#121212"; p.p70 = "#1A1A1A";
            p.p60 = "#242424"; p.p50 = "#303030"; p.p40 = "#8A8A8A"; p.p30 = "#B5B5B5";
            p.p20 = "#DCDCDC"; p.p10 = "#F5F5F5";
            p.accent = "#E8E8E8"; p.accentHover = "#FFFFFF"; p.accentClick = "#B0B0B0";
            p.secondary = "#1A1A1A"; p.tertiary = "#141414"; p.alternate = "#242424";
            p.overlay = "#7A7A7A"; p.separator = "#242424";
            p.subHeader = "#0A0A0A"; p.subBg = "#000000"; p.text = "#F5F5F5";
            p.toggleOff = "#242424"; p.toggleOn = "#E8E8E8";
            list << make(QStringLiteral("oled_neutral"), QStringLiteral("OLED neutral"),
                         QStringLiteral("true black, gray accent"), p);
        }
        {
            Palette p;
            p.p100 = "#0A1128"; p.p90 = "#101A3A"; p.p80 = "#16224A"; p.p70 = "#1C2B5C";
            p.p60 = "#24356F"; p.p50 = "#2E4185"; p.p40 = "#7C93C9"; p.p30 = "#A9BCE6";
            p.p20 = "#D6E2FA"; p.p10 = "#EEF4FF";
            p.accent = "#4C9AFF"; p.accentHover = "#6FB0FF"; p.accentClick = "#2B7BE0";
            p.secondary = "#16224A"; p.tertiary = "#1C2B5C"; p.alternate = "#24356F";
            p.overlay = "#101A3A"; p.separator = "#24356F";
            p.subHeader = "#101A3A"; p.subBg = "#0A1128"; p.text = "#EEF4FF";
            p.toggleOff = "#24356F"; p.toggleOn = "#4C9AFF";
            list << make(QStringLiteral("dark_blue"), QStringLiteral("dark blue"),
                         QStringLiteral("navy surfaces, cool accent"), p);
        }
        {
            Palette p;
            p.p100 = "#2E3440"; p.p90 = "#3B4252"; p.p80 = "#434C5E"; p.p70 = "#4C566A";
            p.p60 = "#5E6779"; p.p50 = "#6C7A96"; p.p40 = "#93A1BC"; p.p30 = "#D8DEE9";
            p.p20 = "#E5E9F0"; p.p10 = "#ECEFF4";
            p.accent = "#88C0D0"; p.accentHover = "#9BD2E0"; p.accentClick = "#5E81AC";
            p.secondary = "#3B4252"; p.tertiary = "#434C5E"; p.alternate = "#4C566A";
            p.danger = "#BF616A"; p.notice = "#81A1C1"; p.success = "#A3BE8C";
            p.warning = "#EBCB8B";
            p.overlay = "#3B4252"; p.separator = "#4C566A";
            p.subHeader = "#3B4252"; p.subBg = "#2E3440"; p.text = "#ECEFF4";
            p.toggleOff = "#4C566A"; p.toggleOn = "#88C0D0";
            list << make(QStringLiteral("nord"), QStringLiteral("Nord"),
                         QStringLiteral("nordic frost, low contrast"), p);
        }
        {
            Palette p;
            p.p100 = "#282A36"; p.p90 = "#2E303E"; p.p80 = "#343746"; p.p70 = "#3A3D4E";
            p.p60 = "#44475A"; p.p50 = "#4D5066"; p.p40 = "#6272A4"; p.p30 = "#A4A7C6";
            p.p20 = "#D8DAE9"; p.p10 = "#F8F8F2";
            p.accent = "#BD93F9"; p.accentHover = "#CAA6FA"; p.accentClick = "#A374E8";
            p.secondary = "#44475A"; p.tertiary = "#3A3D4E"; p.alternate = "#6272A4";
            p.danger = "#FF5555"; p.notice = "#8BE9FD"; p.success = "#50FA7B";
            p.warning = "#F1FA8C";
            p.overlay = "#282A36"; p.separator = "#44475A";
            p.subHeader = "#2E303E"; p.subBg = "#282A36"; p.text = "#F8F8F2";
            p.toggleOff = "#44475A"; p.toggleOn = "#BD93F9";
            list << make(QStringLiteral("dracula"), QStringLiteral("Dracula"),
                         QStringLiteral("purple, pink and cyan on slate"), p);
        }
        {
            Palette p;
            p.p100 = "#002B36"; p.p90 = "#073642"; p.p80 = "#0E4152"; p.p70 = "#164C60";
            p.p60 = "#205A6E"; p.p50 = "#2A6A80"; p.p40 = "#586E75"; p.p30 = "#93A1A1";
            p.p20 = "#C8D0D0"; p.p10 = "#EEE8D5";
            p.accent = "#268BD2"; p.accentHover = "#3F9BE0"; p.accentClick = "#1B6FB0";
            p.secondary = "#073642"; p.tertiary = "#0E4152"; p.alternate = "#164C60";
            p.danger = "#DC322F"; p.notice = "#2AA198"; p.success = "#859900";
            p.warning = "#B58900";
            p.overlay = "#073642"; p.separator = "#0E4152";
            p.subHeader = "#073642"; p.subBg = "#002B36"; p.text = "#EEE8D5";
            p.toggleOff = "#0E4152"; p.toggleOn = "#268BD2";
            list << make(QStringLiteral("solarized_dark"), QStringLiteral("Solarized dark"),
                         QStringLiteral("teal base, warm text"), p);
        }
        {
            Palette p;
            p.p100 = "#FFFFFF"; p.p90 = "#F7F8FA"; p.p80 = "#F1F2F8"; p.p70 = "#E8EAF2";
            p.p60 = "#DFE2EC"; p.p50 = "#D0D4E0"; p.p40 = "#8A90A2"; p.p30 = "#5A6072";
            p.p20 = "#2A2E3C"; p.p10 = "#14161C";
            p.accent = "#2B6FD6"; p.accentHover = "#3F83E8"; p.accentClick = "#1D5AB8";
            p.secondary = "#E8EAF2"; p.tertiary = "#F1F2F8"; p.alternate = "#DFE2EC";
            p.danger = "#D63A5F"; p.overlay = "#FFFFFF"; p.separator = "#DFE2EC";
            p.subHeader = "#F7F8FA"; p.subBg = "#FFFFFF";
            p.text = "#14161C"; p.textDim = "#5A6072";
            p.toggleOff = "#DFE2EC"; p.toggleOn = "#2B6FD6";
            p.light = true;
            list << make(QStringLiteral("light_minimal"), QStringLiteral("light minimal"),
                         QStringLiteral("clean white, blue accent"), p);
        }
        {
            Palette p;
            p.p100 = "#000000"; p.p90 = "#03100A"; p.p80 = "#06170F"; p.p70 = "#0A1F14";
            p.p60 = "#0F2A1B"; p.p50 = "#14381F"; p.p40 = "#2E7D4F"; p.p30 = "#66BB6A";
            p.p20 = "#A5D6A7"; p.p10 = "#D7FFDF";
            p.accent = "#00FF41"; p.accentHover = "#4CFF6E"; p.accentClick = "#00C733";
            p.secondary = "#0A1F14"; p.tertiary = "#06170F"; p.alternate = "#0F2A1B";
            p.danger = "#FF5252"; p.notice = "#00BFA5"; p.success = "#00E676";
            p.warning = "#FFD740";
            p.overlay = "#03100A"; p.separator = "#14381F";
            p.subHeader = "#03100A"; p.subBg = "#000000"; p.text = "#D7FFDF";
            p.toggleOff = "#14381F"; p.toggleOn = "#00FF41";
            list << make(QStringLiteral("green_matrix"), QStringLiteral("green matrix"),
                         QStringLiteral("black with phosphor green"), p);
        }
        {
            Palette p;
            p.p100 = "#0B0000"; p.p90 = "#160303"; p.p80 = "#1E0606"; p.p70 = "#270909";
            p.p60 = "#330D0D"; p.p50 = "#431212"; p.p40 = "#8E2424"; p.p30 = "#C25B5B";
            p.p20 = "#E79A9A"; p.p10 = "#FFF3F3";
            p.accent = "#FF4D4D"; p.accentHover = "#FF6B6B"; p.accentClick = "#D93636";
            p.secondary = "#270909"; p.tertiary = "#1E0606"; p.alternate = "#330D0D";
            p.danger = "#FF4D4D"; p.notice = "#FF8A80"; p.success = "#66BB6A";
            p.warning = "#FFB454";
            p.overlay = "#160303"; p.separator = "#330D0D";
            p.subHeader = "#160303"; p.subBg = "#0B0000"; p.text = "#FFF3F3";
            p.toggleOff = "#330D0D"; p.toggleOn = "#FF4D4D";
            list << make(QStringLiteral("red_dark"), QStringLiteral("red dark"),
                         QStringLiteral("dark crimson"), p);
        }
        return list;
    }();
    return presets;
}

const ThemePreset *byId(const QString &id)
{
    static const QList<ThemePreset> presets = all();
    for (const ThemePreset &p : presets) {
        if (p.id == id)
            return &p;
    }
    return nullptr;
}

QVariantList toVariantList()
{
    QVariantList out;
    const QList<ThemePreset> presets = all();
    for (const ThemePreset &p : presets) {
        out << QVariantMap { { "id", p.id },
                             { "name", p.name },
                             { "desc", p.desc },
                             { "light", p.light },
                             { "toggleOff", p.toggleOff },
                             { "toggleOn", p.toggleOn },
                             { "colorCount", p.colors.size() },
                             { "preview", p.preview } };
    }
    return out;
}

namespace {

// Compare on RGB only: an 8-digit (ARGB) slot keeps its alpha when patched,
// so "#FF8B5CF6" in the EXE already equals a preset's "#8B5CF6" and staging it
// would show a change that is not one.
QString rgbOnly(const QString &hex)
{
    QString value = hex.trimmed();
    if (value.size() == 9 && value.startsWith(QLatin1Char('#')))
        value = QStringLiteral("#") + value.mid(3);
    return value;
}

} // namespace

QMap<QString, QString> matchEntries(const ThemePreset &preset, const QList<QmlEntry> &entries)
{
    QMap<QString, QString> out;
    for (const QmlEntry &e : entries) {
        const QString v = preset.colors.value(normalize(e.name));
        if (v.isEmpty())
            continue;                 // name this preset does not cover
        if (v.compare(rgbOnly(e.value), Qt::CaseInsensitive) != 0)
            out.insert(e.name, v);
    }
    return out;
}

QMap<QString, QString> matchHexColors(const ThemePreset &preset,
                                      const QList<HexColorItem> &colors)
{
    QMap<QString, QString> out;
    for (const HexColorItem &c : colors) {
        const QString v = preset.colors.value(normalize(c.name));
        if (v.isEmpty())
            continue;                 // name this preset does not cover
        if (v.compare(rgbOnly(c.currentHex), Qt::CaseInsensitive) != 0)
            out.insert(c.name, v);
    }
    return out;
}

} // namespace ThemePresets
