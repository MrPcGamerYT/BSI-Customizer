// ---------------------------------------------------------------------------
// Discord Rich Presence command line (BSI-Customizer --rpcid ...)
//
// The same native engines the UI uses (RpcIdScan / RpcIdPatcher /
// VersionResource), exposed as a scriptable command line. It replaces the old
// standalone reference script: the shipped EXE is the only thing required, so
// no interpreter, sidecar file or extra download is involved, and both the UI
// and the CLI read and write exactly the same `<file>.rpcid-bak` backup and
// `<file>.rpcid.json` patch state.
//
// Exit codes: 0 success, 1 the operation failed, 2 the command line is wrong.
// ---------------------------------------------------------------------------

#include "core/PatchFile.h"
#include "core/RpcIdPatcher.h"
#include "core/VersionResource.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <cstdio>
#include <string>
#include <string_view>

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
    QFile file(options.path);
    if (!QFileInfo(options.path).isFile() || !file.open(QIODevice::ReadOnly)) {
        line(QStringLiteral("Could not read %1.").arg(options.path));
        return 1;
    }
    const QByteArray data = file.readAll();
    file.close();

    const std::string_view view(data.constData(), static_cast<std::size_t>(data.size()));
    const VersionResource::Info info = VersionResource::read(view);
    if (!info.ok) {
        line(QStringLiteral("%1: %2.").arg(QFileInfo(options.path).fileName(),
                                           QString::fromStdString(info.error)));
        return 1;
    }

    line(QStringLiteral("Names inside %1").arg(QFileInfo(options.path).fileName()));
    for (const VersionResource::StringEntry &entry : info.strings) {
        line(QStringLiteral("  %1 = \"%2\" (max %3 characters)")
                 .arg(QString::fromStdString(entry.key), QString::fromStdString(entry.value))
                 .arg(entry.cap - 1));
    }
    if (!options.hasSet) {
        line(QStringLiteral("The name Discord itself shows comes from the Discord application, "
                            "not from this Windows version resource."));
        return 0;
    }

    const QStringList keys = options.key.isEmpty()
        ? QStringList{ QStringLiteral("FileDescription"), QStringLiteral("ProductName") }
        : QStringList{ options.key };

    QByteArray patched = data;
    QStringList written;
    QStringList refused;
    for (const QString &key : keys) {
        const auto matches = VersionResource::withKey(info, key.toStdString());
        if (matches.empty()) {
            refused << QStringLiteral("%1 is not in this file").arg(key);
            continue;
        }
        for (const VersionResource::StringEntry *entry : matches) {
            std::string why;
            if (VersionResource::setString(patched.data(), static_cast<std::size_t>(patched.size()),
                                           *entry, options.set.toStdString(), &why)) {
                written << key;
            } else {
                refused << QStringLiteral("%1: %2").arg(key, QString::fromStdString(why));
            }
        }
    }
    for (const QString &problem : refused)
        line(problem);
    if (written.isEmpty()) {
        line(QStringLiteral("Nothing was changed."));
        return 1;
    }
    if (options.dryRun) {
        line(QStringLiteral("Preview only: %1 would become \"%2\". Nothing was written.")
                 .arg(written.join(QStringLiteral(", ")), options.set));
        return 0;
    }
    if (!options.yes) {
        line(QStringLiteral("Add --yes to write \"%1\" into %2.")
                 .arg(options.set, written.join(QStringLiteral(", "))));
        return 1;
    }

    const QString backup = RpcIdPatcher::backupPath(options.path);
    if (!QFileInfo::exists(backup) && !QFile::copy(options.path, backup)) {
        line(QStringLiteral("Could not create the original-file backup. No changes were made."));
        return 1;
    }
    QString error;
    if (!PatchFile::writeVerified(options.path, data, patched, &error)) {
        line(error);
        return 1;
    }
    line(QStringLiteral("Set %1 to \"%2\"; the file size is unchanged and the original is backed up at %3.")
             .arg(written.join(QStringLiteral(", ")), options.set, QFileInfo(backup).fileName()));
    return 0;
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
