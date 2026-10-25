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
    const out = [];
    for (let i = 0; i < index.length; ++i) {
        const item = index[i];
        const row = item.row;
        if (kind === 1 && row.kind !== "image" && row.kind !== "svg") continue;
        if (kind === 2 && row.kind !== "svg") continue;
        if (kind === 3 && !row.text) continue;
        if (kind === 4 && row.kind !== "binary") continue;
        let matches = true;
        for (let t = 0; t < terms.length; ++t) {
            if (item.text.indexOf(terms[t]) < 0) { matches = false; break; }
        }
        if (matches) out.push(row); // preserve the backend index, not the filtered position
    }
    return out;
}
