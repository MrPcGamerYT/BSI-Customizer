# BSI Customizer Discord Login — AI Developer Handoff

> **Purpose:** Give an AI developer enough repository-specific context to maintain the Discord OAuth login and authorization gate without weakening it. Read this file together with [`AUTH.md`](AUTH.md); the source code is authoritative if documentation and code disagree.

## Repository model

BSI Customizer uses Discord OAuth2 to identify a person and authorize access to its desktop workspace. It is **not** a browser-only login, a Discord bot login, or proof that a user is trustworthy. The actual gate is native C++ (`AuthManager` plus guarded `Backend` operations); QML only presents the login experience. The feature follows the same broad Discord-only, role-gated architecture as the repository's Premium-Optimizer/FalcoForge port.

### Important files

| File | Responsibility |
| --- | --- |
| `src/core/AuthManager.h/.cpp` | Config loading, OAuth2 authorization-code + PKCE, loopback callback, code/token exchange, Discord identity and live role checks, saved-session revalidation, sign-in/sign-out state. This is the login and access-policy authority. |
| `src/core/SessionStore.h/.cpp` | Signed local identity envelope and protected refresh-token persistence. No network/OAuth logic belongs here. |
| `src/core/Backend.h/.cpp` | Guards native QML-facing operations when an `AuthManager` is attached and not authorized. A UI-only check is not an authorization check. |
| `src/qml/LoginGate.qml`, `src/qml/Main.qml` | Login and resume UI; calls the native auth object. Presentation only. |
| `src/main.cpp` | Creates and wires the runtime auth manager/backend; inspect it before changing startup or object lifetimes. |
| `auth.example.json` | Safe-to-commit configuration template. Never put real secrets or private production policy in this example. |
| `docs/AUTH.md` | User/developer setup, Discord portal, configuration, CI secret, and security explanation. |
| `src/selftest.cpp` | Offline auth regression tests (including PKCE, policy parsing, role matching, session integrity and gate behavior). |

## Required authorization invariants

1. **Fail closed.** Missing configuration, invalid OAuth response, state mismatch, token refresh error, Discord API outage, missing role scope, malformed role response, non-membership, or no qualifying role must leave `authorized == false`. Never grant access because an API could not be reached.
2. **Native gate is authoritative.** Keep checks in `Backend` for every native operation that changes or inspects protected workspace data. QML visibility, disabled buttons, route guards, saved profiles, and login-card state are not sufficient. New backend operations must use the existing guarded-entry pattern.
3. **Live role policy wins over the allowlist.** If both `discord.guildId` and non-empty `discord.roles` configure a role gate, fetch the current signed-in user's member record and evaluate its current role IDs on every successful login and every saved-session continuation. `discord.allowUsers` must not bypass that gate. Use the allowlist only when there is no configured role gate.
4. **Never authorize from cached session data.** `session.json`'s identity, access tier, role, timestamp, and signature are not proof of current entitlement. Refresh the OAuth credential, retrieve `/users/@me`, ensure the account ID still matches the saved identity, and re-check the active policy before opening the gate.
5. **PKCE and callback state stay intact.** Generate a high-entropy one-time verifier and random state per authorization attempt; send only the S256 challenge to Discord; validate the returned state before exchanging the code. Keep the verifier in memory, use the exact redirect URI, and expire/cancel the handoff as the current implementation does.
6. **Do not ship a client secret.** The distributed app is a public OAuth client using PKCE. Anything embedded in a desktop executable can be extracted. Release configuration must keep `discord.clientSecret` empty. If confidential credentials become necessary, move the exchange and authorization decision to a server.
7. **Protect refresh credentials.** On Windows, `SessionStore` protects the refresh token with current-user DPAPI and writes it separately from the signed identity. Keep secure-storage failure visible and fail closed when a saved credential cannot be checked. Do not log tokens, authorization codes, callback URLs containing codes, client secrets, or raw auth JSON.
8. **Sign-out means removal.** Sign-out/forget must clear both the identity session and protected refresh token, then revoke local authorization. Avoid retaining a stale authorized UI/backend state.
9. **Be honest about the threat model.** A desktop binary and local checks can be modified by its owner. This gate protects normal product use; it cannot provide server-grade enforcement against a determined local attacker. High-value entitlements or server resources must be checked by a trusted server.
10. **Keep non-GUI tooling behavior intentional.** Tests and command-line tools may instantiate `Backend` without an `AuthManager` and are deliberately ungated. Do not mistake that test/CLI path for the interactive app's security boundary.

## OAuth and authorization flow

1. `AuthManager::startDiscord()` checks configuration and creates a fresh PKCE verifier, S256 challenge, state, and short-lived handoff.
2. The app opens Discord's OAuth authorize URL with `identify` and, for a role policy, `guilds.members.read`. The listener binds to `127.0.0.1:<redirectPort>`; the default is `43713`. The exact callback path is `/auth/discord/callback`. If the listener cannot start, the UI offers manual paste of the resulting redirect URL/code; that path must still use the same state/verifier validation.
3. The callback's state is checked before exchanging the one-time code at Discord's OAuth token endpoint. The application then calls `/users/@me` to obtain the signed-in account from Discord (not from user-entered profile data).
4. With `guildId` plus `roles`, request the current user's member record at `/users/@me/guilds/{guildId}/member` using that access token and match current role IDs against the configured role map and `requiredTier`. This is a user OAuth check; no bot token or privileged bot intent is needed. A 404 means the account is not in the configured server; 403 generally means the required member scope is missing. Both keep the gate closed.
5. Without a configured role gate, match the verified Discord user ID against `allowUsers`. A role gate, when configured, remains mandatory even for allowlisted IDs.
6. Only after the selected policy grants access does the auth manager publish the authorized state. It stores the identity in a signed, machine-bound envelope and stores the refresh token with Windows DPAPI. On startup / “Continue as…”, refresh with Discord, verify the same account, fetch current roles again when applicable, and fail closed on any failure.

The local session signature is tamper-evidence for this application, not a remote authority: the signing secret is stored on that PC. The identity/session file alone must never unlock the product.

## Configure Discord and the application

### 1. Create/configure the Discord OAuth application

1. In the Discord Developer Portal, create or select the application and copy its **Application ID** (numeric client ID).
2. Register this exact redirect URI in OAuth2 settings (change the port only if the app's configured port also changes):

   `http://127.0.0.1:43713/auth/discord/callback`

   Scheme, host, port, and path must match exactly. Use `127.0.0.1`, not `localhost` (which may resolve to IPv6 while this app binds IPv4 loopback).
3. Configure it as a public/native PKCE client. Leave the client secret empty in release configuration. Do not ask a user to paste a Discord password, bot token, OAuth access token, or refresh token into the app or into chat.
4. For a role gate, ensure the user grants `identify guilds.members.read`. The implementation adds `guilds.members.read` automatically when `guildId` plus `roles` are configured and always includes `identify`.

### 2. Choose an access policy

**Preferred for distributed/private releases: server role gate.** Use the Discord server ID for `guildId`, role IDs (not user IDs) as keys in `roles`, and set `requiredTier` if only one tier or higher should qualify. Configure the desired tier semantics in the role map; see `docs/AUTH.md` for accepted role formats. The account must be a member of that server.

```json
{
  "discord": {
    "clientId": "<Discord Application ID>",
    "clientSecret": "",
    "scope": "identify guilds.members.read",
    "redirectPort": 43713,
    "guildId": "<Discord server ID>",
    "requiredTier": "Dev",
    "roles": {
      "<Discord role ID>": { "tier": "Dev", "label": "Dev" }
    },
    "inviteUrl": "https://discord.gg/<invite>"
  }
}
```

An allowlist-only config is useful for development/small controlled use, but it is not sufficient for an official release build that enforces embedded auth:

```json
{
  "discord": {
    "clientId": "<Discord Application ID>",
    "clientSecret": "",
    "scope": "identify",
    "redirectPort": 43713,
    "allowUsers": ["<Discord user ID>"]
  }
}
```

### 3. Supply configuration safely

- `auth.example.json` is the template. For local development, copy it to an untracked `auth.json` next to the executable, or set `BSIC_AUTH_FILE` to a private file. Never commit a filled production config.
- Development builds can also read the whole JSON blob from `ZLP_AUTH_JSON` (`BSIC_AUTH_JSON` is an alias); per-field `BSIC_DISCORD_*` environment variables can override supported fields in development.
- The official release embeds the whole JSON blob from the GitHub Actions repository secret **`ZLP_AUTH_JSON`** at configure/build time. The release workflow enables embedded-policy enforcement, requires a valid embedded config and server role gate, and ignores local/environment overrides for that policy. Update the secret in repository Actions secrets; re-run the release workflow/build for it to take effect. Never put the blob in a source file, issue, PR comment, or AI prompt.
- For exact config parsing and precedence, inspect `AuthManager::loadConfig()`; it is authoritative. Note that the role map comes from the selected JSON policy rather than a per-role environment override.

## Safe change and review checklist for an AI

Before changing login/auth code:

- Read `docs/AUTH.md`, this handoff, the relevant source files above, and the auth tests in `src/selftest.cpp`.
- Trace both initial sign-in and saved-session startup. Review cancellation, wrong state, expired/invalid token, refresh-token rotation, missing secure storage, 403/404 role responses, network outage, no membership, changed account, changed/removed role, and sign-out.
- Keep policy selection consistent in both `completeDiscord()` and saved-session revalidation. Do not make a helper that grants access based on a cached role or user ID when a role gate is active.
- Preserve the exact callback path/host/port relationship among `AuthManager`, the Developer Portal setup docs, and tests.
- Make configuration, UI, and backend changes together only when required; the backend guard remains decisive. Avoid adding credentials, logging secrets, broad CORS/web redirects, arbitrary callback hosts, or a new bypass flag.
- Add/update deterministic offline tests for every policy/security change. Tests should assert rejection as well as acceptance. Do not claim a real Discord sign-in was tested by unit tests.
- Run the repository's documented CI/self-test workflow; do not claim live GUI or Discord validation unless it was actually performed.
- Report what is verified and what still depends on Developer Portal configuration or a live Discord account. Never invent Discord IDs, role IDs, tokens, scopes, or test results.

## Useful references

- Setup and complete configuration details: [`AUTH.md`](AUTH.md)
- Safe template: [`../auth.example.json`](../auth.example.json)
- Auth implementation: `src/core/AuthManager.cpp`, `src/core/AuthManager.h`
- Protected session: `src/core/SessionStore.cpp`, `src/core/SessionStore.h`
- Native authorization guards: `src/core/Backend.cpp`, `src/core/Backend.h`
- Login presentation: `src/qml/LoginGate.qml`, `src/qml/Main.qml`
- Offline auth regression coverage: `src/selftest.cpp` (`[auth]` section)
