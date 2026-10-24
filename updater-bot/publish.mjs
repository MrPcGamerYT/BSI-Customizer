#!/usr/bin/env node
// ---------------------------------------------------------------------------
// BSI Customizer - Discord releases publisher
//
// The repository is private, so a released EXE has nowhere public to ask "is
// there a newer build?" and a GitHub token cannot be shipped inside it.
// Discord solves both halves:
//
//   * this script uploads the built EXE and a tiny manifest.json to one channel
//     of your server and leaves exactly one "latest build" message behind;
//   * the application (src/core/UpdateChecker.cpp) reads that message with a
//     read-only bot token, compares the version, verifies size + SHA-256 and
//     replaces itself.
//
// Usage
//   node updater-bot/publish.mjs --exe build/artifacts/BSI-Customizer-2.0.1-win64.exe \
//        --version 2.0.1 --notes-file notes.md
//
// Options
//   --exe <path>            the portable EXE to publish (required)
//   --version <2.0.1>       default: read from the file name
//   --notes <text>          release notes (plain text, Discord markdown ok)
//   --notes-file <path>     read the notes from a file instead
//   --min-supported <ver>   older builds must update even after "Later"
//   --mandatory             same effect, shown as required in the UI
//   --url <https://...>     do not upload the EXE; point the manifest at this
//                           https link instead (for files over Discord's cap)
//   --channel <id>          override the channel ID
//   --manifest <name>       manifest attachment name (default manifest.json)
//   --dry-run               print the manifest, touch nothing
//   --quiet                 only print the result
//
// Configuration
//   updater-bot/config.json (git-ignored) or environment variables:
//     DISCORD_CHANNEL_ID      the releases channel
//     DISCORD_BOT_TOKEN       a bot that may post there, or
//     DISCORD_WEBHOOK_URL     a channel webhook (posts without a bot token)
//     BSI_UPDATE_HMAC_KEY     optional: sign the manifest (must match auth.json)
//     DISCORD_API_BASE        optional, default https://discord.com/api/v10
//
// Node 18+ (global fetch/FormData). No dependencies, nothing to install.
// ---------------------------------------------------------------------------

import { createHash, createHmac } from 'node:crypto';
import { existsSync } from 'node:fs';
import { readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const STATE_FILE = path.join(HERE, '.state.json');

function die(message) {
    console.error(`error: ${message}`);
    process.exit(1);
}

function info(message) {
    if (!process.env.BSI_PUBLISH_QUIET) console.log(message);
}

const USAGE = `BSI Customizer - Discord releases publisher

  node updater-bot/publish.mjs --exe <BSI-Customizer-X.Y.Z-win64.exe> [options]
  node updater-bot/publish.mjs --version 2.1.0 --url <https link>   (EXE already uploaded)

  --version <ver>        default: from the file name
  --notes <text>         release notes
  --notes-file <path>    release notes from a file
  --min-supported <ver>  builds below this must update
  --mandatory            mark the update as required
  --url <https://...>    publish only the manifest, EXE lives at this link
                         (pCloud, Drive, S3, any https host - it is verified)
  --skip-verify          do not fetch --url to check it (offline publishing)
  --manifest <name>      manifest attachment name (default manifest.json)
  --dry-run              print the manifest and exit
  --quiet                only print the result

Environment / updater-bot/config.json: DISCORD_CHANNEL_ID, DISCORD_BOT_TOKEN
(or DISCORD_WEBHOOK_URL), BSI_UPDATE_HMAC_KEY, DISCORD_API_BASE.
See updater-bot/README.md.`;

function parseArgs(argv) {
    const args = { notes: undefined, mandatory: false, dryRun: false };
    for (let i = 0; i < argv.length; i += 1) {
        const arg = argv[i];
        const next = () => {
            if (i + 1 >= argv.length) die(`${arg} needs a value`);
            return argv[++i];
        };
        switch (arg) {
            case '--exe': args.exe = next(); break;
            case '--version': args.version = next(); break;
            case '--notes': args.notes = next(); break;
            case '--notes-file': args.notesFile = next(); break;
            case '--min-supported': args.minSupported = next(); break;
            case '--url': args.url = next(); break;
            case '--channel': args.channel = next(); break;
            case '--manifest': args.manifest = next(); break;
            case '--skip-verify': args.skipVerify = true; break;
            case '--mandatory': args.mandatory = true; break;
            case '--dry-run': args.dryRun = true; break;
            case '--quiet': args.quiet = true; break;
            case '--help': case '-h': console.log(USAGE); process.exit(0); break;
            default: die(`unknown option ${arg}\n\n${USAGE}`);
        }
    }
    if (args.quiet) process.env.BSI_PUBLISH_QUIET = '1';
    return args;
}

async function loadConfig(args, required = true) {
    let file = {};
    if (existsSync(path.join(HERE, 'config.json'))) {
        try {
            file = JSON.parse(await readFile(path.join(HERE, 'config.json'), 'utf8'));
        } catch (error) {
            die(`updater-bot/config.json is not valid JSON: ${error.message}`);
        }
    }
    const env = process.env;
    const config = {
        apiBase: (env.DISCORD_API_BASE || file.apiBase || 'https://discord.com/api/v10').replace(/\/+$/, ''),
        channelId: args.channel || env.DISCORD_CHANNEL_ID || env.DISCORD_UPDATER_CHANNEL_ID || file.channelId,
        botToken: env.DISCORD_PUBLISH_TOKEN || env.DISCORD_BOT_TOKEN || file.botToken,
        webhookUrl: env.DISCORD_WEBHOOK_URL || file.webhookUrl,
        hmacKey: env.BSI_UPDATE_HMAC_KEY || file.hmacKey,
        manifestName: args.manifest || file.manifest || 'manifest.json',
    };
    if (config.botToken && config.botToken.toLowerCase().startsWith('bot ')) {
        config.botToken = config.botToken.slice(4).trim();
    }
    config.useWebhook = Boolean(config.webhookUrl);
    if (!config.useWebhook && (!config.channelId || !config.botToken)) {
        if (required) {
            die('need DISCORD_CHANNEL_ID + DISCORD_BOT_TOKEN (or DISCORD_WEBHOOK_URL).\n'
                + 'Set them in updater-bot/config.json or the environment - see updater-bot/README.md.');
        }
        config.channelId = config.channelId || '(not configured)';
        config.botToken = config.botToken || '';
    }
    if (config.useWebhook && !config.channelId) {
        // The webhook URL already names the channel; only used for reporting.
        config.channelId = 'webhook';
    }
    return config;
}

async function api(route, { method = 'GET', token, body, absolute = false } = {}) {
    const url = absolute ? route : `${process.env.DISCORD_API_BASE?.replace(/\/+$/, '') || 'https://discord.com/api/v10'}${route}`;
    const headers = { 'User-Agent': 'BSI-Customizer-publisher/1.0 (+https://github.com/MrPcGamerYT/BSI-Customizer)' };
    let payload = body;
    if (token) headers.Authorization = token.toLowerCase().startsWith('bot ') ? token : `Bot ${token}`;
    if (body && !(body instanceof FormData)) {
        headers['Content-Type'] = 'application/json';
        payload = JSON.stringify(body);
    }

    for (let attempt = 0; attempt < 3; attempt += 1) {
        const response = await fetch(url, { method, headers, body: payload });
        const text = await response.text();
        let data = null;
        try {
            data = text ? JSON.parse(text) : null;
        } catch {
            data = text;
        }
        if (response.status === 429) {
            const wait = Number(data?.retry_after ?? 1);
            await new Promise((resolve) => setTimeout(resolve, Math.ceil(wait * 1000) + 250));
            continue;
        }
        if (!response.ok) {
            const detail = typeof data === 'string' ? data : (data?.message ?? '');
            throw new Error(`${method} ${redact(url)} -> HTTP ${response.status}${detail ? `: ${detail}` : ''}`);
        }
        return data;
    }
    throw new Error(`${method} ${redact(url)} kept returning 429 (rate limited)`);
}

// Never print a webhook token by accident.
function redact(url) {
    return String(url).replace(/\/webhooks\/(\d+)\/[A-Za-z0-9._-]+/g, '/webhooks/$1/***');
}

function signedText(manifest) {
    return ['bsi-update-v1', manifest.version, manifest.file, String(manifest.size), manifest.sha256]
        .join('\n') + '\n';
}

function sign(manifest, key) {
    return createHmac('sha256', key).update(signedText(manifest), 'utf8').digest('base64');
}

function versionFromName(fileName) {
    const match = /(\d+\.\d+\.\d+(?:\.\d+)?)/.exec(fileName);
    return match ? match[1] : null;
}

function humanSize(bytes) {
    return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
}

async function readNotes(args) {
    if (args.notesFile) {
        const text = await readFile(args.notesFile, 'utf8');
        return text.trim();
    }
    return (args.notes ?? '').trim();
}

async function loadState() {
    if (!existsSync(STATE_FILE)) return {};
    try {
        return JSON.parse(await readFile(STATE_FILE, 'utf8'));
    } catch {
        return {};
    }
}

async function saveState(state) {
    try {
        await writeFile(STATE_FILE, `${JSON.stringify(state, null, 2)}\n`, 'utf8');
    } catch (error) {
        info(`warning: could not write ${STATE_FILE}: ${error.message}`);
    }
}

function messagePayload(manifest, notes) {
    const lines = [`**BSI Customizer ${manifest.version}**`];
    if (manifest.mandatory) lines.push('*This update is required.*');
    if (notes) lines.push('', notes.slice(0, 1800));
    if (!manifest.url) lines.push('', '`manifest.json` and the portable EXE are attached.');
    return {
        content: lines.join('\n').slice(0, 1900),
        allowed_mentions: { parse: [] },
    };
}

function filesFor(manifest, args, bytes) {
    const form = new FormData();
    form.append('files[0]',
        new Blob([JSON.stringify(manifest, null, 2)], { type: 'application/json' }),
        args.manifestName);
    if (bytes) {
        form.append('files[1]',
            new Blob([bytes], { type: 'application/octet-stream' }),
            manifest.file);
    }
    return form;
}

async function publishWithBot(config, args, manifest, bytes, payload) {
    const me = await api('/users/@me', { token: config.botToken });
    info(`publishing as ${me.username} (${me.id}) to channel ${config.channelId}`);

    const history = await api(`/channels/${config.channelId}/messages?limit=100`, { token: config.botToken });
    const previous = (Array.isArray(history) ? history : []).filter((message) =>
        message.author?.id === me.id
        && (message.attachments ?? []).some((attachment) => attachment.filename === config.manifestName));

    const form = filesFor(manifest, args, bytes);
    form.append('payload_json', JSON.stringify(payload));
    const posted = await api(`/channels/${config.channelId}/messages`, {
        method: 'POST',
        token: config.botToken,
        body: form,
    });

    for (const old of previous) {
        if (old.id === posted.id) continue;
        try {
            await api(`/channels/${config.channelId}/messages/${old.id}`, { method: 'DELETE', token: config.botToken });
            info(`removed the previous release message ${old.id}`);
        } catch (error) {
            info(`warning: could not remove the previous release message ${old.id}: ${error.message}`);
        }
    }

    try {
        await api(`/channels/${config.channelId}/pins/${posted.id}`, { method: 'PUT', token: config.botToken });
        info('pinned the release message');
    } catch (error) {
        info(`warning: the message is published but could not be pinned (Manage Messages): ${error.message}`);
    }
    return posted.id;
}

async function publishWithWebhook(config, args, manifest, bytes, payload) {
    const state = await loadState();
    const form = filesFor(manifest, args, bytes);
    form.append('payload_json', JSON.stringify(payload));
    const posted = await api(`${config.webhookUrl}?wait=true`, {
        method: 'POST',
        body: form,
        absolute: true,
    });

    const previous = state.webhookMessageId;
    if (previous && previous !== posted.id) {
        try {
            await api(`${config.webhookUrl}/messages/${previous}`, { method: 'DELETE', absolute: true });
            info(`removed the previous release message ${previous}`);
        } catch (error) {
            info(`warning: could not remove the previous release message ${previous}: ${error.message}`);
        }
    }
    state.webhookMessageId = posted.id;
    await saveState(state);
    return posted.id;
}


// ---------------------------------------------------------------------------
// Hosting the EXE somewhere else (pCloud, Drive, S3, ...)
//
// A 30 MB EXE does not fit a Discord message on an unboosted server, so the
// manifest may carry `url` instead of the file. Share links from pCloud are
// web pages, not files, so this resolves the page exactly like the application
// does (src/core/UpdateChecker.cpp) and then checks that the hosted file really
// is this build: same byte count, same SHA-256.
// ---------------------------------------------------------------------------

const DOWNLOAD_KEYS = [
    'downloadlink', 'downloadLink', 'downloadurl', 'downloadUrl',
    'directlink', 'directLink', 'filelink', 'fileLink', 'fileurl', 'fileUrl',
];

function unescapePageValue(value) {
    return value
        .replace(/\\\//g, '/')
        .replace(/\\u002f/gi, '/')
        .replace(/&amp;/g, '&')
        .replace(/&#x2f;/gi, '/')
        .replace(/&#47;/g, '/')
        .trim();
}

function isHttpsUrl(text) {
    try {
        return new URL(text).protocol === 'https:';
    } catch {
        return false;
    }
}

function resolveDownloadLink(page, baseUrl) {
    for (const key of DOWNLOAD_KEYS) {
        const pattern = new RegExp(`"${key}"\\s*:\\s*"([^"]{8,4096})"`, 'i');
        const match = pattern.exec(page);
        if (!match) continue;
        const candidate = unescapePageValue(match[1]);
        if (isHttpsUrl(candidate)) return candidate;
    }
    const href = /href\s*=\s*["']([^"']+\.(?:exe|zip|7z|msi|bin)(?:\?[^"']*)?)["']/gi;
    for (const match of page.matchAll(href)) {
        const candidate = unescapePageValue(match[1]);
        try {
            const absolute = new URL(candidate, baseUrl).toString();
            if (isHttpsUrl(absolute)) return absolute;
        } catch { /* not a URL, keep looking */ }
    }
    const bare = /https:\/\/[^"'\s<>()]+\.(?:exe|zip)(?:\?[^"'\s<>()]*)?/i.exec(page);
    return bare && isHttpsUrl(bare[0]) ? bare[0] : null;
}

function looksLikeHtml(buffer) {
    const head = buffer.subarray(0, 1024).toString('latin1').replace(/^\uFEFF/, '').trim().toLowerCase();
    if (head.startsWith('mz') || head.startsWith('pk')) return false;
    return head.startsWith('<!doctype html') || head.startsWith('<html') || head.startsWith('<head')
        || head.startsWith('<?xml') || (head.startsWith('<') && head.includes('</'));
}

async function fetchHosted(url, { maxBytes = 300 * 1024 * 1024 } = {}) {
    const response = await fetch(url, {
        redirect: 'follow',
        headers: {
            'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) BSI-Customizer-publisher/1.0',
            Accept: '*/*',
        },
    });
    if (!response.ok) {
        throw new Error(`HTTP ${response.status} from ${url}`);
    }
    const declared = Number(response.headers.get('content-length') ?? 0);
    if (declared > maxBytes) throw new Error(`${declared} bytes is larger than expected`);
    return { buffer: Buffer.from(await response.arrayBuffer()), contentType: response.headers.get('content-type') ?? '' };
}

async function verifyHostedUrl(url, expectedSize = 0, expectedSha = '') {
    let current = url;
    for (let hop = 0; hop < 3; hop += 1) {
        const { buffer, contentType } = await fetchHosted(current);
        const html = contentType.includes('text/html') || looksLikeHtml(buffer);
        if (!html) {
            const sha = createHash('sha256').update(buffer).digest('hex');
            const ok = (!expectedSha || sha === expectedSha)
                && (!expectedSize || buffer.length === expectedSize);
            const fileName = decodeURIComponent(new URL(current).pathname.split('/').pop() || '');
            return {
                ok,
                url: current,
                size: buffer.length,
                sha256: sha,
                buffer,
                fileName: fileName.toLowerCase().endsWith('.exe')
                    ? fileName
                    : `BSI-Customizer-${sha.slice(0, 8)}-win64.exe`,
            };
        }
        const resolved = resolveDownloadLink(buffer.toString('utf8'), current);
        if (!resolved) {
            return {
                ok: false,
                url: current,
                reason: 'the link opened a web page that has no download link on it'
                    + (/pcloud/i.test(new URL(url).host)
                        ? ' - on pCloud put the EXE in the Public folder and use Share > Direct link'
                        : ''),
            };
        }
        info(`  ${new URL(current).host} answered with a page, following it to ${new URL(resolved).host}`);
        current = resolved;
    }
    return { ok: false, url: current, reason: 'still a web page after three hops' };
}

async function main() {
    const args = parseArgs(process.argv.slice(2));
    if (Number(process.versions.node.split('.')[0]) < 18) {
        die(`Node 18 or newer is required (found ${process.versions.node}); fetch/FormData are built in.`);
    }
    if (!args.exe && !args.url) {
        die(`--exe <file> (or --version + --url when the EXE is already uploaded)\n\n${USAGE}`);
    }

    const config = await loadConfig(args, !args.dryRun);
    args.manifestName = config.manifestName;
    if (args.url && !/^https:\/\//i.test(args.url)) die('--url must be an https:// link');

    // The EXE can come from the build folder or straight from the host it was
    // uploaded to: `--version 2.1.0 --url <pCloud link>` is the one-line form.
    let bytes = null;
    let manifestFile = null;
    if (args.exe) {
        const exePath = path.resolve(args.exe);
        if (!existsSync(exePath)) die(`no such file: ${exePath}`);
        if (!exePath.toLowerCase().endsWith('.exe')) die('--exe must be the portable .exe');
        bytes = await readFile(exePath);
        manifestFile = path.basename(exePath);
    }

    let hosted = null;
    if (args.url && !args.skipVerify) {
        try {
            hosted = await verifyHostedUrl(args.url, 0, '');
        } catch (error) {
            die(`could not read the hosted file: ${error.message}\n`
                + 'Check that the link is public and downloads the EXE without a page in between,'
                + ' or publish with --skip-verify and test with --check-updates --download.');
        }
        if (!hosted.buffer) {
            die(`the hosted link does not serve a file: ${hosted.reason ?? 'no download link'}\n`
                + 'Upload the exact EXE you are publishing, or pass --skip-verify.');
        }
        if (!bytes) {
            bytes = hosted.buffer;
            manifestFile = hosted.fileName;
        }
    }

    if (!bytes) die('no EXE to publish: pass --exe, or a --url the publisher can download');
    if (!manifestFile) manifestFile = args.version
        ? `BSI-Customizer-${args.version}-win64.exe`
        : 'BSI-Customizer-win64.exe';

    const version = args.version || versionFromName(manifestFile)
        || (args.url ? versionFromName(decodeURIComponent(args.url)) : null);
    if (!version) die('cannot read the version from the file name - pass --version 2.0.1');
    if (!/^\d+\.\d+\.\d+(\.\d+)?$/.test(version)) die(`"${version}" is not a version like 2.0.1`);
    if (args.minSupported && !/^\d+\.\d+\.\d+(\.\d+)?$/.test(args.minSupported)) {
        die('--min-supported must be a version like 2.0.0');
    }

    // Only reachable in the --url form: name the file the way the app expects
    // instead of whatever the host called it.
    if (!/bsi/i.test(manifestFile)) manifestFile = `BSI-Customizer-${version}-win64.exe`;

    const size = bytes.length;
    const sha256 = createHash('sha256').update(bytes).digest('hex');
    const notes = await readNotes(args);
    const manifest = {
        schema: 1,
        app: 'BSI Customizer',
        version,
        file: manifestFile,
        size,
        sha256,
        released: new Date().toISOString().replace(/\.\d{3}Z$/, 'Z'),
        mandatory: Boolean(args.mandatory),
    };
    if (args.minSupported) manifest.minSupported = args.minSupported;
    if (args.url) manifest.url = args.url;
    if (notes) manifest.notes = notes;
    if (config.hmacKey) manifest.signature = sign(manifest, config.hmacKey);

    info(`BSI Customizer ${version}`);
    info(`  file     ${manifestFile} (${humanSize(size)})`);
    info(`  sha256   ${sha256}`);
    info(`  channel  ${config.channelId}${config.useWebhook ? ' (webhook)' : ' (bot)'}`);
    info(`  signed   ${config.hmacKey ? 'yes (HMAC-SHA256)' : 'no - set BSI_UPDATE_HMAC_KEY to sign'}`);

    // The manifest promises size + SHA-256; make sure the host really serves
    // exactly that file before every installed copy is pointed at it.
    if (args.url) {
        if (args.skipVerify) {
            info('  hosted   not checked (--skip-verify)');
        } else {
            const hostedSize = hosted.buffer.length;
            const hostedSha = createHash('sha256').update(hosted.buffer).digest('hex');
            if (hostedSize !== size || hostedSha !== sha256) {
                die(`the hosted link does not serve this build:\n`
                    + `  expected ${size} bytes, sha256 ${sha256}\n`
                    + `  got      ${hostedSize} bytes, sha256 ${hostedSha}\n`
                    + 'Upload the exact file you are publishing, or pass --skip-verify.');
            }
            info(`  hosted   ${hosted.url}`);
            info(`  verified ${humanSize(hostedSize)}, SHA-256 matches the published EXE`);
        }
    }

    if (args.dryRun) {
        info('dry run: nothing was posted to Discord.');
        console.log(JSON.stringify(manifest, null, 2));
        return;
    }

    const payload = messagePayload(manifest, notes);
    const messageId = config.useWebhook
        ? await publishWithWebhook(config, args, manifest, args.url ? null : bytes, payload)
        : await publishWithBot(config, args, manifest, args.url ? null : bytes, payload);

    info(`published message ${messageId}`);
    console.log(`BSI Customizer ${version} is live for the updater (message ${messageId}).`);
}

main().catch((error) => {
    const message = String(error?.message ?? error);
    if (/HTTP 413|Request entity too large|too large/i.test(message)) {
        console.error('error: Discord refused the upload because it is larger than this server allows.');
        console.error('       Free servers cap uploads at 10 MB. Either boost the server,');
        console.error('       or host the EXE elsewhere and publish only the manifest:');
        console.error('         node updater-bot/publish.mjs --exe <file> --url https://your-host/BSI-Customizer.exe');
        process.exit(1);
    }
    die(message);
});
