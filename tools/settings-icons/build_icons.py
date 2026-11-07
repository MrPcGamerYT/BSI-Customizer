"""Build QML Icons.js: the user's 29 supplied keys plus Settings additions."""
from pathlib import Path
import json
ROOT=Path(__file__).resolve().parents[2]
# Supplied shapes retain their original geometry and ICON_COLOR contract.
BASE={
'back':'<path d="m15 6-6 6 6 6"/>',
'close':'<path d="M6 6l12 12M18 6L6 18"/>',
'collapse':'<path d="m14 6-6 6 6 6M20 6l-6 6 6 6"/>',
'eco':'<path d="M12 3c-4 4-7 8-7 12a7 7 0 0 0 14 0c0-4-3-8-7-12z"/><path d="M12 13v5"/>',
'fullscreen':'<path d="M9 3H3v6M15 3h6v6M3 15v6h6M21 15v6h-6"/>',
'gamepad':'<path d="M6 9h12a4 4 0 0 1 4 4v1a4 4 0 0 1-4 4H6a4 4 0 0 1-4-4v-1a4 4 0 0 1 4-4z"/><path d="M8 12h2M9 11v2M16 11.5h.01M18 13.5h.01"/>',
'help':'<circle cx="12" cy="12" r="9"/><path d="M9.5 9a2.5 2.5 0 0 1 5 0c0 1.5-2.5 2-2.5 4"/><circle cx="12" cy="17" r="0.8" fill="ICON_COLOR"/>',
'home':'<path d="m4 11 8-7 8 7"/><path d="M6 10v9a1 1 0 0 0 1 1h10a1 1 0 0 0 1-1v-9"/><path d="M10 21v-6h4v6"/>',
'install':'<path d="M12 4v11"/><path d="m8 11 4 4 4-4"/><path d="M5 19h14"/>',
'keymap':'<rect x="2" y="6" width="20" height="12" rx="2"/><path d="M6 10h.01M10 10h.01M14 10h.01M18 10h.01M6 14h.01M10 14h.01M14 14h4"/>',
'macro':'<circle cx="8" cy="8" r="3"/><circle cx="8" cy="16" r="3"/><path d="M16 8l5 4-5 4"/><path d="M8 11v2"/>',
'maximize':'<rect x="4" y="4" width="16" height="16" rx="2"/><path d="M9 9h6v6"/>',
'menu':'<path d="M5 7h14M5 12h14M5 17h14"/>',
'minimize':'<path d="M6 18h12"/>',
'mute':'<path d="M4 10h3l5-4v12l-5-4H4z"/><path d="M17 9l5 6M22 9l-5 6"/>',
'pin':'<path d="M12 17v5"/><path d="M9 3h6l-1 7h3l-5 5-5-5h3z"/>',
'recent':'<rect x="6" y="4" width="14" height="14" rx="2"/><path d="M4 8v10a2 2 0 0 0 2 2h10"/>',
'rotate':'<path d="M4 12a8 8 0 0 1 8-8"/><path d="M4 4v4h4"/><path d="M20 12a8 8 0 0 1-8 8"/><path d="M20 20v-4h-4"/>',
'screenshot':'<path d="M4 8h3l2-3h6l2 3h3v11H4z"/><circle cx="12" cy="13" r="3.5"/>',
'settings':'<circle cx="12" cy="12" r="3"/><path d="M12 2v2M12 20v2M4.93 4.93l1.41 1.41M17.66 17.66l1.41 1.41M2 12h2M20 12h2M4.93 19.07l1.41-1.41M17.66 6.34l1.41-1.41"/>',
'sync':'<path d="M4 12a8 8 0 0 1 13-6"/><path d="M20 4v4h-4"/><path d="M20 12a8 8 0 0 1-13 6"/><path d="M4 20v-4h4"/>',
'volume':'<path d="M4 10h3l5-4v12l-5-4H4z"/><path d="M16 9.5a4 4 0 0 1 0 5"/><path d="M18.5 7a7 7 0 0 1 0 10"/>',
'memory':'<rect x="5" y="6" width="14" height="12" rx="2"/><path d="M9 6V4M12 6V4M15 6V4M9 20v-2M12 20v-2M15 20v-2M5 10h2M5 14h2M17 10h2M17 14h2"/>',
'record':'<rect x="3" y="5" width="18" height="14" rx="3"/><circle cx="12" cy="12" r="3" fill="ICON_COLOR"/>',
'media':'<rect x="3" y="3" width="18" height="18" rx="3"/><circle cx="8.5" cy="8.5" r="1.5" fill="ICON_COLOR"/><path d="m4 18 4.5-5 3 3 4-5.5 5 7.5"/>',
'location':'<path d="M12 21s-7-5.5-7-11a7 7 0 1 1 14 0c0 5.5-7 11-7 11z"/><circle cx="12" cy="10" r="2.5"/>',
'shake':'<rect x="9" y="3" width="6" height="18" rx="2"/><path d="M5 8l-2 2 2 2-2 2M19 8l2 2-2 2 2 2"/>',
'lock':'<rect x="5" y="11" width="14" height="10" rx="2"/><path d="M8 11V7a4 4 0 0 1 8 0v4"/>',
'more':'<circle cx="12" cy="6" r="1.2" fill="ICON_COLOR"/><circle cx="12" cy="12" r="1.2" fill="ICON_COLOR"/><circle cx="12" cy="18" r="1.2" fill="ICON_COLOR"/>'
}
EXTRA={
'performance':'<path d="M4 18a9 9 0 1 1 16 0"/><path d="m12 13 4-5M5.5 8.5l1.2.7M18.5 8.5l-1.2.7M12 4v1.5M6 17h12"/><circle cx="12" cy="13" r="1.5"/>',
'display':'<rect x="3" y="4" width="18" height="13" rx="2.5"/><path d="M8 21h8M12 17v4M6 14h12"/>',
'graphics':'<path d="m12 3 8.5 4.8v8.4L12 21l-8.5-4.8V7.8L12 3Z"/><path d="m3.5 7.8 8.5 4.8 8.5-4.8M12 12.6V21M7.8 5.4l8.4 4.8"/>',
'gameSettings':'<rect x="3" y="4" width="18" height="16" rx="3"/><path d="M3 8h18M7 14h5M9.5 11.5v5"/><circle cx="16.5" cy="12.5" r=".8" fill="ICON_COLOR"/><circle cx="18.3" cy="15.5" r=".8" fill="ICON_COLOR"/>',
'controller':'<path d="M7.5 7h9c2 0 3 1.5 3.6 3.6l1.2 6c.5 3-1.4 4-3.3 1.8L15.5 16h-7L6 18.4C4.1 20.6 2.2 19.6 2.7 16.6l1.2-6C4.5 8.5 5.5 7 7.5 7Z"/><path d="M6 11.5h5M8.5 9v5"/><circle cx="16" cy="10.5" r=".7" fill="ICON_COLOR"/><circle cx="18" cy="13" r=".7" fill="ICON_COLOR"/>',
'preferences':'<path d="M4 6h3M11 6h9M4 12h9M17 12h3M4 18h3M11 18h9"/><circle cx="9" cy="6" r="2"/><circle cx="15" cy="12" r="2"/><circle cx="9" cy="18" r="2"/>',
'smartphone':'<rect x="6.5" y="2.5" width="11" height="19" rx="3"/><path d="M10 5h4M11 18.5h2"/>',
'palette':'<path d="M12 3a9 9 0 1 0 0 18h1a2 2 0 0 0 1.5-3.3c-.9-1.1-.2-2.7 1.3-2.7H18a3 3 0 0 0 3-3 9 9 0 0 0-9-9Z"/><circle cx="7.5" cy="10" r=".8" fill="ICON_COLOR"/><circle cx="11" cy="6.8" r=".8" fill="ICON_COLOR"/><circle cx="15.5" cy="7.5" r=".8" fill="ICON_COLOR"/><circle cx="17.5" cy="11" r=".8" fill="ICON_COLOR"/>',
'advanced':'<path d="m10 3-.6 2.1-1.5.9-2.2-.5-2 3.4 1.5 1.6v1.8l-1.5 1.6 2 3.5 2.2-.5 1.5.9.6 2.1h4l.6-2.1 1.5-.9 2.2.5 2-3.5-1.5-1.6v-1.8l1.5-1.6-2-3.4-2.2.5-1.5-.9L14 3h-4Z"/><circle cx="12" cy="11.5" r="3"/>',
'info':'<circle cx="12" cy="12" r="9"/><path d="M11 11h1v6M10 17h4"/><circle cx="12" cy="7" r=".8" fill="ICON_COLOR"/>',
'headphones':'<path d="M4 14v-3a8 8 0 0 1 16 0v3"/><rect x="3" y="11" width="4" height="9" rx="2"/><rect x="17" y="11" width="4" height="9" rx="2"/>',
'harddrive':'<rect x="3" y="4" width="18" height="16" rx="3"/><path d="M3 12h18M7 8h6M7 16h.01M10 16h.01"/>',
'expand':'<path d="m4 6 6 6-6 6M10 6l6 6-6 6"/>',
'restore':'<path d="M8 8V4h12v12h-4"/><rect x="4" y="8" width="12" height="12" rx="2"/>'
}
ALIASES={'engine':'performance','cpu':'performance','cube':'graphics','layers':'graphics','person':'gameSettings','theme':'palette','gear':'advanced','sliders':'preferences','keyboard':'keymap','audio':'headphones','storage':'harddrive','device':'smartphone','phone':'smartphone','about':'info'}

def svg(body):
 return '<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="ICON_COLOR" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round">'+body+'</svg>'
def build():
 shapes={k:svg(v) for k,v in {**BASE,**EXTRA}.items()}
 text='.pragma library\n// Supplied 29 keys preserved; Settings additions and compatibility aliases.\nvar shapes = '+json.dumps(shapes,indent=2)+';\n'
 for name,target in ALIASES.items():text+='shapes['+json.dumps(name)+'] = shapes['+json.dumps(target)+'];\n'
 text+='''
// Input is restricted to a solid RGB color. Never insert arbitrary SVG/CSS.
function svg(name, color) {
    var safe = /^#(?:[a-fA-F0-9]{3}|[a-fA-F0-9]{6})$/.test(String(color)) ? String(color) : "#ffffff";
    var shape = Object.prototype.hasOwnProperty.call(shapes, name) ? shapes[name] : shapes.preferences;
    return shape.replace(/ICON_COLOR/g, safe);
}
function url(name, color) {
    return "data:image/svg+xml;charset=utf-8," + encodeURIComponent(svg(name, color));
}
'''
 return text
if __name__=='__main__':
 path=ROOT/'assets/emulator-settings/Icons.js';path.write_text(build())
 print('Created Icons.js:',len(BASE),'preserved keys +',len(EXTRA),'new shapes +',len(ALIASES),'aliases')
