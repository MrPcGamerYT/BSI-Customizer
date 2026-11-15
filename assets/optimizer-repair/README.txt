VOID OPTIMIZER / MATCHED HELPER REPAIR V3
Developed By NOVA XIS
https://www.youtube.com/@nova_xis20

WHY THIS REPAIR
The screenshot shows the older 'REVIEW ONLY' page and a helper that rejects
extended optimizer options. Replacing QML does not update a previously built
EXE or Windows' registered URL handler. That older combination can launch an
old helper for a timer/awake/auto-restore request and stop at its whitelist.

This pack pairs the embedded OptimizerPage.qml with a tested x64 .NET Framework
helper. The page uses voidoptimizer-v3:// and displays HELPER V3. Registration
is ONLY for that new per-user protocol, avoiding the old v1/v2 dispatch link.
The validation guard is retained; unknown/duplicate/malformed parameters remain
errors. This is not a workaround that ignores timer or sleep options.

INSTALL (Windows 10/11 x64)
1. In any OLD helper, Restore the current session, then close the helper.
   Close the emulator. Do not force-kill an active helper or delete recovery.
2. Extract this entire ZIP into a NEW PERMANENT folder, for example
   Documents\VOID-Optimizer-Repair-V3. Do not run from inside the ZIP, and do not
   move/delete the folder after registering it.
3. Run SETUP-HELPER.cmd and accept its registration dialog. This registers only
   this Windows user's v3 URL link; it does NOT apply performance tuning.
   If a different v3 handler already exists, registration stops instead of
   overwriting it. Unregister it using its owning helper after restoring and
   closing that helper, or retain/rebuild the existing installation in place.
   Do not manually disable the ownership check or overwrite arbitrary registry keys.
4. Back up your emulator's existing Assets\OptimizerPage.qml, then replace ONLY
   that file with Assets\OptimizerPage.qml from this ZIP. Leave Theme.qml,
   shared Icons.js, SettingsShell.qml, sidebar/topbar, boot and DLLs unchanged.
5. Restart the emulator. The embedded Session tools page must now show HELPER V3,
   NOT REVIEW ONLY. Choose the options and press Review changes. The separate
   helper must be titled 'VOID Optimizer v3 — session helper'.
6. Explicitly select the intended HD-Player instance in the helper. Inspect its
   settings and confirm Review & start session. Review/launch is NOT an applied
   result. Keep the helper open during play; Restore or normal close undoes the
   session. Restoring failures retain the recovery record and are reported.

If HELPER V3 does not appear, the host is loading a different/cached page. This
archive does not patch an embedded EXE or guess another loader path. Send the
active optimizer page/loader instead of replacing unrelated Assets files.
This pack targets the embedded Session tools page shown in the screenshot,
not an unrelated standalone F9 optimizer QML window.

OPTIONS / LIMITS
Balanced=Normal, Performance=AboveNormal, Eco=BelowNormal for ONE explicitly
selected same-user HD-Player process. No High/Realtime priority, affinity,
RAM cleaner, process killing, services, drivers, boot options or game-memory edits.
Optional existing High performance power plan and current-user Game DVR changes
are reviewed and read back. They may affect heat/battery/game recordings.
Timer URI values are exact 100-nanosecond units: 0=off, 10000=1 ms, 5000=0.5 ms.
A native timer request is held by the HELPER, not injected into the player.
Windows may scope/ignore it; the platform's supported range is checked. There
is no FPS/latency guarantee. Awake only requests session-scoped idle-sleep
prevention. Auto-restore monitors the selected process identity, not just PID.

TESTING / RECOVERY
BUILD-REPORT.json records source/EXE hashes. Windows helper self-tests use a fake
OS adapter to exercise parsing, rollback, retry, durable recovery, identity
checks, concurrent changes and timer/awake acquire/release behavior. The real
Windows form is smoke-tested with a fake session, without applying tuning or
registering a protocol. Real Qt tests generate every valid page request with
previewOnly=true. This is NOT a live emulator or real performance test.

The helper is unsigned. No antivirus, SmartScreen or security-policy changes
are performed or required by the scripts. Source/Optimizer.cs is included.
Do not proceed if your security tooling or compiler reports a problem.

To undo: Restore and close the v3 helper, run REMOVE-HELPER-LINK.cmd from this
same folder, then restore your backed-up OptimizerPage.qml if desired. The old
v1/v2 links are deliberately not deleted or reassigned. Boot/RPC remain unchanged.

PROVENANCE
Based on the repository's prior helper/page source at
bc4c68522878f7c060e82a3524e127099d4b0c54. This repair versions the launch contract
and pairs tested source/binary/page; it does not import old EXEs or replace the
rest of that historical emulator UI pack.
