VOID / MATCHED SETTINGS-SHELL ICONS
Developed By NOVA XIS
https://www.youtube.com/@nova_xis20

DROP-IN INSTALLATION FOR THE QML YOU PROVIDED
1. Back up the existing Icons.js beside your SettingsShell.qml.
2. Replace that Icons.js with the Icons.js in this ZIP.
3. Keep your existing import "Icons.js" as Icons and
   source: Icons.url(icon.kind, icon.stroke.toString()). No Image source change needed.
4. Reload/restart the player after deploying through your existing verified QML setup.

No red icon variants or red style tokens are included. Default stroke is #f4f4f7,
exactly matching BsiOutlineIcon.stroke in your code. Your own opacity binding
makes idle icons 0.76, selected/hover icons 1, and pressed icons 0.8. We do not
bake opacity or animation into the SVG, so your hover/press/selection motion
continues to operate only on the icon. Hitboxes, native pages, guards, Save,
close handling and pageContainer are not changed by this library.

Supported kinds from your code:
performance, display, graphics, gameSettings, controller, smartphone, palette,
advanced, keyboard, info, headphones, harddrive, preferences, close.
Also includes devices, gamepad, phone, shortcuts, about, user-data and aliases.
Unknown names fall back to preferences. Unsafe color strings fall back to white.

OPTIONAL: DISTINCT DEVICES TAB
Your current iconKind combines phone and device IDs into smartphone. To show
the separate Devices symbol, replace this one line:
    if (/phone|device/.test(key)) return "smartphone"
with:
    if (/phone/.test(key)) return "smartphone"
    if (/device/.test(key)) return "devices"
Leave the rest of iconKind and your native callbacks unchanged.

Icons.js is self-contained and embeds every SVG geometry; the svg/ and png/
folders are optional editable assets, not runtime dependencies for Icons.url.
Copy Icons.js next to YOUR shell, not over unrelated global icon libraries.
This ZIP does not replace the host Window or automatically patch any EXE.
preview.png is a design preview, not an emulator screenshot or a runtime test.
