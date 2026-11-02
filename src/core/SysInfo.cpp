#include <QFileInfo>
#include "AdbClient.h"
#include "SysInfo.h"

#ifdef Q_OS_LINUX
#include <QFile>
#include <QTextStream>
#endif

double SysInfo::ramGb() const
{
#ifdef Q_OS_WIN
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms))
        return double(ms.ullTotalPhys) / 1073741824.0;
    return 0.0;
#elif defined(Q_OS_LINUX)
    QFile f(QStringLiteral("/proc/meminfo"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        while (!in.atEnd()) {
            const QString line = in.readLine();
            if (line.startsWith(QLatin1String("MemTotal:"))) {
                const QStringList parts = line.simplified().split(QLatin1Char(' '));
                if (parts.size() >= 2)
                    return parts.at(1).toDouble() / 1048576.0; // kB -> GB
            }
        }
    }
    return 0.0;
#else
    return 0.0;
#endif
}

QVariantMap SysInfo::snapshot(const QString &targetPath) const
{
    double available=-1;
#ifdef Q_OS_WIN
    MEMORYSTATUSEX memory {};
    memory.dwLength=sizeof(memory);
    if(GlobalMemoryStatusEx(&memory)) available=double(memory.ullAvailPhys)/1073741824.0;
#endif
    const QStorageInfo system(QDir::rootPath());
    const QStorageInfo target(targetPath.isEmpty()?QDir::rootPath():QFileInfo(targetPath).absolutePath());
    return {{"os",os()},{"cores",cpuCores()},{"ramGb",ramGb()},{"availableRamGb",available},
        {"systemFreeGb",system.isReady()?double(system.bytesAvailable())/1073741824.0:-1.0},
        {"targetFreeGb",target.isReady()?double(target.bytesAvailable())/1073741824.0:-1.0},
        {"adbPath",Adb::findAdb(targetPath)},{"adbDirectories",Adb::customizerDirectories()}};
}
