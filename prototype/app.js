"use strict";

// Keep the standalone preview isolated from browser globals.
(() => {
  const paths = {
    grid: '<rect x="3" y="3" width="7" height="7" rx="1.5"/><rect x="14" y="3" width="7" height="7" rx="1.5"/><rect x="3" y="14" width="7" height="7" rx="1.5"/><rect x="14" y="14" width="7" height="7" rx="1.5"/>',
    layers: '<path d="m12 3 10 5-10 5L2 8zM2 12l10 5 10-5M2 16l10 5 10-5"/>',
    palette:
      '<path d="M12 3a9 9 0 1 0 0 18h1a2 2 0 0 0 1-3.7 1.7 1.7 0 0 1 1-3.1h2A4 4 0 0 0 21 10c0-4-4-7-9-7Z"/><circle cx="7.5" cy="10" r=".8"/><circle cx="11" cy="7" r=".8"/><circle cx="15.5" cy="8" r=".8"/><circle cx="7.5" cy="14" r=".8"/>',
    folder:
      '<path d="M3 7V5a2 2 0 0 1 2-2h5l2 3h7a2 2 0 0 1 2 2v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/>',
    bolt: '<path d="m13 2-9 12h7l-1 8 10-13h-7z"/>',
    shield:
      '<path d="m12 3 8 3v6c0 5-8 9-8 9s-8-4-8-9V6z"/><path d="m8.5 12 2.5 2.5 4.5-5"/>',
    play: '<rect x="3" y="4" width="18" height="16" rx="3"/><path d="m10 8 6 4-6 4z"/>',
    sliders:
      '<path d="M4 7h7m4 0h5M4 17h3m4 0h9"/><circle cx="13" cy="7" r="2"/><circle cx="9" cy="17" r="2"/>',
    discord:
      '<path d="M8 5 5 6C3 9 2 13 3 17l4 2 1-2m8-12 3 1c2 3 3 7 2 11l-4 2-1-2M8 6c3-1 5-1 8 0M7 16c4 2 6 2 10 0"/><circle cx="8" cy="12" r="1"/><circle cx="16" cy="12" r="1"/>',
    image:
      '<rect x="3" y="3" width="18" height="18" rx="3"/><circle cx="8" cy="8" r="1.5"/><path d="m21 16-6-6L3 21"/>',
    settings:
      '<path d="m10 3-1 3-3 1-2 3 2 2v3l3 1 1 3h4l1-3 3-1v-3l2-2-2-3-3-1-1-3z"/><circle cx="12" cy="11" r="3"/>',
    sun: '<circle cx="12" cy="12" r="4"/><path d="M12 2v2m0 16v2M2 12h2m16 0h2M5 5l1.5 1.5m11 11L19 19M5 19l1.5-1.5m11-11L19 5"/>',
    info: '<circle cx="12" cy="12" r="9"/><path d="M12 11v6m0-10v.1"/>',
    arrow: '<path d="M4 12h16m-6-6 6 6-6 6"/>',
    arrowUp: '<path d="M6 18 18 6M6 6h12v12"/>',
    activity: '<path d="M2 12h5l3-8 4 16 3-8h5"/>',
    check: '<path d="m5 12 4 4L19 6"/>',
    clock: '<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>',
    search: '<circle cx="10.5" cy="10.5" r="6.5"/><path d="m16 16 5 5"/>',
    file: '<path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8zM14 2v6h6M8 13h8m-8 4h5"/>',
    upload: '<path d="M12 16V3m-5 5 5-5 5 5M4 16v4h16v-4"/>',
    refresh:
      '<path d="M20 7v5h-5M4 17v-5h5M5 7a8 8 0 0 1 13-2l2 3M4 16l2 3a8 8 0 0 0 13-2"/>',
    code: '<path d="m8 6-6 6 6 6m8-12 6 6-6 6m-3-15-2 18"/>',
    heart:
      '<path d="M20 5c-3-3-6-1-8 1-2-2-5-4-8-1-5 5 8 15 8 15S25 10 20 5Z"/>',
    monitor:
      '<rect x="2" y="3" width="20" height="14" rx="2"/><path d="M8 21h8m-4-4v4"/>',
  };
  const icon = (name) =>
    `<svg viewBox="0 0 24 24" aria-hidden="true">${paths[name] || paths.grid}</svg>`;
  const escapeHtml = (value) =>
    String(value).replace(
      /[&<>"']/g,
      (c) =>
        ({
          "&": "&amp;",
          "<": "&lt;",
          ">": "&gt;",
          '"': "&quot;",
          "'": "&#39;",
        })[c],
    );
  const nav = [
    { group: "WORKSPACE" },
    { id: "dashboard", label: "Overview", icon: "grid" },
    { group: "CUSTOMIZATION" },
    { id: "theme", label: "Theme studio", icon: "palette" },
    { id: "resources", label: "Resource explorer", icon: "folder" },
    { id: "fps", label: "Performance", icon: "bolt" },
    { id: "boot", label: "Boot experience", icon: "play" },
    { id: "ui", label: "Player interface", icon: "sliders" },
    { group: "TOOLBOX" },
    { id: "integrity", label: "Integrity & safety", icon: "shield" },
    { id: "rpc", label: "Discord presence", icon: "discord" },
    { id: "gif", label: "Animated presence", icon: "image", badge: "GIF" },
    { id: "workspace", label: "Patch workspace", icon: "layers" },
    { group: "APPLICATION" },
    { id: "appearance", label: "Appearance", icon: "sun" },
    { id: "about", label: "About BSI", icon: "info" },
  ];
  const palettes = {
    mint: {
      name: "Arctic mint",
      description: "Fresh & focused",
      accent: "#6ce6d0",
      background: "#101b22",
      secondary: "#aaa0ff",
    },
    violet: {
      name: "Midnight violet",
      description: "Quietly expressive",
      accent: "#b7a1ff",
      background: "#191525",
      secondary: "#f0a3c8",
    },
    blue: {
      name: "Electric blue",
      description: "Cool & precise",
      accent: "#78afff",
      background: "#101b2c",
      secondary: "#76dfdd",
    },
  };
  function readPreferences() {
    try {
      return JSON.parse(
        localStorage.getItem("bsi-concept-preferences") || "{}",
      );
    } catch {
      return {};
    }
  }
  const preferences = readPreferences();
  const state = {
    page: "dashboard",
    player: "BlueStacks 5",
    palette: "mint",
    colors: { ...palettes.mint },
    fps: 240,
    scanned: false,
    scanning: false,
    filter: "all",
    query: "",
    bootUrl: null,
    hold: 3000,
    muted: true,
    pending: new Map(),
    themeMode: "auto",
    ui: {
      "Hide promotional cards": true,
      "Simplify the home screen": true,
      "Show player toolbar": true,
      "Custom window title": false,
    },
    safety: {
      "Create a backup first": true,
      "Check file freshness": true,
      "Verify after writing": true,
    },
    presence: {
      name: "BlueStacks 5",
      details: "Making it my own",
      status: "In the customization studio",
      button: "My community",
    },
    appearance: {
      mode: preferences.mode === "light" ? "light" : "dark",
      density: preferences.density === "compact" ? "compact" : "comfortable",
      accent: /^#[0-9a-f]{6}$/i.test(preferences.accent || "")
        ? preferences.accent
        : "#6ce6d0",
    },
    history: [
      {
        label: "Design workspace initialized",
        detail: "Sample data · just now",
      },
      {
        label: "Original backup available",
        detail: "Example only · 2 minutes ago",
      },
      {
        label: "Sample player selected",
        detail: "BlueStacks 5 · 3 minutes ago",
      },
    ],
  };
  const main = document.getElementById("main");
  const dialog = document.getElementById("confirm-dialog");
  let toastTimer,
    confirmedAction = null;
  function fillIcons(root = document) {
    root.querySelectorAll("[data-icon]").forEach((el) => {
      el.innerHTML = icon(el.dataset.icon);
    });
  }
  function notify(message) {
    const toast = document.getElementById("toast");
    toast.textContent = message;
    toast.classList.add("show");
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => toast.classList.remove("show"), 4500);
  }
  function saveAppearance() {
    try {
      localStorage.setItem(
        "bsi-concept-preferences",
        JSON.stringify(state.appearance),
      );
    } catch {
      /* Storage is optional in private browsing. */
    }
  }
  function applyAppearance() {
    document.body.classList.toggle("light", state.appearance.mode === "light");
    document.body.classList.toggle(
      "compact",
      state.appearance.density === "compact",
    );
    const accent =
      state.appearance.mode === "light"
        ? "#" +
          state.appearance.accent
            .slice(1)
            .match(/../g)
            .map((x) =>
              Math.round(parseInt(x, 16) * 0.43)
                .toString(16)
                .padStart(2, "0"),
            )
            .join("")
        : state.appearance.accent;
    document.documentElement.style.setProperty("--accent", accent);
    document.documentElement.style.setProperty(
      "--accent-rgb",
      accent
        .slice(1)
        .match(/../g)
        .map((x) => parseInt(x, 16))
        .join(","),
    );
    saveAppearance();
  }
  function queue(key, label) {
    state.pending.set(key, label);
    notify(`${label} added to the demo workspace. No files changed.`);
  }
  function requestAction(title, description, action) {
    document.getElementById("dialog-title").textContent = title;
    document.getElementById("dialog-description").textContent = description;
    confirmedAction = action;
    dialog.returnValue = "cancel";
    dialog.showModal();
  }
  dialog.addEventListener("close", () => {
    if (dialog.returnValue === "confirm" && confirmedAction) confirmedAction();
    confirmedAction = null;
  });
  function go(page) {
    if (location.hash.slice(1) === page) render();
    else location.hash = page;
  }
  function button(text, action, type = "secondary", name = "arrow") {
    return `<button class="button ${type}" data-action="${action}">${name ? icon(name) : ""}${text}</button>`;
  }
  function heading(
    eyebrow,
    title,
    description,
    action = "Open workspace",
    target = "workspace",
  ) {
    return `<div class="page-heading"><div><div class="eyebrow">${eyebrow}</div><h1>${title}</h1><p class="subtitle">${description}</p></div>${action ? `<button class="button secondary" data-go="${target}">${icon("layers")}${action}</button>` : ""}</div>`;
  }
  function panelTitle(name, title, badge = "") {
    return `<div class="panel-title">${icon(name)}<h2>${title}</h2>${badge ? `<span class="status-pill neutral">${badge}</span>` : ""}</div>`;
  }
  function note(text) {
    return `<div class="note">${icon("info")}<span>${text}</span></div>`;
  }
  function actionBar(label, action) {
    return `<div class="action-bar"><p>Preview only. Your executable is never modified.</p><div class="buttons">${button("Reset", "reset-page", "secondary", "refresh")}${button(label, action, "primary", "layers")}</div></div>`;
  }
  function toggle(label, description, group, checked) {
    return `<div class="control-row"><div><h3>${label}</h3><p>${description}</p></div><button class="switch" role="switch" aria-label="${label}" aria-checked="${checked}" data-toggle="${group}" data-label="${label}"></button></div>`;
  }
  function activityList() {
    return `<ul class="activity-list">${state.history
      .slice(0, 3)
      .map(
        (h) =>
          `<li><i class="activity-dot"></i><div><b>${escapeHtml(h.label)}</b><small>${escapeHtml(h.detail)}</small></div></li>`,
      )
      .join("")}</ul>`;
  }
  function targetPanel() {
    return `<section class="panel installation"><div class="section-heading"><h2>Selected player</h2><span>SAMPLE</span></div><div class="install-heading"><span class="player-logo">${icon("layers")}</span><div><b>${state.player}</b><small>${state.player === "BlueStacks 5" ? "5.22.0 · Android 11" : "5.21.0 · Android 9"}</small></div><span class="status-pill">Demo</span></div><div class="install-path" title="Illustrative path only">C:\\Program Files\\${state.player === "BlueStacks 5" ? "BlueStacks_nxt" : "MSI App Player"}\\HD-Player.exe</div><div class="install-meta"><span>Qt resource format</span><b>Qt 6 / example</b></div><div class="install-meta"><span>Original backup</span><b style="color:var(--accent)">Example available</b></div>${button("Switch demo player", "switch-player", "secondary", "refresh")}</section>`;
  }
  function playerPreview() {
    return `<div class="preview-window" id="player-preview" style="--player-accent:${state.colors.accent};--player-bg:${state.colors.background};--player-secondary:${state.colors.secondary}"><div class="preview-titlebar">${icon("layers")} ${state.player}<span>− □ ×</span></div><div class="preview-player"><div class="player-search">Search games and apps</div><div class="app-icons"><div class="app-icon"><i>${icon("grid")}</i>System apps</div><div class="app-icon"><i>${icon("play")}</i>Media manager</div><div class="app-icon"><i>${icon("settings")}</i>Settings</div></div><div class="preview-bottom"><span>Your player. Your space.</span><button data-action="sample-launch">Explore games</button></div></div></div>`;
  }
  function dashboard() {
    const tools = [
      [
        "theme",
        "palette",
        "Theme studio",
        "A palette that feels like you.",
        "#6ce6d0",
      ],
      [
        "resources",
        "folder",
        "Resource explorer",
        "Find the details. Make them yours.",
        "#b4a0ff",
      ],
      ["fps", "bolt", "Performance", "A smoother way to play.", "#ecc58c"],
      [
        "boot",
        "play",
        "Boot experience",
        "Make a great first impression.",
        "#8fb7ff",
      ],
      [
        "rpc",
        "discord",
        "Discord presence",
        "Show the world what’s playing.",
        "#a6a7ff",
      ],
      [
        "ui",
        "sliders",
        "Player interface",
        "Less clutter. More you.",
        "#e8a1c1",
      ],
    ];
    return `${heading("YOUR PERSONAL CONTROL ROOM", "Make your player, yours.", "A little more personality. A lot more possibility.")}
<section class="hero"><div class="hero-copy"><span class="hero-tag">${icon("bolt")} A NEW WAY TO CUSTOMIZE</span><h2>Same player.<br>A whole new <em>experience.</em></h2><p>Fine-tune the look, feel, and little details of your emulator. All your customization tools, thoughtfully in one place.</p><div class="hero-links"><button class="button primary" data-go="theme">Open theme studio ${icon("arrow")}</button><button class="subtle" data-go="workspace">Explore workspace ${icon("arrowUp")}</button></div></div><div class="illustration" aria-hidden="true"><div class="orbit"></div><div class="orbit two"></div><div class="screen"><div class="screen-top"><i></i><i></i><i></i></div><div class="screen-inside"><div class="stack-symbol"><i></i><i></i><i></i></div><div class="screen-text">Your next look.<small>MAKE IT PERSONAL</small></div></div></div><span class="floating-dot"></span><div class="floating-chip">${icon("palette")}<div>A little more you.<small>Limitless possibilities.</small></div></div></div></section>
<div class="metrics">${[
      ["layers", "12", "Workspace screens"],
      ["palette", "3", "Curated palettes"],
      ["shield", "Local", "Privacy by design"],
      ["clock", String(state.pending.size), "Demo changes queued"],
    ]
      .map(
        ([i, v, l]) =>
          `<div class="metric"><span class="metric-icon">${icon(i)}</span><div><b>${v}</b><small>${l}</small></div></div>`,
      )
      .join("")}</div>
<div class="dashboard-grid"><section><div class="section-heading"><h2>Your creative toolkit</h2><span>One player. Endless possibilities.</span></div><div class="tool-grid">${tools.map(([id, i, title, desc, tone]) => `<button class="tool-card" data-go="${id}" style="--tone:${tone};--tint:${tone}0c"><span class="tool-icon">${icon(i)}</span><span class="card-arrow">${icon("arrowUp")}</span><h3>${title}</h3><p>${desc}</p></button>`).join("")}</div></section><aside class="dashboard-aside" aria-label="Player and activity">${targetPanel()}<section class="panel activity-panel"><div class="section-heading"><h2>Recent activity</h2><button data-go="workspace" aria-label="View all activity">${icon("arrowUp")}</button></div>${activityList()}</section></aside></div>
<div class="bottom-tip"><span>${icon("shield")}<span><b>A little peace of mind.</b> The real app’s backup and verification safeguards belong in the new design, too.</span></span><button data-go="integrity">Explore safety ${icon("arrow")}</button></div>`;
  }
  function theme() {
    return `${heading("LOOK & FEEL", "A new mood for your player.", "Explore curated palettes or build something unmistakably yours.")}<div class="editor-grid"><section class="panel">${panelTitle("palette", "Palette & colors", "DESIGN PREVIEW")}<div class="field"><label>Theme detection method</label><div class="segmented">${[
      ["auto", "Auto detect"],
      ["qml", "QML · new"],
      ["hex", "Hex · classic"],
    ]
      .map(
        ([id, label]) =>
          `<button data-method="${id}" class="${state.themeMode === id ? "selected" : ""}">${label}</button>`,
      )
      .join(
        "",
      )}</div></div><div class="field"><label>Start with a signature palette</label><div class="palette-grid">${Object.entries(
      palettes,
    )
      .map(
        ([id, p]) =>
          `<button class="palette ${id === state.palette ? "selected" : ""}" data-palette="${id}"><span class="swatches"><i style="--swatch:${p.accent}"></i><i style="--swatch:${p.secondary}"></i><i style="--swatch:${p.background}"></i></span><b>${p.name}</b><small>${p.description}</small></button>`,
      )
      .join(
        "",
      )}</div></div><div class="field"><label>Make it your own</label>${[
      ["accent", "Accent color", "Buttons, highlights & active elements"],
      ["background", "Player background", "The foundation of your workspace"],
      [
        "secondary",
        "Secondary accent",
        "Supporting details & subtle highlights",
      ],
    ]
      .map(
        ([key, label, desc]) =>
          `<div class="color-row"><div><b>${label}</b><small>${desc}</small></div><div class="color-value"><code id="value-${key}">${state.colors[key].toUpperCase()}</code><input type="color" value="${state.colors[key]}" data-color="${key}" aria-label="${label}"></div></div>`,
      )
      .join(
        "",
      )}</div>${actionBar("Add theme to workspace", "queue-theme")}</section><section><div class="section-heading"><h2>Live canvas</h2><span>Illustrative player preview</span></div>${playerPreview()}${note("Try a palette or adjust a color. The canvas updates instantly. This is a design illustration, not a screenshot or a live emulator.")}</section></div>`;
  }
  const resourceSamples = [
    {
      name: "UiThemeObject.qml",
      path: "qml/theme",
      type: "qml",
      size: "3.8 KB",
      icon: "code",
    },
    {
      name: "PlrBootProgressBar.qml",
      path: "qml/player",
      type: "qml",
      size: "6.2 KB",
      icon: "code",
    },
    {
      name: "player-logo.svg",
      path: "images/branding",
      type: "svg",
      size: "2.1 KB",
      icon: "image",
    },
    {
      name: "home-background.png",
      path: "images/backgrounds",
      type: "image",
      size: "148 KB",
      icon: "image",
    },
    {
      name: "PlrAboutSetting.qml",
      path: "qml/settings",
      type: "qml",
      size: "8.4 KB",
      icon: "code",
    },
    {
      name: "toolbar-home.svg",
      path: "images/toolbar",
      type: "svg",
      size: "980 B",
      icon: "image",
    },
  ];
  function resourceRows() {
    const rows = resourceSamples.filter(
      (r) =>
        (state.filter === "all" || state.filter === r.type) &&
        `${r.name} ${r.path}`.toLowerCase().includes(state.query.toLowerCase()),
    );
    return `<table class="resource-table"><thead><tr><th>RESOURCE NAME</th><th>TYPE</th><th>SIZE</th><th></th></tr></thead><tbody>${rows.map((r) => `<tr><td>${icon(r.icon)}<div>${r.name}<small>${r.path}</small></div></td><td><span class="status-pill neutral">${r.type.toUpperCase()}</span></td><td>${r.size}</td><td><button class="button secondary" data-inspect="${r.name}">Inspect ${icon("arrowUp")}</button></td></tr>`).join("")}</tbody></table>${!rows.length ? '<div class="empty-state"><h3>No matching sample resources</h3><p>Try another name or select a different file type.</p></div>' : ""}`;
  }
  function resources() {
    return `${heading("UNDER THE SURFACE", "Every detail, within reach.", "Explore the resources behind your player. Scanning always starts with you.")}<section class="panel"><div class="toolbar"><label class="search-field">${icon("search")}<input id="resource-search" type="search" aria-label="Search sample resources" placeholder="Search resources by name or path…" value="${escapeHtml(state.query)}" ${!state.scanned ? "disabled" : ""}></label><select id="resource-filter" aria-label="Resource file type"><option value="all">All file types</option>${[
      ["qml", "QML source"],
      ["svg", "SVG icons"],
      ["image", "Images"],
    ]
      .map(
        ([v, l]) =>
          `<option value="${v}" ${state.filter === v ? "selected" : ""}>${l}</option>`,
      )
      .join(
        "",
      )}</select><button class="button primary" data-action="scan-resources" ${state.scanning ? "disabled" : ""}>${icon("search")}${state.scanning ? "Scanning samples…" : state.scanned ? "Scan again" : "Scan resources"}</button></div><div id="resource-results">${state.scanned ? resourceRows() : `<div class="empty-state"><div class="empty-icon">${icon("folder")}</div><h2>A world of details, waiting.</h2><p>No scan runs automatically. Click Scan resources to explore a small set of illustrative resources.</p><span class="status-pill neutral">No executable is read by this prototype</span></div>`}</div></section><div style="margin-top:18px">${note("Sample data only. Real resource inspection and replacement will need an explicit, secured connection to the existing native engines.")}</div>`;
  }
  function performancePage() {
    return `${heading("SMOOTHER BY DESIGN", "Find your flow.", "Shape the performance controls around the way you play.")}<div class="editor-grid"><section class="panel">${panelTitle("bolt", "Frame rate target", "ILLUSTRATION")}<p class="subtitle">Choose the target you would like to review in the patch workspace.</p><div class="big-value"><span id="fps-value">${state.fps}</span><small>FPS</small></div><div class="fps-presets">${[60, 90, 120, 144, 240].map((v) => `<button class="${v === state.fps ? "selected" : ""}" data-fps="${v}">${v} FPS</button>`).join("")}</div><input type="range" min="30" max="360" step="1" value="${state.fps}" id="fps-range" aria-label="Target frame rate"><div class="range-labels"><span>30 FPS</span><span>360 FPS</span></div>${actionBar("Queue FPS target", "queue-fps")}</section><section class="panel">${panelTitle("monitor", "Before you apply")}<div class="progress-list"><div class="progress-item"><h3>Check the player build</h3><p>Support must come from an actual DLL inspection, not from a version label.</p></div><div class="progress-item"><h3>Close the emulator</h3><p>No changes should be written while the player is running.</p></div><div class="progress-item"><h3>Back up, patch, verify</h3><p>The native engine remains responsible for validation and safe writes.</p></div></div>${note("A target is not a performance guarantee. Your game, display, hardware, and player settings determine the actual frame rate.")}</section></div>`;
  }
  function boot() {
    return `${heading("THE FIRST IMPRESSION", "A better hello.", "Turn your player’s boot screen into a moment that feels personal.")}<div class="editor-grid"><section class="panel">${panelTitle("play", "Boot video")}<div class="upload-area">${icon("upload")}<h3>Give your startup a signature.</h3><p>Choose a local video to preview. Nothing is uploaded.</p><input type="file" accept="video/*" id="boot-file" aria-label="Choose a local boot video"></div><div class="field"><label for="boot-hold">Minimum hold time <span id="hold-label">${(state.hold / 1000).toFixed(1)} seconds</span></label><input type="range" id="boot-hold" min="0" max="10000" step="500" value="${state.hold}"><div class="range-labels"><span>No hold</span><span>10 seconds</span></div></div>${toggle("Mute boot audio", "Keep startup quiet, even with a custom video.", "boot", state.muted)}${actionBar("Queue boot settings", "queue-boot")}</section><section><div class="section-heading"><h2>Startup canvas</h2><span>Local browser preview</span></div><div class="boot-preview">${state.bootUrl ? `<video id="boot-video" controls ${state.muted ? "muted" : ""} src="${state.bootUrl}"></video>` : '<div><div class="stack-symbol"><i></i><i></i><i></i></div><p>Your story starts here.</p></div>'}</div><div style="margin-top:18px">${note("Selecting a video only opens it in this browser tab. The prototype does not encode, copy, or install it into your emulator.")}</div></section></div>`;
  }
  function playerInterface() {
    const desc = {
      "Hide promotional cards": "A quieter home screen with room to breathe.",
      "Simplify the home screen":
        "Keep frequently used actions front and center.",
      "Show player toolbar": "Keep navigation and controls within easy reach.",
      "Custom window title": "Give your emulator window a personal name.",
    };
    return `${heading("LESS CLUTTER. MORE YOU.", "Make space for what matters.", "A thoughtful home for your player’s interface controls.")}<div class="editor-grid"><section class="panel">${panelTitle("sliders", "Interface preferences")}${Object.entries(
      state.ui,
    )
      .map(([l, v]) => toggle(l, desc[l], "ui", v))
      .join(
        "",
      )}<div class="field" style="margin-top:20px"><label>Window title<input id="window-title" maxlength="80" placeholder="My personal player" value="${escapeHtml(state.windowTitle || "My personal player")}"></label></div>${actionBar("Queue interface settings", "queue-ui")}</section><section><div class="section-heading"><h2>A cleaner perspective</h2><span>Visual concept</span></div>${playerPreview()}${note("These are proposed controls for the new interface. Actual availability must be determined by the existing engine for each player build.")}</section></div>`;
  }
  function integrity() {
    return `${heading("CONFIDENCE, BUILT IN", "Personalize with peace of mind.", "Make safety visible, understandable, and part of every change.")}<div class="editor-grid"><section class="panel">${panelTitle("shield", "Your safety checklist", "CONCEPT")}<div class="control-row"><div><h3>Create a backup first</h3><p>Preserve the original file before making a change. Always required in this concept.</p></div><span class="status-pill">Always on</span></div><div class="control-row"><div><h3>Check file freshness</h3><p>Refuse a patch if the executable changed after inspection.</p></div><span class="status-pill">Always on</span></div><div class="control-row"><div><h3>Verify after writing</h3><p>Re-read the result before reporting a successful operation.</p></div><span class="status-pill">Always on</span></div><div class="action-bar"><p>Simulation only. No backups exist in this browser.</p>${button("Preview recovery flow", "recovery", "secondary", "refresh")}</div></section><section class="panel">${panelTitle("file", "Example verification report")}<div class="spec-list">${[
      ["Selected target", state.player],
      ["Inspection", "Not performed"],
      ["Backup", "Illustrative only"],
      ["Write status", "No writes performed"],
      ["Native verification", "Not connected"],
    ]
      .map(
        ([k, v]) => `<div class="spec-row"><span>${k}</span><b>${v}</b></div>`,
      )
      .join(
        "",
      )}</div><div style="margin-top:22px">${note("No security or integrity bypass is executed here. This screen demonstrates how the desktop app could communicate real verification results.")}</div></section></div>`;
  }
  function discord(animated = false) {
    return `${heading("LET YOUR PRESENCE SPEAK", animated ? "A presence with personality." : "You’re playing. Make it known.", animated ? "Explore an animated-presence editor with a clean, connected workflow." : "Bring your activity, community, and personal details together.")}<div class="editor-grid"><section class="panel">${panelTitle(animated ? "image" : "discord", animated ? "Animated presence" : "Activity details", "LOCAL PREVIEW")}<div class="field-grid"><label class="full">Application ID<input inputmode="numeric" id="application-id" placeholder="Your Discord application ID" maxlength="20" value="${escapeHtml(state.appId || "")}"></label><label class="full">Display name<input data-presence="name" maxlength="80" value="${escapeHtml(state.presence.name)}"></label><label class="full">Details<input data-presence="details" maxlength="100" value="${escapeHtml(state.presence.details)}"></label><label class="full">State<input data-presence="status" maxlength="100" value="${escapeHtml(state.presence.status)}"></label>${animated ? '<label class="full">Animated image URL<input id="gif-url" type="url" placeholder="https://your-host.example/presence.gif"><small class="subtitle">Design field only. No image URL is fetched.</small></label>' : ""}<label class="full">Button label<input data-presence="button" maxlength="32" value="${escapeHtml(state.presence.button)}"></label></div>${actionBar("Queue presence settings", animated ? "queue-gif" : "queue-rpc")}</section><section><div class="section-heading"><h2>Presence canvas</h2><span>Not connected to Discord</span></div><div class="discord-card"><div class="discord-profile"><div class="discord-avatar">PC</div><div><h3>mrpcgamer</h3><small>Preview profile</small></div></div><h4>PLAYING A GAME</h4><div class="discord-playing"><div class="game-icon">${icon(animated ? "image" : "layers")}</div><div><b id="presence-name">${escapeHtml(state.presence.name)}</b><p><span id="presence-details">${escapeHtml(state.presence.details)}</span><br><span id="presence-status">${escapeHtml(state.presence.status)}</span></p></div></div><div class="discord-buttons"><button id="presence-button" data-action="presence-example">${escapeHtml(state.presence.button)}</button></div></div><div style="margin-top:18px">${note("This is an illustrative card, not a Discord connection. The final app must report IPC activity separately from Discord accepting or displaying a presence.")}</div></section></div>`;
  }
  function workspace() {
    return `${heading("THOUGHTFUL CHANGES, ONE PLACE", "Your next look, lined up.", "Review every detail before anything touches your player.", "Back to overview", "dashboard")}<div class="editor-grid"><section class="panel">${panelTitle("layers", "Pending changes", `${state.pending.size} QUEUED`)}${state.pending.size ? `<div>${Array.from(state.pending, ([key, label]) => `<div class="log-entry">${icon("sliders")}<div><b>${escapeHtml(label)}</b><small>Design-only configuration · not a native patch</small></div><button class="button ghost" data-remove="${key}">Remove</button></div>`).join("")}</div><div class="action-bar"><p>No files will be written.</p>${button("Simulate review & apply", "apply-demo", "primary", "check")}</div>` : `<div class="empty-state"><div class="empty-icon">${icon("layers")}</div><h2>A fresh canvas.</h2><p>Choose a palette or adjust a setting, then add it to your workspace to see the review flow.</p><button class="button primary" data-go="theme">Explore theme studio ${icon("arrow")}</button></div>`}</section><section class="panel">${panelTitle("activity", "Session activity", "DEMO")}${state.history.map((h) => `<div class="log-entry">${icon("check")}<div><b>${escapeHtml(h.label)}</b><small>${escapeHtml(h.detail)}</small></div></div>`).join("")}</section></div><div style="margin-top:20px">${note("The prototype’s queue resets when you reload. It does not provide a native dry run, patch validation, or executable recovery. Those remain in the existing C++ engines.")}</div>`;
  }
  function appearance() {
    return `${heading("A SPACE THAT FEELS RIGHT", "Set your own atmosphere.", "Small touches that make this workspace feel at home.", "", "dashboard")}<div class="editor-grid"><section class="panel">${panelTitle("sun", "Workspace appearance")}<div class="control-row"><div><h3>Color scheme</h3><p>Switch the entire prototype between light and dark.</p></div><select id="appearance-mode" aria-label="Color scheme"><option value="dark" ${state.appearance.mode === "dark" ? "selected" : ""}>Dark graphite</option><option value="light" ${state.appearance.mode === "light" ? "selected" : ""}>Soft light</option></select></div><div class="control-row"><div><h3>Accent color</h3><p>A little color, exactly where it belongs.</p></div><div class="color-choices">${["#6ce6d0", "#b7a1ff", "#78afff", "#f0a3c8"].map((c) => `<button class="accent-choice ${c === state.appearance.accent ? "selected" : ""}" style="--choice:${c}" data-accent="${c}" aria-label="Accent color ${c}" aria-pressed="${c === state.appearance.accent}"></button>`).join("")}</div></div><div class="control-row"><div><h3>Interface density</h3><p>Give things room to breathe, or keep them compact.</p></div><select id="appearance-density" aria-label="Interface density"><option value="comfortable" ${state.appearance.density === "comfortable" ? "selected" : ""}>Comfortable</option><option value="compact" ${state.appearance.density === "compact" ? "selected" : ""}>Compact</option></select></div><div style="margin-top:24px">${note("Only appearance preferences are saved in this browser. No credentials, file paths, or patch profiles are stored.")}</div></section><section><div class="section-heading"><h2>Built to feel like a desktop app</h2></div><div class="panel"><div class="progress-list"><div class="progress-item"><h3>A clearer hierarchy</h3><p>Tools organized around what you want to do, not how the code works.</p></div><div class="progress-item"><h3>Purposeful color</h3><p>Calm surfaces, readable content, and a focused accent.</p></div><div class="progress-item"><h3>Visible safety</h3><p>Preview, review, and recovery are part of the experience.</p></div></div></div></section></div>`;
  }
  function about() {
    return `${heading("A NEW CHAPTER", "Familiar power. Fresh perspective.", "An interface concept for the next BSI Customizer experience.", "")}<section class="panel about-card"><div class="brand-mark about-mark">b<span>i</span></div><h2>BSI Studio</h2><p>Your player. Your personality.<br>A web-style design concept for BSI Customizer.</p><span class="status-pill">HTML · CSS · JavaScript</span><span class="status-pill neutral">No runtime migration yet</span><div class="spec-list"><div class="spec-row"><span>Project</span><b>BSI Customizer</b></div><div class="spec-row"><span>Developer</span><b>Mr. PC Gamer</b></div><div class="spec-row"><span>Community credit</span><b>Nova</b></div><div class="spec-row"><span>Native patching engines</span><b>Unchanged</b></div></div>${note("This is a separate design prototype. BSI Studio is a proposed interface name, not an approved product rename. Windows WebView2 integration and a production frontend are not implemented here.")}</section>`;
  }
  const pages = {
    dashboard,
    theme,
    resources,
    fps: performancePage,
    boot,
    ui: playerInterface,
    integrity,
    rpc: () => discord(false),
    gif: () => discord(true),
    workspace,
    appearance,
    about,
  };
  function render() {
    const requested = location.hash.slice(1) || "dashboard";
    state.page = Object.hasOwn(pages, requested) ? requested : "dashboard";
    document.getElementById("navigation").innerHTML = nav
      .map((n) =>
        n.group
          ? `<div class="nav-heading">${n.group}</div>`
          : `<a href="#${n.id}" class="nav-item ${state.page === n.id ? "active" : ""}" ${state.page === n.id ? 'aria-current="page"' : ""} title="${n.label}">${icon(n.icon)}<span class="nav-text">${n.label}</span>${n.badge ? `<span class="nav-badge">${n.badge}</span>` : ""}</a>`,
      )
      .join("");
    document.getElementById("breadcrumb").textContent = nav.find(
      (n) => n.id === state.page,
    ).label;
    main.innerHTML = pages[state.page]();
    main.dataset.page = state.page;
    document.title = `${nav.find((n) => n.id === state.page).label} — BSI Studio concept`;
    main.style.animation = "none";
    void main.offsetWidth;
    main.style.animation = "";
  }
  const actions = {
    "switch-player": () => {
      state.player =
        state.player === "BlueStacks 5" ? "MSI App Player" : "BlueStacks 5";
      state.history.unshift({
        label: "Demo player switched",
        detail: `${state.player} · sample only`,
      });
      render();
      notify("Sample player changed. No installation was detected or opened.");
    },
    "scan-resources": () => {
      if (state.scanning) return;
      state.scanning = true;
      render();
      setTimeout(() => {
        state.scanned = true;
        state.scanning = false;
        state.history.unshift({
          label: "Sample resources explored",
          detail: "6 illustrative entries · no file read",
        });
        if (state.page === "resources") render();
        notify("6 sample resources loaded. No executable was scanned.");
      }, 650);
    },
    "queue-theme": () =>
      queue(
        "theme",
        `${palettes[state.palette]?.name || "Custom"} theme palette`,
      ),
    "queue-fps": () => queue("fps", `${state.fps} FPS target`),
    "queue-boot": () =>
      queue("boot", `Boot settings · ${(state.hold / 1000).toFixed(1)}s hold`),
    "queue-ui": () => queue("ui", "Player interface preferences"),
    "queue-rpc": () => queue("rpc", "Discord presence settings"),
    "queue-gif": () => queue("gif", "Animated presence settings"),
    "sample-launch": () =>
      notify("Illustration only. No game or emulator is launched."),
    "presence-example": () =>
      notify("Illustrative Discord button. No external link is opened."),
    recovery: () =>
      requestAction(
        "Preview a recovery?",
        "The real app would restore a verified original backup after checking the selected target. This simulation does not open or modify any file.",
        () => {
          state.history.unshift({
            label: "Recovery flow previewed",
            detail: "Simulation · no file restored",
          });
          notify("Recovery flow simulated. No files were changed.");
        },
      ),
    "apply-demo": () =>
      requestAction(
        "Review your demo changes",
        `You have ${state.pending.size} proposed change${state.pending.size === 1 ? "" : "s"}. This demonstrates the final confirmation step only. No validation, backup, or executable patch will actually run.`,
        () => {
          const count = state.pending.size;
          state.pending.clear();
          state.history.unshift({
            label: `${count} demo change${count === 1 ? "" : "s"} reviewed`,
            detail: "Simulation complete · no files changed",
          });
          render();
          notify("Demo review complete. No real patch was applied.");
        },
      ),
    "reset-page": () => {
      if (state.page === "theme") {
        state.palette = "mint";
        state.colors = { ...palettes.mint };
        state.themeMode = "auto";
      }
      if (state.page === "fps") state.fps = 240;
      if (state.page === "boot") {
        state.hold = 3000;
        state.muted = true;
        if (state.bootUrl) URL.revokeObjectURL(state.bootUrl);
        state.bootUrl = null;
      }
      if (state.page === "ui") {
        state.ui = {
          "Hide promotional cards": true,
          "Simplify the home screen": true,
          "Show player toolbar": true,
          "Custom window title": false,
        };
        state.windowTitle = "";
      }
      if (state.page === "rpc" || state.page === "gif") {
        state.presence = {
          name: "BlueStacks 5",
          details: "Making it my own",
          status: "In the customization studio",
          button: "My community",
        };
        state.appId = "";
      }
      render();
      notify(
        "Preview controls reset. Queued changes are managed separately in the workspace.",
      );
    },
  };
  document.addEventListener("click", (event) => {
    const el = event.target.closest("button,[data-go]");
    if (!el) return;
    if (el.dataset.go) {
      go(el.dataset.go);
      return;
    }
    if (el.dataset.action) {
      actions[el.dataset.action]?.();
      return;
    }
    if (el.dataset.palette) {
      state.palette = el.dataset.palette;
      state.colors = { ...palettes[state.palette] };
      render();
      return;
    }
    if (el.dataset.method) {
      state.themeMode = el.dataset.method;
      render();
      return;
    }
    if (el.dataset.fps) {
      state.fps = Number(el.dataset.fps);
      render();
      return;
    }
    if (el.dataset.accent) {
      state.appearance.accent = el.dataset.accent;
      applyAppearance();
      render();
      return;
    }
    if (el.dataset.remove) {
      state.pending.delete(el.dataset.remove);
      render();
      return;
    }
    if (el.dataset.inspect) {
      const r = resourceSamples.find((r) => r.name === el.dataset.inspect);
      notify(
        `Sample: ${r.name} · ${r.size} · ${r.path}. No real file is opened.`,
      );
      return;
    }
    if (el.dataset.toggle) {
      const current = el.getAttribute("aria-checked") === "true";
      el.setAttribute("aria-checked", String(!current));
      if (el.dataset.toggle === "ui") state.ui[el.dataset.label] = !current;
      else if (el.dataset.toggle === "boot") {
        state.muted = !current;
        const video = document.getElementById("boot-video");
        if (video) video.muted = state.muted;
      }
    }
  });
  document.addEventListener("input", (event) => {
    const el = event.target;
    if (el.dataset.color) {
      state.colors[el.dataset.color] = el.value;
      document.getElementById(`value-${el.dataset.color}`).textContent =
        el.value.toUpperCase();
      const preview = document.getElementById("player-preview");
      if (el.dataset.color === "accent")
        preview.style.setProperty("--player-accent", el.value);
      if (el.dataset.color === "background")
        preview.style.setProperty("--player-bg", el.value);
      if (el.dataset.color === "secondary")
        preview.style.setProperty("--player-secondary", el.value);
      return;
    }
    if (el.dataset.presence) {
      state.presence[el.dataset.presence] = el.value;
      document.getElementById(`presence-${el.dataset.presence}`).textContent =
        el.value;
      return;
    }
    if (el.id === "fps-range") {
      state.fps = Number(el.value);
      document.getElementById("fps-value").textContent = state.fps;
      document
        .querySelectorAll("[data-fps]")
        .forEach((b) =>
          b.classList.toggle("selected", Number(b.dataset.fps) === state.fps),
        );
    }
    if (el.id === "boot-hold") {
      state.hold = Number(el.value);
      document.getElementById("hold-label").textContent =
        `${(state.hold / 1000).toFixed(1)} seconds`;
    }
    if (el.id === "resource-search") {
      state.query = el.value;
      document.getElementById("resource-results").innerHTML = resourceRows();
    }
    if (el.id === "application-id") {
      el.value = el.value.replace(/\D/g, "");
      state.appId = el.value;
    }
    if (el.id === "window-title") state.windowTitle = el.value;
  });
  document.addEventListener("change", (event) => {
    const el = event.target;
    if (el.id === "resource-filter") {
      state.filter = el.value;
      if (state.scanned)
        document.getElementById("resource-results").innerHTML = resourceRows();
    }
    if (el.id === "appearance-mode") {
      state.appearance.mode = el.value;
      applyAppearance();
    }
    if (el.id === "appearance-density") {
      state.appearance.density = el.value;
      applyAppearance();
    }
    if (el.id === "boot-file" && el.files[0]) {
      const file = el.files[0];
      if (!file.type.startsWith("video/")) {
        notify("Please select a video file for the local preview.");
        el.value = "";
        return;
      }
      if (file.size > 250 * 1024 * 1024) {
        notify(
          "Please choose a video smaller than 250 MB for this browser preview.",
        );
        el.value = "";
        return;
      }
      if (state.bootUrl) URL.revokeObjectURL(state.bootUrl);
      state.bootUrl = URL.createObjectURL(file);
      render();
      notify(`Local preview ready: ${file.name}. Nothing was uploaded.`);
    }
  });
  document
    .getElementById("profile-button")
    .addEventListener("click", () => go("about"));
  document
    .getElementById("appearance-shortcut")
    .addEventListener("click", () => go("appearance"));
  document
    .getElementById("activity-shortcut")
    .addEventListener("click", () => go("workspace"));
  window.addEventListener("hashchange", () => {
    render();
    main.focus({ preventScroll: true });
    window.scrollTo({ top: 0, behavior: "instant" });
  });
  window.addEventListener("beforeunload", () => {
    if (state.bootUrl) URL.revokeObjectURL(state.bootUrl);
  });
  applyAppearance();
  fillIcons();
  render();
})();
