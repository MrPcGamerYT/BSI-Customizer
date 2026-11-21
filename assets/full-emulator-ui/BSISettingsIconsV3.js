.pragma library
// VOID SettingsShell icons. Passive, static silver-white SVGs; no animations,
// network access, timers or input handling. The caller owns motion and opacity.
var style = "rounded";
var defaultStroke = "#f4f4f7";
var paths = {
    "performance": "<path d=\"M4.2 18a9 9 0 1 1 15.6 0\"/><path d=\"m12 12 4-5M5 12h1m12 0h1M8 6l.5 1M12 4v1\"/><circle cx=\"12\" cy=\"13\" r=\"1.5\"/><path d=\"M8 20h8\"/>",
    "display": "<rect x=\"3\" y=\"4\" width=\"18\" height=\"12\" rx=\"2\"/><path d=\"M9 20h6m-3-4v4M7 12h10\"/>",
    "graphics": "<rect x=\"3\" y=\"6\" width=\"18\" height=\"12\" rx=\"2\"/><circle cx=\"12\" cy=\"12\" r=\"3.3\"/><path d=\"m12 8.7 1 2.3 2.3 1-2.3 1-1 2.3-1-2.3-2.3-1 2.3-1ZM7 3v3m5-3v3m5-3v3M7 18v3m5-3v3m5-3v3\"/>",
    "gamesettings": "<path d=\"m12 3 8 4.5v9L12 21l-8-4.5v-9L12 3Z\"/><path d=\"m4.5 7.8 7.5 4.4 7.5-4.4M12 12.2v8M7 12v4m-2-2h4m7-1h.01m2 2h.01\"/>",
    "devices": "<rect x=\"3\" y=\"4\" width=\"12\" height=\"10\" rx=\"2\"/><path d=\"M7 18h4m-2-4v4\"/><rect x=\"17\" y=\"10\" width=\"4\" height=\"10\" rx=\"2\"/><path d=\"M19 13v2\"/>",
    "gamepad": "<path d=\"M8 7h8a4 4 0 0 1 4 3.2l1 6.1a2.7 2.7 0 0 1-4.4 2.4l-2.1-1.9h-5l-2.1 1.9A2.7 2.7 0 0 1 3 16.3l1-6.1A4 4 0 0 1 8 7Z\"/><path d=\"M7 10v5m-2.5-2.5h5M15.5 11h.01m2.5 2.5h.01M9 4h6\"/>",
    "preferences": "<path d=\"M4 6h7m4 0h5M4 12h2m4 0h10M4 18h10m4 0h2\"/><rect x=\"11\" y=\"4\" width=\"4\" height=\"4\" rx=\"1\"/><rect x=\"6\" y=\"10\" width=\"4\" height=\"4\" rx=\"1\"/><rect x=\"14\" y=\"16\" width=\"4\" height=\"4\" rx=\"1\"/>",
    "phone": "<rect x=\"6\" y=\"2.5\" width=\"12\" height=\"19\" rx=\"2.5\"/><path d=\"M10 5h4M11 18.5h2\"/>",
    "shortcuts": "<rect x=\"2.5\" y=\"5.5\" width=\"19\" height=\"13\" rx=\"2\"/><path d=\"M6 9h.01m4 0h.01m4 0h.01m4 0h.01M6 12h.01m4 0h.01m4 0h.01m4 0h.01M7 15h10\"/>",
    "userdata": "<ellipse cx=\"12\" cy=\"5\" rx=\"8\" ry=\"3\"/><path d=\"M4 5v7c0 1.7 3.6 3 8 3s8-1.3 8-3V5M4 12v7c0 1.7 3.6 3 8 3s8-1.3 8-3v-7\"/><path d=\"M8 10h.01M8 17h.01\"/>",
    "advanced": "<path d=\"m9.5 3-.6 2.2-2 .9-2.1-.7-2.5 4.2L4 11.2v2.2l-1.7 1.5 2.5 4.2 2.1-.7 2 .9.6 2.2h5l.6-2.2 2-.9 2.1.7 2.5-4.2-1.7-1.5v-2.2l1.7-1.6-2.5-4.2-2.1.7-2-.9-.6-2.2Z\"/><circle cx=\"12\" cy=\"12.2\" r=\"3.2\"/>",
    "about": "<circle cx=\"12\" cy=\"12\" r=\"9\"/><path d=\"M12 11v6M12 7h.01\"/>",
    "close": "<path d=\"m6 6 12 12M6 18 18 6\"/>",
    "palette": "<path d=\"M12 3a9 9 0 1 0 0 18h1a2 2 0 0 0 1.6-3.2 1.6 1.6 0 0 1 1.3-2.6H18a3 3 0 0 0 3-3c0-5-4-9.2-9-9.2Z\"/><circle cx=\"7\" cy=\"11\" r=\".8\"/><circle cx=\"9\" cy=\"6.8\" r=\".8\"/><circle cx=\"14\" cy=\"6.5\" r=\".8\"/><circle cx=\"17.5\" cy=\"10\" r=\".8\"/>",
    "headphones": "<path d=\"M4 14v-3a8 8 0 0 1 16 0v3\"/><rect x=\"3\" y=\"12\" width=\"4\" height=\"8\" rx=\"2\"/><rect x=\"17\" y=\"12\" width=\"4\" height=\"8\" rx=\"2\"/>"
};
var aliases = {
    "gamesettings": "gamesettings",
    "controller": "gamepad",
    "smartphone": "phone",
    "keyboard": "shortcuts",
    "info": "about",
    "harddrive": "userdata",
    "userdata": "userdata",
    "engine": "performance",
    "resolution": "display",
    "device": "devices",
    "audio": "headphones",
    "sound": "headphones",
    "theme": "palette",
    "storage": "userdata",
    "graphics": "graphics"
};

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

paths.optimizer = "<path d=\"m13.5 2-9 12h6l-1 8 10-13h-6l1-7Z\"/>";

// Resolve actual native page sources and opaque OEM IDs, never tab positions.
function classifyTab(value) {
    var key = String(value === undefined || value === null ? "" : value)
        .replace(/^.*[\\/]/, "").toLowerCase().replace(/[^a-z0-9]/g, "");
    if (/optimizer|optimiser|boost/.test(key)) return "optimizer";
    if (/performance|engine|cpu|framerate/.test(key)) return "performance";
    if (/display|resolution|dpi/.test(key)) return "display";
    if (/graphic|renderer|gpu/.test(key)) return "graphics";
    if (/gamepad|controller|joystick/.test(key)) return "controller";
    if (/gamesetting|appsetting|gameconfig/.test(key)) return "gameSettings";
    if (/phone|smartphone/.test(key)) return "smartphone";
    if (/device|peripheral/.test(key)) return "devices";
    if (/theme|palette|appearance/.test(key)) return "palette";
    if (/advance/.test(key)) return "advanced";
    if (/shortcut|keyboard|keymap/.test(key)) return "keyboard";
    if (/about|info/.test(key)) return "info";
    if (/audio|sound|headphone/.test(key)) return "headphones";
    if (/storage|userdata|harddrive/.test(key)) return "harddrive";
    if (/preference/.test(key)) return "preferences";
    return "";
}
function kindForTab(id, pageSource, label) {
    return classifyTab(pageSource) || classifyTab(id) || classifyTab(label) || "preferences";
}

// Static transparent 48px fallback; all Settings assets are silver-white.
var pngFiles = {
  "performance": "performance",
  "display": "display",
  "graphics": "graphics",
  "gamesettings": "game-settings",
  "devices": "devices",
  "gamepad": "gamepad",
  "preferences": "preferences",
  "phone": "phone",
  "shortcuts": "shortcuts",
  "userdata": "user-data",
  "advanced": "advanced",
  "about": "about",
  "close": "close",
  "palette": "palette",
  "headphones": "headphones",
  "optimizer": "optimizer"
};
function pngUrl(kind) {
    return Qt.resolvedUrl("BSISettingsIcons/png/" + pngFiles[normalizedKind(kind)] + ".png");
}
