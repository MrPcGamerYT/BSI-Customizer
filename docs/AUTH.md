# Dev Sign-in Gate (Discord only)

Port of the Premium-Optimizer (FalcoForge) login system - same architecture,
one way in: **Discord**. This tool is private dev software, so the login page
offers a single option and access follows a **dev allowlist** of Discord user
IDs instead of a paid plan.

## Rule

**The login card is UX only.** Authority is `AuthManager` + the guards in
`Backend`: while the Discord sign-in is not verified, every action in the UI
refuses to run (`Sign in with Discord to unlock BSI Customizer.`). The
command-line tools (`--selftest`, `--rpcid`, `--theme-probe`) are unchanged.

The gate opens only after a verified Discord sign-in whose user ID is on the
allowlist. A saved session is **never** an automatic login - it only earns the
one-click **"Welcome back - Continue as …"** strip, and that continue
re-checks the allowlist before the gate opens (same rule as FalcoForge).

## The flow (one to one with Premium-Optimizer)

1. **Continue with Discord** - the only option on the card.
2. OAuth2 public-client **PKCE**: `code_verifier` stays on this PC, the
   authorize URL carries the S256 challenge, `prompt=consent`.
3. A loopback listener on `127.0.0.1:43713` catches the browser redirect at
   `/auth/discord/callback` and shows the branded result page. If the port
   cannot be opened, the card falls back to paste-the-link (`login-manual`).
4. The code is exchanged for a token (no client secret needed), the identity
   comes from `/users/@me`, and the **dev allowlist** decides access. A
   rejected sign-in shows the account's Discord ID on the result page so it
   can be added to the allowlist.
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
`BSIC_DISCORD_ALLOW_USERS`, `BSIC_DISCORD_REDIRECT_PORT`,
`BSIC_DISCORD_SCOPE`, `BSIC_DISCORD_INVITE`.
See the template's `_env` block.

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

## Discord developer portal

- Create an application; copy its **Application ID** into `discord.clientId`.
- Add a redirect URI that matches exactly:
  **`http://127.0.0.1:43713/auth/discord/callback`**
  (scheme, host, port and path all have to match; `127.0.0.1` is used on
  purpose - `localhost` may resolve to `::1`).
- No OAuth2 secret is required (public client + PKCE). The scopes used are
  `identify` only.

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

## Testing

```sh
BSI-Customizer --selftest
```

The `[auth]` section pins the login system without touching the network:
session sign/verify + tamper rejection, purpose-bound tokens, the PKCE S256
math, callback URL parsing, config parsing from env, the Backend gate
authority, and the one-click continue over a saved allowlisted session.
