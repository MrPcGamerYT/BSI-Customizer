# Experimental native Shaders — user verification

This is a **test build, not a verified universal shader release**. It adds a real
**Shaders** page to the player's own Settings using a normal **BSIShaders.dll**
Qt module. It is not an overlay, `version.dll`/system proxy, launcher, or FPS /
optimizer tweak. Recipients do not need Python, Node or a running Customizer.
The player still needs its own compatible emulator installation and the installed
`BSIShaders` directory; the modified EXE is not standalone.

**No merge yet.** Automated resource/package/recovery/UI and software pixel tests
do not establish that your player's game presentation works on your GPU/driver.

## Before you start

- Use a disposable/test installation and a non-sensitive offline game first.
  Graphics hooks may be incompatible with anti-cheat/protected content or a
  particular driver, and can crash the player. Do not disable protections.
- Keep a separate **full installation backup** (including your current EXE and
  any existing shader directory). Patching invalidates the EXE's signature.
- Close **every** player instance before inspecting for installation, applying,
  removing or recovering. Keep them closed until the operation finishes.
- The initial bundle contains **Windows x64 release MSVC Qt 5.15.2 and 6.8.3**
  modules. Inspection checks the player's actual Qt Core/Gui/Qml fixed versions,
  imported symbols and dependency closure. **Another Qt version is refused**;
  do not rename a module, replace Qt/VC/system DLLs or force a mismatched install.
  Send the inspection report if your version is different.

## Install — where the controls actually are

1. Open the experimental **Single EXE Release** test artifact. Choose the
   `…-win64-…` artifact for the portable EXE (the separate `…-setup-…` artifact is
   an optional installer). GitHub wraps artifact downloads in a ZIP; the ZIP is
   only a download container, not another feature. Verify the build commit and
   SHA-256 recorded in that run's summary. Do not use an older successful run.
2. Sign in normally, select the correct `HD-Player.exe`, and go to
   **PATCHER → Player interface → Settings panel**. Apply the native top-tabs
   Settings redesign if it is not already applied. This is a required, separately
   reviewed step; shader installation does not guess/replace an unknown shell.
3. In the same Player interface page, open **Shaders → 1. Inspect compatibility**.
   Nothing is installed on page entry or by inspection. Read the report and
   hashes. Unsupported versions, insufficient fixed resource space, edited
   sources, unowned files, running players or pending recovery block installation.
4. Choose **2. Review installation**, read the risk dialog, explicitly acknowledge
   it, and choose **Install native shaders**. Updates/reapplies use the same
   reviewed flow. A DLL-only update invalidates old install/remove reviews.
5. Launch that exact player EXE normally. Use the retained sidebar **Settings**
   button. **Drag/scroll the horizontal top-tab strip left to its right-hand end**
   to find **Shaders** after the existing categories. Its icon can be customized
   or hidden through the Settings-panel editor like the other tab icons.

The owned module directory contains exactly four files:

```
BSIShaders/BSIShaders.dll
BSIShaders/qmldir
BSIShaders/bsi-shaders-owner.json
BSIShaders/THIRD-PARTY-NOTICES.txt
```

The last file accompanies binary redistribution. Do not add other files to this
owned directory; conflicts are preserved, not silently deleted. No Qt/VC/system
runtime replacement is installed. The optional import is caught: if the module
is missing or fails to load, native startup should continue with the shader row
inactive. This fallback still requires testing in the real player.

## Verify effects, not just saved sliders

1. Run a game and choose a still/repeatable scene. Record the player version,
   GPU actually rendering the game, driver, Windows version and configured
   **game Graphics renderer**. The Qt/interface renderer is not the game API.
2. In native **Settings → Shaders**, choose **Vivid** or **Clear**, enable visual
   effects and **Save changes**. Unsaved edits must not alter the game.
3. Compare the actual game image before/after. If needed, temporarily use a
   stronger saturation/contrast value (for example +60), Save, and compare the
   same scene. Record the **Observed game API**, status and **Processed game
   frames** counter. Successful submissions should increase the counter; the
   counter alone is still not visual proof. Close Settings while comparing if
   the game pauses behind it, then reopen to check the count.
4. Disable effects and Save. The original game image must return and processing
   must bypass. **Reset all** restores the neutral disabled defaults. Zero values
   also bypass processing. These are visual effects, not a promise of more FPS,
   HDR display output or an exact copy of the uploaded shader algorithms.
5. Repeat for **OpenGL, DirectX and Vulkan**, selecting the game's Graphics
   renderer and fully restarting the player after each change. A loaded graphics
   DLL or an ANGLE translation layer does not prove another game API was tested.
   Unrecognized/unsupported/protected surfaces should remain untouched with a
   bypass status, not display a false success.

Also check Save/reopen/restart persistence, individual reset, presets, Discard,
No/Discard prompts on tab switching and close, per-instance separation, native
callbacks/Save/footer, resize/fullscreen, changing monitors and hybrid GPUs.
Test custom/default/hidden icons and Settings Update without losing Shaders.

### Results to send back

A screenshot/video of the actual same game scene with effects off/on, plus the
native shader status/counter and inspection report, is much more useful than a
screenshot of sliders alone. Do not include login/session tokens.

| GPU + driver | Player build | Configured game renderer | Observed game API | Visible off/on change? | Counter increases? | Resize/restart result |
| --- | --- | --- | --- | --- | --- | --- |
| Your hardware | | OpenGL | | | | |
| Your hardware | | DirectX (record 11/12 when observed) | | | | |
| Your hardware | | Vulkan | | | | |

Intel, AMD, NVIDIA and other GPUs use the same capability checks; **none of those
physical hardware combinations is certified by the build's automated fixtures**.
Report a bypass/failure as such; do not force it or call saved values a pass.

## Remove and recover

- **Normal removal:** close all player instances, return to Customizer
  **PATCHER → Player interface → Shaders**, inspect again, then choose
  **Remove shaders only**, acknowledge and confirm. This removes the owned
  native source fragments and module, preserving the Settings redesign,
  unrelated later customizations and saved (now inactive) shader preferences.
  Remove shaders before using **Restore original Settings panel**.
- **Interrupted operation:** **do not launch the player**. Keep
  `<EXE>.bsi-shaders-transaction.json` and its sibling
  `<EXE>.bsi-shaders-<UUID>.recovery` directory. Close all instances, inspect, then
  choose **Recover transaction**. A prepared transaction restores the verified
  previous EXE/module pair; a committed one only verifies/finishes cleanup.
  A changed EXE/module/snapshot/journal is a conflict, not permission to overwrite.
- If recovery is refused, save the report and retain all recovery files. Do not
  mix an EXE from one snapshot with a DLL directory from another, delete an active
  journal, run another patcher, or use a stale whole-EXE backup over later edits.
  Ask for help or restore your independently kept **matching full installation**
  backup with every instance closed.

Two filesystem objects cannot be swapped simultaneously. The implementation
journals verified snapshots and uses ordered writes/rollback; its disposable-file
interruption tests are **not power-loss or filesystem-durability certification**.

### Optional read-only diagnostics

No script/interpreter is needed. The delivered application itself supports:

```powershell
.\BSI-Customizer-<version>-win64.exe --shader-bundle-check
.\BSI-Customizer-<version>-win64.exe --shaders-inspect "C:\YourPlayer\HD-Player.exe"
```

These inspect embedded payload/target bytes without loading a player/plugin DLL
or modifying the player. Installation/removal/recovery remain authorized,
explicitly acknowledged UI operations. A missing native tab despite a successful
installation is a **failed verification**, not something to work around with a
system-proxy DLL or by replacing the player's runtime.
