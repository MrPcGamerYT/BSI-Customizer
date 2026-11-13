# Dev Sign-in Gate (Discord only)

Port of the Premium-Optimizer (FalcoForge) login system - same architecture,
one way in: **Discord**. This tool is private dev software, so the login page
offers a single option. A configured Discord server role gate (`guildId` +
`roles` + `requiredTier`) is mandatory; the user-ID allowlist is only used if
no role gate is configured.

## Rule

**The login card is UX only.** Authority is `AuthManager` + the guards in
`Backend`: while the Discord sign-in is not verified, every action in the UI
refuses to run (`Sign in with Discord to unlock BSI Customizer.`). The
command-line tools (`--selftest`, `--rpcid`, `--theme-probe`) are unchanged.

The gate opens only after Discord has returned the current account and, when
configured, its current guild-member roles. If `discord.guildId` and
`discord.roles` are configured, that **live server role gate is authoritative**
for every account, including IDs also listed in `discord.allowUsers`. The
allowlist is used only when no role gate is configured. A missing role or
server membership is rejected; the cached role in the local session is never
trusted.

The role check is live: `GET /users/@me/guilds/{guildId}/member` with a fresh
OAuth access token, which requires `guilds.members.read`. On each app launch,
the saved OAuth refresh token is refreshed and Discord's current account and
role are queried before the gate opens. If the network, token refresh, or role
lookup fails, the gate remains locked.

The **"Welcome back - Continue as …"** card displays the saved account and
lets the user retry. The identity file alone cannot open the gate. Windows
DPAPI protects the refresh token for the current Windows user; old sessions
without a protected token require a fresh Discord sign-in.

## The flow (one to one with Premium-Optimizer)

1. **Continue with Discord** - the only option on the card.
2. OAuth2 public-client **PKCE**: `code_verifier` stays on this PC, the
   authorize URL carries the S256 challenge, `prompt=consent`.
3. A loopback listener on `127.0.0.1:43713` catches the browser redirect at
   `/auth/discord/callback` and shows the branded result page. If the port
   cannot be opened, the card falls back to paste-the-link (`login-manual`).
4. The one-time code is exchanged for access and refresh tokens. The account
   comes from `/users/@me`; if a role gate is configured, the current guild
   member endpoint must grant a configured role, otherwise the allowlist is
   checked.
5. A successful sign-in writes a signed, machine-bound identity session and
   stores the refresh token protected by Windows DPAPI. On every next launch,
   Discord is contacted again and current access is checked before unlocking.

## Auth config

File: `auth.json` (gitignored), template `auth.example.json` at the repo root.

The whole `auth.json` travels as **one JSON blob** under the repository secret
`ZLP_AUTH_JSON` (alias `BSIC_AUTH_JSON`) and is embedded at build time. The
release workflow uses `BSI_REQUIRE_EMBEDDED_AUTH=ON`: it fails if the secret is
missing, and that release treats the embedded Discord policy as authoritative.
A local `auth.json` or environment variable cannot replace the released guild,
role list, or allowlist. The CI development build still accepts runtime config
so self-tests can run without production credentials.

For local development builds only, config is selected in this order:

1. per-field `BSIC_DISCORD_*` environment variables
2. `ZLP_AUTH_JSON` / `BSIC_AUTH_JSON` environment blob
3. `$BSIC_AUTH_FILE`, then `auth.json` next to the EXE, then the app data dir
4. a build-time embedded blob, if present

The development-only env overrides are `BSIC_AUTH_FILE`,
`BSIC_AUTH_JSON`/`ZLP_AUTH_JSON`, `BSIC_DISCORD_CLIENT_ID`,
`BSIC_DISCORD_CLIENT_SECRET`, `BSIC_DISCORD_ALLOW_USERS`,
`BSIC_DISCORD_GUILD_ID`, `BSIC_DISCORD_REQUIRED_TIER`,
`BSIC_DISCORD_REDIRECT_PORT`, `BSIC_DISCORD_SCOPE`, and
`BSIC_DISCORD_INVITE`. See the template's `_env` block.

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
- When `guildId` + `roles` configure a role gate, the live role check is
  mandatory; `allowUsers` does not bypass it. The allowlist is used only when
  no role gate is configured.

For a distributed desktop release, leave `discord.clientSecret` empty and
configure Discord as a public PKCE client. A secret embedded in an EXE is
extractable, so the release workflow rejects a non-empty `clientSecret`. For a
development-only build, a confidential client secret may be supplied locally,
but it must not be committed or distributed. If Discord requires a confidential
client, put the token exchange behind a server instead of shipping that secret
inside the desktop app.

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

Both workflows can pass the repository secret `ZLP_AUTH_JSON` (whole
`auth.json` blob) to the configure step, and CMake embeds it into the EXE as
`:/bsic_auth/auth.json`. The release workflow requires it and refuses an
allowlist-only config: the embedded Discord guild and role list are the policy
for every release. Set the secret under *Settings → Secrets and variables →
Actions*; use a server-role config such as:

```json
{
  "discord": {
    "clientId": "<Application ID>",
    "scope": "identify guilds.members.read",
    "guildId": "<Discord server ID>",
    "requiredTier": "Dev",
    "roles": { "<role ID>": { "tier": "Dev", "label": "Dev" } }
  }
}
```

`redirectPort` inside the blob is optional (default `43713`). The CI workflow
may run without an embedded policy for development/testing, but a release
build fails if the secret is absent or does not contain a server role gate.

The blob is read at **configure time**, so changing the secret changes
nothing until the workflow is run again (merge to `main`, push a tag, or use
*Run workflow*). Development builds clean up a pasted ```` ```json ```` fence,
UTF-8 BOM, or chat-markdown links; release builds require plain JSON so CMake
can verify the public-client/no-secret policy before embedding it.

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
Premium-Optimizer blob; the local identity is saved, but Discord refresh and
current role membership are required again at each launch.)

## Security boundary

This improves authorization freshness and protects the saved refresh token, but
a desktop executable running on a user's PC cannot be made impossible to patch
or bypass by its owner. The C++ checks are a deterrent and protect normal UI/API
use; a determined binary modifier can still change local code. Do not embed
high-value secrets or rely on this client alone to protect server-side assets.
For stronger enforcement, sensitive operations and entitlement decisions must
be performed by a server that validates Discord roles on every request.

## Testing

```sh
BSI-Customizer --selftest
```

The `[auth]` section pins the login system without touching the network:
session sign/verify + tamper rejection, protected-token storage on Windows,
purpose-bound tokens, the PKCE S256 math, callback URL parsing, config parsing
from env (including the role gate and markdown-mangled scope repair), the
tier matcher, the Backend gate authority, and fail-closed behavior for a saved
identity that has no live Discord credential.
