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

// What the image is, and whether it can be written.
QVariantMap info(const QString &imagePath);
// { ok, path, parent, entries: [{name, path, dir, size, mtime, mode}] }
QVariantMap listing(const QString &imagePath, const QString &innerPath);
// Writes one file out; `localPath` is a file path for a file, a folder for a folder.
QVariantMap extract(const QString &imagePath, const QString &innerPath, const QString &localPath);
// Local file to `innerDir` (keeping its name), or replacing the same name there.
QVariantMap addFile(const QString &imagePath, const QString &innerDir, const QString &localFile);
// A whole folder tree, recreated inside the image.
QVariantMap addFolder(const QString &imagePath, const QString &innerDir, const QString &localDir);
QVariantMap makeFolder(const QString &imagePath, const QString &innerDir, const QString &name);
QVariantMap remove(const QString &imagePath, const QString &innerPath);

// The block journal: what the editor changed last, and how to put it back.
bool hasUndo(const QString &imagePath);
QVariantMap undo(const QString &imagePath);
QVariantMap discardUndo(const QString &imagePath);

} // namespace RootVhd
