#include "engine/prime_gap_native/checkpoint_publish.h"
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <iostream>
int main() {
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;
    const auto final = temporary.filePath("state");
    const auto path = final.toUtf8().toStdString();
    for (int i = 1; i <= 3; ++i) {
        const auto content = QByteArray("checkpoint-") + QByteArray::number(i);
        const auto result = pgn_checkpoint::publish(path, content.constData(), static_cast<uint32_t>(content.size()));
        if (!result.ok || QFileInfo::exists(final + ".pending")) return 2;
        QFile input(final);
        if (!input.open(QIODevice::ReadOnly) || input.readAll() != content) return 3;
    }
    const auto archived = pgn_checkpoint::archive(path);
    if (!archived.ok || archived.destination.empty() || QFileInfo::exists(final)) return 4;
    QFile input(QString::fromUtf8(archived.destination.data(), qsizetype(archived.destination.size())));
    if (!input.open(QIODevice::ReadOnly) || input.readAll() != "checkpoint-3") return 5;
    const auto absent = pgn_checkpoint::archive(path);
    if (!absent.ok || !absent.destination.empty()) return 6;
    std::cout << "3 replacement checkpoint commits and exact archive PASS; own temporary files only\n";
}
