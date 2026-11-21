// Diagnostic probe (NOT part of the product build): runs the real
// ThemeEngine hex-colour pipeline against a copy of an HD-Player.exe and
// reports what it found and what it wrote. Used to reproduce the
// "Hex colour changer breaks HD-Player" report against the pinned
// reference players.
#include "ThemeEngine.h"

#include <QFile>
#include <cstdio>

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "usage: theme_probe <exe> <profile|auto> <colorName> <newHex>\n");
        return 2;
    }
    const QString path = QString::fromLocal8Bit(argv[1]);
    QString profile = QString::fromLocal8Bit(argv[2]);
    const QString name = QString::fromLocal8Bit(argv[3]);
    const QString newHex = QString::fromLocal8Bit(argv[4]);

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    const QByteArray data = f.readAll();
    f.close();

    if (profile == QLatin1String("auto"))
        profile = ThemeEngine::detectProfileFromPath(path);
    const ThemeInfo info = ThemeEngine::findThemeHex(data, profile);
    printf("profile=%s found=%d mode=%s\n", qPrintable(profile), int(info.found), qPrintable(info.mode));

    for (const QString &n : ThemeEngine::qmlColorNames()) {
        const QString cur = ThemeEngine::findQmlColor(data, n);
        if (!cur.isEmpty())
            printf("qmlColor %s = %s\n", qPrintable(n), qPrintable(cur));
    }

    if (newHex == QLatin1String("list"))
        return 0;

    HexColorItem item;
    item.name = name;
    item.currentHex = ThemeEngine::findQmlColor(data, name);
    item.color = newHex;
    item.enabled = true;

    const auto r = ThemeEngine::applyHexTheme(
        path, profile, {}, { item },
        [](const QString &m, const QString &c) {
            fprintf(stderr, "[log:%s] %s\n", qPrintable(c), qPrintable(m));
        });
    printf("apply ok=%d msg=%s\n", int(r.ok), qPrintable(r.msg));
    return r.ok ? 0 : 1;
}
