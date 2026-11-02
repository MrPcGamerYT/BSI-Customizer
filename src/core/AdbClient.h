#pragma once

#include <QString>
#include <QStringList>

/**
 * Adb — a minimal bridge to the player's own `HD-Adb.exe`.
 *
 * BlueStacks ships its adb next to `HD-Player.exe`, so nothing has to be
 * downloaded: the tool starts a private adb server on a free port, connects to
 * the running instance's published port and runs one shell command at a time.
 * The server it starts is the one it stops again; an adb server that was
 * already running (BlueStacks' own) is left alone.
 */
namespace Adb {

struct Output
{
    bool ok = false;
    QString out;      // stdout with the completion marker removed
    QString error;    // why it failed, or the command's stderr tail
};

// Trusted application directories first (outer portable EXE, then runtime),
// then the selected player's folder. Never search cwd/PATH. ADB is user supplied.
QStringList customizerDirectories();
// Deterministic resolver, also used by fixture-only tests. Relative roots ignored.
QString findAdbInDirectories(const QStringList &directories);
QString findAdb(const QString &exePath);

// The instance's published Android port, from bluestacks.conf
// (`bst.instance.<name>.status.adb_port`, or the older `adb_port`).
quint16 instancePort(const QString &confPath, const QString &instance);

// Single-command form passed as one argv item to older HD-Adb shell clients.
// Appends an exit marker without nested `sh -c`/parentheses, which some of
// BlueStacks' bundled adb versions split incorrectly on the remote side.
QString remoteShellCommand(const QString &command);

class Client
{
public:
    // Starts the private server and connects. `error` explains a failure.
    Client(const QString &adb, const QString &serial, QString *error);
    ~Client();
    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;

    bool ok() const { return m_ok; }
    QString serial() const { return m_serial; }

    // Runs `command` in the guest and reports its exit status.
    Output shell(const QString &command, int timeoutMs = 25000) const;
    // Copies `remotePath` (file or folder) to `localPath`.
    Output pull(const QString &remotePath, const QString &localPath,
                int timeoutMs = 15 * 60 * 1000) const;

private:
    QString m_adb;
    QString m_serial;
    quint16 m_port = 0;
    bool m_ok = false;
};

} // namespace Adb
