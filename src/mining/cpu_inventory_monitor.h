#pragma once
#include <QProcess>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <functional>
#include <memory>

namespace pgn::mining {
struct CpuInventoryEndpoint { QString program; QStringList arguments; QString expectedHost; };
// Hardware discovery is a one-shot, bounded native query. It cannot open work.
inline void probeCpuInventory(const CpuInventoryEndpoint& endpoint, QObject* owner,
                             std::function<void(QJsonObject)> accept,
                             std::function<void(QString)> reject) {
    auto* process = new QProcess(owner);
    auto* deadline = new QTimer(process); deadline->setSingleShot(true);
    auto buffer = std::make_shared<QByteArray>();
    auto done = std::make_shared<bool>(false);
    const auto fail = [process, reject, done](const QString& message) {
        if (*done) return; *done = true;
        if (process->state() != QProcess::NotRunning) process->kill();
        reject(message); process->deleteLater();
    };
    QObject::connect(process, &QProcess::readyReadStandardOutput, process, [process, buffer, fail] {
        buffer->append(process->readAllStandardOutput());
        if (buffer->size() > 32768) fail("CPU discovery response exceeds 32 KiB");
    });
    QObject::connect(process, &QProcess::readyReadStandardError, process, [process] { process->readAllStandardError(); });
    QObject::connect(deadline, &QTimer::timeout, process, [fail] { fail("CPU discovery timed out"); });
    QObject::connect(process, &QProcess::errorOccurred, process, [fail](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) fail("CPU discovery could not start");
    });
    QObject::connect(process, qOverload<int,QProcess::ExitStatus>(&QProcess::finished), process,
        [process, deadline, endpoint, buffer, done, accept, fail](int code, QProcess::ExitStatus status) {
            if (*done) return; deadline->stop(); buffer->append(process->readAllStandardOutput());
            QJsonParseError error; const auto document = QJsonDocument::fromJson(*buffer, &error);
            if (buffer->size() > 32768 || code != 0 || status != QProcess::NormalExit || error.error != QJsonParseError::NoError || !document.isObject()) {
                fail("CPU discovery returned an invalid response"); return;
            }
            const auto value = document.object(); const auto host = value.value("host").toString();
            const auto topology = value.value("topology").toArray();
            QSet<int> cpus; bool valid = !topology.isEmpty() && topology.size() <= 256;
            for (const auto& core : topology) for (const auto& cpu : core.toObject().value("cpus").toArray()) {
                const int id = cpu.toInt(-1);
                if (!cpu.isDouble() || cpu.toDouble() != id || id < 0 || id > 4095 || cpus.contains(id)) valid = false;
                cpus.insert(id);
            }
            const auto maximum = value.value("maximum_workers").toInt();
            if (value.value("schema") != "PGN_CPU_INVENTORY_ONLY_V1" || value.value("compute_started").toBool(true) || value.value("work_supported").toBool(true) ||
                value.value("prime_arithmetic").toBool(true) || host.isEmpty() || host.size() > 64 ||
                (!endpoint.expectedHost.isEmpty() && host.compare(endpoint.expectedHost, Qt::CaseInsensitive) != 0) ||
                !valid || cpus.isEmpty() || cpus.size() > 256 || maximum < 1 || maximum > cpus.size()) {
                fail("CPU discovery identity or capabilities were refused"); return;
            }
            *done = true; accept(value); process->deleteLater();
        });
    process->setProgram(endpoint.program); process->setArguments(endpoint.arguments);
    process->setProcessChannelMode(QProcess::SeparateChannels); deadline->start(8000); process->start();
}
}
