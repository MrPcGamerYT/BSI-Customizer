# Settings-tab defaults

These eleven standalone SVGs are the original, unchanged files from the user's
[`tab-icons.zip`](https://github.com/dsajgddghdsaakovvgheww/BSI-Customizer/blob/main/tab-icons.zip).

Archive SHA-256:
`e2dba9b369d058200b7f73c877bfd5c62e4eaf61def34899c35f8760474cf4c7`

`default-icons.json` maps native QML page IDs (including known older names), not
translated labels or tab positions. In the twelve-tab native model:

- **Devices** is `PlrAudioSetting.qml`; **Phone** is `PlrDeviceSetting.qml`.
- **Game settings** and **Gamepad** share the supplied controller icon. The pack
  contains no separate Gamepad drawing.
- Unrecognized future pages are left without a guessed replacement.

CMake embeds the manifest and SVGs in both UI builds. The patcher combines these
defaults with saved per-tab overrides on each Apply; it does not write defaults
to the user's profile. Removing an override selects the pack icon again.

The supplied drawings use `currentColor`. Standalone images cannot inherit a
text foreground, so the loader gives uncolored SVG roots `color="#e7e9f2"` while
preserving explicitly chosen root colors/styles. Source files in this folder
are never rewritten. Editable profile assets remain SVG; the player receives
local PNG runtime copies to avoid optional SVG-plugin/data-URI incompatibilities
and to keep all twelve tabs within the fixed About resource slot.

The selected tab tints the runtime icon silhouette with the player's own
selected-tab color, matching its label and soft background. Unselected icons
retain their supplied/custom colors; selection never rewrites the source SVG
or its immutable PNG copy. The public Qt Quick Controls `ToolButton.icon`
renderer supports this in both Qt 5 and Qt 6 without an extra effects import.
