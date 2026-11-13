import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const source = readFileSync(new URL('../src/qml/WindowGeometry.js', import.meta.url), 'utf8')
  .replace(/^\.pragma library\s*/, '');
const context = {};
vm.runInNewContext(source, context, { filename: 'WindowGeometry.js' });

const screens = [
  { name: 'small laptop', x: 0, y: 0, width: 800, height: 560 },
  { name: 'HD laptop', x: 0, y: 0, width: 1366, height: 728 },
  { name: 'Full HD', x: 0, y: 0, width: 1920, height: 1040 },
  { name: 'QHD', x: 0, y: 0, width: 2560, height: 1400 },
  { name: 'ultrawide', x: 0, y: 0, width: 3440, height: 1400 },
  { name: '4K', x: 0, y: 0, width: 3840, height: 2080 },
  { name: 'portrait', x: 0, y: 0, width: 1080, height: 1880 },
  { name: 'secondary monitor left of primary', x: -2560, y: 40, width: 2560, height: 1400 },
];

for (const screen of screens) {
  const geometry = context.initialGeometry(screen);
  assert.ok(geometry.width > 0 && geometry.height > 0, `${screen.name}: non-zero window`);
  assert.ok(geometry.width <= screen.width && geometry.height <= screen.height, `${screen.name}: fits available work area`);
  assert.ok(geometry.minimumWidth <= geometry.width && geometry.minimumHeight <= geometry.height, `${screen.name}: minimums fit initial size`);
  assert.ok(Math.abs((geometry.x * 2 + geometry.width) - (screen.x * 2 + screen.width)) <= 1, `${screen.name}: horizontally centered`);
  assert.ok(Math.abs((geometry.y * 2 + geometry.height) - (screen.y * 2 + screen.height)) <= 1, `${screen.name}: vertically centered`);
  const aspect = geometry.width / geometry.height;
  assert.ok(aspect >= 1.24 && aspect <= 1.61, `${screen.name}: usable landscape layout (${aspect})`);
  assert.ok(geometry.width <= 1920 && geometry.height <= 1180, `${screen.name}: large displays are capped`);
  console.log(`PASS: ${screen.name}: ${geometry.width}x${geometry.height} at (${geometry.x}, ${geometry.y})`);
}
