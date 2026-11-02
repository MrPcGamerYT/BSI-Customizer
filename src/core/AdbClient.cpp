#include "AdbClient.h"

#include "DebloatConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStringList>
#include <QThread>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace {

constexpr int kStartTimeout = 30000;

QString parentModulePath()
{
#ifdef Q_OS_WIN
    const HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE)return {};
    PROCESSENTRY32W entry {}; entry.dwSize=sizeof(entry);
    DWORD parent=0;
    for(BOOL more=Process32FirstW(snapshot,&entry);more;more=Process32NextW(snapshot,&entry)) {
        if(entry.th32ProcessID==GetCurrentProcessId()) { parent=entry.th32ParentProcessID; break; }
    }
    CloseHandle(snapshot);
    if(!parent)return {};
    const HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,parent);
    if(!process)return {};
    wchar_t module[32768] {}; DWORD length=32768;
    const BOOL found=QueryFullProcessImageNameW(process,0,module,&length);
    CloseHandle(process);
    if(found)return QString::fromWCharArray(module,int(length));
#endif
    return {};
}

QString finishedText(const QString &value)
{
    return QString(value).remove(QLatin1Char('\r'));
}

// A port that is almost certainly free for our private server. The connect
// attempt is the real check; a collision just makes `start-server` fail and
// the next candidate is used.
quint16 pickPort()
{
    return quint16(5037 + 1 + (QRandomGenerator::global()->bounded(400)));
}

} // namespace

namespace Adb {

QStringList customizerDirectories()
{
    QStringList roots;
    // The launcher overwrites both markers; inherited paths cannot redirect it.
    // Ordinary unpacked/development runs do not honor a launcher path alone.
    if (qEnvironmentVariable("BSI_PORTABLE")==QLatin1String("1")) {
        const QFileInfo outer(qEnvironmentVariable("BSI_LAUNCHER_PATH"));
        const auto parent=QDir::cleanPath(QDir::fromNativeSeparators(parentModulePath()));
        const auto advertised=QDir::cleanPath(QDir::fromNativeSeparators(outer.filePath()));
        // Never execute an inherited user-environment path in an elevated raw
        // install. Only the actual waiting parent launcher can supply this root.
        if (outer.isAbsolute() && !parent.isEmpty() && parent.compare(advertised,Qt::CaseInsensitive)==0
            && outer.isFile() && outer.suffix().compare(QLatin1String("exe"),Qt::CaseInsensitive)==0)
            roots << outer.absolutePath();
    }
    roots << QCoreApplication::applicationDirPath();
    roots.removeDuplicates();
    return roots;
}

QString findAdbInDirectories(const QStringList &directories)
{
    for (const auto &directory : directories) {
        if (directory.isEmpty() || !QDir::isAbsolutePath(directory)) continue;
        // A dedicated adb/ folder is convenient for installers; loose sidecars
        // beside BSI-Customizer.exe are supported too. Never recurse or use PATH.
        for (const auto &relative : {QStringLiteral("adb/adb.exe"),QStringLiteral("adb.exe"),
                                    QStringLiteral("HD-Adb.exe"),QStringLiteral("HD-Adb")}) {
            const QFileInfo candidate(QDir(directory).filePath(relative));
            if (candidate.isFile() && !candidate.isSymLink())
                return QDir::toNativeSeparators(candidate.absoluteFilePath());
        }
    }
    return {};
}

QString findAdb(const QString &exePath)
{
#ifndef Q_OS_WIN
    Q_UNUSED(exePath);
    return {};
#else
    auto roots=customizerDirectories();
    // Empty or relative player paths must not introduce the working directory.
    if (!exePath.isEmpty() && QFileInfo(exePath).isAbsolute()) roots << QFileInfo(exePath).absolutePath();
    return findAdbInDirectories(roots);
#endif
}

QString remoteShellCommand(const QString &command)
{
    return QStringLiteral("%1; echo \"BSI-EXIT:$?\"").arg(command);
}

quint16 instancePort(const QString &confPath, const QString &instance)
{
    QFile file(confPath);
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    const DebloatConfig::File conf = DebloatConfig::File::parse(file.readAll());
    file.close();
    if (!conf.isValid())
        return 0;
    const auto read = [&conf](const QString &key) -> quint16 {
        bool ok = false;
        const int value = conf.value(key).trimmed().toInt(&ok);
        return (ok && value > 0 && value < 65536) ? quint16(value) : quint16(0);
    };
    quint16 port = read(QStringLiteral("bst.instance.%1.status.adb_port").arg(instance));
    if (port == 0)
        port = read(QStringLiteral("bst.instance.%1.adb_port").arg(instance));
    if (port == 0)
        port = read(QStringLiteral("bst.instance.%1.status.adb_port")
                        .arg(instance.toLower()));
    return port;
}

Client::Client(const QString &adb, const QString &serial, QString *error)
    : m_adb(adb)
    , m_serial(serial)
{
    const auto fail = [error](const QString &why) {
        if (error)
            *error = why;
    };
    if (m_adb.isEmpty() || !QFileInfo::exists(m_adb)) {
        fail(QStringLiteral("HD-Adb.exe was not found next to HD-Player.exe."));
        return;
    }
#ifndef Q_OS_WIN
    fail(QStringLiteral("Running Android commands needs Windows."));
    return;
#else
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("ANDROID_ADB_SERVER_PORT"), QString::number(pickPort()));
    env.insert(QStringLiteral("ADB_MDNS"), QStringLiteral("0"));

    const auto run = [&](const QStringList &args, int timeout) -> QString {
        QProcess proc;
        proc.setProcessEnvironment(env);
        proc.setProcessChannelMode(QProcess::SeparateChannels);
        proc.start(m_adb, args);
        if (!proc.waitForStarted(timeout)) {
            fail(QStringLiteral("HD-Adb.exe did not start."));
            return QString();
        }
        if (!proc.waitForFinished(timeout)) {
            proc.kill();
            proc.waitForFinished(3000);
            fail(QStringLiteral("ADB timed out."));
            return QString();
        }
        const QString out = finishedText(QString::fromLocal8Bit(proc.readAllStandardOutput()));
        const QString err = finishedText(QString::fromLocal8Bit(proc.readAllStandardError()));
        const QString both = out + err;
        if (both.contains(QLatin1String("cannot connect to daemon"))
            || both.contains(QLatin1String("daemon not running"))) {
            fail(QStringLiteral("ADB could not start its server."));
            return QString();
        }
        if (proc.exitStatus() != QProcess::NormalExit) {
            fail(QStringLiteral("ADB crashed."));
            return QString();
        }
        return out + err;
    };

    // A private server on a private port: never touches the instance's own.
    m_port = quint16(env.value(QStringLiteral("ANDROID_ADB_SERVER_PORT")).toUShort());
    run(QStringList{QStringLiteral("start-server")}, kStartTimeout);

    const QString target = m_serial.contains(QLatin1Char(':')) ? m_serial
                                                              : QStringLiteral("127.0.0.1:%1").arg(m_port);
    const QString connectOut = run(QStringList{QStringLiteral("connect"), target}, kStartTimeout);
    if (connectOut.isEmpty())
        return; // fail() already explained
    if (!connectOut.contains(QLatin1String("connected to"))
        && !connectOut.contains(QLatin1String("already connected"))) {
        fail(QStringLiteral("ADB could not reach the instance (%1).")
                 .arg(finishedText(connectOut).trimmed().section(QLatin1Char('\n'), 0, 0)));
        return;
    }

    // `connect` only opens the socket; the guest must answer before the
    // package commands are worth trying.
    QString state;
    for (int attempt = 0; attempt < 12; ++attempt) {
        state = run(QStringList{QStringLiteral("-s"), target, QStringLiteral("get-state")}, 10000)
                    .trimmed();
        if (state == QLatin1String("device"))
            break;
        QThread::msleep(500);
    }
    if (state != QLatin1String("device")) {
        fail(state.isEmpty() ? QStringLiteral("The instance did not answer ADB.")
                             : QStringLiteral("The instance is %1, not ready.").arg(state));
        return;
    }
    m_serial = target;
    m_ok = true;
#endif
}

Client::~Client()
{
#ifndef Q_OS_WIN
    return;
#else
    // Stop only the server this client started on its own port.
    if (m_adb.isEmpty() || m_port == 0)
        return;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("ANDROID_ADB_SERVER_PORT"), QString::number(m_port));
    QProcess proc;
    proc.setProcessEnvironment(env);
    proc.start(m_adb, QStringList{QStringLiteral("kill-server")});
    if (proc.waitForStarted(5000))
        proc.waitForFinished(8000);
#endif
}

Output Client::shell(const QString &command, int timeoutMs) const
{
    Output result;
#ifndef Q_OS_WIN
    result.error = QStringLiteral("Running Android commands needs Windows.");
    Q_UNUSED(command);
    Q_UNUSED(timeoutMs);
    return result;
#else
    if (!m_ok) {
        result.error = QStringLiteral("ADB is not connected.");
        return result;
    }
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("ANDROID_ADB_SERVER_PORT"), QString::number(m_port));
    QProcess proc;
    proc.setProcessEnvironment(env);
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    // Pass one shell command argument. Older BlueStacks HD-Adb builds join
    // argv differently and turn `sh -c (command)` into invalid shell syntax.
    proc.start(m_adb, QStringList{QStringLiteral("-s"), m_serial, QStringLiteral("shell"),
                                  remoteShellCommand(command)});
    if (!proc.waitForStarted(timeoutMs)) {
        result.error = QStringLiteral("HD-Adb.exe did not start.");
        return result;
    }
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(3000);
        result.error = QStringLiteral("The command timed out.");
        return result;
    }

    const QString rawOut = QString::fromLocal8Bit(proc.readAllStandardOutput());
    const QString rawErr = finishedText(QString::fromLocal8Bit(proc.readAllStandardError()));
    static const QRegularExpression marker(QStringLiteral("BSI-EXIT:(-?\\d+)"));
    int code = -1;
    QString body = rawOut;
    if (const auto match = marker.match(rawOut); match.hasMatch()) {
        code = match.captured(1).toInt();
        body = rawOut.left(int(match.capturedStart()));
    }
    body = finishedText(body).trimmed();
    result.out = body;
    if (code == 0) {
        result.ok = true;
    } else if (!rawErr.isEmpty()) {
        result.error = rawErr.trimmed();
    } else {
        result.error = body.isEmpty() ? QStringLiteral("The command failed.") : body;
    }
    if (proc.exitStatus() != QProcess::NormalExit) {
        result.ok = false;
        if (result.error.isEmpty())
            result.error = QStringLiteral("ADB crashed.");
    }
    return result;
#endif
}

Output Client::pull(const QString &remotePath, const QString &localPath, int timeoutMs) const
{
    Output result;
#ifndef Q_OS_WIN
    Q_UNUSED(remotePath);
    Q_UNUSED(localPath);
    Q_UNUSED(timeoutMs);
    result.error = QStringLiteral("Running Android commands needs Windows.");
    return result;
#else
    if (!m_ok) {
        result.error = QStringLiteral("ADB is not connected.");
        return result;
    }
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("ANDROID_ADB_SERVER_PORT"), QString::number(m_port));
    QProcess proc;
    proc.setProcessEnvironment(env);
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    // `-a` keeps the transfer that already exists on this machine intact
    // instead of inventing a new copy every time.
    proc.start(m_adb, QStringList{QStringLiteral("-s"), m_serial, QStringLiteral("pull"),
                                  QStringLiteral("-a"), remotePath, localPath});
    if (!proc.waitForStarted(30000)) {
        result.error = QStringLiteral("HD-Adb.exe did not start.");
        return result;
    }
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(3000);
        result.error = QStringLiteral("Copying out of the instance timed out.");
        return result;
    }
    const QString out = finishedText(QString::fromLocal8Bit(proc.readAllStandardOutput()));
    const QString err = finishedText(QString::fromLocal8Bit(proc.readAllStandardError()));
    result.out = (out + QLatin1Char('\n') + err).trimmed();
    if (proc.exitStatus() != QProcess::NormalExit) {
        result.error = QStringLiteral("ADB crashed while copying.");
        return result;
    }
    if (proc.exitCode() != 0) {
        result.error = result.out.isEmpty() ? QStringLiteral("The copy failed.") : result.out;
        return result;
    }
    result.ok = true;
    return result;
#endif
}

} // namespace Adb
