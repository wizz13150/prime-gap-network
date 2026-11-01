#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>

#include <optional>

namespace FrontierDeviceControlSidecar {

inline constexpr char REQUEST_SCHEMA[] =
    "gapcoin-frontier-device-control-request-v1";
inline constexpr char RESPONSE_SCHEMA[] =
    "gapcoin-frontier-device-control-response-v1";
inline constexpr qint64 MAX_RESPONSE_BYTES = 64 * 1024;

struct Request {
    QString requestId;
    QString deviceId;
    QString budgetKind;
    bool enabled{true};
    int budgetPercent{100};
    QDateTime requestedUtc;
};

struct Response {
    QString requestId;
    QString deviceId;
    QString status;
    QString detail;
    QString budgetKind;
    bool enabled{false};
    int budgetPercent{0};
    std::optional<qint64> processId;
    std::optional<QDateTime> telemetryObservedUtc;

    bool applied() const noexcept { return status == QLatin1String("applied"); }
};

// Creates <request_id>.request.json without replacing an existing request.
// The caller owns directory creation; this function never controls a device.
bool WriteRequestCreateOnly(const QString& directory,
                            const Request& request,
                            QString& error);

// Parses the exact v1 response object and binds it to the expected request and
// device identities. No response is returned on a schema or identity mismatch.
std::optional<Response> ParseResponse(const QByteArray& json,
                                      const Request& expected,
                                      QString& error);

// Reads at most MAX_RESPONSE_BYTES before applying the same strict parser.
std::optional<Response> ReadResponse(const QString& path,
                                     const Request& expected,
                                     QString& error);

} // namespace FrontierDeviceControlSidecar
