.pragma library

var packs = [
    { id: "carbon", name: "VOID / Carbon", tint: "#707177", accent: "#d8faec", radius: 4, height: 32, padding: 12, motion: "fade", icons: "line" },
    { id: "amethyst", name: "Amethyst", tint: "#9c84b9", accent: "#c5a6f7", radius: 12, height: 36, padding: 18, motion: "rise", icons: "rounded" },
    { id: "arctic", name: "Arctic", tint: "#bacde1", accent: "#8acbff", radius: 8, height: 34, padding: 16, motion: "slide", icons: "line" },
    { id: "ember", name: "Ember", tint: "#c67f73", accent: "#ff9e8c", radius: 2, height: 32, padding: 12, motion: "fade", icons: "bold" },
    { id: "matrix", name: "Matrix", tint: "#7e9e87", accent: "#91edb1", radius: 0, height: 30, padding: 10, motion: "slide", icons: "bold" },
    { id: "nord", name: "Nord", tint: "#b0d0d5", accent: "#a6e3ee", radius: 10, height: 38, padding: 20, motion: "scale", icons: "rounded" }
];
function create(index) {
    var p = packs[index];
    return { schema: "bsi.skin", version: 1, name: p.name, base: p.id,
        accent: p.accent, surface: "#101012", radius: p.radius, height: p.height,
        padding: p.padding, motion: p.motion, duration: 240, iconStyle: p.icons,
        palette: {}, tokens: {}, icons: [] };
}
