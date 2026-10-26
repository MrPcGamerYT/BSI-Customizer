# BSI Customizer

[![CI](https://github.com/MrPcGamerYT/BSI-Customizer/actions/workflows/ci.yml/badge.svg)](https://github.com/MrPcGamerYT/BSI-Customizer/actions/workflows/ci.yml)

Qt Quick desktop customizer for BlueStacks / MSI App Player on Windows.

## Sign-in gate (Discord only)

This is a private dev tool: the app opens on the Premium-Optimizer-style
sign-in card with **one option - Continue with Discord**. Access follows
either a dev allowlist of Discord user IDs (`discord.allowUsers`) or a
**Discord server role gate** (`discord.guildId` + `discord.roles` +
`discord.requiredTier`, checked live with the `guilds.members.read` scope);
a rejected sign-in shows the account's Discord ID on the result page so it
can be added. The flow is Discord OAuth2 with
PKCE + a `127.0.0.1:43713` loopback callback (paste-the-link fallback), and a
successful sign-in writes an HMAC-signed machine-bound session that only
earns a one-click "Welcome back" continue on the next launch. The gate is
enforced in C++ (`AuthManager` + `Backend` guards), not only in the UI.
The whole `auth.json` can also be baked into the EXE at build time from the
`ZLP_AUTH_JSON` repository secret (the Actions workflows pass it through), so
releases sign in with no sidecar config file.
Setup: [docs/AUTH.md](docs/AUTH.md) and `auth.example.json`.

## Download: installer (and the single EXE next to it)

The release download is **`BSI-Customizer-<version>-setup.exe`** — a normal
installer that puts the app in `C:\Program Files\BSI Customizer`, adds a Start
menu shortcut and an uninstaller, and starts it when it is done.

The same release also carries **`BSI-Customizer-<version>-win64.exe`**, which is
the whole app in one file: Qt/QML plugins, SVG support and the VC runtime DLLs
included. A native launcher unpacks into a private temporary directory, runs the
app, and cleans up on normal exit. Nothing is installed and nothing is
downloaded at runtime, so it runs from a USB stick — it just cannot update
itself, because Windows will not let a running program overwrite its own file.

That is the reason the installer is the default download: **the built-in updater
runs the setup**, so an update replaces the installed files properly and starts
the new build. See [the full portable workflow/build guide](docs/PORTABLE.md)
for Actions, local builds, clean-environment tests, temporary-file behavior, and
license/DLL extraction. Every push and pull request builds the Windows x64 EXE
on a GitHub-hosted runner, compiles the installer and runs the whole engine
self-test there (`--selftest`), so a change is only mergeable when the real MSVC
build is green.

## Theme patching

1. Select the **HD-Player.exe from the installation you actually launch**.
2. Close **all** running HD-Player instances. Running processes keep their old theme in memory and Windows can lock their files.
3. Load the theme (Auto picks QML (new) or Hex (old)), choose a preset or edit colors, then click **Patch Theme** (QML) or **Apply Theme** (Hex).
4. Start BlueStacks again. You can close it, change the palette, and relaunch it again; another theme changer is not a prerequisite.

The QML patcher validates Qt resource trees (14-byte v1 and 22-byte v2/v3 nodes), preserves neighboring resource headers, and uses memory-relative offsets when relocating a resource across PE sections. It refreshes the resource timestamp so a previous run's disk cache is rejected, and invalidates the theme's embedded compiled QML where identified. It does not globally disable Qt caching or delete application data.

If an older patch already wrote the desired colors but BlueStacks still shows an old palette, click **Patch Theme** again (even without new edits) to refresh the cache. Ambiguous resource layouts and edits that cannot fit are reported instead of silently claiming success. Use the **Hex (old)** method for legacy builds without a QML theme.

Presets work in **both** methods. On **QML (new)** a preset stages the palette for **Patch Theme**. On **Hex (old)** the same eleven presets fill the **New value** column for every palette color the EXE carries (the camelCase names such as `accentNormal` and `primary70`), ready for **Apply Theme**. Names are matched against the spelling variants BlueStacks uses across builds, so a palette looks the same on a Qt 6 player and on a legacy Qt 5 / MSI 5 one. **Recolor switches** uses the preset's toggle colors in either method.

The original `.bak` is preserved. Theme edits are made against the current EXE, not a stale backup. Writes are checked and atomically replaced where supported; close BlueStacks and run the tool as administrator if Windows refuses access.

## Sidebar buttons in the title bar

The layout tweak moves the **original sidebar** into a host in the middle of the
title bar, instead of creating a second set of controls. This keeps button state,
animations, and popup ownership attached to the same instance used by the player.
The host keeps the same margin on both sides and follows window resizing, so the
row stays centred exactly as it always has; the title bar's own Back/Home/Recent
step aside while the row is shown, because the row carries the sidebar's own
copies of those three buttons.
The buttons stay visible until the available width runs out; only then does a
three-dot overflow button replace the buttons that don't fit. Sound, controls,
and other button panels open directly beneath the button that launched them
(or beneath the three dots when opened from overflow), staying inside the
visible screen of the monitor the player window is on - the panel is clamped by
its own visible edge, not by the popup window's shadow area. There is no
separate option for responsive overflow or panel placement - that behavior is
built into the title-bar move and applied automatically. Untick and apply to
restore the stock sidebar.

The tweak recognizes the player's sidebar by its **structure** (the instance
that carries the stock sidebar width, the sidebar component and its button
strip), not by one exact palette, visibility or Android-frame expression.
Those spellings change between BlueStacks builds, and matching them literally
used to report *Unavailable* on such a build. A value this tool has not seen
before is replaced by the patched value plus a tagged copy of the original
text, so undo still restores the document byte-for-byte.

The title-bar move needs only the **button strip**: one layout inside
`PlrSidebar.qml` that holds the sidebar buttons. Builds whose sidebar has no
three-dot button, Repeater model or windowed popups are moved as well; they
keep their own overflow code (the status line says *"This build keeps its own
overflow menu"*). A sidebar with no movable strip at all is refused with that
reason instead of being written blindly.

### Choosing which buttons the row shows, and in what order

The first apply already puts the row in a sensible order: **Back, Home and
Recent**, then **Fullscreen, Sound, Files / APK and Settings**, then the rest of
the sidebar buttons in the order the player declares them.

The **Top-bar button order** card below the layout option lists every button the
player really has - the strip's own buttons, including Back, Home and Recent, and
the entries its Repeater renders - as one numbered row. **Up/down** move a
button, the tick leaves it out of the row, and **Apply order** writes the result.
**Reset to default** puts the order above back. Back, Home and Recent are marked
*always shown*: they can be moved but not hidden, because the tool never removes
the player's navigation. The card follows the layout by itself: applying or
restoring the title-bar move refreshes it, so it never shows a stale row.

Moving a button moves its **declaration**, not its contents: the declaration
keeps its bindings and its line breaks, so turning the layout off restores the
stock document byte-for-byte. Hiding one switches the member that decides its
visibility - for the strip's own buttons, which the player's resize handler
re-shows, that happens in one tagged block at the end of `fResetSidebar()` that
a later run replaces or removes verbatim. The row's own space calculation reads
the same list, so an unticked button stays unticked when the window is resized;
Settings is one of these buttons, with a single copy in the row that can be
moved and unticked like any other.

The patched document is always longer than the record it replaces, so the
record is built in a gap **the file already has** - a run of zero bytes inside
the same data section that no resource owns - and the resource tree node is
re-pointed at it. Nothing is appended and no section changes size, so
`HD-Player.exe` keeps its exact size; the gap is taken with a little slack, so
the next order or visibility change still fits in place. Only an image with no
usable gap falls back to the mapped storage older versions always added.

If you used an earlier version of the title-bar tweak, close BlueStacks,
keep the option checked, and click **Update** to upgrade the old layout.
Restart BlueStacks afterward. Actual windowed-popup behavior still needs
testing on each supported player build; unrecognized QML is refused rather
than guessed.

## UI tab: clean-up, layout, removals and the About page

The **UI** tab (formerly *Extras*) holds everything that changes what the
player window shows, plus a one-click clean-up and the About editor. Every
option refuses to write while BlueStacks is running, keeps the original
`.bak`, verifies the rewritten QML by reading it back, and clears the player's
QML disk cache so the change is picked up on the next start.

**Clean player UI (one click).** One apply hides every extra button.

* **Title bar:** help, menu/settings, quests, rewards, raw-mode warning,
  now.gg account, nowbuX, pin-to-top, the top-bar icon list, macro, sync,
  screen recording and the sidebar toggle.
* **Sidebar:** screenshot, screen record, media folder, rotate, location,
  shake, lock cursor, macro, sync, eco mode, instance manager, side panel,
  live stream, chat, settings and the strip's own Back / Home / Recent.

What stays is what the window needs: the BlueStacks logo, the title/version,
the title bar's **Back, Home and Recent** buttons, the minimize/restore/close
controls — and in the sidebar **fullscreen, volume, game controls, install
APK and trim memory**. **The sidebar itself is not removed**: it keeps those
five buttons in place. The sidebar only leaves the side of the game view when
**Sidebar buttons to the top bar** is applied, which moves it into the title
bar; the two options compose. Press **Restore stock UI** to write the original
QML back byte-for-byte (this also undoes a sidebar-hiding clean-up written by
an older version of the tool). A player that still carries such an older
clean-up — title-bar buttons hidden, sidebar buttons visible — is recognised:
the button reads **Update clean-up** and writes the missing sidebar half (and
brings the strip back) without restoring first.

The two title-bar labels — the player **version** and the engine label next to
it (**N32**, **P64**) — are hidden without adding a single byte: the label's own
`text:` binding is emptied instead of a `visible: false` member being added. A
player record has a fixed size and the file has no free space to move it into,
so a growing edit used to end in "needs N bytes but its record only holds M";
an edit that only ever gets shorter cannot. The element, its bindings and its
place in the layout stay, so nothing the player reads is changed.

**Ads / UI Removal** nulls the selected ad/UI strings so those elements never
render. Back, Home, Recent, the title-bar logo and fullscreen are not removal
rows: the clean-up above is the only thing that hides navigation, and it keeps
them.

**Settings → About page.** The card reads the page itself, so every field
starts with exactly what that build prints — nothing is retyped:

* the **About text** — the copyright/Terms-of-use sentence, markup flattened to
  the words the page shows;
* one row per stock link (**Support email**, **Support**, **Website**), each
  with three separate fields: the **name** the row starts with, the **text the
  row prints** for the link itself, and the **address it opens**. The address is
  taken from the row's own handler (`Qt.openUrlExternally(…)`) or, for rows that
  open a mail client, from the address the row prints. The printed text and the
  address are independent — a row can open `https://example.com/join` and still
  print `Join the channel`; leave the text empty and the row prints the address
  the way the stock page does;
* a **Show** tick per row, so a single stock link can be hidden while the
  others stay.

Some pages gate both sections with a binding such as
`visible: uiBackend.oem !== "msi5"`, which is false on every MSI install, so
those sections are drawn hidden there — and the thin separator line drawn
*between* the two sections carries the same check, which is why it is the whole
page that is scanned for it and not just the two blocks. The patch always
switches all of them on by default: that is what makes the edited text and the
links visible at all, and `true` is the only value that is correct on both the
MSI and the BlueStacks builds (where the binding is already true). The toggle is
still there as **Sections shown — switch back to stock** for anyone who wants the
page left exactly as the build draws it, and the card says which build drew them
hidden. **Diagnose** prints what the editor found on the selected EXE — the page,
its current text, every row and the reason if the card refuses — so nothing has
to be guessed.

A BlueStacks player carries the page **twice**: `PlrAboutSetting.qml` as text,
and a compiled copy of the same page that the window actually loads. Every word
the page prints — the headings, each row's name, the address it opens, the
words the link shows, the copyright sentence, the update button's caption — is
a UTF-16LE record of that compiled unit's string table, and editing only the
`.qml` reaches a copy nothing displays (which is exactly what "it patches but
the page never changes" looks like).

The card therefore reads **that** table, and the fields show what it says:
`FF FF FF FF`, the character count, the header size, then the text. The page is
not recognized by any sentence — it is found by the headings it never loses
(`Important links`, `About and other info`) and parsed by shape:

```
[name] [nxt_cn] " - <a …>label</a>" " - <a …>label</a>" [address] [address]
```

so the name, the words the anchor prints and the address it opens are read as
three separate things, for both the normal and the China branch of a row. Each
row's **position** is its identity, which is what makes an edited page read back
exactly and a second apply a no-op — and a hidden row stays a row (its name and
printed words are blanked, its anchor is left pointing nowhere) instead of
disappearing from the list, so it can be switched back on later with its address
intact. The name it had is kept in the hidden anchor's `href` — on a player that
carries no `.qml`, that is the only copy of it left — so showing the row again
gives back its name, its words and its address.

A page built with the Qt QML compiler also carries a **compiled copy** of the
page itself (a `qv4cdata` unit), and a compiled unit takes precedence over the
`.qml` source even when the disk cache is empty - which is why a row hidden in
the source still painted its dash, and why the update button could only ever be
blanked instead of removed. Applying therefore switches that unit's format
version off, so the player compiles the edited source instead; restoring puts the
version back from the backup. On a build whose page exists *only* as compiled
code the unit is left alone (switching it off would leave no page at all), and
the string edits below are what it gets.

A player that keeps its QML out of `HD-Player.exe` - a small launcher with the
page in a DLL beside it, as newer MSI builds do - is handled too: the file that
carries the page is located next to the selected EXE and that is the file that is
read and written, so "not embedded in this player" only appears when nothing in
the folder has the page.

A build whose resource registration this tool cannot follow - the MSI 5.12
player is one, and it writes no `qres` header at all - is not "not embedded" any
more: the page's record is located by its own words, and the resource tree is
then asked to map it. A page found that way is a **normal resource** - it is
rewritten through its node, its timestamp is refreshed so the player recompiles
it, and when the edited page is longer than the room its record has (the MSI
record is full to the last byte) the record is built in **free space the file
already has** and the tree node is re-pointed at it. The file still cannot grow
by a single byte, and the pool's own payloads are excluded from that free space,
so a long run of zeros inside, say, a silent WAV is never mistaken for a cave. A
record that no tree maps is still read, and written back in place inside the
room it already has, with Apply saying exactly how many bytes are missing when
it does not fit.

Two more rules keep that page inside its budget. The About line is only
rewritten when it does not already print what it should - a field the user left
alone is not re-tagged - and the OEM check the MSI build uses to hide both
bottom sections is replaced by `true/*msi5*/`, which is **shorter** than the
expression it replaces and puts the check back byte for byte on restore.

Every replacement is written **inside its own record**: the count is updated and
the rest of the slot is padded, so the file never grows and no offset moves. A
text longer than its slot is refused with the length that fits, never truncated,
and the link fields stop at the room their own record has, so a value the player
would have to refuse cannot even be typed — the card prints those numbers under
each row (on the builds we have: about 7–15 characters for a name, 19–23 for the
words a row prints, 23–31 for the address), because a record is as long as the
player made it and cannot grow. The About sentence is the literal right after
its heading, with the "Terms of use" link the page appends to it kept when the
edited text still ends with those words and cleared when it does not.

Applying writes every copy of the page the file carries, then reads the page
back **from the file on disk** — a write that does not read back is undone and
reported instead of being called a success. The player's QML caches are cleared
too, both next to `HD-Player.exe` and in the places Qt itself uses
(`%LOCALAPPDATA%\…\qmlcache`, `<ProgramData>\BlueStacks_nxt\…`), because a
compiled unit from an earlier launch otherwise keeps painting the old page.
Closing the player window does not stop BlueStacks: quit it from the tray icon
before the new page can be compiled.

One apply keeps the page and its layout exactly as it is (logo, version banner,
section headings) and only changes what it says: your text replaces the
copyright line, and each link row is rewritten with the name, the words it
prints and the address you left in it. In the compiled copy the address record
holds the address and the anchor holds the words — never the other way round, so
editing the address really changes where the row goes. Rows ticked off, a row
left without an address, any stock row the list does not name, and the stock QQ
row are hidden - in the source with `visible: false`, so nothing of them is left
(no name, no address, and no " - " in front of where the link was). The
**Check for updates** button is removed together with its "More info" and
incompatible-version rows, so nothing on the page points at BlueStacks updates
any more; on a page that exists only as compiled code its caption is cleared
instead, which is all a string edit can do there. Press **Restore stock About**
to restore the untouched page byte-for-byte.

Both tools work on the Qt 5 and Qt 6 players: BlueStacks 5.22 and the older
5.10 build, plus the MSI App Player 5.9 and the current MSI build. The
clean-up understands both shapes the player uses to re-assert button
visibility (`PlrTopbarElement.pVisible` and the MSI 5.9
`iTopbarItems.append({ "id" : …, "visible" : … })` list) and both sidebar
shapes (an explicit `pIsFeatureEnable` binding, or none — the tool adds one
and removes it again on restore), and the About page is rewritten on both page
generations (they differ only in the copyright year).

## Embedded resources

The **Resources** tab lists **every** file embedded in HD-Player.exe's Qt
resource pools — QML, JavaScript, SVG markup, PNG/JPEG/BMP/GIF/ICO/WEBP/TGA
art, JSON/XML/CSS/HTML, compiled caches and everything else. The SVG and
Images tabs are gone: both live here, with the whole pool beside them.

- **Scan Resources** walks every resource pool the EXE registers (plus the main
  QML pool) and lists each entry with its path, format, size and, for pictures,
  its pixel dimensions. Filter by name or by kind (Pictures / SVG / Code /
  Binary).
- **Pictures** get a thumbnail in the list and a large preview; **Extract…**
  saves the selected entry exactly as stored and **Extract All** writes the
  whole pool to `embedded_resources\` next to the EXE, keeping its folders.
  What a row holds is decided from its **bytes**, not from its name: a build
  that stores an icon under an unfamiliar or missing extension still gets its
  picture. When QML will not take a `data:` URL the same bytes are handed to it
  as a temp file with the right suffix (which is how the SVG plugin is loaded on
  the older players), and an entry that genuinely is not a picture says so
  under the preview instead of leaving an empty box.
- The list belongs to one EXE: selecting another `HD-Player.exe` drops it and
  the tab scans itself again, so a preview can never be read from the previous
  file at the new file's coordinates.
- **Code** — QML, JS, SVG, JSON, XML, CSS, HTML and text — opens in the editor
  as the source the player actually loads, and **Save code** writes it back.
  Editing QML also invalidates its compiled `.qmlc` companion and the player's
  QML disk cache, so the new source is what runs.
- **Binary** entries (compiled caches, fonts) are listed and can be extracted
  or replaced with a file of the same kind.
- **Replace with file…** validates a picture first (its real format is sniffed
  from the bytes and has to match the entry) and verifies the rewrite before
  the EXE is replaced on disk. The original stays as `HD-Player.exe.bak` and
  nothing is written while HD-Player.exe runs.

**Two hard limits, checked before anything is written.**

1. *The embedded file never grows.* An edit or replacement that decodes to more
   bytes than the entry already holds is refused outright — no save, no backup,
   no change to the EXE.
2. *HD-Player.exe never grows.* The record has to fit the space the entry
   already owns. Because the shipped pools are packed without padding, that is
   usually too tight for a recompressed edit, so the writer looks for an empty
   gap inside the same PE section, builds the record there and re-points the
   resource tree node at it. Every other payload is blocked out of the search,
   the file size is asserted again after the rewrite, and the new record is read
   back through the tree before the EXE on disk is touched. Nothing is ever
   appended and no section changes size. Only when there is no gap left is the
   edit refused, with the exact byte counts.

The meter under the editor shows both numbers live as you type — the entry's
slot (`used / available`) and the file size (`file N of M bytes`) — and turns
red the moment the embedded file would grow.

Restart BlueStacks after a replacement; the player reads these resources at
startup.

## Gradient themes

BlueStacks paints the sidebar and top bar from `plr.qml`'s `PlrSidebar` /
`PlrTopbar` / `PlrTitleBar` instances, not from the type-level Rectangle
roots. **Auto** — the default — inspects the selected EXE's import table and
reports what it found ("Auto-detected a Qt 6 player → using QML (new).",
"Auto-detected a Qt 5 player → using Hex (old).", …): Qt 6 players get
**QML (new)**, older Qt 5 / MSI 5 players get **Hex (old)**. You can still
pick **QML (new)** or **Hex (old)** manually; on a mismatched choice the QML
method simply loads nothing and the status line explains why.

- **QML (new)** — Theme → Gradients. Gradients can paint every verified
  compatible target: player-window chrome instances (sidebar included), the
  Rectangle backgrounds, and supported button/border slots.
- **Hex (old)** — **Gradient theme**. The same full surface list as the QML
  editor: top bar / title bar / sidebar chrome, backgrounds, buttons and
  every other compatible target. Pick a single surface or **All gradient
  targets** to paint them at once, each with its own two stops and a
  horizontal/vertical direction (a solid color fallback always remains,
  e.g. `gradient: Gradient { … }; color: …`, so a rejected or partially
  compiled gradient never leaves the surface blank; the solid hex colors
  stay editable through "Apply theme").

Qt 5.15 can load the gradient source, but compiled `qv4cdata` units with an
empty source path are invalidated first; the on-disk `<install>/cache/qmlcache`
is also cleared so Qt 6 reloads the edited QML.

Only gradients the UI can actually show are offered: a `property Gradient` inside
`UiThemeObject.qml` is listed when another resource reads it by name, and skipped
(otherwise) with the count reported in the status line. Both players supplied during
development read `UiTheme.colors.` 760 (Qt 5 / MSI 5) and 1123 (Qt 6) times and
`UiTheme.gradients.` never; the old build reaches one gradient at all - a 1 px ring
around a button, through `pColorState.borderGradient` in `UiButton.qml`. Patching a
declaration nobody reads used to report success and change nothing on screen, which is
why the reference check exists.

After loading the QML theme on a Qt 6 player, open **Theme → Gradients**. The
editor discovers verified compatible targets: player-window chrome instances
first, then Rectangle-based sidebar/top-bar backgrounds, the supported
button-background branches, existing gradient bindings, and native properties
typed `Gradient` (including button borders). Choose two `#RRGGBB` or
`#AARRGGBB` stops and a horizontal/vertical direction; the editor shows a live
preview before applying. Surfaces and theme-level gradient definitions are
listed first, and the filter box narrows the list by target name, QML file or
kind. Clear/apply staged solid-color edits first, then close BlueStacks before
writing. On the **Hex (old)** method the gradient editor offers the same full
target list — pick any surface (or the combined entry to paint them all).

A property typed `color` cannot hold a Gradient object. These edits therefore
modify the compatible rendering/Gradient binding while preserving the original
solid color. **Solid fallback** sets the selected gradient to `null`; it does not
restore an old multi-stop gradient definition. Text/icon colors and unsupported
controls remain solid. Button-background gradients intentionally use the same
stops in all supported rendering states; native border slots can be edited
individually. This is not an arbitrary CSS gradient string in a hex-color field.

Writes use validated Qt resource mappings, mapped growth when necessary,
targeted cache refresh and read-back verification, with the first `.bak`
preserved. Unknown/ambiguous layouts and incompatible QtQuick imports are not
modified. The initial editor supports two-stop linear gradients using QtQuick
2.12+ or Qt 6 APIs, not radial/diagonal gradients or every custom control type.
The regression suite renders synthetic horizontal/vertical/solid cases in Qt
and checks actual pixels; it never executes QML extracted from the user's EXE.

## Boot video and integrity

Boot patching plays the selected installation's `Assets/Promo.mp4` without importing any media module into the player's own QML. The tool writes a small player, `Assets/BootVideo.qml`, next to `HD-Player.exe`; that file tries Qt6 Multimedia, then Qt5 Multimedia, then a WebEngine `<video>` at run time, each attempt inside its own `try`, so a missing module costs the video and never the boot window. The player's boot screen (`PlrBootProgressBar.qml`) is found by its content rather than by a fixed resource name and is given one `Loader` pointed at that file through a `file:///` URL; the stock artwork, the promotion banner and the promotion button are hidden, the loading text stays. The player window (`plr.qml`) is taught to keep its boot screen - and so the video - on screen for the configured hold time after boot reports complete: builds that swap components get a timer on the swap, builds that simply unload their boot loader get the unload deferred. Both files are rewritten inside the record they already occupy, so the EXE never grows; when a record is tight its licence banner is dropped to make room. Resource timestamps and the player's QML disk cache are refreshed so Qt recompiles the edited files; the first `.bak` is preserved, the write is read back and verified, and a player whose boot screen cannot be identified is refused instead of guessed. Hold `0` leaves the player's own timing alone.

Integrity patching follows `E8 ???????? 84 C0 74 ??` in `.text`, with bounded RIP-relative references to the verify/fail messages when disambiguation is needed. Natural NOP sequences are not counted as clean candidates. Already-applied patches are verified by a message reference or matching backup evidence. Only the two JZ bytes are NOPed; dry-run is read-only, the first backup is preserved, and writes/restores are verified rather than truncating/deleting the EXE first. Close BlueStacks yourself before patching; the tool does not forcibly terminate your sessions. Full-backup restoration also rolls back theme/boot edits made after that backup.

Theme/boot resource validation also reads supported `qRegisterResourceData` argument sequences and embedded binary-QRC headers. This allows verified noncanonical node ordering/unused nodes without treating arbitrary offsets as resource pointers. Unrelated opaque resource codecs no longer invalidate an otherwise bounded resource tree. The edited resource's bytes, aliases and cache records are still verified. `--theme-probe` includes resource mapping evidence; a screenshot alone cannot identify every unsupported executable layout.

## FPS slider

Supported targets (64-bit x86 Windows DLLs):

- **Qt5QuickTemplates2.dll**
- **Qt6QuickTemplates2.dll**

These contain `QQuickSlider::setTo` / `QQuickRangeSlider::setTo`. **libOpenglRender.dll is not the FPS target.**

**FPS auto-detection is enabled by default.** The app finds BlueStacks/MSI installations under the real Program Files locations and Windows registry paths. When several installations exist, choose one with **Use** on the Dashboard; the app never patches a randomly chosen installation. It reads the selected HD-Player.exe's actual normal/delay imports (and bounded local UI dependencies when necessary) to detect Qt 5 or Qt 6, then selects that installation's matching slider DLL. Actions recheck the target after in-place updates, and stale saved DLL paths are refreshed. If no Qt import evidence is available, a single local DLL version can be selected; two versions or conflicting evidence require a manual choice. No fallback to a different installation or wrong Qt major is allowed.

Use **Auto-detect** on the FPS page to refresh the selection. Disable the automatic-selection checkbox only when deliberately selecting a DLL manually. File-dialog paths are decoded through QUrl, so spaces, Unicode and UNC paths are preserved.

Choose a high-FPS maximum from 60–1440, close BlueStacks, and patch. Only the 240 FPS high-frame-rate maximum is replaced. Normal mode stays at its default **60 FPS**, with no normal-mode setting or rewrite. Other slider values are unchanged. This changes the settings slider's upper limit, not a guarantee of in-game FPS.

A per-DLL `.bak` supports restoring the original. Unsupported setter code and backups from a different DLL version are refused. Older high/normal-mode detours (including the `read it.txt` reference) and previous max-only patches can be upgraded using their matching clean backup.

The patcher decodes complete supported x64 entry instructions instead of requiring one exact five-byte prologue. It supports common MSVC/MinGW stack-save/frame variants, relocates RIP-relative operands, and preserves CET landing instructions. Compiler entry jumps are followed only when their destination is a verified PE runtime-function entry; unknown third-party detours are not overwritten.

If Analyze reports a **recognized partial FPS patch**, keep the matching clean `.bak` and click **Patch FPS**. Both setters are rebuilt together; no unknown setter is silently skipped. If a build is still unsupported, the selectable State message includes the exact setter, RVA and entry bytes. Copy that message and provide the affected DLL and `.bak` for diagnosis—do not delete the backup to bypass the safety check.

## Discord Rich Presence ID utility

The **Discord Rich Presence** page and the `--rpcid` command line are both
served by the engines compiled into `BSI-Customizer.exe`. Nothing launches an
interpreter, loads a sidecar script, or needs a separately installed
component: the single EXE is the whole tool. They detect the Game SDK's
little-endian 64-bit integer form (at any file alignment, including qwords
referenced by code), and the ASCII and UTF-16 forms used by classic
`discord-rpc`, and preserve the original file size when replacing an ID.
Detection does not require a specific Application ID: every build - not only
the stock BlueStacks one - is found by the same evidence rules (code
immediates, text, repeated stored constants). The stock BlueStacks ID
(`821361671729709086`) is additionally recognized outright whenever it turns
up, the same way the reference scanner always finds it, so it is never held
back by a coin-flip tie against unrelated noise elsewhere in a large
executable. Junk-shaped numbers (round values, repeated digit patterns, parts
of longer numbers) are filtered out, and an undocumented candidate is only
acted on automatically when the evidence is unambiguous, so the detector does
not report a wrong ID on builds that used to be mis-read; an ambiguous scan
asks for the current ID instead of touching the file. Close the emulator
before writing; the first backup is kept as `<file>.rpcid-bak`.

32-bit builds are supported too: a 64-bit ID that a 32-bit compiler cannot
load as one immediate is pushed or moved as two separate 32-bit halves, and
that pair is harvested back into a single Application ID and patched exactly
like a 64-bit one.

Relocating an already-patched slot does not depend only on remembering what
value was last written there. Right after a successful patch, the unchanging
bytes immediately around that exact slot are captured as a small signature
and saved next to the usual patch state. A later run tries, in order: the
remembered current ID; that remembered signature (so the slot is found again
even if its value was since changed by something else, as long as the
surrounding code did not); a small table of signatures for specific
documented builds, usable even on a file that has never been patched before;
and, as a last resort, whatever short run of bytes differs between the file
and its own `.rpcid-bak`. Only if none of those apply does it fall back to
the full evidence-based scan described above. A `find` scan additionally
looks for an Application ID stored inside a zlib-compressed blob embedded in
the file (a packed resource, for instance); that is reported for information
only; it is never selected automatically or written to, since patching
compressed bytes in place would just corrupt the stream. All of this runs
fully offline — no network request is ever made to look an ID up.

```powershell
BSI-Customizer.exe --rpcid selftest
BSI-Customizer.exe --rpcid find "C:\Program Files\BlueStacks_nxt\HD-Player.exe"
BSI-Customizer.exe --rpcid replace HD-Player.exe --new 1234567890123456789 --yes
BSI-Customizer.exe --rpcid undo HD-Player.exe
BSI-Customizer.exe --rpcid status HD-Player.exe --json
BSI-Customizer.exe --rpcid name HD-Player.exe --set "My Player" --yes
```

The customizer is a GUI application, so a console command prints into the
terminal it was started from and never stops to ask a question there: writing
commands require `--yes`, and without it they print the same preview
`--dry-run` would. Exit codes are 0 (done), 1 (the operation failed) and 2
(the command line itself is wrong). Everything runs offline.

Re-patching is supported: every replace records the IDs it has written in
`<file>.rpcid.json`, so the next replace targets the current ID exactly
instead of re-guessing from a heuristic scan. The UI and the command line
share that state file and the `.rpcid-bak` backup, so a file patched in one
can be re-patched or undone from the other. The sites are always re-derived
from the bytes on disk and the result is verified by reading the written
offsets back, so the same executable can be re-pointed to a new Application ID
any number of times. If a stored copy sits in a slot too small for the new ID
(a longer decimal string has nowhere to grow), that copy is skipped, reported
as a leftover in `status` (and as an "incomplete" badge in the UI), and
repaired automatically by the next patch it fits — instead of silently
leaving the old ID behind. `undo` restores the backup and clears the recorded
state.

`replace --dry-run` previews changes, and `--old <id>` selects a specific
candidate when a binary contains more than one application ID (it must be the
full 17-20 digit ID, and it must actually be present in the file).
The **Windows name** card on the same page, and `name --set` on the command
line, edit the Windows version resource (`FileDescription` and `ProductName`
by default, or a single `--key`) — the text Explorer, Task Manager and most
overlays show for the executable. Every translation in the file is updated
inside the slot the compiler reserved, so the resource tree and the file size
stay valid, and a text that does not fit is refused with the exact limit
instead of corrupting the resource; the card lists each name with its limit
and will not let you type past the shortest one. The original is kept as the
same `.rpcid-bak` backup, so **Undo** restores it. It does not rename the
Discord application itself, which must still be changed in the Discord
Developer Portal. `--rpcid selftest` runs the complete engine self-test, the
same one CI runs.

## Self-update through Discord

The repository is private, so GitHub Releases cannot be the update channel: a
shipped EXE has no public endpoint to ask, and a GitHub token cannot travel
inside the file. The updater therefore uses a **Discord server**: the publisher
(`updater-bot/publish.mjs`) uploads the portable EXE plus a small
`manifest.json` to one channel, and the app reads the newest manifest with a
**read-only bot token** limited to that channel.

- **Publishing is a message.** Post the version and the download link in a
  private channel (`BSI Customizer v2.0.2` + the pCloud link) and every copy
  picks it up — no tool, no bot process, nothing to run. A `manifest.json`
  attachment (what `updater-bot/publish.mjs` writes) is the strict alternative
  and takes precedence when present.
- `UpdateChecker` (`src/core/UpdateChecker.{h,cpp}`) checks a few seconds after
  the window opens (switchable on the About page) and shows a strip under the
  title bar when a newer `version` is announced: **What's new** / **Update now**
  / **Later**.
- The download is written to `%TEMP%` and verified before anything is replaced:
  exact byte size, SHA-256 and a 64-bit PE check. Any mismatch deletes the file.
- **Install and restart** hands the swap to a hidden PowerShell helper that
  waits for the app to exit, copies the verified EXE over it, starts it again
  and deletes itself. Nothing outside `%TEMP%` is touched before that.
- The manifest can be HMAC-SHA256 signed (`updater.hmacKey`), in which case an
  unsigned manifest is refused.
- `url` lets the EXE live on any https host (pCloud, Drive, S3) when it is
  larger than the server's 10 MB upload limit. Landing pages such as pCloud's
  share links are followed to the real download link, and the publisher
  verifies the hosted file's size + SHA-256 before it posts anything.
- Configuration is the `updater` object of the same `auth.json` the sign-in gate
  uses, so one `ZLP_AUTH_JSON` secret configures both. `BSI_NO_UPDATER=1`
  disables it.

Full setup (bot permissions, channel, secrets, publishing, troubleshooting):
[`docs/UPDATER.md`](docs/UPDATER.md) and
[`updater-bot/README.md`](updater-bot/README.md). The `updater` block can be
baked into the EXE from the `ZLP_AUTH_JSON` secret, so users need no sidecar
file — see *Release checklist* in that document.

Verify a channel without opening the window:

```powershell
.\BSI-Customizer.exe --check-updates --json
# 0 = up to date, 3 = update available, 4 = downloaded and verified (--download),
# 1 = error, 2 = no updater config

.\BSI-Customizer.exe --check-updates --explain
# the same check, plus what the bot received from the channel, message by message
```

If the channel clearly has the release but the updater "sees nothing", run the
`--explain` form: when every message comes back with no text and no attachments,
Discord is withholding them because the app does not hold the **Message Content
intent** — a privileged per-app switch in the Developer Portal. See
[Troubleshooting](docs/UPDATER.md#troubleshooting) for the three fixes (enable
the intent, mention the bot in the message, or let `publish.mjs` post it).

## Build and verify

Requires CMake 3.21+, C++17, and Qt 6.5+ with Core, Gui, Qml, Quick, QuickControls2, and Svg. **Qt 6 builds the customizer UI; it does not limit FPS patching to Qt 6 installations. Qt 5 and Qt 6 target DLLs use the same patching engine.**

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

Run the built executable with `--selftest`. On Windows with the Visual Studio generator:

```powershell
$env:QT_QPA_PLATFORM = "offscreen"
Start-Process build/Release/BSI-Customizer.exe -ArgumentList '--selftest' -Wait
```

The self-test covers resource detection/relocation, actual Qt QML loading in separate processes with disk caching enabled, repeated patch–launch cycles, embedded cache invalidation, palette parsing, and both FPS DLLs with MSVC/MinGW export names. On Windows x64 it executes controlled setter fixtures to verify that only 240 FPS is replaced while 60 FPS and other values (including NaN) pass unchanged. It also tests multiple compiler prologue styles, instruction boundaries, observable RIP-relative relocation, verified entry thunks, partial-patch recovery, legacy upgrades, repatching and exact restoration. Windows CI requires successful patch/repatch/restore checks on real **Qt 5.15.2** and **Qt 6.8.3** DLL copies, as well as the full Qt5/Qt6 × MSVC/MinGW fixture matrix. Neither modified DLL is loaded, and both originals remain untouched. No installed BlueStacks instance is exercised. GitHub CI runs the Windows build and self-test on pull requests.

The Qt 5 test reference is extracted from the official PySide2 5.15.2.1 Windows x64 wheel — a plain ZIP archive that happens to contain a genuine `Qt5QuickTemplates2.dll` — with pinned SHA-256 checks for both the archive and the DLL. Nothing from it is installed, imported or executed, and no interpreter takes part in the build. It stays in the ignored build directory and is never installed, linked, executed, committed, or bundled with the app. `BSI_TEST_QT5_REFERENCE` defaults ON in Windows GitHub CI and OFF for ordinary/offline builds. To run the same real-Qt5 check locally, configure with `-DBSI_TEST_QT5_REFERENCE=ON`. Alternatively, an ordinary build's `--selftest` accepts `BSI_FPS_QT5_DLL` pointing to a clean `Qt5QuickTemplates2.dll`; only a temporary copy is patched.

The packaging step's licence/attribution collector is a CMake script with its
own offline self-test (no network, no Qt, no interpreter):

```sh
cmake -P cmake/tests/CollectQtNoticesTest.cmake
```

For an unsupported real EXE, collect a read-only diagnostic report:

```powershell
.\BSI-Customizer.exe --theme-probe "C:\Program Files\BlueStacks_nxt\HD-Player.exe"
```

## Third-party notices

The restricted x64 prologue decoder uses [HDE64 from MinHook v1.3.4](src/third_party/hde64/README.md), under its BSD license. The notice below is included in the README shipped with binary releases. CMake builds and installations also include `THIRD-PARTY-NOTICES.txt`.

<details>
<summary>HDE64 / MinHook BSD license notices</summary>

```text
﻿MinHook - The Minimalistic API Hooking Library for x64/x86
Copyright (C) 2009-2017 Tsuda Kageyu.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

================================================================================
Portions of this software are Copyright (c) 2008-2009, Vyacheslav Patkov.
================================================================================
Hacker Disassembler Engine 32 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

-------------------------------------------------------------------------------
Hacker Disassembler Engine 64 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

</details>

A diagnostic EXE can be supplied with `-DBSI_EXE_TEST_FILE=<absolute-path>`. In GitHub CI, a user-supplied `HD-Player.exe` at the repository root is also recognized as optional diagnostic input. Self-tests only read it and patch disposable copies; they do not execute its native code or QML. The probe records its SHA-256 so results from different installations/builds are not confused. Diagnostic input is not a runtime dependency of the portable EXE.

Compatibility is version-adaptive, not a guarantee for every past/future build: FPS detours currently support Windows x64 Qt 5/Qt 6 setters, and boot rewriting requires a recognizable boot-controller/media API with a validated resource mapping. Unknown architectures, ambiguous runtime selection and unrecognized resource layouts are refused instead of reporting a false success. Integrity behavior is not changed by these detection improvements.
