
### Interaction polish

The native control center keeps its bento/inspector layout and near-black palette.
Shared buttons, navigation groups, pack cards, color fields and switches use short
accent-tinted hover/focus transitions; menus, settings, the design drawer and lazy
pages reveal without blocking navigation. The thin header strip tracks real backend
work/page loading, not a simulated delay. Boot content/behavior is unchanged.

Appearance (also the app menu) exposes **Animated background** and **Reduce motion**.
Both are saved with the other appearance preferences. Reduced motion overrides
ambient animation and interface transition durations, while keeping busy status
visible as a static line. Background movement uses two cached radial textures and
12 small scene-graph points, without per-frame Canvas repaint, blur or resource
scans; it stops while the window is inactive or minimized. These ambient customizer effects are separate from player component deployment.


### Player component deployment and limits

New catalog drafts enable `playerUi` (a typed, optional boolean); older saved
profiles without the field retain the palette/token/manual-icon behavior. In
player GUI mode, **Check player GUI coverage** reads only manually scanned,
registered resources from the selected EXE. Staging compiles matched source
contracts for buttons, icon buttons, text fields, combos, sliders, switches,
dialogs, Settings, shadows, tooltips, toast, sidebar and topbar, plus known icon
families and their hover/click/disabled variants. Controls using those shared
types inherit the changes without replacing their callbacks/business logic.

Player components receive actual corner radii, color/knob transitions and popup
fades. The other preview modes (rise/slide/scale) are not deployed motion styles.
The compiler uses fixed generated QML and typed colors/numbers, never profile
scripts. Switching packs updates one settings block instead of stacking effects.

**This is not universal full-emulator support.** A full-GUI request is refused if
required components/icon families are missing or ambiguous, or a compiled shadow
cannot be identified precisely. Missing shared sources may live in companion
DLLs/RCCs or be compiled-only; those files are not patched by this single-EXE
transaction. Native/Android surfaces, outer OS-window clipping, raster artwork,
boot and unrelated optimizer UI remain unchanged. No palette-only operation is
silently substituted for the requested GUI pack.

Dry Run verifies mapped storage, byte-for-byte read-back and exact-source cache
invalidation before Apply; file size cannot grow. Up to 128 bounded operations
share one EXE recovery snapshot. Resource scans stay explicit. Coverage is not a
claim of a successful live emulator launch.

### Native form consistency

Text fields, JSON areas, dropdowns, numeric inputs, checkboxes and tabs use shared
rounded dark controls with accent focus/hover states. The lower duplicate Home
and Settings sidebar shortcuts are removed; group headings only expand/collapse.
The original feature editors and the bottom activity panel remain available.
