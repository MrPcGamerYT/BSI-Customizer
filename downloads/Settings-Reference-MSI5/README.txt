SETTINGS — SCREENSHOT REFERENCE STYLE (V2)
=========================================
For the existing MSI Settings bridge you supplied. Emulator UI, not Customizer.

INSTALL — no CMD, Python, server or resource rewrite
--------------------------------------------------
1. Close Settings and fully exit the emulator.
2. Back up your existing file:
   C:\Program Files\BlueStacks_msi5\Assets\BSISettingsOutline.qml
3. Copy Assets\BSISettingsOutline.qml from this ZIP over that file.
   Approve the normal Windows administrator prompt if required.
4. Keep your current UISettingWindow.qml / UiSettingsWindow.qml resource
   UNCHANGED. Its loader already points to the correct asset and passes
   bsiHost and bsiTheme. No new embedded resource/slot is installed here.
5. Restart and open Settings. Check navigation, discard/cancel, Save and close.

If your installation is elsewhere, the path in your EXISTING native bridge
must match the asset's actual location. Do not rename the installed asset.
Restore your backed-up asset to undo this visual change.

WHAT THIS VERSION DOES
----------------------
* Black gradient frame, fine grey border, rounded visual corners.
* White/grey outline icons, including Graphics cube and Advanced gear.
* Outlined selected tab with a narrow white indicator and subtle hover.
* Text navigation stays visible down to 720 px; below that it becomes an
  icon rail with tooltips. The rail scrolls if native entries exceed height.
* Retains bsiHost / bsiTheme / pageContainer bridge API.
* Selection follows the native host's actual pCurrentTabIndex. Clicking
  another entry never locally forces selection before the discard guard.
* The native settings model is the ONLY source of tabs. Themes appears
  only if your native model actually contains it. No dummy pages/tabs.
* Native page loaders, page arguments, callbacks, authentication/config
  logic and native Save/discard/close behavior remain in the existing host.

IMPORTANT DIFFERENCES FROM YOUR PASTED SHELL
------------------------------------------
No duplicate Save changes button or footer is added: native pages already
own their Save/commit logic. The unconnected extra button has been removed.
No unused custom ComboBox is provided: declaring a BsiComboBox component
alone does not restyle controls inside the loaded native settings pages.
No hardcoded fallback tab list and no writable selectedIndex local override.

SCOPE / HONEST LIMITS
--------------------
This asset styles the settings FRAME, HEADER, NAVIGATION and ICONS.
Loaded native settings pages and their dropdowns/sliders/buttons keep their
own styles, fonts and backend logic. It does not mutate UiTheme globally,
force Amatic SC, or recolor/rebuild arbitrary native child controls. Exact
changes inside those controls require their real QML definitions.
Native OS shadows/corners are not modified. Visual rounded frame only.

VERIFICATION
------------
Qt6.8.3 software fixture tests: actual QML creation/Canvas rendering, ten
native-context page-loader doubles with args/init callbacks, guarded tab
navigation, close callback and widths 719/720/700/840/1200 px passed.
Original monochrome/external fixture regression and source contracts are
also checked. This is NOT a live Windows MSI runtime result.
Preview.png is labeled inside the image: fixture pages/controls are test
DOUBLES. Its displayed CPU/FPS/Save controls are NOT packaged as settings
pages and are NOT proof of live emulator integration.
Sandbox software tests use abort-on-call shims for unused GL/EGL/XKB/DBus
interfaces; no such APIs were invoked in the successful software tests.

FILES
-----
Assets\BSISettingsOutline.qml  The only installation asset
Preview.png                   Clearly labeled Qt fixture render
README.txt                    These instructions
