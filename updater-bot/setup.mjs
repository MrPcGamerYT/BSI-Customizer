#!/usr/bin/env node
// ---------------------------------------------------------------------------
// BSI Customizer - Discord updater setup check
//
// Answers the only questions that matter after copying IDs out of Discord:
// is the token valid, is the bot in that server, can it read that channel, and
// what exactly goes into auth.json? It never posts, deletes or changes anything.
//
//   DISCORD_BOT_TOKEN=... node updater-bot/setup.mjs --channel 123456789012345678
//   node updater-bot/setup.mjs --channel ... --token ... [--json]
//
// Exit codes: 0 everything the app needs works, 1 something is missing,
// 2 the command line or the configuration is wrong.
// ---------------------------------------------------------------------------

import { existsSync } from 'node:fs';
import { readFile } from 'node:fs/promises';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));

function die(message, code = 2) {
    console.error(`error: ${message}`);
    process.exit(code);
}

function parseArgs(argv) {
    const args = { json: false };
    for (let i = 0; i < argv.length; i += 1) {
        const next = () => {
            if (i + 1 >= argv.length) die(`${argv[i]} needs a value`);
            return argv[++i];
        };
        switch (argv[i]) {
            case '--channel': args.channel = next(); break;
            case '--token': args.token = next(); break;
            case '--auth-file': args.authFile = next(); break;
            case '--json': args.json = true; break;
            case '--help': case '-h':
                console.log(`BSI Customizer - Discord updater setup check

  node updater-bot/setup.mjs --auth-file auth.json        (reads updater.* from it)
  node updater-bot/setup.mjs --channel <channelId> [--token <bot token>] [--json]
  DISCORD_BOT_TOKEN=... node updater-bot/setup.mjs --channel <channelId>

Checks the token, the server, the channel and the read/write permissions the
app and publish.mjs need, then prints the auth.json block to paste. Changes
nothing on Discord. With --auth-file it also lints that file the way the app
reads it (markdown-mangled values, missing updater block, ignored keys).
Exit 0 when the app can update from that channel.`);
                process.exit(0);
                break;
            default: die(`unknown option ${argv[i]}`);
        }
    }
    return args;
}

// Chat apps turn URLs and dotted words into "[label](target)" markdown when a
// file is pasted through them - AuthManager strips that, and so does this, so
// both read the same file the same way.
function stripMarkdownLink(value, preferLabel) {
    if (typeof value !== 'string') return value;
    return value.replace(/\[([^\]]*)\]\(([^)]*)\)/g, (_all, label, target) =>
        (preferLabel ? label : target)).trim();
}

function cleanToken(value) {
    return stripMarkdownLink(String(value ?? ''), false).replace(/^bot\s+/i, '').trim();
}

function findAuthFile(args) {
    const candidates = [
        args.authFile,
        process.env.BSIC_AUTH_FILE,
        path.join(process.cwd(), 'auth.json'),
        path.join(HERE, '..', 'auth.json'),
    ].filter(Boolean);
    for (const candidate of candidates) {
        const resolved = path.resolve(candidate);
        if (existsSync(resolved)) return resolved;
    }
    return args.authFile ? path.resolve(args.authFile) : null;
}

async function readJsonFile(file, label) {
    try {
        return JSON.parse(await readFile(file, 'utf8'));
    } catch (error) {
        die(`${label} is not valid JSON: ${error.message}`);
    }
}

// What the application would make of an auth.json. Everything here is advisory:
// the app is the authority, this only explains it before you ship the file.
function lintAuthJson(authFile, auth) {
    const notes = [];
    const problems = [];
    if (!auth || typeof auth !== 'object') {
        problems.push('the file is not a JSON object');
        return { notes, problems };
    }

    const discord = auth.discord && typeof auth.discord === 'object' ? auth.discord : {};
    for (const [key, preferLabel] of [['scope', true], ['inviteUrl', false]]) {
        const raw = discord[key];
        if (typeof raw === 'string' && /\[[^\]]*\]\([^)]*\)/.test(raw)) {
            notes.push(`${key} was pasted as a markdown link; the app reads it as `
                + `"${stripMarkdownLink(raw, preferLabel)}" (harmless, but feel free to fix the file)`);
        }
    }

    const caller = auth.updater;
    if (!caller || typeof caller !== 'object') {
        problems.push('there is no "updater" object - the app has no self-updater without it');
    } else {
        const channel = stripMarkdownLink(String(caller.channelId ?? ''), false);
        if (!channel) problems.push('updater.channelId is empty (the releases channel ID)');
        else if (!/^\d{5,25}$/.test(channel)) problems.push(`updater.channelId "${channel}" is not a numeric channel ID`);
        if (!cleanToken(caller.botToken)) problems.push('updater.botToken is empty (the bot token)');
        if (caller.apiBase && !/^https:\/\//i.test(caller.apiBase)) {
            problems.push(`updater.apiBase "${caller.apiBase}" must be https://`);
        }
        if (caller.enabled === false) notes.push('updater.enabled is false - the updater is switched off in this file');
        if (!caller.hmacKey) {
            notes.push('updater.hmacKey is empty: the channel is the trust boundary. '
                + 'Set it (and BSI_UPDATE_HMAC_KEY when publishing) to sign manifests.');
        }
    }

    const allowUsers = Array.isArray(discord.allowUsers) ? discord.allowUsers : [];
    const hasUserGate = allowUsers.some((id) => /^\d{5,25}$/.test(String(id).trim()));
    const hasRoleGate = Boolean(discord.guildId) && discord.roles
        && Object.keys(discord.roles).length > 0;
    if (!hasUserGate && !hasRoleGate) {
        problems.push('neither discord.allowUsers nor discord.guildId + discord.roles is set - '
            + 'nobody could sign in');
    }
    if (discord.clientSecret) {
        notes.push('discord.clientSecret is set; it is optional for the PKCE public client');
    }
    for (const key of Object.keys(auth.session ?? {})) {
        if (!['rememberMe', 'offlineGraceDays'].includes(key)) {
            notes.push(`session.${key} is not read by the app (ignored)`);
        }
    }
    if (auth.session && auth.session.offlineGraceDays !== undefined) {
        notes.push('session.offlineGraceDays is not read by the app (harmless, ignored)');
    }
    return { notes, problems };
}

async function loadConfig(args) {
    const authFile = findAuthFile(args);
    let auth = null;
    let lint = { notes: [], problems: [] };
    if (authFile) {
        if (!existsSync(authFile)) die(`no such file: ${authFile}`);
        auth = await readJsonFile(authFile, authFile);
        lint = lintAuthJson(authFile, auth);
    }

    let file = {};
    if (existsSync(path.join(HERE, 'config.json'))) {
        file = await readJsonFile(path.join(HERE, 'config.json'), 'updater-bot/config.json');
    }

    const updater = auth && typeof auth.updater === 'object' ? auth.updater : {};
    const discord = auth && typeof auth.discord === 'object' ? auth.discord : {};
    const token = cleanToken(args.token || process.env.DISCORD_BOT_TOKEN
        || process.env.DISCORD_PUBLISH_TOKEN || updater.botToken || file.botToken);
    const channel = stripMarkdownLink(String(args.channel || process.env.DISCORD_CHANNEL_ID
        || process.env.DISCORD_UPDATER_CHANNEL_ID || updater.channelId || file.channelId || ''), false);
    const apiBase = (process.env.DISCORD_API_BASE || updater.apiBase || file.apiBase
        || 'https://discord.com/api/v10').replace(/\/+$/, '');
    const invite = stripMarkdownLink(String(discord.inviteUrl ?? ''), false);

    return { token, channel, apiBase, authFile, lint, invite, updater };
}

async function api(apiBase, route, token) {
    const response = await fetch(`${apiBase}${route}`, {
        headers: {
            Authorization: `Bot ${token}`,
            'User-Agent': 'BSI-Customizer-setup/1.0 (+https://github.com/MrPcGamerYT/BSI-Customizer)',
        },
    });
    const text = await response.text();
    let data = null;
    try {
        data = text ? JSON.parse(text) : null;
    } catch {
        data = text;
    }
    return { ok: response.ok, status: response.status, data };
}

const PERMISSIONS = {
    VIEW_CHANNEL: 1024n,
    READ_MESSAGE_HISTORY: 65536n,
    SEND_MESSAGES: 2048n,
    ATTACH_FILES: 32768n,
    MANAGE_MESSAGES: 8192n,
};

const ALL_PERMISSIONS = Object.values(PERMISSIONS).reduce((all, value) => all | value, 0n);

// Discord's channel permission order: role permissions, the @everyone overwrite,
// then role overwrites, then the member overwrite (whose allow beats a deny).
function permissionsOf(channel, guild, member, botUserId) {
    if (guild.owner_id === botUserId) return ALL_PERMISSIONS;

    let allowed = 0n;
    let denied = 0n;
    const everyone = guild.roles.find((role) => role.id === guild.id);
    if (everyone) allowed |= BigInt(everyone.permissions);
    for (const roleId of member.roles ?? []) {
        const role = guild.roles.find((r) => r.id === roleId);
        if (role) allowed |= BigInt(role.permissions);
    }

    const overwrites = channel.permission_overwrites ?? [];
    const apply = (overwrite, memberLevel) => {
        allowed |= BigInt(overwrite.allow);
        if (memberLevel) denied &= ~BigInt(overwrite.allow);
        denied |= BigInt(overwrite.deny);
    };
    for (const overwrite of overwrites) {
        if (overwrite.type === 0 && overwrite.id === guild.id) apply(overwrite, false);
    }
    for (const overwrite of overwrites) {
        if (overwrite.type === 0 && overwrite.id !== guild.id
            && (member.roles ?? []).includes(overwrite.id)) {
            apply(overwrite, false);
        }
    }
    for (const overwrite of overwrites) {
        if (overwrite.type === 1 && overwrite.id === botUserId) apply(overwrite, true);
    }
    return allowed & ~denied;
}

function has(mask, permission) {
    return (mask & PERMISSIONS[permission]) === PERMISSIONS[permission];
}

// The updater block to paste back into auth.json (or the ZLP_AUTH_JSON secret).
function skeleton(config) {
    return {
        apiBase: config.apiBase || 'https://discord.com/api/v10',
        channelId: config.channel || '1234567890123456789',
        botToken: config.token ? 'PASTE_THE_BOT_TOKEN' : 'PASTE_THE_BOT_TOKEN',
        manifest: 'manifest.json',
        hmacKey: '',
        enabled: true,
    };
}

async function main() {
    const args = parseArgs(process.argv.slice(2));
    const config = await loadConfig(args);

    const report = { ok: false, channel: config.channel, problems: [], warnings: [] };
    const fail = (text) => report.problems.push(text);
    const warn = (text) => report.warnings.push(text);

    // Everything the file itself says, before a single request goes out. A
    // missing updater block or a mangled value is reported here, not as a
    // confusing 401 later.
    for (const problem of config.lint.problems) fail(problem);
    for (const note of config.lint.notes) warn(note);

    if (!config.token || !config.channel) {
        if (args.json) {
            console.log(JSON.stringify({
                ok: false,
                authFile: config.authFile,
                problems: report.problems,
                warnings: report.warnings,
                authJsonBlock: { updater: skeleton(config) },
            }, null, 2));
            process.exit(1);
        }
        console.log(config.authFile ? `file      ${config.authFile}` : 'file      (none - no auth.json found)');
        if (!config.token) console.log('PROBLEM   no bot token: set updater.botToken in the file, or pass --token');
        if (!config.channel) console.log('PROBLEM   no channel id: set updater.channelId in the file, or pass --channel');
        for (const note of report.warnings) console.log(`note      ${note}`);
        console.log('');
        console.log('Add this to auth.json, fill the two values, then run this again:');
        console.log('');
        console.log(JSON.stringify({ updater: skeleton(config) }, null, 2));
        process.exit(1);
    }

    const me = await api(config.apiBase, '/users/@me', config.token);
    if (!me.ok) {
        if (me.status === 401) die('the bot token was rejected (401). Reset it in the developer portal and paste the new one.', 1);
        die(`GET /users/@me failed with HTTP ${me.status}`, 1);
    }
    report.bot = { id: me.data.id, username: me.data.username };

    const channel = await api(config.apiBase, `/channels/${config.channel}`, config.token);
    if (!channel.ok) {
        if (channel.status === 404) die('that channel does not exist, or the bot is not in the server (404).', 1);
        if (channel.status === 403) die('the bot cannot see that channel (403): give it View Channel.', 1);
        die(`GET /channels/${config.channel} failed with HTTP ${channel.status}`, 1);
    }
    report.channelName = channel.data.name;
    report.guildId = channel.data.guild_id;
    report.type = channel.data.type;
    if (channel.data.type !== 0) warn('this is not a text channel; the updater reads messages from a text channel.');

    const guild = await api(config.apiBase, `/guilds/${channel.data.guild_id}`, config.token);
    if (!guild.ok) {
        if (guild.status === 403) die('the bot is in the server but cannot read it (403): re-invite it with the bot scope.', 1);
        die(`GET /guilds/${channel.data.guild_id} failed with HTTP ${guild.status}`, 1);
    }
    report.guildName = guild.data.name;

    const member = await api(config.apiBase,
        `/guilds/${channel.data.guild_id}/members/${me.data.id}`, config.token);
    if (!member.ok) die('the bot is not a member of that server: invite it first.', 1);

    const mask = permissionsOf(channel.data, guild.data, member.data, me.data.id);
    report.canRead = has(mask, 'VIEW_CHANNEL') && has(mask, 'READ_MESSAGE_HISTORY');
    report.canPublish = report.canRead && has(mask, 'SEND_MESSAGES') && has(mask, 'ATTACH_FILES');

    // The app's real request: the newest messages of the channel.
    const history = await api(config.apiBase, `/channels/${config.channel}/messages?limit=50`, config.token);
    if (!history.ok) {
        fail(history.status === 403
            ? 'the bot cannot read message history of that channel (403): enable Read Message History.'
            : `reading the channel failed with HTTP ${history.status}.`);
    } else {
        // Both announcement styles count: a manifest.json attachment, or a
        // plain message that names a version and an https link.
        const manifestMessage = history.data.find((message) =>
            (message.attachments ?? []).some((a) => a.filename === 'manifest.json'));
        const textRelease = history.data.find((message) =>
            /\bv?\d+\.\d+(\.\d+){0,2}\b/i.test(message.content ?? '')
            && /https:\/\/\S+/i.test(message.content ?? ''));
        report.manifestFound = Boolean(manifestMessage);
        report.releaseMessage = (manifestMessage ?? textRelease)?.id ?? null;
        if (!manifestMessage && !textRelease) {
            warn('no release in the last 50 messages yet - post the version and the download link.');
        } else if (!manifestMessage) {
            warn(`the newest release is a plain message (${textRelease.id}); it works, but it `
                + 'carries no SHA-256 unless you add a "sha256: ..." line.');
        } else {
            report.messageId = manifestMessage.id;
        }
    }
    if (!report.canPublish) {
        warn('the bot cannot post files here, so publish.mjs will need a token with Send Messages + Attach Files (or a webhook).');
    }

    report.ok = report.problems.length === 0 && Boolean(report.canRead);

    const updater = {
        apiBase: config.apiBase,
        channelId: config.channel,
        botToken: '<the same token, or a read-only one>',
        manifest: 'manifest.json',
        hmacKey: '',
        enabled: true,
    };

    if (args.json) {
        console.log(JSON.stringify({ ...report, updater }, null, 2));
        process.exit(report.ok ? 0 : 1);
    }

    console.log(`bot       ${report.bot.username} (${report.bot.id})`);
    console.log(`server    ${report.guildName} (${report.guildId})`);
    console.log(`channel   #${report.channelName} (${config.channel})`);
    console.log(`read      ${report.canRead ? 'yes' : 'NO'}  (View Channel + Read Message History)`);
    console.log(`publish   ${report.canPublish ? 'yes' : 'no'}`);
    for (const problem of report.problems) console.log(`PROBLEM   ${problem}`);
    for (const warning of report.warnings) console.log(`note      ${warning}`);
    console.log('');
    console.log(config.authFile
        ? `Checked ${config.authFile}. This is the block the app needs in it:`
        : 'Put this in auth.json (next to the EXE, or in the ZLP_AUTH_JSON secret):');
    console.log('');
    console.log(JSON.stringify({ updater }, null, 2));
    console.log('');
    console.log(report.ok
        ? 'The application can update from this channel.'
        : 'Something above has to be fixed before the updater can work.');
    process.exit(report.ok ? 0 : 1);
}

main().catch((error) => die(String(error?.message ?? error), 1));
