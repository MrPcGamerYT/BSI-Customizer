import assert from "node:assert/strict";
import fs from "node:fs";
import vm from "node:vm";
const read = (p) =>
  fs.readFileSync(new URL("../" + p, import.meta.url), "utf8");
const ctx = { window: {}, BsiEffects: { confirm: async () => true } };
vm.createContext(ctx);
vm.runInContext(read("src/web/addons.js"), ctx);
const calls = [];
let revision = 0,
  path = "player.exe",
  refuse = false,
  queued = false;
const ui = ctx.window.BsiAddons.create({
  esc: String,
  button: (l, a) => `[${a}]`,
  getState: () => ({ exePath: path, workspaceRevision: revision }),
  request: async (method, args) => {
    calls.push({ method, ...args });
    if (args.action === "queue-addon") {
      if (queued) throw Error("existing batch");
      queued = true;
      revision++;
      return { ok: true };
    }
    if (args.action === "dry-run") {
      if (refuse) throw Error("unsupported");
      return { ok: true, outputSha256: "verified" };
    }
    if (args.action === "apply") {
      queued = false;
      revision++;
      return { ok: true };
    }
    if (args.action === "history")
      return { ok: true, entries: [{ id: "real-journal-id" }] };
    return { ok: true };
  },
});
const packs = ctx.window.BsiAddons.packs;
assert.equal(packs.length, 6);
const native = read("src/core/AddonPacks.h");
for (const p of packs) {
  assert.ok(native.includes(`{"${p.id}",`));
  assert.ok(native.includes(`, ${p.radius}, ${p.height}, ${p.padding}}`));
}
assert.ok(ui.render(14).includes("preview-only"));
assert.equal(calls.length, 0, "render does not scan or patch");
await ui.action("stage", { dataset: { id: "carbon" } });
await assert.rejects(
  () => ui.action("stage", { dataset: { id: "nord" } }),
  /existing batch/,
);
await ui.action("dry");
assert.ok(ui.render(14).includes("[addon:apply]"));
revision++;
await assert.rejects(() => ui.action("apply"), /changed/);
await ui.action("dry");
path = "other.exe";
await assert.rejects(() => ui.action("apply"), /changed/);
path = "player.exe";
await ui.action("dry");
ctx.BsiEffects.confirm = async () => false;
const count = calls.length;
await ui.action("apply");
assert.equal(calls.length, count);
ctx.BsiEffects.confirm = async () => true;
await ui.action("apply");
assert.equal(calls.at(-1).action, "apply");
assert.equal(calls.at(-1).options.outputSha256, "verified");
await ui.action("dry");
refuse = true;
await assert.rejects(() => ui.action("dry"), /unsupported/);
assert.ok(
  !ui.render(14).includes("[addon:apply]"),
  "failed dry run drops prior approval",
);
await ui.action("history");
await ui.action("restore", { dataset: { index: "0" } });
assert.equal(calls.at(-2).options.id, "real-journal-id");
assert.ok(
  !calls.some((c) => c.action === "clear"),
  "Add-ons never silently clears batches",
);
const main = read("src/qml/Main.qml");
assert.equal((main.match(/SettingsPanel \{/g) || []).length, 1);
assert.ok(!main.includes("settingsPanelCentral"));
assert.ok(!main.includes("engine.rootObjects"));
assert.ok(read("CMakeLists.txt").includes("src/web/addons.js"));
console.log(
  "PASS: six catalog entries; no entry-time operations; busy backend remains authoritative; queue refusal; stale target/revision; confirmation cancellation; dry-run failure invalidation; exact apply/restore arguments; no implicit clear; one modal settings.",
);
