# Self-update through a Discord bot

## The easy way: post the version and the link

There is nothing to install, run or remember. Keep one private channel in your
server and **write a message there**:

```
BSI Customizer v2.0.2
https://u.pcloud.link/publink/show?code=XXXXXXXX
```

That is the whole release. Every copy of the app reads that channel a few
seconds after it starts, sees `2.0.2` is newer than what it runs, offers the
update, downloads it from your pCloud link, checks that the result really is a
64-bit Windows program, replaces itself and restarts.

You can be as tidy or as chatty as you like — the message is parsed, not
matched byte for byte:

```
**BSI Customizer v2.0.2**
https://u.pcloud.link/publink/show?code=XXXXXXXX

Fixed the About editor.
Link names can be edited separately now.
```

What the app looks for:

| in the message | meaning |
| --- | --- |
| `v2.0.2`, `2.0.2`, `version 2.0.2`, `BSI 2.0.2`, or a line that is only `v2.0.2` | the version |
| the **first https link** (preferring one that ends in `.exe` or lives on a file host) | where to download |
| `sha256: <64 hex>` *(optional)* | pins the file; without it the fingerprint is recorded on first download |
| `size: 12345678` *(optional)* | pins the size too |
| everything else | the "What's new" text |

Rules that keep it predictable:

* the **highest version** in the last 50 messages wins, so re-posting a fixed
  link for the same version replaces the old one;
* `http://` links, GIFs (tenor/giphy) and picture links are ignored;
* if the channel also has a `manifest.json` attachment (the strict form below),
  that one wins — use one style per channel;
* if `updater.hmacKey` is set, text messages are **not** accepted (they cannot
  be signed); post manifests instead.

Without a `sha256:` line the download cannot be checked against a known hash
*before* it arrives — it is checked against size + "is this a real 64-bit
program", and its fingerprint is then stored for that version, so the same
version can never quietly become different bytes later. The About page shows
which of the two you are on (*signed* / *hash-pinned* / *no SHA-256 yet*).

Nothing has to be running: the bot is not a program you host, it is a token the
app uses to **read** that channel (View Channel + Read Message History). The
same bot you use for the sign-in gate works — it only ever reads.

---

The repository is private, so GitHub Releases cannot be the update channel:
a released EXE has no public endpoint to ask *"is there a newer build?"*, and a
GitHub token cannot be baked into a file the user can read. The updater
therefore uses a **Discord server** as the delivery channel:

```
  your PC / CI                                    User's PC
+-------------------------+              +-----------------------------+
| updater-bot/publish.mjs |   upload     | BSI Customizer              |
|  - SHA-256 of the EXE   | -----------> |  UpdateChecker (C++)        |
|  - manifest.json        |  manifest +  |  1. GET channel messages    |
|  - signs it (optional)  |  EXE         |  2. newest manifest.json    |
+-------------------------+              |  3. compare versions        |
            |                            |  4. download + verify       |
            v                            |  5. swap EXE, restart       |
   #bsi-releases (Discord channel) <---- +-----------------------------+
```

Read side = a **bot token** with *View Channel* + *Read Message History* on one
channel. Publish side = the same bot (or a channel **webhook**, whose URL stays
in CI secrets only). No OAuth user token is involved: a user token cannot read
channel history, a bot token can.

---

## 1. What the app does

`UpdateChecker` (`src/core/UpdateChecker.{h,cpp}`) is created in `main()` next to
`AuthManager`, exposed to QML as `updates`, and starts a check **4 s after the
window opens** (switchable on the About page, stored in
`QSettings updates/autoCheck`). It does nothing at all when no updater config is
present.

1. `GET {apiBase}/channels/{channelId}/messages?limit=50` with
   `Authorization: Bot <token>`.
2. Takes the release out of that list: a message carrying a `manifest.json`
   attachment when there is one, otherwise the message that names the highest
   version together with an https link (the easy way above).
3. Validates it (schema/version, file name, and size + SHA-256 when the release
   provides them, plus the optional HMAC signature) and compares `version` with
   `BSI_VERSION` (the CMake project version).
4. Newer version → a strip appears under the title bar:
   *Update 2.0.1 is available* → **What's new** / **Update now** / **Later**.
5. **Update now** streams the EXE to `%TEMP%\BSI-Customizer-update\`, hashing as
   it goes, then verifies:
   - byte size equals the manifest `size`,
   - SHA-256 equals the manifest `sha256`,
   - the file is a 64-bit Windows program (`MZ`, `PE\0\0`, machine `0x8664`,
     PE32+ optional header).
   Any mismatch deletes the file and nothing is installed.
6. **Install and restart** writes a hidden PowerShell helper that waits for the
   app's PID to exit, copies the verified EXE over the running one with retries,
   starts it again and deletes itself. A log ends up in
   `%TEMP%\BSI-Customizer-update\apply-update.log` if the copy never succeeds.
7. The app cannot overwrite itself when it is not allowed to (for example an
   install under `Program Files` without rights): the write test fails *before*
   anything is replaced and the UI says so, offering *Show the downloaded file*
   for a manual copy.

Nothing is written outside `%TEMP%` until the user presses **Install and
restart**, and the running EXE is only touched by the helper after the app has
closed.

### "Later"

**Later** stores `updates/skippedVersion`. That exact version stays hidden until
a newer one is published — unless the manifest sets `minSupported` above the
installed version, or `mandatory: true`. `unskip()`/the About page clears it.

### Verifying without the GUI

```
BSI-Customizer.exe --check-updates            # 0 = up to date, 3 = update, 1 = error, 2 = not configured
BSI-Customizer.exe --check-updates --json     # machine-readable
BSI-Customizer.exe --check-updates --download # also fetch + verify it (4)
```

The probe reads the same config as the window, so it is the fastest way to check
a token, a channel ID or a freshly published manifest.

---

## 2. Configuration

The updater block lives in the **same `auth.json`** the sign-in gate uses (see
`docs/AUTH.md`), so one `ZLP_AUTH_JSON` secret configures both and one embedded
blob ships inside the EXE:

```json
{
  "discord": { "...": "unchanged sign-in settings" },
  "updater": {
    "apiBase": "https://discord.com/api/v10",
    "channelId": "1234567890123456789",
    "botToken": "READ-ONLY-BOT-TOKEN",
    "manifest": "manifest.json",
    "hmacKey": "",
    "enabled": true
  }
}
```

Source order (identical to `AuthManager`):

1. `$ZLP_AUTH_JSON` (alias `$BSIC_AUTH_JSON`) — the blob CI bakes in;
2. `$BSIC_AUTH_FILE`, then `auth.json` next to the EXE, then the app-data copy;
3. the blob embedded at build time into `:/bsic_auth/auth.json`.

Per-field environment overrides:
`BSIC_UPDATE_CHANNEL_ID`, `BSIC_UPDATE_TOKEN`, `BSIC_UPDATE_API`,
`BSIC_UPDATE_MANIFEST`, `BSIC_UPDATE_HMAC_KEY`. `BSI_NO_UPDATER=1` disables the
feature (CI and the deployment test set it).

`updater.enabled: false` also disables it, for a build that must never phone
home.

---

## 3. Manifest schema (version 1)

The strict form. When the channel has no `manifest.json` attachment, a plain
message is used instead (see *The easy way* at the top).

`manifest.json` is an attachment of the release message:

```json
{
  "schema": 1,
  "app": "BSI Customizer",
  "version": "2.0.1",
  "file": "BSI-Customizer-2.0.1-win64.exe",
  "size": 18392064,
  "sha256": "4de96eb5f3d5260c89e2dc2f21c2076be68ba4511e8399ce695bdf542a8354ac",
  "released": "2026-10-24T12:00:00Z",
  "notes": "Markdown release notes, plain text is fine.",
  "mandatory": false,
  "minSupported": "1.9.0",
  "url": "https://example.com/BSI-Customizer-2.0.1-win64.exe",
  "signature": "base64 HMAC-SHA256"
}
```

| field | required | meaning |
| --- | --- | --- |
| `schema` | yes | `1` — the app refuses anything else |
| `version` | yes | `2.0.1` (a leading `v` is tolerated); compared with `BSI_VERSION` |
| `file` | yes | bare file name ending in `.exe`; also the attachment name |
| `size` | yes | exact byte count |
| `sha256` | yes | 64 lower-case hex characters |
| `notes` | no | shown in the update strip |
| `released` | no | informational |
| `mandatory` | no | always offered, "Later" does not hide it |
| `minSupported` | no | builds older than this must update |
| `url` | no | where the EXE actually lives (pCloud, Drive, S3, any https host); when absent the app uses the `file` attachment **of the same message**. A landing page is followed to the real file (see section 8) |
| `signature` | when `hmacKey` is set | base64 HMAC-SHA256 (see below) |

The **signature** covers exactly:

```
bsi-update-v1\n
<version>\n
<file>\n
<size>\n
<sha256>\n
```

keyed with `updater.hmacKey` (`BSI_UPDATE_HMAC_KEY` in the publisher). If the key
is configured the app **rejects an unsigned manifest**; if it is not, the channel
itself is the trust boundary. `publish.mjs` computes the same bytes, so the two
implementations are interchangeable (there is a known-answer test for this in
`src/patch_regression_test.cpp`).

`url` makes the manifest independent of Discord's upload limit: publish only the
manifest and point `url` at any https host. The size + SHA-256 checks are
unchanged.

---

## 4. One-time Discord setup

1. **Create the bot — or reuse the login one.** The sign-in gate already has a
   Discord *application*; open that same application in
   <https://discord.com/developers/applications> → **Bot** → *Reset Token* →
   copy it. (If the application has no Bot tab yet, *Add Bot* first.) Turn
   **Public Bot** off. No privileged intents are needed: the app reads the
   message text and attachments without the *Message Content* intent, and the
   bot token is only ever used to read one channel.

   A **user** OAuth login (what the sign-in gate uses) can never read channel
   history — only a bot token can. Reusing the same application keeps it to one
   Discord app; a second app works just as well.

   **"Private application cannot have a default authorization link"** — Discord
   refuses to save *Public Bot = off* while the app still offers its default
   install link. Fix it in the same application: **Installation** tab →
   *Install Link* = **None** → Save, then **Bot** tab → *Public Bot* off → Save.
   The hand-written invite URL below keeps working with Install Link = None.
   If the sign-in gate stops working after that (it uses OAuth2, not the install
   link), either set the install link back to *Discord Provided Link* and leave
   *Public Bot* on — a public bot whose token only reads one channel of your
   server is not a leak of anything — or keep the login app untouched and make a
   **second application** for the updater.
2. **Create a channel** in your server, e.g. `#bsi-releases`, and restrict it to
   the members who should see builds. Copy its ID (Discord → *Settings →
   Advanced → Developer Mode*, then right-click the channel → *Copy Channel ID*).
   If the channel is **private**, add the bot to it explicitly
   (*Edit Channel → Permissions → Add members → the bot*) — a private channel
   hides itself from bots that are not listed.
3. **Invite the bot** with only these permissions in that channel:

   | permission | bit |
   | --- | --- |
   | View Channel | 1024 |
   | Read Message History | 65536 |
   | Send Messages (publisher only) | 2048 |
   | Attach Files (publisher only) | 32768 |
   | Manage Messages (publisher only, to delete the old message) | 8192 |

   ```
   https://discord.com/oauth2/authorize?client_id=<APP_ID>&scope=bot&permissions=109568
   ```

   `109568` = all five. A build that only *reads* needs `66560`
   (View Channel + Read Message History).
   Verify the whole thing without publishing anything (read-only, changes
   nothing on Discord):

   ```bash
   DISCORD_BOT_TOKEN=... node updater-bot/setup.mjs --channel <channelId>
   ```

   It prints the bot, server and channel it resolved, whether the app can read
   them, whether publishing will work, and the exact `auth.json` block to paste.
   Exit code 0 means the updater will work from that channel.

5. **Put the values in the app's config** — `updater.channelId` and
   `updater.botToken` in `auth.json` (or the `ZLP_AUTH_JSON` secret, so every
   build carries them).
6. **Give CI the publisher credentials** (repository → *Settings → Secrets and
   variables → Actions*):

   | secret | used by |
   | --- | --- |
   | `DISCORD_BOT_TOKEN` | `publish.mjs` in `release.yml` |
   | `DISCORD_CHANNEL_ID` | `publish.mjs` |
   | `BSI_UPDATE_HMAC_KEY` | optional signing, must match `updater.hmacKey` |

   (`DISCORD_PUBLISH_TOKEN` and `DISCORD_WEBHOOK_URL` are accepted as
   alternatives; a webhook needs no bot at all to publish.)

Banking on one bot for both roles is fine **if** the bot exists only for this
server: a token found in the EXE can post *only* to the channel where the bot
has Send Messages. For stricter setups use a second bot just for publishing and
leave `updater.botToken` read-only.

---

## 5. Publishing a build

> Remember the easy way at the top: if you only want to publish, **posting a
> message in the channel is enough** and needs none of this.

```bash
# once, locally
cp updater-bot/config.example.json updater-bot/config.json   # then fill it in

# every release, from the built EXE
node updater-bot/publish.mjs \
     --exe build/artifacts/BSI-Customizer-2.0.1-win64.exe \
     --version 2.0.1 \
     --notes-file notes.md

# or in one line, when the EXE is already uploaded and you want a pinned release
node updater-bot/publish.mjs --version 2.0.1 --url "https://u.pcloud.link/publink/show?code=XXXX"
```

The script prints the version, size, SHA-256 and channel, uploads
`manifest.json` + the EXE, pins the message, and deletes the previous release
message so the channel always shows exactly one.

`--dry-run` prints the manifest without touching Discord. `--url https://…`
publishes only the manifest and lets the EXE live anywhere (use it when the file
is bigger than the server's upload limit — 10 MB on unboosted servers; Discord
answers HTTP 413 otherwise).

### Automatic on a release tag

`.github/workflows/release.yml` runs the publisher right after the tested EXE is
attached to the GitHub release (tags `v*`, or a manual run with
*publish_release*). It passes the generated release body as `--notes-file`, so
the Discord message and the GitHub release say the same thing. If the Discord
secrets are missing the step is skipped with a notice — the build itself never
depends on Discord.

### Without CI

`updater-bot/README.md` has the same steps with the exact commands, including
the webhook-only variant.

---

## 6. When the EXE is too big for Discord (pCloud and other hosts)

A free Discord server allows 10 MB per file; the portable EXE is ~30 MB. You
have two options, and the second is the one that needs no server boost:

1. **Boost the server** (50 MB) and upload straight to Discord — nothing else
   to do, the manifest needs no `url`.
2. **Host the EXE elsewhere** and let the manifest point at it. The app
   verifies the download against `size` + `sha256`, so a plain file host is
   enough; it does not need to be a "release" system.

### pCloud, step by step

pCloud has two kinds of link:

| Link | What the app gets | Works? |
| --- | --- | --- |
| **Direct link** (Public folder, `Share > Direct link`) | the file itself | yes, immediately |
| **Share link** (`https://u.pcloud.link/publink/show?code=...`) | a web page with a Download button | yes — the app reads the page and follows the real download link inside it |

The Public folder is a Premium feature; a free account only gives share links.
That is fine: `UpdateChecker` detects the web page, pulls the `downloadlink`
out of it (the same URL the Download button uses) and continues. If pCloud ever
changes that page, the publisher catches it first — see below.

> **Traffic limit.** pCloud share/direct links have a traffic and download
> budget (per link). A 30 MB build downloaded by 100 people is 3 GB. If the link
> runs out, pCloud answers `7005 this link has reached its traffic limit` and
> downloads fail until you publish a **new** link. Keep an eye on it for large
> audiences; a boosted Discord channel, GitHub Releases on a public mirror repo
> or object storage (R2/B2/S3) are unlimited alternatives.

### Publishing with a hosted URL

```bash
# 1. upload BSI-Customizer-2.0.1-win64.exe to pCloud, create a share (or direct) link
# 2. publish ONLY the manifest:
node updater-bot/publish.mjs \
     --exe build/artifacts/BSI-Customizer-2.0.1-win64.exe \
     --url "https://u.pcloud.link/publink/show?code=XXXX" \
     --notes-file notes.md
```

Before it posts anything, the publisher **downloads the hosted file** (following
a landing page if there is one) and compares it with the local EXE:

```
  127.0.0.1:8932 answered with a page, following it to 127.0.0.1:8932
  hosted   https://p-def2.pcloud.com/cfZ.../BSI-Customizer-2.0.1-win64.exe
  verified 30.51 MB, SHA-256 matches the local EXE
```

* wrong file / truncated upload → **refused**, with both hashes printed;
* page without a download link (a folder link, a password-protected link) →
  **refused** with a hint;
* `--skip-verify` posts anyway (only use it when you cannot download on the
  machine you publish from — then check with `--check-updates --download`).

The `url` may also be added/edited by hand in `manifest.json`; it is covered by
the signature when `hmacKey` is set, so it cannot be swapped afterwards either.

### What the user's app does with it

1. reads `url` from the manifest (or the attachment when it is absent);
2. follows at most **two** landings — https only — by reading the download link
   off the page;
3. streams the result into `%TEMP%` and checks size + SHA-256 + 64-bit PE;
4. installs exactly like a Discord-attached build.

Nothing about the swap changes, so all the safety rules above still apply.

## 7. Troubleshooting

The app never says "update failed" without a reason; `--check-updates` prints the
same text in a terminal.

| message | cause |
| --- | --- |
| `Discord rejected the updater token (401)` | token reset in the portal, or `updater.botToken` still has the `Bot ` prefix / is the *client secret* by mistake |
| `The updater bot cannot read the releases channel (403)` | channel overwrites: give the bot View Channel + Read Message History |
| `The releases channel was not found (404)` | wrong channel ID, or the bot is not in that server |
| `Discord is rate-limiting the updater (429)` | too many checks; wait a minute |
| `No release in the last 50 messages of channel …` | nothing published yet, or the message has no `v2.0.x` and no https link — check the two lines |
| `the message does not name a version` | the message had a link but no `v2.0.x` |
| `the message has no download link` | the version is there but no https URL |
| `the download link must start with https://` | `http://` links are refused on purpose |
| `The update manifest is invalid: …` | someone edited `manifest.json` by hand; republish |
| `The update manifest is not signed by this build's key` | `BSI_UPDATE_HMAC_KEY` and `updater.hmacKey` differ |
| `SHA-256 mismatch` | the upload was truncated, or the host served a different file — republish |
| `The link opened a web page instead of the file` | the `url` is a share/landing page with no download link on it (pCloud folder link, password-protected link). Use a **direct link**, or a link whose page offers Download |
| pCloud `7005` / `7006` in the browser | that link hit its traffic/download limit — create a new link and republish |
| `Windows does not let this app write to …` | install lives under `Program Files`; run the app as administrator |
| nothing in the UI at all | `--check-updates` — if that is happy, the app is already current |
| portal: `Private application cannot have a default authorization link` | *Installation* tab → **Install Link = None** → Save; then turn *Public Bot* off |

The helper's log: `%TEMP%\BSI-Customizer-update\apply-update.log`.

---

## 8. Security summary

* The shipped token is **read-only** on **one channel**; it cannot read the
  server's other channels, cannot send, and cannot see message *content* of
  channels it is not in.
* Every download is pinned by size + SHA-256 from the manifest, and must look
  like a 64-bit PE before the swap.
* Optional HMAC signing authenticates the manifest itself, so a leaked read-only
  token is not enough to point the updater at a different EXE.
* Only https URLs are accepted; `file://`, http and non-CDN schemes are refused.
* Nothing is written outside `%TEMP%` before the user presses **Install and
  restart**; a failed verification deletes the file.
* A plain "version + link" message cannot carry a signature. If you set
  `updater.hmacKey`, text releases are refused and only signed manifests are
  accepted; without a hash the first download is fingerprinted and pinned.
* `BSI_NO_UPDATER=1` and `updater.enabled: false` turn the whole feature off.
