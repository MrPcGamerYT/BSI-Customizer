# BlueStacks 5 / MSI App Player 5 — Qt compatibility research

Research date: **2026-11-17**. Scope: native Windows x64 HD-Player, native
Settings → Shaders, and the Qt boundary. This is **not** an exhaustive historical
release list, a new downloadable test-build announcement, or game/GPU certification.

## 1. What is actually evidenced

The four existing references were re-read as **data only**, from the pinned
[HD-Player repository](https://github.com/MrPcGamerYT/HD-Player/tree/6e4dc894eb1b6956060b3f76d13fc438e5178d03).
No player or supplied DLL was executed. The repository contains EXEs/EXE-only
ZIPs, not their matching Qt runtime installations. “Latest” below is an artifact
filename, **not** a claim about today's newest public release.

| Product/sample | Player fixed FileVersion | Qt family from real imports | Additional compiler evidence | Exact installed Qt runtime |
| --- | --- | --- | --- | --- |
| MSI `HD-Player MSI v5.9.exe` | 5.9.300.6315 | Qt 5 | 99 bounded QML-cache headers identify **5.15.4** | Unknown: matching runtime DLLs absent |
| BlueStacks `HD-Player v.5.10 bluestacks.exe` | 5.10.150.1016 | Qt 5 | 105 bounded QML-cache headers identify **5.15.4** | Unknown: matching runtime DLLs absent |
| MSI `HD-Player MSI Latest version.zip` | 5.22.75.6322 | Qt 6 | Imports **`qt_version_tag_6_7`** from Qt6Core | Unknown: minor tag is not a patch version |
| BlueStacks `HD-Player latest version Bluestacks 5.zip` | 5.22.285.1013 | Qt 6 | Imports **`qt_version_tag_6_7`** from Qt6Core | Unknown: minor tag is not a patch version |
| User's installation, reported by the shader inspection screenshot | Not independently supplied with matching runtime files | Qt 5, x64 | Inspection reports **5.15.4** | User-reported 5.15.4; no local runtime execution/hash certification |

All four EXEs are AMD64 (`0x8664`). An Android instance being Nougat/Pie,
32-bit/64-bit, Hyper-V/non-Hyper-V does **not** determine the Windows host's Qt
ABI or the game's presentation API. Inspect the selected EXE and its own DLLs.

### Why these evidence categories are separate

- **Player FileVersion** identifies the emulator build, not the Qt release.
- **Qt5Core/Qt6Core imports** identify the actual imported major family, unlike
  searching arbitrary DLL-name strings.
- **QML-cache `qtVersion`** identifies the compiler that produced that unit.
  The [official Qt 5.15.4 `Unit` prefix](https://github.com/qt/qtdeclarative/blob/04ea6df18a2b06efd133a4f1b13c2e38817279ae/src/qml/common/qv4compileddata_p.h#L939)
  defines this field. A bounded header count does not establish active resource
  ownership or the version of an adjacent/replaced runtime. Never use it as an
  installation override.
- **`qt_version_tag_6_7`** is minor-level build evidence, not proof of 6.7.0,
  6.7.2, 6.7.3, or the runtime currently installed beside that EXE.
- **DLL `VS_FIXEDFILEINFO`** is used for the exact installed runtime. Core, Gui,
  Qml and every Qt provider in the shader dependency closure must agree on the
  exact release version. Editable display strings are not a substitute.

Machine-readable fingerprints and the intentionally unknown runtime fields are
in [`tests/fixtures/emulator-qt-evidence.json`](../tests/fixtures/emulator-qt-evidence.json).
No proprietary binary or extracted native QML was added to Git.

### What web research does and does not establish

[MSI's official App Player page](https://www.msi.com/Landing/appplayer) confirms
its BlueStacks partnership, **not** a per-release Qt version/ABI table. The
[official BlueStacks requirements](https://support.bluestacks.com/hc/en-us/articles/360056129211-System-requirements-for-BlueStacks-5)
distinguish older OS-compatible releases (5.21.300 and below for Windows 7/8)
from newer releases. That is **not** an authoritative mapping of every release
to Qt. Do not infer an exact Qt transition point from an OS cutoff, a forum post,
a DLL-download site, another app's crash log, or a brand/version label.

An exhaustive vendor-backed list of **all** past/current OEM Qt builds was not
found. Therefore this change uses per-installation evidence and explicit ABI
contracts rather than claiming a guessed version list is universal support.

## 2. Important correction to the earlier exact-patch diagnosis

The previous **V2 shader package contract deliberately required exact Qt patch
versions**. Its rejection of the user's 5.15.4 runtime by the 5.15.2 bundle was a
real installer refusal. But **a different patch version alone does not prove
that a correctly built public-API plugin is ABI-incompatible**.

Qt documents backwards binary compatibility within a major family, subject to
compatible toolchain, platform and build configuration. Older public-API
plugins can run on newer compatible runtimes; the reverse and Qt 5 ↔ Qt 6 are
not universal promises:

- [Qt 5.15 plugin deployment rules](https://doc.qt.io/archives/qt-5.15/deployment-plugins.html)
- [Qt 6 plugin deployment rules](https://doc.qt.io/qt-6/deployment-plugins.html)
- [Qt binary compatibility conditions](https://doc.qt.io/qt-6/qt-releases.html#binary-compatibility)
- [Qt 5.15.4's actual loader check](https://github.com/qt/qtbase/blob/ffdd372c7bbda62e9d937f406319f38e3e982774/src/corelib/plugin/qlibrary.cpp)

The original bundle's **6.8.3** build can also be **too new for a Qt 6.7 player**.
A Qt 6 DLL is not automatically compatible with every Qt 6 installation.
Building solely with the Customizer's newest SDK was the wrong coverage policy.

## 3. Chosen design: small public Qt bridge, Qt-independent renderer

With permission to choose the best implementation, the shader path retains a
normal, optionally imported QML DLL for the **small native Settings bridge**:

1. **Rendering/hooks stay independent of Qt.** No QObject/Qt container is passed
   across the render-thread boundary. GL/DXGI/Vulkan surface, device and renderer
   gates remain unchanged.
2. **Separate genuine build baselines:** Qt **5.15.2** and Qt **6.2.4**, x64,
   MSVC release, default namespace/double `qreal`, using public Core/Gui/Qml APIs.
   Both versions stay honestly embedded in the DLL descriptors.
3. **New V3 `API=public-v1` contract:** an audited module can target the same
   major's equal/newer runtime, after **all** normal/delay imports, exports,
   forwarders, release flags and dependency-closure checks pass. This is
   preflight eligibility, not physical runtime/GPU certification.
4. **Existing V2 stays exact-version only.** Nothing silently broadens an old
   DLL's contract or changes a previously installed DLL's metadata.
5. **Same exact version within the player's Qt set.** “Newer than the bridge”
   does not permit mixed Core/Gui/Qml/provider patch levels, debug DLLs, a
   different namespace/compiler ABI, unknown providers or missing symbols.
6. **Native Settings remains native.** Real controls, Save/Discard, metrics and
   per-instance profiles still use the existing bridge. No overlay, localhost
   service, guessed native config key or dummy persistence-only page was added.
7. **Safe optional loading remains.** Initialization is outside DllMain on the
   Qt application thread; missing/failed imports leave native startup running.
   The user must still verify that fallback in a complete player.

The generic startup-DLL importer remains available and unchanged for compatible
regular DLLs. It only makes Windows load a dependency; it does not create a QML
bridge or call an exported initializer. Using it for the current Qt-dependent
DLL would not by itself remove the dependency, and would turn a missing shader
DLL into a required-dependency startup failure. Building a second IPC/config
bridge solely to reuse it adds risk without resolving Qt's public native UI
contract. No system proxy is used by the chosen shader path.

**No player's Qt, VC runtime, system DLL, renderer preference or protections are
replaced, downgraded, auto-upgraded or bypassed.** An unsupported installation
fails closed with a report. Custom OEM Qt configurations remain subject to
actual compatibility checks and user-run verification; public symbol resolution
alone cannot certify every possible custom C++ build configuration.

### Loader metadata detail discovered during the baseline audit

Qt **6.2** uses `qt_plugin_query_metadata`; Qt **6.3+** moc emits
`qt_plugin_query_metadata_v2`. Newer Qt loaders support both. Validation now
requires the entry appropriate to the module's **real compiled** version; it
does not reject a genuine older baseline merely for lacking `_v2`:

- [Qt 6.2.4 plugin exports](https://github.com/qt/qtbase/blob/d3b5353380797f3b67599ccebc5dc916057681e5/src/corelib/plugin/qplugin.h)
- [Qt 6.3 plugin macros](https://github.com/qt/qtbase/blob/v6.3.0/src/corelib/plugin/qplugin.h)
- [Qt 6.7.3 loader's explicit old/new fallback](https://github.com/qt/qtbase/blob/v6.7.3/src/corelib/plugin/qlibrary.cpp)

## 4. Coverage targets vs proof

| Target | New contract's version eligibility | Evidence required before claiming it works |
| --- | --- | --- |
| Qt 5.15.4 x64 | V3 5.15.2 baseline can qualify; V2 still refuses | Exact installed providers/symbols, native plugin/UI loading, actual player/game test |
| Later Qt 5.15 patches | Same-major forward eligibility, not a list of certified releases | Same checks and physical runtime tests |
| Qt 6.7.x x64 | V3 6.2.4 baseline can qualify | Exact installed providers/symbols, native plugin/UI loading, actual player/game test |
| Later Qt 6 minor/patch releases | Same-major forward eligibility only | Full per-installation checks; do not infer hardware support |
| Older than the chosen baseline, Qt 4/7, PE32, ARM64, mixed/debug/unknown ABI | Refused | A separately implemented/audited/tested adapter is needed, not force install |

This does **not** claim all historical BlueStacks/MSI releases have already been
inventoried or certified. Older-than-baseline discoveries must be recorded and
addressed, not silently mapped to a different Qt version. There is no guarantee
for future or arbitrarily modified OEM builds.

### Verification status for this change

- Local strict native suite: **11/11 passed**; the ASan/UBSan build also passed
  **11/11**, with leak detection and halt-on-undefined-behavior enabled. This
  includes **130** package/ABI and **2185** disposable-file/recovery
  assertions, 26 interruption cycles, and synthetic 5.15.4/5.15.19/6.7/6.8
  scenarios. These are not actual Windows DLL/player tests.
- Rebuilt real local Qt 6.8.3 bridge: plugin controls, physical native-tab
  navigation, Save/Discard/persistence, and a separate missing-module process
  passed. Full Customizer Main.qml mock-service flows also passed at 1440×930
  and 980×640; no installed emulator or hardware GPU was exercised.
- Read-only inventory: **11 tests pass**, including all four hash-pinned
  references, compiler-vs-runtime separation, wrong/debug/mixed providers,
  debug/suffixed/mixed import-family refusal, archive no-extraction behavior,
  links and input preservation.
- The source-boundary guard passes. It detects private Qt headers creeping into
  the bridge and any Qt headers creeping into the renderer. It is **not** an
  independent ABI proof.
- The first Windows 6.2.4 build attempt (`487353c`) failed **before runtime
  testing**: the moving `windows-latest` image selected VS 2026, whose STL has
  removed the `stdext` array iterators referenced by the genuine Qt 6.2.4
  headers. This is not a passing baseline test. The corrected shader and
  release jobs pin `windows-2022` and explicitly select VS 2022/v143, where
  those SDK headers remain supported. No Qt headers or version strings were
  patched to hide the toolchain mismatch. See [local build requirements](PORTABLE.md).
- The pinned-toolchain attempt (`cfc0c0e`) compiled the real Qt 5/6.2 bridge DLLs,
  then caught an independent MSVC raw-string limit in the installer/test license
  asset. The fix splits only the C++ representation into ≤4 KiB raw tokens.
  All **17,709 installed notice bytes** remain identical (SHA-256
  `fd42573ba8962381dbf5f27419147cc33f454bc1b97e4134113ef9102554fd9c`),
  with both a source check and compiled C++ bundle assertion. No upstream notice
  was shortened and existing owned installations keep the same notice bytes.
  This failed attempt is not counted as a runtime/ABI pass either.
- Further Windows fixtures passed on Qt 5 and on the **6.2.4 → 6.8.3**
  public bridge, but two separate environment failures remained: the bundled
  builder resolved **6.8.3 CoreTools inside a 6.2.4 CMake configure**, and the
  6.7.3 Python host raised `0xc0000139` creating `QGuiApplication` **before
  importing BSIShaders**. The builder now isolates target/host/CoreTools prefixes
  and verifies `QT_VERSION` at compile time. Both UI fixtures also isolate the
  host's own **native QPA/private plugins before creating the application**, in
  addition to their existing QML isolation. A platform plugin is not the small
  public BSI bridge and must not be borrowed from the older build SDK. A follow-up
  caught PySide6's flattened Windows wheel layout: its Qt DLLs are beside the
  binding, not in QLibraryInfo's reported (nonexistent) `bin`. Host setup now
  requires the matching Core DLL inside that Python binding; six filesystem
  tests cover flat/nested layouts, canonical host identity, missing/wrong-major
  DLLs and rejecting an external SDK. Test expectations also canonicalize Windows
  short TEMP names (`RUNNER~1`); they do not require the same path spelling.
  These test files are never loaded as DLLs. Corrected Windows
  jobs must still finish; none of these failed attempts is called a pass.
- New CI targets actual Windows **6.2.4-built plugin → 6.7.3 and 6.8.3 hosts**,
  a Windows 5.15.2/PyQt5 host, and a Linux distro-built Qt 5 plugin on an
  independent newer PyQt5 Qt 5 runtime. These are **pending until the exact-head
  jobs finish**. The tests print compiled/runtime versions from the actually
  loaded C++ bridge, not from the job label.
- Real **Windows Qt 5.15.4 plugin loading**, installed HD-Player navigation,
  game pixels and Intel/AMD/NVIDIA/other physical GPU/driver combinations are
  **not certified by the synthetic fixtures or Linux tests**.
- No merge and no new fixed-download claim follows from this document.

## 5. Reproduce the inventory (developer/research only)

No interpreter is required by the delivered Customizer or the patched player.
This separate optional research utility needs `pefile==2024.8.26`:

```powershell
python -m pip install pefile==2024.8.26
python scripts/Inspect-EmulatorQt.py "C:\YourPlayer\HD-Player.exe"
```

It reads the explicit EXE and adjacent Qt DLLs only—never PATH/CWD providers,
registry-selected installations or recursively discovered binaries. ZIP inputs
are read in memory without extracting paths or borrowing DLLs beside the ZIP.
Linked/reparse paths, oversized/truncated inputs, duplicate ZIP players and
case-ambiguous runtime names refuse. Reports retain hashes and unknown fields.
It never installs, loads or modifies the player or its DLLs, and a complete
inventory still reports shader support as **not certified by this inventory**.

Run the inventory tests and optional pinned evidence check:

```powershell
python tests/emulator_qt_report_test.py -v
$env:BSI_QT_REFERENCE_DIR = "C:\ReadOnlyReferenceCheckout"
python tests/emulator_qt_report_test.py -v
python tests/shaders/public_qt_api_contract_test.py
```

The application already provides read-only `--shaders-inspect <HD-Player.exe>`.
The revised report distinguishes **actual player Qt** from **selected module
build baseline** and lists the compatibility contract. All actual install,
remove and recovery actions still require fresh reviews and acknowledgment.
