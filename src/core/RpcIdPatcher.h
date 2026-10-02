#pragma once

#include <QList>
#include <QString>
#include <QStringList>

/**
 * Discord Rich Presence (Application ID) patching for emulator executables.
 *
 * This is the shipping implementation of the feature: it runs inside the
 * application, with no helper script, interpreter or other external component
 * to deploy — the same reason every other patcher in this project is native.
 *
 * The UI and the `--rpcid` command line share one on-disk contract: the first
 * backup is `<file>.rpcid-bak` and the remembered patch state is
 * `<file>.rpcid.json`, so a file patched from either side can be re-patched or
 * undone from the other. RpcIdScan holds the detection heuristics.
 */
namespace RpcIdPatcher {

struct Result
{
    bool ok = false;
    QString msg;       // ready-to-show report (may span several lines)
    QStringList ids;   // IDs worth an online name lookup, best first
};

struct CandidateInfo
{
    QString id;
    QString known;     // documented product name, empty when unknown
    QString date;      // snowflake creation date
    int score = 0;
    int refs = 0;
    bool confident = false;
};

struct Status
{
    bool ok = false;
    bool patched = false;          // this file is on an ID we wrote earlier
    bool hasBackup = false;        // <file>.rpcid-bak exists
    QString currentId;             // patched-to ID, or the detected one
    QString originalId;            // ID the file had before the first patch
    QString known;                 // documented name of currentId
    QString msg;
    QStringList leftovers;         // older IDs still present in the file
    QList<CandidateInfo> candidates;
};

/** One editable Windows version-resource string. */
struct NameEntry
{
    QString key;      // "FileDescription", "ProductName", ...
    QString value;    // what the file shows today
    int max = 0;      // how many characters the reserved slot holds
};

struct Names
{
    bool ok = false;
    QString msg;
    QList<NameEntry> entries;   // one per key and translation, in file order
};

QString backupPath(const QString &exePath);
QString statePath(const QString &exePath);

/** Fast, offline state of one file — drives the UI's badge and ID field. */
Status status(const QString &path);

/** Human-readable scan of a file (or of every EXE/DLL inside a folder). */
Result find(const QString &path);

/**
 * Replace the Application ID in place; the file size never changes.
 * `oldIds` overrides the search (empty = remembered ID, else heuristics).
 * Leftover copies of earlier IDs are repaired whenever the new ID fits.
 */
Result replace(const QString &path, const QString &newId,
               const QStringList &oldIds = {}, bool dryRun = false, bool all = false);

/** Restore `<file>.rpcid-bak` over the file and forget the patch state. */
Result undo(const QString &path);

/** The Windows version-resource strings Explorer and Task Manager show. */
Names names(const QString &path);

/**
 * Rename the executable in place: `key` (empty = FileDescription and
 * ProductName) is set in every translation, inside the slot the compiler
 * reserved, so the resource tree stays valid and the file size never changes.
 * Text that does not fit is refused with the exact limit. This does not
 * rename the Discord application itself.
 */
Result setName(const QString &path, const QString &text, const QString &key = {},
               bool dryRun = false);

} // namespace RpcIdPatcher
