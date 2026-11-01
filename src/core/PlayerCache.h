#pragma once
#include <QStringList>

namespace PlayerCache {
struct Result {
    bool ok = false;
    QString msg;
    int removed = 0;
    int failed = 0;
};
// Only exact HD-Player application folders under these AppData roots qualify.
// Explicit roots make regression tests independent of the user's real caches.
Result purge(const QStringList &appDataRoots);
}
