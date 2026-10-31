import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import { performance } from 'node:perf_hooks';

const context = vm.createContext({});
vm.runInContext(fs.readFileSync(new URL('../src/qml/ResourceSearch.js', import.meta.url), 'utf8')
    .replace(/^\.pragma library\s*/, ''), context);
const rows = [
    { index: 17, path: 'ui/PlrSidebar.qml', name: 'PlrSidebar.qml', format: 'QML', kind: 'text', text: true },
    { index: 23, path: 'images/Sidebar Dark.svg', name: 'Sidebar Dark.svg', format: 'SVG', kind: 'svg', text: true },
    { index: 42, path: 'images/logo.png', name: 'logo.png', format: 'PNG', kind: 'image', text: false },
    { index: 99, path: 'cache/PlrSidebar.qmlc', name: 'PlrSidebar.qmlc', format: 'QMLC', kind: 'binary', text: false },
    { index: 104, path: 'data/[theme].json', name: '[theme].json', format: 'JSON', kind: 'text', text: true },
];
const prepared = context.prepare(rows);
const ids = (query, kind = 0) => Array.from(context.filter(prepared, query, kind), row => row.index);
assert.deepEqual(ids('  SIDEBAR qml '), [17, 99]);
assert.deepEqual(ids('UI\\PLRSIDEBAR'), [17]);
assert.deepEqual(ids('sidebar svg'), [23]);
assert.deepEqual(ids('"sidebar dark"'), [23]);
assert.deepEqual(ids('[theme].json'), [104]);
assert.deepEqual(ids(''), [17, 23, 42, 99, 104]);
assert.deepEqual(ids('   '), ids(''));
assert.deepEqual(ids('absent'), []);
assert.deepEqual(ids('', 1), [23, 42]); // SVG counts as a picture
assert.deepEqual(ids('', 2), [23]);
assert.deepEqual(ids('', 3), [17, 23, 104]); // SVG also has editable code
assert.deepEqual(ids('', 4), [99]);
assert.equal(context.filter(prepared, 'sidebar', 0)[0], rows[0]);
assert.equal(rows[0].index, 17); // no filtered-position/EXE-offset mixup
assert.equal(Object.hasOwn(rows[0], 'searchText'), false); // metadata is not mutated

const sizes = context.prepare([
    {index: 1, path: 'fonts/ui.ttf', format: 'TTF', kind: 'binary', size: 4096, modified: true},
    {index: 2, path: 'icons/logo.png', format: 'PNG', kind: 'image', size: 512, modified: false},
    {index: 3, path: 'ui/panel.qml', format: 'QML', kind: 'text', text: true, size: 2048, modified: true},
]);
const matches = (query, kind = 0) => Array.from(context.filter(sizes, query, kind), r => r.index);
assert.deepEqual(matches('min:1kb max:4kb'), [1,3]);
assert.deepEqual(matches('max:0.5kb'), [2]);
assert.deepEqual(matches('min:1mb'), []);
assert.deepEqual(matches(''), [1,2,3]);
assert.deepEqual(matches('', 5), [1]);
assert.deepEqual(matches('', 6), [1,3]);
assert.deepEqual(matches('panel min:1kb', 6), [3]);
assert.deepEqual(matches('min:4kb max:1kb'), []);
assert.deepEqual(matches('min:invalid'), []);

const large = Array.from({ length: 20000 }, (_, index) => ({
    index, path: `ui/file_${index}.qml`, name: `file_${index}.qml`, format: 'QML', kind: 'text', text: true,
}));
const index = context.prepare(large);
const start = performance.now();
for (let i = 0; i < 100; ++i) {
    const found = context.filter(index, `file_${i}.qml`, 3);
    assert.equal(found.length, 1);
    assert.equal(found[0].index, i);
}
console.log(`PASS: resource metadata search; 100 filters over 20,000 rows: ${(performance.now() - start).toFixed(1)} ms`);
