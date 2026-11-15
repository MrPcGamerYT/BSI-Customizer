VOID OPTIMIZER / HELPER 05 UPDATE
Developed By NOVA XIS

YOUR STUDIO04 PAGE IS ALREADY LOADING (AS IN YOUR SCREENSHOT)
This update redesigns the SEPARATE Windows helper, not just the embedded page.
The protocol remains voidoptimizer-v3 and existing session recovery stays
compatible. There is no need to reinstall the embedded page if Studio04 is visible.

UPGRADE AN ALREADY-REGISTERED HELPER (RECOMMENDED)
1. Restore the session in the old helper, then CLOSE it. Do not force-kill it.
2. Extract this new ZIP to a temporary review folder first.
3. Open the permanent helper-pack folder you previously registered. Back up
   its VOID-Optimizer.exe and BUILD-REPORT.json. Replace those TWO files with
   the new copies, keeping exactly the SAME folder and EXE filename. No registry
   change is required. Also retain the new README and Source for reference.
4. Run that permanent folder's CHECK-INSTALL.cmd for the same emulator root.
   It must report PAGE OK and LINK OK with the new build-report hash.
5. Open that folder's VOID-Optimizer.exe (or Review in helper in Settings).
   The redesigned header must show HELPER 05. Click Refresh, then explicitly
   choose a verified player instance. Nothing applies during scanning.
If you want to MOVE the helper to a different permanent folder instead, Restore
and close it, run REMOVE-HELPER-LINK.cmd in the OLD folder, then SETUP-HELPER.cmd
in the NEW folder. The ownership guard is retained; never force a registry edit.
Do not replace the EXE while a session/helper is running.

DETECTION / WHY THE OLD LIST COULD BE EMPTY
The old code read Process.MainModule and silently dropped inaccessible entries.
New discovery uses OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION),
QueryFullProcessImageName, creation time and the process owner's token. It does
not enumerate modules or request all-access. It works across x86/x64 players.
The same query-based identity check is used again for priority read/write;
only a priority write requests PROCESS_SET_INFORMATION, on the matched handle.
No debug privilege, automatic elevation or cross-user workaround is used.

Refresh reports ready, blocked and exited candidates. Open Diagnostics for
Windows error codes and image paths. If no candidate is found, verify the exact
process name in Task Manager > Details while Android is running. You may enter
its PID and press Inspect PID; only a running, same-user HD-Player.exe with a
readable image/creation time/owner can be offered. Then select it explicitly.
Renamed players and arbitrary PIDs are deliberately refused, not guessed.
For Access Denied, match elevation under the SAME Windows account, or restart
the emulator normally. If Windows/security policy still refuses access, send
the diagnostic text. The helper does not disable security controls.

UI / SAFETY
Dark rounded sections, custom dark choice menus/toggles/buttons, readable
inactive states, DPI-aware layout, scrollable content and pinned review/status
area replace the old white dropdowns and huge empty log. Diagnostics are now a
separate copyable dialog. Inspect/Refresh never starts tuning; explicit player
selection, exact confirmation, same-user identity checks and durable recovery
are retained. The helper still offers only Normal/AboveNormal/BelowNormal.
The title remains 'VOID Optimizer v3 - session helper' for existing activation;
HELPER 05 identifies the new UI/detector, not a new URI protocol.

WINDOWS VERIFICATION
Tests include real harmless x86 and x64 HD-Player-name fixture processes,
query-only discovery and priority reads, wrong/exited identity rejection,
manual PID whitelist, and deterministic denied/other-user/empty diagnostics.
UI smoke tests render the actual WinForms window with a FAKE target/session;
helper05-preview.png and helper05-compact.png show this actual rendered form.
No real emulator tuning, registration, power/timer or priority WRITES happen
in tests. Live OEM emulator detection/permissions still require your check.
BUILD-REPORT.json includes EXE and all C# source hashes. The helper is unsigned.
Source/helper.manifest plus Source/*.cs can be built using the Windows .NET
Framework x64 csc compiler with /target:winexe /platform:x64 /codepage:65001
/win32manifest:helper.manifest and references to System.Windows.Forms.dll,
System.Drawing.dll, System.Xml.dll and System.Core.dll.

The following retained Studio04 instructions apply ONLY if installing the
embedded optimizer page for the first time, not for the helper-only upgrade.

--------------------------------------------------------------------------
VOID OPTIMIZER / STUDIO 04
Developed By NOVA XIS
https://www.youtube.com/@nova_xis20

WHAT CHANGED
Black native-QML design with profile cards, isolated monochrome PNG icons,
rounded sections, hover/focus states, animated toggles and a persistent review
bar. No shared Icons.js/Theme.qml replacement; no navigation or Settings shell
replacement. Optimizer stays where the host already places it, last.

The older screenshot's REVIEW ONLY page can still dispatch an incompatible
helper. Studio04 uses the matched voidoptimizer-v3:// contract, not v1/v2.
The unsafe-option guard is NOT disabled: unknown, duplicate, malformed and
unsupported values remain errors. The compiled helper's safety logic is the
same tested V3 logic, not a new performance engine.

IMPORTANT CORRECTION TO PREVIOUS COPY INSTRUCTIONS
The earlier full-UI installer loads external assets under
<emulator root>\Assets\VOIDReferenceUI\, not the top-level Assets folder.
Do NOT copy this pack's Assets folder over an entire emulator Assets folder.
The scripts require the historical .VOID-Reference-UI.json state to identify
and hash-check the tracked page. They do not claim to inspect the compiled
loader or clear its cache. The visible badge is the required LIVE check.

INSTALL (Windows 10/11 x64)
1. Restore any current optimizer session and close every helper. Close the
   emulator/player. Keep old state/backups/recovery files.
2. Extract the COMPLETE ZIP into a PERMANENT folder, e.g.
   Documents\VOID-Optimizer-Studio04. Never run inside the ZIP. The registered
   helper must remain at this path after setup.
3. Run INSTALL-OPTIMIZER.cmd. Choose the emulator root containing HD-Player.exe,
   NOT Assets. Read the exact target and type INSTALL to confirm.
   Only the tracked optimizer page, six new private PNG icons, and their hash
   entries in .VOID-Reference-UI.json change. A backup folder is printed inside
   the emulator root. Keep it. Shared icons/theme, SettingsShell, EXEs, DLLs,
   auth, native pages/navigation, boot and RPC are untouched.
   Missing/stale state, a changed page, wrong root, reparse points or private
   asset collisions STOP the install. Do not delete state or bypass the check.
   Share the diagnostic text, .VOID-Reference-UI.json and active page instead.
4. Run SETUP-HELPER.cmd and accept the helper's registration dialog. This ONLY
   registers this user's voidoptimizer-v3 link, not performance settings.
   A different existing V3 owner is refused. Restore/close that owning helper,
   then unregister through its own removal script; do not force registry edits.
   v1/v2 handlers are untouched. Moving a registered folder breaks its link.
5. Run CHECK-INSTALL.cmd; select the same root. Require PAGE OK and LINK OK.
   This checks exact pack page/icon hashes, tracked state and handler command.
6. Restart emulator Settings, open its last Optimizer / Session tools entry.
   REQUIRE the badge: STUDIO 04 / HELPER V3. If it still says REVIEW ONLY,
   STOP: a different/cached page is active. Send the active loader and page;
   do not keep replacing guessed folders or launch the old review button.
7. Choose options, press Review in helper. The window should be titled
   'VOID Optimizer v3 - session helper' (with a long dash in the actual title).
   Select the intended running HD-Player instance, inspect actions, then
   explicitly confirm Review & start session. Launching is NOT proof of apply.
   Keep the helper open. Restore or normal close undoes the session; failures
   preserve the durable recovery record and are reported rather than hidden.

If Windows policy blocks an unsigned PowerShell script, do not weaken that
policy. Review/sign the included source through your normal trusted process.
If the emulator is installed in a protected directory, file permission errors
stop installation. Use the required trusted administrator context for FILE
installation only. Register/open the helper under the Windows user who plays.
The scripts never disable antivirus, SmartScreen, UAC or execution policy.

UNDO
Restore and close the session helper, then run RESTORE-OPTIMIZER.cmd, choose
that emulator root and paste its printed Studio04 backup folder. Restore first
verifies all installed hashes and backup hashes; externally changed files stop
restoration. Exact old page/state bytes return; added private PNGs are removed.
The backup stays available. Use REMOVE-HELPER-LINK.cmd from the registered
folder to remove ONLY that helper's V3 link. Restoring files does not unregister.
Do not run other UI/theme installers concurrently. If interrupted, preserve the
printed backup/manifest and share the error; do not delete tracked UI state.

OPTIONS / LIMITS
Balanced=Normal, Performance=AboveNormal, Background=BelowNormal priority for
ONE explicitly selected same-user HD-Player. No High/Realtime, affinity, RAM
cleaner, process killing, services, injection, drivers, boot or game-memory edits.
Power plan and current-user Game DVR changes are optional and reviewed/read
back. They can affect heat, battery use and Windows recordings.
Timer URI units are 100 ns: 0=default, 10000=1 ms, 5000=0.5 ms. The HELPER holds
the native request; Windows may scope/ignore it. Supported range is checked.
No FPS/latency guarantee. Awake requests session idle-sleep prevention, not
screen policy. Auto-restore checks PID, start time, image and Windows identity.
The persistent review bar is staged choices/status, NOT live performance data.

TESTING / PROVENANCE
BUILD-REPORT.json records source/helper hashes. Real Qt tests exercise controls,
wide/narrow viewports and all 144 request combinations without external launch.
Windows filesystem tests exercise tracked install, decoy isolation, refusal,
state preservation and exact restore. Helper self-tests use a fake OS adapter
for rollback, recovery, identity, timer/awake and transaction behavior. The real
WinForms window is smoke-tested with a fake session. No protocols registered
or real tuning applied during tests. OEM emulator rendering/performance and the
user's installed binary loader remain unverified until step 6.
Adapted from BSI-Customizer bc4c68522878f7c060e82a3524e127099d4b0c54.
Helper source and LICENSE are included. The helper/scripts are unsigned.
