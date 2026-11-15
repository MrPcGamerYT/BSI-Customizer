#!/usr/bin/env python3
"""Build a monochrome Icons.js drop-in for the user's animated SettingsShell.
SVG/JS generation uses Python's standard library. --render additionally needs
Pillow and resvg-py. No emulator files are modified.
"""
from pathlib import Path
import argparse
import importlib.util
import io
import json
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("settings_icons", ROOT / "scripts/generate_settings_window_icons.py")
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
ICONS = dict(base.ICONS)
ICONS.update({
    "close": ("Close", '<path d="m6 6 12 12M6 18 18 6"/>'),
    "palette": ("Theme", '<path d="M12 3a9 9 0 1 0 0 18h1a2 2 0 0 0 1.6-3.2 1.6 1.6 0 0 1 1.3-2.6H18a3 3 0 0 0 3-3c0-5-4-9.2-9-9.2Z"/><circle cx="7" cy="11" r=".8"/><circle cx="9" cy="6.8" r=".8"/><circle cx="14" cy="6.5" r=".8"/><circle cx="17.5" cy="10" r=".8"/>'),
    "headphones": ("Audio", '<path d="M4 14v-3a8 8 0 0 1 16 0v3"/><rect x="3" y="12" width="4" height="8" rx="2"/><rect x="17" y="12" width="4" height="8" rx="2"/>'),
})
ALIASES = {
    "gamesettings": "game-settings", "controller": "gamepad", "smartphone": "phone",
    "keyboard": "shortcuts", "info": "about", "harddrive": "user-data", "userdata": "user-data",
    "engine": "performance", "resolution": "display", "device": "devices", "audio": "headphones",
    "sound": "headphones", "theme": "palette", "storage": "user-data", "graphics": "graphics",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--render", action="store_true")
    args = parser.parse_args()
    out = ROOT / "assets/settings-shell-matched-icons"
    out.mkdir(parents=True, exist_ok=True)
    paths = {key.replace("-", ""): value[1] for key, value in ICONS.items()}
    aliases = {key: value.replace("-", "") for key, value in ALIASES.items()}
    js = '''.pragma library
// VOID SettingsShell icons. Passive, static silver-white SVGs; no animations,
// network access, timers or input handling. The caller owns motion and opacity.
var style = "rounded";
var defaultStroke = "#f4f4f7";
var paths = %s;
var aliases = %s;

function normalizedKind(kind) {
    var key = String(kind || "preferences").toLowerCase().replace(/[^a-z0-9]/g, "");
    if (Object.prototype.hasOwnProperty.call(aliases, key)) key = aliases[key];
    return Object.prototype.hasOwnProperty.call(paths, key) ? key : "preferences";
}
function svg(kind, stroke) {
    var tint = String(stroke || defaultStroke);
    if (!/^#[0-9a-fA-F]{6}$/.test(tint)) tint = defaultStroke;
    return '<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="'
        + tint + '" stroke-width="1.65" stroke-linecap="round" stroke-linejoin="round">'
        + paths[normalizedKind(kind)] + '</svg>';
}
function url(kind, stroke) {
    return "data:image/svg+xml," + encodeURIComponent(svg(kind, stroke));
}
''' % (json.dumps(paths, indent=4), json.dumps(aliases, indent=4))
    (out / "Icons.js").write_text(js, encoding="utf-8")
    for name, (_, geometry) in ICONS.items():
        data = '<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#f4f4f7" stroke-width="1.65" stroke-linecap="round" stroke-linejoin="round">' + geometry + '</svg>\n'
        path = out / "svg" / (name + ".svg")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(data, encoding="utf-8")
    (out / "README.txt").write_text('''VOID / MATCHED SETTINGS-SHELL ICONS
Developed By NOVA XIS
https://www.youtube.com/@nova_xis20

DROP-IN INSTALLATION FOR THE QML YOU PROVIDED
1. Back up the existing Icons.js beside your SettingsShell.qml.
2. Replace that Icons.js with the Icons.js in this ZIP.
3. Keep your existing import "Icons.js" as Icons and
   source: Icons.url(icon.kind, icon.stroke.toString()). No Image source change needed.
4. Reload/restart the player after deploying through your existing verified QML setup.

No red icon variants or red style tokens are included. Default stroke is #f4f4f7,
exactly matching BsiOutlineIcon.stroke in your code. Your own opacity binding
makes idle icons 0.76, selected/hover icons 1, and pressed icons 0.8. We do not
bake opacity or animation into the SVG, so your hover/press/selection motion
continues to operate only on the icon. Hitboxes, native pages, guards, Save,
close handling and pageContainer are not changed by this library.

Supported kinds from your code:
performance, display, graphics, gameSettings, controller, smartphone, palette,
advanced, keyboard, info, headphones, harddrive, preferences, close.
Also includes devices, gamepad, phone, shortcuts, about, user-data and aliases.
Unknown names fall back to preferences. Unsafe color strings fall back to white.

OPTIONAL: DISTINCT DEVICES TAB
Your current iconKind combines phone and device IDs into smartphone. To show
the separate Devices symbol, replace this one line:
    if (/phone|device/.test(key)) return "smartphone"
with:
    if (/phone/.test(key)) return "smartphone"
    if (/device/.test(key)) return "devices"
Leave the rest of iconKind and your native callbacks unchanged.

Icons.js is self-contained and embeds every SVG geometry; the svg/ and png/
folders are optional editable assets, not runtime dependencies for Icons.url.
Copy Icons.js next to YOUR shell, not over unrelated global icon libraries.
This ZIP does not replace the host Window or automatically patch any EXE.
preview.png is a design preview, not an emulator screenshot or a runtime test.
''', encoding="utf-8")
    if args.render:
        import resvg_py
        from PIL import Image, ImageDraw, ImageFont
        image = Image.new("RGB", (1440, 900), "#070709")
        draw = ImageDraw.Draw(image)
        def text(x,y,value,size=14,color="#c6c6cd"):
            draw.text((x,y),value,font=ImageFont.truetype(str(ROOT / "src/qml/fonts/Inter.ttf"),size),fill=color)
        def glyph(name,x,y,size=32,opacity=1):
            png = resvg_py.svg_to_bytes(svg_string=(out / "svg" / (name+".svg")).read_text(),width=size*2,height=size*2)
            icon = Image.open(io.BytesIO(png)).convert("RGBA").resize((size,size),Image.Resampling.LANCZOS)
            if opacity != 1: icon.putalpha(icon.getchannel("A").point(lambda v:int(v*opacity)))
            image.paste(icon,(x,y),icon)
        text(48,32,"VOID / ANIMATED SETTINGS SHELL",13,"#a5a5b0")
        text(48,61,"Silver-white. Nothing red.",34,"#f4f4f7")
        text(48,112,"Your exact icon color #f4f4f7. Idle 76% / Hover & selected 100% / Pressed 80%.",15,"#858591")
        draw.rounded_rectangle((48,164,355,824),radius=15,fill="#0c0c0d",outline="#56565c")
        text(69,184,"Settings",22,"#f3f3f5")
        for i,(name,(label,_)) in enumerate(list(ICONS.items())[:12]):
            y=241+i*46
            if i==0:
                draw.rounded_rectangle((63,y-5,340,y+31),radius=11,fill="#202025",outline="#e4e4eb")
                draw.rounded_rectangle((68,y+3,70,y+22),radius=1,fill="#f3f3f7")
            glyph(name,81,y+2,20,1 if i==0 else .76)
            text(117,y+3,label,13,"#f7f7fa" if i==0 else "#c6c6cd")
        for i,(name,(label,_)) in enumerate(ICONS.items()):
            x,y=393+(i%5)*199,164+(i//5)*159
            draw.rounded_rectangle((x,y,x+183,y+144),radius=12,fill="#101013",outline="#2c2c33")
            glyph(name,x+73,y+20,36)
            text(x+15,y+76,label,13,"#e2e2e8")
            glyph(name,x+15,y+109,18,.76)
            glyph(name,x+45,y+109,18,1)
            glyph(name,x+75,y+109,18,.8)
            text(x+105,y+112,"idle / hot",10,"#767680")
        text(393,703,"DROPS INTO YOUR EXISTING QML",12,"#9999a5")
        text(393,734,'source: Icons.url(icon.kind, icon.stroke.toString())',17,"#e4e4eb")
        text(393,775,"No baked-in animation. Your hover, press and selection behaviors stay in control.",13,"#8e8e9c")
        draw.line((48,851,1392,851),fill="#2a2a30")
        text(48,869,"Developed By NOVA XIS",12,"#92929f")
        text(1125,869,"DESIGN PREVIEW ONLY",11,"#73737f")
        image.save(out / "preview.png")
        for name in ICONS:
            for size in (24,48):
                path=out / "png" / str(size) / (name+".png")
                path.parent.mkdir(parents=True,exist_ok=True)
                path.write_bytes(resvg_py.svg_to_bytes(svg_string=(out / "svg" / (name+".svg")).read_text(),width=size,height=size))
    archive_path = ROOT / "deliverables/VOID-SettingsShell-Matched-Icons.zip"
    archive_path.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(archive_path,"w",zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(out.rglob("*")):
            if path.is_file(): archive.write(path,"VOID-SettingsShell-Matched-Icons/"+path.relative_to(out).as_posix())
    print(archive_path)

if __name__ == "__main__":
    main()
