.pragma library

// Navigation metadata only. Opening/searching this catalog never invokes an engine.
var tools = [
    { title: "Overview & installations", detail: "Choose a player installation; detect installations explicitly.", route: 0, terms: "home target install exe path" },
    { title: "Color studio", detail: "Edit supported theme colors and gradients.", route: 1, terms: "theme palette recolor hex" },
    { title: "Resource browser", detail: "Manually scan, search, preview and replace embedded resources.", route: 2, terms: "icons images svg fonts qml extract logo scan" },
    { title: "Performance & FPS", detail: "Inspect supported frame-rate patches before applying.", route: 3, terms: "fps frames limit performance" },
    { title: "Integrity & safety", detail: "Inspect integrity support and restore existing patches.", route: 4, terms: "integrity safety check" },
    { title: "Boot experience", detail: "Edit the existing boot-video workflow without changing it on entry.", route: 5, terms: "startup video mp4 splash loading" },
    { title: "Player interface", detail: "Supported sidebar, layout, Settings and About editors.", route: 6, terms: "sidebar buttons order hide settings about layout typography font spacing icons titlebar" },
    { title: "Discord application ID", detail: "Inspect or patch the player's existing Discord ID.", route: 7, terms: "rpc presence discord app id names" },
    { title: "Animated presence & corners", detail: "Configure GIF RPC, activity images and emulator-window corners.", route: 8, terms: "gif rpc discord round rounded corners window images buttons" },
    { title: "Compatibility inspection", detail: "Open Workspace, then explicitly inspect the selected EXE.", route: 9, terms: "supported mapping report capacity compatibility" },
    { title: "Review staged changes", detail: "Review queued operations; Dry Run before confirming a single-EXE batch.", route: 9, terms: "workspace pending batch apply dry run conflicts changes" },
    { title: "Portable customization profiles", detail: "Load/save staged profiles in the existing Workspace editor.", route: 9, terms: "json import export presets profile" },
    { title: "Debloat & disk manager", detail: "Explicit ADB, Android and virtual-disk operations.", route: 10, terms: "adb android root vhd disk apps device instance optimizer" },
    { title: "Backups & recovery", detail: "Manually load snapshots, inspect metadata and confirm a hash-checked restore.", route: 11, terms: "backup history snapshots rollback restore recovery undo" },
    { title: "Add-ons & Skin Studio", detail: "Edit supported native player components, colors and icon families.", route: 12, terms: "skins packs icons theme design collection animation radius" },
    { title: "Customizer appearance", detail: "Application theme, accent, background animation and reduced motion.", tab: 0, terms: "app settings motion animation font background accent" },
    { title: "Updates", detail: "Review customizer updates in Settings.", tab: 1, terms: "version download update release" },
    { title: "About the customizer", detail: "Application information in Settings.", tab: 2, terms: "developer credits about" }
];
function search(query) {
    var stopWords = ["a", "an", "the", "to", "for", "my", "change", "edit", "open", "show", "find", "how", "do", "i", "can", "please"];
    var words = String(query || "").toLowerCase().trim().split(/\s+/).filter(function(s) { return s.length > 0 && stopWords.indexOf(s) < 0; });
    return tools.map(function(tool, index) {
        var title = tool.title.toLowerCase();
        var haystack = (title + " " + tool.detail + " " + tool.terms).toLowerCase();
        var score = 0;
        for (var i = 0; i < words.length; ++i) {
            if (haystack.indexOf(words[i]) < 0) return null;
            score += title.indexOf(words[i]) === 0 ? 8 : title.indexOf(words[i]) >= 0 ? 4 : 1;
        }
        return { tool: tool, score: score, order: index };
    }).filter(function(row) { return row !== null; }).sort(function(a,b) {
        return b.score - a.score || a.order - b.order;
    }).map(function(row) { return row.tool; });
}
function operation(op) {
    op = op || {};
    var kind = String(op.kind || "unknown");
    var keys = Object.keys(op.edits || {}).sort();
    if (kind === "theme") return { title: op.name || "Theme palette", detail: keys.length + " color edit(s): " + keys.slice(0,8).join(", "), kind: kind };
    if (kind === "appearance") return { title: "Player appearance", detail: keys.length + " token edit(s): " + keys.slice(0,8).join(", "), kind: kind };
    if (kind === "resource" || kind === "player-qml") return { title: op.path || "Embedded resource", detail: kind === "player-qml" ? "Compiled player QML component" : "Embedded resource replacement", kind: kind };
    return { title: op.name || op.path || kind, detail: "Inspect this operation in Workspace.", kind: kind };
}
function normalizedPath(value) {
    var p = String(value || "").replace(/\\/g,"/").replace(/\/+$/, "");
    return /^[a-z]:\//i.test(p) || p.indexOf("//") === 0 ? p.toLowerCase() : p;
}
function sameTarget(a,b) { return !!a && !!b && normalizedPath(a) === normalizedPath(b); }
function filename(value) { return String(value || "").replace(/\\/g,"/").split("/").pop() || "No target selected"; }
function bytes(value) {
    if (value === undefined || value === null || value === "") return "Not recorded";
    var n = Number(value);
    if (!isFinite(n) || n < 0) return "Not recorded";
    return n < 1024 ? n + " B" : n < 1048576 ? (n/1024).toFixed(1) + " KiB" : (n/1048576).toFixed(2) + " MiB";
}
function history(entries,query,target) {
    var words = String(query || "").toLowerCase().trim().split(/\s+/).filter(function(s) { return !!s; });
    return (entries || []).filter(function(row) {
        if (target && !sameTarget(row.path,target)) return false;
        var haystack = [row.id,row.path,row.state,row.time].join(" ").toLowerCase();
        return words.every(function(word) { return haystack.indexOf(word) >= 0; });
    });
}
function stateLabel(state) {
    return state === "applied" ? "Applied" : state === "restored" ? "Restored" : state === "prepared" ? "Prepared / needs verification" : "Unknown state";
}
