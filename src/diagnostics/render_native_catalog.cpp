#include "ui/catalog_projection.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <iostream>

// Finite read-only projection probe. It never starts a relay, mining worker,
// wallet or chain. A fixed descriptor gives both builds the same source view.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) return 2;
    QFile descriptor(QString::fromLocal8Bit(argv[1]));
    if (!descriptor.open(QIODevice::ReadOnly) || descriptor.size() > 16384) return 3;
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(descriptor.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) return 4;
    const auto input = document.object();
    if (input.value("schema").toString() != "pgn-fixed-projection-view-1") return 5;
    bool revisionOk = false;
    const auto revisionText = input.value("revision").toString();
    const auto revision = revisionText.toULongLong(&revisionOk);
    if (!revisionOk || QString::number(revision) != revisionText || revision > 9223372036854775807ULL) return 6;
    const auto width = input.value("width").toInt();
    const auto height = input.value("height").toInt();
    for (const char* name : {"min_x", "max_x", "min_y", "max_y"})
        if (!input.value(name).isDouble() || !std::isfinite(input.value(name).toDouble())) return 7;
    const pgn::ui::DenseView view{input.value("min_x").toDouble(), input.value("max_x").toDouble(),
        input.value("min_y").toDouble(), input.value("max_y").toDouble(), QSize(width, height), revision};
    const auto raster = pgn::ui::renderNativeCatalog(input.value("database").toString(),
        input.value("scope").toString(), input.value("axis").toString(), view, {}, qRgb(90, 220, 170));
    QByteArray pixels;
    if (!raster.image.isNull()) pixels = QByteArray::fromRawData(
        reinterpret_cast<const char*>(raster.image.constBits()), raster.image.sizeInBytes());
#ifdef __OPTIMIZE__
    constexpr bool optimized = true;
#else
    constexpr bool optimized = false;
#endif
    const QJsonObject result{{"schema", "pgn-fixed-projection-result-1"},
        {"descriptor_sha256", QString::fromLatin1(QCryptographicHash::hash(document.toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex())},
        {"revision", revisionText}, {"scope", raster.scope}, {"axis", raster.axis},
        {"complete", raster.complete}, {"error", raster.error},
        {"points", QString::number(raster.points)}, {"rows_read", QString::number(raster.rowsRead)},
        {"elapsed_ns", QString::number(raster.elapsedNs)}, {"compiler_optimized", optimized},
        {"pixel_sha256", QString::fromLatin1(QCryptographicHash::hash(pixels, QCryptographicHash::Sha256).toHex())},
        {"observed_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"cpu_mining", false}, {"source_mutation", false}, {"network_credit", false}};
    std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << '\n';
    return raster.complete && raster.error.isEmpty() ? 0 : 8;
}
