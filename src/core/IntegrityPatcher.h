#pragma once

#include <QByteArray>
#include <QString>

// Integrity (tamper) bypass — neutralizes the disk-integrity check in
// HD-Player.exe. Port of integrity_analyze / integrity_patch / restore.
namespace IntegrityPatcher {

struct Analysis
{
    bool ok = false;
    QString state;  // nofile | bad | absent | notfound | ambig | clean | patched
    QString msg;
    qint64 site = -1;
    QString how;
    QByteArray bytes;   // file contents when state == clean/patched
    int candidates = 0;
};

struct Result { bool ok = false; QString msg; };

Analysis analyze(const QString &exePath);
Result patch(const QString &exePath, bool dry = false);
Result restore(const QString &exePath);

} // namespace IntegrityPatcher
