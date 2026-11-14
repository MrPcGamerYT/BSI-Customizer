/* Fixed, offline preview catalog. Native AddonPacks is the write authority. */
(() => {
  "use strict";
  const packs = [
    {
      id: "carbon",
      name: "VOID / Carbon",
      accent: "#c4c4c8",
      radius: 8,
      height: 32,
      padding: 12,
      motion: "fade",
      desc: "Monochrome. Compact. Nothing unnecessary.",
    },
    {
      id: "amethyst",
      name: "Amethyst",
      accent: "#ac8cff",
      radius: 12,
      height: 36,
      padding: 18,
      motion: "rise",
      desc: "Soft geometry with a violet highlight.",
    },
    {
      id: "arctic",
      name: "Arctic",
      accent: "#69b7ff",
      radius: 8,
      height: 34,
      padding: 16,
      motion: "glide",
      desc: "Cool blue. Balanced spacing. Clear hierarchy.",
    },
    {
      id: "ember",
      name: "Ember",
      accent: "#ff727d",
      radius: 6,
      height: 32,
      padding: 12,
      motion: "snap",
      desc: "Compact rounding and a restrained red accent.",
    },
    {
      id: "matrix",
      name: "Matrix",
      accent: "#70e5aa",
      radius: 4,
      height: 30,
      padding: 10,
      motion: "step",
      desc: "Dense controls. Tight corners. Signal green.",
    },
    {
      id: "nord",
      name: "Nord",
      accent: "#88c0d0",
      radius: 10,
      height: 38,
      padding: 20,
      motion: "scale",
      desc: "Room to breathe, with a quiet frost accent.",
    },
  ];
  window.BsiAddons = {
    packs,
    create({ esc, button, request, getState }) {
      let selected = "carbon",
        staged = "",
        plan = null,
        history = [];
      const ws = (action, options = {}) =>
        request("workspace", { action, options });
      const valid = () =>
        plan?.ok &&
        plan.outputSha256 &&
        plan.path === getState().exePath &&
        plan.revision === getState().workspaceRevision;
      function render(page) {
        if (page === 13)
          return `<section class="sec"><div class="collection-eyebrow">RECOVERY CENTER</div><h2>Keep a way back.</h2><p class="note">Only Workspace and Add-ons transactions appear here. Legacy editor backups remain on their original pages. Restore refuses updated or independently modified executables.</p>${button("Refresh snapshots", "addon:history")}<div class="recovery-list">${history.map((row, i) => `<article class="editor-card"><h3>${esc(row.id)}</h3><pre class="editor-result">${esc(JSON.stringify(row, null, 2))}</pre>${button("Restore this snapshot…", "addon:restore", false, `data-index="${i}"`)}</article>`).join("") || '<p class="empty">Refresh to load recovery journals. Nothing is read or written on page entry.</p>'}</div></section>`;
        const p = packs.find((p) => p.id === selected);
        return `<section class="sec collection"><div class="collection-head"><div><div class="collection-eyebrow">THE PLAYER COLLECTION / 01—06</div><h2>Same dark canvas.<br>A different character.</h2></div><span class="settings-badge">6 CURATED PACKS</span></div><p class="note">Select a design to replay its preview. Native installation changes matched QML palette colors and supported button radius, height and padding only. Motion below is preview-only; emulator animations, icons, native dialogs, optimizer and boot are unchanged. These packs do not install the full VOID emulator UI.</p><div class="pack-grid">${packs.map((p) => `<button class="pack-card ${p.id === selected ? "selected" : ""}" data-addon-preview="${p.id}" aria-pressed="${p.id === selected}" style="--pack:${p.accent};--radius:${p.radius}px"><span class="pack-sample"><i></i><b>𝓥𝓞𝓘𝓓</b><span>Play</span></span><span class="pack-title">${p.name}</span><small>${p.desc}</small></button>`).join("")}</div><div class="pack-detail" style="--pack:${p.accent};--radius:${p.radius}px"><div class="player-preview motion-${p.motion}"><div class="preview-top"><b>𝓥𝓞𝓘𝓓</b><span>—　□　×</span></div><div class="preview-body"><aside>⌂<br>◈<br>⚙</aside><div class="preview-content"><small>YOUR SPACE. YOUR RULES.</small><h3>${p.name}</h3><p>Developed By NOVA XIS</p><div class="preview-game">PLAYER INTERFACE<br><button type="button" data-addon-preview="${p.id}" style="height:${p.height}px;padding:0 ${p.padding}px">Replay preview →</button></div><small>youtube.com/@nova_xis20</small></div></div></div><div class="pack-spec"><div class="collection-eyebrow">SELECTED DESIGN</div><h3>${p.name}</h3><p>${p.desc}</p><dl><dt>Button corners</dt><dd>${p.radius}px</dd><dt>Button height</dt><dd>${p.height}px</dd><dt>Horizontal padding</dt><dd>${p.padding}px</dd><dt>Preview motion</dt><dd>${p.motion} · not installed</dd></dl>${button("1. Stage this pack", "addon:stage", true, `data-id="${p.id}"`)}${button("2. Verify Dry Run", "addon:dry", false, "")}${valid() ? button("3. Review & apply…", "addon:apply", true) : '<button class="btn" disabled>3. Run a fresh Dry Run first</button>'}<p class="note">Existing queued edits are never discarded. Close every player instance before applying. Unsupported targets are refused.</p></div></div>${staged ? `<p class="note">Staged pack: ${esc(staged)}. Selecting another preview does not replace your queued pack. Use Workspace to review or explicitly clear the batch.</p>` : ""}${plan ? `<pre class="editor-result">${esc(JSON.stringify(plan, null, 2))}</pre>` : ""}<p class="note">Need the original complete VOID pack? It is a separate delivery; these six scoped collections are not a full emulator skin replacement.</p></section>`;
      }
      return {
        render,
        select(id) {
          if (packs.some((p) => p.id === id)) selected = id;
        },
        reset() {
          plan = null;
          history = [];
          staged = "";
        },
        async action(name, el) {
          if (name === "stage") {
            plan = null;
            await ws("queue-addon", { id: el.dataset.id });
            staged = el.dataset.id;
          } else if (name === "dry") {
            plan = null;
            const result = await ws("dry-run");
            plan = {
              ...result,
              path: getState().exePath,
              revision: getState().workspaceRevision,
            };
          } else if (name === "apply") {
            if (!valid())
              throw Error("Target or batch changed. Run Dry Run again.");
            if (
              !(await BsiEffects.confirm(
                "Apply the verified palette and button geometry to the selected player? Close all emulator instances first. Motion, icons, boot and native dialogs are not included. A hash-verified recovery snapshot will be retained.",
              ))
            )
              return;
            if (!valid())
              throw Error(
                "Plan changed during confirmation. Run Dry Run again.",
              );
            const hash = plan.outputSha256;
            plan = null;
            await ws("apply", { outputSha256: hash });
            staged = "";
          } else if (name === "history") {
            const r = await ws("history");
            history = r.entries || [];
          } else if (name === "restore") {
            const row = history[Number(el.dataset.index)];
            if (!row) throw Error("Refresh recovery journals.");
            if (
              !(await BsiEffects.confirm(
                "Restore snapshot " +
                  row.id +
                  "? The native engine requires the exact post-patch file hash and a closed player.",
              ))
            )
              return;
            plan = null;
            await ws("restore", { id: row.id });
            const r = await ws("history");
            history = r.entries || [];
          }
        },
      };
    },
  };
})();
