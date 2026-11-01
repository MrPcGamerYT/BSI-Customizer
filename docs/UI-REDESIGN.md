# Native studio UI refresh

The HTML concept in `prototype/` has been adapted to the production Qt Quick interface. The desktop application still uses QML and its existing C++ engines; this is **not** a WebView2 migration. The product name remains **BSI Customizer**.

## Applied

- Graphite surfaces, restrained borders, mint default accent, and coordinated light-mode colors.
- A quieter title bar with current-page breadcrumbs, grouped sidebar navigation, and the existing window controls.
- A scrollable overview with a hero, toolkit shortcuts, real installation/resource/workspace counts, installation selection, and editable target paths.
- Shared flat cards, buttons, navigation items, toggles, and notifications across the existing editors.
- Restyled sign-in surfaces without changing Discord authorization, saved-session resume, or manual callback flows.
- A collapsed-by-default activity log that automatically expands on errors.
- Keyboard focus/activation for shared action buttons and sidebar entries.

The existing twelve page indices and backend connections remain intact. No sample installations, fabricated verification results, or prototype simulation actions were copied into production. Resource scanning remains explicitly initiated by the Scan Resources button. Backup/recovery, busy-state modal protection, the authorization gate, updater, and cache-purge confirmation remain in place.

## Preferences

The default accent for a fresh settings profile is mint (`#6ce6d0`). Existing saved accents are preserved. Choose mint in **Appearance** if you want the prototype's exact accent on an existing installation. Light mode derives a darker UI accent for legibility without overwriting the saved preference.

## Verification

The QML metadata check now lints **all production QML files**, including all screens and shared components:

```sh
python tests/workspace_qml_lint_test.py
```

It requires the PySide6 6.8 toolchain and passes with Qt 6.8.3. Existing C++ self-tests were extended to check the twelve-page stack, sidebar, and dashboard-to-Resources navigation. The native UI test still checks live dark/light theme binding updates.

A Windows build and full native self-test run are still required before distributing an EXE. Local GUI execution in the sandbox is blocked by the missing system `libGL.so.1`; metadata lint is not runtime or rendering verification. The browser prototype remains a separate visual reference and does not display the running desktop application.
