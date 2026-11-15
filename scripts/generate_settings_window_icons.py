#!/usr/bin/env python3
"""Generate original Settings-tab SVG/PNG assets matched to the VOID black UI.
Run: PYTHONPATH=.cache/icon-tools python3 scripts/generate_settings_window_icons.py
Rendering dependencies (optional for --svg-only): Pillow and resvg-py.
No player files are read or modified.
"""
from pathlib import Path
import argparse
import hashlib
import io
import json
import zipfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
ICONS = {
    "performance": ("Performance", '<path d="M4.2 18a9 9 0 1 1 15.6 0"/><path d="m12 12 4-5M5 12h1m12 0h1M8 6l.5 1M12 4v1"/><circle cx="12" cy="13" r="1.5"/><path d="M8 20h8"/>'),
    "display": ("Display", '<rect x="3" y="4" width="18" height="12" rx="2"/><path d="M9 20h6m-3-4v4M7 12h10"/>'),
    "graphics": ("Graphics", '<rect x="3" y="6" width="18" height="12" rx="2"/><circle cx="12" cy="12" r="3.3"/><path d="m12 8.7 1 2.3 2.3 1-2.3 1-1 2.3-1-2.3-2.3-1 2.3-1ZM7 3v3m5-3v3m5-3v3M7 18v3m5-3v3m5-3v3"/>'),
    "game-settings": ("Game settings", '<path d="m12 3 8 4.5v9L12 21l-8-4.5v-9L12 3Z"/><path d="m4.5 7.8 7.5 4.4 7.5-4.4M12 12.2v8M7 12v4m-2-2h4m7-1h.01m2 2h.01"/>'),
    "devices": ("Devices", '<rect x="3" y="4" width="12" height="10" rx="2"/><path d="M7 18h4m-2-4v4"/><rect x="17" y="10" width="4" height="10" rx="2"/><path d="M19 13v2"/>'),
    "gamepad": ("Gamepad", '<path d="M8 7h8a4 4 0 0 1 4 3.2l1 6.1a2.7 2.7 0 0 1-4.4 2.4l-2.1-1.9h-5l-2.1 1.9A2.7 2.7 0 0 1 3 16.3l1-6.1A4 4 0 0 1 8 7Z"/><path d="M7 10v5m-2.5-2.5h5M15.5 11h.01m2.5 2.5h.01M9 4h6"/>'),
    "preferences": ("Preferences", '<path d="M4 6h7m4 0h5M4 12h2m4 0h10M4 18h10m4 0h2"/><rect x="11" y="4" width="4" height="4" rx="1"/><rect x="6" y="10" width="4" height="4" rx="1"/><rect x="14" y="16" width="4" height="4" rx="1"/>'),
    "phone": ("Phone", '<rect x="6" y="2.5" width="12" height="19" rx="2.5"/><path d="M10 5h4M11 18.5h2"/>'),
    "shortcuts": ("Shortcuts", '<rect x="2.5" y="5.5" width="19" height="13" rx="2"/><path d="M6 9h.01m4 0h.01m4 0h.01m4 0h.01M6 12h.01m4 0h.01m4 0h.01m4 0h.01M7 15h10"/>'),
    "user-data": ("User data", '<ellipse cx="12" cy="5" rx="8" ry="3"/><path d="M4 5v7c0 1.7 3.6 3 8 3s8-1.3 8-3V5M4 12v7c0 1.7 3.6 3 8 3s8-1.3 8-3v-7"/><path d="M8 10h.01M8 17h.01"/>'),
    "advanced": ("Advanced", '<path d="m9.5 3-.6 2.2-2 .9-2.1-.7-2.5 4.2L4 11.2v2.2l-1.7 1.5 2.5 4.2 2.1-.7 2 .9.6 2.2h5l.6-2.2 2-.9 2.1.7 2.5-4.2-1.7-1.5v-2.2l1.7-1.6-2.5-4.2-2.1.7-2-.9-.6-2.2Z"/><circle cx="12" cy="12.2" r="3.2"/>'),
    "about": ("About", '<circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7h.01"/>'),
}
COLORS = {"normal": "#BFC0CA", "hover": "#F5F5F8", "accent": "#FF2448", "disabled": "#51515D"}


def svg(name, color):
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" '
            'viewBox="0 0 24 24" fill="none" stroke="' + color + '" '
            'stroke-width="1.65" stroke-linecap="round" stroke-linejoin="round">'
            + ICONS[name][1] + '</svg>\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--svg-only", action="store_true")
    args = parser.parse_args()
    output = ROOT / "assets/settings-window-icons"
    output.mkdir(parents=True, exist_ok=True)
    if not args.svg_only:
        import resvg_py
        from PIL import Image, ImageDraw, ImageFont
    manifest = {"name": "VOID Settings Window Icons", "viewBox": "0 0 24 24", "strokeWidth": 1.65,
                "colors": COLORS, "icons": [], "files": {}}
    for name, (label, _) in ICONS.items():
        manifest["icons"].append({"name": name, "label": label})
        for state, color in COLORS.items():
            data = svg(name, color)
            ET.fromstring(data)
            path = output / "svg" / state / (name + ".svg")
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(data, encoding="utf-8")
            manifest["files"][path.relative_to(output).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
            if not args.svg_only:
                for size in (24, 48):
                    png = output / ("png-" + str(size)) / state / (name + ".png")
                    png.parent.mkdir(parents=True, exist_ok=True)
                    png.write_bytes(resvg_py.svg_to_bytes(svg_string=data, width=size, height=size))
                    manifest["files"][png.relative_to(output).as_posix()] = hashlib.sha256(png.read_bytes()).hexdigest()
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (output / "README.txt").write_text(
        "VOID / SETTINGS WINDOW ICONS\nDeveloped By NOVA XIS\nhttps://www.youtube.com/@nova_xis20\n\n"
        "12 original, distinct Settings-tab symbols matched to the black/silver emulator screenshot.\n"
        "Transparent backgrounds. 24-unit viewBox, 1.65-unit rounded strokes.\n"
        "States: normal silver, hover/selected white, optional accent red, disabled muted grey.\n"
        "The screenshot uses WHITE selected icons with red controls; use hover/ for that look.\n"
        "Red accent versions are optional, not a requirement to recolor the UI.\n\n"
        "svg/: scalable editable source, compatible with the static-SVG importer.\n"
        "png-24/ and png-48/: transparent PNGs at native and 2x size.\n"
        "preview.png: enlarged design sheet, NOT an emulator screenshot.\n\n"
        "Use Player Interface's Settings-panel custom icon picker when available, or map an exact\n"
        "SVG resource in Emulator UI Studio after a manual scan. Tab IDs/resources vary by player.\n"
        "No guessed resource mappings, QML patch, profile or EXE modification is included. Merely\n"
        "copying these files into Assets does not attach them to native Settings.\n"
        "These assets have not been applied to the emulator. Boot files remain unchanged.\n",
        encoding="utf-8")
    if not args.svg_only:
        scale = 2
        image = Image.new("RGB", (1440*scale, 960*scale), "#070709")
        draw = ImageDraw.Draw(image)
        font_path = str(ROOT / "src/qml/fonts/Inter.ttf")
        def font(size): return ImageFont.truetype(font_path, size*scale)
        def text(x,y,value,size=14,color="#CACAD3"):
            draw.text((x*scale,y*scale),value,font=font(size),fill=color)
        def box(rect,fill,outline=None,radius=12):
            draw.rounded_rectangle(tuple(int(v*scale) for v in rect),radius=radius*scale,fill=fill,outline=outline,width=scale)
        def line(coords,fill,width=1):
            draw.line(tuple(int(v*scale) for v in coords), fill=fill, width=width*scale)
        def icon(name,state,x,y,size):
            data = resvg_py.svg_to_bytes(svg_string=svg(name,COLORS[state]),width=size*scale,height=size*scale)
            rgba = Image.open(io.BytesIO(data)).convert("RGBA")
            image.paste(rgba,(x*scale,y*scale),rgba)
        text(48,32,"VOID  /  SETTINGS ICONS",14,"#FF2448")
        text(48,60,"Quiet details. Distinct controls.",34,"#F4F4F7")
        text(48,109,"Matched to your emulator: black surfaces, silver outlines, white selection and restrained red accents.",14,"#888895")
        box((48,161,376,863),"#0D0D10","#36363F",16)
        text(72,184,"Settings",23,"#F4F4F7")
        for i,(name,(label,_)) in enumerate(ICONS.items()):
            y=239+i*47
            if i==0:
                box((63,y-6,361,y+33),"#17171C","#EAEAF0",11)
                line((68,y+3,68,y+24),"#F4F4F7",2)
            icon(name,"hover" if i==0 else "normal",80,y+1,22)
            text(117,y+3,label,14,"#F4F4F7" if i==0 else "#AAAAB6")
        text(412,167,"THE ICON FAMILY",12,"#81818F")
        text(1157,167,"24 px native / SVG",12,"#81818F")
        for i,(name,(label,_)) in enumerate(ICONS.items()):
            col,row=i%4,i//4
            x,y=412+col*244,201+row*183
            box((x,y,x+224,y+164),"#101014","#24242D",14)
            icon(name,"hover",x+89,y+22,42)
            text(x+18,y+81,label,15,"#E4E4EC")
            icon(name,"normal",x+18,y+118,20)
            icon(name,"accent",x+50,y+118,20)
            icon(name,"disabled",x+82,y+118,20)
            text(x+122,y+121,"24 / 48 / SVG",10,"#6F6F7C")
        line((412,783,1368,783),"#25252E")
        text(412,805,"NORMAL",11,"#777785"); icon("preferences","normal",486,801,24)
        text(541,805,"SELECTED",11,"#777785"); icon("preferences","hover",627,801,24)
        text(684,805,"OPTIONAL ACCENT",11,"#777785"); icon("preferences","accent",827,801,24)
        text(884,805,"DISABLED",11,"#777785"); icon("preferences","disabled",969,801,24)
        text(412,847,"Transparent assets. No baked-in tile, glow or background. Your black theme stays intact.",13,"#888895")
        line((48,902,1392,902),"#26262F")
        text(48,923,"Developed By NOVA XIS",12,"#8E8E9B")
        text(1020,923,"DESIGN PREVIEW / NOT APPLIED",11,"#686875")
        image.resize((1440,960),Image.Resampling.LANCZOS).save(output / "preview.png")
    destination=ROOT / "deliverables/VOID-Settings-Window-Icons.zip"
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination,"w",compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(output.rglob("*")):
            if path.is_file(): archive.write(path, "VOID-Settings-Window-Icons/"+path.relative_to(output).as_posix())
    print(f"Generated {len(ICONS)} icons in {len(COLORS)} states: {output}")
    print(destination)

if __name__ == "__main__":
    main()
