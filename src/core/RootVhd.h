#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

/**
 * RootVhd — the hardcode debloater: a file manager for the engine's Android
 * system image.
 *
 * It opens `Root.vhd` (fixed or dynamic), walks the ext4 filesystem inside it
 * (partitioned or not) and lets the user browse, extract, add, replace and
 * delete files and folders, the way a 7-Zip window would — but writing in
 * place, through the engine in `core/rootdisk`.
 *
 * Every write goes through a block journal: the original bytes of every range
 * the editor touches are saved to the application data folder first, so one
 * Undo puts the image back exactly, even if the tool is closed in between.
 * A damaged, dirty or unsupported image is opened read-only and refused for
 * writing rather than guessed at.
 */
namespace RootVhd {

// Where the engine files of an installation usually live, best guess first.
QStringList engineFolders(const QString &exePath);
// The Root.vhd of this installation, or an empty string.
QString findImage(const QString &exePath);
// Extract Root.vhd from a selected 7z/7-Zip SFX engine package into the
// application's private cache. No archive utility is needed at runtime.
QVariantMap extractEngineArchive(const QString &archivePath);

// What the image is, and whether it can be written.
QVariantMap info(const QString &imagePath);
// Clears the Windows read-only attribute only after an explicit user request,
// then verifies that both the VHD and ext4 volume can be written.
QVariantMap enableWriteAccess(const QString &imagePath);
// { ok, path, parent, entries: [{name, path, dir, size, mtime, mode}] }
QVariantMap listing(const QString &imagePath, const QString &innerPath);
// Every entry anywhere in the image whose name contains `query`, so a package
// folder or an .apk can be found without walking the tree by hand.
QVariantMap search(const QString &imagePath, const QString &query, int limit = 500);
// Writes one file out; `localPath` is a file path for a file, a folder for a folder.
QVariantMap extract(const QString &imagePath, const QString &innerPath, const QString &localPath);
// Add a local file to `innerDir` (keeping its name); refuses duplicate names.
QVariantMap addFile(const QString &imagePath, const QString &innerDir, const QString &localFile);
// Replace the bytes of an existing regular file while keeping its in-image name/path.
QVariantMap replaceFile(const QString &imagePath, const QString &innerPath, const QString &localFile);
// A whole folder tree, recreated inside the image.
QVariantMap addFolder(const QString &imagePath, const QString &innerDir, const QString &localDir);
// Whatever the path is — a file or a folder tree — added to `innerDir`.
QVariantMap addPath(const QString &imagePath, const QString &innerDir, const QString &localPath);
QVariantMap makeFolder(const QString &imagePath, const QString &innerDir, const QString &name);
QVariantMap rename(const QString &imagePath, const QString &innerPath, const QString &newName);
QVariantMap move(const QString &imagePath, const QString &innerPath,
                 const QString &destinationDir);
QVariantMap copy(const QString &imagePath, const QString &innerPath,
                 const QString &destinationDir, const QString &newName = QString());
QVariantMap remove(const QString &imagePath, const QString &innerPath);

// The block journal: what the editor changed last, and how to put it back.
bool hasUndo(const QString &imagePath);
QVariantMap undo(const QString &imagePath);
QVariantMap discardUndo(const QString &imagePath);

} // namespace RootVhd
