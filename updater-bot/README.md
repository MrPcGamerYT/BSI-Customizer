# updater-bot — publish builds to Discord

The application updates itself from a Discord channel (see
[`docs/UPDATER.md`](../docs/UPDATER.md)).

**You do not need anything in this folder to publish a build.** Post a message
in the channel instead:

```
BSI Customizer v2.0.2
https://u.pcloud.link/publink/show?code=XXXXXXXX
```

Everything below is the optional strict form (a pinned/signed `manifest.json`,
automation, CI).

```
node updater-bot/publish.mjs --exe build/artifacts/BSI-Customizer-2.0.1-win64.exe \
     --version 2.0.1 --notes-file notes.md
```

It uploads `manifest.json` + the EXE, pins the message and deletes the previous
one, so the channel always has exactly one "latest build" message. The app
finds it by looking for a `manifest.json` attachment in the newest messages.

## Setup (5 minutes)

1. **Application + bot**
   <https://discord.com/developers/applications> → *New Application* →
   *Bot* → *Reset Token* → copy the token. Switch **Public Bot** off.
   *(No privileged intents needed.)*
2. **Channel** — create `#bsi-releases`, copy its ID
   (*Settings → Advanced → Developer Mode* on, right-click → *Copy Channel ID*).
3. **Invite the bot** to that channel with View Channel, Read Message History,
   Send Messages, Attach Files and Manage Messages:
   `https://discord.com/oauth2/authorize?client_id=<APP_ID>&scope=bot&permissions=109568`
   A build that only reads needs `66560`.
4. **Tell the app** — `updater.channelId` + `updater.botToken` in `auth.json`
   (or the `ZLP_AUTH_JSON` secret). Read-only token is enough for the app; the
   publisher needs the send/attach/manage bits.
5. **Check it before publishing anything**
   ```bash
   node updater-bot/setup.mjs --auth-file auth.json
   ```
   Reads the `updater` block out of the file, lints it the way the app does
   (notes markdown-mangled values, a missing block, ignored keys), then checks
   the bot, server, channel and permissions live. Read-only; it prints the exact
   block to paste. Exit code 0 means the updater will work from that channel.
   `--channel`/`--token`/`DISCORD_BOT_TOKEN` work instead of a file.
6. **Configure this script**
   ```bash
   cp updater-bot/config.example.json updater-bot/config.json
   # channelId + botToken (+ optional hmacKey), then:
   node updater-bot/publish.mjs --help
   ```

Environment variables work instead of `config.json` (CI uses this):
`DISCORD_CHANNEL_ID`, `DISCORD_BOT_TOKEN` (or `DISCORD_PUBLISH_TOKEN`),
`DISCORD_WEBHOOK_URL`, `BSI_UPDATE_HMAC_KEY`, `DISCORD_API_BASE`.

## Commands

| command | effect |
| --- | --- |
| `--exe <file>` | the portable EXE (required) |
| `--version 2.0.1` | default: read from the file name |
| `--notes-file notes.md` | release notes shown in the app's update strip |
| `--min-supported 2.0.0` | older builds must update even after "Later" |
| `--mandatory` | mark it required |
| `--url https://…` | publish only the manifest; the EXE lives at that link (downloaded and checked before posting) |
| `--skip-verify` | do not download `--url` to check it (offline publishing) |
| `--dry-run` | print the manifest, send nothing (works without credentials) |
| `--channel <id>` / `--manifest <name>` | one-off overrides |

## Verify from the user side

```bash
BSI-Customizer.exe --check-updates --json
```

`0` = up to date, `3` = update available, `4` = downloaded and verified
(`--download`), `1` = error, `2` = the app has no updater config.

## Webhook instead of a bot token

A channel webhook can post without a bot. Set `DISCORD_WEBHOOK_URL`
(`https://discord.com/api/webhooks/<id>/<token>`) and the script uses it; the
last message id is kept in `updater-bot/.state.json` so the previous message can
be removed. Webhooks cannot pin, and CI runners do not keep `.state.json`, so
the bot-token route is the recommended one.

## pCloud / Google Drive / any https host

The manifest may carry `url` instead of attaching the EXE. `publish.mjs`
downloads that URL first, follows a landing page if there is one (pCloud share
links are pages, not files) and refuses to publish unless the hosted file has
the same size and SHA-256 as the local EXE:

```bash
node updater-bot/publish.mjs \
     --exe build/artifacts/BSI-Customizer-2.0.1-win64.exe \
     --url "https://u.pcloud.link/publink/show?code=XXXX" \
     --notes-file notes.md
```

Google Drive links work too, but the file must be "Anyone with the link" and
large files hit Drive's virus-scan page — prefer a plain host. pCloud's
direct links from the **Public folder** (`Share > Direct link`) download
straight away and need no page hop at all.

## Upload limits

Unboosted servers cap uploads at 10 MB (HTTP 413 otherwise). If the portable
EXE is larger, host it anywhere https and publish the manifest only:

```bash
node updater-bot/publish.mjs --exe build/artifacts/BSI-Customizer-2.0.1-win64.exe \
     --url https://your-host/BSI-Customizer-2.0.1-win64.exe
```

`size` + `sha256` in the manifest still pin the download, so a plain file host or
object storage is enough.

## Files

| file | purpose |
| --- | --- |
| `publish.mjs` | the publisher (Node 18+) |
| `setup.mjs` | read-only setup check: token, server, channel, permissions |
| `config.example.json` | template for `config.json` (git-ignored) |
| `.state.json` | last webhook message id (git-ignored, webhook mode only) |
