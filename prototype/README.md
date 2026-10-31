# BSI Studio — interface prototype

An independent, interactive HTML/CSS/JavaScript concept for a full BSI Customizer redesign. **BSI Studio is a proposed interface name, not a product rename.** No native application files, CMake targets, QML screens, or patching engines are changed by this prototype.

## Run

From the repository root:

```sh
python -m http.server 8080 --bind 0.0.0.0 --directory prototype
```

Open the server URL in a browser. There is no npm install, build step, CDN, remote font, analytics, or API dependency. Opening `index.html` directly also works; appearance storage may be unavailable under browser file-URL restrictions.

## Try

- Navigate all twelve screens using the sidebar.
- Theme studio: change palettes or colors and see the canvas update.
- Resource explorer: explicitly click **Scan resources** to load six sample entries, then search/filter them. Opening the page never starts a scan.
- Performance: use presets or the FPS slider.
- Boot experience: preview a local video, adjust hold time, and mute audio. Videos use an in-memory browser object URL; no upload occurs. Maximum preview size: 250 MB.
- Player interface: toggle proposed controls.
- Discord/animated presence: edit the illustrative card. No Discord connection or remote image request is made.
- Patch workspace: queue settings, remove them, and walk through a clearly labeled simulated confirmation.
- Appearance: switch dark/light, accent color, and comfortable/compact density. Only these appearance choices are saved in browser local storage.

## Boundaries

All installation information, file paths, resources, verification reports, backup indicators, and activity entries are illustrative. No executable is selected, read, scanned, patched, or restored. Safety checks on the Integrity screen are intentionally not disableable. The queue and session state are transient; this is not a functional patch profile or native dry run.

There is no WebView2 integration yet. A production migration would require a secured, explicit native bridge, trusted local content, runtime packaging/install handling, and real integration tests for all existing functionality and safeguards. This prototype is meant to approve the visual direction first.
