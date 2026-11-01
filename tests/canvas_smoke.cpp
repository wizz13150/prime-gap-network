#include "ui/mining_canvas.h"
#include <QApplication>
#include <QImage>
#include <QPainter>
#include <memory>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("PGN"));
    QCoreApplication::setApplicationName(QStringLiteral("PGN renderer test"));
    std::unique_ptr<QWidget> canvas(pgn::ui::createMiningCanvas());
    canvas->resize(1280, 768);
    QImage image(canvas->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    canvas->render(&painter);
    painter.end();
    if (image.pixelColor(2, 2).alpha() == 0) return 1;
    if (argc > 1 && !image.save(QString::fromLocal8Bit(argv[1]))) return 2;
    return 0;
}
