.pragma library

var packs = [
    { id: "carbon", name: "VOID / Carbon", tint: "#18181f", accent: "#d8faec", radius: 8, height: 32, padding: 12, motion: "fade", icons: "line" },
    { id: "amethyst", name: "Amethyst", tint: "#68538e", accent: "#c5a6f7", radius: 12, height: 36, padding: 18, motion: "rise", icons: "rounded" },
    { id: "arctic", name: "Arctic", tint: "#3b62ad", accent: "#8acbff", radius: 8, height: 34, padding: 16, motion: "slide", icons: "line" },
    { id: "matrix", name: "Matrix", tint: "#b7744f", accent: "#91edb1", radius: 4, height: 30, padding: 10, motion: "slide", icons: "bold" },
    { id: "ember", name: "Ember", tint: "#b97577", accent: "#ff9e8c", radius: 6, height: 32, padding: 12, motion: "fade", icons: "bold" },
    { id: "nord", name: "Nord", tint: "#42867e", accent: "#a6e3ee", radius: 10, height: 38, padding: 20, motion: "scale", icons: "rounded" }
];
function create(index) {
    var p = packs[index];
    return { schema: "bsi.skin", version: 1, name: p.name, base: p.id,
        accent: p.accent, surface: "#101012", radius: p.radius, height: p.height,
        padding: p.padding, motion: p.motion, duration: 240, iconStyle: p.icons, playerUi: true,
        palette: {}, tokens: {}, icons: [] };
}
