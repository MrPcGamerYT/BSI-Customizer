BOOTVIDEO.QML — DIRECT PROMO.MP4 PLAYER

1. Keep a backup of your existing Assets/BootVideo.qml.
2. Replace that file with the supplied complete BootVideo.qml.
3. Put your supported video beside it, named exactly Promo.mp4.
   Example: <folder containing your patched HD-Player.exe>/Assets/Promo.mp4
4. Fully close and restart the emulator.

This replaces the previous HTML/orbit wrapper with a video-only wrapper.
The old wrapper's public fields are retained, but its embedded logo, rings
and BootScreen.html are not rendered by this version. No HTML is required.
It tries Qt6 multimedia, then Qt5 if the Qt6 component cannot be created.
A runtime decoder/file error is shown instead of silently claiming playback.
It does NOT install multimedia modules/codecs or patch HD-Player.exe.
An existing native boot loader must already load Assets/BootVideo.qml.
It does not change the native boot controller, completion gate or hold timer.
Do not replace your full native boot controller with this visual wrapper.

Use H.264/AVC, 8-bit yuv420p MP4 as a compatibility test, not a guarantee
of support in your installed emulator. Do not merely rename another format.

Verification: JS control-flow/generated-source checks passed. Actual Qt/QML
runtime and MP4 decoding were NOT tested for this new wrapper in this turn;
the sandbox has no installed Qt runtime. Live Windows MSI remains untested.
If an error appears, provide its exact text and the MSI Player version.
