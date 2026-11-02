#pragma once

#include <QDir>
#include <QObject>
#include <QStorageInfo>
#include <QSysInfo>
#include <QThread>
#include <QVariantMap>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

/**
 * SysInfo — read-only hardware facts for the dashboard.
 */
class SysInfo : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int cpuCores READ cpuCores CONSTANT)
    Q_PROPERTY(double ramGb READ ramGb CONSTANT)
    Q_PROPERTY(double diskFreeGb READ diskFreeGb CONSTANT)
    Q_PROPERTY(QString os READ os CONSTANT)

public:
    using QObject::QObject;

    int cpuCores() const { return qMax(1, QThread::idealThreadCount()); }
    double ramGb() const;
    QVariantMap snapshot(const QString &targetPath) const;
    double diskFreeGb() const
    {
        return double(QStorageInfo(QDir::rootPath()).bytesAvailable()) / 1073741824.0;
    }
    QString os() const { return QSysInfo::prettyProductName(); }
};
