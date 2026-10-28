#pragma once

#include <QString>

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

// `HD-Adb.exe` next to the given player, or the one in the installation root.
QString findAdb(const QString &exePath);

// The instance's published Android port, from bluestacks.conf
// (`bst.instance.<name>.status.adb_port`, or the older `adb_port`).
quint16 instancePort(const QString &confPath, const QString &instance);

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
