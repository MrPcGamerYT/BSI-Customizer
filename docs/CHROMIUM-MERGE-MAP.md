# BSI Customizer 3.0.1 — existing customizer → Chromium UI merge map

This is an adapter integration, not a rewrite of the C++ binary patchers. The
existing engines, native login and settings persistence remain the source of
truth. These are source mappings; Windows native/real-file verification is still
required before a production release.

| Web tool | Existing native calls used |
|---|---|
| Overview | `detectInstalls`, `useInstall`, `detectInstances`, `SettingsManager::openExeFolder` |
| Theme studio | `loadTheme`, `themeEntries`, `hexColors`, `setThemeEdit`, `setHexColor`, `setHexEnabled`, `applyPreset`, `applyHexPreset`, `patchThemeQml`, `applyHex`, `previewHex`, `clearThemeEdits`, `resetHexColors`, `recolorTogglesFromPreset` |
| Surface gradients | `loadGradientTargets`, `gradientTargets`, `applyThemeGradient`, `applyHexGradient` |
| Resources | `scanResources`, `resourceInfos`, `resourceTextAt`, `resourcePreviewAt`, `resourcePreviewNote`, `resourceBudgetAt`, `resourceTextSize`, `resourcePayloadSize`, `saveResourceText`, `replaceResourceWithFile`, `extractResource`, `extractAllResources` |
| Performance | `fpsDetect`, `fpsAnalyze`, `fpsPatch`, `fpsRestore`, `SettingsManager::setFpsAutoDetect` |
| Integrity | `integrityCheck`, `integrityPatch`, `integrityRestore` |
| Boot | `bootPatch`, `purgePlayerCache`, `workspace("profile-section", boot)` |
| Player Interface | `removalNames`, `applyRemovals`, `layoutTweaks`, `applyLayoutTweak`, `sidebarButtonRows`, `applySidebarOrder`, `aboutPageState`, `aboutPageReport`, `applyAboutPage`, `workspace("profile-section", about)` |
| Discord ID / names | `rpcidFind`, `rpcidStatus`, `rpcidReplace`, `rpcidUndo`, `rpcidNames`, `rpcidSetName` |
| Animated presence | `gifRpcInspect`, `gifRpcApply`, `gifRpcRestore`, `workspace("profile-section", gifRpc)`, `workspace("rpc")` |
| Workspace / profiles | `customizationProfile`, `workspaceOperations`, `workspace` actions: inspect, diagnostics, profile-load/save, queue-theme/resource, clear, theme-dry-run, dry-run, apply, history, restore, image, video, rpc |
| Appearance / targets | Existing `Backend` path setters, `ThemeManager` Dark/Light/System and accent setters, `restoreBackup` |
| About / updates | Existing `UpdateChecker` state, auto-check, check, download, apply, cancel, skip/unskip, open-download, staged-file and helper-log actions |
| Debloat / ADB | Original `debloatState/Options/Apply/Restore`, `androidState/Scan/Apply/Restore` |
| Engine images | Original `rootvhdFind/OpenEngineArchive/Info/EnableWrite/List/Search/Extract/Add/AddFolder/Replace/Mkdir/Rename/Move/Copy/Remove/Undo/DiscardUndo/EngineFolders` |
| Video preview | Original Qt Multimedia `MediaPlayer` / `VideoOutput`, HTML play/pause/crop and bounded native viewport geometry |
| Login | Original `LoginGate.qml`, `AuthManager` and `SessionStore` — not reimplemented in the renderer |

## Behaviors deliberately preserved

- Opening Resources or Workspace does not scan. Cached workspace/profile reads
  do not inspect the EXE. Resources scan is initiated by **Scan Resources** only.
- Search uses equivalent logic to `src/qml/ResourceSearch.js`; tests compare
  indices across queries/kinds. Filtering does not read payloads or change the
  selected resource. Page indices retain their original backend index.
- A visible About link maps to `hidden: false`, not the reverse.
- Profile import stages settings only. Applying boot/GIF files remains separate;
  no cross-file atomicity is promised. Raw replacement bytes stay native rather
  than being serialized into browser snapshots/batch summaries.
- Batch Apply needs a successful current Dry Run. The UI forwards its
  `outputSha256`; native fingerprint/capacity/conflict checks and journal recovery
  are not replaced by frontend checks. Failed Dry Runs revoke the Apply control.
- Operation errors/refusals are propagated. Plugin inspection/config saving does
  not claim an observed Discord connection or GIF animation.
- Qt file dialogs handle import/export/source paths; no unrestricted local-file
  origin is exposed to Chromium. HTML preview controls use the original Qt
  Multimedia decoder, with only video pixels composited into the page.

## Verification boundaries

Local tests use a clearly isolated test transport for HTML interactions. The
55-command native locked-access regression is compiled/run by the separate
Windows Chromium workflow, not certified by browser fixtures. Authenticated
native end-to-end tests and real target execution remain outstanding.
