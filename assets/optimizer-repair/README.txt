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
