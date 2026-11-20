# Extra phones in the **stock** Phone dropdown

This feature extends the existing `Settings → Phone → Choose a predefined
profile` ComboBox. It does not add a second selector or replace/recolor the
page. The original warning, layout, fonts, controls, custom fields, network
provider, callbacks and native Save/Discard flow remain in place.

## Use

1. Close **every** HD-Player instance. Select the player EXE you actually launch.
2. In Customizer's **Player interface / UI** options, enable **Extra phones in
   the stock Phone dropdown** and Apply.
3. Restart the player, open **Settings → Phone**, and open its existing dropdown.
   The eight entries below appear first; the existing server-provided entries
   remain underneath in their original relative order.
4. Choose a phone and use the player's **Save changes**. Selecting a row only
   stages a native draft. Native navigation/close prompts and Discard still
   decide whether to save that draft.

**Installing the choices does not select a phone, change FPS/root/renderer
settings, modify game files or clear Android data.** However, **saving a
*different* device identity is the player's native profile-switch operation**.
Its warning, “Changing this will delete Android framework data,” is preserved.
Back up important emulator data before switching; this is not a promise that
switching is harmless.

### Entries, in dropdown order

| Display name | Manufacturer | Brand | Model sent to the native custom-profile API |
|---|---|---|---|
| ASUS ROG Phone 9 | `asus` | `asus` | `ASUSAI2501A` |
| ASUS ROG Phone 8 | `asus` | `asus` | `ASUS_AI2401_A` |
| REDMAGIC 10 Pro | `nubia` | `nubia` | `NX789J` |
| REDMAGIC 9 Pro | `nubia` | `nubia` | `NX769J` |
| OnePlus 13 | `OnePlus` | `OnePlus` | `CPH2649` |
| OnePlus 12 | `OnePlus` | `OnePlus` | `CPH2573` |
| Samsung Galaxy S25 Ultra | `samsung` | `samsung` | `SM-S938B` |
| Samsung Galaxy S24 Ultra | `samsung` | `samsung` | `SM-S928B` |

These are regional **model-identity presets**, not full hardware/fingerprint
emulation or an OEM/game certification. ASUS's A variant is shared across some
ROG 9/9 Pro catalog records; a marketing label does not uniquely distinguish
those devices. No screen-refresh specification is used as evidence of an
in-game FPS unlock.

If the cloud already supplies an entry with the **exact same display name**,
that real cloud code takes precedence and the entry appears only once, at the
top. Otherwise the choice stages `device_profile_code=custom` and the three
identity fields above. Save calls the **existing**
`setCustomDeviceProfile(manufacturer, brand, model, carrier)` API—not an invented
cloud `pcode`. It preserves the selected carrier and requires a nonempty carrier
before saving a local preset. Callbacks, not an invented success status, control
commit/failure. A matching saved local identity reopens as its named choice;
unknown custom identities remain manual custom profiles.

## Local helper and recovery dependency—keep these files

The fixed-size Phone QML slot could not accommodate the full inline extension
on the tested players without growing the EXE. EXE growth is forbidden. The
installed source therefore imports an **independently authored local JavaScript
helper**, not a network script, DLL proxy or game plugin.

The feature stores files below Qt's **AppLocalDataLocation** for Customizer:

```text
<Customizer local app data>/phone-profiles/<SHA256 of canonical EXE path>/
    PhoneProfiles-<SHA256 of generated script>.js
    recovery.json
```

On Windows this is in the current user's local application-data area; use the
actual Qt application name/organization paths, not a hard-coded username.

- The EXE contains an **absolute local file URL**. Keep this folder while the
  feature is installed. It is **not a self-contained portable patched EXE**.
  Undo before moving the player, changing Windows accounts or clearing
  Customizer app data. A missing/unreadable helper can prevent the Phone page
  from loading. Reapplying from the same Customizer build can recreate a
  *missing* helper if the valid original-source snapshot is still available.
- Existing checksum-named helpers are never silently overwritten/adopted when
  their bytes differ. The installer validates them when applying; the filename
  is **not** runtime signature enforcement. Treat this local executable-script
  directory as trusted application data and do not edit its contents.
- `recovery.json` contains the exact original UTF-8 Phone source (including a
  possible BOM), target identity and source/helper hashes. It is needed to undo
  the compact installation. Keep it with the helper. A newer/different helper
  version or path is not silently substituted into an existing installation.
- The original native source is recovered only when recompiling that snapshot
  with the expected edits/helper URL exactly matches the installed source.
  Missing/damaged snapshots or later Phone-source edits refuse instead of
  guessing or overwriting them. Other resource edits are preserved.

### Undo

Close the player, uncheck the same option and Apply **from the same Windows
account/Customizer build and original player path**. Removal restores the exact
decoded native Phone source. It works even when the script itself is missing
or edited, provided the verified source snapshot remains intact; removal does
not execute the script. The existing `.bak` is preserved. Content-addressed
helper files are deliberately retained because a separate patched EXE copy may
still reference them.

**Undo removes choices, not the already saved identity.** To change that
identity, choose a stock profile inside the player and use native Save changes
with its data warning. Do not blindly replace an EXE with an old `.bak`: that
can also undo unrelated changes. If local recovery was lost, use a known clean
matching player installation/backup and reapply desired customizations.

## What is and is not verified

### Implemented/tested contracts

- Real Qt Controls hosts on **Qt 5.15.2, 5.15.19 and 6.8.3**: eight entries first,
  unchanged authored stock-control geometry/font metrics, physical dropdown
  selection and Save, native API argument triplets, draft-only selection,
  asynchronous success/failure, repeated/clean Save, an intervening selection
  while a response is pending, Discard and genuine destroy/reopen.
- Existing cloud-code priority and duplicate removal, manual/unknown identities,
  carrier-only changes, missing-carrier refusal, and unchanged FPS/root/renderer
  configuration values. The backend services in these UI tests are explicitly
  **authored test doubles**, not a running emulator backend.
- Actual binary-QRC Phone source importing the local JS through an absolute,
  percent-encoded path (including spaces/non-ASCII) on those three Qt hosts.
- Production resource planner and disk installer: locks, new backup/read-back,
  helper/snapshot staging, missing/modified recovery data, altered helpers,
  backup I/O refusal and safe retry, idempotence and exact decoded-source undo.
  POSIX linked-file refusals are tested locally. Windows process enumeration is
  fail-closed in production code but needs Windows execution qualification.
- All four hash-pinned [player reference images](../tests/fixtures/emulator-qt-evidence.json):
  MSI latest, BlueStacks 5 latest, MSI 5.9 and BlueStacks 5.10. In-memory cycles
  use a realistic content-addressed **Windows** helper URL. Production installer
  cycles use their actual local-helper paths on **disposable EXE copies**.
  Tests verify no EXE growth, unchanged imports/unrelated decoded resources,
  exact decoded native-source restoration and unchanged original inputs. They
  never launch a reference EXE. Cache invalidation means this is **not** a claim
  of whole-EXE byte-for-byte restoration.

### Still requires an installed-player/game test

No guest Android property read-back, installed player callback implementation,
game allowlist, server-controlled graphics menu, anti-cheat compatibility or
measured 90/120/144-FPS result has been verified here. Some games gate high-FPS
options by reported identity, but model strings alone may be insufficient; game
version/region, Android version, emulator detection, graphics API and game/server
policy can also matter. **None of these entries guarantees a high-FPS unlock or
actual FPS gain.** Use only configurations permitted by the relevant game.

For an authorized manual check: note player/game version and region, back up
emulator data, select/Save a profile, restart as the player requests, confirm the
saved selection survives reopening, then check the game's own FPS-options menu.
Compare against the same game's stock-profile baseline. Keep menu availability
separate from measured frame rate. Do not claim an unlock from the dropdown
label or the phone's display refresh rate alone.

## Identifier provenance

Model names/identifiers were cross-checked against the community
[certified-android-devices snapshot `1492d025…`](https://github.com/androidtrackers/certified-android-devices/blob/1492d025b4aaf91f805320a0d180949976c88015/by_brand.json),
selected with an as-of-2026-11-17 cutoff. This is a community catalog, **not** a
game high-FPS allowlist or an OEM attestation of every manufacturer/brand/model
triplet.

Additional property evidence, read as data only:

- [ASUS AI2501 firmware dump, commit `62f32bc5…`](https://github.com/Jiovanni-dump/asus_asusai2501_dump/tree/62f32bc581bb3ada8926e7be17dd553fc3ec8cd7):
  `system/build.prop`, `product/etc/build.prop` and `vendor/build.prop` identify
  manufacturer/brand `asus`; their base model is `ASUSAI2501`, not evidence that
  the A regional suffix uniquely identifies the non-Pro phone.
- [AndroidSystemPropertyCollect, commit `e11eb4e9…`](https://github.com/getActivity/AndroidSystemPropertyCollect/tree/e11eb4e91174ff99d629addd73243eb28595f74c):
  the REDMAGIC 9 Pro and 10S Pro reports show `nubia/nubia/NX769J` and
  `nubia/nubia/NX789J`; the OnePlus 12 report shows `OnePlus/OnePlus/PJD110`.
  The latter supports brand/manufacturer spelling, **not** direct read-back of
  the selected CPH2573 regional variant. Shared family/model reports do not
  prove a game treats every marketing variant alike.

The remaining regional triplets have not all been independently read back from
physical OEM phones. No external script/module from a purported FPS-unlocker
project is executed or redistributed.

## Reproduce

With a Qt Core development SDK and CMake:

```sh
cmake -S tests/shaders -B build-phone -DBSI_TEST_NATIVE_SETTINGS=ON -DBSI_TEST_VULKAN=OFF
cmake --build build-phone --target phone-profiles-test --parallel 2
ctest --test-dir build-phone -R phone-profiles-native-contract --output-on-failure
BSI_PHONE_PROFILES_TEST="$PWD/build-phone/phone-profiles-test" \
  python tests/run_qt_runtime_test.py tests/phone_profiles_gui_test.py
```

The GUI test needs the independent PySide6 or PyQt5 runtime and `rcc` (or set
`BSI_PHONE_RCC`). Set `BSI_TEST_QT_API=PyQt5` to select Qt 5; use separate Python
processes/virtual environments for different Qt runtimes. On Windows use the
actual `Release/phone-profiles-test.exe` output path.

For the pinned sample checkout (ZIP inputs are supported):

```sh
BSI_PHONE_PROFILES_TEST="$PWD/build-phone/phone-profiles-test" \
BSI_PHONE_REFERENCE_DIR="/path/to/pinned/HD-Player-checkout" \
  python tests/phone_profiles_references_test.py
```

Tests isolate local application data with Qt test mode and a unique application
name. Originals stay read-only; disk changes occur on temporary copies only.
The normal application also compiles `PhoneProfiles`/`PhoneProfilesInstall`
through the root Visual Studio/CMake project; no Python is needed at runtime.
