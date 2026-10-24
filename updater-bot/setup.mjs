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
            case '--json': args.json = true; break;
            case '--help': case '-h':
                console.log(`BSI Customizer - Discord updater setup check

  DISCORD_BOT_TOKEN=... node updater-bot/setup.mjs --channel <channelId> [--json]

Checks the token, the server, the channel and the read/write permissions the
app and publish.mjs need, then prints the auth.json block to paste. Changes
nothing on Discord. Exit 0 when the app can update from that channel.`);
                process.exit(0);
                break;
            default: die(`unknown option ${argv[i]}`);
        }
    }
    return args;
}

async function loadConfig(args) {
    let file = {};
    if (existsSync(path.join(HERE, 'config.json'))) {
        try {
            file = JSON.parse(await readFile(path.join(HERE, 'config.json'), 'utf8'));
        } catch (error) {
            die(`updater-bot/config.json is not valid JSON: ${error.message}`);
        }
    }
    const token = args.token || process.env.DISCORD_BOT_TOKEN || process.env.DISCORD_PUBLISH_TOKEN
        || file.botToken;
    const channel = args.channel || process.env.DISCORD_CHANNEL_ID
        || process.env.DISCORD_UPDATER_CHANNEL_ID || file.channelId;
    if (!token) die('no bot token: pass --token or set DISCORD_BOT_TOKEN');
    if (!channel) die('no channel id: pass --channel or set DISCORD_CHANNEL_ID');
    if (!/^\d{5,25}$/.test(channel)) die(`"${channel}" is not a Discord channel ID (it is a number)`);
    const apiBase = (process.env.DISCORD_API_BASE || file.apiBase || 'https://discord.com/api/v10')
        .replace(/\/+$/, '');
    return { token: token.replace(/^bot\s+/i, '').trim(), channel, apiBase };
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

async function main() {
    const args = parseArgs(process.argv.slice(2));
    const config = await loadConfig(args);

    const report = { ok: false, channel: config.channel, problems: [], warnings: [] };
    const fail = (text) => report.problems.push(text);
    const warn = (text) => report.warnings.push(text);

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
        const manifestMessage = history.data.find((message) =>
            (message.attachments ?? []).some((a) => a.filename === 'manifest.json'));
        report.manifestFound = Boolean(manifestMessage);
        if (!manifestMessage) {
            warn('no manifest.json attachment in the last 50 messages yet - publish a build first.');
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
    console.log('Put this in auth.json (next to the EXE, or in the ZLP_AUTH_JSON secret):');
    console.log('');
    console.log(JSON.stringify({ updater }, null, 2));
    console.log('');
    console.log(report.ok
        ? 'The application can update from this channel.'
        : 'Something above has to be fixed before the updater can work.');
    process.exit(report.ok ? 0 : 1);
}

main().catch((error) => die(String(error?.message ?? error), 1));
