.pragma library

// Metadata-only search. Keep normalization outside the keystroke loop; never
// read/decompress resources or change the selected editor just to filter rows.
function normalize(text) {
    return String(text || "").toLowerCase().replace(/\\/g, "/");
}

function prepare(rows) {
    const out = [];
    for (let i = 0; i < rows.length; ++i) {
        const row = rows[i];
        out.push({ row: row, text: normalize(row.path) + "\n" + normalize(row.name)
                                 + "\n" + normalize(row.format) + "\n" + normalize(row.kind) });
    }
    return out;
}

function filter(index, query, kind) {
    // All words must match; quoted phrases remain together. Literal matching
    // (not regex) makes dots, brackets and slashes in file paths predictable.
    const terms = [];
    const tokens = normalize(query).match(/"[^"]+"|\S+/g) || [];
    for (let i = 0; i < tokens.length; ++i)
        terms.push(tokens[i].replace(/^"|"$/g, ""));
    let min = 0, max = -1;
    const words = [];
    for (const term of terms) {
        const range = /^(min|max):([0-9]+(?:\.[0-9]+)?)(b|kb|mb)?$/.exec(term);
        if (!range) { words.push(term); continue; }
        const scale = range[3] === "mb" ? 1048576 : (range[3] === "kb" ? 1024 : 1);
        const value = +range[2] * scale;
        if (range[1] === "min") min = Math.max(min, value); else max = max < 0 ? value : Math.min(max, value);
    }
    const out = [];
    for (let i = 0; i < index.length; ++i) {
        const item = index[i];
        const row = item.row;
        if (kind === 1 && row.kind !== "image" && row.kind !== "svg") continue;
        if (kind === 2 && row.kind !== "svg") continue;
        if (kind === 3 && !row.text) continue;
        if (kind === 4 && row.kind !== "binary") continue;
        if (kind === 5 && !/^(ttf|otf|woff|woff2)$/i.test(row.format || "")) continue;
        if (kind === 6 && !row.modified) continue;
        if ((min > 0 && row.size < min) || (max >= 0 && row.size > max)) continue;
        let matches = true;
        for (let t = 0; t < words.length; ++t) {
            if (item.text.indexOf(words[t]) < 0) { matches = false; break; }
        }
        if (matches) out.push(row); // preserve the backend index, not the filtered position
    }
    return out;
}
