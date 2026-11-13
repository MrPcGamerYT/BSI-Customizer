#pragma once
#include <QByteArray>
#include <QList>

namespace QtResources {
// Proven array bases from qRegisterResourceData calls or an embedded QRC header.
struct Pool {
    qint64 tree = -1;
    qint64 names = -1;
    qint64 dataRva = -1;
    int version = 0;
    bool registered = false;
};
QList<Pool> registeredPools(const QByteArray &image);
}
