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
 * On-disk compatibility with the standalone reference script (rpcid.py) is
 * kept on purpose: the first backup is `<file>.rpcid-bak` and the remembered
 * patch state is `<file>.rpcid.json`, so a file patched by either one can be
 * re-patched or undone by the other. RpcIdScan holds the detection heuristics.
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

} // namespace RpcIdPatcher
