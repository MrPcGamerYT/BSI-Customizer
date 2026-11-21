#pragma once
#include "PotatoResolution.h"
#include <QObject>
#include <QPointer>
#include <QString>

namespace BsiShaders {
// Small, GUI-thread-only public Qt adapter to the SAME native config methods
// used by the stock Display page. No private emulator/Qt headers or config-file
// guessing. An incomplete/unknown method contract is an unavailable feature.
class NativeDisplayBinding {
public:
    bool bind(QObject *backend,QString *error);
    bool available() const;
    bool bindPlayer(QObject *player,QString *error);
    bool canRestart() const;
    bool restart(QString *error) const;
    bool read(DisplayResolution *resolution,QString *error) const;
    bool modified(bool *value,QString *error) const;
    bool stage(const DisplayResolution &resolution,QString *error) const;
    bool commit(QString *error) const;
private:
    QPointer<QObject> backend,player;
};
} // namespace BsiShaders
