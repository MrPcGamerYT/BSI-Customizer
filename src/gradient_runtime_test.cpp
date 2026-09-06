// Render only the generated synthetic regression QML, never extracted user QML.
#include <QColor>
#include <QCoreApplication>
#include <QEventLoop>
#include <QImage>
#include <QPointF>
#include <QQuickView>
#include <QSGRendererInterface>
#include <QTimer>
#include <QUrl>
#include <cmath>
#include <cstdio>

int runGradientRuntimeTest(const QStringList &args)
{
    // exe, switch, synthetic QML path, first, second, horizontal, enabled
    if (args.size() != 7) return 2;
    const QColor first(args[3]), second(args[4]);
    if (!first.isValid() || !second.isValid()) return 2;
    const bool horizontal = args[5] == QLatin1String("1"), enabled = args[6] == QLatin1String("1");
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QQuickView view;
    view.setColor(Qt::transparent);
    view.setResizeMode(QQuickView::SizeViewToRootObject);
    view.setSource(QUrl::fromLocalFile(args[2]));
    if (view.status() != QQuickView::Ready) return 1;
    view.show();
    QEventLoop loop;
    QTimer::singleShot(500, &loop, &QEventLoop::quit);
    loop.exec();
    const QImage image = view.grabWindow();
    if (image.isNull() || image.width() < 8 || image.height() < 8) {
        std::fprintf(stderr, "No software-rendered gradient image was produced\n");
        return 1;
    }
    bool ok = true;
    for (const QPointF &point : { QPointF(.25, .25), QPointF(.75, .25), QPointF(.25, .75) }) {
        const int x = int(point.x() * image.width()), y = int(point.y() * image.height());
        const QColor actual = image.pixelColor(x, y);
        const double t = enabled ? (horizontal ? point.x() : point.y()) : 0;
        const auto expected = [t](int a, int b) { return int(std::lround(a + (b - a) * t)); };
        const bool matches = std::abs(actual.red() - expected(first.red(), second.red())) <= 10
                             && std::abs(actual.green() - expected(first.green(), second.green())) <= 10
                             && std::abs(actual.blue() - expected(first.blue(), second.blue())) <= 10;
        if (!matches) std::fprintf(stderr, "Unexpected pixel at %d,%d: %s\n", x, y, qPrintable(actual.name()));
        ok &= matches;
    }
    std::printf("Gradient pixel test: %s\n", ok ? "passed" : "failed");
    return ok ? 0 : 1;
}
