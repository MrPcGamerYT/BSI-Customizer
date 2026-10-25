// ---------------------------------------------------------------------------
// Discord Rich Presence command line (BSI-Customizer --rpcid ...)
//
// The same native engines the UI uses (RpcIdScan / RpcIdPatcher /
// VersionResource) exposed as a scriptable command line. It replaces the old
// standalone reference script: the shipped EXE is the only thing required, so
// no interpreter, sidecar file or extra download is involved, and both the UI
// and the CLI read and write exactly the same `<file>.rpcid-bak` backup and
// `<file>.rpcid.json` patch state.
//
// Exit codes: 0 success, 1 the operation failed, 2 the command line is wrong.
// ---------------------------------------------------------------------------

#include "core/RpcIdPatcher.h"
#include "core/UpdateChecker.h"

#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <cstdio>

// Implemented in selftest.cpp: the whole engine suite, the same one CI runs.
int runSelfTest();

namespace {

void line(const QString &text)
{
    std::printf("%s\n", qPrintable(text));
}

int usage(const QString &problem = QString())
{
    if (!problem.isEmpty())
        line(QStringLiteral("error: %1\n").arg(problem));
    line(QStringLiteral(
        "BSI Customizer — Discord Rich Presence command line\n"
        "\n"
        "  BSI-Customizer.exe --rpcid find    <file-or-folder>\n"
        "  BSI-Customizer.exe --rpcid status  <file> [--json]\n"
        "  BSI-Customizer.exe --rpcid replace <file> --new <id> [--old <id>]... [--all] [--dry-run] [--yes]\n"
        "  BSI-Customizer.exe --rpcid undo    <file>\n"
        "  BSI-Customizer.exe --rpcid name    <file> [--set <text>] [--key <name>] [--dry-run] [--yes]\n"
        "  BSI-Customizer.exe --rpcid selftest\n"
        "\n"
        "Options\n"
        "  --new <id>    the Discord Application ID to write (17-20 digits)\n"
        "  --old <id>    the ID currently in the file; repeat to patch several\n"
        "  --all         allow more than one detected ID to be replaced at once\n"
        "  --set <text>  new Windows version-resource text for `name`\n"
        "  --key <name>  which version-resource key to set (default:\n"
        "                FileDescription and ProductName)\n"
        "  --dry-run     report what would change; write nothing\n"
        "  --yes, -y     confirm a write (required: this is a GUI binary, so it\n"
        "                never stops to ask on the console)\n"
        "  --json        machine-readable output for `status`\n"
        "\n"
        "Close the emulator before writing. The first backup is kept next to the\n"
        "file as <file>.rpcid-bak; `undo` restores it. Everything runs offline."));
    return problem.isEmpty() ? 0 : 2;
}

struct Options
{
    QString command;
    QString path;
    QString newId;
    QStringList oldIds;
    QString set;
    QString key;
    bool hasSet = false;
    bool all = false;
    bool dryRun = false;
    bool yes = false;
    bool json = false;
};

/** Report a Result the way the UI log does: one status line per line. */
int report(const RpcIdPatcher::Result &result)
{
    if (!result.msg.isEmpty())
        line(result.msg);
    return result.ok ? 0 : 1;
}

int commandStatus(const Options &options)
{
    const RpcIdPatcher::Status status = RpcIdPatcher::status(options.path);
    if (!options.json) {
        line(status.msg);
        if (!status.known.isEmpty())
            line(QStringLiteral("Known: %1").arg(status.known));
        if (!status.originalId.isEmpty())
            line(QStringLiteral("Original ID: %1").arg(status.originalId));
        if (!status.leftovers.isEmpty())
            line(QStringLiteral("Older copies still present: %1").arg(status.leftovers.join(QStringLiteral(", "))));
        for (const RpcIdPatcher::CandidateInfo &candidate : status.candidates) {
            line(QStringLiteral("Candidate %1 — score %2, %3 occurrence(s), created %4%5")
                     .arg(candidate.id).arg(candidate.score).arg(candidate.refs)
                     .arg(candidate.date, candidate.known.isEmpty() ? QString()
                                                                    : QStringLiteral(" (%1)").arg(candidate.known)));
        }
        line(QStringLiteral("Backup: %1").arg(status.hasBackup ? QStringLiteral("yes") : QStringLiteral("no")));
        return status.ok ? 0 : 1;
    }

    QJsonArray candidates;
    for (const RpcIdPatcher::CandidateInfo &candidate : status.candidates) {
        candidates.append(QJsonObject{
            { QStringLiteral("id"), candidate.id },
            { QStringLiteral("score"), candidate.score },
            { QStringLiteral("refs"), candidate.refs },
            { QStringLiteral("date"), candidate.date },
            { QStringLiteral("confident"), candidate.confident },
            { QStringLiteral("known"), candidate.known.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                                 : QJsonValue(candidate.known) }
        });
    }
    QJsonArray leftovers;
    for (const QString &id : status.leftovers)
        leftovers.append(id);
    const QJsonObject object{
        { QStringLiteral("ok"), status.ok },
        { QStringLiteral("path"), options.path },
        { QStringLiteral("patched"), status.patched },
        { QStringLiteral("current_id"), status.currentId.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                                   : QJsonValue(status.currentId) },
        { QStringLiteral("original_id"), status.originalId.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                                     : QJsonValue(status.originalId) },
        { QStringLiteral("known"), status.known.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                          : QJsonValue(status.known) },
        { QStringLiteral("name"), QJsonValue(QJsonValue::Null) },  // offline tool: no lookups
        { QStringLiteral("msg"), status.msg },
        { QStringLiteral("leftovers"), leftovers },
        { QStringLiteral("candidates"), candidates },
        { QStringLiteral("backup"), status.hasBackup }
    };
    std::printf("%s\n", QJsonDocument(object).toJson(QJsonDocument::Compact).constData());
    return status.ok ? 0 : 1;
}

int commandName(const Options &options)
{
    const RpcIdPatcher::Names names = RpcIdPatcher::names(options.path);
    if (!names.ok) {
        line(names.msg);
        return 1;
    }
    line(QStringLiteral("Names inside %1").arg(QFileInfo(options.path).fileName()));
    for (const RpcIdPatcher::NameEntry &entry : names.entries) {
        line(QStringLiteral("  %1 = \"%2\" (max %3 characters)")
                 .arg(entry.key, entry.value).arg(entry.max));
    }
    if (!options.hasSet) {
        line(QStringLiteral("The name Discord itself shows comes from the Discord application, "
                            "not from this Windows version resource."));
        return 0;
    }
    if (!options.dryRun && !options.yes) {
        const RpcIdPatcher::Result preview =
            RpcIdPatcher::setName(options.path, options.set, options.key, true);
        const int code = report(preview);
        if (code == 0)
            line(QStringLiteral("Add --yes to write this change."));
        return code == 0 ? 1 : code;
    }
    return report(RpcIdPatcher::setName(options.path, options.set, options.key, options.dryRun));
}

} // namespace

int runRpcIdCli(const QStringList &arguments)
{
    Options options;
    QStringList positional;
    for (int i = 0; i < arguments.size(); ++i) {
        const QString argument = arguments.at(i);
        const auto value = [&]() -> QString {
            return (i + 1 < arguments.size()) ? arguments.at(++i) : QString();
        };
        if (argument == QLatin1String("--new")) {
            options.newId = value();
            if (options.newId.isEmpty()) return usage(QStringLiteral("--new needs an Application ID"));
        } else if (argument == QLatin1String("--old")) {
            const QString old = value();
            if (old.isEmpty()) return usage(QStringLiteral("--old needs an Application ID"));
            options.oldIds << old;
        } else if (argument == QLatin1String("--set")) {
            if (i + 1 >= arguments.size()) return usage(QStringLiteral("--set needs a text"));
            options.set = arguments.at(++i);
            options.hasSet = true;
        } else if (argument == QLatin1String("--key")) {
            options.key = value();
            if (options.key.isEmpty()) return usage(QStringLiteral("--key needs a name"));
        } else if (argument == QLatin1String("--all")) {
            options.all = true;
        } else if (argument == QLatin1String("--dry-run")) {
            options.dryRun = true;
        } else if (argument == QLatin1String("--json")) {
            options.json = true;
        } else if (argument == QLatin1String("--yes") || argument == QLatin1String("-y")) {
            options.yes = true;
        } else if (argument == QLatin1String("--help") || argument == QLatin1String("-h")
                   || argument == QLatin1String("help")) {
            return usage();
        } else if (argument.startsWith(QLatin1String("-"))) {
            return usage(QStringLiteral("unknown option %1").arg(argument));
        } else {
            positional << argument;
        }
    }
    if (positional.isEmpty())
        return usage();
    options.command = positional.takeFirst();
    if (!positional.isEmpty())
        options.path = positional.takeFirst();
    if (!positional.isEmpty())
        return usage(QStringLiteral("unexpected argument %1").arg(positional.first()));

    const QStringList needPath{ QStringLiteral("find"), QStringLiteral("status"),
                                QStringLiteral("replace"), QStringLiteral("undo"),
                                QStringLiteral("name") };
    if (needPath.contains(options.command) && options.path.isEmpty())
        return usage(QStringLiteral("%1 needs a file").arg(options.command));

    if (options.command == QLatin1String("selftest"))
        return runSelfTest();
    if (options.command == QLatin1String("find"))
        return report(RpcIdPatcher::find(options.path));
    if (options.command == QLatin1String("status"))
        return commandStatus(options);
    if (options.command == QLatin1String("undo"))
        return report(RpcIdPatcher::undo(options.path));
    if (options.command == QLatin1String("name"))
        return commandName(options);
    if (options.command == QLatin1String("replace")) {
        if (options.newId.isEmpty())
            return usage(QStringLiteral("replace needs --new <Application ID>"));
        if (!options.dryRun && !options.yes) {
            // Preview first, then say exactly what is missing: a GUI-subsystem
            // binary has no reliable console prompt to confirm on.
            const int previewed = report(RpcIdPatcher::replace(options.path, options.newId,
                                                               options.oldIds, true, options.all));
            if (previewed == 0)
                line(QStringLiteral("Add --yes to write this change."));
            return previewed == 0 ? 1 : previewed;
        }
        return report(RpcIdPatcher::replace(options.path, options.newId, options.oldIds,
                                            options.dryRun, options.all));
    }
    return usage(QStringLiteral("unknown command %1").arg(options.command));
}

// ---------------------------------------------------------------------------
// Self-update probe (BSI-Customizer --check-updates)
//
// Reads the Discord releases channel exactly like the window's updater does, so
// the bot token + channel ID setup can be verified from a terminal (or CI)
// without opening the UI. Exit codes: 0 up to date, 1 the check failed, 2 the
// updater is not configured / the command line is wrong, 3 an update is
// available, 4 an update was downloaded and verified (--download).
//
// --explain prints what the bot actually received, one line per message. It is
// there for the one failure Discord reports silently: an app without the
// Message Content intent gets every message with its text and attachments
// stripped, which looks exactly like an empty channel from the outside.
// ---------------------------------------------------------------------------

namespace {

int runUpdateCommand(const QStringList &arguments)
{
    bool json = false;
    bool wantDownload = false;
    bool explain = false;
    int timeoutSeconds = 30;
    for (int i = 0; i < arguments.size(); ++i) {
        const QString argument = arguments.at(i);
        if (argument == QLatin1String("--json")) {
            json = true;
        } else if (argument == QLatin1String("--download")) {
            wantDownload = true;
        } else if (argument == QLatin1String("--explain")) {
            explain = true;
        } else if (argument == QLatin1String("--timeout") && i + 1 < arguments.size()) {
            timeoutSeconds = qMax(1, arguments.at(++i).toInt());
        } else if (argument.startsWith(QLatin1String("-"))) {
            line(QStringLiteral("error: unknown option %1").arg(argument));
            return 2;
        }
    }

    const UpdateChecker::Config config = UpdateChecker::loadConfig();
    if (!config.configured) {
        line(config.setupHint);
        if (json) {
            QJsonObject result{
                { QStringLiteral("ok"), false },
                { QStringLiteral("configured"), false },
                { QStringLiteral("hint"), config.setupHint },
            };
            line(QStringLiteral("%1").arg(QString::fromUtf8(
                QJsonDocument(result).toJson(QJsonDocument::Compact))));
        }
        return 2;
    }

    UpdateChecker checker;
    QEventLoop loop;
    int exitCode = 1;
    bool finished = false;
    QJsonObject result;
    auto finish = [&](int code) {
        exitCode = code;
        finished = true;
        loop.quit();
    };

    QObject::connect(&checker, &UpdateChecker::stateChanged, &loop, [&] {
        if (finished)
            return;
        const QString state = checker.state();
        if (state == QLatin1String("checking"))
            return;
        if (state == QLatin1String("error")) {
            result = QJsonObject{ { QStringLiteral("ok"), false },
                                  { QStringLiteral("state"), state },
                                  { QStringLiteral("error"), checker.note() },
                                  { QStringLiteral("report"), checker.channelReport() } };
            finish(1);
            return;
        }
        if (state == QLatin1String("disabled")) {
            result = QJsonObject{ { QStringLiteral("ok"), false },
                                  { QStringLiteral("state"), state },
                                  { QStringLiteral("error"), checker.note() } };
            finish(2);
            return;
        }
        if (state == QLatin1String("uptodate")) {
            result = QJsonObject{ { QStringLiteral("ok"), true },
                                  { QStringLiteral("state"), state },
                                  { QStringLiteral("current"), checker.currentVersion() },
                                  { QStringLiteral("available"), false } };
            finish(0);
            return;
        }
        if (state == QLatin1String("available")) {
            result = QJsonObject{
                { QStringLiteral("ok"), true },
                { QStringLiteral("state"), state },
                { QStringLiteral("current"), checker.currentVersion() },
                { QStringLiteral("available"), true },
                { QStringLiteral("latest"), checker.latestVersion() },
                { QStringLiteral("file"), checker.fileName() },
                { QStringLiteral("size"), checker.downloadSize() },
                { QStringLiteral("released"), checker.releasedAt() },
                { QStringLiteral("mandatory"), checker.mandatory() },
                { QStringLiteral("trust"), checker.trust() },
                { QStringLiteral("url"), checker.downloadUrl().isEmpty()
                                            ? QString()
                                            : checker.downloadUrl() },
                { QStringLiteral("notes"), checker.releaseNotes() },
            };
            if (!wantDownload) {
                finish(3);
                return;
            }
            checker.download();
            return;
        }
        if (state == QLatin1String("downloading"))
            return;
        if (state == QLatin1String("ready")) {
            result.insert(QStringLiteral("state"), state);
            result.insert(QStringLiteral("staged"), checker.stagedFile());
            finish(4);
            return;
        }
    });

    if (!json) {
        line(QStringLiteral("BSI Customizer %1 - update check").arg(checker.currentVersion()));
        line(QStringLiteral("  Channel %1%2")
                 .arg(config.channelId,
                      config.sourceFile.isEmpty()
                          ? QString()
                          : QStringLiteral(" (config: %1)").arg(config.sourceFile)));
    }

    QTimer::singleShot(timeoutSeconds * 1000, &loop, [&] {
        if (finished)
            return;
        // Mark it finished BEFORE cancelling: cancel() changes the state, and
        // the handler above would otherwise start a download after the timeout.
        finished = true;
        checker.cancel();
        exitCode = 1;
        result = QJsonObject{ { QStringLiteral("ok"), false },
                              { QStringLiteral("state"), QStringLiteral("timeout") },
                              { QStringLiteral("error"),
                                QStringLiteral("no answer within %1 s").arg(timeoutSeconds) } };
        loop.quit();
    });

    checker.check();
    loop.exec();

    if (json) {
        line(QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)));
        return exitCode;
    }
    // --explain prints what the bot actually received from the channel. A
    // failed check without it is one paragraph the user cannot act on.
    if (explain) {
        line(QStringLiteral("  What the bot sees:"));
        for (const QString &reportLine : checker.channelReport().split(QLatin1Char('\n')))
            line(QStringLiteral("  %1").arg(reportLine));
    }
    if (exitCode == 0) {
        line(QStringLiteral("  Result: you are on the newest build (%1).")
                 .arg(checker.currentVersion()));
    } else if (exitCode == 3) {
        line(QStringLiteral("  Result: version %1 is available (%2%3).")
                 .arg(checker.latestVersion(), checker.fileName(),
                      checker.downloadSize().isEmpty()
                          ? QString()
                          : QStringLiteral(", %1").arg(checker.downloadSize())));
        const QString trust = checker.trust();
        line(QStringLiteral("  Trust: %1")
                 .arg(trust == QLatin1String("signed")
                          ? QStringLiteral("a signed manifest")
                          : trust == QLatin1String("pinned")
                                ? QStringLiteral("pinned to the SHA-256 in the release")
                                : QStringLiteral("no SHA-256 in the release message - the "
                                                 "fingerprint is recorded on first download")));
        if (!checker.releaseNotes().trimmed().isEmpty()) {
            line(QStringLiteral("  Notes:"));
            for (const QString &noteLine : checker.releaseNotes().split(QLatin1Char('\n')))
                line(QStringLiteral("    %1").arg(noteLine));
        }
        line(QStringLiteral("  Add --download to fetch and verify it, or open the app and press Update."));
    } else if (exitCode == 4) {
        line(QStringLiteral("  Result: version %1 downloaded and verified.").arg(checker.latestVersion()));
        line(QStringLiteral("  File: %1").arg(checker.stagedFile()));
        line(QStringLiteral("  Start the app and press Install, or replace the EXE with this file."));
    } else {
        line(QStringLiteral("  Result: %1").arg(result.value(QStringLiteral("error")).toString()));
    }
    return exitCode;
}

} // namespace

int runUpdateCli(const QStringList &arguments)
{
    // Check a file on disk the way the download path checks a finished
    // download, without a Discord channel:
    //   BSI-Customizer.exe --check-updates --verify-file <path> [--json]
    //
    // The release workflow runs this on the installer it just built. That is
    // the one thing reading the source cannot stand in for: the bytes Inno
    // Setup actually produced. 2.5.1 shipped a setup the app itself deleted at
    // 100%, because the verifier wanted the app's machine from a 32-bit stub.
    const int verifyAt = arguments.indexOf(QStringLiteral("--verify-file"));
    if (verifyAt >= 0) {
        const QString path = verifyAt + 1 < arguments.size() ? arguments.at(verifyAt + 1) : QString();
        const bool json = arguments.contains(QStringLiteral("--json"));
        if (path.isEmpty()) {
            line(QStringLiteral("error: --verify-file needs the path of a downloaded file"));
            return 2;
        }
        const QFileInfo file(path);
        if (!file.exists() || !file.isFile()) {
            line(QStringLiteral("error: no such file: %1").arg(path));
            return 1;
        }

        QString error;
        UpdateChecker::PeInfo pe;
        const bool readable = UpdateChecker::readPeInfo(file.absoluteFilePath(), &pe, &error);
        const bool installer = UpdateChecker::looksLikeInstaller(file.absoluteFilePath());
        // A setup is run, not re-executed in place, so its own header is never
        // asked about: Inno's loader is 32-bit whatever it installs, and asking
        // "would this PC run it" about that stub is what made a perfectly good
        // release setup print `runs: no` and exit 1.
        const bool runnable = installer
            || UpdateChecker::looksLikeExecutable(file.absoluteFilePath(), &error);
        // Copying a running program over itself needs the same architecture;
        // a setup is started instead, so it is not measured against it either.
        const bool replaceable = !installer
            && UpdateChecker::canReplaceThisProgram(file.absoluteFilePath(), &error);

        if (json) {
            QJsonObject result{
                { QStringLiteral("ok"), runnable },
                { QStringLiteral("file"), file.absoluteFilePath() },
                { QStringLiteral("size"), double(file.size()) },
                { QStringLiteral("readable"), readable },
                { QStringLiteral("machine"), readable ? pe.machineName() : QString() },
                { QStringLiteral("pe32Plus"), readable ? pe.pe32Plus : false },
                { QStringLiteral("installer"), installer },
                { QStringLiteral("runnable"), runnable },
                { QStringLiteral("canReplace"), replaceable },
                { QStringLiteral("error"), runnable ? QString() : error },
            };
            line(QStringLiteral("%1").arg(QString::fromUtf8(
                QJsonDocument(result).toJson(QJsonDocument::Compact))));
            return runnable ? 0 : 1;
        }

        line(QStringLiteral("file:    %1").arg(file.absoluteFilePath()));
        line(QStringLiteral("size:    %1 bytes").arg(file.size()));
        if (readable) {
            line(QStringLiteral("header:  %1, %2 optional header, %3")
                     .arg(pe.machineName(),
                          pe.pe32Plus ? QStringLiteral("PE32+") : QStringLiteral("PE32"),
                          pe.isDll ? QStringLiteral("a library") : QStringLiteral("a program")));
        }
        line(QStringLiteral("setup:   %1").arg(installer ? QStringLiteral("yes") : QStringLiteral("no")));
        line(QStringLiteral("runs:    %1").arg(runnable ? QStringLiteral("yes") : QStringLiteral("no")));
        line(QStringLiteral("replace: %1").arg(replaceable ? QStringLiteral("yes") : QStringLiteral("no")));
        if (!runnable)
            line(QStringLiteral("reason:  %1").arg(error));
        line(runnable ? QStringLiteral("VERDICT: the updater would keep this file")
                      : QStringLiteral("VERDICT: the updater would delete this file"));
        return runnable ? 0 : 1;
    }
    return runUpdateCommand(arguments);
}
