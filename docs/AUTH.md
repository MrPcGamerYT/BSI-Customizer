# Dev Sign-in Gate (Discord only)

Port of the Premium-Optimizer (FalcoForge) login system - same architecture,
one way in: **Discord**. This tool is private dev software, so the login page
offers a single option and access follows either a **dev allowlist** of
Discord user IDs or a **Discord server role gate** (the same `guildId` +
`roles` + `requiredTier` shape Premium-Optimizer's auth.json uses).

## Rule

**The login card is UX only.** Authority is `AuthManager` + the guards in
`Backend`: while the Discord sign-in is not verified, every action in the UI
refuses to run (`Sign in with Discord to unlock BSI Customizer.`). The
command-line tools (`--selftest`, `--rpcid`, `--theme-probe`) are unchanged.

The gate opens only after a verified Discord sign-in that passes **at least
one** of these:

- the user ID is in `discord.allowUsers`, or
- the account is a member of `discord.guildId` and one of its roles is listed
  in `discord.roles` with a `tier` equal to `discord.requiredTier` (any listed
  role when `requiredTier` is empty).

The role check is live: `GET /users/@me/guilds/{guildId}/member` with the
OAuth2 token, which requires the `guilds.members.read` scope. A non-member or
a member without the role is rejected with the account's Discord ID on the
result page, so it can be added to `discord.allowUsers` as a fallback.

A saved session is **never** an automatic login - it only earns the one-click
**"Welcome back - Continue as …"** strip, and that continue re-checks the
allowlist **or** the saved role against the current config before the gate
opens (same rule as FalcoForge).

## The flow (one to one with Premium-Optimizer)

1. **Continue with Discord** - the only option on the card.
2. OAuth2 public-client **PKCE**: `code_verifier` stays on this PC, the
   authorize URL carries the S256 challenge, `prompt=consent`.
3. A loopback listener on `127.0.0.1:43713` catches the browser redirect at
   `/auth/discord/callback` and shows the branded result page. If the port
   cannot be opened, the card falls back to paste-the-link (`login-manual`).
4. The code is exchanged for a token (no client secret needed), the identity
   comes from `/users/@me`, then the **dev allowlist or the guild role gate**
   decides access. A rejected sign-in shows the account's Discord ID on the
   result page so it can be added to the allowlist.
5. A successful sign-in writes an HMAC-signed, machine-bound session
   (`SessionStore`, in the app data dir). Editing or copying the file to
   another PC invalidates it.

## Auth config

File: `auth.json` (gitignored), template `auth.example.json` at the repo root.

The whole `auth.json` can also travel as **one JSON blob** under the name
**`ZLP_AUTH_JSON`** (alias `BSIC_AUTH_JSON`):
- **runtime env var** `ZLP_AUTH_JSON` = the blob text (wins over any file);
- **repository secret** `ZLP_AUTH_JSON` = baked into the EXE at build time by
  both GitHub Actions workflows (`ci.yml`, `release.yml`), so the released
  portable EXE signs in with no `auth.json` beside it.

Precedence (first source that parses wins for the `discord` object):

1. per-field env vars `BSIC_DISCORD_*` (they override individual values of
   whatever source was picked below)
2. `ZLP_AUTH_JSON` / `BSIC_AUTH_JSON` env blob
3. `$BSIC_AUTH_FILE`, then `auth.json` next to the EXE, then the app data dir
4. the blob embedded at build time from the `ZLP_AUTH_JSON` secret

Env overrides: `BSIC_AUTH_FILE`, `BSIC_AUTH_JSON`/`ZLP_AUTH_JSON`,
`BSIC_DISCORD_CLIENT_ID`, `BSIC_DISCORD_CLIENT_SECRET`,
`BSIC_DISCORD_ALLOW_USERS`, `BSIC_DISCORD_GUILD_ID`,
`BSIC_DISCORD_REQUIRED_TIER`, `BSIC_DISCORD_REDIRECT_PORT`,
`BSIC_DISCORD_SCOPE`, `BSIC_DISCORD_INVITE`.
See the template's `_env` block.

Allowlist gate (simplest):

```json
{
  "discord": {
    "clientId": "<Application ID from the Discord developer portal>",
    "scope": "identify",
    "redirectPort": 43713,
    "allowUsers": ["<your Discord user ID>"]
  }
}
```

Server role gate (the Premium-Optimizer shape - `allowUsers` may be omitted):

```json
{
  "discord": {
    "clientId": "<Application ID>",
    "scope": "identify guilds.members.read",
    "guildId": "<Discord server ID>",
    "requiredTier": "Dev",
    "roles": {
      "<role ID>": { "tier": "Dev", "label": "Dev" }
    },
    "inviteUrl": "https://discord.gg/your-invite"
  }
}
```

- `roles` maps a **role ID** (not a user ID) to its tier; the object form
  `{ "tier": "Dev", "label": "Dev" }` and a plain `"Dev"` string both work.
  An array of role IDs / `{ "id": ... }` objects is accepted too.
- `requiredTier` is compared case-insensitively. Leave it empty to accept any
  listed role. A role listed **without** a tier is an explicit grant and is not
  filtered by `requiredTier`.
- `guilds.members.read` is appended to `scope` automatically when a role gate
  is configured, and `identify` is always present. Comma/semicolon-separated
  scopes are normalised to the space-separated form Discord requires.
- Values that a chat app rewrote as markdown (`[guilds.members.read](http://…)`)
  are cleaned up on load, so such a blob still signs in.
- `allowUsers` and the role gate are an **OR**: listing a user ID bypasses the
  guild lookup for that account.

If Discord rejects the token exchange with `invalid_client` while
`clientSecret` is empty, the application does not allow secret-less PKCE: put
the current Client Secret from **Developer Portal → OAuth2** into
`discord.clientSecret` (it travels in `ZLP_AUTH_JSON`, never in git), or enable
the **Public Client** flag for the application if the portal offers it.

## Discord developer portal

- Create an application; copy its **Application ID** into `discord.clientId`.
- Add a redirect URI that matches exactly:
  **`http://127.0.0.1:43713/auth/discord/callback`**
  (scheme, host, port and path all have to match; `127.0.0.1` is used on
  purpose - `localhost` may resolve to `::1`).
- No OAuth2 secret is required when the application is a public client
  (PKCE). With the server role gate, request `identify guilds.members.read`;
  with the allowlist gate, `identify` is enough.
- The role gate reads the **signed-in user's own member object** in the
  configured guild, so the account must be a member of that server. No bot
  user or privileged intent is involved.

## CI / the ZLP_AUTH_JSON secret

Both workflows pass the repository secret `ZLP_AUTH_JSON` (whole `auth.json`
blob) to the configure step, and CMake embeds it into the EXE as
`:/bsic_auth/auth.json`. Set it under *Settings → Secrets and variables →
Actions*; the content is exactly one `auth.json` file, e.g.:

```json
{"discord":{"clientId":"<Application ID>","allowUsers":["<your Discord user ID>"]}}
```

`redirectPort` inside the blob is optional (default `43713`). If the secret is
absent the build simply carries no embed and the file/env paths above still
work - the self-test says which case it saw.

The blob is read at **configure time**, so changing the secret changes
nothing until the workflow is run again (merge to `main`, push a tag, or use
*Run workflow*). A secret pasted together with a ```` ```json ```` fence, a
UTF-8 BOM, or chat-markdown links is cleaned up on load; still, plain JSON is
the least surprising form.

Role-gate secret example (no user IDs needed):

```json
{
  "discord": {
    "clientId": "1561651354953453628",
    "clientSecret": "",
    "scope": "identify guilds.members.read",
    "guildId": "1380048834876674108",
    "requiredTier": "Dev",
    "roles": {
      "1488215210010611932": { "tier": "Dev", "label": "Dev" }
    },
    "inviteUrl": "https://discord.gg/5qkKPRZkWa"
  },
  "session": { "offlineGraceDays": 1, "rememberMe": true }
}
```

(The `session` object is accepted for compatibility with the
Premium-Optimizer blob; the local session is always saved and stays valid on
this PC until sign-out.)

## Testing

```sh
BSI-Customizer --selftest
```

The `[auth]` section pins the login system without touching the network:
session sign/verify + tamper rejection, purpose-bound tokens, the PKCE S256
math, callback URL parsing, config parsing from env (including the role gate
and markdown-mangled scope repair), the role/tier matcher, the Backend gate
authority, and the one-click continue over a saved allowlisted or role-gated
session.
