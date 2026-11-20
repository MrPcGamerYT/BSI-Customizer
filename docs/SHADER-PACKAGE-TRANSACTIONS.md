# Experimental shader package validation and recovery

**Backend preparation, not an enabled/released feature.** `ShaderInstall` is not
connected to a Customizer button/CLI action and no shader payload is bundled.
Nothing here certifies a running player, real game effects or a physical GPU.
Do not merge the feature on these tests alone.

## Package/ABI boundary

`ShaderPackage` only inspects bytes. It never uses LoadLibrary, QPluginLoader,
DllMain or an exported function to inspect a candidate DLL.

- Native AMD64 PE32+ EXE/DLL types, section bounds/non-overlap/alignment and
  file-backed directories are required. An EXE renamed `.dll` is refused.
- The actual **named data export** `BSIShadersDescriptor` must be read-only,
  non-executable, non-forwarded and contain the complete bounded v2 contract:
  `BSI_SHADERS_V2;QT=x.y.z;ARCH=x64;ABI=msvc;BUILD=release;NS=default;END;`.
  Finding a string anywhere in the file is not sufficient. Qt's matching native
  `qt_plugin_instance` and metadata-query function exports must also exist.
- Player and plugin must import the same Qt Core/Gui/Qml family through actual
  descriptors. Qt providers must have **exact matching release file AND product
  VS_FIXEDFILEINFO versions** in every version-resource language. Editable
  `FileVersion` display strings, a matching major alone and loaded-module names
  are not used as compatibility proof. Debug/private/mixed versions and custom
  namespaces are not supported by this conservative contract.
- All used non-system import names/ordinals must resolve in the supplied native
  providers, including **delay imports, transitive dependencies and forwarders**.
  Forwarding cycles and missing/unknown/debug CRT dependencies refuse. Legitimate
  zero-initialized runtime data exports are not confused with executable code;
  the plugin descriptor and native entry points still require file-backed data.
- `qmldir` must be the exact owned regular-plugin definition (LF/CRLF accepted),
  not a script/import-path redirect. This is **BSIShaders.dll**, not a proxy DLL.
- Limits: 128 MiB per PE, 512 MiB total validation input, 64 providers, bounded
  import/export/string/resource walks and forwarding depth. Parser failures
  return no successful package identity/runtime hashes.

The descriptor/owner file is **not a digital signature** and these checks are
not a malware or arbitrary-custom-Qt certification. A release must be built from
reviewed sources with a pinned/trusted package SHA-256. Custom/other ABI builds
must be refused, not guessed. Qt's own loader and installed-player tests remain
additional gates. The current build is not certified against every historical
player Qt version.

## Filesystem backend

`ShaderInstall::apply` requires an acknowledged operation, an exact current EXE
SHA-256 and the exact reviewed package SHA-256. `remove` also requires the current
EXE hash. Runtime dependency bytes are read only from the player's directory;
only known VC-runtime names may fall back to Windows' GetSystemDirectory path.
No dependency is searched through PATH/cwd or copied, updated or overwritten.
An adjacent system-library override is refused instead of endorsing a proxy.

The intended module location is `<player-directory>/BSIShaders/`, for the
engine's application-directory QML import path. It contains exactly:

- `BSIShaders.dll`
- `qmldir`
- `bsi-shaders-owner.json` (canonical schema, target EXE basename, package hash,
  compiled Qt version)

An existing directory without this complete exact owned set, extra files,
changed hashes or a mismatched target is never adopted/overwritten/deleted.
Linked/junction/noncanonical paths are refused. Native source ownership and
capacity are separately checked by `ShaderSettingsImage`; the recognized
redesigned top-tab Settings shell is a prerequisite, not something this backend
guesses or replaces on an arbitrary raw player. Updates must start from a
complete owned EXE/module pair. Removal uses the current owned fragments,
not a stale whole-EXE backup that could undo later customizations. A missing
module can be removed from otherwise exactly owned native sources without
inventing or deleting another DLL.

## Recoverable, not globally atomic

An EXE and module directory cannot be exchanged as one atomic filesystem object.
The backend uses an exclusive per-player lock, fail-closed Windows process
checks, expected-content comparisons, QSaveFile without direct-write fallback,
exclusive staging and complete read-back instead. The player must remain closed
throughout; this is not a protocol for live DLL replacement or another patcher's
concurrent writes.

Before changing a target, it writes and verifies both whole EXE snapshots and
both owned module snapshots in a unique sibling recovery directory. An exclusive
canonical journal records all snapshot hashes and target identity; every snapshot
is re-read before the first mutation. Runtime dependencies are rechecked before
publication/EXE commit and final verification.

1. **Install:** publish the staged regular module, then atomically replace and
   read back the bounded EXE source plan.
2. **Update:** park the verified owned directory and publish the complete new
   directory; an already-correct EXE is byte-unchanged.
3. **Remove:** remove the optional bootstrap/native row from the EXE first, then
   park the owned directory. Other native/custom resource bytes survive.
4. After the full resulting pair is verified, atomically change the journal from
   `prepared` to `committed`, then clean up.

An ordinary pre-commit failure attempts a verified rollback. Interrupted
`prepared` transactions restore the exact **previous EXE and module pair**.
Interrupted `committed` transactions only finish cleanup and never silently undo
a committed update. A recovery interrupted during its own directory renames can
be recovered again. Both current objects and all snapshots must be known before
any recovery write; an independently changed EXE/module/backup/journal is a
conflict, not permission to overwrite it. A failed rollback retains snapshots
and reports that the player must not be launched.

Cleanup removes the external journal only after a verified consistent target
pair, before deleting snapshots. Interruption during later cleanup can leave
**inert backup files** rather than a journal pointing at half-deleted backups.
Pre-journal staging failures also leave inert material for inspection. Cleanup
never recursively deletes a user directory; only exactly checked owned files
are removed. This is tested process/I/O interruption recovery, **not a tested
sudden-power-loss/filesystem-durability guarantee**.

## Encoder failure containment

Tight fixed-slot compression no longer calls the upstream encoder unguarded.
`BoundedZopfli.c` contains all upstream allocations and exit calls in **C frames
only**, with a per-thread allocation ledger and 64 MiB live-allocation cap. OOM,
budget exhaustion or an encoder exit cleans up and leaves the caller's output
untouched. The C++ call site keeps ordinary compression, after which capacity
checks can safely refuse the operation. No longjmp crosses a C++ destructor or
Qt frame; there is no subprocess or recipient helper runtime.

Strict sanitizer review also exposed two upstream unaligned/aliasing typed
loads. The 23 original source/license hashes remain unchanged. A separately
input/output-hash-verified build-only `memcpy` correction fixes the generated
match source; UBSan alignment checks are **not disabled**. See the mandatory
`src/third_party/zopfli/README.bsi.md` notice. Older exit-zero sanitizer runs that
contained recoverable UBSan diagnostics must not be cited as clean validation.
Current sanitizer tests use `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1` and
leak detection, with the C guard and encoder both instrumented.

## Automated evidence and remaining gates

The isolated suite exercises synthetic registered-resource player images and
synthetic native PE packages, never executing them:

- Strict Qt 5/6 package/type/descriptor/version/export/dependency refusals.
- Complete install, idempotent repeat, update without EXE churn, removal with a
  later unrelated customization preserved and no runtime DLL writes.
- **26** ordinary I/O-failure/interruption cases across install/update/removal,
  parking/publication, EXE replacement and the committed marker; further cases
  cover rollback failure, interrupted recovery, locks, symlinks, unowned/extra
  files and conflicting target/snapshot/journal data.
- **Every one of 4,288 allocation points** in the small encoder fixture is
  fault-injected, plus fatal exit, input/output/memory limits, unchanged failed
  output, zero remaining allocations, subsequent success and concurrent calls.
- A linker-stub test exercises the actual production C++ fallback on guard
  failure. The real fixed-slot/reference tests still require a smaller exact
  encoding, not a stubbed success.

Windows CI additionally requires read-only validation of the **actual compiled
Qt 5.15.2 and 6.8.3 release modules against their actual SDK/VC dependencies**,
then the existing plugin/native-Settings physical-input fixtures. These checks
must pass at the new head; adding a test is not a passing result.

Still required: release payload selection/distribution, safe Customizer UI and
preflight/recovery UX, the player's real QML import-path behavior, actual
installed-player navigation/missing-incompatible-package behavior, Windows
presentation validation and game-frame evidence, and the physical
Intel/AMD/NVIDIA + configured/observed renderer matrix. No merge is authorized
by preparation/backend/software results alone.
