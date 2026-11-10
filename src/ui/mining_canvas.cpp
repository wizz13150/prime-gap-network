#include "ui/render/card_metric_caption.h"
#include "telemetry.h"
#include "ui/diagnostic_capture_view.h"
#include "ui/mining_control_binding.h"
#include "ui/dense_point_layer.h"
#include "ui/catalog_layer.h"
#include "ui/catalog_frame_presentation.h"
#include "ui/range_visuals.h"
#include "ui/mining_work_tile.h"
#include "ui/booking_labels.h"
#include "ui/booking_rail_layout.h"
#include "ui/throttle_badge.h"
#include "ui/mining_summary.h"
#include "mining/cpu_activity.h"
#include "ui/cpu_card_metrics.h"
#include "ui/block_eta.h"
#include "mining/resource_age.h"
#include "ui/historical_proof_presentation.h"
#include "ui/local_source_proof_presentation.h"
#include "ui/native_history_caption.h"
#include "booking/booking_model.h"
#include "booking/decimal_range.h"
#include "booking/range_identity.h"
#include "mining_status_button.h"
#include "verification_stage_counts.h"
#include "candidate_archive.h"
#include "run_filter.h"
#include "timeline_clock.h"
#include "device_strip_layout.h"
#include "display_rate.h"
#include "gauge_layout.h"
#include "metric_caption.h"
#include "gap_display_filter.h"
#include "rate_gauge_scale.h"
#include "device_card_bands.h"
#include "ui/position_formats.h"
#include "horizon_device_visuals.h"
#include "horizon_graph_controls.h"
#include "horizon_difficulty_graph.h"
#include "horizon_graph_tooltip.h"
#include "target_curve.h"
#include "run_kpi.h"
#include "search_geometry.h"
#include "tooltip_layout.h"
#include "device_control_sidecar.h"
#include "gpu_controller_ui.h"
#include "device_provenance.h"
#include "miner_selection.h"
#include "graph_view.h"
#include "render/graph_border_ticks.h"
#include "horizon_graph_model.h"
#include "horizon_workspace_style.h"
#include "gap_event_geometry.h"
#include "golden_s6_identity.h"
#include "official_crt_catalogue.h"
#include "merit_threshold.h"
#include "prime_gap_signal.h"
#include "producer_rail.h"
#include "proof_state.h"
#include "proof_timeline.h"
#include "verifier_progress.h"
#include "verifier_display_state.h"
#include <QApplication>
#include <QAbstractTextDocumentLayout>
#include <QButtonGroup>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFrame>
#include <QFormLayout>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QHash>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QLibrary>
#include <QLocale>
#include <QLockFile>
#include <QMainWindow>
#include <QMessageBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QUuid>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPointer>
#include <QPushButton>
#include <QComboBox>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QSet>
#include <QSignalBlocker>
#include <QShortcut>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QSpinBox>
#include <QStyle>
#include <QSysInfo>
#include <QTabBar>
#include <QTabWidget>
#include <QThread>
#include <QStringList>
#include <QTimer>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QTimeZone>
#include <QTextDocument>
#include <QUrl>
#include <QVector>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif


#include "mining_canvas.h"
#include "ui/render/mining_layout.h"
namespace {
struct LastPaintExecution {
    QWidget* widget; const char* property; QElapsedTimer timer;
    LastPaintExecution(QWidget* value,const char* name):widget(value),property(name){timer.start();}
    ~LastPaintExecution(){widget->setProperty(property,timer.nsecsElapsed());}
};
QString CompactHash(const QString& value)
{
    if (value.size() < 25) return value;
    return value.left(13) + QString::fromUtf8("\u2026") + value.right(10);
}

QString HexIntegerToDecimal(const QString& source)
{
    QString hex = source.trimmed();
    const bool prefixed = hex.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive);
    const bool containsHexLetter = hex.contains(
        QRegularExpression(QStringLiteral("[a-f]"),
                           QRegularExpression::CaseInsensitiveOption));
    // Canonical history stores decimal endpoints.  Live engine events are now
    // explicitly 0x-prefixed; older affected events contain hexadecimal
    // letters.  Never reinterpret an all-decimal persisted prime as hex.
    if (!prefixed && !containsHexLetter) return hex;
    if (prefixed) hex.remove(0, 2);
    if (hex.isEmpty()) return {};
    QString decimal = QStringLiteral("0");
    for (const QChar character : hex) {
        const ushort lower = character.toLower().unicode();
        const int digit = character.digitValue() >= 0
            ? character.digitValue()
            : (lower >= 'a' && lower <= 'f' ? 10 + lower - 'a' : -1);
        if (digit < 0 || digit > 15) return {};
        int carry = digit;
        for (qsizetype index = decimal.size(); index-- > 0;) {
            const int value = (decimal.at(index).unicode() - '0') * 16 + carry;
            decimal[index] = QChar::fromLatin1(static_cast<char>('0' + value % 10));
            carry = value / 10;
        }
        while (carry > 0) {
            decimal.prepend(QChar::fromLatin1(static_cast<char>('0' + carry % 10)));
            carry /= 10;
        }
    }
    const qsizetype firstNonZero = decimal.indexOf(
        QRegularExpression(QStringLiteral("[1-9]")));
    return firstNonZero < 0 ? QStringLiteral("0") : decimal.mid(firstNonZero);
}

bool DecimalDifferenceEquals(const QString& left, const QString& right,
                             const QString& expected)
{
    static const QRegularExpression decimal(QStringLiteral("^[1-9][0-9]*$"));
    if (!decimal.match(left).hasMatch() || !decimal.match(right).hasMatch() ||
        !decimal.match(expected).hasMatch() || right.size() < left.size() ||
        (right.size() == left.size() && right <= left)) return false;
    QString difference(right.size(), QLatin1Char('0'));
    int borrow = 0;
    int leftIndex = left.size() - 1;
    for (int rightIndex = right.size() - 1; rightIndex >= 0;
         --rightIndex, --leftIndex) {
        int value = right.at(rightIndex).digitValue() - borrow -
                    (leftIndex >= 0 ? left.at(leftIndex).digitValue() : 0);
        if (value < 0) { value += 10; borrow = 1; } else borrow = 0;
        difference[rightIndex] = QChar(QLatin1Char('0').unicode() + value);
    }
    if (borrow != 0) return false;
    qsizetype first = 0;
    while (first + 1 < difference.size() &&
           difference.at(first) == QLatin1Char('0')) ++first;
    return difference.mid(first) == expected;
}

QString JsonText(const QJsonObject& object, const char* key)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (value.isString()) return value.toString();
    if (value.isDouble()) return QString::number(value.toDouble(), 'g', 15);
    if (value.isBool()) return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    return {};
}

QString JsonInteger(const QJsonObject& object, const char* key)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isDouble()) return {};
    return QString::number(value.toVariant().toLongLong());
}

enum class FrontierDeviceControlStatus {
    Idle,
    Pending,
    Applied,
    Rejected,
};

enum class FrontierDeviceBudgetKind {
    CpuThreads,
    GpuPowerWorkShare,
};

struct FrontierDeviceControlCommand {
    QString requestId;
    QString deviceId;
    FrontierDeviceBudgetKind budgetKind{FrontierDeviceBudgetKind::GpuPowerWorkShare};
    bool enabled{true};
    int budgetPercent{100};
    QDateTime requestedUtc;
};

class ObservatoryCanvas final : public QWidget
{
public:
    ObservatoryCanvas(QString cliPath, QWidget* parent = nullptr, bool legacyR4Mode = false)
        : QWidget(parent),
          m_cliPath(QDir::toNativeSeparators(std::move(cliPath))),
          m_logo(QStringLiteral(":/frontier/app-icon.png")),
          m_legacyR4Mode(legacyR4Mode)
    {
        // Rendering is reused; previous engines, private laboratory profiles,
        // executable fingerprints, and wallet RPC are not part of this module.
        // Match the teaser's expanded live-GPU card on a fresh installation.
        m_collapsedDevices.insert(QStringLiteral("GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac"), false);
        // The historical Horizon screen opens on chronology; the newer
        // Frontier scientific screen opens on physical gap length.  Both
        // meanings stay available through the explicit axis switch.
        m_graphXAxisMode = legacyR4Mode ? FrontierGraphXAxis::Time
                                        : FrontierGraphXAxis::Length;
        // Horizon is a persistent observatory: before the first new run,
        // CURRENT is empty while the archive and LATEST GAP tile describe
        // history. Open the legacy/Diamond surface on that same dataset.
        m_showFullHistory = legacyR4Mode;
        m_host = QSysInfo::machineHostName();
        if (m_host.isEmpty()) {
            m_host = QProcessEnvironment::systemEnvironment().value(QStringLiteral("COMPUTERNAME"));
        }
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setCursor(Qt::CrossCursor);
        QSettings settings;
        settings.beginGroup(QStringLiteral("frontier/device-card-collapsed-v3"));
        for (const QString& deviceId : settings.childKeys()) {
            m_collapsedDevices.insert(deviceId, settings.value(deviceId, true).toBool());
        }
        settings.endGroup();
        // Keep the render clock independent from telemetry without burning a
        // logical CPU on full dashboard/history repaints. Eight frames/s is
        // enough for both active and quiet states; the phase step below is
        // time-normalized, so cadence never changes represented search speed.
        m_animation.setInterval(125);
        connect(&m_animation, &QTimer::timeout, this, [this] {
            // A34: hidden presentation has no animation work. Resource/miner
            // telemetry owns separate timers and stays live for active devices.
            if (!isVisible()||property("pgn_number_lens_only").toBool()) return;
            // Render cadence is fixed and independent from all telemetry
            // processes.  Only the phase velocity follows real emitted work,
            // through a direct logarithmic projection: a large throughput
            // change is perceptible without turning the visualization into a
            // strobe or making repaint cost data-dependent.
            double workPerSecond = 0.0;
            bool minerActive = false;
            const auto includeLane = [&workPerSecond, &minerActive](
                                         const FrontierTelemetrySnapshot& lane) {
                if (!minerLaneLive(lane)) return;
                minerActive = true;
                const auto workRate = FrontierFirstEmittedProducerRate(
                    {lane.gapPairsWindowPerSecond, lane.epochGapPairsPerSecond,
                     lane.gapPairsTestedPerSecond, lane.powmPerSecond});
                if (workRate) workPerSecond += *workRate;
            };
            if (m_hasTelemetry) includeLane(m_telemetry);
            if (m_hasSecondaryTelemetry) includeLane(m_secondaryTelemetry);
            for (auto iterator = m_additionalTelemetry.cbegin();
                 iterator != m_additionalTelemetry.cend(); ++iterator) {
                includeLane(iterator.value());
            }
            const int desiredRenderIntervalMs = 125;
            if (m_animation.interval() != desiredRenderIntervalMs) {
                m_animation.setInterval(desiredRenderIntervalMs);
            }
            double target = std::clamp(std::log10(1.0 + workPerSecond) / 6.0, 0.0, 1.0);
            if (workPerSecond <= 0.0 && m_hasResourceTelemetry) {
                double peakUtilization = 0.0;
                for (const FrontierResourceDevice& device : m_resourceTelemetry.devices) {
                    peakUtilization = std::max(
                        peakUtilization, device.utilizationPercent.value_or(0.0));
                }
                target = std::clamp(peakUtilization / 285.0, 0.0, 0.35);
            }
            m_activityAnimationRatio = target;
            const double elapsedScale = desiredRenderIntervalMs / 33.0;
            const double phaseStep =
                (0.0022 + 0.0218 * m_activityAnimationRatio) * elapsedScale;
            m_phase = std::fmod(m_phase + phaseStep, 1.0);
            update();
        });
        m_animation.start();
        m_deviceSpring.setDuration(360);
        m_deviceSpring.setEasingCurve(QEasingCurve::OutCubic);
        connect(&m_deviceSpring,&QVariantAnimation::valueChanged,this,[this](const QVariant& value){
            m_deviceScrollX=value.toDouble();update();
        });
    }

    ~ObservatoryCanvas() override
    {
        // Do not leave a worker callback racing the QWidget's member teardown.
        // stop() drops obsolete pending work and joins the one bounded worker
        // before QObject destruction begins.
        m_graphProjectionWorker.stop();
    }

    QImage graphControlQaImage(const QSize& size) {
        // Actual Golden graph painter; no RPC, journals, controllers or miners.
        QImage image(size,QImage::Format_ARGB32_Premultiplied);image.fill(QColor("#02131a"));
        QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);
        drawCandidatePlot(painter,QRectF(QPointF(),QSizeF(size)));
        return image;
    }

    void bindObservationCatalog(const QString& database) {
        delete m_observationCatalog;
        m_observationCatalog=new pgn::ui::CatalogLayer(database,[this]{
            m_plotImageKey.clear();update();
        },this);
    }
    void configureNumberLens(){setProperty("pgn_number_lens_only",true);m_animation.stop();}
    QJsonObject catalogDiagnostics() const {
        return {{"all",m_nativeAllCatalog?m_nativeAllCatalog->diagnostics():QJsonObject{}},
            {"current",m_nativeCurrentCatalog?m_nativeCurrentCatalog->diagnostics():QJsonObject{}}};
    }
    void bindNativeCatalog(const QString& database) {
        delete m_nativeAllCatalog;delete m_nativeCurrentCatalog;
        setProperty("pgn_catalog_source_index_seen",false);
        setProperty("pgn_catalog_archive_error",QString{});
        setProperty("pgn_catalog_archive_loaded",false);
        const auto color=deviceIdentityColor(QStringLiteral("GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac"),QStringLiteral("GPU")).rgba();
        auto changed=[this]{m_plotImageKey.clear();update();};
        m_nativeAllCatalog=new pgn::ui::CatalogLayer(database,changed,this,true,color);
        m_nativeCurrentCatalog=new pgn::ui::CatalogLayer(database,changed,this,true,color);
        setProperty("pgn_native_indexed",true);
    }
    bool selectNativeGraphAxis(const QString& axis) {
        if(axis!="time"&&axis!="length")return false;
        m_graphXAxisMode=axis=="time"?FrontierGraphXAxis::Time:FrontierGraphXAxis::Length;
        m_graphZoomActive=false;m_hoveredEventId.clear();m_plotImageKey.clear();
        setProperty("pgn_requested_graph_axis",axis);update();return true;
    }
    void presentNativeIndex(const QJsonObject& packet) {
        if(!m_nativeAllCatalog||!m_nativeCurrentCatalog)return;
        const bool archiveOnly=packet.value("archive_only")==true;
        // Startup SQL can finish after a newer SOURCE publication. It must
        // never regress that index or replace its actual current-run scope.
        if(archiveOnly&&property("pgn_catalog_source_index_seen").toBool())return;
        if(!packet.value("complete").toBool()){
            if(archiveOnly){setProperty("pgn_catalog_archive_error",packet.value("error").toString());update();}
            return;
        }
        if(!archiveOnly)setProperty("pgn_catalog_source_index_seen",true);
        setProperty("pgn_catalog_archive_error",QString{});
        const auto bounds=[](const QJsonObject& object){
            pgn::ui::CatalogBounds b;b.minX=object.value("min_x").toDouble();b.maxX=object.value("max_x").toDouble();
            b.minY=object.value("min_y").toDouble();b.maxY=object.value("max_y").toDouble();
            b.points=object.value("points").toInteger();b.revision=object.value("revision").toInteger();return b;
        };
        const auto all=packet.value("all").toObject(),current=packet.value("current").toObject();
        const auto previousAll=m_nativeAllCatalog->serial(),previousCurrent=m_nativeCurrentCatalog->serial();
        m_nativeAllCatalog->publishBounds({},bounds(all.value("length").toObject()),bounds(all.value("time").toObject()));
        if(!archiveOnly)m_nativeCurrentCatalog->publishBounds(current.value("scope").toString(),bounds(current.value("length").toObject()),bounds(current.value("time").toObject()));
        if(archiveOnly)setProperty("pgn_catalog_archive_loaded",true);
        // Stored index counts are not the last-painted viewport. They remain
        // valid while Mining is hidden behind Community or Wallet.
        setProperty("pgn_catalog_time_points",m_nativeAllCatalog->timeBounds().points);
        setProperty("pgn_catalog_total_points",m_nativeAllCatalog->lengthBounds().points);
        setProperty("pgn_catalog_undated_points",m_nativeAllCatalog->lengthBounds().points-m_nativeAllCatalog->timeBounds().points);
        setProperty("pgn_graph_history_points",m_nativeAllCatalog->lengthBounds().points);
        if(!archiveOnly){setProperty("pgn_catalog_current_time_points",m_nativeCurrentCatalog->timeBounds().points);
            setProperty("pgn_catalog_current_scope",current.value("scope").toString());}
        if(previousAll==m_nativeAllCatalog->serial()&&previousCurrent==m_nativeCurrentCatalog->serial())return;
        setProperty("pgn_archive_pairs",m_nativeAllCatalog->lengthBounds().points);
        m_plotImageKey.clear();update();
    }
    void bindBookingModel(pgn::booking::BookingModel* model) {
        m_pgnBookingModel=model;
        auto changed=[this]{
            refreshBookingRail();const auto& job=m_pgnBookingModel->runtime();
            setProperty("pgn_booking_run",job.run);setProperty("pgn_booking_positions",job.positions);
            setProperty("pgn_booking_start",job.start);setProperty("pgn_booking_end",job.end);
            setProperty("pgn_booking_observed_utc",job.observedUtc.toUTC().toString(Qt::ISODateWithMs));update();
        };
        changed();model->subscribe(this,changed);
    }
    QRectF reservationSegmentRect(const QString& id)const {
        for(const auto& hit:m_pgnBookingHits)if(hit.second==id)return hit.first;
        return {};
    }
    QColor deviceAccentFor(const QString& id) const { return deviceIdentityColor(id,QStringLiteral("GPU")); }

    void setNetworkObservation(const std::optional<pgn::wallet::ChainObservation>& observation)
    {
        if(observation&&!observation->valid())return;
        m_chainObservation=observation;
        setProperty("pgn_wallet_height",observation?QVariant(QString::number(observation->height)):QVariant{});
        setProperty("pgn_wallet_genesis",observation?QVariant(observation->genesis):QVariant{});
        setProperty("pgn_wallet_observed_utc",observation?QVariant(observation->observedUtc):QVariant{});
        update();
    }

    void setTelemetrySnapshot(const FrontierTelemetrySnapshot& snapshot, bool secondary = false)
    {
        if (m_legacyR4Mode && snapshot.sourceMode == QLatin1String("live") &&
            !snapshot.runId.isEmpty() && snapshot.observedUtc.isValid() &&
            snapshot.state != QLatin1String("stopped") &&
            snapshot.state != QLatin1String("failed")) {
            const auto birth=QDateTime::fromString(snapshot.raw.value("pgn_run_started_utc").toString(),Qt::ISODateWithMs);
            const qint64 observedMs = birth.isValid()?birth.toMSecsSinceEpoch():snapshot.observedUtc.toUTC().toMSecsSinceEpoch();
            auto iterator = m_legacyRunStartedUtcMs.find(snapshot.runId);
            if (iterator == m_legacyRunStartedUtcMs.end()) {
                m_legacyRunStartedUtcMs.insert(snapshot.runId, observedMs);
                m_lastLegacyRunStartedUtcMs = observedMs;
            } else {
                *iterator = std::min(*iterator, observedMs);
            }
        }
        recordLaneSample(snapshot, secondary);
        if (secondary) {
            m_secondaryTelemetry = snapshot;
            m_hasSecondaryTelemetry = true;
        } else {
            m_telemetry = snapshot;
            m_hasTelemetry = true;
        }
        update();
    }

    void setAdditionalTelemetrySnapshot(const QString& stableId,
                                        const FrontierTelemetrySnapshot& snapshot)
    {
        if (stableId.isEmpty()) return;
        if (m_legacyR4Mode && snapshot.sourceMode == QLatin1String("live") &&
            !snapshot.runId.isEmpty() && snapshot.observedUtc.isValid() &&
            snapshot.state != QLatin1String("stopped") &&
            snapshot.state != QLatin1String("failed")) {
            const auto birth=QDateTime::fromString(snapshot.raw.value("pgn_run_started_utc").toString(),Qt::ISODateWithMs);
            const qint64 observedMs = birth.isValid()?birth.toMSecsSinceEpoch():snapshot.observedUtc.toUTC().toMSecsSinceEpoch();
            auto iterator = m_legacyRunStartedUtcMs.find(snapshot.runId);
            if (iterator == m_legacyRunStartedUtcMs.end()) {
                m_legacyRunStartedUtcMs.insert(snapshot.runId, observedMs);
                m_lastLegacyRunStartedUtcMs = observedMs;
            } else {
                *iterator = std::min(*iterator, observedMs);
            }
        }
        m_additionalTelemetry.insert(stableId, snapshot);
        update();
    }

    void setCpuControllerSnapshot(const FrontierTelemetrySnapshot& sample) {
        auto& control=m_deviceControls[sample.gpuUuid];
        const int maximum=sample.raw.value("pgn_cpu_maximum_workers").toInt();
        const int requested=sample.raw.value("pgn_cpu_requested_threads").toInt();
        if(maximum>0&&requested>0&&control.status!=FrontierDeviceControlStatus::Pending&&m_draggingBudgetDevice!=sample.gpuUuid)
            control.budgetPercent=qRound(100.*requested/maximum);
        setAdditionalTelemetrySnapshot(sample.gpuUuid,sample);
    }

    void setVerifierTelemetrySnapshot(const FrontierTelemetrySnapshot& snapshot)
    {
        m_verifierTelemetry = snapshot;
        m_hasVerifierTelemetry = true;
        update();
    }

    void beginGraphDataBatch()
    {
        ++m_graphDataBatchDepth;
    }

    void endGraphDataBatch()
    {
        if (m_graphDataBatchDepth <= 0) return;
        --m_graphDataBatchDepth;
        if (m_graphDataBatchDepth != 0) return;
        if(!m_nativeAllCatalog)setProperty("pgn_archive_pairs",static_cast<qlonglong>(m_candidates.size()));
        if (m_graphDataBatchNeedsSort) {
            std::stable_sort(m_candidates.begin(), m_candidates.end(),
                             [](const CandidatePoint& a, const CandidatePoint& b) {
                return a.observedUtc < b.observedUtc;
            });
            rebuildCandidateIndex();
            m_graphDataBatchNeedsSort = false;
        }
        // This is only the rich tooltip/latest-result cache. Every admitted
        // native row is already committed to SQLite before reaching the UI;
        // complete graphs/counts use its index, never this bounded cache.
        if(m_nativeAllCatalog&&m_candidates.size()>512){
            FrontierLosslessCandidateArchive<CandidatePoint> retained;
            for(std::size_t i=m_candidates.size()-512;i<m_candidates.size();++i)retained.append(m_candidates[i]);
            m_candidates=std::move(retained);rebuildCandidateIndex();
        }
        setProperty("pgn_rich_cache_points",static_cast<qlonglong>(m_candidates.size()));
        if (m_graphDataBatchDirty) {
            m_graphDataBatchDirty = false;
            ++m_graphProjectionGeneration;
            update();
        }
        if (!m_resolvingPendingGapEvents && !m_pendingGapEvents.isEmpty()) {
            requestPendingGapDifficultyRange();
        }
    }

    void markGraphDataChanged()
    {
        if (m_graphDataBatchDepth > 0) {
            m_graphDataBatchDirty = true;
            return;
        }
        ++m_graphProjectionGeneration;
        update();
    }

    void addGapEvent(const FrontierGapEvent& event, bool secondary = false)
    {
        if (property("pgn_graph_inputs_paused").toBool()) return;
        bool gapOk = false;
        bool meritOk = false;
        // Exact receipts upgrade the graph coordinate in every view.  Legacy
        // probable events retain their declared candidate gap, but proof state
        // remains explicit; chronology is metadata and never an X coordinate.
        const QString graphGap = !event.exactGap.isEmpty() ? event.exactGap : event.gap;
        const double gap = graphGap.toDouble(&gapOk);
        const double merit = event.merit.toDouble(&meritOk);
        if (!gapOk || !meritOk || gap <= 0.0 || merit <= 0.0 ||
            (!event.observedUtc.isValid() && !event.domainId.startsWith(QLatin1String("pgn-")))) return;
        // Archive and producer telemetry remain lossless.  An exact durable
        // result also stays visible while authenticated historical difficulty
        // is loading or temporarily unavailable.  It is target-pending, never
        // claimed network-eligible, and the dynamic projection removes it only
        // if a later authenticated target proves it below the boundary.
        if (!event.domainId.startsWith(QLatin1String("pgn-")) && event.domainId != QLatin1String("gapcoin-platinium-lab")) {
            const FrontierGapGraphIngestionDecision graphDecision =
                FrontierClassifyGapGraphProjection(
                    event.meritQ48.toStdString(), event.networkTargetQ48.toStdString(),
                    merit, applicableNetworkDifficulty(event.observedUtc));
            const QString pendingKey = pendingGapEventKey(event, secondary);
            if (graphDecision == FrontierGapGraphIngestionDecision::AwaitNetworkDifficulty) {
                rememberPendingGapEvent(pendingKey, event, secondary);
            } else {
                m_pendingGapEvents.remove(pendingKey);
                if (graphDecision ==
                    FrontierGapGraphIngestionDecision::RejectBelowNetworkDifficulty) return;
            }
        }

        CandidatePoint point;
        point.sequence = event.sequence;
        point.gap = gap;
        point.merit = merit;
        point.meritQ48 = event.meritQ48;
        point.networkTargetQ48 = event.networkTargetQ48;
        point.status = event.proofState.isEmpty() ? QStringLiteral("UNVERIFIED")
                                                  : event.proofState;
        point.terminalDisposition = m_candidateTerminalDispositions.value(
            event.candidateId.trimmed().toLower());
        point.left = event.endpointLeft;
        point.right = event.endpointRight;
        point.leftGlobalIndex = event.leftGlobalIndex;
        point.rightGlobalIndex = event.rightGlobalIndex;
        point.segment = event.segment;
        point.lane = secondary ? QStringLiteral("B") : QStringLiteral("A");
        point.runId = event.runId;
        point.domainId = event.domainId;
        point.candidateId = event.candidateId;
        if (!event.networkOutcome.trimmed().isEmpty()) {
            point.networkOutcome = event.networkOutcome.trimmed().toLower();
            point.networkOutcomeUtc = event.observedUtc.toUTC();
        }
        const auto savedOutcome = m_gapNetworkOutcomes.constFind(
            gapNetworkOutcomeKey(point.runId, point.candidateId));
        if (savedOutcome != m_gapNetworkOutcomes.cend()) {
            point.networkOutcome = savedOutcome->first;
            point.networkOutcomeUtc = savedOutcome->second;
        }
        if (point.terminalDisposition.isEmpty() && candidateRejectedOrStale(point)) {
            point.terminalDisposition = point.networkOutcome.toLower().contains(
                QStringLiteral("stale")) ? QStringLiteral("STALE") : QStringLiteral("REJECTED");
        }
        point.blockHeight = event.blockHeight;
        point.blockHash = event.blockHash;
        point.certificateKind = event.certificateKind;
        point.certificateSha256 = event.certificateSha256;
        point.numberBits = event.numberBits;
        point.computeShift = event.computeShift;
        point.verifiedUtc = event.verifiedUtc.toUTC();
        const FrontierTelemetrySnapshot* sourceLane = secondary
            ? &m_secondaryTelemetry : &m_telemetry;
        if (sourceLane->runId != event.runId) {
            for (auto iterator = m_additionalTelemetry.cbegin();
                 iterator != m_additionalTelemetry.cend(); ++iterator) {
                if (iterator->runId == event.runId ||
                    (!event.gpuUuid.isEmpty() && iterator->gpuUuid.compare(
                         event.gpuUuid, Qt::CaseInsensitive) == 0)) {
                    sourceLane = &iterator.value();
                    break;
                }
            }
        }
        if (sourceLane->runId == event.runId) {
            point.deviceName = sourceLane->gpuName;
            point.deviceId = event.gpuUuid.isEmpty() ? sourceLane->gpuUuid : event.gpuUuid;
            if (!point.numberBits) point.numberBits = sourceLane->numberBits;
            if (!point.computeShift) point.computeShift = sourceLane->computeShift;
        } else if (!event.gpuUuid.isEmpty()) {
            point.deviceId = event.gpuUuid;
        }
        point.observedUtc = event.observedUtc.toUTC();
        // Presentation identity preserves the full escaped tuple. No digest is
        // computed per discovery and no truncated hash can collide silently.
        const QJsonArray identity{point.lane, point.runId,
            point.observedUtc.toString(Qt::ISODateWithMs), graphGap, event.merit,
            point.left, point.right, point.leftGlobalIndex, point.rightGlobalIndex};
        point.eventId = QString::fromUtf8(QJsonDocument(identity).toJson(QJsonDocument::Compact));
        // A duplicate may still upgrade proof metadata, merit or chronology.
        // Publication is coalesced when a history chunk is being admitted.
        const qsizetype duplicateIndex = findCandidateIndex(point);
        if (duplicateIndex >= 0) {
            CandidatePoint* duplicate = &m_candidates[static_cast<std::size_t>(duplicateIndex)];
            if (duplicate->deviceId.isEmpty() && !point.deviceId.isEmpty()) {
                duplicate->deviceId = point.deviceId;
                duplicate->deviceName = point.deviceName;
                duplicate->lane = point.lane;
            }
            if (duplicate->meritQ48.isEmpty() && !point.meritQ48.isEmpty()) {
                duplicate->meritQ48 = point.meritQ48;
            }
            if (duplicate->networkTargetQ48.isEmpty() && !point.networkTargetQ48.isEmpty()) {
                duplicate->networkTargetQ48 = point.networkTargetQ48;
            }
            if (candidateRejectedOrStale(point) || duplicate->networkOutcome.isEmpty()) {
                if (!point.networkOutcome.isEmpty()) {
                    duplicate->networkOutcome = point.networkOutcome;
                    duplicate->networkOutcomeUtc = point.networkOutcomeUtc;
                    if (duplicate->terminalDisposition.isEmpty()) {
                        duplicate->terminalDisposition = point.terminalDisposition;
                    }
                }
            }
            if (duplicate->terminalDisposition.isEmpty() &&
                !point.terminalDisposition.isEmpty()) {
                duplicate->terminalDisposition = point.terminalDisposition;
                duplicate->proofTransitionAtMs = QDateTime::currentMSecsSinceEpoch();
            }
            const FrontierProofTimelineTimes mergedTimes = FrontierMergeProofTimelineTimes(
                {duplicate->observedUtc.isValid()
                     ? duplicate->observedUtc.toMSecsSinceEpoch() : -1,
                 duplicate->verifiedUtc.isValid()
                     ? duplicate->verifiedUtc.toMSecsSinceEpoch() : -1},
                {point.observedUtc.isValid() ? point.observedUtc.toMSecsSinceEpoch() : -1,
                 point.verifiedUtc.isValid() ? point.verifiedUtc.toMSecsSinceEpoch() : -1});
            const bool discoveryChanged = mergedTimes.discoveredUtcMs >= 0 &&
                duplicate->observedUtc.toMSecsSinceEpoch() != mergedTimes.discoveredUtcMs;
            if (mergedTimes.discoveredUtcMs >= 0) {
                duplicate->observedUtc = QDateTime::fromMSecsSinceEpoch(
                    mergedTimes.discoveredUtcMs, Qt::UTC);
            }
            if (mergedTimes.verifiedUtcMs >= 0) {
                duplicate->verifiedUtc = QDateTime::fromMSecsSinceEpoch(
                    mergedTimes.verifiedUtcMs, Qt::UTC);
            }
            if (FrontierProofRevisionDominates(duplicate->status.toStdString(),
                                               point.status.toStdString())) {
                duplicate->status = point.status;
                duplicate->gap = point.gap;
                duplicate->merit = point.merit;
                duplicate->left = point.left;
                duplicate->right = point.right;
                duplicate->certificateKind = point.certificateKind;
                duplicate->certificateSha256 = point.certificateSha256;
                duplicate->candidateId = point.candidateId;
                duplicate->proofTransitionAtMs = QDateTime::currentMSecsSinceEpoch();
                if (point.numberBits) duplicate->numberBits = point.numberBits;
                if (point.computeShift) duplicate->computeShift = point.computeShift;
            }
            if (discoveryChanged && m_graphDataBatchDepth > 0) {
                indexCandidate(*duplicate, duplicateIndex);
                m_graphDataBatchNeedsSort = true;
            } else if (discoveryChanged) {
                std::stable_sort(m_candidates.begin(), m_candidates.end(),
                                 [](const CandidatePoint& a, const CandidatePoint& b) {
                    return a.observedUtc < b.observedUtc;
                });
                rebuildCandidateIndex();
            } else {
                indexCandidate(*duplicate, duplicateIndex);
            }
            markGraphDataChanged();
            return;
        }
        point.appendedAtMs = QDateTime::currentMSecsSinceEpoch();
        point.proofTransitionAtMs = point.appendedAtMs;
        const auto chronologicalLess = [](const CandidatePoint& a, const CandidatePoint& b) {
            return a.observedUtc < b.observedUtc;
        };
        if (m_candidates.empty() || !chronologicalLess(point, m_candidates.back())) {
            m_candidates.append(point);
            indexCandidate(m_candidates.back(),
                           static_cast<qsizetype>(m_candidates.size() - 1));
        } else if (m_graphDataBatchDepth > 0) {
            // Preserve O(1) ingestion inside a chunk.  A single stable sort and
            // index rebuild is paid at the publication boundary, never once
            // per historical row.
            m_candidates.append(point);
            indexCandidate(m_candidates.back(),
                           static_cast<qsizetype>(m_candidates.size() - 1));
            m_graphDataBatchNeedsSort = true;
        } else {
            const auto position = std::lower_bound(m_candidates.begin(), m_candidates.end(),
                                                   point, chronologicalLess);
            m_candidates.insert(position, point);
            rebuildCandidateIndex();
        }
        const qint64 age = point.observedUtc.msecsTo(QDateTime::currentDateTimeUtc());
        if (age >= -1000 && age <= 4000) m_pulsingEventId = point.eventId;
        markGraphDataChanged();
    }

    void setGapNetworkOutcome(const QString& runId,
                              const QString& candidateId,
                              const QString& outcome,
                              const QDateTime& observedUtc)
    {
        const QString canonicalRunId = runId.trimmed();
        const QString canonicalCandidateId = candidateId.trimmed();
        const QString canonicalOutcome = outcome.trimmed().toLower();
        if (canonicalRunId.isEmpty() || canonicalCandidateId.isEmpty() ||
            canonicalOutcome.isEmpty() || !observedUtc.isValid()) return;
        if (!canonicalOutcome.contains(QStringLiteral("accept")) &&
            !canonicalOutcome.contains(QStringLiteral("reject")) &&
            !canonicalOutcome.contains(QStringLiteral("stale"))) return;
        const QDateTime canonicalUtc = observedUtc.toUTC();
        const QString key = gapNetworkOutcomeKey(canonicalRunId, canonicalCandidateId);
        const auto isNegative = [](const QString& value) {
            const QString lowered = value.toLower();
            return lowered.contains(QStringLiteral("reject")) ||
                lowered.contains(QStringLiteral("stale"));
        };
        const auto existing = m_gapNetworkOutcomes.constFind(key);
        if (existing != m_gapNetworkOutcomes.cend()) {
            // A terminal node verdict outranks a reordered or late positive
            // heartbeat; restart replay must not repaint red as accepted.
            if (isNegative(existing->first) && !isNegative(canonicalOutcome)) return;
            if (existing->second > canonicalUtc) return;
            if (existing->second == canonicalUtc && existing->first == canonicalOutcome) return;
        }
        m_gapNetworkOutcomes.insert(key, qMakePair(canonicalOutcome, canonicalUtc));
        bool changed = false;
        for (CandidatePoint& point : m_candidates) {
            if (point.runId != canonicalRunId ||
                point.candidateId.compare(canonicalCandidateId, Qt::CaseInsensitive) != 0) {
                continue;
            }
            if (isNegative(point.networkOutcome) && !isNegative(canonicalOutcome)) continue;
            if (point.networkOutcomeUtc.isValid() && point.networkOutcomeUtc > canonicalUtc) continue;
            if (point.networkOutcome == canonicalOutcome &&
                point.networkOutcomeUtc == canonicalUtc) continue;
            point.networkOutcome = canonicalOutcome;
            point.networkOutcomeUtc = canonicalUtc;
            if (isNegative(canonicalOutcome)) {
                point.terminalDisposition = canonicalOutcome.contains(QStringLiteral("stale"))
                    ? QStringLiteral("STALE") : QStringLiteral("REJECTED");
                m_candidateTerminalDispositions.insert(
                    point.candidateId.trimmed().toLower(), point.terminalDisposition);
            }
            changed = true;
        }
        if (changed) update();
    }

    void markGapEventTerminal(const QString& candidateId,
                              FrontierGapTerminalDisposition disposition)
    {
        const QString key = candidateId.trimmed().toLower();
        if (key.isEmpty() || disposition == FrontierGapTerminalDisposition::None) return;
        const QString label = disposition == FrontierGapTerminalDisposition::Rejected
            ? QStringLiteral("REJECTED") : QStringLiteral("STALE");
        m_candidateTerminalDispositions.insert(key, label);
        bool changed = false;
        for (CandidatePoint& point : m_candidates) {
            if (point.candidateId.trimmed().compare(key, Qt::CaseInsensitive) != 0 ||
                point.terminalDisposition == label) {
                continue;
            }
            point.terminalDisposition = label;
            point.proofTransitionAtMs = QDateTime::currentMSecsSinceEpoch();
            changed = true;
        }
        if (changed) update();
    }

    HorizonDifficultyGraph* networkDifficultyGraph() const
    { return m_difficultyMode ? m_difficultyMode->graph() : nullptr; }
    void addLegacyDifficultySample(const QDateTime& observedUtc, double difficulty,
                                  bool blockTimestamp = false)
    {
        if (!m_legacyR4Mode || !observedUtc.isValid() ||
            !std::isfinite(difficulty) || difficulty <= 0.0) return;
        const qint64 timestamp = observedUtc.toUTC().toMSecsSinceEpoch();
        if (m_difficultyMode) m_difficultyMode->graph()->appendObservation(
            HorizonDifficulty::Coin::Gapcoin, QStringLiteral("gapcoin-wallet-observations-v1"),
            timestamp, difficulty, blockTimestamp ? HorizonDifficulty::Clock::BlockUtc
                                                  : HorizonDifficulty::Clock::ObservationUtc);
        const auto position = std::lower_bound(
            m_legacyDifficulty.begin(), m_legacyDifficulty.end(), timestamp,
            [](const QPair<qint64, double>& sample, qint64 value) {
                return sample.first < value;
            });
        if (position != m_legacyDifficulty.end() && position->first == timestamp) {
            position->second = difficulty;
        } else {
            m_legacyDifficulty.insert(position, qMakePair(timestamp, difficulty));
        }
        constexpr qsizetype kMaximumSamples = 20000;
        if (m_legacyDifficulty.size() > kMaximumSamples) {
            m_legacyDifficulty.remove(0, m_legacyDifficulty.size() - kMaximumSamples);
        }
        markGraphDataChanged();
    }

    void addLegacyDifficultyCoverage(qint64 minimumUtcMs, qint64 maximumUtcMs)
    {
        if (!m_legacyR4Mode || maximumUtcMs < minimumUtcMs) return;
        m_legacyDifficultyCoverage.append(qMakePair(minimumUtcMs, maximumUtcMs));
        constexpr qsizetype kMaximumCoverageRanges = 64;
        if (m_legacyDifficultyCoverage.size() > kMaximumCoverageRanges) {
            m_legacyDifficultyCoverage.remove(
                0, m_legacyDifficultyCoverage.size() - kMaximumCoverageRanges);
        }
        resolvePendingGapEvents();
    }

    void setResourceTelemetry(const FrontierResourceTelemetrySnapshot& snapshot)
    {
        if (snapshot.sequence == m_resourceTelemetry.sequence &&
            snapshot.observedUtc == m_resourceTelemetry.observedUtc &&
            snapshot.receivedMonotonicNs == m_resourceTelemetry.receivedMonotonicNs &&
            snapshot.valid == m_resourceTelemetry.valid &&
            snapshot.stale == m_resourceTelemetry.stale &&
            snapshot.error == m_resourceTelemetry.error) {
            return;
        }
        m_resourceTelemetry = snapshot;
        m_hasResourceTelemetry = snapshot.valid;
        if (snapshot.valid && !snapshot.stale) {
            for (const FrontierResourceDevice& device : snapshot.devices) {
                QVector<LaneSample>& history = m_resourceHistories[device.deviceId];
                if (!history.isEmpty() && history.constLast().sequence == snapshot.sequence) continue;
                LaneSample sample;
                sample.sequence = snapshot.sequence;
                sample.observedUtc = snapshot.observedUtc;
                sample.utilizationEmitted = device.utilizationPercent.has_value();
                sample.utilization = device.utilizationPercent.value_or(0.0);
                bool usedOk = false;
                bool totalOk = false;
                const double used = device.memoryUsedBytes.toDouble(&usedOk);
                const double total = device.memoryTotalBytes.toDouble(&totalOk);
                sample.vramEmitted = usedOk && totalOk && total > 0.0;
                sample.vramFraction = sample.vramEmitted ? used / total : 0.0;
                sample.powerEmitted = device.powerWatts.has_value();
                sample.powerWatts = device.powerWatts.value_or(0.0);
                sample.temperatureEmitted = device.temperatureC.has_value();
                sample.temperatureC = device.temperatureC.value_or(0.0);
                history.append(sample);
                while (history.size() > 240) history.removeFirst();
            }
        }
        update();
    }

    void setTargetCurve(const FrontierTargetCurveLoadResult& curve)
    {
        if (curve.accepted == m_targetCurve.accepted &&
            curve.error == m_targetCurve.error &&
            curve.snapshot.observedUtc == m_targetCurve.snapshot.observedUtc) {
            return;
        }
        m_targetCurve = curve;
        ++m_graphProjectionGeneration;
        resolvePendingGapEvents();
        update();
    }

    void setDeviceControlHandler(std::function<void(const FrontierDeviceControlCommand&)> handler)
    {
        m_deviceControlHandler = std::move(handler);
        update();
    }
    QRectF miningToggleRect(const QString& id) const {
        for (const auto& hit : m_deviceControlHits) if (hit.deviceId == id) return hit.toggle;
        return {};
    }
    QRectF miningBudgetRect(const QString& id)const{for(const auto& hit:m_deviceControlHits)if(hit.deviceId==id)return hit.slider;return {};}
    QStringList miningTooltips() const {
        QStringList descriptions;
        for (const auto& hit : m_uiTooltipHits) if (!hit.text.isEmpty()) descriptions.append(hit.text);
        return descriptions;
    }

    void setGpuControllerSnapshot(const FrontierGpuControllerUi::Snapshot& snapshot)
    {
        m_gpuControllerSnapshot = snapshot;
        update();
    }

    void setLegacyDifficultyRangeHandler(
        std::function<void(qint64, qint64)> handler)
    {
        m_legacyDifficultyRangeHandler = std::move(handler);
        requestPendingGapDifficultyRange();
    }

    void resolveDeviceControl(const QString& deviceId,
                              const QString& requestId,
                              bool applied,
                              const QString& detail)
    {
        auto iterator = m_deviceControls.find(deviceId);
        if (iterator == m_deviceControls.end() || iterator->requestId != requestId) return;
        if (!applied && iterator->toggleRequestPending) {
            iterator->enabled = iterator->enabledBeforeRequest;
        }
        iterator->toggleRequestPending = false;
        if (applied) iterator->known = true;
        iterator->status = applied ? FrontierDeviceControlStatus::Applied
                                   : FrontierDeviceControlStatus::Rejected;
        iterator->detail = detail;
        update();
    }

    void setQaCollapsedDevices(const QStringList& deviceIds)
    {
        if (!deviceIds.isEmpty()) m_collapsedDevices.clear();
        for (const QString& deviceId : deviceIds) {
            if (!deviceId.trimmed().isEmpty()) m_collapsedDevices[deviceId] = true;
        }
        update();
    }

    void setQaGraphState(bool fullHistory, int zoomSteps)
    {
        m_showFullHistory = fullHistory;
        m_qaPendingZoomSteps = std::max(0, zoomSteps);
        update();
    }

    void setMinimumMeritFilterMode(bool enabled)
    {
        m_minimumMeritFilterMode = enabled;
        if (enabled) m_showFullHistory = true;
        update();
    }

    void setQaDeviceScrollEnd(bool scrollEnd)
    {
        m_qaDeviceScrollEnd = scrollEnd;
        update();
    }

    void setExactReceiptInventory(qulonglong count)
    {
        if (m_exactReceiptInventory == count) return;
        m_exactReceiptInventory = count;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QElapsedTimer paintClock; paintClock.start();
        if(property("pgn_values_audit_enabled").toBool())setProperty("pgn_values_audit_rects",QJsonArray{});
        m_uiTooltipHits.clear();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        QLinearGradient background(0, 0, width(), height());
        background = HorizonDeviceVisuals::canvasBrush(QRectF(0,0,width(),height()));
        painter.fillRect(rect(), background);

        if(property("pgn_number_lens_only").toBool()){
            drawCandidatePlot(painter,QRectF(rect()).adjusted(8,8,-8,-8));
            setProperty("pgn_paint_ns",paintClock.nsecsElapsed());return;
        }
        if(property("pgn_compact_mining").toBool()){
            drawDashboard(painter);drawUiTooltip(painter);
            setProperty("pgn_paint_ns",paintClock.nsecsElapsed());return;
        }
        drawFieldTexture(painter);
        drawTopTelemetry(painter);
        const auto beforeDashboard = paintClock.nsecsElapsed();
        drawDashboard(painter);
        const auto afterDashboard = paintClock.nsecsElapsed();
        drawBottomTelemetry(painter);
        drawUiTooltip(painter);
        // One actual execution, never a moving average or telemetry substitute.
        setProperty("pgn_paint_ns",paintClock.nsecsElapsed());
        setProperty("pgn_dashboard_ns",afterDashboard-beforeDashboard);
    }

    void wheelEvent(QWheelEvent* event) override
    {
        const QPoint angle = event->angleDelta();
        const QPoint pixel = event->pixelDelta();
        const bool hasPixelDelta = !pixel.isNull();
        const int dominant = hasPixelDelta
            ? (std::abs(pixel.x()) > std::abs(pixel.y()) ? pixel.x() : pixel.y())
            : (std::abs(angle.x()) > std::abs(angle.y()) ? angle.x() : angle.y());
        const double steps = dominant / (hasPixelDelta ? 50.0 : 120.0);
        if (std::abs(steps) < 1.0e-6) return;
        if(m_bookingRailArea.contains(event->position())&&m_bookingRailSegments.size()>m_bookingRailCapacity){
            m_bookingRailOffset=std::clamp(m_bookingRailOffset+(steps<0?1:-1)*m_bookingRailCapacity,
                0,std::max(0,int(m_bookingRailSegments.size())-m_bookingRailCapacity));
            update();event->accept();return;
        }
        if (m_candidateGraphRect.contains(event->position()) && m_graphBoundsValid) {
            const double xRatio = (event->position().x() - m_candidateGraphRect.left()) /
                m_candidateGraphRect.width();
            const double yRatio = (event->position().y() - m_candidateGraphRect.top()) /
                m_candidateGraphRect.height();
            const auto zoomBase = m_graphZoomActive ? m_graphView : m_graphPresentedView;
            m_graphView = m_graphXAxisMode == FrontierGraphXAxis::Time
                ? FrontierZoomGraphView(zoomBase, xRatio, yRatio, steps,
                                        60.0 * 1000.0)
                : FrontierZoomGapGraphView(zoomBase, xRatio, yRatio, steps);
            m_graphZoomActive = true;
            update();
            event->accept();
            return;
        }
        if (m_deviceStripRect.contains(event->position()) && m_deviceScrollMaximum > 0.0) {
            m_deviceScrollX = std::clamp(m_deviceScrollX - steps * 92.0,
                                         0.0, m_deviceScrollMaximum);
            update();
            event->accept();
            return;
        }
        event->ignore();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if(event->button()==Qt::LeftButton){
            for(const auto& hit:m_responsiveSectionHits)if(hit.first.contains(event->position())){
                m_responsiveSection=hit.second;update();event->accept();return;
            }
            for(const auto& hit:m_devicePageHits)if(hit.first.contains(event->position())){
                m_devicePage+=hit.second;update();event->accept();return;
            }
        }
        if(event->button()==Qt::LeftButton){for(const auto& hit:m_pgnSummaryPages)if(hit.first.contains(event->position())){
            m_pgnSummaryPage=hit.second;update();event->accept();return;}}
        if(m_pgnBookingModel&&event->button()==Qt::LeftButton){
            for(const auto& hit:m_bookingRailNavigation)if(hit.first.contains(event->position())){
                m_bookingRailOffset=std::clamp(m_bookingRailOffset+hit.second*m_bookingRailCapacity,
                    0,std::max(0,int(m_bookingRailSegments.size())-m_bookingRailCapacity));
                update();event->accept();return;
            }
            for(const auto& hit:m_pgnBookingHits)if(hit.first.contains(event->position())){
                m_pgnBookingModel->select(hit.second);event->accept();return;
            }
        }
        if (!qEnvironmentVariable("HORIZON_QA_CAPTURE_DIR").isEmpty()) {
            qInfo() << "QA_GAPCOIN_CLICK" << event->position()
                    << "controls" << m_deviceControlHits.size()
                    << "handler" << static_cast<bool>(m_deviceControlHandler);
            for (const auto& hit : m_deviceControlHits)
                qInfo() << "QA_GAPCOIN_HIT" << hit.kind << hit.toggle << hit.supported;
        }
        if (event->button() == Qt::LeftButton && m_legacyR4Mode) {
            for (const auto& preset : m_legacyTimeScaleHits) {
                if (!preset.first.contains(event->position())) continue;
                const int presetIndex = std::clamp(
                    preset.second, 0,
                    static_cast<int>(FrontierTimeRangePresetCount) - 1);
                m_legacyTimeRangeIndex = presetIndex;
                m_legacyShowAllHistory = FrontierTimeRangePresets[presetIndex].allHistory;
                if (!m_legacyShowAllHistory) {
                    m_legacyIntervalHours = std::max(1, static_cast<int>(
                        FrontierTimeRangePresets[presetIndex].spanMs /
                        (60LL * 60LL * 1000LL)));
                }
                m_graphZoomActive = false;
                m_hoveredEventId.clear();
                update();
                event->accept();
                return;
            }
        }
        if (event->button() == Qt::LeftButton) {
            for (const auto& axis : m_graphXAxisModeHits) {
                if (!axis.first.contains(event->position())) continue;
                if (m_graphXAxisMode == axis.second) break;
                m_graphXAxisMode = axis.second;
                m_graphZoomActive = false;
                m_hoveredEventId.clear();
                update();
                event->accept();
                return;
            }
        }
        if (event->button() == Qt::LeftButton && !m_legacyR4Mode &&
            m_graphXAxisMode == FrontierGraphXAxis::Time) {
            for (const auto& range : m_graphRangeHits) {
                if (!range.first.contains(event->position())) continue;
                if (m_graphRange == range.second) break;
                m_graphRange = range.second;
                m_graphZoomActive = false;
                m_hoveredEventId.clear();
                update();
                event->accept();
                return;
            }
        }
        if (event->button() == Qt::LeftButton && m_graphResetHit.contains(event->position())) {
            m_graphZoomActive = false;
            if(m_nativeAllCatalog){m_legacyTimeRangeIndex=static_cast<int>(FrontierTimeRangePresetCount)-1;m_legacyShowAllHistory=true;}
            m_hoveredEventId.clear();
            update();
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton &&
            m_candidateGraphRect.contains(event->position())) {
            const bool pointHit = std::any_of(m_candidateHits.cbegin(), m_candidateHits.cend(),
                                              [event](const CandidateHit& hit) {
                                                  return hit.area.contains(event->position());
                                              });
            if (!pointHit) {
                m_graphDragMode = GraphDragMode::Pan;
                m_graphDragStart = event->position();
                m_graphDragStartView = m_graphPresentedView;
                m_graphDragMoved = false;
                setCursor(Qt::ClosedHandCursor);
            }
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton) {
            for (const DeviceCollapseHit& hit : m_deviceCollapseHits) {
                if (hit.area.contains(event->position())) {
                    const bool collapsed = !m_collapsedDevices.value(hit.deviceId, true);
                    m_collapsedDevices[hit.deviceId] = collapsed;
                    QSettings settings;
                    settings.beginGroup(QStringLiteral("frontier/device-card-collapsed-v3"));
                    settings.setValue(hit.deviceId, collapsed);
                    settings.endGroup();
                    update();
                    event->accept();
                    return;
                }
            }
            for(const auto& group:m_totalGroupHits)if(group.second.contains(event->position())){const auto cards=miningLaneCards();bool allOn=true,pending=false;for(const auto& card:cards)if(card.kind==group.first&&pgn::ui::miningSourceManaged(this,card.stableId,card.snapshot.raw.value("pgn_cpu_control").toBool())){const auto c=m_deviceControls.value(card.stableId);allOn&=c.enabled;pending|=c.status==FrontierDeviceControlStatus::Pending;}if(!pending)for(const auto& card:cards)if(card.kind==group.first&&pgn::ui::miningSourceManaged(this,card.stableId,card.snapshot.raw.value("pgn_cpu_control").toBool())){auto& state=m_deviceControls[card.stableId];state.enabledBeforeRequest=state.enabled;state.enabled=!allOn;state.toggleRequestPending=true;requestDeviceControl({card.stableId,card.kind,{}, {},true});}event->accept();return;}
            for (const DeviceControlHit& hit : m_deviceControlHits) {
                if (hit.toggle.contains(event->position())) {
                    if (m_deviceControlHandler && hit.supported) {
                        DeviceControlState& state = m_deviceControls[hit.deviceId];
                        state.enabledBeforeRequest = state.enabled;
                        state.enabled = !state.enabled;
                        state.toggleRequestPending = true;
                    }
                    requestDeviceControl(hit);
                    event->accept();
                    return;
                }
                if (hit.slider.contains(event->position())) {
                    if (!m_deviceControlHandler || !hit.supported) {
                        requestDeviceControl(hit);
                        event->accept();
                        return;
                    }
                    m_draggingBudgetDevice = hit.deviceId;
                    setBudgetFromPosition(hit, event->position().x());
                    setCursor(Qt::SizeHorCursor);
                    event->accept();
                    return;
                }
            }
        }
        if (event->button() == Qt::LeftButton && m_deviceStripRect.contains(event->position())) {
            m_deviceSpring.stop();m_draggingDevices = true;
            m_deviceDragAnchor = event->position().x() + m_deviceScrollX;
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton && m_currentRunToggleHit.contains(event->position())) {
            m_showFullHistory = false;
            m_hoveredEventId.clear();
            update();
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton && m_fullHistoryToggleHit.contains(event->position())) {
            m_showFullHistory = true;
            m_hoveredEventId.clear();
            update();
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton && m_graphXAxisHit.contains(event->position())) {
            m_graphDragMode = GraphDragMode::PanX;
            m_graphDragStart = event->position();
            m_graphDragStartView = m_graphPresentedView;
            setCursor(Qt::SizeHorCursor);
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton && m_graphYAxisHit.contains(event->position())) {
            m_graphDragMode = GraphDragMode::PanY;
            m_graphDragStart = event->position();
            m_graphDragStartView = m_graphPresentedView;
            setCursor(Qt::SizeVerCursor);
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton &&
            m_candidateGraphRect.contains(event->position())) {
            QVector<QString> clustered;
            for (const CandidateHit& hit : m_candidateHits) {
                if (hit.area.contains(event->position())) clustered.append(hit.eventId);
            }
            if (!clustered.isEmpty()) {
                int index = clustered.indexOf(m_hoveredEventId);
                m_hoveredEventId = clustered.at((index + 1) % clustered.size());
                update();
                event->accept();
                return;
            }
        }
        if (event->button() == Qt::LeftButton &&
            (m_previousMinerHit.contains(event->position()) ||
             m_nextMinerHit.contains(event->position()))) {
            const int count = minerLanes().size();
            if (count > 1) {
                const int delta = m_previousMinerHit.contains(event->position()) ? -1 : 1;
                m_minerSelection.index = (m_minerSelection.index + delta + count) % count;
                m_minerSelection.explicitNavigation = true;
                m_minerSelection.explicitUntilMs =
                    QDateTime::currentMSecsSinceEpoch() + 24 * 60 * 60 * 1000;
                const QVector<const FrontierTelemetrySnapshot*> lanes = minerLanes();
                m_minerSelection.selectedWasLive =
                    minerLaneLive(*lanes.at(m_minerSelection.index));
                update();
            }
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        // Candidate tooltips are anchored to the actual cursor position.  Do
        // this before the early return for a point hit so the fixed +24/+18
        // placement never reuses a stale position.
        m_mousePosition = event->position();
        if (m_graphDragMode != GraphDragMode::None) {
            const QPointF delta = event->position() - m_graphDragStart;
            if (m_graphDragMode == GraphDragMode::Pan) {
                if (FrontierGraphDragExceedsThreshold(delta.x(), delta.y())) {
                    m_graphDragMoved = true;
                }
                if (m_graphDragMoved) {
                    m_graphView = FrontierPanGraphView(
                        m_graphDragStartView,
                        delta.x() / std::max(1.0, m_candidateGraphRect.width()),
                        delta.y() / std::max(1.0, m_candidateGraphRect.height()));
                    m_graphZoomActive = true;
                }
                setCursor(Qt::ClosedHandCursor);
            } else if (m_graphDragMode == GraphDragMode::PanX) {
                m_graphView = FrontierPanSingleGraphAxis(
                    m_graphDragStartView, true,
                    delta.x() / std::max(1.0, m_candidateGraphRect.width()));
                m_graphZoomActive = true;
                setCursor(Qt::SizeHorCursor);
            } else if (m_graphDragMode == GraphDragMode::PanY) {
                m_graphView = FrontierPanSingleGraphAxis(
                    m_graphDragStartView, false,
                    delta.y() / std::max(1.0, m_candidateGraphRect.height()));
                m_graphZoomActive = true;
                setCursor(Qt::SizeVerCursor);
            }
            update();
            event->accept();
            return;
        }
        if (!m_draggingBudgetDevice.isEmpty()) {
            for (const DeviceControlHit& hit : m_deviceControlHits) {
                if (hit.deviceId == m_draggingBudgetDevice) {
                    setBudgetFromPosition(hit, event->position().x());
                    setCursor(Qt::SizeHorCursor);
                    event->accept();
                    return;
                }
            }
        }
        if (m_draggingDevices) {
            m_deviceScrollX = pgn::ui::elasticDeviceScroll(
                m_deviceDragAnchor-event->position().x(),m_deviceScrollMaximum);

            update();
            event->accept();
            return;
        }
        QString hovered;
        double nearestDistance = std::numeric_limits<double>::max();
        for (const CandidateHit& hit : m_candidateHits) {
            if (hit.area.contains(event->position())) {
                if (hit.eventId == m_hoveredEventId) {
                    hovered = hit.eventId;
                    nearestDistance = -1.0;
                    break;
                }
                const double dx = hit.center.x() - event->position().x();
                const double dy = hit.center.y() - event->position().y();
                const double distance = dx * dx + dy * dy;
                if (distance < nearestDistance) {
                    nearestDistance = distance;
                    hovered = hit.eventId;
                }
            }
        }
        if (hovered != m_hoveredEventId) {
            m_hoveredEventId = hovered;
            update();
        }
        if (!hovered.isEmpty()) {
            m_hoveredUiTooltip.clear();
            setCursor(Qt::PointingHandCursor);
            event->accept();
            return;
        }
        QString uiTooltip;
        for (const UiTooltipHit& hit : m_uiTooltipHits) {
            if (hit.area.contains(event->position())) {
                uiTooltip = hit.text;
                break;
            }
        }
        if (uiTooltip != m_hoveredUiTooltip) {
            m_hoveredUiTooltip = uiTooltip;
            update();
        }
        if (!uiTooltip.isEmpty()) {
            setCursor(Qt::PointingHandCursor);
            event->accept();
            return;
        }
        if (m_previousMinerHit.contains(event->position()) ||
            m_nextMinerHit.contains(event->position())) {
            setCursor(Qt::PointingHandCursor);
        } else {
            bool overControl = std::any_of(
                m_deviceCollapseHits.cbegin(), m_deviceCollapseHits.cend(),
                [event](const DeviceCollapseHit& hit) {
                    return hit.area.contains(event->position());
                });
            for (const DeviceControlHit& hit : m_deviceControlHits) {
                if (hit.toggle.contains(event->position()) || hit.slider.contains(event->position())) {
                    overControl = true;
                    break;
                }
            }
            if (overControl) {
                setCursor(Qt::PointingHandCursor);
                QWidget::mouseMoveEvent(event);
                return;
            }
            setCursor(m_deviceStripRect.contains(event->position())
                          ? Qt::OpenHandCursor
                          : Qt::CrossCursor);
        }
        QWidget::mouseMoveEvent(event);
    }

    void leaveEvent(QEvent* event) override
    {
        if (!m_hoveredEventId.isEmpty() || !m_hoveredUiTooltip.isEmpty()) {
            m_hoveredEventId.clear();
            m_hoveredUiTooltip.clear();
            update();
        }
        QWidget::leaveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if ((event->button() == Qt::LeftButton && m_graphDragMode == GraphDragMode::Pan) ||
            (event->button() == Qt::LeftButton &&
             (m_graphDragMode == GraphDragMode::PanX ||
              m_graphDragMode == GraphDragMode::PanY))) {
            m_graphDragMode = GraphDragMode::None;
            m_graphDragMoved = false;
            setCursor(Qt::CrossCursor);
            update();
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton && !m_draggingBudgetDevice.isEmpty()) {
            for (const DeviceControlHit& hit : m_deviceControlHits) {
                if (hit.deviceId == m_draggingBudgetDevice) {
                    requestDeviceControl(hit);
                    break;
                }
            }
            m_draggingBudgetDevice.clear();
            setCursor(Qt::PointingHandCursor);
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton && m_draggingDevices) {
            m_draggingDevices = false;
            const qreal target=std::clamp(m_deviceScrollX,0.,m_deviceScrollMaximum);
            if(std::abs(target-m_deviceScrollX)>.01){
                m_deviceSpring.setStartValue(m_deviceScrollX);
                m_deviceSpring.setEndValue(target);m_deviceSpring.start();
            }
            setCursor(Qt::OpenHandCursor);
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

public:
    double animationPhaseForQa() const { return m_phase; }
    QJsonArray miningDeviceRates() const {
        QJsonArray result;
        for(const auto& card:miningLaneCards()){
            const auto& lane=card.snapshot;
            const auto summary=pgn::ui::summarizeLiveCardRates({&lane},m_pgnFinishedRuns);
            const bool cpu=card.kind=="CPU";
            const auto host=lane.raw.value("pgn_cpu_host").toString();
            const bool validation=lane.raw.value("pgn_cpu_status").toObject().value("validation_only").toBool();
            QJsonObject row{{"id",card.stableId},{"kind",card.kind},
                {"label",cpu?QString("CPU ")+host:lane.gpuName},
                {"active",minerLaneLive(lane)||(lane.stale&&!lane.terminalSealed)},{"validation_only",validation},
                {"role",cpu?QString("audit"):lane.raw.value("pgn_work_role").toString("search")}};
            if(summary.positionsRate)row.insert("positions_per_second",*summary.positionsRate);
            const auto cpuStatus=lane.raw.value("pgn_cpu_status").toObject();
            const auto counter=cpu?pgn::ui::cpuMiningRates(cpuStatus,lane.authoritative&&!lane.stale).value("positions").toString():lane.rowsCompleted;
            const auto age=pgn::mining::sourceAgeLowerBoundNs(lane.raw);
            const auto receiptAge=lane.observedUtc.isValid()?lane.observedUtc.msecsTo(QDateTime::currentDateTimeUtc()):-1;
            const bool fresh=lane.authoritative&&!lane.stale&&(age?*age>=0&&*age<=7'000'000'000LL:receiptAge>=0&&receiptAge<=5000);
            row.insert("selected",lane.raw.value("pgn_device_enabled").toBool(lane.raw.value("legacy_miner_bound").toBool(minerLaneLive(lane))));
            QString counterEpoch=lane.runId,total=cpu?cpuStatus.value("total_positions").toString():lane.rowsTotal;bool zeroOrigin=cpu&&!counter.isEmpty();
            if(!cpu&&!lane.domainStart.isEmpty()&&!lane.domainEnd.isEmpty())try{
                zeroOrigin=pgn::booking::subtractUnsigned(lane.domainEnd,lane.domainStart)==total;
                counterEpoch=lane.domainStart+"/"+lane.domainEnd;
            }catch(const std::invalid_argument&){}
            row.insert("counter_epoch",counterEpoch);row.insert("counter_total",total);row.insert("counter_zero_origin",zeroOrigin);row.insert("completed_positions",counter);row.insert("counter_fresh",fresh);
            result.append(row);
        }
        return result;
    }
    QVariantList miningCardStates() const {
        QVariantList result;
        for(const auto& card:miningLaneCards())result.append(MiningCardState(card.stableId,card.snapshot.gpuName.isEmpty()?card.label:card.snapshot.gpuName,
            card.snapshot.raw.contains("legacy_miner_bound")?card.snapshot.raw.value("legacy_miner_bound").toBool():minerLaneLive(card.snapshot)));
        return result;
    }
private:
    enum class GraphDragMode { None, Pan, PanX, PanY };

    struct LaneSample {
        qint64 sequence{-1};
        QDateTime observedUtc;
        bool utilizationEmitted{false};
        double utilization{0.0};
        bool vramEmitted{false};
        double vramFraction{0.0};
        bool powerEmitted{false};
        double powerWatts{0.0};
        bool temperatureEmitted{false};
        double temperatureC{0.0};
        bool rateEmitted{false};
        double positionsPerSecond{0.0};
    };

    struct CandidatePoint {
        QString eventId;
        qint64 sequence{-1};
        double gap{0.0};
        double merit{0.0};
        QString meritQ48;
        QString networkTargetQ48;
        QString status;
        QString networkOutcome;
        QString terminalDisposition;
        QString left;
        QString right;
        QString lane;
        QString deviceName;
        QString deviceId;
        QString runId;
        QString domainId;
        QString magnitude;
        QString logN;
        QString candidateId;
        QString row;
        QString blockHeight;
        QString blockHash;
        QString segment;
        QString leftGlobalIndex;
        QString rightGlobalIndex;
        QString certificateKind;
        QString certificateSha256;
        std::optional<int> numberBits;
        std::optional<int> computeShift;
        QDateTime observedUtc;
        QDateTime verifiedUtc;
        QDateTime networkOutcomeUtc;
        qint64 appendedAtMs{0};
        qint64 proofTransitionAtMs{0};
    };

    struct PendingGapEvent {
        FrontierGapEvent event;
        bool secondary{false};
    };

    static QString pendingGapEventKey(const FrontierGapEvent& event, bool secondary)
    {
        const QJsonArray identity{secondary ? QStringLiteral("B") : QStringLiteral("A"),
            event.runId, QString::number(event.sequence),
            event.observedUtc.toUTC().toString(Qt::ISODateWithMs), event.candidateId,
            event.exactGap.isEmpty() ? event.gap : event.exactGap,
            event.meritDecimal.isEmpty() ? event.merit : event.meritDecimal,
            event.certificateSha256};
        return QString::fromUtf8(QJsonDocument(identity).toJson(QJsonDocument::Compact));
    }

    bool legacyDifficultyCovers(qint64 observedUtcMs) const
    {
        for (const auto& coverage : m_legacyDifficultyCoverage) {
            if (observedUtcMs >= coverage.first && observedUtcMs <= coverage.second) return true;
        }
        return false;
    }

    std::optional<double> applicableNetworkDifficulty(const QDateTime& observedUtc) const
    {
        if (!observedUtc.isValid()) return std::nullopt;
        if (m_legacyR4Mode) {
            const qint64 observedUtcMs = observedUtc.toUTC().toMSecsSinceEpoch();
            if (legacyDifficultyCovers(observedUtcMs)) {
                return FrontierApplicableNetworkDifficulty(m_legacyDifficulty, observedUtcMs);
            }
            // A newly discovered live gap must not disappear from the graph
            // while the authenticated historical staircase is being fetched.
            // The wallet snapshot is authoritative only in a narrow live
            // window; it is never projected backwards onto old history.
            const qint64 liveAgeMs = std::llabs(
                QDateTime::currentDateTimeUtc().toMSecsSinceEpoch() - observedUtcMs);
            const QJsonValue liveDifficulty = m_telemetry.raw.value(
                QStringLiteral("legacy_network_difficulty"));
            if (liveAgeMs <= 10 * 60 * 1000 && liveDifficulty.isDouble() &&
                std::isfinite(liveDifficulty.toDouble()) &&
                liveDifficulty.toDouble() > 0.0) {
                return liveDifficulty.toDouble();
            }
            return std::nullopt;
        }
        if (!m_targetCurve.accepted) return std::nullopt;
        const FrontierTargetCurvePoint* target =
            m_targetCurve.snapshot.lookupAtOrBefore(observedUtc);
        return target == nullptr ? std::nullopt
                                 : std::optional<double>{target->targetMerit};
    }

    void rememberPendingGapEvent(const QString& key,
                                 const FrontierGapEvent& event,
                                 bool secondary)
    {
        m_pendingGapEvents.insert(key, PendingGapEvent{event, secondary});
        // A history chunk can contribute thousands of target-dependent rows.
        // Scan their range once at endGraphDataBatch(), never after every row.
        if (!m_resolvingPendingGapEvents && m_graphDataBatchDepth == 0)
            requestPendingGapDifficultyRange();
    }

    void requestPendingGapDifficultyRange()
    {
        if (!m_legacyR4Mode || !m_legacyDifficultyRangeHandler ||
            m_pendingGapEvents.isEmpty()) return;
        qint64 minimumUtcMs = std::numeric_limits<qint64>::max();
        qint64 maximumUtcMs = std::numeric_limits<qint64>::min();
        for (auto iterator = m_pendingGapEvents.cbegin();
             iterator != m_pendingGapEvents.cend(); ++iterator) {
            const qint64 observedUtcMs =
                iterator->event.observedUtc.toUTC().toMSecsSinceEpoch();
            minimumUtcMs = std::min(minimumUtcMs, observedUtcMs);
            maximumUtcMs = std::max(maximumUtcMs, observedUtcMs);
        }
        if (minimumUtcMs > maximumUtcMs) return;
        // Include a small predecessor/successor margin so the staircase lookup
        // always has the block target in force immediately before the event.
        constexpr qint64 kDifficultyMarginMs = 60'000;
        minimumUtcMs -= kDifficultyMarginMs;
        maximumUtcMs += kDifficultyMarginMs;
        const auto handler = m_legacyDifficultyRangeHandler;
        QTimer::singleShot(0, this, [handler, minimumUtcMs, maximumUtcMs] {
            handler(minimumUtcMs, maximumUtcMs);
        });
    }

    void resolvePendingGapEvents()
    {
        if (m_pendingGapEvents.isEmpty() || m_resolvingPendingGapEvents) return;
        const QList<PendingGapEvent> pending = m_pendingGapEvents.values();
        m_pendingGapEvents.clear();
        m_resolvingPendingGapEvents = true;
        beginGraphDataBatch();
        for (const PendingGapEvent& item : pending) {
            addGapEvent(item.event, item.secondary);
        }
        endGraphDataBatch();
        m_resolvingPendingGapEvents = false;
        requestPendingGapDifficultyRange();
    }

    FrontierGapGraphIngestionDecision candidateGraphDecision(
        const CandidatePoint& point) const
    {
        // A laboratory observation is deliberately outside the network
        // difficulty filter. Admitting it to the lab graph never marks it as
        // a submitted block or a wallet reward.
        if (point.domainId == QLatin1String("gapcoin-platinium-lab"))
            return FrontierGapGraphIngestionDecision::Admit;
        return FrontierClassifyGapGraphProjection(
            point.meritQ48.toStdString(), point.networkTargetQ48.toStdString(),
            point.merit, applicableNetworkDifficulty(point.observedUtc));
    }

    bool candidateNetworkEligible(const CandidatePoint& point) const
    {
        // Local scientific observations are not network submissions. Avoid
        // rebuilding a legacy difficulty predicate for every PGN paint row.
        if(point.domainId.startsWith(QLatin1String("pgn-")))return true;
        return candidateGraphDecision(point) !=
            FrontierGapGraphIngestionDecision::RejectBelowNetworkDifficulty;
    }

    static bool candidateExact(const CandidatePoint& point)
    {
        if(point.domainId.startsWith(QLatin1String("pgn-")) &&
           point.status==QLatin1String("prp_pending"))return false;
        if (point.domainId == QLatin1String("gapcoin-platinium-lab") &&
            point.status == QLatin1String("lab_exact_prp_not_submitted") &&
            point.certificateKind == QLatin1String("PLATINIUM-LAB-GMP25-ADJACENCY") &&
            !point.left.isEmpty() && !point.right.isEmpty()) return true;
        if (FrontierProofStateRank(point.status.toStdString()) == FrontierProofRank::Exact) {
            return true;
        }
        // Gapcoin 0.16.3 calls the terminal independent endpoint replay
        // "host_validated".  It is exact for this legacy consensus predicate
        // when (and only when) the canonical adjacency certificate and both
        // endpoints are present.  Treating the bare status as probable made
        // the header lag behind the graph even though the durable journal had
        // already accepted the same result as EXACT_GAPCOIN_PRP.
        return (point.status.compare(QStringLiteral("host_validated"), Qt::CaseInsensitive) == 0 ||
                point.status == QLatin1String("node_accepted_pending_canonical")) &&
            point.certificateKind == QLatin1String("GAPCOIN-0.16.3-PRP-ADJACENCY") &&
            !point.left.isEmpty() && !point.right.isEmpty();
    }

    static QString gapNetworkOutcomeKey(const QString& runId, const QString& candidateId)
    {
        return runId.trimmed().toLower() + QChar(0x1f) + candidateId.trimmed().toLower();
    }

    static bool candidateRejectedOrStale(const CandidatePoint& point)
    {
        const FrontierGraphStatus status = FrontierClassifyGraphStatus(
            point.status, point.networkOutcome);
        return status == FrontierGraphStatus::Rejected || status == FrontierGraphStatus::Stale;
    }

    static QStringList candidateIdentityKeys(const CandidatePoint& point)
    {
        const QChar separator(0x1f);
        const QString statusLower = point.status.toLower();
        const bool filterPoint = statusLower.contains(QStringLiteral("filter_off")) ||
            statusLower.contains(QStringLiteral("filter_on"));
        const QString filterLane = statusLower.contains(QStringLiteral("filter_off"))
            ? QStringLiteral("FILTER_OFF")
            : (statusLower.contains(QStringLiteral("filter_on"))
                   ? QStringLiteral("FILTER_ON") : QString{});
        const QString phasePrefix = filterPoint ? filterLane + separator : QString{};
        QStringList keys{QStringLiteral("EVENT") + separator + phasePrefix + point.eventId};
        if (!point.left.isEmpty() && !point.right.isEmpty()) {
            keys.append(QStringLiteral("ENDPOINTS") + separator +
                        phasePrefix +
                        point.left + separator + point.right);
        }
        if (!point.runId.isEmpty() && !point.leftGlobalIndex.isEmpty() &&
            !point.rightGlobalIndex.isEmpty()) {
            keys.append(QStringLiteral("INDICES") + separator + phasePrefix +
                        point.runId + separator +
                        point.leftGlobalIndex + separator + point.rightGlobalIndex);
        }
        if (!point.runId.isEmpty() && !point.candidateId.isEmpty()) {
            keys.append(QStringLiteral("CANDIDATE") + separator + phasePrefix +
                        point.runId + separator +
                        point.candidateId);
        }
        return keys;
    }

    qsizetype findCandidateIndex(const CandidatePoint& point) const
    {
        for (const QString& key : candidateIdentityKeys(point)) {
            const auto indexed = m_candidateIdentityIndex.constFind(key);
            if (indexed != m_candidateIdentityIndex.cend()) return indexed.value();
        }
        return -1;
    }

    void indexCandidate(const CandidatePoint& point, qsizetype index)
    {
        for (const QString& key : candidateIdentityKeys(point)) {
            m_candidateIdentityIndex.insert(key, index);
        }
    }

    void rebuildCandidateIndex()
    {
        m_candidateIdentityIndex.clear();
        for (std::size_t index = 0; index < m_candidates.size(); ++index) {
            indexCandidate(m_candidates[index], static_cast<qsizetype>(index));
        }
    }

    const CandidatePoint* candidateByEventId(const QString& eventId) const
    {
        if (eventId.isEmpty()) return nullptr;
        CandidatePoint probe;
        probe.eventId = eventId;
        const qsizetype index = findCandidateIndex(probe);
        if (index < 0 || static_cast<std::size_t>(index) >= m_candidates.size()) return nullptr;
        return &m_candidates[static_cast<std::size_t>(index)];
    }

    struct CandidateHit {
        QRectF area;
        QPointF center;
        QString eventId;
    };

    struct DeviceControlState {
        bool enabled{false};
        bool enabledBeforeRequest{false};
        bool known{false};
        bool observationSeen{false};
        bool observedEnabled{false};
        qint64 observationSequence{-1};
        QDateTime observationUtc;
        bool toggleRequestPending{false};
        int budgetPercent{100};
        FrontierDeviceControlStatus status{FrontierDeviceControlStatus::Idle};
        QString requestId;
        QString detail;
    };

    void observeDeviceControl(const QString& id,const FrontierTelemetrySnapshot& lane) {
        auto& control=m_deviceControls[id];
        if(lane.engineId==QLatin1String("frontier-exact-cpu-verifier")&&minerLaneLive(lane)){
            control.enabled=true;control.known=true;control.status=FrontierDeviceControlStatus::Applied;
            control.detail=QStringLiteral("OBSERVED PID %1 · %2 THREADS · LIVE HEARTBEAT").arg(lane.verifierProcessId,lane.verifierThreads);return;
        }
        if(control.status==FrontierDeviceControlStatus::Pending)return;
        if(lane.raw.contains("pgn_cpu_inventory")&&!lane.raw.value("pgn_cpu_control").toBool())return;
        const auto explicitState=lane.raw.value("pgn_device_enabled");
        const auto legacyState=lane.raw.value("legacy_miner_bound");
        if(!explicitState.isBool()&&!(m_legacyR4Mode&&legacyState.isBool()))return;
        const bool enabled=explicitState.toBool(legacyState.toBool());
        // Repainting an older telemetry frame must not undo a newer command ACK.
        if(control.observationSeen&&control.observationSequence==lane.sequence&&
           control.observationUtc==lane.observedUtc&&control.observedEnabled==enabled)return;
        control.observationSeen=true;control.observationSequence=lane.sequence;
        control.observationUtc=lane.observedUtc;control.observedEnabled=enabled;
        control.enabled=enabled;control.known=true;
        if(control.status!=FrontierDeviceControlStatus::Rejected)
            control.status=enabled?FrontierDeviceControlStatus::Applied:FrontierDeviceControlStatus::Idle;
    }

    static QString deviceControlCaption(const DeviceControlState& control) {
        if(control.status==FrontierDeviceControlStatus::Pending)
            return control.enabled?QStringLiteral("STARTING"):QStringLiteral("STOPPING");
        if(!control.known)return QStringLiteral("WAIT");
        return control.enabled?QStringLiteral("ON"):QStringLiteral("OFF");
    }

    struct DeviceControlHit {
        QString deviceId;
        QString kind;
        QRectF toggle;
        QRectF slider;
        bool supported{false};
    };

    struct DeviceCollapseHit {
        QString deviceId;
        QRectF area;
    };

    struct UiTooltipHit {
        QRectF area;
        QString text;
    };

    static QFont Font(int pixels, int weight = QFont::Normal, bool mono = false)
    {
        return HorizonDeviceVisuals::font(pixels,weight,mono);
    }

    static QColor Alpha(const QColor& color, int alpha)
    {
        QColor result = color;
        result.setAlpha(alpha);
        return result;
    }

    void drawModulo30Signature(QPainter& painter, const QPointF& center)
    {
        // Number-theory signature: primes > 5 can only occupy these eight
        // residue classes modulo 30.  It is deliberately tiny: an easter egg,
        // not decoration competing with the mining telemetry.
        const QColor mint(QStringLiteral("#69efc5"));
        const QColor cyan(QStringLiteral("#46c8ee"));
        const QVector<int> residues{1, 7, 11, 13, 17, 19, 23, 29};
        constexpr qreal radius = 20.0;
        painter.save();
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(48, 104, 108, 105), 1));
        painter.drawEllipse(center, radius, radius);
        painter.setPen(QPen(QColor(52, 101, 105, 48), 1));
        painter.drawEllipse(center, radius - 5.0, radius - 5.0);
        for (int index = 0; index < residues.size(); ++index) {
            const double angle = (-90.0 + residues.at(index) * 12.0) *
                3.14159265358979323846 / 180.0;
            const QPointF direction(std::cos(angle), std::sin(angle));
            const QPointF node = center + direction * radius;
            painter.setPen(Qt::NoPen);
            painter.setBrush(index % 2 == 0 ? Alpha(mint, 205) : Alpha(cyan, 205));
            painter.drawEllipse(node, 2.15, 2.15);
        }
        painter.setFont(Font(6, QFont::DemiBold, true));
        painter.setPen(QColor(QStringLiteral("#8eb8b4")));
        painter.drawText(QRectF(center.x() - 14, center.y() - 8, 28, 12),
                         Qt::AlignCenter, QStringLiteral("MOD"));
        painter.setFont(Font(8, QFont::DemiBold, true));
        painter.setPen(QColor(QStringLiteral("#e8f7f4")));
        painter.drawText(QRectF(center.x() - 14, center.y() + 1, 28, 12),
                         Qt::AlignCenter, QStringLiteral("30"));
        painter.restore();
        m_uiTooltipHits.append({QRectF(center.x() - radius - 5, center.y() - radius - 5,
                                       (radius + 5) * 2, (radius + 5) * 2),
            QStringLiteral("Prime wheel modulo 30: every prime greater than 5 lies in residue class 1, 7, 11, 13, 17, 19, 23 or 29")});
    }

    void drawPrimePipeline(QPainter& painter,
                           const QRectF& area,
                           const QVector<const FrontierTelemetrySnapshot*>& lanes,
                           int activeVerifierCount)
    {
        const QColor mint(QStringLiteral("#69efc5"));
        const QColor cyan(QStringLiteral("#46c8ee"));
        const QColor amber(QStringLiteral("#ffad4a"));
        const QColor exact(QStringLiteral("#75ff9f"));
        const QColor quiet(QStringLiteral("#50716f"));
        bool searchActive = false;
        bool survivorSignal = false;
        bool pairSignal = false;
        for (const FrontierTelemetrySnapshot* lane : lanes) {
            if (!minerLaneLive(*lane)) continue;
            searchActive = true;
            bool ok = false;
            const qulonglong survivors = lane->survivors.toULongLong(&ok);
            survivorSignal = survivorSignal || (ok && survivors > 0);
            pairSignal = pairSignal ||
                (lane->gapPairsWindowPerSecond && *lane->gapPairsWindowPerSecond > 0.0) ||
                (lane->epochGapPairsPerSecond && *lane->epochGapPairsPerSecond > 0.0) ||
                (lane->gapPairsTestedPerSecond && *lane->gapPairsTestedPerSecond > 0.0);
        }
        bool candidateSignal = false;
        bool exactSignal = immutableExactReceiptCount() > 0;
        for (auto iterator = m_candidates.crbegin(); iterator != m_candidates.crend(); ++iterator) {
            if (!iterator->observedUtc.isValid() ||
                iterator->observedUtc.toMSecsSinceEpoch() < m_uiSessionStartedUtcMs) continue;
            candidateSignal = true;
            const QString state = iterator->status.toLower();
            if (state.startsWith(QStringLiteral("exact")) ||
                state.contains(QStringLiteral("certified")) ||
                state.contains(QStringLiteral("verified"))) exactSignal = true;
            break;
        }

        struct Stage { QString label; bool active; QColor color; };
        const QVector<Stage> stages{
            {QStringLiteral("SEARCH"), searchActive, cyan},
            {QStringLiteral("SIEVE"), searchActive, mint},
            {QStringLiteral("SURVIVORS"), survivorSignal, mint},
            {QStringLiteral("PAIRS"), pairSignal, amber},
            {QStringLiteral("PRP"), candidateSignal || activeVerifierCount > 0, amber},
            {QStringLiteral("EXACT"), exactSignal, exact},
        };

        painter.save();
        painter.setBrush(QColor(2, 13, 20, 205));
        painter.setPen(QPen(QColor(42, 82, 88, 105), 1));
        painter.drawRoundedRect(area, 10, 10);
        const qreal labelWidth = 142.0;
        painter.setFont(Font(7, QFont::DemiBold, true));
        painter.setPen(QColor(QStringLiteral("#89aaa7")));
        painter.drawText(QRectF(area.left() + 12, area.top(), labelWidth - 18, area.height()),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("PRIME FILTER PIPELINE"));
        const qreal lineLeft = area.left() + labelWidth;
        const qreal lineRight = area.right() - 14.0;
        const qreal y = area.center().y();
        const qreal step = stages.size() > 1
            ? (lineRight - lineLeft) / (stages.size() - 1) : 0.0;
        painter.setPen(QPen(QColor(58, 91, 96, 100), 1.3));
        painter.drawLine(QPointF(lineLeft, y), QPointF(lineRight, y));
        for (int index = 0; index < stages.size(); ++index) {
            const Stage& stage = stages.at(index);
            const QPointF node(lineLeft + index * step, y);
            const QColor stageColor = stage.active ? stage.color : quiet;
            if (stage.active) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(Alpha(stageColor, 26));
                painter.drawEllipse(node, 9.0, 9.0);
            }
            painter.setBrush(QColor(2, 15, 22, 245));
            painter.setPen(QPen(Alpha(stageColor, stage.active ? 235 : 115),
                                stage.active ? 2.0 : 1.0));
            painter.drawEllipse(node, 4.0, 4.0);
            if (stage.active) {
                painter.setBrush(stageColor);
                painter.setPen(Qt::NoPen);
                painter.drawEllipse(node, 1.7, 1.7);
            }
            painter.setFont(Font(6, QFont::DemiBold, true));
            painter.setPen(Alpha(stageColor, stage.active ? 225 : 130));
            painter.drawText(QRectF(node.x() - step * 0.46, area.top() + 2,
                                    step * 0.92, 11),
                             Qt::AlignCenter, stage.label);
        }
        painter.restore();
        m_uiTooltipHits.append({area,
            QStringLiteral("Live prime-gap pipeline. Color is state, not decoration: search → sieve → survivors → adjacent pairs → probable-prime verification → exact receipt")});
    }

    [[nodiscard]] qulonglong immutableExactReceiptCount() const
    {
        if (m_exactReceiptInventory > 0) return m_exactReceiptInventory;
        if(m_cachedExactGeneration==m_graphProjectionGeneration)return m_cachedExactCount;
        qulonglong count = 0;
        for (const CandidatePoint& point : m_candidates) {
            const bool exact = candidateExact(point);
            // Only a sealed receipt has certificate provenance.  A producer's
            // own "verified" label must not inflate the immutable total.
            if (exact && !point.certificateSha256.isEmpty()) ++count;
        }
        m_cachedExactCount=count;m_cachedExactGeneration=m_graphProjectionGeneration;
        return count;
    }

    [[nodiscard]] FrontierVerifierProgress verifierProgress(
        const FrontierTelemetrySnapshot& lane) const
    {
        const auto counter = [](const QString& text) {
            bool ok = false;
            const qulonglong value = text.toULongLong(&ok);
            return ok ? static_cast<std::uint64_t>(value) : std::uint64_t{0};
        };
        return {
            counter(lane.verifierQueueClaimed),
            counter(lane.verifierQueueVerified),
            counter(lane.verifierQueueExact),
            counter(lane.verifierQueueErrors),
            counter(lane.verifierQueueSeen),
            static_cast<std::uint64_t>(immutableExactReceiptCount()),
        };
    }

    [[nodiscard]] QString verifierProgressText(
        const FrontierTelemetrySnapshot& lane) const
    {
        const FrontierVerifierProgress progress = verifierProgress(lane);
        return QStringLiteral("SESSION %1/%2 · TOTAL %3/%4 · PENDING %5 · %6")
            .arg(progress.sessionVerified)
            .arg(progress.sessionTotal())
            .arg(progress.totalVerified())
            .arg(progress.sourceSeenTotal)
            .arg(progress.totalPending())
            .arg(progress.caughtUp() ? QStringLiteral("CAUGHT UP")
                                     : QStringLiteral("WORKING"));
    }

    [[nodiscard]] QString verifierRateText(
        const FrontierTelemetrySnapshot& lane) const
    {
        const FrontierVerifierProgress progress = verifierProgress(lane);
        if (lane.verifierItemsPerSecond && *lane.verifierItemsPerSecond > 0.0) {
            return QStringLiteral("VERIFY WINDOW %1/s")
                .arg(*lane.verifierItemsPerSecond, 0, 'f', 2);
        }
        return progress.caughtUp() ? QStringLiteral("IDLE")
                                   : QStringLiteral("AWAITING RATE");
    }

    [[nodiscard]] FrontierVerifierDisplayState verifierDisplayState(
        const FrontierTelemetrySnapshot& lane) const
    {
        return FrontierVerifierDisplay(
            true, minerLaneLive(lane), lane.terminalSealed,
            lane.state.toStdString(), verifierProgress(lane));
    }

    [[nodiscard]] bool deviceHasFreshGapPulse(const QString& deviceId,
                                              qint64 nowMs) const
    {
        if (deviceId.isEmpty() || m_pulsingEventId.isEmpty()) return false;
        const CandidatePoint* point = candidateByEventId(m_pulsingEventId);
        return point && point->deviceId.compare(deviceId, Qt::CaseInsensitive) == 0 &&
            point->appendedAtMs > 0 && nowMs >= point->appendedAtMs &&
            nowMs - point->appendedAtMs < 3000;
    }

    void drawUiTooltip(QPainter& painter)
    {
        if (m_hoveredUiTooltip.isEmpty()) return;
        painter.save();
        painter.setFont(Font(8, QFont::DemiBold, true));
        const QFontMetrics metrics(painter.font());
        const qreal boxWidth = std::clamp<qreal>(metrics.horizontalAdvance(m_hoveredUiTooltip) + 24,
                                                 190.0, 470.0);
        const QRect measured = metrics.boundingRect(
            QRect(0, 0, qRound(boxWidth - 20), 200),
            Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
            m_hoveredUiTooltip);
        const qreal boxHeight = std::clamp<qreal>(measured.height() + 12, 30.0, 120.0);
        const FrontierTooltipPlacement placement = FrontierPlaceFixedTooltip(
            QRectF(8, 78, width() - 16, height() - 106),
            m_mousePosition, m_mousePosition, QSizeF(boxWidth, boxHeight));
        const QRectF box = placement.rect;
        painter.setBrush(QColor(1, 9, 16, 250));
        painter.setPen(QPen(QColor(QStringLiteral("#69efc5")), 1));
        painter.drawRoundedRect(box, 8, 8);
        painter.setPen(QColor(QStringLiteral("#edf9f7")));
        painter.drawText(box.adjusted(10, 4, -10, -4),
                         Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
                         m_hoveredUiTooltip);
        painter.restore();
    }

    QVector<const FrontierTelemetrySnapshot*> minerLanes() const
    {
        QVector<const FrontierTelemetrySnapshot*> lanes;
        // Operational carousel is deliberately live-only. Stale/inactive
        // devices remain visible as physical cards but cannot displace an
        // active producer. CPU auditors retain their separate engine role;
        // their work must not become search-gap output or mined tickets.
        if (m_hasTelemetry && minerLaneLive(m_telemetry)) lanes.append(&m_telemetry);
        if (m_hasSecondaryTelemetry && minerLaneLive(m_secondaryTelemetry)) {
            lanes.append(&m_secondaryTelemetry);
        }
        QStringList additionalIds = m_additionalTelemetry.keys();
        std::sort(additionalIds.begin(), additionalIds.end(),
                  [](const QString& left, const QString& right) {
                      return left.compare(right, Qt::CaseInsensitive) < 0;
        });
        for (const QString& id : additionalIds) {
            const auto iterator = m_additionalTelemetry.constFind(id);
            if (iterator != m_additionalTelemetry.cend() && minerLaneLive(iterator.value())) {
                lanes.append(&iterator.value());
            }
        }
        return lanes;
    }

    static bool minerLaneLive(const FrontierTelemetrySnapshot& lane)
    {
        if (!lane.authoritative || lane.stale || lane.terminalSealed || lane.runId.isEmpty()) return false;
        if(lane.raw.contains("pgn_process_running"))return lane.raw.value("pgn_process_running").toBool();
        const QString state = lane.state.toLower();
        return state != QLatin1String("idle") && state != QLatin1String("paused") && state != QLatin1String("complete") && state != QLatin1String("failed") &&
            state != QLatin1String("disconnected") && state != QLatin1String("stopped");
    }

    bool includeCandidate(const CandidatePoint& point,
                          const std::vector<std::string>& activeRunIds,
                          bool fullHistory) const
    {
        // PGN CURRENT belongs to the physical source run, not this window's
        // birth time. A GUI relaunch cannot erase already published work.
        if(point.domainId.startsWith(QLatin1String("pgn-")))
            return fullHistory || std::find(activeRunIds.cbegin(),activeRunIds.cend(),
                point.runId.toStdString())!=activeRunIds.cend();
        return FrontierIncludeGapEvent(point.runId.toStdString(), activeRunIds,
                                       fullHistory,
                                       point.observedUtc.toMSecsSinceEpoch(),
                                       m_uiSessionStartedUtcMs);
    }

    std::optional<qulonglong> sessionCandidateDelta(
        const FrontierTelemetrySnapshot& lane) const
    {
        if (lane.runId.isEmpty()) return std::nullopt;
        bool currentOk = false;
        const qulonglong current = lane.cumulativeGapCandidates.toULongLong(&currentOk);
        const auto baseline = m_sessionCandidateBaselines.constFind(lane.runId);
        if (!currentOk || baseline == m_sessionCandidateBaselines.cend()) return std::nullopt;
        return FrontierSessionCounterDelta(current, baseline.value());
    }

    void setBudgetFromPosition(const DeviceControlHit& hit, qreal x)
    {
        DeviceControlState& state = m_deviceControls[hit.deviceId];
        const double fraction = std::clamp((x - hit.slider.left()) / hit.slider.width(), 0.0, 1.0);
        if(hit.kind=="CPU"){const auto sample=m_additionalTelemetry.value(hit.deviceId);const int maximum=std::max(1,sample.raw.value("pgn_cpu_maximum_workers").toInt());const int workers=1+qRound(fraction*(maximum-1));state.budgetPercent=qRound(100.*workers/maximum);}
        else state.budgetPercent = std::clamp(qRound(fraction * 20.0) * 5, 5, 100);
        state.status = FrontierDeviceControlStatus::Idle;
        state.detail.clear();
        update();
    }

    void requestDeviceControl(const DeviceControlHit& hit)
    {
        DeviceControlState& state = m_deviceControls[hit.deviceId];
        state.requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!m_deviceControlHandler || !hit.supported) {
            // An unavailable capability is neutral, not a rejected command:
            // no backend request was emitted and no failure should be painted.
            state.status = FrontierDeviceControlStatus::Idle;
            state.requestId.clear();
            state.detail = hit.kind == QLatin1String("GPU")
                ? QStringLiteral("GPU CONTROL HANDLER NOT CONNECTED · NO COMMAND SENT")
                : QStringLiteral("BACKEND CONTROL NOT CONNECTED");
            update();
            return;
        }
        state.status = FrontierDeviceControlStatus::Pending;
        state.detail = QStringLiteral("AWAITING BACKEND");
        FrontierDeviceControlCommand command;
        command.requestId = state.requestId;
        command.deviceId = hit.deviceId;
        command.budgetKind = hit.kind == QLatin1String("CPU")
            ? FrontierDeviceBudgetKind::CpuThreads
            : FrontierDeviceBudgetKind::GpuPowerWorkShare;
        command.enabled = state.enabled;
        command.budgetPercent = state.budgetPercent;
        command.requestedUtc = QDateTime::currentDateTimeUtc();
        m_deviceControlHandler(command);
        update();
    }

    void recordLaneSample(const FrontierTelemetrySnapshot& snapshot, bool secondary)
    {
        if (!snapshot.authoritative || snapshot.stale || snapshot.sequence < 0) return;
        if (!snapshot.runId.isEmpty() && !m_sessionCandidateBaselines.contains(snapshot.runId)) {
            bool baselineOk = false;
            const qulonglong baseline = snapshot.cumulativeGapCandidates.toULongLong(&baselineOk);
            if (baselineOk) m_sessionCandidateBaselines.insert(snapshot.runId, baseline);
        }
        QVector<LaneSample>& history = secondary ? m_secondaryHistory : m_primaryHistory;
        if (!history.isEmpty() && history.constLast().sequence == snapshot.sequence) return;
        LaneSample sample;
        sample.sequence = snapshot.sequence;
        sample.observedUtc = snapshot.observedUtc;
        sample.utilizationEmitted = snapshot.gpuSmUtilPercent.has_value();
        sample.utilization = snapshot.gpuSmUtilPercent.value_or(0.0);
        bool usedOk = false;
        bool totalOk = false;
        const double used = snapshot.gpuVramUsedBytes.toDouble(&usedOk);
        const double total = snapshot.gpuVramTotalBytes.toDouble(&totalOk);
        sample.vramEmitted = usedOk && totalOk && total > 0.0;
        sample.vramFraction = sample.vramEmitted ? used / total : 0.0;
        sample.powerEmitted = snapshot.gpuPowerWatts.has_value();
        sample.powerWatts = snapshot.gpuPowerWatts.value_or(0.0);
        sample.temperatureEmitted = snapshot.gpuTemperatureC.has_value();
        sample.temperatureC = snapshot.gpuTemperatureC.value_or(0.0);
        const std::optional<double> displayedPositions = FrontierDisplayedRate(
            snapshot.e2eWindowPositionsPerSecond, snapshot.e2ePositionsPerSecond);
        sample.rateEmitted = displayedPositions.has_value();
        sample.positionsPerSecond = displayedPositions.value_or(0.0);
        history.append(sample);
        while (history.size() > 180) history.removeFirst();
    }

    void drawFieldTexture(QPainter& painter)
    {
        painter.save();
        painter.setPen(Qt::NoPen);
        for (int y = 86; y < height() - 55; y += 34) {
            for (int x = 26 + ((y / 34) % 2) * 17; x < width() - 25; x += 34) {
                const int alpha = 8 + ((x * 13 + y * 7) % 11);
                painter.setBrush(QColor(74, 222, 196, alpha));
                painter.drawEllipse(QPointF(x, y), 0.8, 0.8);
            }
        }
        painter.restore();
    }

    void drawRunKpis(QPainter& painter,
                     const QRectF& area,
                     const QVector<const FrontierTelemetrySnapshot*>& lanes)
    {
        QSet<QString> activeSearchRunIds;
        for (const FrontierTelemetrySnapshot* lane : lanes) {
            if (!lane || !minerLaneLive(*lane)) continue;
            activeSearchRunIds.insert(lane->runId);
        }
        const CandidatePoint* latestGap = nullptr;
        if(m_headerGeneration!=m_graphProjectionGeneration || m_headerRuns!=activeSearchRunIds ||
            m_headerFullHistory!=m_showFullHistory){
        for (const CandidatePoint& point : m_candidates) {
            if (!candidateNetworkEligible(point)&&!m_nativeAllCatalog) continue;
            const bool isExact = candidateExact(point);
            const bool inSelectedDataset = m_showFullHistory ||
                activeSearchRunIds.contains(point.runId);
            if ((isExact||m_nativeAllCatalog) && inSelectedDataset && point.observedUtc.isValid() &&
                (!latestGap || point.observedUtc > latestGap->observedUtc)) {
                latestGap = &point;
            }
        }
        m_headerGeneration=m_graphProjectionGeneration;m_headerRuns=activeSearchRunIds;m_headerFullHistory=m_showFullHistory;
        if(latestGap)m_headerLatest=*latestGap;else m_headerLatest.reset();
        }else{latestGap=m_headerLatest?&*m_headerLatest:nullptr;}
        const auto compactRate = [](const std::optional<double>& rate) {
            if (!rate || !std::isfinite(*rate) || *rate < 0.0) return QStringLiteral("AWAIT");
            if (*rate >= 1.0e9) return QStringLiteral("%1 G").arg(*rate / 1.0e9, 0, 'f', 2);
            if (*rate >= 1.0e6) return QStringLiteral("%1 M").arg(*rate / 1.0e6, 0, 'f', 2);
            if (*rate >= 1.0e3) return QStringLiteral("%1 K").arg(*rate / 1.0e3, 0, 'f', 1);
            return QString::number(*rate, 'f', 0);
        };
        {
            // PGN network work units/target are not specified by the current
            // protocol. Do not import another chain's PPS/difficulty or infer
            // a network measurement from a local engine's source telemetry.
            const std::optional<double> networkPps;
            const std::optional<double> networkDifficulty;
            // Reuse the same exact-interval summary as TOTAL/device pages.
            // Unaligned device clocks yield unavailable aggregate rates, not
            // a sum of differently sampled measurements.
            const auto sourceSummary = pgn::ui::summarizeMining(lanes, m_pgnFinishedRuns);
            QStringList profiles;
            for (const FrontierTelemetrySnapshot* lane : lanes) {
                if (!lane) continue;
                if (!lane->engineProfile.isEmpty() && !profiles.contains(lane->engineProfile)) {
                    profiles.append(lane->engineProfile);
                }
            }
            const QString configValue = profiles.isEmpty()
                ? QStringLiteral("AWAIT")
                : profiles.join(QStringLiteral(" + ")).toUpper();
            // Only source telemetry can describe the PGN engine. An unrelated
            // miner's inherited process environment is not configuration.
            const QString latestLabel = m_nativeAllCatalog?QStringLiteral("LATEST SOURCE GAP · PROOF PENDING"):latestGap
                ? QStringLiteral("LATEST GAP · %1 · S%2")
                      .arg(latestGap->observedUtc.toLocalTime().toString(QStringLiteral("HH:mm")),
                           latestGap->computeShift
                               ? QString::number(*latestGap->computeShift)
                               : QString::fromUtf8("—"))
                : QStringLiteral("LATEST GAP");
            const QString latestValue = latestGap
                ? QStringLiteral("Δ %1 · M %2")
                      .arg(latestGap->gap, 0, 'f', 0)
                      .arg(latestGap->merit, 0, 'f', 6)
                : QStringLiteral("AWAIT");
            const QString latestTooltip = m_nativeAllCatalog?QStringLiteral("Latest retained source PRP observation in the bounded recent cache; exact decimal endpoints remain in the durable archive. This is NOT an independent primality/gap certificate or verified coverage."):latestGap
                ? QStringLiteral("Latest exact gap · %1 · Δ %2 · merit %3 · shift %4 · device %5 · proof %6")
                      .arg(latestGap->observedUtc.toLocalTime().toString(QStringLiteral("dd/MM/yyyy HH:mm:ss")))
                      .arg(latestGap->gap, 0, 'f', 0)
                      .arg(latestGap->merit, 0, 'f', 6)
                      .arg(latestGap->computeShift
                               ? QString::number(*latestGap->computeShift)
                               : QString::fromUtf8("—"),
                           latestGap->deviceName.isEmpty() ? latestGap->deviceId
                                                           : latestGap->deviceName,
                           latestGap->status)
                : QStringLiteral("No exact live gap has been emitted in this UI history yet.");
            const QString workLabel = QStringLiteral("POSITIONS/s · PRIMALITY TESTS/s");
            const QString workValue = QStringLiteral("%1 · %2")
                .arg(compactRate(sourceSummary.positionsRate), compactRate(sourceSummary.testsRate));
            const QString workTooltip = QStringLiteral("Unsmoothed adjacent producer-counter deltas divided by the exact publication interval. Positions and primality tests are different units. A publication burst is not a GPU execution-time measurement. No moving mean or exponential filter. Aggregate rates require identical clock namespace and interval endpoints.");
            const QString yieldLabel = QStringLiteral("RETAINED gaps/s · PROOF STATUS");
            const QString yieldValue = QStringLiteral("%1 · SOURCE PRP / PROOF PENDING")
                .arg(compactRate(sourceSummary.retainedRate));
            const QString yieldTooltip = QStringLiteral("Retained gaps are source observations emitted above the engine's gap gate, not every processed gap, independently certified coverage or accepted network work. Rate uses the adjacent source publication interval, without smoothing. A current source observation is not promoted by an older proof receipt.");
            struct LegacyKpi {
                QString label;
                QString value;
                QColor color;
                QString tooltip;
            };
            const QVector<LegacyKpi> legacyKpis{
                {QStringLiteral("PGN · CONFIG"),
                 configValue,
                 QColor(QStringLiteral("#a9b8bf")),
                 QStringLiteral("PGN engine configuration. Actual source telemetry qualifies physical work; a UI label does not establish engine identity, performance or network acceptance.")},
                {workLabel, workValue,
                 QColor(QStringLiteral("#46c8ee")),
                 workTooltip},
                {yieldLabel, yieldValue,
                   QColor(QStringLiteral("#ffd166")),
                   yieldTooltip},
                {QStringLiteral("NETWORK / TARGET"),
                  QStringLiteral("%1 · %2")
                      .arg(compactRate(networkPps),
                           networkDifficulty
                               ? QString::number(*networkDifficulty, 'f', 3)
                               : QStringLiteral("AWAIT")),
                  QColor(QStringLiteral("#e28cff")),
                  QStringLiteral("PGN wallet/network service is not connected. Network throughput and target are unavailable.")},
                {latestLabel, latestValue, QColor(QStringLiteral("#ffad4a")), latestTooltip},
            };
            const qreal gap = 6.0;
            // The configuration tile contains the shortest payload, while the
            // physical-yield tile carries three independent units.  Equal
            // fifths made the leading part of "INTERVALS/s" disappear at the
            // compact window width, so distribute the same total width by
            // content instead of shrinking the label until it is unreadable.
            const QVector<qreal> tileWeights{0.94, 1.03, 1.30, 0.99, 0.74};
            const qreal usableWidth = area.width() - gap * (legacyKpis.size() - 1);
            qreal tileLeft = area.left();
            for (int index = 0; index < legacyKpis.size(); ++index) {
                const LegacyKpi& kpi = legacyKpis.at(index);
                const qreal tileWidth = index + 1 == legacyKpis.size()
                    ? area.right() - tileLeft
                    : usableWidth * tileWeights.at(index) / 5.0;
                const QRectF tile(tileLeft, area.top(), tileWidth, area.height());
                painter.setBrush(QColor(2, 16, 24, 235));
                painter.setPen(QPen(index == 0
                    ? QColor(QStringLiteral("#637985"))
                    : Alpha(kpi.color, 120), 1));
                painter.drawRoundedRect(tile, 7, 7);
                QFont labelFont = Font(7, QFont::DemiBold, true);
                while (labelFont.pointSize() > 6 &&
                       QFontMetrics(labelFont).horizontalAdvance(kpi.label) >
                           qFloor(tile.width() - 14)) {
                    labelFont.setPointSize(labelFont.pointSize() - 1);
                }
                const int labelWidth = QFontMetrics(labelFont).horizontalAdvance(kpi.label);
                if (labelWidth > qFloor(tile.width() - 14)) {
                    labelFont.setStretch(std::clamp(
                        qFloor(100.0 * (tile.width() - 14) / labelWidth), 1, 100));
                }
                painter.setFont(labelFont);
                painter.setPen(index == 0
                    ? QColor(QStringLiteral("#c4d0d4"))
                    : Alpha(kpi.color, 190));
                const QRectF labelRect(tile.left() + 7, tile.top() + 1,
                                       tile.width() - 14, 11);
                painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, kpi.label);
                QFont valueFont = Font(11, QFont::DemiBold, true);
                while (valueFont.pointSize() > 7 &&
                       QFontMetrics(valueFont).horizontalAdvance(kpi.value) >
                           qFloor(tile.width() - 14)) {
                    valueFont.setPointSize(valueFont.pointSize() - 1);
                }
                const int valueWidth = QFontMetrics(valueFont).horizontalAdvance(kpi.value);
                if (valueWidth > qFloor(tile.width() - 14)) {
                    valueFont.setStretch(std::clamp(
                        qFloor(100.0 * (tile.width() - 14) / valueWidth), 1, 100));
                }
                painter.setFont(valueFont);
                painter.setPen(QColor(QStringLiteral("#f2fffc")));
                const QRectF valueRect(tile.left() + 7, tile.top() + 13,
                                       tile.width() - 14, tile.height() - 14);
                painter.drawText(valueRect, Qt::AlignRight | Qt::AlignVCenter, kpi.value);
                m_uiTooltipHits.append({tile, kpi.tooltip});
                tileLeft += tileWidth + gap;
            }
            return;
        }
    }

    void drawTopTelemetry(QPainter& painter)
    {
        const QColor mint(QStringLiteral("#69efc5"));
        const QColor cyan(QStringLiteral("#46c8ee"));
        painter.fillRect(QRectF(0, 0, width(), 82), QColor(1, 9, 14, 226));
        painter.setPen(QPen(QColor(40, 93, 103, 160), 1));
        painter.drawLine(QPointF(0, 81.5), QPointF(width(), 81.5));

        if (!m_logo.isNull()) {
            const QPixmap logo = m_logo.scaled(QSize(78, 78), Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation);
            const QRectF logoTarget(6 + (78 - logo.width()) / 2.0,
                                    2 + (78 - logo.height()) / 2.0,
                                    logo.width(), logo.height());
            painter.drawPixmap(logoTarget, logo, logo.rect());
        }
        painter.setFont(Font(22, QFont::DemiBold));
        painter.setPen(QColor(QStringLiteral("#edf9f7")));
        painter.drawText(QPointF(96, 35), QStringLiteral("PGN HORIZON"));
        if (m_legacyR4Mode && !m_pgnBookingModel) {
            painter.setFont(Font(10, QFont::DemiBold));
            painter.setPen(QColor(QStringLiteral("#b9d7ef")));
            painter.drawText(QPointF(97, 59),
                QStringLiteral("PRIME GAP NETWORK · GPU SEARCH · Δ = q − p"));
        }
        if (!m_legacyR4Mode) {
            painter.setFont(Font(10, QFont::DemiBold));
            painter.setPen(mint);
            painter.drawText(QPointF(97, 59),
                             QStringLiteral("PRIME GAP OBSERVATORY · Δ = q − p"));
        }
        if (!m_legacyR4Mode) drawModulo30Signature(painter, QPointF(382, 36));

        const bool rpcAuthoritative=m_chainObservation&&m_chainObservation->freshAt(QDateTime::currentDateTimeUtc());
        setProperty("pgn_wallet_connected",rpcAuthoritative);
        QString rpcTelemetry;
        if(rpcAuthoritative){
            rpcTelemetry=QStringLiteral("WALLET RPC · CONNECTED · HEIGHT %1").arg(m_chainObservation->height);
        }else if(m_chainObservation){
            rpcTelemetry=QStringLiteral("WALLET RPC · STALE · LAST HEIGHT %1").arg(m_chainObservation->height);
        } else {
            rpcTelemetry = m_legacyR4Mode
                ? QStringLiteral("WALLET RPC · AWAITING CONNECTION")
                : QStringLiteral("RPC OFFLINE · CHAIN DATA HIDDEN");
        }
        painter.setFont(Font(10, QFont::DemiBold, true));
        painter.setPen(rpcAuthoritative ? mint : QColor(QStringLiteral("#ff7e91")));
        const QRectF telemetryRect(450, 16, width() - 470, 19);
        painter.drawText(telemetryRect, Qt::AlignRight | Qt::AlignVCenter, rpcTelemetry);
        if (!rpcAuthoritative) {
            m_uiTooltipHits.append({telemetryRect,
                QStringLiteral("RPC offline; chain identity and values are withheld, and no substitute values are synthesized")});
        }

        const QVector<const FrontierTelemetrySnapshot*> lanes = minerLanes();
        int liveMinerCount = 0;
        for (const FrontierTelemetrySnapshot* lane : lanes) liveMinerCount += minerLaneLive(*lane) ? 1 : 0;
        const qreal kpiLeft = m_legacyR4Mode ? 96.0 : 450.0;
        drawRunKpis(painter, QRectF(kpiLeft, 43, width() - kpiLeft - 20, 31), lanes);
    }

    double rateGaugeFraction(const QString& key,const QString& epoch,const QString& sourceNs,std::optional<double> value){bool ok=false;const auto ns=sourceNs.toULongLong(&ok);const auto result=m_rateGaugeScales[key].observe(value,epoch.toStdString(),ok?std::optional<std::uint64_t>(ns):std::nullopt);return result?result->fraction:0.;}
    double rateGaugeFraction(const FrontierTelemetrySnapshot& lane,const QString& key,const QString& label,std::optional<double> value){return rateGaugeFraction(key+":"+label,lane.runId+":"+lane.raw.value("pgn_compute_epoch").toString(),lane.raw.value("pgn_source_time_ns").toString(),value);}
    void resetRateGaugeEpoch(const QString& id,const FrontierTelemetrySnapshot& lane){const auto epoch=lane.runId+":"+lane.raw.value("pgn_compute_epoch").toString();if(m_rateGaugeEpochs.value(id)==epoch)return;m_rateGaugeEpochs[id]=epoch;for(auto* map:{&m_devicePositionRatePeaks,&m_devicePairRatePeaks})for(auto it=map->begin();it!=map->end();)if(it.key()==id||it.key().startsWith(id+"-"))it=map->erase(it);else ++it;}
    void drawGauge(QPainter& painter,
                   const QPointF& center,
                   qreal radius,
                   double fraction,
                   const QColor& color,
                   const QString& value,
                   const QString& label,
                   qreal labelWidth = 0.0,
                   const QColor& valueColor = QColor{})
    {
        
        const bool geometryAudit=property("pgn_card_layout_audit").toBool()||property("pgn_card_geometry_enabled").toBool();
        QRectF labelBounds;
        const auto valueRectangle=HorizonDeviceVisuals::gauge(painter,center,radius,fraction,color,value,label,labelWidth,4.,7,false,valueColor,geometryAudit?&labelBounds:nullptr);
        if(geometryAudit){
            auto labels=property("pgn_gauge_label_rects").toJsonArray();
            if(labels.size()<128)labels.append(QJsonObject{{"label",label},{"rect",QJsonArray{labelBounds.x(),labelBounds.y(),labelBounds.width(),labelBounds.height()}}});
            setProperty("pgn_gauge_label_rects",labels);
        }
        if(property("pgn_values_audit_enabled").toBool()){
            auto rectangles=property("pgn_values_audit_rects").toJsonArray();
            rectangles.append(QJsonObject{{"label",label},{"value",value},
                {"x",valueRectangle.x()},{"y",valueRectangle.y()},{"w",valueRectangle.width()},{"h",valueRectangle.height()}});
            setProperty("pgn_values_audit_rects",rectangles);
        }
    }

    void drawSparkline(QPainter& painter,
                       const QRectF& area,
                       const QVector<LaneSample>& history,
                       const QColor& color,
                       bool showLabel = true)
    {
        painter.save();
        painter.setPen(QPen(QColor(40, 81, 88, 80), 1));
        for (int i = 1; i < 4; ++i) {
            const qreal y = area.top() + area.height() * i / 4.0;
            painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        }
        if (history.size() >= 2) {
            QPainterPath path;
            bool drawing = false;
            for (int i = 0; i < history.size(); ++i) {
                const qreal x = area.left() + area.width() * i / static_cast<qreal>(history.size() - 1);
                if (!history.at(i).utilizationEmitted) {
                    drawing = false;
                    continue;
                }
                const qreal y = area.bottom() - area.height() *
                    std::clamp(history.at(i).utilization / 100.0, 0.0, 1.0);
                if (!drawing) {
                    path.moveTo(x, y);
                    drawing = true;
                } else {
                    path.lineTo(x, y);
                }
            }
            painter.setPen(QPen(color, 1.7));
            painter.drawPath(path);
        }
        if (showLabel) {
            painter.setFont(Font(7, QFont::DemiBold, true));
            painter.setPen(Alpha(color, 150));
            painter.drawText(QRectF(area.left(), area.top() - 14, area.width(), 12),
                             Qt::AlignLeft, QStringLiteral("NVML GPU-BUSY SAMPLES / NOT SM OCCUPANCY"));
        }
        painter.restore();
    }

    void drawLaneDashboard(QPainter& painter,
                           const QRectF& card,
                           const FrontierTelemetrySnapshot& lane,
                           const QVector<LaneSample>& history,
                           const QString& laneName,
                           const QColor& color)
    {
        painter.save();
        QLinearGradient panel(card.topLeft(), card.bottomRight());
        panel.setColorAt(0.0, QColor(7, 31, 34, 220));
        panel.setColorAt(1.0, QColor(3, 13, 24, 235));
        painter.setBrush(panel);
        painter.setPen(QPen(Alpha(color, 90), 1));
        painter.drawRoundedRect(card, 14, 14);

        const bool live = lane.authoritative && !lane.stale;
        painter.setFont(Font(10, QFont::DemiBold, true));
        painter.setPen(live ? color : QColor(QStringLiteral("#ff7e91")));
        painter.drawText(QRectF(card.left() + 16, card.top() + 11, card.width() - 32, 18),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("GPU %1  /  %2  /  %3")
                             .arg(laneName,
                                  lane.gpuName.isEmpty() ? QStringLiteral("NO PHYSICAL SOURCE") : lane.gpuName,
                                  live ? lane.state.toUpper() : QStringLiteral("DISCONNECTED")));

        // A driver observation is not a median or kernel-duty estimate. The
        // same raw value (including zero) feeds every device presentation.
        const auto rawUtil = lane.gpuSmUtilPercent;
        bool usedOk = false;
        bool totalOk = false;
        const double used = lane.gpuVramUsedBytes.toDouble(&usedOk);
        const double total = lane.gpuVramTotalBytes.toDouble(&totalOk);
        const double vram = usedOk && totalOk && total > 0.0 ? used / total : 0.0;
        drawGauge(painter, QPointF(card.left() + 74, card.top() + 84), 43,
                  rawUtil.value_or(0.0) / 100.0, color,
                  live && rawUtil
                      ? QStringLiteral("%1%").arg(*rawUtil, 0, 'f', 0)
                      : QStringLiteral("--"),
                  QStringLiteral("GPU BUSY · RAW"));
        drawGauge(painter, QPointF(card.left() + 174, card.top() + 84), 43, vram, color,
                  live ? QStringLiteral("%1%").arg(vram * 100.0, 0, 'f', 0) : QStringLiteral("--"),
                  QStringLiteral("VRAM"));

        const auto rateText = [](const std::optional<double>& rate) {
            if (!rate) return QStringLiteral("NOT EMITTED");
            if (*rate >= 1.0e9) return QStringLiteral("%1 G/s").arg(*rate / 1.0e9, 0, 'f', 2);
            if (*rate >= 1.0e6) return QStringLiteral("%1 M/s").arg(*rate / 1.0e6, 0, 'f', 2);
            if (*rate >= 1.0e3) return QStringLiteral("%1 K/s").arg(*rate / 1.0e3, 0, 'f', 1);
            return QStringLiteral("%1 /s").arg(*rate, 0, 'f', 0);
        };
        const qreal infoX = card.left() + 232;
        painter.setFont(Font(8, QFont::DemiBold, true));
        painter.setPen(Alpha(color, 170));
        painter.drawText(QRectF(infoX, card.top() + 43, card.right() - infoX - 12, 14),
                         Qt::AlignLeft, QStringLiteral("PHYSICAL TELEMETRY"));
        painter.setFont(Font(10, QFont::DemiBold, true));
        painter.setPen(QColor(QStringLiteral("#effcf9")));
        painter.drawText(QRectF(infoX, card.top() + 61, card.right() - infoX - 12, 18),
                         Qt::AlignLeft,
                         QStringLiteral("%1 W   ·   %2 °C")
                             .arg(lane.gpuPowerWatts ? QString::number(*lane.gpuPowerWatts, 'f', 1)
                                                     : QStringLiteral("--"),
                                  lane.gpuTemperatureC ? QString::number(*lane.gpuTemperatureC, 'f', 0)
                                                       : QStringLiteral("--")));
        painter.setFont(Font(13, QFont::DemiBold, true));
        painter.setPen(color);
        painter.drawText(QRectF(infoX, card.top() + 84, card.right() - infoX - 12, 22),
                         Qt::AlignLeft,
                         rateText(lane.e2ePositionsPerSecond ? lane.e2ePositionsPerSecond
                                                             : lane.kernelPositionsPerSecond));
        painter.setFont(Font(7, QFont::Normal, true));
        painter.setPen(Alpha(QColor(QStringLiteral("#cce4df")), 150));
        bool cardBestMeritOk = false;
        const double cardBestMerit = lane.bestMerit.toDouble(&cardBestMeritOk);
        const QString cardBestMeritText = cardBestMeritOk && std::isfinite(cardBestMerit)
            ? QString::number(cardBestMerit, 'f', 6)
            : QStringLiteral("NOT EMITTED");
        painter.drawText(QRectF(infoX, card.top() + 110, card.right() - infoX - 12, 30),
                         Qt::AlignLeft | Qt::TextWordWrap,
                         QStringLiteral("SEQ %1  ·  %2\nBEST %3 / MERIT %4")
                             .arg(lane.sequence)
                             .arg(lane.domainId)
                             .arg(lane.bestSpan.isEmpty() ? QStringLiteral("NOT EMITTED") : lane.bestSpan)
                             .arg(cardBestMeritText));
        drawSparkline(painter, QRectF(card.left() + 18, card.bottom() - 53, card.width() - 36, 40),
                      history, color);
        painter.restore();
    }

    QColor candidateColor(const QString& status) const
    {
        const QString lowered = status.toLower();
        // The minimum-merit-filter A/B validation uses these explicit status
        // tokens.  Keep phase colour independent from the physical-GPU ring
        // colour so both cards can be compared in one raw scatter plot.
        if (lowered.contains(QStringLiteral("filter_off")))
            return QColor(QStringLiteral("#ff5f6d"));
        if (lowered.contains(QStringLiteral("filter_on")))
            return QColor(QStringLiteral("#65f29a"));
        if (lowered.contains(QStringLiteral("probable")) ||
            lowered.contains(QStringLiteral("pending")) ||
            lowered.contains(QStringLiteral("base2")))
            return QColor(QStringLiteral("#ffad4a"));
        if (lowered.contains(QStringLiteral("reject")) ||
            lowered.contains(QStringLiteral("stale"))) {
            return QColor(QStringLiteral("#ff314d"));
        }
        if (lowered.contains(QStringLiteral("certified")) ||
            lowered.startsWith(QStringLiteral("exact")))
            return QColor(QStringLiteral("#76f2a8"));
        return QColor(QStringLiteral("#54d8ff"));
    }

    QColor deviceIdentityColor(const QString& stableId,
                               const QString& deviceKind = QString{}) const
    {
        return HorizonDeviceVisuals::identityColor(m_deviceIdentityColors,stableId,deviceKind);
    }

    QColor candidateDeviceColor(const CandidatePoint& point) const
    {
        const bool cpu = point.deviceId.startsWith(QStringLiteral("CPU-"),
                                                   Qt::CaseInsensitive) ||
            point.lane.startsWith(QStringLiteral("CPU"), Qt::CaseInsensitive) ||
            point.deviceName.contains(QStringLiteral("CPU"), Qt::CaseInsensitive);
        const QString identity = !point.deviceId.isEmpty()
            ? point.deviceId
            : (!point.deviceName.isEmpty() ? point.deviceName : point.lane);
        return deviceIdentityColor(identity, cpu ? QStringLiteral("CPU")
                                                : QStringLiteral("GPU"));
    }

    QColor candidateBandColor(const CandidatePoint& point) const
    {
        if (!point.computeShift) return QColor(QStringLiteral("#71858b"));
        const auto index = FrontierShiftBandIndex(*point.computeShift);
        if (!index) return QColor(QStringLiteral("#71858b"));
        const std::array<QColor, 5> colors{
            QColor(QStringLiteral("#69efc5")), QColor(QStringLiteral("#46c8ee")),
            QColor(QStringLiteral("#b58cff")), QColor(QStringLiteral("#ffd166")),
            QColor(QStringLiteral("#ff8f70"))};
        return colors.at(*index);
    }

    QString candidateTargetContext(const CandidatePoint& point) const
    {
        if (!m_targetCurve.accepted || !point.observedUtc.isValid() ||
            point.observedUtc > m_targetCurve.snapshot.observedUtc) {
            return {};
        }
        const FrontierTargetCurvePoint* target =
            m_targetCurve.snapshot.lookupAtOrBefore(point.observedUtc);
        if (target == nullptr) return {};
        const double delta = point.merit - target->targetMerit;
        const QString signedDelta = QStringLiteral("%1%2")
                                        .arg(delta >= 0.0 ? QStringLiteral("+") : QString{})
                                        .arg(delta, 0, 'f', 6);
        return QStringLiteral("HEIGHT %1 · CONSENSUS DIFFICULTY Q48 %2 · TARGET MERIT %3 · DELTA %4")
            .arg(target->height)
            .arg(QString::number(target->requiredMeritQ48))
            .arg(target->targetMerit, 0, 'f', 6)
            .arg(signedDelta);
    }

    void drawCandidatePlot(QPainter& painter, const QRectF& plot)
    {
        LastPaintExecution execution(this,"pgn_plot_ns");
        setProperty("pgn_graph_plot_rectangle",plot.toAlignedRect());
        // Dense ALL/LENGTH plots have immutable data/view coordinates between
        // actual source revisions. Keep their axes/buttons/hit maps too,
        // instead of re-typesetting the same complete graph at animation rate.
        const bool fixedWindow=m_graphXAxisMode==FrontierGraphXAxis::Length ||
            (m_legacyR4Mode&&FrontierTimeRangePresets[std::clamp(m_legacyTimeRangeIndex,0,
              static_cast<int>(FrontierTimeRangePresetCount)-1)].allHistory) ||
            (!m_legacyR4Mode&&m_graphRange==FrontierGraphRange::All);
        const bool largeCatalogue=m_nativeAllCatalog||(m_observationCatalog&&m_showFullHistory&&m_observationCatalog->lengthBounds().points>4096);
        if((m_candidates.size()<=4096&&!largeCatalogue)||!fixedWindow||(m_difficultyMode&&m_difficultyMode->active())){
            setProperty("pgn_plot_cache_hit",false);
            m_plotImageKey.clear();drawCandidatePlotUncached(painter,plot);return;
        }
        const auto key=[this,&plot]{
            QString result=QString::number(m_graphProjectionGeneration)+"/"+
                QString::number(m_observationCatalog?m_observationCatalog->serial():0)+"/"+
                QString::number(m_nativeAllCatalog?m_nativeAllCatalog->serial():0)+"/"+
                QString::number(m_nativeCurrentCatalog?m_nativeCurrentCatalog->serial():0)+"/"+
                QString::number(static_cast<int>(m_graphXAxisMode))+"/"+
                QString::number(m_showFullHistory)+"/"+QString::number(m_minimumMeritFilterMode)+"/"+
                QString::number(m_legacyTimeRangeIndex)+"/"+QString::number(m_graphZoomActive)+"/"+
                QString::number(m_qaPendingZoomSteps)+"/"+QString::number(devicePixelRatioF())+"/"+
                QString::number(plot.x(),'g',17)+"/"+QString::number(plot.y(),'g',17)+"/"+
                QString::number(plot.width(),'g',17)+"/"+QString::number(plot.height(),'g',17)+"/"+
                QString::number(m_graphView.minTimeMs,'g',17)+"/"+QString::number(m_graphView.maxTimeMs,'g',17)+"/"+
                QString::number(m_graphView.minMerit,'g',17)+"/"+QString::number(m_graphView.maxMerit,'g',17);
            const auto lane=[&result,this](const FrontierTelemetrySnapshot& value){
                result+=value.runId+value.state+QString::number(value.authoritative)+QString::number(value.stale)+
                    QString::number(value.terminalSealed)+QString::number(m_legacyRunStartedUtcMs.value(value.runId,-1));
            };
            lane(m_telemetry);lane(m_secondaryTelemetry);
            for(const auto& value:m_additionalTelemetry)lane(value);
            return result;
        };
        const QString observedKey=key();
        if(m_plotImageKey==observedKey&&!m_plotImage.isNull()){
            setProperty("pgn_plot_cache_hit",true);
            painter.drawImage(m_plotImageBounds.topLeft(),m_plotImage);
            m_uiTooltipHits+=m_plotTooltipCache;recordGraphAudit(m_plotImage);return;
        }
        m_plotImageBounds=plot.toAlignedRect();const auto dpr=devicePixelRatioF();
        setProperty("pgn_plot_cache_hit",false);
        setProperty("pgn_plot_rectangle",m_plotImageBounds);
        QImage image(QSize(qCeil(m_plotImageBounds.width()*dpr),qCeil(m_plotImageBounds.height()*dpr)),QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(dpr);image.fill(Qt::transparent);
        const auto tooltipBegin=m_uiTooltipHits.size();
        {QPainter cached(&image);cached.setRenderHints(painter.renderHints());cached.translate(-m_plotImageBounds.topLeft());
         drawCandidatePlotUncached(cached,plot);}
        painter.drawImage(m_plotImageBounds.topLeft(),image);
        recordGraphAudit(image);
        if(m_graphProjectionInput.size()>4096||largeCatalogue){
            m_plotImage=std::move(image);m_plotImageKey=key();
            m_plotTooltipCache=m_uiTooltipHits.mid(tooltipBegin);
        }else m_plotImageKey.clear(); // Small-point pulse/hover stays animated.
    }

    // Explicit, bounded owned-GUI QA only. Disabled in normal operation; no
    // timer, image hashing or retained audit corpus on the productive path.
    void recordGraphAudit(const QImage& image)
    {
        if(!property("pgn_graph_audit_enabled").toBool())return;
        auto rows=property("pgn_graph_audit_frames").toJsonArray();
        if(rows.size()>=128){setProperty("pgn_graph_audit_enabled",false);return;}
        const auto local=m_candidateGraphRect.translated(-m_plotImageBounds.topLeft());
        const auto dpr=image.devicePixelRatio();
        const QRect pixels(qRound(local.x()*dpr),qRound(local.y()*dpr),
                           qRound(local.width()*dpr),qRound(local.height()*dpr));
        const auto field=image.copy(pixels);
        const auto hash=QCryptographicHash::hash(
            QByteArrayView(reinterpret_cast<const char*>(field.constBits()),field.sizeInBytes()),
            QCryptographicHash::Sha256).toHex();
        QJsonObject row{{"utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"field_sha256",QString::fromLatin1(hash)}};
        for(const auto* name:{"pgn_native_display_revision","pgn_native_target_revision",
                "pgn_native_visible_points","pgn_native_loading","pgn_native_display_min_x",
                "pgn_native_display_max_x","pgn_native_display_min_y","pgn_native_display_max_y"})
            row.insert(QString::fromLatin1(name),QJsonValue::fromVariant(property(name)));
        rows.append(row);setProperty("pgn_graph_audit_frames",rows);
    }

    void drawCandidatePlotUncached(QPainter& painter, const QRectF& plot)
    {
        m_candidateHits.clear();
        m_legacyTimeScaleHits.clear();
        m_graphXAxisModeHits.clear();
        m_graphRangeHits.clear();
        if (m_difficultyMode) {
            m_difficultyMode->position(plot, 136);
            if (m_difficultyMode->active()) {
                m_candidateGraphRect = {}; m_graphBoundsValid = false;
                return;
            }
        }
        painter.save();
        // The search-space instrumentation is painted first.  Use a fully
        // opaque rectangular underlay (including the rounded corners) and clip
        // every timeline primitive so no rail label can remain behind it.
        QPainterPath plotPath;
        plotPath.addRoundedRect(plot.adjusted(0.5, 0.5, -0.5, -0.5), 14, 14);
        painter.fillPath(plotPath, QColor(2, 12, 20, 255));
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(44, 92, 103, 170), 1));
        painter.drawPath(plotPath);
        painter.setClipPath(plotPath, Qt::ReplaceClip);
        // Reserve a real left gutter for the rotated Y title and tick labels;
        // the rounded plot clip must never trim the first glyphs.
        // Horizon's time presets occupy two header rows. Keep their summary
        // on a third, disjoint row instead of painting both at y=top+28.
        const QRectF graph = plot.adjusted(88, plot.width()<900 ? 112 : m_legacyR4Mode ? 66 : 44,
                                           m_legacyR4Mode ? -70 : -20, -48);
        m_candidateGraphRect = graph;
        m_graphXAxisHit = QRectF(graph.left(), graph.bottom(), graph.width(), 31);
        m_graphYAxisHit = QRectF(plot.left() + 2, graph.top(),
                                 graph.left() - plot.left() - 4, graph.height());
        painter.setPen(QPen(QColor(55, 101, 110, 75), 1));
        if (!m_legacyR4Mode) {
            for (int i = 0; i <= 5; ++i) {
                const qreal x = graph.left() + graph.width() * i / 5.0;
                painter.drawLine(QPointF(x, graph.top()), QPointF(x, graph.bottom()));
            }
        }
        // Keep labels disjoint even in the minimum-height live layout.
        const int graphIntervals = std::clamp(static_cast<int>(graph.height()/24.0),1,FrontierGraphIntervalCount);
        // Horizontal grid lines are drawn at the actual visible merit ticks
        // after fitting; tick count must not expand the data viewport.
        painter.setFont(Font(10, QFont::DemiBold, true));
        painter.setPen(QColor(QStringLiteral("#dcefed")));
        painter.drawText(QRectF(plot.left() + 16, plot.top() + 7,
                                plot.width() - (plot.width()<900 ? 32 : m_legacyR4Mode ? 510 : 350), 18),
                         Qt::AlignLeft,
                         m_legacyR4Mode
                             ? (m_graphZoomActive
                                    ? QStringLiteral("LIVE GAP DISTRIBUTION · %1 × MERIT · ZOOMED")
                                    : QStringLiteral("LIVE GAP DISTRIBUTION · %1 × MERIT"))
                                   .arg(m_graphXAxisMode == FrontierGraphXAxis::Time
                                            ? QStringLiteral("TIME") : QStringLiteral("LENGTH"))
                             : m_minimumMeritFilterMode
                             ? (m_graphZoomActive
                                    ? QStringLiteral("MINIMUM MERIT FILTER A/B · %1 × MERIT · ZOOMED")
                                    : QStringLiteral("MINIMUM MERIT FILTER A/B · %1 × MERIT"))
                                   .arg(m_graphXAxisMode == FrontierGraphXAxis::Time
                                            ? QStringLiteral("TIME") : QStringLiteral("LENGTH"))
                             : (m_graphZoomActive
                                    ? QStringLiteral("PRIME-GAP MAP · %1 × MERIT · ZOOMED")
                                    : QStringLiteral("PRIME-GAP MAP · %1 × MERIT"))
                                   .arg(m_graphXAxisMode == FrontierGraphXAxis::Time
                                            ? QStringLiteral("TIME") : QStringLiteral("LENGTH")));
        m_currentRunToggleHit = HorizonGraphControls::current(plot);
        m_fullHistoryToggleHit = HorizonGraphControls::history(plot);
        m_graphResetHit = HorizonGraphControls::reset(plot);
        const auto drawToggle = [&painter](const QRectF& area, const QString& label, bool selected) {
            HorizonGraphControls::drawToggle(painter, area, label, selected);
        };
        drawToggle(m_currentRunToggleHit, QStringLiteral("CURRENT RUN"), !m_showFullHistory);
        drawToggle(m_fullHistoryToggleHit, QStringLiteral("FULL HISTORY"), m_showFullHistory);
        m_uiTooltipHits.append({m_currentRunToggleHit,
            QStringLiteral("Show gaps emitted by the current mining run. Persisted history is retained but hidden.")});
        m_uiTooltipHits.append({m_fullHistoryToggleHit,
            QStringLiteral("Show every persisted exact gap from this installation across miner and UI restarts.")});
        const qreal axisButtonWidth = 56.0;
        const qreal axisButtonGap = 4.0;
        const qreal axisStart = plot.left() + 16.0;
        const QRectF timeAxisButton=HorizonGraphControls::axis(plot,0);
        const QRectF lengthAxisButton=HorizonGraphControls::axis(plot,1);
        m_graphTimeAxisHit = timeAxisButton;
        m_graphLengthAxisHit = lengthAxisButton;
        drawToggle(timeAxisButton, QStringLiteral("TIME"),
                   m_graphXAxisMode == FrontierGraphXAxis::Time);
        drawToggle(lengthAxisButton, QStringLiteral("LENGTH"),
                   m_graphXAxisMode == FrontierGraphXAxis::Length);
        m_graphXAxisModeHits.append({timeAxisButton, FrontierGraphXAxis::Time});
        m_graphXAxisModeHits.append({lengthAxisButton, FrontierGraphXAxis::Length});
        m_uiTooltipHits.append({timeAxisButton,
            QStringLiteral("Chronological X axis. Trading ranges below are minutes/hours/days/years, never months.")});
        m_uiTooltipHits.append({lengthAxisButton,
            QStringLiteral("Physical gap length q − p. Dense views render every raw observation into its pixel footprint; no averages or sampling. Imported observations are grey because their device/proof provenance is not promoted.")});
        m_graphRangeHits.clear();
        if (!m_legacyR4Mode && m_graphXAxisMode == FrontierGraphXAxis::Time) {
            const QVector<FrontierGraphRangePreset> ranges = FrontierGraphRangePresets();
            const qreal rangeStart = axisStart + 2.0 * (axisButtonWidth + axisButtonGap) + 8.0;
            const qreal available = std::max<qreal>(120.0, plot.right() - rangeStart - 12.0);
            const qreal rangeGap = 3.0;
            const qreal buttonWidth = std::clamp<qreal>(
                (available - rangeGap * std::max<qsizetype>(0, ranges.size() - 1)) /
                    std::max<qsizetype>(1, ranges.size()), 36.0, 55.0);
            for (int index = 0; index < ranges.size(); ++index) {
                const FrontierGraphRangePreset& preset = ranges.at(index);
                const QRectF button(rangeStart + index * (buttonWidth + rangeGap),
                                    plot.top() + 28.0, buttonWidth, 19.0);
                drawToggle(button, QString::fromLatin1(preset.label).toUpper(),
                           m_graphRange == preset.range);
                m_graphRangeHits.append({button, preset.range});
                m_uiTooltipHits.append({button,
                    preset.durationMs
                        ? QStringLiteral("TIME axis: show the latest %1 of observed events.")
                              .arg(QString::fromLatin1(preset.label))
                        : QStringLiteral("TIME axis: show ALL retained history.")});
            }
        }
        if (m_legacyR4Mode && m_graphXAxisMode == FrontierGraphXAxis::Time) {
            for (int index = 0; index < static_cast<int>(FrontierTimeRangePresetCount); ++index) {
                const FrontierTimeRangePreset& preset = FrontierTimeRangePresets[index];
                const QRectF button=HorizonGraphControls::timeRangeButton(plot,index);
                HorizonGraphControls::drawTimeRange(painter,button,index,
                           !m_graphZoomActive && m_legacyTimeRangeIndex == index);
                m_legacyTimeScaleHits.append({button, index});
                m_uiTooltipHits.append({button,
                    QStringLiteral("Time range: %1 (%2). The range is a viewport only; CURRENT/FULL HISTORY remains a separate dataset selector.")
                        .arg(QString::fromLatin1(preset.label),
                             QString::fromLatin1(preset.tooltip))});
            }
        }
        HorizonGraphControls::drawReset(painter,m_graphResetHit,m_graphZoomActive);
        m_uiTooltipHits.append({m_graphResetHit,
            QStringLiteral("Reset zoom and axis panning; PGN returns to ALL original history in the selected dataset.")});
        painter.setFont(Font(9, QFont::DemiBold, true));
        painter.setPen(QColor(QStringLiteral("#6fe3ca")));
        painter.drawText(QRectF(graph.left(), graph.bottom() + 28, graph.width(), 15),
                         Qt::AlignCenter,
                           m_graphXAxisMode == FrontierGraphXAxis::Time
                               ? QStringLiteral("TIME · UTC")
                               : m_minimumMeritFilterMode
                              ? QStringLiteral("LENGTH · EXACT GAP SIZE Δ · NETWORK-ELIGIBLE EVENTS")
                              : QStringLiteral("LENGTH · GAP SIZE Δ · ACTIVE SEARCH-POLICY OUTPUT"));
        painter.save();
        painter.translate(plot.left() + 26, graph.center().y());
        painter.rotate(-90);
        painter.drawText(QRectF(-graph.height() / 2.0, 0, graph.height(), 14),
                          Qt::AlignCenter, QStringLiteral("MERIT   Δ / ln(p)"));
        painter.restore();
        if (m_legacyR4Mode && m_graphXAxisMode == FrontierGraphXAxis::Time) {
            painter.save();
            painter.translate(plot.right() - 13, graph.center().y());
            painter.rotate(90);
            painter.setPen(QColor(226, 140, 255, 190));
            painter.drawText(QRectF(-graph.height() / 2.0, 0, graph.height(), 14),
                             Qt::AlignCenter, QStringLiteral("NETWORK DIFFICULTY"));
            painter.restore();
        }

        QVector<const CandidatePoint*> points;
        std::vector<FrontierRunLaneState> laneStates;
        const auto appendLane = [&laneStates](const FrontierTelemetrySnapshot& lane, bool configured) {
            if (!configured) return;
            laneStates.push_back({lane.runId.toStdString(), lane.state.toStdString(),
                                  lane.authoritative, lane.stale, lane.terminalSealed});
        };
        appendLane(m_telemetry, m_hasTelemetry);
        appendLane(m_secondaryTelemetry, m_hasSecondaryTelemetry);
        // CURRENT is a union over every authoritative live producer lane,
        // including auxiliary GPUs discovered after the primary/secondary
        // readers were attached.  Restricting this list to A/B made the
        // graph silently look like a 3090-only view while the device strip
        // and rate tiles already showed the other workers.
        QStringList additionalLaneIds = m_additionalTelemetry.keys();
        std::sort(additionalLaneIds.begin(), additionalLaneIds.end(),
                  [](const QString& left, const QString& right) {
            return left.compare(right, Qt::CaseInsensitive) < 0;
        });
        for (const QString& stableId : additionalLaneIds) {
            const auto iterator = m_additionalTelemetry.constFind(stableId);
            if (iterator != m_additionalTelemetry.cend()) appendLane(iterator.value(), true);
        }
        const std::vector<std::string> activeRunIds = FrontierCurrentRunIds(laneStates);
        const auto pointXValue = [this](const CandidatePoint& point) -> qint64 {
            if (m_graphXAxisMode == FrontierGraphXAxis::Time && point.observedUtc.isValid()) {
                return point.observedUtc.toMSecsSinceEpoch();
            }
            // LENGTH is the physical prime-gap coordinate in every mode. A
            // qint64 is sufficient for the displayed integer gap while the
            // archive keeps the original decimal merit/provenance fields.
            if (std::isfinite(point.gap) && point.gap >= 1.0 &&
                point.gap <= static_cast<double>(std::numeric_limits<qint64>::max())) {
                return qRound64(point.gap);
            }
            return 0;
        };
        const bool projectionSourceChanged = !m_graphProjectionInputValid ||
            m_graphProjectionInputGeneration != m_graphProjectionGeneration ||
            m_graphProjectionInputXAxisMode != m_graphXAxisMode ||
            m_graphProjectionInputFullHistory != m_showFullHistory ||
            m_graphProjectionInputMinimumMeritFilter != m_minimumMeritFilterMode ||
            m_graphProjectionInputActiveRunIds != activeRunIds;
        if (projectionSourceChanged) {
            // Rebuild the lossless index only when data, axis semantics, the
            // CURRENT run union, or the dataset selector actually changed.
            // At the normal 8 Hz repaint cadence a million-point archive is
            // therefore not rescanned just to animate the canvas.
            m_graphProjectionInput.clear();
            m_graphProjectionInput.reserve(m_candidates.size());
            m_graphProjectionCurrentEventCount = 0;
            m_graphProjectionHistoryEventCount = 0;
            m_graphProjectionCurrentAcceptedCount = 0;
            m_graphProjectionCurrentRejectedCount = 0;
            m_graphProjectionCurrentStaleCount = 0;
            m_graphProjectionCurrentPendingCount = 0;
            m_graphProjectionMeritBoundsValid = false;
            for (std::size_t archiveIndex = 0; archiveIndex < m_candidates.size(); ++archiveIndex) {
                const CandidatePoint& point = m_candidates[archiveIndex];
                // Re-evaluate the gate on each source-index rebuild. This
                // removes points loaded before target backfill and keeps
                // unknown target provenance fail-closed.
                if (!candidateNetworkEligible(point)) continue;
                // Missing times stay archived in length mode, never as1970.
                if (m_graphXAxisMode == FrontierGraphXAxis::Time &&
                    !point.observedUtc.isValid()) continue;
                const QString statusLower = point.status.toLower();
                const bool filterPoint = statusLower.contains(QStringLiteral("filter_off")) ||
                    statusLower.contains(QStringLiteral("filter_on"));
                const bool current = FrontierGapGraphAdmits(
                    m_minimumMeritFilterMode, filterPoint,
                    includeCandidate(point, activeRunIds, false));
                const bool history = FrontierGapGraphAdmits(
                    m_minimumMeritFilterMode, filterPoint,
                    includeCandidate(point, activeRunIds, true));
                if (current) {
                    ++m_graphProjectionCurrentEventCount;
                    switch (FrontierClassifyGraphStatus(point.status, point.networkOutcome)) {
                    case FrontierGraphStatus::Accepted:
                        ++m_graphProjectionCurrentAcceptedCount;
                        break;
                    case FrontierGraphStatus::Rejected:
                        ++m_graphProjectionCurrentRejectedCount;
                        break;
                    case FrontierGraphStatus::Stale:
                        ++m_graphProjectionCurrentStaleCount;
                        break;
                    case FrontierGraphStatus::Pending:
                        ++m_graphProjectionCurrentPendingCount;
                        break;
                    }
                }
                if (history) ++m_graphProjectionHistoryEventCount;
                if (!(m_showFullHistory ? history : current)) continue;
                // Unknown discovery dates belong to LENGTH/archive, never to
                // a fabricated1970 coordinate on the chronological axis.
                if(m_graphXAxisMode==FrontierGraphXAxis::Time&&!point.observedUtc.isValid())continue;
                m_graphProjectionInput.push_back({archiveIndex, candidateExact(point),
                                                  candidateRejectedOrStale(point)});
                const qint64 x = pointXValue(point);
                if (!m_graphProjectionMeritBoundsValid) {
                    m_graphProjectionMinimumX = x;
                    m_graphProjectionMaximumX = x;
                    m_graphProjectionMinimumMerit = point.merit;
                    m_graphProjectionMaximumMerit = point.merit;
                    m_graphProjectionMeritBoundsValid = std::isfinite(point.merit);
                } else {
                    m_graphProjectionMinimumX = std::min(m_graphProjectionMinimumX, x);
                    m_graphProjectionMaximumX = std::max(m_graphProjectionMaximumX, x);
                    if (std::isfinite(point.merit)) {
                        m_graphProjectionMinimumMerit = std::min(
                            m_graphProjectionMinimumMerit, point.merit);
                        m_graphProjectionMaximumMerit = std::max(
                            m_graphProjectionMaximumMerit, point.merit);
                    }
                }
            }
            m_graphProjectionInputGeneration = m_graphProjectionGeneration;
            m_graphProjectionInputActiveRunIds = activeRunIds;
            m_graphProjectionInputXAxisMode = m_graphXAxisMode;
            m_graphProjectionInputFullHistory = m_showFullHistory;
            m_graphProjectionInputMinimumMeritFilter = m_minimumMeritFilterMode;
            m_graphProjectionInputValid = true;
        }
        const std::vector<FrontierCandidateProjectionRecord>& projectionInput =
            m_graphProjectionInput;
        const bool nativeCurrentMatches=m_nativeCurrentCatalog&&m_nativeCurrentCatalog->ready()&&m_nativeCurrentCatalog->scope()==m_telemetry.runId;
        auto* nativeCatalog=m_showFullHistory?m_nativeAllCatalog:(nativeCurrentMatches?m_nativeCurrentCatalog:nullptr);
        const bool nativeReady=nativeCatalog&&nativeCatalog->ready();
        const auto nativeBounds=nativeReady?(m_graphXAxisMode==FrontierGraphXAxis::Time?nativeCatalog->timeBounds():nativeCatalog->lengthBounds()):pgn::ui::CatalogBounds{};
        const qint64 currentEventCount = m_nativeAllCatalog?(nativeCurrentMatches?m_nativeCurrentCatalog->lengthBounds().points:-1):m_graphProjectionCurrentEventCount;
        // A run's live counter is never the size of the durable archive. Keep
        // the catalogue unavailable while its SQL audit loads, instead of
        // making a new thirteen-gap run look like the entire old history.
        const qint64 historyEventCount = m_nativeAllCatalog?(m_nativeAllCatalog->ready()?m_nativeAllCatalog->lengthBounds().points:-1):m_graphProjectionHistoryEventCount;
        setProperty("pgn_graph_history_points",historyEventCount);
        setProperty("pgn_graph_current_points",currentEventCount);
        painter.setFont(Font(9, QFont::Normal, true));
        painter.setPen(QColor(QStringLiteral("#9ec7c1")));
        if (m_targetCurve.accepted) {
            painter.drawText(QRectF(graph.left(), graph.top() - 16, 250, 13),
                             Qt::AlignLeft,
                             QStringLiteral("CONSENSUS TARGET · %1 AUTHENTICATED SAMPLES")
                                 .arg(m_targetCurve.snapshot.points.size()));
        }
        const QString currentSummary = (currentEventCount<0||historyEventCount<0)?QStringLiteral("HISTORY LOADING · source records retained"):QStringLiteral(
            "CURRENT %1 · A %2 / R %3 / S %4 · PENDING %5 · HISTORY %6 · SHOWING %7")
            .arg(currentEventCount)
            .arg(m_graphProjectionCurrentAcceptedCount)
            .arg(m_graphProjectionCurrentRejectedCount)
            .arg(m_graphProjectionCurrentStaleCount)
            .arg(m_graphProjectionCurrentPendingCount)
            .arg(historyEventCount)
            .arg(points.size());
        const auto nativeCaption=pgn::ui::nativeHistoryCaption(currentEventCount,historyEventCount,
            m_nativeAllCatalog&&m_nativeAllCatalog->ready()?m_nativeAllCatalog->timeBounds().points:0,
            m_observationCatalog?m_observationCatalog->lengthBounds().points:0,m_graphXAxisMode==FrontierGraphXAxis::Time);
        setProperty("pgn_native_history_caption",nativeCaption);
        const auto summaryWidth=std::max(0.0,graph.width()-(m_targetCurve.accepted?260.0:0.0));
        painter.drawText(QRectF(graph.right()-summaryWidth, graph.top() - 16, summaryWidth, 13),
                         Qt::AlignRight,
                         m_legacyR4Mode
                             ? painter.fontMetrics().elidedText(nativeCaption,Qt::ElideRight,static_cast<int>(summaryWidth))
                             : m_minimumMeritFilterMode
                             ? currentSummary + QStringLiteral("  ·  FILTER OFF RED / ON GREEN")
                             : currentSummary);
        m_uiTooltipHits.append({QRectF(graph.left(), graph.top() - 18, graph.width(), 17),
            m_legacyR4Mode?pgn::ui::nativeHistoryExplanation(historyEventCount,
                m_nativeAllCatalog&&m_nativeAllCatalog->ready()?m_nativeAllCatalog->timeBounds().points:0,m_graphXAxisMode==FrontierGraphXAxis::Time):
            QStringLiteral("CURRENT is the union of every authoritative CPU/GPU producer lane. A/R/S/PENDING form a disjoint status partition, so their sum equals CURRENT; rejected or stale terminal outcomes remain red after restart.")});

        // Imported/rich-record caches are separate datasets. Until the native
        // catalogue is ready they cannot stand in for FULL HISTORY or CURRENT.
        if(m_nativeAllCatalog&&!nativeReady){
            m_graphBoundsValid=false;
            setProperty("pgn_graph_x_ticks",QVariantList{});setProperty("pgn_graph_y_ticks",QVariantList{});
            setProperty("pgn_native_loading",true);setProperty("pgn_native_visible_points",0);
            setProperty("pgn_native_display_revision",qulonglong(0));
            const auto error=property("pgn_catalog_archive_error").toString();
            setProperty("pgn_native_error",error);
            setProperty("pgn_native_frame_caption",pgn::ui::catalogFrameCaption(nullptr,error));
            setProperty("pgn_imported_visible_points",0);
            painter.setPen(QColor(error.isEmpty()?"#9ec7c1":"#ffad4a"));
            painter.drawRect(graph);
            painter.drawText(graph,Qt::AlignCenter,error.isEmpty()
                ?QStringLiteral("Loading %1…\nSource records are retained.").arg(m_showFullHistory?"complete history":"current range")
                :QStringLiteral("History unavailable\n%1").arg(error));
            painter.restore();return;
        }

        // The viewport must end at the actual present, not at the previous
        // minute boundary: otherwise a freshly discovered gap can remain
        // outside the graph for up to 59.999 seconds.  Tick labels are rounded
        // independently below, so seconds never leak into the UI.
        const bool timeAxis = m_graphXAxisMode == FrontierGraphXAxis::Time;
        // Stable original-event anchor prevents 40Hz paint-clock cancellation
        // from starving the async raster. This changes no sample or timestamp.
        const qint64 legacyNow = nativeReady&&timeAxis&&nativeBounds.points>0
            ?qint64(std::ceil(nativeBounds.maxX)):QDateTime::currentMSecsSinceEpoch();
        const std::optional<qint64> selectedTimeRange =
            (!m_legacyR4Mode && timeAxis)
                ? FrontierGraphRangeDurationMs(m_graphRange) : std::nullopt;
        const FrontierTimeRangePreset& activeTimeRange = FrontierTimeRangePresets[
            std::clamp(m_legacyTimeRangeIndex, 0,
                       static_cast<int>(FrontierTimeRangePresetCount) - 1)];
        const qint64 legacyDefaultSpanMs = activeTimeRange.allHistory
            ? 0 : activeTimeRange.spanMs;
        setProperty("pgn_graph_time_preset",m_legacyTimeRangeIndex);
        // The explicit trading-style range buttons select one viewport span.
        // The chart always exposes exactly six divisions, independently of
        // the CURRENT RUN / FULL HISTORY dataset selector.  ALL is the only
        // data-driven exception.
        qint64 minTime = (m_legacyR4Mode && timeAxis && legacyDefaultSpanMs > 0)
                                        ? legacyNow - legacyDefaultSpanMs
                         : (selectedTimeRange ? legacyNow - *selectedTimeRange
                                              : (projectionInput.empty() ? 0
                                                                          : m_graphProjectionMinimumX));
        qint64 maxTime = (m_legacyR4Mode && timeAxis && legacyDefaultSpanMs > 0)
                                        ? legacyNow
                         : (selectedTimeRange ? legacyNow
                                               : (projectionInput.empty() ? 1
                                                                           : m_graphProjectionMaximumX));
        double minMerit = m_minimumMeritFilterMode ? 1.0 : 18.0;
        double maxMerit = m_minimumMeritFilterMode ? 20.0 : 23.0;
        bool meritBoundsInitialized = false;
        if (m_graphProjectionMeritBoundsValid &&
            (!m_legacyR4Mode || !timeAxis) && !selectedTimeRange) {
            minTime = std::min(minTime, m_graphProjectionMinimumX);
            maxTime = std::max(maxTime, m_graphProjectionMaximumX);
        }
        const bool includeImported=m_observationCatalog&&m_observationCatalog->ready()&&
            m_showFullHistory&&!m_minimumMeritFilterMode&&!timeAxis&&
            m_observationCatalog->lengthBounds().points>0;
        if(includeImported){
            const auto& bounds=m_observationCatalog->lengthBounds();
            const auto first=qint64(std::floor(bounds.minX)),last=qint64(std::ceil(bounds.maxX));
            minTime=projectionInput.empty()?first:std::min(minTime,first);
            maxTime=projectionInput.empty()?last:std::max(maxTime,last);
        }
        if (maxTime <= minTime) {
            minTime = std::max<qint64>(0, minTime - 1);
            maxTime += 1;
        }
        if (m_legacyR4Mode && timeAxis && activeTimeRange.allHistory && !projectionInput.empty()) {
            // ALL is data-driven: include the complete selected dataset and
            // stop at its last immutable event.  Extending the right edge to
            // the live clock made already-found gaps visibly drift left while
            // no data changed (especially in CURRENT RUN + ALL).
            minTime = m_graphProjectionMinimumX;
            maxTime = std::max(minTime + 1, m_graphProjectionMaximumX);
        }
        if(nativeReady&&nativeBounds.points>0&&(!timeAxis||activeTimeRange.allHistory)){
            const auto first=qint64(std::floor(nativeBounds.minX));
            auto last=qint64(std::ceil(nativeBounds.maxX));
            // ALL ends at the last immutable event, not an empty next minute.
            if(includeImported){minTime=std::min(minTime,first);maxTime=std::max(maxTime,last);}
            else{minTime=first;maxTime=std::max(first+1,last);}
        }
        // Build the paint records once and fit Y from only the points that are
        // actually visible in the selected time/length viewport.  The old
        // archive-wide extrema made a 1/3/6-minute view inherit empty bands
        // from unrelated historical points.
        const bool viewportChanged = projectionSourceChanged || m_denseViewportGeneration != m_graphProjectionGeneration ||
            m_denseViewportMinX != minTime || m_denseViewportMaxX != maxTime ||
            m_denseViewportTimeAxis != timeAxis || m_denseViewportFullHistory != m_showFullHistory ||
            m_denseViewportRuns != activeRunIds;
        if(viewportChanged){
        m_denseViewportRecords.clear();m_denseViewportRecords.reserve(projectionInput.size());
        m_denseVisibleBounds={};
        for (const FrontierCandidateProjectionRecord& record : projectionInput) {
            const CandidatePoint& point = m_candidates[record.archiveIndex];
            const double x = static_cast<double>(pointXValue(point));
            m_denseViewportRecords.push_back({record.archiveIndex, x, point.merit,
                                       record.exact, record.rejectedOrStale});
            FrontierIncludeVisibleGraphPoint(m_denseVisibleBounds,
                                             static_cast<double>(minTime),
                                             static_cast<double>(maxTime),
                                             x, point.merit);
        }
        m_denseViewportGeneration=m_graphProjectionGeneration;
        m_denseViewportMinX=minTime;m_denseViewportMaxX=maxTime;
        m_denseViewportTimeAxis=timeAxis;m_denseViewportFullHistory=m_showFullHistory;
        m_denseViewportRuns=activeRunIds;++m_denseViewportRevision;
        }
        const auto& viewportRecords=m_denseViewportRecords;
        const auto& visibleMeritBounds=m_denseVisibleBounds;
        if (visibleMeritBounds.valid) {
            minMerit = visibleMeritBounds.minimum;
            maxMerit = visibleMeritBounds.maximum;
            meritBoundsInitialized = true;
        }
        const FrontierTargetCurvePoint* currentTarget =
            m_targetCurve.accepted && !m_targetCurve.snapshot.points.isEmpty()
                ? &m_targetCurve.snapshot.points.back() : nullptr;
        // With no visible point, retain a useful target-centred empty view.
        // When points exist, the target/difficulty overlays must not recreate
        // the empty vertical bands that viewport fitting removes.
        if (!meritBoundsInitialized && currentTarget != nullptr &&
            std::isfinite(currentTarget->targetMerit)) {
            minMerit = currentTarget->targetMerit;
            maxMerit = currentTarget->targetMerit;
            meritBoundsInitialized = true;
        }
        if (!meritBoundsInitialized && m_legacyR4Mode && timeAxis &&
            !m_legacyDifficulty.isEmpty()) {
            const auto firstVisibleDifficulty = std::lower_bound(
                m_legacyDifficulty.cbegin(), m_legacyDifficulty.cend(), minTime,
                [](const QPair<qint64, double>& sample, qint64 timestamp) {
                    return sample.first < timestamp;
                });
            const auto includeDifficulty = [&](double value) {
                if (!std::isfinite(value) || value <= 0.0) return;
                if (!meritBoundsInitialized) {
                    minMerit = value;
                    maxMerit = value;
                    meritBoundsInitialized = true;
                } else {
                    minMerit = std::min(minMerit, value);
                    maxMerit = std::max(maxMerit, value);
                }
            };
            // Difficulty is a staircase.  The sample immediately preceding
            // the viewport remains active at its left edge.
            if (firstVisibleDifficulty != m_legacyDifficulty.cbegin()) {
                includeDifficulty(std::prev(firstVisibleDifficulty)->second);
            }
            for (auto sample = firstVisibleDifficulty;
                 sample != m_legacyDifficulty.cend() && sample->first <= maxTime; ++sample) {
                includeDifficulty(sample->second);
            }
        }
        if(includeImported){
            const auto& bounds=m_observationCatalog->lengthBounds();
            minMerit=meritBoundsInitialized?std::min(minMerit,bounds.minY):bounds.minY;
            maxMerit=meritBoundsInitialized?std::max(maxMerit,bounds.maxY):bounds.maxY;
            meritBoundsInitialized=true;
        }
        const bool finiteNativeTimeWindow = timeAxis &&
            (m_legacyR4Mode ? !activeTimeRange.allHistory : selectedTimeRange.has_value());
        const bool selectedNativeY = nativeReady && pgn::ui::nativeWindowYFitAllowed(
            timeAxis,finiteNativeTimeWindow,m_graphZoomActive,!m_legacyDifficulty.isEmpty());
        if(nativeReady&&nativeBounds.points>0&&!selectedNativeY){
            minMerit=meritBoundsInitialized?std::min(minMerit,nativeBounds.minY):nativeBounds.minY;
            maxMerit=meritBoundsInitialized?std::max(maxMerit,nativeBounds.maxY):nativeBounds.maxY;
            meritBoundsInitialized=true;
        }
        const auto contentView = FrontierContentGraphView(
            {static_cast<double>(minTime), static_cast<double>(maxTime), minMerit, maxMerit});
        if (!contentView) {
            m_graphBoundsValid = false;
            painter.setPen(QColor("#ff9a3d"));
            painter.drawText(graph, Qt::AlignCenter, "INVALID SOURCE GRAPH BOUNDS");
            painter.restore();
            return;
        }
        minMerit = contentView->minMerit;
        maxMerit = contentView->maxMerit;

        FrontierGraphView requestedView = m_graphZoomActive
            ? m_graphView
            : FrontierGraphView{static_cast<double>(minTime), static_cast<double>(maxTime),
                                minMerit, maxMerit};
        // Keep both auto-fit and manual interaction geometry independent from
        // rounded labels. Integral merit ticks are selected INSIDE this view.
        m_graphView = requestedView;
        if (!m_graphZoomActive && m_qaPendingZoomSteps > 0) {
            m_graphView = (m_graphXAxisMode == FrontierGraphXAxis::Time)
                ? FrontierZoomGraphView(m_graphView, 0.15, 0.5, m_qaPendingZoomSteps)
                : FrontierZoomGapGraphView(m_graphView, 0.15, 0.5, m_qaPendingZoomSteps);
            m_graphZoomActive = true;
            m_qaPendingZoomSteps = 0;
        }
        minTime = qRound64(m_graphView.minTimeMs);
        maxTime = qRound64(m_graphView.maxTimeMs);
        minMerit = m_graphView.minMerit;
        maxMerit = m_graphView.maxMerit;
        // Request the target geometry BEFORE choosing the displayed geometry.
        // An admission can change auto-fit bounds while the exact raster worker
        // is pending. Reprojecting its old raster into those new axes produces
        // a transient resampled field, then another field when work completes.
        // Publish axes + pixels + caption as one COMPLETE frame instead. Keep
        // m_graphView as the requested interaction target so pending work cannot
        // overwrite a user's zoom or make the worker request its old view again.
        const QString nativeAxis = timeAxis ? QStringLiteral("time") : QStringLiteral("length");
        const pgn::ui::DenseView nativeRequestedView{double(minTime),double(maxTime),minMerit,maxMerit,
            QSize(std::max(1,qRound(graph.width())),std::max(1,qRound(graph.height()))),
            static_cast<std::uint64_t>(nativeBounds.revision)};
        const pgn::ui::CatalogYFit nativeYFit{
            nativeReady&&pgn::ui::nativeWindowYFitAllowed(timeAxis,finiteNativeTimeWindow,
                m_graphZoomActive,!m_legacyDifficulty.isEmpty()),
            currentTarget?std::optional<double>{currentTarget->targetMerit}:std::nullopt};
        const pgn::ui::CatalogRaster* nativeFrame = nullptr;
        if(nativeReady){
            nativeCatalog->request(nativeAxis,nativeRequestedView,nativeYFit);
            nativeFrame = nativeCatalog->raster(nativeAxis,nativeRequestedView,nativeYFit);
        }
        const bool nativeFrameCurrent = nativeFrame != nullptr;
        if(!nativeFrame && nativeReady) nativeFrame = nativeCatalog->lastComplete(nativeAxis);
        if(nativeFrame){
            minTime = qRound64(nativeFrame->view.minX);
            maxTime = qRound64(nativeFrame->view.maxX);
            minMerit = nativeFrame->view.minY;
            maxMerit = nativeFrame->view.maxY;
            if(nativeFrameCurrent&&nativeYFit.enabled&&!m_graphZoomActive)
                m_graphView={double(minTime),double(maxTime),minMerit,maxMerit};
        }
        setProperty("pgn_native_window_fit_requested",nativeYFit.enabled);
        setProperty("pgn_native_staircase_fit_excluded",finiteNativeTimeWindow&&!m_legacyDifficulty.isEmpty());
        m_graphPresentedView = {double(minTime),double(maxTime),minMerit,maxMerit};
        if (m_legacyR4Mode && timeAxis && m_legacyDifficultyRangeHandler &&
            (std::llabs(minTime - m_lastDifficultyRequestMinMs) >= 60000 ||
             std::llabs(maxTime - m_lastDifficultyRequestMaxMs) >= 60000)) {
            m_lastDifficultyRequestMinMs = minTime;
            m_lastDifficultyRequestMaxMs = maxTime;
            const auto handler = m_legacyDifficultyRangeHandler;
            QTimer::singleShot(0, this, [handler, minTime, maxTime] {
                handler(minTime, maxTime);
            });
        }
        m_graphBoundsValid = maxTime > minTime && maxMerit > minMerit;
        const auto niceMeritTicks = FrontierVisibleMeritTicks(
            {double(minTime), double(maxTime), minMerit, maxMerit},
            std::clamp(int(graph.height()/30.0),3,12),
            m_legacyR4Mode && timeAxis);
        const auto meritTicks = FrontierBorderTicks(minMerit, maxMerit,
            niceMeritTicks, graph.height(), 20.);
        const double meritStep = niceMeritTicks.size() > 1
            ? niceMeritTicks[1] - niceMeritTicks[0]
            : FrontierNiceNumericStep((maxMerit - minMerit) / graphIntervals);
        setProperty("pgn_graph_presented_bounds", QVariantList{double(minTime), double(maxTime), minMerit, maxMerit});
        setProperty("pgn_graph_fit_policy", "content-with-independent-ticks");
        painter.save();
        painter.setPen(QPen(QColor(55, 101, 110, 75), 1));
        for (double value : meritTicks) {
            const qreal y = graph.bottom() - graph.height() * (value - minMerit) / (maxMerit - minMerit);
            painter.drawLine(QPointF(graph.left(), y), QPointF(graph.right(), y));
        }
        painter.restore();

        painter.setFont(Font(9, QFont::DemiBold, true));
        painter.setPen(QColor(QStringLiteral("#9bc7c3")));
        const qint64 spanMs = maxTime - minTime;
        const int horizontalIntervals=std::clamp(int(graph.width()/110.0),2,12);
        const double xStep=timeAxis
            ?FrontierNiceTimeStepMs(std::max(60000.0,double(spanMs)/horizontalIntervals))
            :std::max(1.0,FrontierNiceNumericStep(double(spanMs)/horizontalIntervals));
        QVariantList xTicks;
        QVariantList xTickLabels;
        std::vector<double> niceXTicks;
        double xValue=std::ceil(double(minTime)/xStep)*xStep;
        for(int tick=0;tick<64&&xValue<=double(maxTime);++tick){
            niceXTicks.push_back(xValue);
            const double next=xValue+xStep;if(next<=xValue)break;xValue=next;
        }
        const auto visibleXTicks=FrontierBorderTicks(double(minTime),double(maxTime),
            niceXTicks,graph.width(),timeAxis?140.:90.);
        for(const double xValue:visibleXTicks){
            const qreal x=graph.left()+graph.width()*(xValue-double(minTime))/double(spanMs);
            painter.setPen(QPen(QColor(55,101,110,110),1));
            painter.drawLine(QPointF(x,graph.top()),QPointF(x,graph.bottom()));
            painter.setPen(QPen(QColor("#9bc7c3"),1));
            painter.drawLine(QPointF(x,graph.bottom()),QPointF(x,graph.bottom()+4));
            painter.drawLine(QPointF(x,graph.top()-4),QPointF(x,graph.top()));
            const bool lower=xValue==double(minTime),upper=xValue==double(maxTime);
            const QString label=timeAxis
                ?QDateTime::fromMSecsSinceEpoch(qRound64(xValue),Qt::UTC).toString(
                    lower||upper
                        ?spanMs<60000?"HH:mm:ss.zzz":spanMs>=31536000000LL?"yyyy-MM-dd":"dd/MM HH:mm:ss"
                        :xStep>=86400000.0?"dd/MM":"dd/MM HH:mm")
                :QString::number(qRound64(xValue));
            const qreal labelWidth=timeAxis?130.:86.;
            const qreal labelLeft=lower?x:upper?x-labelWidth:x-labelWidth/2.;
            const auto alignment=lower?Qt::AlignLeft:upper?Qt::AlignRight:Qt::AlignHCenter;
            painter.drawText(QRectF(labelLeft,graph.bottom()+5,labelWidth,16),alignment|Qt::AlignVCenter,label);
            xTicks.append(xValue);
            xTickLabels.append(label);
        }
        setProperty("pgn_graph_x_ticks",xTicks);
        setProperty("pgn_graph_x_tick_labels",xTickLabels);
        QVariantList yTicks;for(const auto value:meritTicks)yTicks.append(value);setProperty("pgn_graph_y_ticks",yTicks);
        painter.setPen(QPen(QColor(111,227,202,145),1));
        painter.drawRect(graph);
        setProperty("pgn_graph_border_tick_contract",true);
        // Interior legacy ticks remain integral. The exact viewport endpoints
        // are independent of those ticks and never reshape the data domain.
        int meritDecimals = 0;
        if (!(m_legacyR4Mode && timeAxis)) {
            double scaledStep = meritStep;
            while (meritDecimals < 9 && std::abs(scaledStep - std::round(scaledStep)) > 1.0e-7) {
                ++meritDecimals; scaledStep *= 10.0;
            }
        }
        for (double value : meritTicks) {
            const qreal y = graph.bottom() - graph.height() *
                (value - minMerit) / (maxMerit - minMerit);
            painter.drawLine(QPointF(graph.left()-4,y),QPointF(graph.left(),y));
            painter.drawLine(QPointF(graph.right(),y),QPointF(graph.right()+4,y));
            const bool endpoint=value==minMerit||value==maxMerit;
            painter.drawText(QRectF(graph.left() - 60, y - 8, 52, 16),
                             Qt::AlignRight | Qt::AlignVCenter,
                             endpoint?QString::number(value,'g',6):QString::number(value,'f',meritDecimals));
        }
        QVector<double> thresholds;
        // Native pixels/axes/target are one COMPLETE frame. Never paint a new
        // live target over an older fitted raster while its replacement waits.
        const auto displayedTarget=m_nativeAllCatalog
            ?(nativeFrame?nativeFrame->yFit.targetY:std::nullopt)
            :(currentTarget?std::optional<double>{currentTarget->targetMerit}:std::nullopt);
        if(displayedTarget)thresholds.append(*displayedTarget);
        setProperty("pgn_native_frame_target_y",displayedTarget?QVariant(*displayedTarget):QVariant{});
        for (const double threshold : thresholds) {
            if (threshold < minMerit || threshold > maxMerit) continue;
            const qreal y = graph.bottom() - graph.height() * (threshold - minMerit) / (maxMerit - minMerit);
            const QColor thresholdColor = QColor(226, 140, 255, 165);
            painter.setPen(QPen(thresholdColor, 1, Qt::DashLine));
            painter.drawLine(QPointF(graph.left(), y), QPointF(graph.right(), y));
            painter.setFont(Font(8, QFont::DemiBold, true));
            painter.drawText(QRectF(graph.right() - 132, y - 14, 128, 13),
                             Qt::AlignRight,
                             QStringLiteral("NETWORK M%1").arg(threshold, 0, 'f', 3));
        }
        if (m_legacyR4Mode && timeAxis) {
            // The right axis describes the same merit-domain transform even
            // before RPC has supplied a difficulty history.  Keep its rounded
            // scale visible at all times; only the authoritative curve itself
            // is withheld while the wallet is offline.
            painter.setFont(Font(8, QFont::DemiBold, true));
            painter.setPen(QColor(226, 140, 255, 175));
            for (double value : meritTicks) {
                const qreal y = graph.bottom() - graph.height() *
                    (value - minMerit) / (maxMerit - minMerit);
                painter.drawText(QRectF(graph.right() + 5, y - 8, 46, 16),
                                 Qt::AlignLeft | Qt::AlignVCenter,
                                 QString::number(value, 'f', meritDecimals));
            }
            if (!m_legacyDifficulty.isEmpty()) {
                // Difficulty is itself a merit threshold.  Both axes share
                // one exact vertical transform; an independent right-axis
                // auto-scale would visually invert gap-vs-difficulty order.
                QPainterPath difficultyPath;
                const auto sampleAtOrAfter = std::lower_bound(
                    m_legacyDifficulty.cbegin(), m_legacyDifficulty.cend(), minTime,
                    [](const QPair<qint64, double>& sample, qint64 timestamp) {
                        return sample.first < timestamp;
                    });
                auto cursor = sampleAtOrAfter;
                bool started = false;
                double currentDifficulty = 0.0;
                const auto difficultyY = [&](double value) {
                    return graph.bottom() - graph.height() *
                        (value - minMerit) / (maxMerit - minMerit);
                };
                if (sampleAtOrAfter != m_legacyDifficulty.cbegin()) {
                    currentDifficulty = std::prev(sampleAtOrAfter)->second;
                    difficultyPath.moveTo(graph.left(), difficultyY(currentDifficulty));
                    started = true;
                } else if (sampleAtOrAfter != m_legacyDifficulty.cend() &&
                           sampleAtOrAfter->first <= maxTime) {
                    currentDifficulty = sampleAtOrAfter->second;
                    const qreal firstX = graph.left() + graph.width() *
                        (sampleAtOrAfter->first - minTime) /
                        static_cast<double>(maxTime - minTime);
                    difficultyPath.moveTo(firstX, difficultyY(currentDifficulty));
                    started = true;
                    ++cursor;
                }
                for (; cursor != m_legacyDifficulty.cend() && cursor->first <= maxTime;
                     ++cursor) {
                    const qreal x = graph.left() + graph.width() *
                        (cursor->first - minTime) /
                        static_cast<double>(maxTime - minTime);
                    if (!started) {
                        currentDifficulty = cursor->second;
                        difficultyPath.moveTo(x, difficultyY(currentDifficulty));
                        started = true;
                        continue;
                    }
                    // Network difficulty is constant between block changes.
                    // Draw a true staircase instead of a misleading diagonal.
                    difficultyPath.lineTo(x, difficultyY(currentDifficulty));
                    currentDifficulty = cursor->second;
                    difficultyPath.lineTo(x, difficultyY(currentDifficulty));
                }
                if (started) {
                    difficultyPath.lineTo(graph.right(), difficultyY(currentDifficulty));
                }
                painter.save();
                painter.setClipRect(graph, Qt::IntersectClip);
                painter.setPen(QPen(QColor(226, 140, 255, 105), 1.35));
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(difficultyPath);
                painter.restore();
            }
            qint64 currentRunStartMs = -1;
            for (const std::string& activeRunId : activeRunIds) {
                const auto iterator = m_legacyRunStartedUtcMs.constFind(
                    QString::fromStdString(activeRunId));
                if (iterator == m_legacyRunStartedUtcMs.cend()) continue;
                currentRunStartMs = currentRunStartMs < 0
                    ? *iterator : std::min(currentRunStartMs, *iterator);
            }
            if (currentRunStartMs < 0) currentRunStartMs = m_lastLegacyRunStartedUtcMs;
            if (currentRunStartMs >= minTime && currentRunStartMs <= maxTime) {
                const qreal runX = graph.left() + graph.width() *
                    (currentRunStartMs - minTime) /
                    static_cast<double>(maxTime - minTime);
                painter.setPen(QPen(QColor(255, 154, 61, 92), 1, Qt::DashLine));
                painter.drawLine(QPointF(runX, graph.top()), QPointF(runX, graph.bottom()));
                painter.setFont(Font(7, QFont::DemiBold, true));
                painter.setPen(QColor(255, 174, 91, 175));
                painter.drawText(QRectF(std::clamp(runX - 34.0, graph.left(), graph.right() - 68.0),
                                        graph.top() + 4, 68, 13),
                                 Qt::AlignCenter, QStringLiteral("RUN START"));
            }
        }
        // Dense rendering visits all source points only when data/view changes.
        // Repaints use one cached image: no point sampling, averages or trends.
        if(includeImported&&(!m_nativeAllCatalog||nativeFrame)){
            const pgn::ui::DenseView importedView{double(minTime),double(maxTime),minMerit,maxMerit,
                QSize(std::max(1,qRound(graph.width())),std::max(1,qRound(graph.height()))),
                static_cast<std::uint64_t>(m_observationCatalog->lengthBounds().revision)};
            m_observationCatalog->request("length",importedView);
            const auto* raster=m_observationCatalog->raster("length",importedView);
            if(!raster)raster=m_observationCatalog->lastComplete("length");
            setProperty("pgn_imported_visible_points",raster?raster->points:0);
            setProperty("pgn_imported_render_ns",raster?raster->elapsedNs:0);
            setProperty("pgn_imported_busy",m_observationCatalog->busy());
            setProperty("pgn_imported_error",m_observationCatalog->error());
            pgn::ui::paintCatalogFrame(painter,graph,importedView,raster);
            painter.setFont(Font(8,QFont::DemiBold,true));painter.setPen(QColor(QStringLiteral("#9ab0b9")));
            const QRectF importedLabel=HorizonGraphControls::importedFrameCaption(plot);
            painter.save();painter.setClipRect(importedLabel,Qt::IntersectClip);
            painter.drawText(importedLabel,Qt::AlignLeft|Qt::AlignVCenter,
                QStringLiteral("DATA SOURCES"));
            painter.restore();
            m_uiTooltipHits.append({importedLabel.adjusted(0,-1,0,1),
                pgn::ui::catalogFrameCaption(raster,m_observationCatalog->error(),QStringLiteral("IMPORTED"))+
                QStringLiteral("\nGrey points are retained observations from earlier sources. Their original exact endpoints are preserved, but their event date and device are unknown. They are separate from current-run results and independently verified coverage. Duplicate origins are not counted as unique results.")});
        }else{setProperty("pgn_imported_visible_points",0);setProperty("pgn_imported_busy",false);}
        if(m_nativeAllCatalog){
            const auto* raster = nativeFrame;
            const bool currentRaster = nativeFrameCurrent;
            const auto& nativeView = nativeRequestedView;
            pgn::ui::paintCatalogFrame(painter,graph,raster?raster->view:nativeView,raster);
            if(!raster){
                painter.setPen(QColor("#9ec7c1"));
                painter.drawText(graph,Qt::AlignCenter,QStringLiteral("Loading %1…").arg(m_showFullHistory?"complete history":"current range"));
            }
            setProperty("pgn_native_display_min_x",double(minTime));
            setProperty("pgn_native_display_max_x",double(maxTime));
            setProperty("pgn_native_display_min_y",minMerit);
            setProperty("pgn_native_display_max_y",maxMerit);
            setProperty("pgn_native_visible_points",raster?raster->points:0);
            setProperty("pgn_native_render_ns",raster?raster->elapsedNs:0);
            setProperty("pgn_native_rows_read",raster?raster->rowsRead:0);
            setProperty("pgn_native_incremental",raster&&raster->incremental);
            setProperty("pgn_native_cache_hits",nativeCatalog?qulonglong(nativeCatalog->cacheHits()):0);
            setProperty("pgn_native_cache_frames",nativeCatalog?qulonglong(nativeCatalog->cachedFrames()):0);
            setProperty("pgn_native_cache_bytes",nativeCatalog?nativeCatalog->cacheBytes():0);
            const auto nativeError=nativeCatalog&&!nativeCatalog->error().isEmpty()?nativeCatalog->error():property("pgn_catalog_archive_error").toString();
            setProperty("pgn_native_loading",!currentRaster);setProperty("pgn_native_display_revision",raster?qulonglong(raster->view.revision):0);setProperty("pgn_native_error",nativeError);
            setProperty("pgn_native_target_revision",qulonglong(nativeView.revision));
            const auto caption=pgn::ui::catalogFrameCaption(raster,nativeError);
            const auto captionRect=HorizonGraphControls::frameCaption(plot);
            setProperty("pgn_native_frame_caption",caption);setProperty("pgn_native_frame_caption_rectangle",captionRect);
            painter.setFont(Font(8,QFont::DemiBold,true));painter.setPen(QColor(QStringLiteral("#779f9d")));
            if(!nativeError.isEmpty())painter.setPen(QColor(QStringLiteral("#ffad4a")));
            painter.save();painter.setClipRect(captionRect,Qt::IntersectClip);
            painter.drawText(captionRect,Qt::AlignRight|Qt::AlignVCenter,
                painter.fontMetrics().elidedText(caption,Qt::ElideMiddle,int(captionRect.width())));
            painter.restore();
            m_uiTooltipHits.append({captionRect,
                QStringLiteral("Displayed complete frame: revision %1, %2 raw points. Index target: revision %3. Refresh: %4. Index error: %5. The old frame remains explicitly identified while the exact worker result is pending; no raw counter or timestamp is smoothed.")
                .arg(raster?qulonglong(raster->view.revision):0).arg(raster?raster->points:0).arg(nativeView.revision)
                .arg(currentRaster?QStringLiteral("complete"):QStringLiteral("pending"))
                .arg(nativeError.isEmpty()?QStringLiteral("none"):nativeError)});
            m_uiTooltipHits.append({graph,QStringLiteral("Every indexed source observation in this viewport contributes its raw pixel footprint. No averages, sampling or invented dates. Exact endpoints and all origins remain in the SQLite archive; the 512 rich UI records are only a display cache. Independent proof remains pending.")});
            painter.restore();return;
        }
        if(viewportRecords.size()>4096){
            const pgn::ui::DenseView view{double(minTime),double(maxTime),minMerit,maxMerit,
                QSize(std::max(1,qRound(graph.width())),std::max(1,qRound(graph.height()))),m_denseViewportRevision};
            if(!m_denseLayer.matches(view)){
                std::vector<pgn::ui::DensePoint> raw;raw.reserve(viewportRecords.size());
                for(const auto& record:viewportRecords){
                    const auto& point=m_candidates[record.archiveIndex];
                    raw.push_back({record.x,record.y,candidateDeviceColor(point).rgba()});
                }
                m_denseLayer.rebuild(view,raw);
            }
            painter.save();painter.setClipRect(graph,Qt::IntersectClip);m_denseLayer.paint(painter,graph);painter.restore();
            painter.setFont(Font(8,QFont::DemiBold,true));painter.setPen(QColor(QStringLiteral("#779f9d")));
            painter.drawText(QRectF(graph.left(),graph.top()-31,graph.width(),12),Qt::AlignRight,
                QStringLiteral("VIEWPORT · %1 RAW POINTS · CACHED RASTER").arg(m_denseLayer.visibleCount()));
            m_uiTooltipHits.append({graph,QStringLiteral("Every visible source point contributes its actual pixel footprint. No averages or sampling. Exact endpoints/provenance remain in the archive; dense hover lookup is not connected yet.")});
            painter.restore();return;
        }
        // The archive remains lossless, while painting is bounded by the
        // current viewport.  Bucket extrema are selected against the actual
        // X/Y bounds (not against chronology), so LENGTH mode remains a useful
        // distribution instead of a random tail sample.
        const std::size_t maximumPaintPoints = std::max<std::size_t>(
            4096, static_cast<std::size_t>(std::max(1.0, graph.width()) * 8.0));
        const FrontierGraphProjectionResult lod = FrontierViewportGraphProjection(
            viewportRecords,
            static_cast<double>(minTime), static_cast<double>(maxTime),
            minMerit, maxMerit, qRound(graph.width()), maximumPaintPoints);
        points.clear();
        points.reserve(static_cast<qsizetype>(lod.archiveIndices.size()));
        for (const std::size_t archiveIndex : lod.archiveIndices) {
            if (archiveIndex < m_candidates.size()) points.append(&m_candidates[archiveIndex]);
        }
        painter.setFont(Font(8, QFont::DemiBold, true));
        painter.setPen(QColor(QStringLiteral("#779f9d")));
        painter.drawText(QRectF(graph.left(), graph.top() - 31, graph.width(), 12),
                         Qt::AlignRight,
                         lod.sampled
                             ? QStringLiteral("VIEWPORT LOD · %1 / %2 POINTS")
                                   .arg(points.size()).arg(lod.visibleCount)
                             : QStringLiteral("VIEWPORT · %1 POINTS").arg(points.size()));

        // Individual source points only: no mean, bin smoothing or synthetic
        // trend overlays. The complete graph engine replacement is deferred.
        if (points.isEmpty()) {
            painter.restore();
            return;
        }
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        struct PlotCluster {
            QPointF center;
            QVector<const CandidatePoint*> members;
        };
        const CandidatePoint* latestPoint = nullptr;
        qint64 latestPointTime = std::numeric_limits<qint64>::min();
        for (const CandidatePoint* candidate : points) {
            const qint64 candidateTime = pointXValue(*candidate);
            // "Latest" means the latest point that is actually visible in the
            // selected viewport.  A point outside a manually panned/zoomed
            // view must not suppress the orange emphasis of the visible tail.
            if (candidateTime < minTime || candidateTime > maxTime ||
                candidate->merit < minMerit || candidate->merit > maxMerit) {
                continue;
            }
            if (candidateTime > latestPointTime ||
                (candidateTime == latestPointTime && latestPoint != nullptr &&
                 candidate->appendedAtMs > latestPoint->appendedAtMs)) {
                latestPoint = candidate;
                latestPointTime = candidateTime;
            }
        }
        QVector<PlotCluster> clusters;
        for (const CandidatePoint* pointPointer : points) {
            const CandidatePoint& point = *pointPointer;
            const qint64 pointTime = pointXValue(point);
            if (pointTime < minTime || pointTime > maxTime ||
                point.merit < minMerit || point.merit > maxMerit) continue;
            const qreal rawX = graph.left() + graph.width() *
                (pointXValue(point) - minTime) /
                static_cast<double>(maxTime - minTime);
            const qreal rawY = graph.bottom() - graph.height() *
                (point.merit - minMerit) / (maxMerit - minMerit);
            const QPointF center(
                FrontierClampMarkerCoordinate(rawX, graph.left(), graph.right(), 12.0),
                FrontierClampMarkerCoordinate(rawY, graph.top(), graph.bottom(), 12.0));
            // One draw record per event.  Overlapping points may overlap on
            // screen, but they are never replaced by a synthetic cluster.
            auto cluster = clusters.end();
            if (cluster == clusters.end()) {
                clusters.append({center, {pointPointer}});
            } else {
                cluster->members.append(pointPointer);
            }
        }
        painter.save();
        painter.setClipRect(graph, Qt::IntersectClip);
        for (const PlotCluster& cluster : clusters) {
            const CandidatePoint& point = *cluster.members.first();
            const qreal x = cluster.center.x();
            const qreal y = cluster.center.y();
            const QString displayStatus = point.terminalDisposition.isEmpty()
                ? point.status : point.terminalDisposition;
            const QColor proofColor = candidateColor(displayStatus);
            const QColor deviceColor = candidateDeviceColor(point);
            const bool terminalRejectedOrStale = !point.terminalDisposition.isEmpty();
            const bool pulse = std::any_of(cluster.members.cbegin(), cluster.members.cend(),
                                           [this, nowMs](const CandidatePoint* member) {
                return member->eventId == m_pulsingEventId &&
                    nowMs - member->appendedAtMs >= 0 && nowMs - member->appendedAtMs < 2800;
            });
            const bool latest = std::any_of(cluster.members.cbegin(), cluster.members.cend(),
                                            [latestPoint](const CandidatePoint* member) {
                return member == latestPoint;
            });
            const QColor latestColor(QStringLiteral("#ffad4a"));
            const bool clustered = cluster.members.size() > 1;
            const QString statusLower = point.status.toLower();
            const bool filterOff = statusLower.contains(QStringLiteral("filter_off"));
            const bool filterOn = statusLower.contains(QStringLiteral("filter_on"));
            const qreal radius = filterOn ? 3.5 : (filterOff ? 5.4 : (clustered ? 7.2 : (pulse
                ? 5.0 + 1.4 * (0.5 + 0.5 * std::sin(m_phase * 18.84955592))
                : 4.2)));
            const QRectF hitArea(x - radius - 6, y - radius - 6,
                                 (radius + 6) * 2, (radius + 6) * 2);
            for (const CandidatePoint* member : cluster.members) {
                m_candidateHits.append({hitArea, cluster.center, member->eventId});
            }
            if (pulse) {
                painter.setPen(QPen(Alpha(deviceColor, 65), 5));
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(QPointF(x, y), radius + 5.5, radius + 5.5);
            }
            painter.setPen(QPen(Alpha(deviceColor, 205), latest ? 2.2 : 1.2));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(QPointF(x, y), radius + 2.7, radius + 2.7);
            const QColor bodyColor = terminalRejectedOrStale ? proofColor : deviceColor;
            painter.setPen(QPen(bodyColor, latest ? 2.8 : 2.0));
            painter.setBrush(Alpha(bodyColor, latest ? 235 : 215));
            if (filterOff) {
                painter.setPen(QPen(proofColor, 2.0));
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(QPointF(x, y), radius, radius);
            } else if (filterOn) {
                painter.setPen(QPen(proofColor, 1.8));
                painter.setBrush(proofColor);
                QPolygonF diamond;
                diamond << QPointF(x, y - radius) << QPointF(x + radius, y)
                        << QPointF(x, y + radius) << QPointF(x - radius, y);
                painter.drawPolygon(diamond);
            } else if (clustered) {
                painter.drawEllipse(QPointF(x, y), radius, radius);
                painter.setFont(Font(8, QFont::DemiBold, true));
                painter.setPen(QColor(QStringLiteral("#ffffff")));
                painter.drawText(QRectF(x - radius, y - radius, radius * 2, radius * 2),
                                 Qt::AlignCenter, QString::number(cluster.members.size()));
            } else if (point.lane == QLatin1String("B")) {
                QPolygonF diamond;
                diamond << QPointF(x, y - radius - 1) << QPointF(x + radius + 1, y)
                        << QPointF(x, y + radius + 1) << QPointF(x - radius - 1, y);
                painter.drawPolygon(diamond);
            } else if (point.deviceId.startsWith(QStringLiteral("CPU-"),
                                                  Qt::CaseInsensitive) ||
                       point.lane.startsWith(QStringLiteral("CPU"),
                                             Qt::CaseInsensitive)) {
                painter.drawRect(QRectF(x - radius, y - radius, radius * 2, radius * 2));
            } else {
                painter.drawEllipse(QPointF(x, y), radius, radius);
            }
            if (!filterOff && !filterOn) {
                painter.setPen(QPen(QColor(2, 12, 20, 235), 0.8));
                painter.setBrush(proofColor);
                painter.drawEllipse(QPointF(x + radius + 2.0, y - radius - 2.0), 2.2, 2.2);
            }
            const bool clusterHovered = std::any_of(cluster.members.cbegin(), cluster.members.cend(),
                                                    [this](const CandidatePoint* member) {
                return member->eventId == m_hoveredEventId;
            });
            if (clusterHovered) {
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(deviceColor, 1.8));
                painter.drawEllipse(QPointF(x, y), radius + 5.0, radius + 5.0);
            }
            if (latest) {
                // The latest exact gap is deliberately source-agnostic and
                // fully orange.  Resource identity remains available in the
                // card/tooltip, but the most recent scientific result must be
                // unmistakable at a glance on a dense multi-device plot.
                painter.setBrush(latestColor);
                painter.setPen(QPen(QColor(QStringLiteral("#ffe1a6")), 2.2));
                painter.drawEllipse(QPointF(x, y), radius + 2.2, radius + 2.2);
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(Alpha(latestColor, 255), 3.2));
                painter.drawEllipse(QPointF(x, y), radius + 6.0, radius + 6.0);
                const qreal latestGlow = 8.7 + 1.8 *
                    (0.5 + 0.5 * std::sin(m_phase * 6.28318530718));
                painter.setPen(QPen(Alpha(latestColor, 125), 2.4));
                painter.drawEllipse(QPointF(x, y), radius + latestGlow,
                                    radius + latestGlow);
            }
        }
        painter.restore();
        const CandidatePoint* hovered = candidateByEventId(m_hoveredEventId);
        if (hovered) {
            const CandidatePoint& point = *hovered;
            const QColor color = candidateDeviceColor(point);
            QPointF tooltipTarget(
                FrontierClampMarkerCoordinate(
                    graph.left() + graph.width() *
                        (pointXValue(point) - minTime) /
                        static_cast<double>(maxTime - minTime),
                    graph.left(), graph.right(), 12.0),
                FrontierClampMarkerCoordinate(
                    graph.bottom() - graph.height() *
                        (point.merit - minMerit) / (maxMerit - minMerit),
                    graph.top(), graph.bottom(), 12.0));
            for (const CandidateHit& hit : m_candidateHits) {
                if (hit.eventId == point.eventId) {
                    tooltipTarget = hit.center;
                    break;
                }
            }
            const qreal targetX = tooltipTarget.x();
            const qreal targetY = tooltipTarget.y();
            const auto breakableHtml = [](const QString& value, int chunk = 30) {
                QString result;
                for (int offset = 0; offset < value.size(); offset += chunk) {
                    if (offset > 0) result += QStringLiteral("&#8203;");
                    result += value.mid(offset, chunk).toHtmlEscaped();
                }
                return result;
            };
            const auto normalHtml = [](const QString& value) { return value.toHtmlEscaped(); };
            const auto row = [](const QString& label, const QString& value) {
                return QStringLiteral(
                    "<tr><td class='label'>%1</td><td class='value'>%2</td></tr>")
                    .arg(label.toHtmlEscaped(), value);
            };
            QStringList clusterIds;
            QStringList clusterSummary;
            for (const CandidateHit& hit : m_candidateHits) {
                if (std::hypot(hit.center.x() - tooltipTarget.x(),
                               hit.center.y() - tooltipTarget.y()) > 0.1 ||
                    clusterIds.contains(hit.eventId)) continue;
                clusterIds.append(hit.eventId);
                const CandidatePoint* member = candidateByEventId(hit.eventId);
                if (member) {
                    const QString deviceLabel = QString::fromStdString(
                        FrontierDeviceProvenanceLabel(member->deviceName.toStdString(),
                                                      member->deviceId.toStdString()));
                    clusterSummary.append(QStringLiteral("%1 · GAP %2 · MERIT %3 · %4")
                        .arg(member->eventId.left(8),
                             QString::number(member->gap, 'f', 0),
                             QString::number(member->merit, 'f', 6),
                             deviceLabel));
                }
            }
            QString html = QStringLiteral(
                "<div class='headline'>%1EVENT %2 &nbsp;·&nbsp; %3</div>"
                "<div class='device'>%4</div><table>")
                .arg(clusterIds.size() > 1
                         ? QStringLiteral("CLUSTER ×%1 &nbsp;·&nbsp; ").arg(clusterIds.size())
                         : QString{},
                     breakableHtml(point.eventId),
                     normalHtml((point.terminalDisposition.isEmpty()
                                     ? point.status : point.terminalDisposition).toUpper()),
                     normalHtml(QString::fromStdString(FrontierDeviceProvenanceLabel(
                         point.deviceName.toStdString(), point.deviceId.toStdString()))));
            if (clusterSummary.size() > 1) {
                html += row(QStringLiteral("CLUSTER"),
                            clusterSummary.join(QStringLiteral("<br/>"))); 
            }
            html += row(QStringLiteral("DISCOVERED LOCAL"),
                        normalHtml(point.observedUtc.toLocalTime().toString(Qt::ISODateWithMs)));
            html += row(QStringLiteral("DISCOVERED UTC"),
                        normalHtml(point.observedUtc.toString(Qt::ISODateWithMs)));
            html += row(QStringLiteral("VERIFIED UTC"),
                        point.verifiedUtc.isValid()
                            ? normalHtml(point.verifiedUtc.toString(Qt::ISODateWithMs))
                            : QString::fromUtf8("—"));
            if (!point.runId.isEmpty()) html += row(QStringLiteral("RUN"), breakableHtml(point.runId));
            if (!point.domainId.isEmpty()) html += row(QStringLiteral("DOMAIN"), breakableHtml(point.domainId));
            QString gapLine = QStringLiteral("%1 &nbsp; <span class='label'>MERIT</span> %2")
                .arg(QString::number(point.gap, 'f', 0), QString::number(point.merit, 'f', 6));
            if (!point.segment.isEmpty()) {
                gapLine += QStringLiteral(" &nbsp; <span class='label'>SEGMENT</span> %1")
                    .arg(normalHtml(point.segment));
            }
            html += row(QStringLiteral("GAP"), gapLine);
            const QString leftPrimeDisplay = m_legacyR4Mode
                ? HexIntegerToDecimal(point.left) : point.left;
            const QString rightPrimeDisplay = m_legacyR4Mode
                ? HexIntegerToDecimal(point.right) : point.right;
            if (!leftPrimeDisplay.isEmpty()) {
                html += row(QStringLiteral("LEFT PRIME"),
                            breakableHtml(leftPrimeDisplay));
            }
            if (!rightPrimeDisplay.isEmpty()) {
                html += row(QStringLiteral("RIGHT PRIME"),
                            breakableHtml(rightPrimeDisplay));
            }
            if (!point.leftGlobalIndex.isEmpty()) {
                html += row(QStringLiteral("LEFT INDEX"), breakableHtml(point.leftGlobalIndex));
            }
            if (!point.rightGlobalIndex.isEmpty()) {
                html += row(QStringLiteral("RIGHT INDEX"), breakableHtml(point.rightGlobalIndex));
            }
            QStringList proof;
            proof.append(point.status);
            if (!point.terminalDisposition.isEmpty()) {
                proof.append(QStringLiteral("SUBMISSION %1").arg(point.terminalDisposition));
            }
            if (!point.blockHeight.isEmpty()) proof.append(QStringLiteral("BLOCK #%1").arg(point.blockHeight));
            if (!point.blockHash.isEmpty()) proof.append(point.blockHash);
            if (!point.certificateKind.isEmpty()) proof.append(point.certificateKind);
            html += row(QStringLiteral("PROOF"), breakableHtml(proof.join(QStringLiteral(" · "))));
            if (!point.networkOutcome.isEmpty()) {
                QString outcome = point.networkOutcome.toUpper();
                if (point.networkOutcomeUtc.isValid()) {
                    outcome += QStringLiteral(" · %1")
                        .arg(point.networkOutcomeUtc.toString(Qt::ISODateWithMs));
                }
                html += row(QStringLiteral("NETWORK STATUS"), breakableHtml(outcome));
            }
            if (!point.certificateSha256.isEmpty()) {
                html += row(QStringLiteral("CERT SHA"), breakableHtml(point.certificateSha256));
            }
            const QString targetContext = candidateTargetContext(point);
            if (!targetContext.isEmpty()) {
                html += row(QStringLiteral("TARGET"), normalHtml(targetContext));
            }
            const FrontierGapGeometryProvenance geometry = FrontierGapGeometryFromEvent(
                point.left.toStdString(), point.right.toStdString(),
                point.numberBits, point.computeShift);
            const int endpointDigits = std::max(leftPrimeDisplay.size(),
                                                rightPrimeDisplay.size());
            html += row(QStringLiteral("DIGITS"),
                        endpointDigits > 0 ? QString::number(endpointDigits)
                                           : QString::fromUtf8("—"));
            html += row(m_legacyR4Mode ? QStringLiteral("NOMINAL BITS")
                                       : QStringLiteral("ACTUAL BITS"),
                        geometry.actualNumberBits
                            ? QString::number(*geometry.actualNumberBits)
                            : QString::fromUtf8("—"));
            html += row(QStringLiteral("HISTORICAL SHIFT"),
                        geometry.historicalShiftEquivalent
                            ? QString::number(*geometry.historicalShiftEquivalent)
                            : QString::fromUtf8("—"));
            html += QStringLiteral("</table>");

            HorizonGraphTooltip::draw(painter,QRectF(8,78,width()-16,height()-106),
                m_mousePosition,QPointF(targetX,targetY),color,html);
        }
        painter.restore();
    }

    void drawIndexPill(QPainter& painter,
                       const QRectF& area,
                       const QString& label,
                       const std::optional<double>& ratio,
                       const QString& rawValue,
                       const QColor& accent)
    {
        const QColor state = !ratio ? QColor(QStringLiteral("#73908f"))
                                    : (*ratio >= 1.0 ? QColor(QStringLiteral("#78f0aa"))
                                                     : QColor(QStringLiteral("#ffad4a")));
        painter.save();
        painter.setBrush(QColor(1, 12, 20, 218));
        painter.setPen(QPen(Alpha(state, 125), 1));
        painter.drawRoundedRect(area, 8, 8);
        painter.setFont(Font(7, QFont::DemiBold, true));
        painter.setPen(Alpha(accent, 175));
        painter.drawText(area.adjusted(9, 4, -7, -area.height() / 2.0),
                         Qt::AlignLeft | Qt::AlignVCenter, label);
        painter.setFont(Font(9, QFont::DemiBold, true));
        painter.setPen(state);
        const QString index = ratio ? QStringLiteral("%1×").arg(*ratio, 0, 'f', 2)
                                    : QStringLiteral("NOT CALIBRATED");
        painter.drawText(area.adjusted(9, area.height() / 2.0 - 4, -7, -3),
                         Qt::AlignLeft | Qt::AlignVCenter, index);
        if (!rawValue.isEmpty()) {
            painter.setFont(Font(7, QFont::Normal, true));
            painter.setPen(QColor(QStringLiteral("#9fc5c0")));
            painter.drawText(area.adjusted(7, area.height() / 2.0 - 4, -8, -3),
                             Qt::AlignRight | Qt::AlignVCenter, rawValue);
        }
        painter.restore();
    }

    void drawGpuControllerBadge(QPainter& painter,
                                const QRectF& area,
                                const QString& gpuUuid)
    {
        if (gpuUuid.isEmpty()) return;
        const FrontierGpuControllerUi::Snapshot& controller = m_gpuControllerSnapshot;
        const QString summary = controller.deviceSummary(gpuUuid);
        const bool attention = !controller.configValid || controller.liveMutationViolation ||
            summary.contains(QStringLiteral("BLOCKED")) ||
            summary.contains(QStringLiteral("FAIL-CLOSED"));
        const bool waiting = !attention &&
            (summary.contains(QStringLiteral("WAITING")) ||
             summary.contains(QStringLiteral("NOT CONFIGURED")));
        const QColor color = attention ? QColor(QStringLiteral("#ff7e91"))
            : waiting ? QColor(QStringLiteral("#ffad4a"))
                      : QColor(QStringLiteral("#69efc5"));
        painter.save();
        painter.setFont(Font(7, QFont::DemiBold, true));
        painter.setPen(Alpha(color, 205));
        painter.drawText(area, Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetrics(painter.font()).elidedText(
                             summary, Qt::ElideRight, qFloor(area.width())));
        painter.restore();
        m_uiTooltipHits.append({area.intersected(m_deviceStripRect),
            summary + QStringLiteral(" · ") + controller.controllerSummary()});
    }

    void drawThrottleBadge(QPainter& painter,
                           const QRectF& area,
                           const QString& stableDeviceId)
    {
        // This is an observational badge, not a control-sidecar state.  It
        // remains live when control is absent or disabled and fails closed to
        // UNKNOWN when the resource relay has no authoritative reason set.
        const FrontierResourceDevice* resource = nullptr;
        for (const FrontierResourceDevice& candidate : m_resourceTelemetry.devices) {
            if (candidate.deviceId.compare(stableDeviceId, Qt::CaseInsensitive) == 0) {
                resource = &candidate;
                break;
            }
        }
        const bool fresh = resourceSampleFresh() && resource != nullptr;
        const QString throttleClass = fresh && resource->throttleTelemetryAuthoritative
            ? resource->throttleClass : QStringLiteral("UNKNOWN");

        QColor color(QStringLiteral("#ffad4a"));
        if (throttleClass == QLatin1String("NONE")) color = QColor(QStringLiteral("#69efc5"));
        else if (throttleClass == QLatin1String("THERMAL")) color = QColor(QStringLiteral("#ff5d67"));
        else if (throttleClass == QLatin1String("POWER/CURRENT")) color = QColor(QStringLiteral("#ffd166"));
        else if (throttleClass == QLatin1String("CLOCK/POLICY")) color = QColor(QStringLiteral("#c69cff"));

        QString label = pgn::ui::throttleBadgeLabel(throttleClass);
        if (fresh) {
            const auto clockText=[](const std::optional<double>& value){return value?QString::number(*value,'f',0):QString::fromUtf8("—");};
            label += QStringLiteral(" · CORE %1 · MEM %2 MHz").arg(clockText(resource->smClockMhz),clockText(resource->memoryClockMhz));
        }
        painter.save();
        painter.setFont(Font(7, QFont::DemiBold, true));
        painter.setPen(Alpha(color, 215));
        painter.drawText(area, Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetrics(painter.font()).elidedText(
                             label, Qt::ElideRight, qFloor(area.width())));
        painter.restore();

        QStringList details;
        details.append(QStringLiteral("THROTTLE %1").arg(throttleClass));
        details.append(QStringLiteral("MONITORING INDEPENDENT OF CONTROL"));
        if (resource != nullptr) {
            details.append(resource->smClockMhz
                ? QStringLiteral("SM CLOCK · %1 MHz · RAW DRIVER SAMPLE").arg(*resource->smClockMhz,0,'f',0)
                : QStringLiteral("SM CLOCK · UNKNOWN · NO INFERRED ZERO"));
            details.append(resource->memoryClockMhz
                ? QStringLiteral("MEMORY CLOCK · %1 MHz · RAW DRIVER SAMPLE").arg(*resource->memoryClockMhz,0,'f',0)
                : QStringLiteral("MEMORY CLOCK · UNKNOWN · NO INFERRED ZERO"));
        }
        if (resource != nullptr && !resource->throttleReasons.isEmpty()) {
            details.append(QStringLiteral("RAW: %1")
                               .arg(resource->throttleReasons.join(QStringLiteral(" · "))));
        }
        if (resource != nullptr && !resource->throttleDetail.isEmpty()) {
            details.append(resource->throttleDetail);
        }
        if (!fresh) details.append(QStringLiteral("resource telemetry unavailable or stale"));
        m_uiTooltipHits.append({area.intersected(m_deviceStripRect), details.join(QStringLiteral(" · "))});
    }

    void drawCpuThreadBudget(QPainter& painter,const QRectF& pod,const FrontierTelemetrySnapshot& lane,const QString& id,const QColor& color,bool supported){
        const int maximum=lane.raw.value("pgn_cpu_maximum_workers").toInt();
        if(maximum<1)return;
        const auto control=m_deviceControls.value(id);const int wanted=std::clamp(qRound(maximum*control.budgetPercent/100.),1,maximum);
        const int active=lane.raw.value("pgn_cpu_active_threads").toInt();
        painter.setFont(Font(7,QFont::DemiBold,true));painter.setPen(Alpha(color,200));
        const auto pages=pgn::ui::cpuMemoryPagesPresentation(lane.raw.value("pgn_cpu_status").toObject(),lane.raw.value("pgn_cpu_control").toBool());
        const qreal badgeWidth=std::min(112.,(pod.width()-24)*.44);
        const QRectF budgetCaption(pod.left()+12,pod.bottom()-24,pod.width()-28-badgeWidth,12);
        const QString budgetText=QString("%1 selected · %2 resident · %3 tasks").arg(wanted).arg(lane.raw.value("pgn_cpu_status").toObject().value("resident_workers").toInt()).arg(active);
        const auto displayedBudget=painter.fontMetrics().elidedText(budgetText,Qt::ElideRight,int(budgetCaption.width()));
        painter.drawText(budgetCaption,Qt::AlignLeft|Qt::AlignVCenter,displayedBudget);
        const QRectF pagesCaption(pod.right()-12-badgeWidth,pod.bottom()-24,badgeWidth,12);
        painter.setPen(pages.color);painter.drawText(pagesCaption,Qt::AlignRight|Qt::AlignVCenter,pages.text);
        m_uiTooltipHits.append({budgetCaption,budgetText});m_uiTooltipHits.append({pagesCaption,pages.tooltip});
        auto pageStates=property("pgn_cpu_memory_pages").toJsonObject();pageStates.insert(id,QJsonObject{{"active",pages.active},{"text",pages.text},{"color",pages.color.name()}});setProperty("pgn_cpu_memory_pages",pageStates);
        const QRectF slider(pod.left()+13,pod.bottom()-8,pod.width()-26,4);const qreal fraction=maximum>1?qreal(wanted-1)/(maximum-1):0.;
        painter.setPen(Qt::NoPen);painter.setBrush(QColor(35,71,78));painter.drawRoundedRect(slider,3,3);painter.setBrush(Alpha(color,supported?210:80));painter.drawRoundedRect(QRectF(slider.left(),slider.top(),slider.width()*fraction,slider.height()),3,3);painter.drawEllipse(QPointF(slider.left()+slider.width()*fraction,slider.center().y()),4,4);
        const auto sliderHit=slider.adjusted(-3,-3,3,3).intersected(m_deviceStripRect);
        if(supported){bool found=false;for(auto& hit:m_deviceControlHits)if(hit.deviceId==id){hit.slider=sliderHit;found=true;break;}if(!found)m_deviceControlHits.append({id,"CPU",{},sliderHit,true});}
        m_uiTooltipHits.append({sliderHit,"Dedicated logical processors. Physical cores are selected before SMT siblings; a budget change is applied after the current bounded pieces finish."});
        if(property("pgn_card_layout_audit").toBool()||property("pgn_card_geometry_enabled").toBool()){
            const auto rect=[](const QRectF& r){return QJsonArray{r.x(),r.y(),r.width(),r.height()};};
            auto footers=property("pgn_cpu_footer_geometry").toJsonObject();
            footers.insert(id,QJsonObject{{"budget",rect(painter.boundingRect(budgetCaption,Qt::AlignLeft|Qt::AlignVCenter,displayedBudget))},
                {"pages",rect(painter.boundingRect(pagesCaption,Qt::AlignRight|Qt::AlignVCenter,pages.text))},{"slider",rect(sliderHit)}});
            setProperty("pgn_cpu_footer_geometry",footers);
        }
    }

    void drawCompactLanePod(QPainter& painter,
                            const QRectF& pod,
                            const FrontierTelemetrySnapshot& lane,
                            const QString& laneName,
                            const QColor& color,
                            const QString& deviceKind,
                            const QString& stableDeviceId)
    {
        painter.save();
        resetRateGaugeEpoch(stableDeviceId,lane);
        const bool verifier = lane.engineId == QLatin1String("frontier-exact-cpu-verifier");
        const bool liveMiner = !verifier && minerLaneLive(lane);
        QLinearGradient glass(pod.topLeft(), pod.bottomRight());
        glass.setColorAt(0.0, liveMiner ? QColor(6, 47, 43, 255) : QColor(4, 31, 34, 255));
        glass.setColorAt(1.0, QColor(2, 10, 21, 255));
        painter.setBrush(glass);
        painter.setPen(QPen(Alpha(color, liveMiner ? 210 : 115), liveMiner ? 1.6 : 1.0));
        painter.drawRoundedRect(pod, 17, 17);
        if (deviceHasFreshGapPulse(stableDeviceId, QDateTime::currentMSecsSinceEpoch())) {
            // Fresh-work accent must not strobe the instruments at the
            // animation clock. It does not alter any measurement or zero.
            const int pulseAlpha = 150;
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(Alpha(color, pulseAlpha), 5.0));
            painter.drawRoundedRect(pod.adjusted(-2, -2, 2, 2), 19, 19);
        }

        const QRectF expandButton(pod.right() - 27, pod.top() + 7, 17, 17);
        painter.setBrush(QColor(4, 24, 31, 235));
        painter.setPen(QPen(Alpha(color, 160), 1));
        painter.drawRoundedRect(expandButton, 7, 7);
        painter.setFont(Font(9, QFont::DemiBold, true));
        painter.setPen(color);
        painter.drawText(expandButton, Qt::AlignCenter, QStringLiteral("+"));
        m_deviceCollapseHits.append({stableDeviceId,
                                     expandButton.adjusted(-7, -7, 7, 7)
                                         .intersected(m_deviceStripRect)});

        const QString controlId = stableDeviceId;
        DeviceControlState& control = m_deviceControls[controlId];
        const bool labCrtTest = lane.raw.value(QStringLiteral("lab_highshift")).toBool(false);
        observeDeviceControl(controlId,lane);
        const bool managedDevice = pgn::ui::miningSourceManaged(this,stableDeviceId,lane.raw.value("pgn_cpu_control").toBool());
        const bool controlConnected = static_cast<bool>(m_deviceControlHandler) && managedDevice;
        const QRectF statePill(pod.right() - 103, pod.top() + 7, 66, 17);
        const QColor stateColor = control.status == FrontierDeviceControlStatus::Pending
            ? QColor(QStringLiteral("#ffad4a"))
            : (control.status == FrontierDeviceControlStatus::Rejected
                   ? QColor(QStringLiteral("#ff7e91"))
                   : (control.enabled ? color : QColor(QStringLiteral("#739da8"))));
        painter.setBrush(Alpha(stateColor, 42));
        painter.setPen(QPen(Alpha(stateColor, 190), 1));
        painter.drawRoundedRect(statePill, 8, 8);
        painter.setFont(Font(7, QFont::DemiBold, true));
        painter.setPen(stateColor);
        painter.drawText(statePill, Qt::AlignCenter,
                         !managedDevice ? (deviceKind=="CPU"?QStringLiteral("OFF"):QStringLiteral("MONITOR")) : labCrtTest ? QStringLiteral("TEST") :
                         deviceControlCaption(control));
        if (controlConnected && !labCrtTest) {
            m_deviceControlHits.append({controlId, deviceKind,
                                        statePill.adjusted(-5, -5, 5, 5)
                                            .intersected(m_deviceStripRect),
                                        QRectF{}, true});
        }
        m_uiTooltipHits.append({statePill.intersected(m_deviceStripRect),
            labCrtTest ? QStringLiteral("External CRT laboratory test · read-only · no block submission") :
            controlConnected && control.status == FrontierDeviceControlStatus::Rejected
                ? control.detail : controlConnected
                ? (lane.raw.value("pgn_test_only").toBool()
                    ? QStringLiteral("Start or stop this bounded RTX 3060 science test. No continuous mining or network credit; state changes only after process acknowledgement.")
                    : QStringLiteral("Turn this %1 mining resource ON or OFF; state changes only after process acknowledgement.").arg(deviceKind))
                : QStringLiteral("Mining control backend unavailable")});

        painter.setFont(Font(9, QFont::DemiBold, true));
        painter.setPen(color);
        const QRectF titleRect(pod.left() + 12, pod.top() + 7, pod.width() - 124, 17);
        QString identity = lane.gpuName.isEmpty() ? stableDeviceId : lane.gpuName;identity.replace("NVIDIA ","");identity.replace("GeForce ","");
        painter.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetrics(painter.font()).elidedText(
                             identity, Qt::ElideRight, qFloor(titleRect.width())));
        painter.setFont(Font(7, QFont::DemiBold, true));
        painter.setPen(Alpha(color, 190));
        const bool benchmarkSource = lane.sourceMode == QLatin1String("benchmark");
        const bool diamondLane = lane.engineId.contains(QStringLiteral("diamond"),
                                                         Qt::CaseInsensitive) ||
            lane.raw.value(QStringLiteral("diamond_continuous")).toBool(false);
        const bool liveV11 = liveMiner && lane.sourceMode == QLatin1String("live") &&
            lane.engineId == QLatin1String("frontier-xmp-multiscale-campaign-worker-v11");
        const QString activeBits = lane.numberBits
            ? QString::number(*lane.numberBits) : QString::fromUtf8("—");
        const QString completedFermat = lane.powmPerSecond
            ? QString::number(*lane.powmPerSecond, 'f', *lane.powmPerSecond < 1000.0 ? 2 : 0)
            : QString::fromUtf8("—");
        QString state = lane.raw.value("pgn_test_only").toBool()
            ? QStringLiteral("BOUNDED TEST · %1 · NO NETWORK CREDIT").arg(lane.raw.value("pgn_process_running").toBool()?"RUNNING":"IDLE")
            : verifier
            ? QStringLiteral("EXACT VERIFIER · %1")
                  .arg(minerLaneLive(lane) ? QStringLiteral("ONLINE")
                                           : QStringLiteral("OFFLINE"))
            : (liveV11
                   ? QStringLiteral("ACTIVE %1b · LAST COMPLETED %2 FERMAT/s")
                         .arg(activeBits, completedFermat)
            : (liveMiner && benchmarkSource
                   ? QStringLiteral("BENCHMARK STREAM · NOT SCIENTIFIC")
                   : (liveMiner ? (labCrtTest ? QStringLiteral("CRT TEST RUNNING · NO BLOCK SUBMISSION")
                                                : diamondLane ? QStringLiteral("DIAMOND CRST RUNNING")
                                                              : QStringLiteral("MINER RUNNING"))
                         : (deviceKind == QLatin1String("CPU")
                                ? QStringLiteral("MINING DISABLED · TELEMETRY %1")
                                      .arg(lane.stale ? QStringLiteral("STALE")
                                                      : QStringLiteral("LIVE"))
                                : QStringLiteral("MONITOR ONLY · %1")
                                      .arg(lane.state.toUpper())))));
        if (lane.raw.value("pgn_cpu_inventory").isObject()) {
            state=pgn::ui::cpuActivityCaption(lane.raw.value("pgn_cpu_status").toObject(),lane.raw.value("pgn_cpu_control").toBool(),property("pgn_cpu_audit_network_status").toString());

        }
        const auto nativeMinimum=lane.raw.value("pgn_native_min_gap").toString();
        if(!nativeMinimum.isEmpty())state+=QStringLiteral(" · MIN GAP %1").arg(nativeMinimum);if(lane.raw.value("pgn_native_profile_k").toInt()>0)state+=QString(" · K%1").arg(lane.raw.value("pgn_native_profile_k").toInt());
        if(!control.enabled||deviceKind=="CPU")painter.drawText(QRectF(pod.left() + 12, pod.top() + 27, pod.width() - 24, 13),
                         Qt::AlignLeft | Qt::AlignVCenter, state);
        if(deviceKind=="CPU")m_uiTooltipHits.append({QRectF(pod.left()+12,pod.top()+27,pod.width()-24,13).intersected(m_deviceStripRect),state});
        if(control.enabled&&deviceKind!="CPU")drawThrottleBadge(painter,
                          QRectF(pod.left() + 12, pod.top() + 27,
                                 pod.width() - 24, 11),
                          stableDeviceId);

        const auto compactAbsoluteRate = [](const std::optional<double>& value) {
            return pgn::ui::compactCardMetricCaption(value);
        };
        const QString key = stableDeviceId;
        const std::optional<double> positions = FrontierDisplayedRate(
            lane.e2eWindowPositionsPerSecond, lane.e2ePositionsPerSecond);
        const std::optional<double> pairs = lane.gapPairsWindowPerSecond
            ? lane.gapPairsWindowPerSecond
            : (lane.epochGapPairsPerSecond ? lane.epochGapPairsPerSecond
                                           : lane.gapPairsTestedPerSecond);
        std::optional<double> candidatesPerMillion;
        if (diamondLane) {
            const double scanned = lane.raw.value(
                QStringLiteral("diamond_cumulative_rows")).toDouble(0.0);
            bool candidatesOk = false;
            const qulonglong candidateCount = lane.cumulativeGapCandidates.toULongLong(
                &candidatesOk);
            if (scanned > 0.0 && candidatesOk) {
                candidatesPerMillion = static_cast<double>(candidateCount) * 1.0e6 / scanned;
            }
        }
        const auto updatePeak = [](QHash<QString, double>& peaks, const QString& id,
                                   const std::optional<double>& value) {
            if (value && std::isfinite(*value) && *value > 0.0) {
                peaks[id] = std::max(peaks.value(id, 0.0), *value);
            }
            return peaks.value(id, 0.0) * 1.15;
        };
        struct CompactMeter { std::optional<double> value; double scale; QString text; QString label; QColor color; };
        QVector<CompactMeter> meters;
        meters.append({lane.gpuSmUtilPercent, 100.0,
                       lane.gpuSmUtilPercent
                           ? QStringLiteral("%1%").arg(*lane.gpuSmUtilPercent, 0, 'f', 0)
                           : QString::fromUtf8("—"),
                       deviceKind == QLatin1String("CPU")
                           ? (m_legacyR4Mode ? QStringLiteral("HOST LOAD")
                                             : QStringLiteral("CPU"))
                           : QStringLiteral("GPU BUSY"), color});
        if (verifier) {
            const FrontierVerifierProgress progress = verifierProgress(lane);
            const bool verifying = lane.verifierItemsPerSecond &&
                *lane.verifierItemsPerSecond > 0.0;
            meters.append({verifying ? lane.verifierItemsPerSecond : std::nullopt, 0.0,
                           verifying ? compactAbsoluteRate(lane.verifierItemsPerSecond)
                                     : (progress.caughtUp() ? QStringLiteral("IDLE")
                                                            : QStringLiteral("WAIT")),
                           QStringLiteral("VERIFY/s"),
                           QColor(QStringLiteral("#69efc5"))});
            meters.append({std::optional<double>(static_cast<double>(progress.totalPending())),
                           0.0, QString::number(progress.totalPending()),
                           QStringLiteral("PENDING"), QColor(QStringLiteral("#ffad4a"))});
        } else {
            if (m_legacyR4Mode && !diamondLane) {
                const QJsonValue ppsValue = lane.raw.value(QStringLiteral("legacy_estimated_pps"));
                const std::optional<double> minerPps = property("pgn_network_ticket_frame").isValid()?lane.e2ePositionsPerSecond:ppsValue.isDouble() &&
                    std::isfinite(ppsValue.toDouble()) && ppsValue.toDouble() >= 0.0
                    ? std::optional<double>(ppsValue.toDouble()) : std::nullopt;
                meters.append({minerPps,
                               updatePeak(m_devicePositionRatePeaks, key + QStringLiteral("-pps"),
                                          minerPps),
                               compactAbsoluteRate(minerPps), QStringLiteral("pos/s"),
                               QColor(QStringLiteral("#b58cff"))});
                meters.append({lane.powmPerSecond,
                               updatePeak(m_devicePositionRatePeaks, key + QStringLiteral("-prp"),
                                          lane.powmPerSecond),
                               compactAbsoluteRate(lane.powmPerSecond), QStringLiteral("tests/s"),
                               QColor(QStringLiteral("#46c8ee"))});
            } else {
                meters.append({positions, updatePeak(m_devicePositionRatePeaks, key, positions),
                               compactAbsoluteRate(positions),
                               diamondLane ? QStringLiteral("CRST rows/s")
                                           : QStringLiteral("POS/s"),
                               QColor(QStringLiteral("#b58cff"))});
            }
            if (diamondLane) {
                meters.append({candidatesPerMillion,
                               updatePeak(m_devicePairRatePeaks,
                                          key + QStringLiteral("-cand-per-m"),
                                          candidatesPerMillion),
                               candidatesPerMillion
                                   ? QString::number(*candidatesPerMillion, 'f', 3)
                                   : QString::fromUtf8("—"),
                               QStringLiteral("CAND/M"),
                               QColor(QStringLiteral("#ffad4a"))});
            } else {
                const QJsonValue processedValue=lane.raw.value(QStringLiteral("pgn_processed_gaps_per_second"));
                const std::optional<double> processed=processedValue.isDouble() && std::isfinite(processedValue.toDouble()) && processedValue.toDouble()>=0
                    ? std::optional<double>(processedValue.toDouble()) : std::nullopt;
                // No producer counter means this requested metric is absent,
                // not a duplicate output gauge or an invented zero. Raw0 is valid.
                // Keep the requested gap throughput on native cards too.
                // Its tooltip states the producer's minimum-gap gate; this
                // counter is not an exhaustive count of every short gap.
                const auto shownProcessed = processed;
                if(shownProcessed&&!property("pgn_network_ticket_frame").isValid())meters.append({shownProcessed, updatePeak(m_devicePairRatePeaks,key+QStringLiteral("-processed"),shownProcessed),
                               compactAbsoluteRate(shownProcessed),QStringLiteral("gaps/s"),QColor(QStringLiteral("#69efc5"))});
                const auto rawYield=lane.raw.value("pgn_retained_gaps_per_billion_positions");
                const std::optional<double> yield=(rawYield.isDouble()?std::optional<double>(rawYield.toDouble()):std::nullopt);
                meters.append({yield,updatePeak(m_devicePairRatePeaks,key+"-yield",yield),pgn::ui::fixedCardMetricCaption(yield),QStringLiteral("gaps/Gpos"),QColor("#b58cff")});
            }
        }
        if(deviceKind=="CPU"&&lane.raw.value("pgn_cpu_inventory").isObject()){const auto cpu=pgn::ui::cpuCardMetrics(lane.raw.value("pgn_cpu_status").toObject(),lane.raw.value("pgn_cpu_control").toBool(),false,color);meters.clear();for(const auto& metric:cpu)meters.append({metric.value,metric.scale,metric.text,metric.label,metric.color});auto states=property("pgn_cpu_card_metrics").toJsonObject();states.insert(stableDeviceId,pgn::ui::cpuMetricSnapshot(cpu));setProperty("pgn_cpu_card_metrics",states);}
        // A pending stop is not a stopped engine: retain live measured rates
        // until the producer actually stops, independently of the request.
        const bool keepLastPgnSample = lane.raw.value(QStringLiteral("pgn_process_running")).toBool(false);
        if (!(deviceKind=="CPU"&&lane.raw.value("pgn_cpu_control").toBool())&&!liveMiner && !keepLastPgnSample) for(int i=1;i<meters.size();++i){meters[i].value.reset();meters[i].text=QStringLiteral("—");}
        const auto bands=pgn::ui::deviceCardBands(pod,1,pgn::ui::hasCpuThreadBudget(deviceKind,lane.raw)?2:0,18.);
        for (int index = 0; index < meters.size(); ++index) {
            const CompactMeter& meter = meters.at(index);
            const FrontierGaugeGeometry gauge = FrontierGaugeGeometryForCard(
                pod.left(), pod.width(), index, meters.size(), bands.centerY,bands.radius);
            drawGauge(painter, QPointF(gauge.centerX,bands.centerY), gauge.radius,
                      (property("pgn_compact_mining").toBool()||deviceKind=="CPU")&&(meter.label.compare("pos/s",Qt::CaseInsensitive)==0||meter.label=="tests/s"||meter.label=="gaps/s"||meter.label=="gaps/Gpos")?rateGaugeFraction(lane,stableDeviceId,meter.label,meter.value):meter.value&&meter.scale>0?*meter.value/meter.scale:0.,
                      meter.color, meter.text, meter.label, gauge.meterWidth - 4.0);
            QString explanation;
            if(meter.label == QLatin1String("pos/s")) explanation=QStringLiteral("Numeric positions advanced per second: delta of the range cursor divided by the exact adjacent source interval. Sieving and jumps discard most integers; this is not the number of primality tests.");
            else if(meter.label == QLatin1String("tests/s")) explanation=QStringLiteral("GPU base-2 probable-prime tests executed per second, measured from the producer counter. Only selected candidates are tested; repeated tests count as work. Not an exact primality certificate, and not numeric positions/s.");
            else if(meter.label == QLatin1String("gaps/s")) explanation=QStringLiteral("Processed gaps/s: candidates above the GPU gap gate examined by the host. NOT every small prime gap. Absent producer counter = unavailable, never inferred from output.");
            else if(meter.label == QLatin1String("gaps/Gpos")) explanation=QStringLiteral("Input/output yield: retained gap delta × 1,000,000,000 / position delta, in the SAME adjacent source interval. Zero positions = undefined, not zero. Compare only with equal level, gap/merit thresholds and verification policy. Not a difficulty or proof score.");
            else explanation=QStringLiteral("Raw driver GPU utilization sample; not an inferred kernel duty cycle.");
            if(meter.label==QLatin1String("gaps/s")||meter.label==QLatin1String("gaps/Gpos"))
                explanation+=QStringLiteral(" Display only: gap counter deltas over the same active-computation window, up to60seconds. Counts, history, source age, pos/s and tests/s are untouched.");
            m_uiTooltipHits.append({QRectF(gauge.centerX-gauge.meterWidth/2.0,pod.top()+43,gauge.meterWidth,63),explanation});
        }
        if(deviceKind=="CPU"&&lane.raw.value("pgn_cpu_inventory").isObject())drawCpuThreadBudget(painter,pod,lane,stableDeviceId,color,controlConnected);
        painter.restore();
    }

    void drawHybridLanePod(QPainter& painter,
                           const QRectF& pod,
                           const FrontierTelemetrySnapshot& lane,
                           const QVector<LaneSample>& history,
                           const QString& laneName,
                           const QColor& color,
                           const QString& deviceKind,
                           const QString& stableDeviceId)
    {
        painter.save();
        const bool diamondLane = lane.engineId.contains(QStringLiteral("diamond"),
                                                         Qt::CaseInsensitive) ||
            lane.raw.value(QStringLiteral("diamond_continuous")).toBool(false);
        const bool nativePgn=lane.engineId==QStringLiteral("pgn-native-walk");
        resetRateGaugeEpoch(stableDeviceId,lane);
        const bool compactPresentation=property("pgn_compact_mining").toBool()&&m_collapsedDevices.value(stableDeviceId,true);
        const auto monotonicAge=pgn::mining::sourceAgeLowerBoundNs(lane.raw);
        const qint64 sampleAgeMs = nativePgn
            ? (monotonicAge?*monotonicAge/1000000:std::numeric_limits<qint64>::max())
            : (lane.observedUtc.isValid()?lane.observedUtc.msecsTo(QDateTime::currentDateTimeUtc()):std::numeric_limits<qint64>::max());
        const bool fresh = lane.raw.value("pgn_cpu_inventory").isObject() || (sampleAgeMs >= -5000 && sampleAgeMs <= 12000);
        const bool live = lane.authoritative && !lane.stale && fresh;
        const bool positiveRate = (lane.e2ePositionsPerSecond && *lane.e2ePositionsPerSecond > 0.0) ||
            (lane.kernelPositionsPerSecond && *lane.kernelPositionsPerSecond > 0.0) ||
            (lane.rateRowsPerSecond && *lane.rateRowsPerSecond > 0.0);
        const bool terminalState = lane.state == QLatin1String("complete") ||
            lane.state == QLatin1String("failed") || lane.state == QLatin1String("disconnected");
        const bool exactVerifierCard =
            lane.engineId == QLatin1String("frontier-exact-cpu-verifier");
        const bool active = !exactVerifierCard && live && !lane.terminalSealed && !terminalState &&
            (lane.raw.value(QStringLiteral("pgn_process_running")).toBool(false) || positiveRate || lane.producerStatus.compare(QStringLiteral("running"),
                                                          Qt::CaseInsensitive) == 0);
        HorizonDeviceVisuals::frame(painter,pod,color,active);
        if (deviceHasFreshGapPulse(stableDeviceId, QDateTime::currentMSecsSinceEpoch())) {
            const int pulseAlpha = 150;
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(Alpha(color, pulseAlpha), 5.0));
            painter.drawRoundedRect(pod.adjusted(-2, -2, 2, 2), 19, 19);
        }
        painter.setPen(QPen(Alpha(color, active ? 235 : 105), active ? 3 : 1,
                            Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(pod.left() + 18, pod.top() + 1.5),
                         QPointF(pod.right() - 18, pod.top() + 1.5));

        const QRectF collapseButton(pod.right() - 27, pod.top() + 7, 17, 17);
        painter.setBrush(QColor(4, 24, 31, 235));
        painter.setPen(QPen(Alpha(color, 150), 1));
        painter.drawRoundedRect(collapseButton, 7, 7);
        painter.setFont(Font(9, QFont::DemiBold, true));
        painter.setPen(color);
        painter.drawText(collapseButton, Qt::AlignCenter, compactPresentation?QStringLiteral("+"):QStringLiteral("−"));
        m_deviceCollapseHits.append({stableDeviceId,
                                     collapseButton.adjusted(-7, -7, 7, 7)
                                         .intersected(m_deviceStripRect)});

        const QString controlId = lane.gpuUuid.isEmpty() ? laneName : lane.gpuUuid;
        DeviceControlState& control = m_deviceControls[controlId];
        observeDeviceControl(controlId,lane);
        // A configured create-only sidecar makes requests available for both
        // CPU and GPU resource cards.  Only an authenticated response may
        // promote a request to APPLIED; READY never claims a backend action.
        // Legacy R4 has an acknowledged process ON/OFF control, but no honest
        // runtime power/work-budget command.  Keep those capabilities
        // separate: the state pill remains actionable while the slider stays
        // absent.
        const bool managedDevice = pgn::ui::miningSourceManaged(this,stableDeviceId,lane.raw.value("pgn_cpu_control").toBool());
        const bool controlConnected = static_cast<bool>(m_deviceControlHandler) && managedDevice;
        const bool budgetControlConnected = controlConnected && !m_legacyR4Mode;
        const FrontierVerifierDisplayState verifierDisplay = exactVerifierCard
            ? verifierDisplayState(lane) : FrontierVerifierDisplayState::Unavailable;
        const bool verifierOnline = verifierDisplay == FrontierVerifierDisplayState::Online;
        const bool verifierArchive = verifierDisplay == FrontierVerifierDisplayState::SealedArchive;
        const QRectF statePill(pod.right() - 156, pod.top() + 7, 120, 17);
        const QColor controlColor = control.status==FrontierDeviceControlStatus::Pending?QColor("#ffad4a"):m_legacyR4Mode ? (control.known && control.enabled ? color : QColor(QStringLiteral("#739da8")))
            : verifierOnline
            ? QColor(QStringLiteral("#8ee7c3"))
            : (!controlConnected ? QColor(QStringLiteral("#739da8"))
            : (control.status == FrontierDeviceControlStatus::Rejected
            ? QColor(QStringLiteral("#ff7e91"))
            : (control.status == FrontierDeviceControlStatus::Pending
                   ? QColor(QStringLiteral("#ffad4a"))
                   : color)));
        painter.setBrush(Alpha(controlColor, 42));
        painter.setPen(QPen(Alpha(controlColor, 190), 1));
        painter.drawRoundedRect(statePill, 8, 8);
        painter.setFont(Font(7, QFont::DemiBold, true));
        painter.setPen(controlColor);
        const QString controlSuffix = control.status == FrontierDeviceControlStatus::Pending
            ? QStringLiteral("PENDING")
            : (control.status == FrontierDeviceControlStatus::Rejected
                   ? QStringLiteral("REJECTED")
                   : (control.status == FrontierDeviceControlStatus::Applied
                           ? QStringLiteral("APPLIED") : QStringLiteral("READY")));
        painter.drawText(statePill, Qt::AlignCenter,
                         m_legacyR4Mode
                             ? deviceControlCaption(control)
                         : exactVerifierCard
                             ? (verifierOnline ? QStringLiteral("VERIFIER ONLINE")
                                : (verifierArchive ? QStringLiteral("SEALED EXACT ARCHIVE")
                                                   : QStringLiteral("VERIFIER OFFLINE")))
                             : (controlConnected
                              ? (control.status == FrontierDeviceControlStatus::Idle
                                      ? QStringLiteral("SIDECAR READY")
                                     : QStringLiteral("%1 · %2")
                                           .arg(control.enabled ? QStringLiteral("ON") : QStringLiteral("OFF"),
                                                controlSuffix))
                               : QStringLiteral("NO CONTROL SIDECAR")));
        if (!control.detail.isEmpty()) {
            m_uiTooltipHits.append({statePill.intersected(m_deviceStripRect),
                QStringLiteral("%1 · REQUEST %2")
                    .arg(control.detail,
                         control.requestId.isEmpty() ? QStringLiteral("NONE")
                                                     : control.requestId)});
        }

        painter.setFont(Font(8, QFont::DemiBold, true));
        painter.setPen(live ? color : QColor(QStringLiteral("#ff7e91")));
        const QRectF titleRect(pod.left() + 12, pod.top() + 7, pod.width() - 176, 17);
        QString compactGpuName = lane.gpuName;
        if (deviceKind == QLatin1String("CPU")) {
            compactGpuName.replace(QStringLiteral("(R)"), QString{}, Qt::CaseInsensitive);
            compactGpuName.replace(QStringLiteral("(TM)"), QString{}, Qt::CaseInsensitive);
            compactGpuName.replace(QRegularExpression(QStringLiteral("\\bCPU\\b"),
                                                       QRegularExpression::CaseInsensitiveOption),
                                   QString{});
            compactGpuName.remove(QRegularExpression(QStringLiteral("\\s+@.*$")));
            compactGpuName.remove(QRegularExpression(QStringLiteral("\\s+[0-9]+\\s*$")));
            compactGpuName = compactGpuName.simplified();
        } else {
            compactGpuName.replace(QStringLiteral("NVIDIA GeForce "), QString{});
        }
        const QString title = compactGpuName.isEmpty() ? QStringLiteral("UNBOUND DEVICE")
                                                        : compactGpuName;
        painter.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, deviceKind=="CPU"?QFontMetrics(painter.font()).elidedText(title,Qt::ElideRight,qFloor(titleRect.width())):title);
        m_uiTooltipHits.append({titleRect.intersected(m_deviceStripRect),
            QStringLiteral("%1 · physical resource telemetry")
                .arg(lane.gpuName.isEmpty() ? QStringLiteral("Unbound device") : lane.gpuName)});

        const auto compactComputeRate = [](const std::optional<double>& value,
                                           const QString& unit) {
            if (!value || !std::isfinite(*value) || *value < 0.0) {
                return QString::fromUtf8("—") + unit;
            }
            if (*value >= 1.0e9) return QStringLiteral("%1G").arg(*value / 1.0e9, 0, 'f', 2) + unit;
            if (*value >= 1.0e6) return QStringLiteral("%1M").arg(*value / 1.0e6, 0, 'f', 1) + unit;
            if (*value >= 1.0e3) return QStringLiteral("%1K").arg(*value / 1.0e3, 0, 'f', 1) + unit;
            return QString::number(*value, 'f', 0) + unit;
        };
        const bool staticProfile = !lane.staticComputeProfile.isEmpty() ||
            lane.staticProfileE2ePositionsPerSecond || lane.staticProfilePowmPerSecond ||
            lane.staticProfileEfficiencyPositionsPerJoule;
        QStringList computeSummary;
        if (lane.raw.value("pgn_cpu_inventory").isObject()) {
            const auto inventory=lane.raw.value("pgn_cpu_inventory").toObject();
            const auto features=inventory.value("capabilities").toObject();
            computeSummary.append(QString("%1 cores · %2 threads · %3").arg(inventory.value("topology").toArray().size()).arg(inventory.value("logical_cpus_available").toInt()).arg(features.value("avx2_usable").toBool()?QString("AVX2"):QString("scalar instruction set")));
        }

        const bool liveV11 = lane.sourceMode == QLatin1String("live") &&
            lane.engineId == QLatin1String("frontier-xmp-multiscale-campaign-worker-v11") &&
            minerLaneLive(lane);
        if (liveV11) {
            const QJsonObject data = lane.raw.value(QStringLiteral("data")).toObject();
            const QString activeBits = lane.numberBits
                ? QString::number(*lane.numberBits) : QString::fromUtf8("—");
            const QString completedFermat = lane.powmPerSecond
                ? QString::number(*lane.powmPerSecond, 'f',
                                  *lane.powmPerSecond < 1000.0 ? 2 : 0)
                : QString::fromUtf8("—");
            const QString ledgerCampaign = JsonInteger(data, "ledger_campaign");
            const QString producerObserved = JsonText(data, "producer_observed_utc");
            QDateTime producerUtc = QDateTime::fromString(producerObserved, Qt::ISODateWithMs);
            if (!producerUtc.isValid()) producerUtc = QDateTime::fromString(producerObserved, Qt::ISODate);
            const QString ledgerAge = producerUtc.isValid()
                ? QStringLiteral("%1s").arg(std::max<qint64>(
                      0, producerUtc.toUTC().secsTo(QDateTime::currentDateTimeUtc())))
                : QString::fromUtf8("—");
            computeSummary.append(
                QStringLiteral("ACTIVE %1b · LAST COMPLETED %2 FERMAT/s · LEDGER %3 · AGE %4")
                    .arg(activeBits, completedFermat,
                         ledgerCampaign.isEmpty() ? QString::fromUtf8("—") : ledgerCampaign,
                         ledgerAge));
        }
        if (lane.engineId == QLatin1String("frontier-exact-cpu-verifier")) {
            computeSummary.append(QStringLiteral("EXACT VERIFIER · %1 THREADS · %2 · %3")
                .arg(lane.verifierThreads, verifierProgressText(lane),
                     verifierRateText(lane)));
        }
        if (!liveV11 && (lane.e2ePositionsPerSecond || lane.powmPerSecond ||
                        lane.efficiencyPositionsPerJoule)) {
            QStringList liveMetrics;
            if(lane.engineId==QLatin1String("pgn-native-walk"))liveMetrics.append(QStringLiteral("RAW SOURCE INTERVAL"));
            const auto displayedE2E=lane.e2ePositionsPerSecond;
            if (displayedE2E) liveMetrics.append(
                (property("pgn_network_ticket_frame").isValid()?QStringLiteral("COMPUTE %1"):QStringLiteral("E2E %1")).arg(compactComputeRate(displayedE2E,
                                                                 QStringLiteral("/s"))));
            if (lane.powmPerSecond) liveMetrics.append(
                QStringLiteral("TESTS %1").arg(compactComputeRate(lane.powmPerSecond,
                                                                  QStringLiteral("/s"))));
            if (lane.efficiencyPositionsPerJoule) liveMetrics.append(
                QStringLiteral("EFF %1").arg(compactComputeRate(lane.efficiencyPositionsPerJoule,
                                                                  QStringLiteral("/J"))));
            computeSummary.append((live?QStringLiteral("LIVE %1"):QStringLiteral("LAST %1 · SOURCE STALE"))
                .arg(liveMetrics.join(QStringLiteral(" "))));
        }
        if (staticProfile) {
            QStringList profileMetrics;
            if (!lane.staticComputeShift.isEmpty()) profileMetrics.append(
                QStringLiteral("S%1").arg(lane.staticComputeShift));
            if (lane.staticProfileE2ePositionsPerSecond) profileMetrics.append(
                QStringLiteral("E2E %1").arg(compactComputeRate(lane.staticProfileE2ePositionsPerSecond,
                                                                 QStringLiteral("/s"))));
            if (lane.staticProfilePowmPerSecond) profileMetrics.append(
                QStringLiteral("POWM %1").arg(compactComputeRate(lane.staticProfilePowmPerSecond,
                                                                  QStringLiteral("/s"))));
            if (lane.staticProfileEfficiencyPositionsPerJoule) profileMetrics.append(
                QStringLiteral("EFF %1").arg(compactComputeRate(
                    lane.staticProfileEfficiencyPositionsPerJoule, QStringLiteral("/J"))));
            computeSummary.append(QStringLiteral("XMP STATIC %1")
                                      .arg(profileMetrics.join(QStringLiteral(" "))));
        }
        if (!computeSummary.isEmpty()&&!compactPresentation) {
            const QString xmpSummary = computeSummary.join(QStringLiteral("  ·  "));
            const QRectF xmpSummaryRect(pod.left() + 12, pod.top() + 25,
                                         pod.width() - 24, 10);
            painter.setFont(Font(7, QFont::DemiBold, true));
            painter.setPen(Alpha(color, 180));
            painter.drawText(xmpSummaryRect, Qt::AlignLeft | Qt::AlignVCenter,
                             QFontMetrics(painter.font()).elidedText(
                                 xmpSummary, Qt::ElideRight,
                                 qFloor(xmpSummaryRect.width())));
            m_uiTooltipHits.append({xmpSummaryRect.adjusted(0, -2, 0, 2)
                                        .intersected(m_deviceStripRect),
                liveV11
                    ? QStringLiteral("ACTIVE is the current producer bit size. LAST COMPLETED FERMAT/s and LEDGER are the latest completed campaign measurement; they are not an instantaneous active-slice rate.")
                    : staticProfile
                    ? QStringLiteral("Live values come only from the active miner. XMP STATIC profile '%1', emitted shift %2: benchmark E2E/s, POWM/s and efficiency are not current throughput")
                          .arg(lane.staticComputeProfile.isEmpty() ? QStringLiteral("unnamed")
                                                                   : lane.staticComputeProfile,
                               lane.staticComputeShift.isEmpty() ? QStringLiteral("unavailable")
                                                                 : lane.staticComputeShift)
                    : QStringLiteral("Live miner telemetry; unavailable values are hidden")});
        }
        if(control.enabled&&deviceKind!="CPU")drawThrottleBadge(painter,
                          QRectF(pod.left() + 12, pod.top() + (pod.height()<130?24:38),
                                 pod.width() - 224, 11),
                          stableDeviceId);

        if(deviceKind=="CPU"){
            painter.setFont(Font(7,QFont::DemiBold,true));painter.setPen(Alpha(color,190));
            const auto state=lane.raw.value("pgn_cpu_state").toString("OFF");
            const QString caption=pgn::ui::cpuActivityCaption(lane.raw.value("pgn_cpu_status").toObject(),lane.raw.value("pgn_cpu_control").toBool(),property("pgn_cpu_audit_network_status").toString());
            const QRectF activity(pod.left()+12,pod.top()+38,pod.width()-24,11);
            painter.drawText(activity,Qt::AlignLeft|Qt::AlignVCenter,painter.fontMetrics().elidedText(caption,Qt::ElideRight,int(activity.width())));
            m_uiTooltipHits.append({activity.intersected(m_deviceStripRect),caption});
        }

        bool usedOk = false;
        bool totalOk = false;
        const double used = lane.gpuVramUsedBytes.toDouble(&usedOk);
        const double total = lane.gpuVramTotalBytes.toDouble(&totalOk);
        const std::optional<double> vram = usedOk && totalOk && total > 0.0
            ? std::optional<double>(used / total)
            : std::nullopt;
        const std::optional<double> rate = property("pgn_network_ticket_frame").isValid()?lane.e2ePositionsPerSecond:FrontierDisplayedRate(
            lane.e2eWindowPositionsPerSecond, lane.e2ePositionsPerSecond);
        struct Meter {
            std::optional<double> value;
            double scale;
            QString text;
            QString label;
            QString tooltip;
            QColor accent;
        };
        const auto compactGaugeRate = [](const std::optional<double>& value) {
            return pgn::ui::compactCardMetricCaption(value);
        };
        const QString unavailable = QString::fromUtf8("—");
        const std::optional<double> gapRate = lane.gapPairsWindowPerSecond
            ? lane.gapPairsWindowPerSecond
            : (lane.epochGapPairsPerSecond ? lane.epochGapPairsPerSecond
                                           : lane.gapPairsTestedPerSecond);
        std::optional<double> candidatesPerMillion;
        if (diamondLane) {
            const double scanned = lane.raw.value(
                QStringLiteral("diamond_cumulative_rows")).toDouble(0.0);
            bool candidatesOk = false;
            const qulonglong candidateCount = lane.cumulativeGapCandidates.toULongLong(
                &candidatesOk);
            if (scanned > 0.0 && candidatesOk) {
                candidatesPerMillion = static_cast<double>(candidateCount) * 1.0e6 / scanned;
            }
        }
        const auto peakScale = [](QHash<QString, double>& peaks,
                                  const QString& key,
                                  const std::optional<double>& current) {
            if (!current || !std::isfinite(*current) || *current <= 0.0) {
                return peaks.value(key, 0.0) * 1.15;
            }
            peaks[key] = std::max(peaks.value(key, 0.0), *current);
            return peaks.value(key) * 1.15;
        };
        const double positionScale = peakScale(m_devicePositionRatePeaks, controlId, rate);
        const double pairScale = peakScale(m_devicePairRatePeaks, controlId, gapRate);
        const QJsonValue legacyPpsValue = lane.raw.value(QStringLiteral("legacy_estimated_pps"));
        const std::optional<double> legacyPps = legacyPpsValue.isDouble() &&
            std::isfinite(legacyPpsValue.toDouble()) && legacyPpsValue.toDouble() >= 0.0
            ? std::optional<double>(legacyPpsValue.toDouble()) : std::nullopt;
        const double legacyPpsScale = peakScale(
            m_devicePositionRatePeaks, controlId + QStringLiteral("-pps"), legacyPps);
        const double prpScale = peakScale(
            m_devicePositionRatePeaks, controlId + QStringLiteral("-prp"), lane.powmPerSecond);
        QVector<Meter> meters{
            {lane.gpuSmUtilPercent, 100.0,
             lane.gpuSmUtilPercent ? QStringLiteral("%1%").arg(*lane.gpuSmUtilPercent, 0, 'f', 0)
                                   : unavailable,
             deviceKind == QLatin1String("CPU")
                 ? (m_legacyR4Mode ? QStringLiteral("HOST LOAD")
                                   : QStringLiteral("CPU"))
                 : QStringLiteral("GPU BUSY"),
             deviceKind == QLatin1String("CPU")
                 ? QStringLiteral("Instant physical CPU utilization")
                 : QStringLiteral("Instant physical NVML GPU-busy value; not SM occupancy"), color},
            {vram, 1.0, vram ? QStringLiteral("%1%").arg(*vram * 100.0, 0, 'f', 0)
                             : unavailable,
             deviceKind == QLatin1String("CPU") ? QStringLiteral("RAM")
                                                  : QStringLiteral("VRAM"),
             deviceKind == QLatin1String("CPU")
                 ? QStringLiteral("System RAM used over total physical RAM")
                 : QStringLiteral("Dedicated VRAM used over total physical VRAM"),
             QColor(QStringLiteral("#62cfff"))},
        };
        if (deviceKind == QLatin1String("GPU")) {
            const double powerScale = lane.gpuPowerLimitWatts.value_or(0.0);
            meters.append({lane.gpuPowerWatts, powerScale,
                           lane.gpuPowerWatts ? QString::number(*lane.gpuPowerWatts, 'f', 0)
                                              : unavailable,
                           QStringLiteral("W"),
                           lane.gpuPowerLimitWatts
                               ? QStringLiteral("Measured board power; arc normalized to emitted NVML power limit %1 W")
                                     .arg(*lane.gpuPowerLimitWatts, 0, 'f', 0)
                               : QStringLiteral("Measured board power; NVML power limit unavailable, so the arc is neutral"),
                           lane.gpuPowerLimitWatts ? QColor(QStringLiteral("#ffd166"))
                                                   : QColor(QStringLiteral("#718b8d"))});
            const QColor temperatureAccent = !lane.gpuTemperatureC
                ? QColor(QStringLiteral("#718b8d"))
                : (*lane.gpuTemperatureC >= 85.0
                       ? QColor(QStringLiteral("#ff5d67"))
                       : (*lane.gpuTemperatureC >= 75.0
                              ? QColor(QStringLiteral("#ffad4a"))
                              : QColor(QStringLiteral("#69efc5"))));
            meters.append({lane.gpuTemperatureC, 100.0,
                           lane.gpuTemperatureC ? QString::number(*lane.gpuTemperatureC, 'f', 0)
                                                : unavailable,
                           QStringLiteral("°C"),
                           QStringLiteral("Physical device temperature on a fixed 0–100 °C scale; orange from 75 °C and red from 85 °C"),
                           temperatureAccent});
        }
        if (m_legacyR4Mode && !diamondLane) {
            meters.append({property("pgn_network_ticket_frame").isValid()?lane.e2ePositionsPerSecond:legacyPps, property("pgn_network_ticket_frame").isValid()?positionScale:legacyPpsScale, compactGaugeRate(property("pgn_network_ticket_frame").isValid()?lane.e2ePositionsPerSecond:legacyPps),
                           QStringLiteral("pos/s"),
                           QString("%1: cumulative position delta / actual source span. Raw adjacent: %2 positions/s over %3ns. Not certified coverage.").arg(QStringLiteral("RAW SOURCE INTERVAL"),pgnRate(lane.e2ePositionsPerSecond),lane.raw.value("pgn_source_interval_ns").toString()),
                           QColor(QStringLiteral("#b58cff"))});
            meters.append({lane.powmPerSecond, prpScale,
                           compactGaugeRate(lane.powmPerSecond), QStringLiteral("tests/s"),
                           QString("%1: primality test delta / actual source span. Raw adjacent: %2 tests/s over %3ns.").arg(QStringLiteral("RAW SOURCE INTERVAL"),pgnRate(lane.powmPerSecond),lane.raw.value("pgn_source_interval_ns").toString()),
                           QColor(QStringLiteral("#46c8ee"))});
        } else {
            meters.append({rate, positionScale, compactGaugeRate(rate),
                           diamondLane ? QStringLiteral("CRST rows/s")
                                       : QStringLiteral("pos/s"),
                           diamondLane
                               ? QStringLiteral("Complete CRST geometry rows processed per second; this physical unit is never relabelled as Gapcoin PPS")
                               : QStringLiteral("E2E sliding-window positions per second from data.rows deltas"),
                           QColor(QStringLiteral("#b58cff"))});
        }
        if (diamondLane) {
            const double yieldScale = peakScale(
                m_devicePairRatePeaks, controlId + QStringLiteral("-cand-per-m"),
                candidatesPerMillion);
            meters.append({candidatesPerMillion, yieldScale,
                           candidatesPerMillion
                               ? QString::number(*candidatesPerMillion, 'f', 3)
                               : unavailable,
                           QStringLiteral("CAND/M"),
                           QStringLiteral("Observed probable candidates per million complete CRST rows in this Diamond run; measured yield, not an estimated probability"),
                           QColor(QStringLiteral("#ffad4a"))});
        } else {
            const QJsonValue processedValue=lane.raw.value(QStringLiteral("pgn_processed_gaps_per_second"));
            const std::optional<double> processed=processedValue.isDouble() && std::isfinite(processedValue.toDouble()) && processedValue.toDouble()>=0
                ? std::optional<double>(processedValue.toDouble()) : std::nullopt;
            const auto shownProcessed = processed;
            if(shownProcessed)meters.append({shownProcessed,peakScale(m_devicePairRatePeaks,controlId+QStringLiteral("-processed"),shownProcessed),
                           compactGaugeRate(shownProcessed),QStringLiteral("gaps/s"),
                           QStringLiteral("Processed gaps/s: owned gaps above the GPU minimum-gap gate; uses a counter-delta mean over up to 60 seconds of active GPU time. Not all short prime gaps; not the count of primality tests."),
                           QColor(QStringLiteral("#69efc5"))});
            const auto rawYield=lane.raw.value("pgn_retained_gaps_per_billion_positions");
            const std::optional<double> yield=(rawYield.isDouble()?std::optional<double>(rawYield.toDouble()):std::nullopt);
            meters.append({yield,peakScale(m_devicePairRatePeaks,controlId+"-yield",yield),pgn::ui::fixedCardMetricCaption(yield),QStringLiteral("gaps/Gpos"),
                QStringLiteral("Retained output per billion input positions over the same active-time counter window, up to 60 seconds. Undefined at zero input. Compare equal bit length/threshold/verification settings only; not independently certified gaps."),QColor("#b58cff")});
        }
        const bool keepLastPgnSample = lane.raw.value(QStringLiteral("pgn_process_running")).toBool(false);
        if(deviceKind=="CPU"&&lane.raw.value("pgn_cpu_inventory").isObject()){const auto cpu=pgn::ui::cpuCardMetrics(lane.raw.value("pgn_cpu_status").toObject(),lane.raw.value("pgn_cpu_control").toBool(),true,color);meters.clear();for(const auto& metric:cpu)meters.append({metric.value,metric.scale,metric.text,metric.label,metric.tooltip,metric.color});auto states=property("pgn_cpu_card_metrics").toJsonObject();states.insert(stableDeviceId,pgn::ui::cpuMetricSnapshot(cpu));setProperty("pgn_cpu_card_metrics",states);}
        if (!(deviceKind=="CPU"&&lane.raw.value("pgn_cpu_control").toBool())&&!active && !keepLastPgnSample) for(auto& meter:meters){
            if(meter.label==QLatin1String("pos/s")||meter.label==QLatin1String("tests/s")||meter.label==QLatin1String("gaps/s")||meter.label==QLatin1String("gaps/Gpos")){
                meter.value.reset();meter.text=QStringLiteral("—");
            }
        }
        const auto bands=pgn::ui::deviceCardBands(pod,compactPresentation?1:2,compactPresentation&&!pgn::ui::hasCpuThreadBudget(deviceKind,lane.raw)?0:2,compactPresentation?18.:20.);
        for (int i = 0; i < meters.size(); ++i) {
            const Meter& meter = meters.at(i);
            const FrontierGaugeGeometry gauge = FrontierGaugeGeometryForCard(
                pod.left(), pod.width(), i, meters.size(), bands.centerY,bands.radius);
            const QPointF center(gauge.centerX,bands.centerY);
            drawGauge(painter, center, gauge.radius,
                      (property("pgn_compact_mining").toBool()||deviceKind=="CPU")&&(meter.label.compare("pos/s",Qt::CaseInsensitive)==0||meter.label=="tests/s"||meter.label=="gaps/s"||meter.label=="gaps/Gpos")?rateGaugeFraction(lane,stableDeviceId,meter.label,meter.value):meter.value&&meter.scale>0?*meter.value/meter.scale:0.,
                      meter.accent, meter.text, meter.label, gauge.meterWidth - 4.0);
            const bool filteredGap = meter.label==QLatin1String("gaps/s")||meter.label==QLatin1String("gaps/Gpos");
            QString metricState = (!active && keepLastPgnSample && i >= 4)
                ? (filteredGap ? QStringLiteral("STALE · LAST DISPLAY SAMPLE · see AGE; not current throughput")
                               : QStringLiteral("STALE · LAST SOURCE SAMPLE · see AGE; not current throughput"))
                : QStringLiteral("LIVE");
            if (!meter.value) {
                if (lane.stale) metricState = QStringLiteral("STALE");
                else if (!lane.error.isEmpty()) metricState = QStringLiteral("ERROR: %1").arg(lane.error);
                else if (!lane.authoritative) metricState = QStringLiteral("WAITING");
                else if (i >= 4) metricState = QStringLiteral("AWAITING DATA");
                else metricState = QStringLiteral("UNAVAILABLE");
            }
            m_uiTooltipHits.append({QRectF(gauge.labelLeft,
                                            gauge.ringTop,
                                            gauge.labelRight - gauge.labelLeft,
                                            gauge.labelBottom - gauge.ringTop).intersected(m_deviceStripRect),
                                    QStringLiteral("%1 · %2%3").arg(meter.tooltip, metricState,
                                        filteredGap ? QStringLiteral(". Display only: gap counters use the same rolling active-computation window, up to 60 seconds. Counts, history, source age, pos/s and tests/s untouched.") : QString{})});
        }
        painter.setFont(Font(7, QFont::DemiBold, true));
        painter.setPen(Alpha(color, 155));
        const auto measuredAge = sampleAgeMs == std::numeric_limits<qint64>::max()
            ? std::optional<qint64>{} : std::optional<qint64>{sampleAgeMs};
        const QString sampleAge = pgn::ui::telemetryAgeCaption(measuredAge, nativePgn);
        std::optional<double> observedCadence = m_resourceTelemetry.samplerObservedIntervalMs;
        if (!observedCadence && history.size() >= 2 &&
            history.at(history.size() - 2).observedUtc.isValid() &&
            history.constLast().observedUtc.isValid()) {
            observedCadence = history.at(history.size() - 2).observedUtc.msecsTo(
                history.constLast().observedUtc);
        }
        const QString sourceInterval = observedCadence
            ? QStringLiteral("%1ms").arg(*observedCadence, 0, 'f', 0)
            : QString::fromUtf8("—");
        const QString relayCost = m_resourceTelemetry.samplerSelfCpuPercentHost
            ? QStringLiteral(" · CPU %1%").arg(*m_resourceTelemetry.samplerSelfCpuPercentHost,
                                                0, 'f', 1)
            : QString{};
        QString clockCaption=QStringLiteral("CORE — · MEM — MHz");
        if(compactPresentation&&resourceSampleFresh())for(const auto& resource:m_resourceTelemetry.devices)if(resource.deviceId.compare(stableDeviceId,Qt::CaseInsensitive)==0){
            clockCaption=QString("CORE %1 · MEM %2 MHz").arg(resource.smClockMhz?QString::number(*resource.smClockMhz,'f',0):QStringLiteral("—"),resource.memoryClockMhz?QString::number(*resource.memoryClockMhz,'f',0):QStringLiteral("—"));break;
        }
        if(deviceKind!="CPU"){
        painter.drawText(QRectF(pod.left() + 12, pod.top() + (compactPresentation?96:126), pod.width() - 220, 12),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         compactPresentation?clockCaption:(nativePgn?QStringLiteral("SRC %1 · GPU OBS %2%3"):QStringLiteral("AGE %1 · OBS %2%3"))
                             .arg(sampleAge, sourceInterval, relayCost));
        if(nativePgn)m_uiTooltipHits.append({QRectF(pod.left()+12,pod.top()+116,pod.width()-220,12),QString("Source age is a monotonic lower bound: producer clock lag + local elapsed since receipt; network transit is unmeasured. Original source UTC retained: %1. GPU OBS is the separate measured driver-sample interval, not the position-counter interval. No UTC-skew clamping, averaging or interpolation.").arg(lane.observedUtc.toUTC().toString(Qt::ISODateWithMs))});
        else if(measuredAge&&*measuredAge<0)m_uiTooltipHits.append({QRectF(pod.left()+12,pod.top()+126,pod.width()-220,12),QStringLiteral("The producer timestamp is ahead of this PC. Source age cannot be inferred from these clocks; original timestamps remain unchanged in diagnostics.")});
        const QRectF budgetLabel(pod.right() - 205, pod.top() + (compactPresentation?96:126), 193, 12);
        painter.setFont(Font(7, QFont::DemiBold, true));
        painter.setPen(controlColor);
        painter.drawText(
            budgetLabel, Qt::AlignCenter,
            m_legacyR4Mode && lane.raw.value(QStringLiteral("lab_highshift")).toBool(false)
                ? QStringLiteral("EXTERNAL CRT TEST · READ ONLY")
            : m_legacyR4Mode
                ? QStringLiteral("PROCESS CONTROL · %1")
                      .arg(!control.known?QStringLiteral("ENGINE WAIT"):
                           control.enabled?QStringLiteral("ENGINE ON"):QStringLiteral("ENGINE OFF"))
            : !controlConnected
                ? (deviceKind == QLatin1String("CPU")
                       ? QStringLiteral("NO CPU CONTROL SIDECAR")
                       : QStringLiteral("NO GPU CONTROL SIDECAR"))
                : QStringLiteral("%1 %2 %3% · %4")
                      .arg(deviceKind == QLatin1String("CPU") ? QStringLiteral("THREADS")
                                                               : QStringLiteral("POWER/WORK"))
                      .arg(control.status == FrontierDeviceControlStatus::Applied
                               ? QStringLiteral("BUDGET") : QStringLiteral("SIDECAR REQUEST"))
                      .arg(control.budgetPercent)
                      .arg(controlSuffix));
        const QRectF slider(pod.right() - 202, pod.top() + (nativePgn?130:118), 187, 7);
        const QRectF visibleToggle = statePill.intersected(m_deviceStripRect);
        QRectF visibleSlider;
        if (budgetControlConnected) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(35, 71, 78, 220));
            painter.drawRoundedRect(slider, 4, 4);
            const qreal filled = slider.width() * control.budgetPercent / 100.0;
            painter.setBrush(Alpha(controlColor, 210));
            painter.drawRoundedRect(QRectF(slider.left(), slider.top(), filled, slider.height()), 4, 4);
            painter.setBrush(controlColor);
            painter.drawEllipse(QPointF(slider.left() + filled, slider.center().y()), 4.5, 4.5);
            visibleSlider = slider.adjusted(-4, -5, 4, 5).intersected(m_deviceStripRect);
        }
        if (controlConnected && (!visibleToggle.isEmpty() || !visibleSlider.isEmpty())) {
            m_deviceControlHits.append({controlId, deviceKind, visibleToggle, visibleSlider,
                                        true});
        }
        drawSparkline(painter, QRectF(pod.left() + 12, pod.bottom() - 13, pod.width() - 24, 8),
                      history, color, false);
        }else{
            if(controlConnected)m_deviceControlHits.append({controlId,deviceKind,statePill.intersected(m_deviceStripRect),{},true});
            drawCpuThreadBudget(painter,pod,lane,stableDeviceId,color,controlConnected);
        }
        painter.restore();
    }

    bool resourceSampleFresh() const {
        const auto age=pgn::mining::resourceReceiptAgeMs(m_resourceTelemetry);
        return m_hasResourceTelemetry&&m_resourceTelemetry.valid&&!m_resourceTelemetry.stale&&
            age&&*age<=15000;
    }
        struct LaneCard {
            FrontierTelemetrySnapshot snapshot;
            QVector<LaneSample> history;
            QString label;
            QColor color;
            QString kind;
            QString stableId;
        };
    QVector<LaneCard> miningLaneCards() const {
        QVector<LaneCard> cards;
        if (m_hasResourceTelemetry) {
            int cpuIndex = 0;
            int gpuIndex = 0;
            for (const FrontierResourceDevice& device : m_resourceTelemetry.devices) {
                FrontierTelemetrySnapshot lane;
                const bool cpuDevice = device.kind.compare(QStringLiteral("CPU"),
                                                            Qt::CaseInsensitive) == 0;
                if (cpuDevice) continue; // GPU-only PGN production stage.
                const bool primaryMatch = m_hasTelemetry &&
                    device.deviceId.compare(m_telemetry.gpuUuid, Qt::CaseInsensitive) == 0;
                const bool secondaryMatch = m_hasSecondaryTelemetry &&
                    device.deviceId.compare(m_secondaryTelemetry.gpuUuid,
                                            Qt::CaseInsensitive) == 0;
                const FrontierTelemetrySnapshot* additionalMatch = nullptr;
                for (auto iterator = m_additionalTelemetry.cbegin();
                     iterator != m_additionalTelemetry.cend(); ++iterator) {
                    if (device.deviceId.compare(iterator->gpuUuid,
                                                Qt::CaseInsensitive) == 0) {
                        additionalMatch = &iterator.value();
                        break;
                    }
                }
                if (primaryMatch) lane = m_telemetry;
                else if (secondaryMatch) lane = m_secondaryTelemetry;
                else if (additionalMatch) lane = *additionalMatch;
                const bool matchedMinerLive = (primaryMatch || secondaryMatch ||
                                               additionalMatch != nullptr) &&
                    minerLaneLive(lane);
                // Hardware polling must not renew stale miner telemetry.
                if (!(primaryMatch || secondaryMatch || additionalMatch)) {
                    lane.authoritative = resourceSampleFresh();
                    lane.stale = !resourceSampleFresh();
                    lane.observedUtc = m_resourceTelemetry.observedUtc;
                }
                lane.gpuUuid = device.deviceId;
                lane.gpuName = device.name;
                lane.gpuSmUtilPercent = device.utilizationPercent;
                lane.gpuMemoryUtilPercent = device.memoryUtilizationPercent;
                lane.gpuVramUsedBytes = device.memoryUsedBytes;
                lane.gpuVramTotalBytes = device.memoryTotalBytes;
                lane.gpuPowerWatts = device.powerWatts;
                lane.gpuPowerLimitWatts = device.powerLimitWatts;
                lane.gpuTemperatureC = device.temperatureC;
                // Physical process acknowledgement and metric freshness are
                // independent. A delayed counter must never turn ON into OFF.
                lane.raw.insert(QStringLiteral("legacy_miner_bound"),
                    lane.raw.value(QStringLiteral("pgn_process_running")).toBool(matchedMinerLive));
                if ((primaryMatch || secondaryMatch || additionalMatch) && !matchedMinerLive &&
                    !lane.raw.value(QStringLiteral("pgn_process_running")).toBool(false)) {
                    lane.rateRowsPerSecond.reset();
                    lane.kernelPositionsPerSecond.reset();
                    lane.e2ePositionsPerSecond.reset();
                    lane.e2eWindowPositionsPerSecond.reset();
                    lane.powmPerSecond.reset();
                    lane.gapPairsTestedPerSecond.reset();
                    lane.gapPairsWindowPerSecond.reset();
                    lane.kernelDutyFraction.reset();
                    lane.efficiencyPositionsPerJoule.reset();
                }
                const bool resourceComputeProfile = device.e2ePositionsPerSecond ||
                    device.powmPerSecond || device.efficiencyPositionsPerJoule;
                if (resourceComputeProfile) {
                    // Static XMP profile data remains in a separate namespace;
                    // it never replaces the live miner's E2E/rate fields.
                    lane.staticComputeProfile = device.computeProfile;
                    lane.staticComputeShift = device.computeShift;
                    lane.staticProfileE2ePositionsPerSecond = device.e2ePositionsPerSecond;
                    lane.staticProfilePowmPerSecond = device.powmPerSecond;
                    lane.staticProfileEfficiencyPositionsPerJoule =
                        device.efficiencyPositionsPerJoule;
                }
                const bool physicallyActive = device.utilizationPercent.value_or(0.0) >= 2.0;
                lane.state = (matchedMinerLive || physicallyActive
                           ? QStringLiteral("running") : QStringLiteral("idle"));
                lane.producerStatus = matchedMinerLive
                    ? QStringLiteral("running") : QStringLiteral("idle");
                const bool cpu = cpuDevice;
                const int index = cpu ? cpuIndex++ : gpuIndex++;
                const QColor accent = deviceIdentityColor(device.deviceId, device.kind);
                cards.append({lane,
                              m_resourceHistories.value(device.deviceId),
                               cpu && m_legacyR4Mode
                                   ? QStringLiteral("HOST CPU %1").arg(index)
                                   : QStringLiteral("%1 %2").arg(device.kind).arg(index),
                               accent,
                               device.kind,
                               device.deviceId});
            }
        } else {
            if (m_hasTelemetry) {
                const QString identity = m_telemetry.gpuUuid.isEmpty()
                    ? m_telemetry.runId : m_telemetry.gpuUuid;
                cards.append({m_telemetry, m_primaryHistory,
                              QStringLiteral("GPU 0"),
                              deviceIdentityColor(identity, QStringLiteral("GPU")),
                              QStringLiteral("GPU"), identity});
            }
            if (m_hasSecondaryTelemetry) {
                const QString identity = m_secondaryTelemetry.gpuUuid.isEmpty()
                    ? m_secondaryTelemetry.runId : m_secondaryTelemetry.gpuUuid;
                cards.append({m_secondaryTelemetry, m_secondaryHistory,
                              QStringLiteral("GPU 1"),
                              deviceIdentityColor(identity, QStringLiteral("GPU")),
                              QStringLiteral("GPU"), identity});
            }
        }
        // CPU hardware identities are retained even while their audit backend is OFF.
        // They use the same LaneCard/painter and never inherit another device's rates.
        for (auto it = m_additionalTelemetry.cbegin(); it != m_additionalTelemetry.cend(); ++it) {
            if (!it->raw.value("pgn_cpu_inventory").isObject()) continue;
            bool present = false; for (const auto& card : cards) present |= card.stableId == it.key();
            if (!present) cards.append({it.value(), {}, it->raw.value("pgn_cpu_host").toString(),
                deviceIdentityColor(it.key(), "CPU"), "CPU", it.key()});
        }
        return cards;
    }

    std::optional<double> displayedGapMetric(const QString& key, std::optional<double> raw,
                                             const QString& clockIdentity, const QString& sourceTimeNs) {
        bool timeOk = false;
        const auto sourceTime = sourceTimeNs.toULongLong(&timeOk);
        return m_gapDisplayFilters[key].observe(raw, clockIdentity.toStdString(),
            timeOk ? std::optional<std::uint64_t>(sourceTime) : std::nullopt);
    }
    std::optional<double> displayedGapMetric(const FrontierTelemetrySnapshot& lane,
                                             const QString& key, const QString& metric,
                                             std::optional<double> raw) {
        const QString clock = lane.raw.value("pgn_clock_namespace").toString();
        const QString identity = clock.isEmpty() ? QString{} :
            clock + QChar(u'\0') + lane.engineId + QChar(u'\0') + lane.runId;
        return displayedGapMetric("device:" + key + ":" + metric, raw, identity,
                                  lane.raw.value("pgn_source_time_ns").toString());
    }
    pgn::ui::MiningSummary pgnSummary() const {return pgn::ui::summarizeMining(minerLanes(),m_pgnFinishedRuns);}
    static QString pgnRate(const std::optional<double>& value){
        if(!value)return QStringLiteral("—");
        if(*value>=1e9)return QString::number(*value/1e9,'f',2)+"G";
        if(*value>=1e6)return QString::number(*value/1e6,'f',2)+"M";
        if(*value>=1e3)return QString::number(*value/1e3,'f',1)+"K";
        return QString::number(*value,'f',1);
    }
    QString pgnKnownPositions(const pgn::ui::MiningSummary& sum) const {
        return pgn::booking::addUnsigned(m_pgnBookingModel->completedPositions(),sum.positions);
    }
    void drawPgnTotalCard(QPainter& painter, const QRectF& pod) {
        const auto sum = property("pgn_network_ticket_frame").isValid()?pgn::ui::summarizeLiveCardRates(minerLanes(),m_pgnFinishedRuns):m_pgnTotalRateWindow.observe(minerLanes(),m_pgnFinishedRuns);
        const QColor accent = HorizonDeviceVisuals::totalCardAccent();
        painter.save();
        painter.setBrush(QColor(3, 28, 34));
        painter.setPen(QPen(accent, 2));
        painter.drawRoundedRect(pod.adjusted(1, 1, -1, -1), 14, 14);
        const bool expanded=!m_collapsedDevices.value("@total",true);const QRectF expand(pod.right()-27,pod.top()+7,17,17);painter.setBrush(QColor(4,24,31));painter.setPen(QPen(accent,1));painter.drawRoundedRect(expand,7,7);painter.setFont(Font(9,QFont::DemiBold,true));painter.drawText(expand,Qt::AlignCenter,expanded?"−":"+");m_deviceCollapseHits.append({"@total",expand.adjusted(-5,-5,5,5)});setProperty("pgn_total_expanded",expanded);setProperty("pgn_total_expand_rectangle",expand);
        const auto cards=miningLaneCards();
        for(int i=0;i<2;++i){
            const QString kind=i?"CPU":"GPU";int count=0,on=0,pending=0,unknown=0;
            for(const auto& card:cards)if(card.kind==kind&&pgn::ui::miningSourceManaged(this,card.stableId,card.snapshot.raw.value("pgn_cpu_control").toBool())){
                ++count;const auto control=m_deviceControls.value(card.stableId);
                on+=control.known&&control.enabled;unknown+=!control.known;
                pending+=control.status==FrontierDeviceControlStatus::Pending;
            }
            const QRectF button(pod.right()-175+i*71,pod.top()+7,66,17);const QByteArray prefix=("pgn_total_"+kind.toLower()).toLatin1();
            setProperty((prefix+"_toggle_rect").constData(),button);
            setProperty((prefix+"_state").constData(),!count?"unavailable":pending?"pending":unknown?"unknown":on==count?"on":on?"mixed":"off");
            const auto color=!count||unknown?QColor("#657f85"):pending?QColor("#ffbd69"):on==count?QColor("#69efc5"):accent;
            painter.setBrush(Alpha(color,28));painter.setPen(QPen(Alpha(color,180),1));painter.drawRoundedRect(button,7,7);painter.setFont(Font(7,QFont::DemiBold,true));
            painter.drawText(button,Qt::AlignCenter,kind+(!count?" —":pending?" …":unknown?" WAIT":on==count?" ON":on?" MIX":" OFF"));
            if(count&&m_deviceControlHandler)m_totalGroupHits.append({kind,button});
            m_uiTooltipHits.append({button,QString("%1 registered %2 device(s). Click to start all, or stop all when all are ON. Status waits for each backend acknowledgement.").arg(count).arg(kind)});
        }
        painter.setFont(Font(10,QFont::DemiBold,true));painter.setPen(accent);const bool compactCard=property("pgn_compact_mining").toBool();painter.drawText(pod.adjusted(12,7,-182,-7),Qt::AlignTop|Qt::AlignLeft,QString("TOTAL · %1").arg(sum.running));
        painter.setFont(Font(8, QFont::Normal, true));
        if(expanded||!compactCard)painter.drawText(QPointF(pod.left() + 12, pod.top() + 36),
                         compactCard&&!sum.running?QStringLiteral("IDLE · no active calculation"):QString("%1 active · %2").arg(sum.running).arg(sum.rateCaption));
        const auto gapRate = sum.processedRate;
        const auto gapYield = sum.yield;
        const QVector<std::optional<double>> values{sum.positionsRate, sum.testsRate, gapRate, gapYield};
        const QStringList labels= sum.searchRatesOnly?QStringList{"pos/s","tests/s*","gaps/s*","gaps/Gpos*"}:QStringList{"pos/s", "tests/s", "gaps/s", "gaps/Gpos"};
        const auto bands=pgn::ui::deviceCardBands(pod,expanded?1:0,2,expanded?19.:18.);
        for (int i = 0; i < values.size(); ++i) {
            const bool compact=!expanded;
            const auto geometry = FrontierGaugeGeometryForCard(pod.left(), pod.width(), i, values.size(), bands.centerY,bands.radius);
            const QString caption = i == 3 && values[i]
                ? pgn::ui::fixedCardMetricCaption(values[i]) : pgn::ui::compactCardMetricCaption(values[i]);
            drawGauge(painter, QPointF(geometry.centerX,bands.centerY), geometry.radius,
                      rateGaugeFraction("@total:"+labels[i],sum.displayClockIdentity,sum.sourceTimeNs,values[i]),
                      accent, caption, labels[i], geometry.meterWidth - 4.0, accent);
        }
        painter.setFont(Font(7,QFont::Normal,true));painter.setPen(accent);
        if(expanded){std::optional<double> watts,temp;double gpuRate=0;bool hasRate=false;if(resourceSampleFresh())for(const auto& device:m_resourceTelemetry.devices)if(device.kind=="GPU"){if(device.powerWatts)watts=watts.value_or(0)+*device.powerWatts;if(device.temperatureC)temp=std::max(temp.value_or(0),*device.temperatureC);}for(const auto& card:cards)if(card.kind=="GPU"&&minerLaneLive(card.snapshot)&&card.snapshot.e2ePositionsPerSecond){gpuRate+=*card.snapshot.e2ePositionsPerSecond;hasRate=true;}const auto efficiency=watts&&*watts>0&&hasRate?std::optional<double>(gpuRate/ *watts/1e9):std::nullopt;painter.drawText(QRectF(pod.left()+12,pod.top()+126,pod.width()-24,12),Qt::AlignLeft,QString("GPU power %1 W · Max %2 °C").arg(watts?QString::number(*watts,'f',0):QString("—"),temp?QString::number(*temp,'f',0):QString("—")));painter.drawText(QRectF(pod.left()+12,pod.top()+140,pod.width()-24,12),Qt::AlignLeft,QString("GPU efficiency %1 Gpos/J").arg(efficiency?QString::number(*efficiency,'f',2):QString("—")));setProperty("pgn_total_gpu_watts",watts?QVariant(*watts):QVariant{});setProperty("pgn_total_gpu_efficiency",efficiency?QVariant(*efficiency):QVariant{});}
        if(!expanded)painter.drawText(QPointF(pod.left() + 12, pod.bottom() - 16), property("pgn_network_ticket_frame").isValid()?QString("Ticket · ")+pgn::ui::cardMetricText(pgn::ui::positionFormats(property("pgn_network_ticket_frame").toJsonObject().value("positions").toString()).human)+" · "+pgn::ui::cardMetricText(pgn::ui::positionFormats(property("pgn_network_ticket_frame").toJsonObject().value("positions").toString()).decimal):compactCard?QStringLiteral("Current device calculation rates"):"KNOWN POSITIONS · " + pgnKnownPositions(sum));
        const auto count = m_nativeAllCatalog && m_nativeAllCatalog->ready()
            ? QString::number(m_nativeAllCatalog->lengthBounds().points) : QString("—");
        if(!expanded)painter.drawText(QPointF(pod.left() + 12, pod.bottom() - 4), property("pgn_network_ticket_frame").isValid()?(expanded?QString("Network audits separate"):pgn::ui::cardMetricText(pgn::ui::positionFormats(property("pgn_network_ticket_frame").toJsonObject().value("positions").toString()).binary)+" positions"):compactCard?QStringLiteral("Ticket results below"):"ARCHIVED SOURCE GAPS · " + count);
        m_uiTooltipHits.append({pod,sum.displayRateEstimate?QString("Total positions/s sums fresh search and CPU audit device rates, estimated over their own measurement windows. Search tests/s, gaps/s and gaps/Gpos describe search output only; CPU audits do not report these counters. Gap yield uses search positions only. Missing/stale readings remain unavailable; no absent CPU counter is treated as zero. Exact source-clock totals and original device observations are preserved."):QString("TOTAL integrates cumulative position, test and gap deltas over the displayed source-clock window, up to 60 seconds. Device observations, exact counts and source age are unchanged. Missing data never becomes zero.")});
        setProperty("pgn_total_card_first", true);
        setProperty("pgn_display_rate_caption",sum.rateCaption);
        setProperty("pgn_display_rate_span_ns",sum.intervalNs);
        setProperty("pgn_display_positions_per_second",sum.positionsRate?QVariant(*sum.positionsRate):QVariant{});
        setProperty("pgn_display_tests_per_second",sum.testsRate?QVariant(*sum.testsRate):QVariant{});
        setProperty("pgn_display_gaps_per_second",gapRate?QVariant(*gapRate):QVariant{});
        setProperty("pgn_total_only_mean",!sum.displayRateEstimate);
        setProperty("pgn_total_search_metrics_only",sum.searchRatesOnly);
        setProperty("pgn_total_positions", pgnKnownPositions(sum));
        setProperty("pgn_total_card_accent", accent.name(QColor::HexRgb));
        setProperty("pgn_total_card_gauges", labels);
        painter.restore();
    }
    void drawPgnCentral(QPainter& painter){
        m_pgnSummaryPages.clear();const auto all=minerLanes();QVector<const FrontierTelemetrySnapshot*> active;
        for(const auto* lane:all)if(lane->raw.value("pgn_process_running").toBool())active.append(lane);
        const FrontierTelemetrySnapshot* selected=nullptr;for(const auto* lane:active)if(lane->gpuUuid==m_pgnSummaryPage)selected=lane;
        if(!selected)m_pgnSummaryPage.clear();
        const auto sum=selected?pgn::ui::summarizeMining({selected}):pgnSummary();
        const auto area=m_pgnCentralArea;const QColor accent=selected?deviceIdentityColor(selected->gpuUuid,"GPU"):QColor("#69efc5");
        painter.save();painter.setBrush(QColor(2,20,28,248));painter.setPen(QPen(QColor("#2a6972"),1));painter.drawRoundedRect(area,12,12);
        auto page=[&](const QString& id,const QString& label,qreal x,qreal w){QRectF box(x,area.top()+7,w,24);pgn::ui::drawRangeTile(painter,box,id.isEmpty()?QColor("#69efc5"):deviceIdentityColor(id,"GPU"),label,{}, {},id==m_pgnSummaryPage);m_pgnSummaryPages.append({box,id});};
        page({},"TOTAL",area.left()+10,90);qreal x=area.left()+108;
        for(const auto* lane:active){const auto w=std::clamp(qreal(lane->gpuName.size()*6+24),qreal(110),qreal(215));if(x+w>area.right()-10)break;
            page(lane->gpuUuid,lane->gpuName,x,w);x+=w+7;}
        const qreal bodyTop=area.top()+38,cellHeight=std::max(qreal(23),(area.height()-58)/3);
        const auto archived=m_nativeAllCatalog&&m_nativeAllCatalog->ready()?QString::number(m_nativeAllCatalog->lengthBounds().points):QString("—");
        const QString retained=selected?sum.retained:archived;
        const auto& current=m_pgnBookingModel->runtime();
        const auto identity=pgn::booking::identifyRange(m_pgnBookingModel->source(),current.start,current.end);
        std::optional<pgn::ui::BlockEtaEstimator::Estimate> eta;
        qulonglong currentLength=0;
        const FrontierTelemetrySnapshot* currentLane=nullptr;
        for(const auto* lane:all)if(lane->runId==current.run){currentLane=lane;break;}
        if(current.running&&currentLane&&minerLaneLive(*currentLane)){
            const auto age=pgn::mining::sourceAgeLowerBoundNs(currentLane->raw);
            bool clockOk=false,positionOk=false,lengthOk=false;
            const auto clock=currentLane->raw.value("pgn_source_time_ns").toString().toULongLong(&clockOk);
            const auto position=currentLane->rowsCompleted.toULongLong(&positionOk);
            qulonglong length=0;
            try{length=pgn::booking::subtractUnsigned(current.end,current.start).toULongLong(&lengthOk);}catch(const std::invalid_argument&){}
            currentLength=length;
            if(age&&*age<=5'000'000'000LL&&clockOk&&positionOk&&lengthOk)
                eta=m_blockEta.observe((currentLane->gpuUuid+":"+current.run).toStdString(),clock,position,length);
            else m_blockEta.reset();
        }else m_blockEta.reset();
        QString etaValue=current.running?QStringLiteral("AWAITING FRESH WINDOW"):QStringLiteral("PAUSED");
        if(eta){
            const auto seconds=static_cast<qint64>(std::ceil(eta->seconds));
            const auto duration=seconds>=3600?QString("%1h %2m").arg(seconds/3600).arg((seconds/60)%60)
                :QString("%1m %2s").arg(seconds/60).arg(seconds%60);
            etaValue=QString("~%1 · %2").arg(duration,QDateTime::currentDateTime().addSecs(seconds).toString("HH:mm"));
        }
        const auto formatDuration=[](double value){const auto seconds=qint64(std::ceil(std::max(0.,value)));
            return seconds>=3600?QString("%1h %2m %3s").arg(seconds/3600).arg((seconds/60)%60).arg(seconds%60)
                :QString("%1m %2s").arg(seconds/60).arg(seconds%60);};
        const auto born=currentLane?QDateTime::fromString(currentLane->raw.value("pgn_run_started_utc").toString(),Qt::ISODateWithMs):QDateTime{};
        const auto elapsed=born.isValid()&&currentLane&&current.running
            ?std::optional<double>(std::max(qint64(0),born.secsTo(QDateTime::currentDateTimeUtc()))):std::nullopt;
        const auto blockElapsed=current.blockStartedUtc.isValid()
            ?std::optional<double>(std::max(qint64(0),current.blockStartedUtc.secsTo(current.running?QDateTime::currentDateTimeUtc():current.observedUtc))):std::nullopt;
        setProperty("pgn_block_elapsed_seconds",blockElapsed?QVariant(*blockElapsed):QVariant{});
        setProperty("pgn_block_started_utc",current.blockStartedUtc.isValid()?current.blockStartedUtc.toUTC().toString(Qt::ISODateWithMs):QString{});
        const auto fullBlock=eta&&currentLength?std::optional<double>(double(currentLength)/eta->positionsPerSecond):std::nullopt;
        setProperty("pgn_run_elapsed_seconds",elapsed?QVariant(*elapsed):QVariant{});
        setProperty("pgn_full_block_seconds",fullBlock?QVariant(*fullBlock):QVariant{});
        setProperty("pgn_block_eta_seconds",eta?QVariant(eta->seconds):QVariant{});
        setProperty("pgn_block_eta_window_seconds",eta?QVariant(eta->windowSeconds):QVariant{});
        setProperty("pgn_block_eta_caption",etaValue);
        // Historical known positions belong to the TOTAL device card only,
        // never to a producer's current-work rail. Keep two context fields
        // above the three interval counters instead of leaving an empty slot.
        const QVector<QPair<QString,QString>> cells{{selected?"RETAINED · CURRENT RUN":"RETAINED · NATIVE ARCHIVE",retained},{"MINING NOW · EXACT RANGE IN TOOLTIP",pgn::ui::bookingRailTitle(identity,current.run)},
            {"REMAINING · BLOCK ETA",etaValue},
            {"POSITIONS / SECOND · "+sum.rateCaption,pgnRate(sum.positionsRate)},{"TESTS / SECOND · "+sum.rateCaption,pgnRate(sum.testsRate)},{"OUTPUT / 1 BILLION INPUT POSITIONS",sum.yield?QString::number(*sum.yield,'f',6):QString("—")},
            {"ELAPSED · BLOCK WALL TIME",blockElapsed?formatDuration(*blockElapsed):QString("TIMING PENDING")},
            {"FULL BLOCK · RECENT MEAN",fullBlock?formatDuration(*fullBlock):QString("WARMING")},
            {"100 BLOCKS · RECENT MEAN",fullBlock?formatDuration(100* *fullBlock):QString("WARMING")}};
        QVariantList metricRectangles;
        for(int i=0;i<cells.size();++i){const int row=i/3,columns=3,column=i%3;const QRectF cell(area.left()+14+column*(area.width()-28)/columns,bodyTop+row*cellHeight,(area.width()-28)/columns,cellHeight);
            metricRectangles.append(cell);
            painter.setFont(Font(7,QFont::DemiBold,true));painter.setPen(QColor("#85b8c2"));painter.drawText(cell.adjusted(0,0,-8,-cell.height()+11),Qt::AlignLeft,painter.fontMetrics().elidedText(cells[i].first,Qt::ElideMiddle,int(cell.width()-8)));
            painter.setFont(Font(cellHeight>=36?11:9,QFont::DemiBold,true));painter.setPen(QColor("#e7f9f1"));painter.drawText(cell.adjusted(0,12,-8,0),Qt::AlignLeft,painter.fontMetrics().elidedText(cells[i].second,Qt::ElideMiddle,int(cell.width()-8)));m_uiTooltipHits.append({cell,i==6?QString("Elapsed wall time from this block's first actual process birth, retained through verified checkpoint ancestry and application restarts. Includes checkpoint/test pauses. Current process only: %1. Full block at recent mean is a rate-based projection, not elapsed history.").arg(elapsed?formatDuration(*elapsed):QString("PAUSED")):cells[i].first+"\n"+cells[i].second});}
        painter.setFont(Font(7,QFont::Normal,true));painter.setPen(QColor("#7fa6b0"));
        const auto age=sum.sourceAgeLowerBoundNs?"≥"+QString::number(*sum.sourceAgeLowerBoundNs/1000000)+"ms":"UNKNOWN";
        const QRectF footer(area.left()+14,area.bottom()-17,area.width()-28,12);
        const auto footerText=QString("%1 · SOURCE AGE %2 · %3 · SPAN %4ns · PROOFS PENDING").arg(identity.declaration,age,sum.rateCaption,sum.intervalNs);
        painter.drawText(footer,Qt::AlignLeft|Qt::AlignVCenter,
            painter.fontMetrics().elidedText(footerText,Qt::ElideMiddle,int(footer.width())));
        m_uiTooltipHits.append({footer,footerText});
        if(metricRectangles.size()>2)m_uiTooltipHits.append({metricRectangles[2].toRectF(),
            eta?QString("Estimated remaining local range time, not a network-block prediction. %1 seconds of cumulative source positions / source time; ~%2 Gpositions/s. Includes publication delays, never a selected peak. Clock time is approximate; unavailable after5s source silence or pause.").arg(eta->windowSeconds,0,'f',1).arg(eta->positionsPerSecond/1e9,0,'f',2)
                :QString("Awaiting at least5 seconds of fresh cumulative source counters for this exact GPU/run. A paused or stale source has no ETA.")});
        setProperty("pgn_central_metric_rectangles",metricRectangles);
        setProperty("pgn_central_footer_rectangle",footer);
        m_uiTooltipHits.append({area,QString("Current-work context and raw adjacent source publication rates. Historical known positions remain in the TOTAL card only, not this producer rail; they are not certified coverage. Level N is bit length. Exact current range [%1, %2). Ratio = retained delta / position delta × 1e9; undefined when input is zero. Multi-device rates are unavailable unless exact clock intervals match.").arg(m_pgnBookingModel->runtime().start,m_pgnBookingModel->runtime().end)});
        setProperty("pgn_central_has_cumulative_positions",false);
        setProperty("pgn_central_page",selected?selected->gpuUuid:QString("TOTAL"));setProperty("pgn_central_active_pages",active.size()+1);painter.restore();
        if(sum.sourceAgeLowerBoundNs)setProperty("pgn_raw_age_lower_bound_ns",QString::number(*sum.sourceAgeLowerBoundNs));else setProperty("pgn_raw_age_lower_bound_ns",QVariant{});
        setProperty("pgn_raw_source_utc",sum.observedUtc.toUTC().toString(Qt::ISODateWithMs));
    }
    void drawDashboard(QPainter& painter)
    {
        setProperty("pgn_gauge_label_rects",QJsonArray{});
        setProperty("pgn_cpu_footer_geometry",QJsonObject{});
        m_deviceControlHits.clear();
        m_deviceCollapseHits.clear();m_totalGroupHits.clear();
        const QColor mint(QStringLiteral("#69efc5"));
        const QColor cyan(QStringLiteral("#46c8ee"));
        // Derive this frame's strip before drawing its dependent central area.
        auto layout=pgn::ui::MiningLayout::fit(width(),height());
        const bool compact=property("pgn_compact_mining").toBool();
        if(compact){layout.sections=false;layout.sideBySide=false;layout.scene={12,0,double(width()-24),double(height())};layout.cards={16,8,double(width()-32),154};}
        const QRectF scene=layout.scene;
        m_responsiveSectionHits.clear();m_devicePageHits.clear();
        if(layout.sections&&m_responsiveSection!=2){
            m_plotImageKey.clear();
            m_candidateGraphRect={};m_graphXAxisHit={};m_graphYAxisHit={};
            m_graphResetHit={};m_currentRunToggleHit={};m_fullHistoryToggleHit={};
            m_graphTimeAxisHit={};m_graphLengthAxisHit={};
            m_graphXAxisModeHits.clear();m_graphRangeHits.clear();m_legacyTimeScaleHits.clear();m_candidateHits.clear();
        }
        m_pgnCentralArea={};m_pgnSummaryPages.clear();m_pgnBookingHits.clear();m_bookingRailNavigation.clear();m_bookingRailArea={};
        setProperty("pgn_central_summary_rectangle",QRectF{});
        setProperty("pgn_graph_plot_rectangle",QRectF{});
        setProperty("pgn_responsive_layout",layout.sections?"sections":layout.sideBySide?"side-by-side":"stacked");
        if(layout.sections){
            const QStringList labels{"DEVICES","WORK / VERIFICATION","GAP HISTORY"};
            const qreal w=scene.width()/3.;
            for(int i=0;i<3;++i){
                const QRectF button(scene.left()+i*w,scene.top(),w-4,24);
                pgn::ui::drawRangeTile(painter,button,cyan,labels[i],{}, {},m_responsiveSection==i);
                m_responsiveSectionHits.append({button,i});
            }
        }
        const auto cards=miningLaneCards();
        // Synchronize every device before painting TOTAL, which precedes them.
        // A first source frame must never show the previous/default aggregate.
        for(const auto& card:cards)observeDeviceControl(card.stableId,card.snapshot);
        // Nine native PGN measures use the SAME Horizon gauges, with enough
        // horizontal space for full strokes, values and individual hitboxes.
        const qreal podWidth = m_pgnBookingModel
            ? std::clamp(scene.width()*0.42,504.0,544.0)
            : std::clamp(scene.width()*0.34,400.0,440.0);
        constexpr qreal compactWidth = 270.0;
        bool expandedCard=!m_collapsedDevices.value("@total",true);if(compact)for(const auto& card:cards)if(!m_collapsedDevices.value(card.stableId,true))expandedCard=true;
        bool cpuBudgetCard=false;for(const auto& card:cards)cpuBudgetCard|=pgn::ui::hasCpuThreadBudget(card.kind,card.snapshot.raw);
        const qreal podHeight = compact&&!expandedCard?(cpuBudgetCard?128.0:112.0):m_pgnBookingModel ? 154.0 : 142.0;
        if(compact){layout.cards.setHeight(podHeight);const int required=int(podHeight)+16;if(height()!=required)QTimer::singleShot(0,this,[this,required]{setFixedHeight(required);});}
        const qreal cardGap = 14.0;
        m_deviceStripRect = layout.cards;
        if(!compact&&(!layout.sections||m_responsiveSection==1))drawSearchSpace(painter);
        if(!layout.sections||m_responsiveSection==0){
        QVector<qreal> cardWidths;
        const qreal summaryWidth=m_pgnBookingModel?compactWidth:0;
        qreal totalWidth = summaryWidth>0?summaryWidth+(cards.isEmpty()?0:cardGap):0;
        for (const LaneCard& card : cards) {
            const qreal width = m_collapsedDevices.value(card.stableId, true)
                ? compactWidth : podWidth;
            cardWidths.append(width);
            totalWidth += width;
        }
        if (!cards.isEmpty()) totalWidth += cardGap * (cards.size() - 1);
        // A single centered strip: all devices retain their native gauges and
        // controls. Overflow scrolls, while quiet edges allow bounded play.
        m_deviceScrollMaximum=std::max(0.,totalWidth-m_deviceStripRect.width());
        if(!m_draggingDevices&&m_deviceSpring.state()!=QAbstractAnimation::Running)
            m_deviceScrollX=std::clamp(m_deviceScrollX,0.,m_deviceScrollMaximum);
        const qreal startX=m_deviceStripRect.left()+
            std::max(0.,(m_deviceStripRect.width()-totalWidth)/2.)-m_deviceScrollX;
        setProperty("pgn_device_strip_content_width",totalWidth);
        setProperty("pgn_device_strip_scroll_maximum",m_deviceScrollMaximum);
        setProperty("pgn_device_strip_scroll",m_deviceScrollX);
        setProperty("pgn_device_strip_left",startX);
        setProperty("pgn_device_strip_view_left",m_deviceStripRect.left());
        setProperty("pgn_device_strip_top",m_deviceStripRect.top());
        setProperty("pgn_device_strip_view_width",m_deviceStripRect.width());
        painter.save();painter.setClipRect(m_deviceStripRect);
        QJsonArray cardLayout;
        const bool publishCardLayout=property("pgn_card_layout_audit").toBool()||property("pgn_card_geometry_enabled").toBool();
        const auto recordCard=[&](const QString& id,const QString& kind,const QRectF& rect,bool expanded){
            if(!publishCardLayout)return;
            const auto bands=pgn::ui::deviceCardBands(rect,kind=="TOTAL"?(expanded?1:0):(expanded?2:1),kind=="TOTAL"?2:(expanded||kind=="CPU"&&rect.height()>112?2:0),expanded?20.:18.);
            cardLayout.append(QJsonObject{{"id",id},{"kind",kind},{"expanded",expanded},
                {"rect",QJsonArray{rect.x(),rect.y(),rect.width(),rect.height()}},
                {"visible",rect.intersects(m_deviceStripRect)},{"header_end",bands.headerEnd},
                {"gauge_top",bands.centerY-bands.radius},{"gauge_bottom",bands.centerY+bands.radius+18},
                {"footer_start",bands.footerStart}});
        };
        qreal cardX=startX;
        if(summaryWidth>0)recordCard("@total","TOTAL",QRectF(cardX,m_deviceStripRect.top(),summaryWidth,compact?(m_collapsedDevices.value("@total",true)?112.:154.):podHeight),!m_collapsedDevices.value("@total",true));
        if(summaryWidth>0){drawPgnTotalCard(painter,QRectF(cardX,m_deviceStripRect.top(),summaryWidth,compact?(m_collapsedDevices.value("@total",true)?112.:154.):podHeight));cardX+=summaryWidth+(cards.isEmpty()?0:cardGap);}
        for(int i=0;i<cards.size();++i){const auto& card=cards[i];
            const QRectF cardRect(cardX,m_deviceStripRect.top(),cardWidths[i],compact?(m_collapsedDevices.value(card.stableId,true)?(pgn::ui::hasCpuThreadBudget(card.kind,card.snapshot.raw)?128.:112.):154.):podHeight);
            recordCard(card.stableId,card.kind,cardRect,!m_collapsedDevices.value(card.stableId,true));
            if(cardRect.intersects(m_deviceStripRect)){
                if(m_collapsedDevices.value(card.stableId,true))
                    drawCompactLanePod(painter,cardRect,card.snapshot,card.label,card.color,card.kind,card.stableId);
                else drawHybridLanePod(painter,cardRect,card.snapshot,card.history,card.label,card.color,card.kind,card.stableId);
            }
            cardX+=cardWidths[i]+cardGap;
        }
        if(publishCardLayout)setProperty("pgn_card_layout",cardLayout);
        painter.restore();
        if(cards.isEmpty()&&!m_pgnBookingModel){painter.setFont(Font(9,QFont::DemiBold,true));painter.setPen(QColor("#ff9a3d"));
            painter.drawText(m_deviceStripRect,Qt::AlignCenter,"NO RESOURCE DEVICE TELEMETRY EMITTED");}
        }
        if(compact)return; // Reservation exploration is in Community; no unrelated queue in this ticket view.
        if(!layout.sections||m_responsiveSection==2)drawCandidatePlot(painter,layout.graph);
    }

    void drawSearchSpace(QPainter& painter)
    {
        LastPaintExecution execution(this,"pgn_search_space_ns");
        const QColor mint(QStringLiteral("#69efc5"));
        const QColor cyan(QStringLiteral("#46c8ee"));
        const auto layout=pgn::ui::MiningLayout::fit(width(),height());
        const QRectF scene=layout.work;
        const qreal inset=scene.width()<700?36:100;
        const qreal left = scene.left() + inset;
        const qreal right = scene.right() - inset;
        const qreal plotTop = scene.bottom()+8;
        // The legacy composition owns a strict central viewport.  Its verifier
        // annotation used to extend into the graph header because the rail was
        // only 22 px above plotTop while its text extended 21 px downward.
        const qreal magnitudeY = scene.top()+2;
        // Keep the shift instrument, producer annotation and animated rail in
        // three disjoint bands.  The previous 52 px separation placed the
        // PRODUCER RAIL caption directly on top of the shift ruler.
        const qreal instrumentSafeInset = scene.width()<700?8:std::clamp(scene.width()*.10,80.,130.);
        const qreal overviewHeight=m_pgnBookingModel?std::max(68.,32.+bookingOverview(scene.width()-2*instrumentSafeInset-28).height):0.;
        const qreal producerY = magnitudeY+(m_pgnBookingModel?overviewHeight+20:49);
        // Legacy central layout is divided into reserved horizontal bands:
        // producer rail, primary state, gap badge, verifier rail/annotation,
        // then the graph.  This prevents three independent annotations from
        // occupying the same pixels at 1280x768.
        // The current-verification annotation owns 21px below its rail. Never
        // force a fixed central height through the graph's top border.
        const qreal verifierY = scene.bottom()-29;
        const qreal centerY = (producerY+verifierY)/2.0;

        painter.setPen(QPen(QColor(46, 88, 94, 145), 1));
        painter.drawRoundedRect(scene, 17, 17);

        // Gapcoin cards are compact; the remaining central viewport belongs to
        // the producer/verifier instruments even on tall windows.

        painter.save();
        painter.setClipRect(QRectF(scene.left() + 1, scene.top() + 1,
                                   scene.width() - 2,
                                   std::max<qreal>(1.0, plotTop - scene.top() - 8.0)));

        QLinearGradient voidGradient(left, centerY, right, centerY);
        voidGradient.setColorAt(0.0, QColor(30, 118, 101, 72));
        voidGradient.setColorAt(0.18, QColor(5, 19, 24, 18));
        voidGradient.setColorAt(0.50, QColor(0, 3, 9, 210));
        voidGradient.setColorAt(0.82, QColor(4, 18, 25, 18));
        voidGradient.setColorAt(1.0, QColor(26, 112, 124, 72));
        painter.setPen(Qt::NoPen);
        painter.setBrush(voidGradient);
        painter.drawRoundedRect(QRectF(left, producerY - 38, right - left, verifierY - producerY + 76), 58, 58);
        drawLogRuler(painter,
                     scene.left() + instrumentSafeInset,
                     scene.right() - instrumentSafeInset,
                     magnitudeY);

        const QVector<const FrontierTelemetrySnapshot*> lanes = minerLanes();
        int activeMinerCount = 0;
        int activeVerifierCount = 0;
        QSet<QString> activeRunIds;
        qint64 verifierObservedMs = 0;
        for (const FrontierTelemetrySnapshot* lane : lanes) {
            if (minerLaneLive(*lane)) {
                ++activeMinerCount;
                activeRunIds.insert(lane->runId);
            }
            const QString verifier = lane->verifierStatus.toLower();
            const bool verifierEmitted = !verifier.isEmpty() &&
                !verifier.contains(QStringLiteral("not-emitted")) &&
                !verifier.contains(QStringLiteral("pending"));
            const bool verifierRunning = verifier.contains(QStringLiteral("active")) ||
                verifier.contains(QStringLiteral("running")) ||
                verifier.contains(QStringLiteral("verifying")) ||
                verifier.contains(QStringLiteral("verified")) ||
                verifier.contains(QStringLiteral("exact"));
            if (lane->authoritative && !lane->stale && verifierEmitted && verifierRunning) {
                ++activeVerifierCount;
                if (lane->observedUtc.isValid()) {
                    verifierObservedMs = std::max(verifierObservedMs,
                                                   lane->observedUtc.toMSecsSinceEpoch());
                }
            }
        }
        if (minerLaneLive(m_verifierTelemetry) && m_verifierTelemetry.observedUtc.isValid()) {
            ++activeVerifierCount;
            verifierObservedMs = std::max(verifierObservedMs,
                                           m_verifierTelemetry.observedUtc.toMSecsSinceEpoch());
        }
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const CandidatePoint* intervalEvent = nullptr;
        if(m_intervalCacheGeneration!=m_graphProjectionGeneration || m_intervalCacheRuns!=activeRunIds){
        m_intervalCacheEvent.reset();
        for (auto iterator = m_candidates.crbegin(); iterator != m_candidates.crend(); ++iterator) {
            if (!iterator->observedUtc.isValid() ||
                iterator->observedUtc.toMSecsSinceEpoch() < m_uiSessionStartedUtcMs ||
                !activeRunIds.contains(iterator->runId)) continue;
            const qint64 signalAtMs = iterator->proofTransitionAtMs > 0
                ? iterator->proofTransitionAtMs : iterator->appendedAtMs;
            const qint64 ageMs = signalAtMs > 0 && signalAtMs <= nowMs
                ? nowMs - signalAtMs : -1;
            if (ageMs >= 0 && ageMs <= 4000) {
                m_intervalCacheEvent=*iterator;
                break;
            }
        }
        m_intervalCacheGeneration=m_graphProjectionGeneration;m_intervalCacheRuns=activeRunIds;
        }
        if(m_intervalCacheEvent){
            const auto& cached=*m_intervalCacheEvent;
            const qint64 at=cached.proofTransitionAtMs>0?cached.proofTransitionAtMs:cached.appendedAtMs;
            if(at>0&&at<=nowMs&&nowMs-at<=4000)intervalEvent=&cached;
        }
        FrontierPrimeGapSignalKind signalKind = FrontierPrimeGapSignalKind::Idle;
        qint64 signalAgeMs = -1;
        if (intervalEvent) {
            const QString state = intervalEvent->status.toLower();
            const qint64 signalAtMs = intervalEvent->proofTransitionAtMs > 0
                ? intervalEvent->proofTransitionAtMs : intervalEvent->appendedAtMs;
            signalAgeMs = signalAtMs > 0 ? nowMs - signalAtMs : -1;
            if (state.contains(QStringLiteral("reject"))) {
                signalKind = FrontierPrimeGapSignalKind::Rejected;
            } else if (state.startsWith(QStringLiteral("exact")) ||
                       state.contains(QStringLiteral("certified")) ||
                       state.contains(QStringLiteral("verified"))) {
                signalKind = FrontierPrimeGapSignalKind::Exact;
            } else {
                signalKind = FrontierPrimeGapSignalKind::Candidate;
            }
        }
        const FrontierPrimeGapVisualState intervalVisual = FrontierPrimeGapVisual(
            signalKind, signalAgeMs, activeVerifierCount > 0, verifierObservedMs, nowMs);
        if (m_legacyR4Mode) {
            drawLegacyActivityMatrix(painter, left + 48, right - 48,
                                     producerY + 7, verifierY - 7);
        }
        if (!m_legacyR4Mode) {
            drawPrimePipeline(painter,
                              QRectF(scene.left() + 130, scene.top() + 159,
                                     scene.width() - 260, 31),
                              lanes, activeVerifierCount);
        }
        const QColor leftBoundaryColor = intervalVisual.exactLocked
            ? QColor(QStringLiteral("#75ff9f")) : mint;
        const QColor rightBoundaryColor = intervalVisual.exactLocked
            ? QColor(QStringLiteral("#75ff9f")) : cyan;
        drawBoundary(painter, QPointF(left, centerY), leftBoundaryColor,
                     QStringLiteral("p"), QStringLiteral("LEFT PRIME"),
                     intervalVisual.boundaryIntensity, intervalVisual.exactLocked,
                     m_pgnBookingModel&&scene.width()<700);
        drawBoundary(painter, QPointF(right, centerY), rightBoundaryColor,
                     QStringLiteral("q"), QStringLiteral("RIGHT PRIME"),
                     intervalVisual.boundaryIntensity, intervalVisual.exactLocked,
                     m_pgnBookingModel&&scene.width()<700);
        if (!m_legacyR4Mode) {
            drawPrimeGapInterval(painter, left, right, centerY + 57,
                                 intervalVisual, signalKind, intervalEvent);
        }
        const bool benchmarkOnly = !lanes.isEmpty() && std::all_of(
            lanes.cbegin(), lanes.cend(), [](const FrontierTelemetrySnapshot* lane) {
                return lane->sourceMode == QLatin1String("benchmark");
            });
        bool diamondProducer = false;
        qulonglong diamondCandidates = 0;
        qulonglong diamondExact = 0;
        for (const FrontierTelemetrySnapshot* lane : lanes) {
            if (!lane) continue;
            const bool diamondLane = lane->engineId.contains(
                QStringLiteral("diamond"), Qt::CaseInsensitive) ||
                lane->raw.value(QStringLiteral("diamond_continuous")).toBool(false);
            if (!diamondLane) continue;
            diamondProducer = true;
            bool candidatesOk = false;
            const qulonglong laneCandidates = lane->cumulativeGapCandidates.toULongLong(
                &candidatesOk);
            if (candidatesOk) diamondCandidates += laneCandidates;
            const QJsonValue exactValue = lane->raw.value(
                QStringLiteral("diamond_exact_total"));
            if (exactValue.isDouble() && exactValue.toDouble() >= 0.0) {
                diamondExact += static_cast<qulonglong>(exactValue.toDouble());
            }
        }
        const QString producerState = lanes.isEmpty()
            ? QStringLiteral("WAITING FOR LIVE PRODUCER")
            : (diamondProducer
                   ? QStringLiteral("%1/%2 DIAMOND CRST PRODUCERS ACTIVE · UUID-BOUND")
                         .arg(activeMinerCount).arg(lanes.size())
            : (benchmarkOnly
                   ? QStringLiteral("%1/%2 BENCHMARK STREAMS ACTIVE · UUID-DEDUPED")
                         .arg(activeMinerCount).arg(lanes.size())
                   : QStringLiteral("%1/%2 MINERS ACTIVE · UUID-DEDUPED")
                         .arg(activeMinerCount).arg(lanes.size())));
        drawProducerRail(painter, left, right, producerY, lanes, producerState);

        const FrontierVerifierDisplayState verifierDisplay = m_hasVerifierTelemetry
            ? verifierDisplayState(m_verifierTelemetry)
            : FrontierVerifierDisplayState::Unavailable;
        const bool verifierArchive = verifierDisplay == FrontierVerifierDisplayState::SealedArchive;
        bool verifierActive = activeVerifierCount > 0 || verifierArchive;
        bool structuredVerifier = m_hasVerifierTelemetry || activeVerifierCount > 0;
        QString verifierState = verifierArchive
            ? QStringLiteral("SEALED EXACT ARCHIVE · %1")
                  .arg(verifierProgressText(m_verifierTelemetry))
            : (activeVerifierCount > 0 && m_hasVerifierTelemetry
            ? QStringLiteral("%1 · %2")
                  .arg(verifierProgressText(m_verifierTelemetry),
                       verifierRateText(m_verifierTelemetry))
            : (m_chainObservation && m_chainObservation->freshAt(QDateTime::currentDateTimeUtc())
                   ? QStringLiteral("CHAIN IDENTITY VERIFIED / SCIENTIFIC VERIFIER WAITING")
                   : QStringLiteral("VERIFIER SOURCE OFFLINE")));
        if (m_legacyR4Mode) {
            if (diamondProducer) {
                structuredVerifier = true;
                const qulonglong pending = diamondCandidates > diamondExact
                    ? diamondCandidates - diamondExact : 0;
                verifierActive = diamondCandidates > 0;
                verifierState = QStringLiteral(
                    "DIAMOND EXACT PIPELINE · CANDIDATES %1 · EXACT %2 · PENDING %3")
                    .arg(diamondCandidates).arg(diamondExact).arg(pending);
            } else {
            // Every legacy CPU/GPU worker owns an independent result journal.
            // Reading only the first active lane made the central verifier
            // appear offline while another device was already validating
            // exact gaps.  Aggregate all live lanes, as the footer does.
            const auto legacyCount = [&lanes](const char* key) -> int {
                int total = 0;
                const QString field = QString::fromLatin1(key);
                for (const FrontierTelemetrySnapshot* legacyLane : lanes) {
                    if (legacyLane) total += legacyLane->raw.value(field).toInt(0);
                }
                return total;
            };
            const int validated = legacyCount("legacy_verifier_host_validated");
            const int submitted = legacyCount("legacy_verifier_submitted");
            const int pending = legacyCount("legacy_verifier_pending_canonical");
            const int rejected = legacyCount("legacy_verifier_rejected");
            const int chainErrors = legacyCount("legacy_verifier_chain_errors");
            structuredVerifier = structuredVerifier || validated > 0 || pending > 0 || chainErrors > 0;
            verifierActive = (validated > 0 || pending > 0) && chainErrors == 0;
            if (chainErrors > 0) {
                verifierState = QStringLiteral("VERIFIER INTEGRITY ERROR · %1 BROKEN EVENT CHAINS")
                    .arg(chainErrors);
            } else if (pending > 0 && validated == 0) {
                verifierState = QStringLiteral("NODE ACCEPTED %1 · SUBMITTED %2 · LOCAL TRACE NOT PROVIDED · CANONICAL CONFIRMATION PENDING")
                    .arg(pending).arg(submitted);
            } else if (pending > 0) {
                verifierState = QStringLiteral("HOST EXACT %1 · SUBMITTED %2 · NODE ACCEPTED %3 · CANONICAL CONFIRMATION PENDING")
                    .arg(validated).arg(submitted).arg(pending);
            } else if (validated > 0) {
                verifierState = QStringLiteral("HOST EXACT %1 · SUBMITTED %2 · REJECTED/STALE %3")
                    .arg(validated).arg(submitted).arg(rejected);
            } else {
                verifierState = QStringLiteral("WAITING FOR FIRST STRUCTURED VERIFICATION CHAIN");
            }
            }
        }
        const auto localProof=pgn::ui::localSourceProofPresentation(property("pgn_local_source_proof").value<QJsonObject>(),activeRunIds);
        if(!structuredVerifier&&localProof.present){verifierState=localProof.caption;verifierActive=localProof.active;}
        if(property("pgn_cpu_audit_enabled").isValid()&&!property("pgn_cpu_audit_enabled").toBool()&&!structuredVerifier){verifierState="CPU AUDIT OFF · GPU MINING INDEPENDENT";verifierActive=false;}
        const auto independent=property("pgn_independent_verification_receipt").value<QJsonObject>();
        const auto independentError=property("pgn_independent_verification_error").toString();
        // Historical independent evidence does not replace real chain errors,
        // node acceptance or current-source proof status. No range credit.
        const auto historicalProof=pgn::ui::historicalProofPresentation(independent,independentError);
        // Archives belong to Number Lens / Exact records. They must never
        // substitute an idle live verifier with an apparently current result.
        setProperty("pgn_live_verifier_state",verifierState);
        setProperty("pgn_archived_verification_separate",true);
        drawRail(painter,
                 left,
                 right,
                 verifierY,
                 cyan,
                 localProof.present?QStringLiteral("BACKGROUND CPU AUDIT"):QStringLiteral("RESULT CHECKS"),
                 verifierState,
                 verifierActive,
                 std::fmod(m_phase + 0.31, 1.0),
                 true);

        // Reserve the actual boundary instruments before placing the summary.
        // The old 8px inset painted its opaque card over both p/q instruments.
        const qreal summaryInset=scene.width()<700?40:65;
        m_pgnCentralArea=QRectF(left+summaryInset,producerY+9,right-left-2*summaryInset,verifierY-producerY-18);
        const qreal footprint=scene.width()<700?35:42;
        setProperty("pgn_central_boundary_rectangles",QVariantList{
            QRectF(left-footprint,centerY-39,2*footprint,90),
            QRectF(right-footprint,centerY-39,2*footprint,90)});
        setProperty("pgn_central_summary_rectangle", m_pgnCentralArea);
        setProperty("pgn_central_verifier_y", verifierY);
        drawLiveMinerTelemetry(painter, left, right, centerY);

        painter.restore();
    }

    void drawLegacyActivityMatrix(QPainter& painter, qreal left, qreal right,
                                  qreal top, qreal bottom)
    {
        if (right <= left || bottom - top < 8.0) return;
        QVector<const FrontierTelemetrySnapshot*> activeLanes;
        for (const FrontierTelemetrySnapshot* lane : minerLanes()) {
            if (lane && minerLaneLive(*lane)) activeLanes.append(lane);
        }
        if (activeLanes.isEmpty()) return;
        // Animation represents emitted mining work only.  Physical resources
        // in MONITOR state used to generate the misleading CPU-MONITOR glyphs
        // behind every label in the centre.  More than two active workers are
        // segmented horizontally rather than stealing vertical space.
        const int laneCount = activeLanes.size();
        const int rowCount = std::min(2, laneCount);
        const int lanesPerRow = (laneCount + rowCount - 1) / rowCount;
        const qreal laneHeight = (bottom - top) / std::max(1, rowCount);
        const qreal segmentWidth = (right - left) / std::max(1, lanesPerRow);
        static constexpr char glyphs[] = "0123456789ABCDEF";
        painter.save();
        painter.setClipRect(QRectF(left, top, right - left, bottom - top));
        painter.setFont(Font(7, QFont::DemiBold, true));
        for (int laneIndex = 0; laneIndex < laneCount; ++laneIndex) {
            const int row = laneIndex % rowCount;
            const int slot = laneIndex / rowCount;
            const qreal segmentLeft = left + slot * segmentWidth;
            const qreal segmentRight = std::min(right, segmentLeft + segmentWidth);
            const int columns = std::clamp(qRound((segmentRight - segmentLeft) / 14.0), 4, 64);
            const FrontierTelemetrySnapshot& lane = *activeLanes.at(laneIndex);
            const std::optional<double> pairRate = lane.gapPairsWindowPerSecond
                ? lane.gapPairsWindowPerSecond
                : (lane.epochGapPairsPerSecond ? lane.epochGapPairsPerSecond
                                                : lane.gapPairsTestedPerSecond);
            const std::optional<double> prpRate = lane.powmPerSecond;
            const bool cpu = lane.engineId.contains(QStringLiteral("cpu"), Qt::CaseInsensitive);
            const QString identity = lane.gpuUuid.isEmpty() ? lane.runId : lane.gpuUuid;
            const QColor base = deviceIdentityColor(identity,
                cpu ? QStringLiteral("CPU") : QStringLiteral("GPU"));
            const qreal y = top + laneHeight * (row + 0.5);
            const double emittedWork = pairRate.value_or(prpRate.value_or(0.0));
            const double workRatio = std::clamp(std::log10(1.0 + emittedWork) / 6.0,
                                                0.0, 1.0);
            const double speed = 0.22 + 1.78 * workRatio;
            for (int column = 0; column < columns; ++column) {
                const double seed = std::fmod(column / static_cast<double>(columns) +
                    m_phase * speed + laneIndex * 0.173, 1.0);
                const qreal x = segmentLeft + seed * (segmentRight - segmentLeft);
                const int head = (column * 11 + laneIndex * 7 +
                                  qRound(m_phase * 500.0)) & 15;
                const int alpha = qRound(20.0 + 120.0 * workRatio *
                    (0.45 + 0.55 * std::fmod(column * 0.37 + laneIndex * 0.19, 1.0)));
                painter.setPen(Alpha(base, std::clamp(alpha, 18, 140)));
                painter.drawText(QRectF(x - 5, y - 7, 10, 14), Qt::AlignCenter,
                                 QString(QChar::fromLatin1(glyphs[head])));
            }
        }
        painter.restore();
    }

    void drawLiveMinerTelemetry(QPainter& painter, qreal left, qreal right, qreal centerY)
    {
        LastPaintExecution execution(this,"pgn_live_miner_ns");
        if(m_pgnBookingModel){drawPgnCentral(painter);return;}
        const QColor quiet(QStringLiteral("#759c9a"));
        const QVector<const FrontierTelemetrySnapshot*> lanes = minerLanes();

        painter.save();
        if (lanes.isEmpty()) {
            m_previousMinerHit = {};
            m_nextMinerHit = {};
            painter.setFont(Font(9, QFont::DemiBold, true));
            painter.setPen(quiet);
            const QString status = QStringLiteral("NO LIVE MINER STREAM");
            const QRectF idleStatus = m_legacyR4Mode
                ? QRectF(left + 100, centerY - 25, right - left - 200, 32)
                : QRectF(left + 100, centerY - 22, right - left - 200, 44);
            painter.drawText(idleStatus,
                             Qt::AlignCenter | Qt::TextWordWrap,
                             status);
            painter.restore();
            return;
        }

        std::vector<bool> liveStates;
        liveStates.reserve(lanes.size());
        for (const FrontierTelemetrySnapshot* candidate : lanes) {
            liveStates.push_back(minerLaneLive(*candidate));
        }
        m_minerSelection = FrontierResolveMinerSelection(
            m_minerSelection, liveStates, QDateTime::currentMSecsSinceEpoch());
        const FrontierTelemetrySnapshot& lane = *lanes.at(m_minerSelection.index);
        const bool diamondLane = lane.engineId.contains(QStringLiteral("diamond"),
                                                         Qt::CaseInsensitive) ||
            lane.raw.value(QStringLiteral("diamond_continuous")).toBool(false);
        const bool cpuLane = lane.engineId.contains(QStringLiteral("cpu"),
                                                    Qt::CaseInsensitive);
        const QString laneIdentity = lane.gpuUuid.isEmpty() ? lane.runId : lane.gpuUuid;
        const QColor laneColor = deviceIdentityColor(
            laneIdentity, cpuLane ? QStringLiteral("CPU") : QStringLiteral("GPU"));
        const qreal panelInset = diamondLane ? 76.0 : (m_legacyR4Mode && height()<900 ? 72.0 : 154.0);
        const qreal panelHeight = diamondLane ? 164.0 : 62.0;
        const QRectF informationPanel(left + panelInset,
                                      centerY - panelHeight / 2.0,
                                      right - left - panelInset * 2.0,
                                      panelHeight);
        int activeCount = 0;
        for (const FrontierTelemetrySnapshot* candidate : lanes) {
            activeCount += minerLaneLive(*candidate) ? 1 : 0;
        }
        if (lanes.size() > 1) {
            m_previousMinerHit = QRectF(informationPanel.left() - 35, centerY - 13, 25, 26);
            m_nextMinerHit = QRectF(informationPanel.right() + 10, centerY - 13, 25, 26);
        } else {
            m_previousMinerHit = {};
            m_nextMinerHit = {};
        }
        // A single opaque HUD owns every central label.  It deliberately
        // exposes only decisions an operator needs at a glance; raw run IDs,
        // domains, queues and unavailable epochs belong in logs/science, not
        // in the live search theatre.
        painter.setBrush(QColor(1, 14, 21, 246));
        painter.setPen(QPen(QColor(43, 102, 108, 185), 1));
        painter.drawRoundedRect(informationPanel, 12, 12);
        if (lanes.size() > 1) {
            const auto drawNavigator = [&painter](const QRectF& area,
                                                   const QString& glyph) {
                painter.setBrush(QColor(5, 36, 44, 245));
                painter.setPen(QPen(QColor(QStringLiteral("#69efc5")), 1));
                painter.drawRoundedRect(area, 8, 8);
                painter.setFont(Font(12, QFont::DemiBold, true));
                painter.setPen(QColor(QStringLiteral("#e8fffb")));
                painter.drawText(area, Qt::AlignCenter, glyph);
            };
            drawNavigator(m_previousMinerHit, QStringLiteral("‹"));
            drawNavigator(m_nextMinerHit, QStringLiteral("›"));
            painter.setFont(Font(7, QFont::DemiBold, true));
            painter.setPen(QColor(QStringLiteral("#8fcbd1")));
            painter.drawText(QRectF(informationPanel.center().x() - 55,
                                    informationPanel.top() - 15, 110, 12),
                             Qt::AlignCenter,
                             QStringLiteral("RESOURCE %1 / %2")
                                 .arg(m_minerSelection.index + 1).arg(lanes.size()));
        }

        const auto rate = [](const std::optional<double>& value) {
            if (!value || !std::isfinite(*value) || *value < 0.0) return QStringLiteral("—");
            const double number = *value;
            if (number >= 1.0e9) return QStringLiteral("%1 G").arg(number / 1.0e9, 0, 'f', 2);
            if (number >= 1.0e6) return QStringLiteral("%1 M").arg(number / 1.0e6, 0, 'f', 2);
            if (number >= 1.0e3) return QStringLiteral("%1 K").arg(number / 1.0e3, 0, 'f', 1);
            return QString::number(number, 'f', number < 10.0 ? 1 : 0);
        };

        const CandidatePoint* latestGap = nullptr;
        if(m_laneExactCacheGeneration!=m_graphProjectionGeneration){m_laneExactCache.clear();m_laneExactCountCache.clear();m_laneExactCacheGeneration=m_graphProjectionGeneration;}
        const QString laneKey=lane.runId+QChar(0x1f)+lane.gpuUuid+(diamondLane?"/diamond":"/producer");
        const auto cachedLane=m_laneExactCache.constFind(laneKey);
        if(cachedLane==m_laneExactCache.cend()){
        for (auto candidate = m_candidates.crbegin(); candidate != m_candidates.crend(); ++candidate) {
            if (!candidateExact(*candidate)&&!m_nativeAllCatalog) continue;
            const bool sameRun = !lane.runId.isEmpty() && candidate->runId == lane.runId;
            const bool uuidFallback = lane.runId.isEmpty() && !lane.gpuUuid.isEmpty() &&
                candidate->deviceId.compare(lane.gpuUuid, Qt::CaseInsensitive) == 0;
            if (sameRun || uuidFallback) {
                latestGap = &*candidate;
                break;
            }
            if (!diamondLane && !latestGap&&!m_nativeAllCatalog) latestGap = &*candidate;
        }
        m_laneExactCache.insert(laneKey,latestGap?static_cast<qsizetype>(latestGap-&m_candidates[0]):-1);
        }else if(*cachedLane>=0)latestGap=&m_candidates[static_cast<std::size_t>(*cachedLane)];

        constexpr int cellColumns = 3;
        const int cellRows = 1;
        const int cellCount = cellColumns * cellRows;
        const qreal cellWidth = informationPanel.width() / cellColumns;
        const qreal cellHeight = informationPanel.height() / cellRows;
        for (int column = 1; !diamondLane && column < cellColumns; ++column) {
            painter.setPen(QPen(QColor(60, 144, 133, 65), 1));
            const qreal x = informationPanel.left() + cellWidth * column;
            painter.drawLine(QPointF(x, informationPanel.top() + 8),
                             QPointF(x, informationPanel.bottom() - 8));
        }
        if (diamondLane) {
            painter.setPen(QPen(QColor(60, 144, 133, 65), 1));
            painter.drawLine(QPointF(informationPanel.center().x(),
                                     informationPanel.top() + 8),
                             QPointF(informationPanel.center().x(),
                                     informationPanel.bottom() - 8));
        }
        const auto drawCell = [&painter, &informationPanel, cellWidth, cellHeight](
                                  int index, const QString& title,
                                  const QString& value, const QString& detail,
                                  const QColor& color) {
            const int column = index % 3;
            const int row = index / 3;
            const QRectF cell(informationPanel.left() + cellWidth * column + 10,
                              informationPanel.top() + cellHeight * row + 4,
                              cellWidth - 20, cellHeight - 8);
            const auto fitSingleLine = [](QFont font, const QString& text,
                                          qreal width, int minimumPointSize) {
                while (font.pointSize() > minimumPointSize &&
                       QFontMetrics(font).horizontalAdvance(text) > qFloor(width)) {
                    font.setPointSize(font.pointSize() - 1);
                }
                const int measuredWidth = QFontMetrics(font).horizontalAdvance(text);
                if (measuredWidth > qFloor(width)) {
                    font.setStretch(std::clamp(
                        qFloor(100.0 * width / measuredWidth), 1, 100));
                }
                return font;
            };
            painter.setFont(fitSingleLine(Font(7, QFont::DemiBold, true),
                                          title, cell.width(), 5));
            painter.setPen(Alpha(color, 175));
            painter.drawText(QRectF(cell.left(), cell.top(), cell.width(), 13),
                             Qt::AlignRight | Qt::AlignVCenter, title);
            painter.setFont(fitSingleLine(Font(10, QFont::DemiBold, true),
                                          value, cell.width(), 6));
            painter.setPen(QColor(QStringLiteral("#f2fffc")));
            painter.drawText(QRectF(cell.left(), cell.top() + 14, cell.width(), 19),
                             Qt::AlignLeft | Qt::AlignVCenter, value);
            painter.setFont(fitSingleLine(Font(7, QFont::Normal, true),
                                          detail, cell.width(), 5));
            painter.setPen(Alpha(color, 190));
            painter.drawText(QRectF(cell.left(), cell.top() + 36, cell.width(), 13),
                             Qt::AlignLeft | Qt::AlignVCenter, detail);
        };

        if (diamondLane) {
            const auto rawInteger = [&lane](const char* key) -> qint64 {
                const QJsonValue value = lane.raw.value(QString::fromLatin1(key));
                return value.isDouble() && value.toDouble() >= 0.0
                    ? static_cast<qint64>(value.toDouble()) : -1;
            };
            const auto rawDouble = [&lane](const char* key) -> std::optional<double> {
                const QJsonValue value = lane.raw.value(QString::fromLatin1(key));
                if (!value.isDouble() || !std::isfinite(value.toDouble()) ||
                    value.toDouble() < 0.0) return std::nullopt;
                return value.toDouble();
            };
            const QLocale metricLocale(QLocale::English);
            const qint64 rows = rawInteger("diamond_cumulative_rows");
            const qint64 bandRows = rawInteger("diamond_band_rows");
            const qint64 remaining = rawInteger("diamond_geometry_remaining");
            const qint64 capacity = rawInteger("diamond_geometry_capacity");
            const qint64 band = rawInteger("diamond_band_ordinal");
            const qint64 bandsClosed = rawInteger("diamond_bands_closed");
            const qint64 bandCandidates = rawInteger("diamond_band_candidates");
            const qint64 bandExact = rawInteger("diamond_band_exact");
            const qint64 exactTotal = rawInteger("diamond_exact_total");
            const qint64 targetMerit = rawInteger("diamond_target_merit");
            bool candidatesOk = false;
            const qulonglong candidates = lane.cumulativeGapCandidates.toULongLong(
                &candidatesOk);
            const qulonglong exact = exactTotal >= 0
                ? static_cast<qulonglong>(exactTotal) : 0;
            const QString profile = lane.raw.value(
                QStringLiteral("diamond_profile_id")).toString();
            const QString geometryValue = QStringLiteral("S%1 · %2b · M%3")
                .arg(lane.computeShift.value_or(0))
                .arg(lane.numberBits.value_or(0))
                .arg(targetMerit >= 0 ? QString::number(targetMerit)
                                      : QStringLiteral("—"));
            const QString geometryDetail = QStringLiteral("%1 · BAND %2")
                .arg(profile.isEmpty() ? QStringLiteral("CRST PROFILE") : profile,
                     band >= 0 ? QString::number(band) : QStringLiteral("—"));
            const qint64 consumed = bandRows >= 0 ? bandRows
                : (capacity > 0 && remaining >= 0
                    ? std::max<qint64>(0, capacity - remaining) : -1);
            const double completion = capacity > 0 && consumed >= 0
                ? 100.0 * static_cast<double>(consumed) / static_cast<double>(capacity)
                : 0.0;
            const std::optional<double> rowRate = FrontierDisplayedRate(
                lane.e2eWindowPositionsPerSecond, lane.e2ePositionsPerSecond);
            const auto duration = [](double seconds) {
                if (!std::isfinite(seconds) || seconds < 0.0) return QStringLiteral("—");
                if (seconds < 60.0) return QStringLiteral("%1s").arg(seconds, 0, 'f', 0);
                if (seconds < 3600.0) return QStringLiteral("%1m %2s")
                    .arg(static_cast<int>(seconds) / 60)
                    .arg(static_cast<int>(seconds) % 60);
                if (seconds < 86400.0) return QStringLiteral("%1h %2m")
                    .arg(static_cast<int>(seconds) / 3600)
                    .arg((static_cast<int>(seconds) / 60) % 60);
                return QStringLiteral("%1d %2h")
                    .arg(static_cast<int>(seconds) / 86400)
                    .arg((static_cast<int>(seconds) / 3600) % 24);
            };
            const QString eta = remaining >= 0 && rowRate && *rowRate > 0.0
                ? duration(static_cast<double>(remaining) / *rowRate)
                : QStringLiteral("—");
            const QString latestValue = latestGap
                ? QStringLiteral("Δ%1 · M%2")
                      .arg(latestGap->gap, 0, 'f', 0)
                      .arg(latestGap->merit, 0, 'f', 6)
                : QStringLiteral("AWAITING EXACT GAP");
            const auto preciseDecimal = [](const std::optional<double>& value) {
                if (!value || !std::isfinite(*value) || *value < 0.0)
                    return QStringLiteral("—");
                if (*value == 0.0) return QStringLiteral("0.0000");
                const int decimals = std::clamp(
                    static_cast<int>(std::ceil(-std::log10(*value))) + 3, 4, 16);
                return QString::number(*value, 'f', decimals);
            };
            const auto ratioText = [](qint64 numerator, qint64 denominator) {
                return numerator >= 0 && denominator > 0
                    ? QString::number(static_cast<double>(numerator) /
                                          static_cast<double>(denominator), 'f', 6)
                    : QStringLiteral("—");
            };
            const QRectF leftPanel(informationPanel.left() + 12,
                                   informationPanel.top() + 5,
                                   informationPanel.width() / 2.0 - 22,
                                   informationPanel.height() - 10);
            const QRectF rightPanel(informationPanel.center().x() + 10,
                                    informationPanel.top() + 5,
                                    informationPanel.width() / 2.0 - 22,
                                    informationPanel.height() - 10);
            const auto drawDashboard = [&painter](
                const QRectF& area, const QString& heading,
                const QVector<QPair<QString, QString>>& metrics, const QColor& color) {
                painter.setFont(Font(8, QFont::DemiBold, true));
                painter.setPen(color);
                painter.drawText(QRectF(area.left(), area.top(), area.width(), 15),
                                 Qt::AlignLeft | Qt::AlignVCenter, heading);
                const qreal rowHeight = (area.height() - 19.0) /
                    std::max<qsizetype>(1, metrics.size());
                for (qsizetype row = 0; row < metrics.size(); ++row) {
                    const qreal y = area.top() + 19.0 + row * rowHeight;
                    painter.setPen(QPen(QColor(48, 112, 111, 40), 1));
                    if (row) painter.drawLine(QPointF(area.left(), y),
                                              QPointF(area.right(), y));
                    painter.setFont(Font(7, QFont::Normal, true));
                    painter.setPen(Alpha(color, 170));
                    painter.drawText(QRectF(area.left(), y, area.width() * .47,
                                            rowHeight),
                                     Qt::AlignLeft | Qt::AlignVCenter,
                                     metrics[row].first);
                    QFont valueFont = Font(8, QFont::DemiBold, true);
                    painter.setFont(valueFont);
                    const qreal valueWidth = area.width() * .51;
                    while (valueFont.pointSize() > 6 &&
                           painter.fontMetrics().horizontalAdvance(metrics[row].second) >
                               valueWidth) {
                        valueFont.setPointSize(valueFont.pointSize() - 1);
                        painter.setFont(valueFont);
                    }
                    painter.setPen(QColor(QStringLiteral("#f2fffc")));
                    painter.drawText(QRectF(area.right() - valueWidth, y, valueWidth,
                                            rowHeight),
                                     Qt::AlignRight | Qt::AlignVCenter,
                                     metrics[row].second);
                }
            };
            const QString verified = consumed >= 0 && capacity > 0
                ? QStringLiteral("%1 / %2 (%3%)")
                      .arg(metricLocale.toString(consumed), metricLocale.toString(capacity))
                      .arg(completion, 0, 'f', 2)
                : QStringLiteral("AWAITING BAND");
            const QString remainingValue = remaining >= 0
                ? QStringLiteral("%1 · ETA %2")
                      .arg(metricLocale.toString(remaining), eta)
                : QStringLiteral("—");
            const QString bandYield = QStringLiteral("%1 / %2 · %3")
                .arg(bandCandidates >= 0 ? metricLocale.toString(bandCandidates)
                                         : QStringLiteral("—"),
                     bandExact >= 0 ? metricLocale.toString(bandExact)
                                    : QStringLiteral("—"),
                     ratioText(bandExact, bandCandidates));
            const QString liveRatios = QStringLiteral("λ %1 · Qm %2")
                .arg(preciseDecimal(rawDouble("diamond_geometry_lambda")),
                     preciseDecimal(rawDouble("diamond_qm_band_live")));
            const auto expectedMean = rawDouble("diamond_expected_exact_mean");
            const auto expectedLow = rawDouble("diamond_expected_exact_low95");
            const auto expectedHigh = rawDouble("diamond_expected_exact_high95");
            const QString expectedExact = expectedMean && expectedLow && expectedHigh
                ? QStringLiteral("%1 · 95% %2–%3")
                      .arg(*expectedMean, 0, 'f', 2)
                      .arg(*expectedLow, 0, 'f', 2)
                      .arg(*expectedHigh, 0, 'f', 2)
                : QStringLiteral("—");
            bool bestMeritOk = false;
            const double bestMerit = lane.bestMerit.toDouble(&bestMeritOk);
            const QString bestOrLatest = !lane.bestGap.isEmpty() && bestMeritOk &&
                    std::isfinite(bestMerit)
                ? QStringLiteral("BEST Δ%1 · M%2")
                      .arg(lane.bestGap, QString::number(bestMerit, 'f', 6))
                : latestValue;
            const QVector<QPair<QString, QString>> liveMetrics{
                {QStringLiteral("GEOMETRY / BAND"), geometryValue +
                    QStringLiteral(" · B%1").arg(band >= 0 ? QString::number(band)
                                                           : QStringLiteral("—"))},
                {QStringLiteral("ROWS VERIFIED"), verified},
                {QStringLiteral("INSTANT CRST ROWS/s"), rate(rowRate)},
                {QStringLiteral("ROWS REMAINING"), remainingValue},
                {QStringLiteral("CAND / EXACT / RATIO"), bandYield},
                {QStringLiteral("λ / Qm LIVE (PROVISIONAL)"), liveRatios},
                {QStringLiteral("EXPECTED EXACTS / BAND (MODEL)"), expectedExact}};
            const QVector<QPair<QString, QString>> historyMetrics{
                {QStringLiteral("BANDS CLOSED"), bandsClosed >= 0
                    ? metricLocale.toString(bandsClosed) : QStringLiteral("—")},
                {QStringLiteral("CUMULATIVE ROWS"), rows >= 0
                    ? metricLocale.toString(rows) : QStringLiteral("—")},
                {QStringLiteral("CUMULATIVE CAND / EXACT"), QStringLiteral("%1 / %2")
                    .arg(candidatesOk ? metricLocale.toString(candidates)
                                      : QStringLiteral("—"),
                         metricLocale.toString(exact))},
                {QStringLiteral("Qm CUMULATIVE"), preciseDecimal(rawDouble("diamond_qm_cumulative"))},
                {QStringLiteral("SOURCE OBSERVED UTC"), lane.observedUtc.isValid()
                    ? lane.observedUtc.toUTC().toString(Qt::ISODateWithMs) : QStringLiteral("—")},
                {QStringLiteral("BEST / LATEST EXACT"), bestOrLatest}};
            drawDashboard(leftPanel, QStringLiteral("LIVE · CURRENT BAND"), liveMetrics,
                          QColor(QStringLiteral("#46c8ee")));
            drawDashboard(rightPanel, QStringLiteral("HISTORY · CUMULATIVE"), historyMetrics,
                          QColor(QStringLiteral("#75ff9f")));
            painter.restore();
            return;
        }

        QString workerName;
        if (!lane.gpuUuid.trimmed().isEmpty()) {
            for (const FrontierResourceDevice& device : m_resourceTelemetry.devices) {
                if (device.deviceId.compare(lane.gpuUuid, Qt::CaseInsensitive) == 0 &&
                    !device.name.trimmed().isEmpty()) {
                    workerName = device.name.trimmed();
                    break;
                }
            }
        }
        if (workerName.isEmpty()) workerName = lane.gpuName.trimmed();
        if (workerName.isEmpty()) workerName = lane.engineId;

        int laneExact = 0;
        qint64 laneWindowStartMs = m_uiSessionStartedUtcMs;
        const auto laneStarted = m_legacyRunStartedUtcMs.constFind(lane.runId);
        if (laneStarted != m_legacyRunStartedUtcMs.cend()) {
            laneWindowStartMs = std::max(laneWindowStartMs, *laneStarted);
        }
        const QString countKey=laneKey+QChar(0x1f)+QString::number(laneWindowStartMs);
        const auto cachedCount=m_laneExactCountCache.constFind(countKey);
        if(cachedCount!=m_laneExactCountCache.cend())laneExact=*cachedCount;
        else{
        for (const CandidatePoint& point : m_candidates) {
            if (!candidateNetworkEligible(point) || !candidateExact(point) ||
                !point.observedUtc.isValid() ||
                point.observedUtc.toMSecsSinceEpoch() < laneWindowStartMs) {
                continue;
            }
            const bool sameRun = !lane.runId.isEmpty() && point.runId == lane.runId;
            const bool uuidFallback = lane.runId.isEmpty() && !lane.gpuUuid.isEmpty() &&
                point.deviceId.compare(lane.gpuUuid, Qt::CaseInsensitive) == 0;
            if (sameRun || uuidFallback) ++laneExact;
        }
        m_laneExactCountCache.insert(countKey,laneExact);
        }
        const QString workerDetail = QStringLiteral("%1/%2 ACTIVE / NATIVE WALK")
            .arg(activeCount).arg(lanes.size());
        const QString searchValue = QStringLiteral("%1 positions/s")
            .arg(rate(lane.e2ePositionsPerSecond));
        const bool labCrtTest = lane.raw.value(
            QStringLiteral("lab_highshift")).toBool(false);
        QString searchDetail;
        if (labCrtTest) {
            const QJsonValue crtRate = lane.raw.value(
                QStringLiteral("lab_crt_rows_per_second"));
            const QJsonValue candidates = lane.raw.value(
                QStringLiteral("lab_qualified_candidates_total"));
            searchDetail = QStringLiteral("%1 CRT ROWS/s · %2 LAB CANDIDATES")
                .arg(crtRate.isDouble() ? rate(crtRate.toDouble()) : QString::fromUtf8("—"))
                .arg(candidates.isDouble() ? QString::number(candidates.toDouble(), 'f', 0)
                                            : QString::fromUtf8("—"));
        } else {
            searchDetail = QStringLiteral("%1 tests/s / %2 retained gaps/s")
                .arg(rate(lane.powmPerSecond), rate(lane.gapPairsTestedPerSecond));
        }
        if (!lane.bestStatus.isEmpty()) searchDetail += QStringLiteral(" · %1").arg(lane.bestStatus);
        const QString gapValue = latestGap
            ? QStringLiteral("Δ %1 · M%2")
                  .arg(latestGap->gap, 0, 'f', 0)
                  .arg(latestGap->merit, 0, 'f', 6)
            : QStringLiteral("SCANNING…");
        const QString gapDetail = latestGap && latestGap->observedUtc.isValid()
            ? QStringLiteral("%1 · %2")
                  .arg(m_nativeAllCatalog&&!candidateExact(*latestGap)?QStringLiteral("SOURCE PRP · PROOF PENDING"):labCrtTest ? QStringLiteral("LAB EXACT · NOT CHAIN")
                                  : QStringLiteral("EXACT"))
                  .arg(latestGap->observedUtc.toLocalTime().toString(QStringLiteral("dd/MM HH:mm:ss")))
            : (m_nativeAllCatalog?QStringLiteral("NO SOURCE GAP IN THIS RUN YET"):QStringLiteral("NO EXACT GAP IN THIS VIEW YET"));
        const QColor latestGapColor = latestGap ? candidateDeviceColor(*latestGap) : quiet;
        drawCell(0, QStringLiteral("ACTIVE WORKER"), workerName,
                 workerDetail, laneColor);
        drawCell(1, QStringLiteral("LIVE SEARCH"), searchValue,
                 searchDetail, laneColor);
        drawCell(2, m_nativeAllCatalog?QStringLiteral("LATEST SOURCE GAP"):QStringLiteral("LATEST EXACT GAP"), gapValue,
                 gapDetail, latestGapColor);

        painter.restore();
    }

    void refreshBookingRail(){
        if(!m_pgnBookingModel||m_bookingRailRevision==m_pgnBookingModel->jobsRevision())return;
        m_bookingRailRevision=m_pgnBookingModel->jobsRevision();m_bookingRailSegments.clear();
        m_pgnFinishedRuns.clear();for(const auto& job:m_pgnBookingModel->recentJobs())if(job.state=="SOURCE_FINISHED")m_pgnFinishedRuns.insert(job.id);
        const auto& jobs=m_pgnBookingModel->jobs();
        QString first,last;
        for(const auto& job:jobs){
            if(job.state=="PAUSED"&&job.id!=m_pgnBookingModel->runtime().run)continue;
            if(first.isEmpty()||pgn::booking::compareUnsigned(job.start,first)<0)first=job.start;
            if(last.isEmpty()||pgn::booking::compareUnsigned(job.end,last)>0)last=job.end;
        }
        if(first.isEmpty()){m_bookingRailFirst.clear();m_bookingRailLast.clear();return;}
        m_bookingRailFirst=first;m_bookingRailLast=last;m_bookingRailBits=pgn::booking::bitLength(last);
        for(const auto& job:jobs){
            if(job.state=="PAUSED"&&job.id!=m_pgnBookingModel->runtime().run)continue;
            const auto& physical=m_pgnBookingModel->runtime();
            if(job.kind=="LOCAL_INTENT"&&(job.state=="DISPATCHING"||job.state=="DISPATCHED")&&
               !physical.run.isEmpty()&&job.start==physical.start&&job.end==physical.end&&
               job.device.compare(physical.device,Qt::CaseInsensitive)==0&&
               std::any_of(jobs.cbegin(),jobs.cend(),[&](const auto& row){return row.kind=="RUNTIME"&&row.id==physical.run;}))continue;
            m_bookingRailSegments.append({job,pgn::booking::rangeFraction(job.start,first,last),
                pgn::booking::rangeFraction(job.end,first,last),deviceIdentityColor(job.device,"GPU")});
        }
    }
    pgn::ui::BookingRailOverview bookingOverview(qreal width) const {
        QVector<bool> active;QStringList keys;
        const QString current=m_pgnBookingModel?m_pgnBookingModel->runtime().run:QString{};
        for(const auto& segment:m_bookingRailSegments){const auto& job=segment.job;
            active.append(pgn::ui::bookingRailJobActive(job.state,job.id==current));
            const auto identity=pgn::booking::identifyRange(m_pgnBookingModel->source(),job.start,job.end);
            keys.append(QString::number(identity.zone)+"/"+job.state);
        }
        return pgn::ui::bookingRailOverview(QRectF(0,0,std::max(1.,width),36),active,keys);
    }
    void drawBookingRail(QPainter& painter,const QRectF& area){
        m_pgnBookingHits.clear();m_bookingRailNavigation.clear();m_bookingRailArea=area;
        setProperty("pgn_booking_rail_visual_fractions",QVariantList{});
        setProperty("pgn_booking_rail_observed_fractions",QVariantList{});
        setProperty("pgn_booking_texture_rate",QVariant{});setProperty("pgn_booking_texture_phase",QVariant{});
        painter.setPen(QPen(QColor(48,110,120,170),1));painter.setBrush(QColor(3,24,31,235));painter.drawRoundedRect(area,8,8);
        painter.setFont(Font(9,QFont::DemiBold,true));painter.setPen(QColor("#8bb8c5"));
        setProperty("pgn_reservation_segments",m_bookingRailSegments.size());
        if(m_bookingRailSegments.isEmpty()){
            painter.drawText(area.adjusted(12,2,-12,-2),Qt::AlignCenter,"LOCAL RESERVATION RAIL · AWAITING A PHYSICAL JOB / LOCAL INTENT");return;
        }
        const QRectF track=area.adjusted(12,24,-12,-8);
        const auto& runtime=m_pgnBookingModel->runtime();
        QVector<pgn::booking::RangeIdentity> identities;QStringList titles;
        QFont cellFont("Segoe UI");cellFont.setPixelSize(12);cellFont.setWeight(QFont::DemiBold);
        const QFontMetrics metrics(cellFont);qreal minimumWidth=112;
        int currentIndex=-1;
        for(int i=0;i<m_bookingRailSegments.size();++i){const auto& job=m_bookingRailSegments[i].job;
            identities.append(pgn::booking::identifyRange(m_pgnBookingModel->source(),job.start,job.end));
            titles.append(pgn::ui::bookingRailTitle(identities.last(),job.id));
            minimumWidth=std::max(minimumWidth,qreal(metrics.horizontalAdvance(titles.last())+18));
            if(job.id==runtime.run)currentIndex=i;
        }
        auto overview=bookingOverview(track.width());
        for(auto& cell:overview.cells)cell.translate(track.topLeft());
        for(auto& group:overview.groups)group.area.translate(track.topLeft());
        m_bookingRailOffset=0;m_bookingRailCapacity=m_bookingRailSegments.size();
        const QRectF heading(area.left()+12,area.top()+3,std::max(0.,area.width()-24),18);
        int activeCount=0;for(const auto& group:overview.groups)if(group.active)activeCount+=group.count;
        const QString label=QString("OUR WORK · %1 active · %2 queued / retained · ALL %3 jobs · click for details")
            .arg(activeCount).arg(m_bookingRailSegments.size()-activeCount).arg(m_bookingRailSegments.size());
        painter.drawText(heading,Qt::AlignVCenter|Qt::AlignLeft,painter.fontMetrics().elidedText(label,Qt::ElideRight,int(heading.width())));
        setProperty("pgn_booking_rail_heading",heading);setProperty("pgn_booking_rail_track",track);
        setProperty("pgn_booking_rail_first",0);setProperty("pgn_booking_rail_capacity",m_bookingRailCapacity);
        setProperty("pgn_booking_rail_rows",overview.rows);setProperty("pgn_booking_rail_groups",overview.groups.size());
        setProperty("pgn_booking_rail_previous",QRectF{});setProperty("pgn_booking_rail_next",QRectF{});
        QStringList visibleIds,visibleTitles,visibleDetails;QVariantList visibleFractions,observedFractions;
        if(!m_bookingVisualClock.isValid())m_bookingVisualClock.start();
        const auto presentationTime=m_bookingVisualClock.elapsed();
        std::optional<double> textureRate;
        if(runtime.running)for(const auto* lane:minerLanes()){
            if(lane->runId!=runtime.run)continue;
            if(lane->raw.contains("pgn_process_running")&&!lane->raw.value("pgn_process_running").toBool())continue;
            textureRate=FrontierDisplayedRate(lane->e2eWindowPositionsPerSecond,lane->e2ePositionsPerSecond);
            break;
        }
        const auto texturePhase=m_bookingTextureMotion.advance(runtime.run,textureRate,runtime.running,presentationTime);
        setProperty("pgn_booking_texture_rate",textureRate?QVariant(*textureRate):QVariant{});
        setProperty("pgn_booking_texture_phase",texturePhase?QVariant(*texturePhase):QVariant{});
        for(int index=0;index<m_bookingRailSegments.size();++index){
            const auto& segment=m_bookingRailSegments[index];const auto rectangle=overview.cells[index];
            const bool current=segment.job.id==runtime.run;
            const auto& jobIdentity=identities[index];const auto& title=titles[index];
            const auto detail=current?pgn::ui::bookingRailRuntimeDetail(runtime.running,runtime.fraction,segment.job.state)
                :pgn::ui::bookingRailState(segment.job.state);
            const auto observed=pgn::ui::observedWorkFraction(current?runtime.fraction:
                segment.job.state=="SOURCE_FINISHED"?std::optional<double>(1.):std::nullopt);
            if(current)m_bookingWorkTransition.observe(runtime.run,observed,presentationTime);
            const auto presented=current?m_bookingWorkTransition.value(presentationTime):observed;
            pgn::ui::drawMiningWorkTile(painter,rectangle,segment.color,presented,current,current?texturePhase:std::nullopt);
            visibleFractions.append(presented?QVariant(*presented):QVariant{});
            observedFractions.append(observed?QVariant(*observed):QVariant{});
            if(rectangle.width()>=104){
            painter.save();painter.setClipRect(rectangle.adjusted(1,1,-1,-1),Qt::IntersectClip);
            painter.setFont(cellFont);painter.setPen(QColor("#dbf8f2"));
            const QRectF top=rectangle.adjusted(8,2,-8,-rectangle.height()+18);
            painter.drawText(top,Qt::AlignCenter,metrics.elidedText(title,Qt::ElideRight,int(top.width())));
            QFont detailFont("Segoe UI");detailFont.setPixelSize(10);painter.setFont(detailFont);painter.setPen(QColor("#91c4cc"));
            const QRectF bottom(rectangle.left()+6,rectangle.top()+18,rectangle.width()-12,13);
            painter.drawText(bottom,Qt::AlignCenter,painter.fontMetrics().elidedText(detail,Qt::ElideRight,int(bottom.width())));
            painter.restore();}
            visibleIds.append(segment.job.id);visibleTitles.append(title);visibleDetails.append(detail);
            m_pgnBookingHits.append({rectangle,segment.job.id});
            m_uiTooltipHits.append({rectangle,jobIdentity.title+"\n"+jobIdentity.declaration+"\n"+QString("%1 · %2\nExact range [%3, %4)\nDevice %5\n%6\nLocal intent/physical job only, not an exclusive community reservation or independent coverage certificate. %7 bits measures bit length, not difficulty. Click to inspect Community.")
                .arg(pgn::ui::localWorkLabel(segment.job.state),segment.job.kind=="RUNTIME"?"Observed GPU work":"Saved local range",segment.job.start,segment.job.end,segment.job.device,
                    current?(runtime.positions.isEmpty()?"Progress unavailable":runtime.positions+" positions · source "+runtime.observedUtc.toUTC().toString(Qt::ISODateWithMs)):"Awaiting physical execution")
                .arg(pgn::booking::bitLength(segment.job.start))});
        }
        QStringList groupTitles;
        for(const auto& group:overview.groups){if(group.active||group.count<2)continue;
            const auto& firstJob=m_bookingRailSegments[group.first].job;
            const auto& lastJob=m_bookingRailSegments[group.first+group.count-1].job;
            QString positions="0";for(int i=group.first;i<group.first+group.count;++i)positions=pgn::booking::addUnsigned(positions,pgn::booking::subtractUnsigned(m_bookingRailSegments[i].job.end,m_bookingRailSegments[i].job.start));
            const QString title=pgn::booking::rangeDisplayTitle(firstJob.start,lastJob.end,positions);
            const QString detail=QString("%1 · %2").arg(group.count).arg(pgn::ui::bookingRailState(m_bookingRailSegments[group.first].job.state));
            painter.save();painter.setClipRect(group.area,Qt::IntersectClip);
            painter.setPen(QPen(QColor("#69efc5"),1));painter.setBrush(QColor(3,24,31,185));painter.drawRoundedRect(group.area.adjusted(.5,.5,-.5,-.5),4,4);
            painter.setFont(cellFont);painter.setPen(QColor("#dbf8f2"));
            painter.drawText(group.area.adjusted(5,1,-5,-17),Qt::AlignCenter,metrics.elidedText(title,Qt::ElideRight,int(group.area.width()-10)));
            QFont font("Segoe UI");font.setPixelSize(10);painter.setFont(font);painter.setPen(QColor("#91c4cc"));
            painter.drawText(group.area.adjusted(5,18,-5,-2),Qt::AlignCenter,painter.fontMetrics().elidedText(detail,Qt::ElideRight,int(group.area.width()-10)));
            painter.restore();groupTitles.append(title+" · "+detail);
        }
        setProperty("pgn_booking_rail_group_titles",groupTitles);
        setProperty("pgn_booking_rail_visible_ids",visibleIds);setProperty("pgn_booking_rail_visible_titles",visibleTitles);
        setProperty("pgn_booking_rail_visible_details",visibleDetails);
        setProperty("pgn_booking_rail_visual_fractions",visibleFractions);
        setProperty("pgn_booking_rail_observed_fractions",observedFractions);
    }
    void drawLogRuler(QPainter& painter,
                      qreal left,
                      qreal right,
                      qreal y)
    {
        if(m_pgnBookingModel){const qreal railHeight=std::max(68.,32.+bookingOverview(right-left-28).height);
            drawBookingRail(painter,QRectF(left+2,y+1,right-left-4,railHeight));return;}
        const QColor cyan(QStringLiteral("#46c8ee"));
        const QColor text(QStringLiteral("#dbeceb"));
        const QVector<const FrontierTelemetrySnapshot*> lanes = minerLanes();
        const FrontierTelemetrySnapshot* liveLane = nullptr;
        for (const FrontierTelemetrySnapshot* lane : lanes) {
            if (minerLaneLive(*lane)) {
                liveLane = lane;
                break;
            }
        }

        std::optional<int> liveBits;
        std::optional<int> liveShift;
        if (liveLane) {
            liveBits = liveLane->numberBits;
            liveShift = liveLane->computeShift;
        }

        if (m_legacyR4Mode) {
            const QColor mint(QStringLiteral("#69efc5"));
            const QRectF area(left + 2, y + 1, right - left - 4, 36);
            painter.setPen(QPen(QColor(48, 110, 120, 170), 1));
            painter.setBrush(QColor(3, 24, 31, 235));
            painter.drawRoundedRect(area, 8, 8);
            constexpr int minimumShift = 14;
            constexpr int maximumShift = 1024;
            const auto shiftX = [area](int shift) {
                const double ratio = (std::log2(static_cast<double>(shift)) - std::log2(14.0)) /
                    (std::log2(1024.0) - std::log2(14.0));
                return area.left() + 12.0 + std::clamp(ratio, 0.0, 1.0) *
                    (area.width() - 24.0);
            };
            const std::array<int, 8> ticks{14, 32, 64, 128, 256, 512, 768, 1024};
            for (const int shift : ticks) {
                const qreal x = shiftX(shift);
                painter.setPen(QPen(QColor(80, 141, 149, 145), 1));
                painter.drawLine(QPointF(x, area.top() + 13), QPointF(x, area.bottom() - 4));
                painter.setFont(Font(7, QFont::DemiBold, true));
                painter.setPen(QColor(QStringLiteral("#8bb8c5")));
                painter.drawText(QRectF(x - 24, area.top() + 1, 48, 11),
                                 Qt::AlignCenter, QString::number(shift));
            }
            QHash<int, int> activeShiftCounts;
            QHash<int, QStringList> activeShiftResources;
            QHash<int, QVector<QColor>> activeShiftColors;
            for (const FrontierTelemetrySnapshot* lane : lanes) {
                if (!lane || !minerLaneLive(*lane) || !lane->computeShift) continue;
                const int shift = std::clamp(*lane->computeShift, minimumShift, maximumShift);
                activeShiftCounts[shift] += 1;
                const bool cpu = lane->engineId.contains(QStringLiteral("cpu"),
                                                         Qt::CaseInsensitive);
                QString identity = cpu ? QStringLiteral("CPU") : QStringLiteral("GPU");
                if (!lane->gpuName.trimmed().isEmpty()) {
                    identity += QStringLiteral(" · %1").arg(lane->gpuName.trimmed());
                }
                if (!cpu && !lane->gpuUuid.isEmpty()) {
                    identity += QStringLiteral(" · UUID …%1").arg(lane->gpuUuid.right(8));
                }
                activeShiftResources[shift].append(identity);
                const QString stableIdentity = lane->gpuUuid.isEmpty()
                    ? lane->runId : lane->gpuUuid;
                activeShiftColors[shift].append(deviceIdentityColor(
                    stableIdentity, cpu ? QStringLiteral("CPU") : QStringLiteral("GPU")));
            }
            struct ShiftMarker {
                int shift{};
                int resources{};
                qreal x{};
                QStringList identities;
                QVector<QColor> colors;
            };
            QVector<ShiftMarker> markers;
            for (auto iterator = activeShiftCounts.cbegin(); iterator != activeShiftCounts.cend(); ++iterator) {
                markers.append({iterator.key(), iterator.value(), shiftX(iterator.key()),
                                activeShiftResources.value(iterator.key()),
                                activeShiftColors.value(iterator.key())});
            }
            std::sort(markers.begin(), markers.end(), [](const ShiftMarker& a, const ShiftMarker& b) {
                return a.x < b.x;
            });
            // Preserve an exact hairline for every active shift.  Labels are
            // then collision-clustered in screen space, so dozens of devices
            // cannot overwrite one another or become invisible.
            for (const ShiftMarker& marker : markers) {
                const int colorCount = std::max(1, static_cast<int>(marker.colors.size()));
                const qreal totalWidth = std::min<qreal>(8.0, colorCount * 1.8);
                for (int colorIndex = 0; colorIndex < colorCount; ++colorIndex) {
                    const QColor color = marker.colors.isEmpty()
                        ? mint : marker.colors.at(colorIndex);
                    const qreal offset = colorCount == 1 ? 0.0
                        : -totalWidth / 2.0 + (colorIndex + 0.5) * totalWidth / colorCount;
                    painter.setPen(QPen(Alpha(color, 220), 1.5));
                    painter.drawLine(QPointF(marker.x + offset, area.top() + 12),
                                     QPointF(marker.x + offset, area.bottom() - 1));
                }
            }
            struct MarkerCluster {
                QVector<ShiftMarker> members;
                qreal center{};
                int resources{};
            };
            QVector<MarkerCluster> clusters;
            for (const ShiftMarker& marker : markers) {
                if (clusters.isEmpty() || marker.x - clusters.back().center > 46.0) {
                    clusters.append({{marker}, marker.x, marker.resources});
                } else {
                    MarkerCluster& cluster = clusters.back();
                    const int previousResources = cluster.resources;
                    cluster.resources += marker.resources;
                    cluster.center = (cluster.center * previousResources + marker.x * marker.resources) /
                        std::max(1, cluster.resources);
                    cluster.members.append(marker);
                }
            }
            for (const MarkerCluster& cluster : clusters) {
                const qreal halfWidth = std::min<qreal>(31.0,
                    11.0 + std::log2(cluster.resources + 1.0) * 4.0);
                const QRectF active(cluster.center - halfWidth, area.top() + 13,
                                    halfWidth * 2.0, area.height() - 15);
                QVector<QColor> clusterColors;
                for (const ShiftMarker& member : cluster.members) {
                    clusterColors += member.colors;
                }
                if (clusterColors.isEmpty()) clusterColors.append(mint);
                QLinearGradient identityGradient(active.topLeft(), active.topRight());
                for (int colorIndex = 0; colorIndex < clusterColors.size(); ++colorIndex) {
                    const qreal position = clusterColors.size() == 1 ? 0.0
                        : colorIndex / static_cast<qreal>(clusterColors.size() - 1);
                    identityGradient.setColorAt(position, Alpha(clusterColors.at(colorIndex), 180));
                }
                painter.setBrush(identityGradient);
                painter.setPen(QPen(clusterColors.constFirst(), 2));
                painter.drawRoundedRect(active, 6, 6);
                painter.setFont(Font(7, QFont::DemiBold, true));
                painter.setPen(QColor(QStringLiteral("#ffffff")));
                const QString label = cluster.members.size() == 1
                    ? (cluster.resources > 1
                           ? QStringLiteral("%1×%2").arg(cluster.resources).arg(cluster.members.first().shift)
                           : QString::number(cluster.members.first().shift))
                    : QStringLiteral("%1 DEV").arg(cluster.resources);
                painter.drawText(active, Qt::AlignCenter, label);
                QStringList exactShifts;
                QStringList identities;
                for (const ShiftMarker& member : cluster.members) {
                    exactShifts.append(member.resources > 1
                        ? QStringLiteral("%1×%2").arg(member.resources).arg(member.shift)
                        : QString::number(member.shift));
                    for (const QString& identity : member.identities) {
                        identities.append(QStringLiteral("SHIFT %1 · %2")
                                              .arg(member.shift).arg(identity));
                    }
                }
                m_uiTooltipHits.append({active,
                    QStringLiteral("%1 ACTIVE RESOURCES · EXACT SHIFTS %2\n%3")
                        .arg(cluster.resources)
                        .arg(exactShifts.join(QStringLiteral(", ")),
                             identities.join(QLatin1Char('\n')))});
            }
            m_uiTooltipHits.append({area,
                QStringLiteral("Search geometry rail · no network consensus geometry is inferred")});
            return;
        }

        painter.setFont(Font(10, QFont::DemiBold, true));
        painter.setPen(cyan);
        painter.drawText(QRectF(left, y - 29, 300, 18),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("SEARCH SHIFT RANGES / NUMBER SIZE"));
        if (liveLane && FrontierHasExactLiveGeometry(liveBits, liveShift)) {
            QStringList emitted;
            if (liveBits) emitted.append(QStringLiteral("%1 BITS").arg(*liveBits));
            if (liveShift) emitted.append(QStringLiteral("SHIFT %1").arg(*liveShift));
            const bool benchmarkGeometry = liveLane->sourceMode == QLatin1String("benchmark");
            const QString geometryState = QStringLiteral("%1 · %2")
                .arg(benchmarkGeometry ? QStringLiteral("BENCHMARK") : QStringLiteral("LIVE"))
                .arg(emitted.join(QStringLiteral(" · ")));
            const QRectF stateRect(right - 360, y - 29, 360, 18);
            painter.setBrush(QColor(3, 25, 31, 235));
            painter.setPen(QPen(Alpha(cyan, 135), 1));
            painter.drawRoundedRect(stateRect, 8, 8);
            painter.setFont(Font(8, QFont::DemiBold, true));
            painter.setPen(text);
            painter.drawText(stateRect.adjusted(8, 0, -8, 0), Qt::AlignCenter, geometryState);
            m_uiTooltipHits.append({stateRect,
                benchmarkGeometry
                    ? QStringLiteral("number_bits and compute_shift carried by the active benchmark adapter; not scientific live-miner telemetry")
                    : QStringLiteral("Exact number_bits/bit_length and compute_shift emitted by the active miner")});
        }

        painter.setPen(QPen(QColor(48, 110, 120, 125), 1));
        painter.drawLine(QPointF(left, y), QPointF(right, y));
        const std::array<QColor, 5> bandColors{
            QColor(QStringLiteral("#69efc5")), QColor(QStringLiteral("#46c8ee")),
            QColor(QStringLiteral("#b58cff")), QColor(QStringLiteral("#ffd166")),
            QColor(QStringLiteral("#ff8f70"))};
        const qreal legendWidth = (right - left) / static_cast<qreal>(FrontierShiftBands.size());
        for (std::size_t index = 0; index < FrontierShiftBands.size(); ++index) {
            const FrontierShiftBand& band = FrontierShiftBands.at(index);
            const QColor& bandColor = bandColors.at(index);
            const bool liveActive = liveShift && *liveShift >= band.from &&
                *liveShift <= band.to;
            const QRectF area(left + index * legendWidth + 2, y + 8,
                              legendWidth - 4, 21);
            painter.setPen(QPen(Alpha(bandColor, liveActive ? 255 : 110),
                                liveActive ? 2.3 : 1));
            painter.setBrush(Alpha(bandColor, liveActive ? 125 : 22));
            painter.drawRoundedRect(area, 7, 7);
            painter.setFont(Font(8, QFont::DemiBold, true));
            painter.setPen(liveActive ? QColor(QStringLiteral("#ffffff")) : bandColor);
            const bool benchmarkActive = liveActive && liveLane &&
                liveLane->sourceMode == QLatin1String("benchmark");
            const QString suffix = liveActive
                ? (benchmarkActive ? QStringLiteral(" · BENCH") : QStringLiteral(" · LIVE"))
                : QString{};
            const QString label = QString::fromLatin1(band.label);
            painter.drawText(area, Qt::AlignCenter, label + suffix);
            const QString tooltip = liveActive
                ? (benchmarkActive
                       ? QStringLiteral("Benchmark adapter carried compute_shift %1 inside informational range %2; number bits: %3")
                       : QStringLiteral("Active miner emitted compute_shift %1 inside informational range %2; exact number bits: %3"))
                      .arg(*liveShift)
                      .arg(label)
                      .arg(liveBits ? QString::number(*liveBits) : QStringLiteral("unavailable"))
                : QStringLiteral("%1 is an informational/filter range, not an engine restriction")
                      .arg(label);
            m_uiTooltipHits.append({area, tooltip});
        }
    }

    void drawPrimeGapInterval(QPainter& painter,
                              qreal left,
                              qreal right,
                              qreal y,
                              const FrontierPrimeGapVisualState& visual,
                              FrontierPrimeGapSignalKind kind,
                              const CandidatePoint* event)
    {
        painter.save();
        const QColor idle(QStringLiteral("#5b8785"));
        const QColor candidate(QStringLiteral("#ffad4a"));
        const QColor exact(QStringLiteral("#75ff9f"));
        const qreal start = left + 35;
        const qreal finish = right - 35;
        const qreal midpoint = (start + finish) / 2.0;
        const QColor bracketColor = visual.exactLocked ? exact : idle;
        painter.setPen(QPen(Alpha(bracketColor,
                                  qRound(75.0 * visual.boundaryIntensity)), 1.2,
                                  Qt::DotLine, Qt::RoundCap));
        painter.drawLine(QPointF(start, y), QPointF(finish, y));
        painter.setPen(QPen(Alpha(bracketColor,
                                  qRound(115.0 * visual.boundaryIntensity)), 1.2));
        painter.drawLine(QPointF(start, y - 6), QPointF(start, y + 4));
        painter.drawLine(QPointF(finish, y - 6), QPointF(finish, y + 4));
        if (!m_legacyR4Mode) {
            painter.setFont(Font(7, QFont::DemiBold, true));
            painter.setPen(Alpha(bracketColor, qRound(155.0 * visual.boundaryIntensity)));
            painter.drawText(QRectF(midpoint - 72, y - 30, 144, 14),
                             Qt::AlignCenter, QStringLiteral("Δ = q − p"));
        }

        if (visual.transferVisible) {
            const qreal head = start + (finish - start) * visual.transferProgress;
            painter.setPen(QPen(Alpha(candidate, 42), 8, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(QPointF(start, y), QPointF(head, y));
            painter.setPen(QPen(Alpha(candidate, 235), 2.2, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(QPointF(start, y), QPointF(head, y));
            painter.setPen(Qt::NoPen);
            painter.setBrush(candidate);
            painter.drawEllipse(QPointF(head, y), 3.2, 3.2);
        }

        QString label = QStringLiteral("SEARCHING THE VOID BETWEEN p AND q");
        QColor labelColor = idle;
        if (visual.exactLocked && event) {
            label = QStringLiteral("EXACT Δ %1 · MERIT %2")
                        .arg(QString::number(event->gap, 'f', 0),
                             QString::number(event->merit, 'f', 6));
            labelColor = exact;
        } else if (visual.transferVisible && event) {
            label = QStringLiteral("CANDIDATE Δ %1 · MERIT %2")
                        .arg(QString::number(event->gap, 'f', 0),
                             QString::number(event->merit, 'f', 6));
            labelColor = candidate;
        } else if (kind == FrontierPrimeGapSignalKind::Rejected && visual.rejectedFade) {
            // Rejection deliberately emits no result label: the interval only
            // fades, then returns to its static idle state.
            label.clear();
        }
        if (!label.isEmpty()) {
            const QRectF badge(midpoint - 126, y - 9, 252, 18);
            painter.setBrush(QColor(2, 14, 21, 238));
            painter.setPen(QPen(Alpha(labelColor, 150), 1));
            painter.drawRoundedRect(badge, 8, 8);
            painter.setFont(Font(8, QFont::DemiBold, true));
            painter.setPen(Alpha(labelColor, 225));
            painter.drawText(badge, Qt::AlignCenter, label);
        }
        painter.restore();
    }

    void drawBoundary(QPainter& painter,
                      const QPointF& point,
                      const QColor& color,
                      const QString& symbol,
                      const QString& label,
                      qreal intensity,
                      bool locked,
                      bool compact=false)
    {
        painter.save();
        intensity = std::clamp(intensity, 0.0, 1.0);
        const qreal outer=compact?25:31;
        painter.setPen(QPen(Alpha(color, qRound(35 * intensity)), compact?10:15));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(point, outer, outer);
        painter.setPen(QPen(Alpha(color, qRound(120 * intensity)), 2));
        painter.drawEllipse(point, compact?20:25, compact?20:25);
        const QVector<int> wheelResidues{1, 7, 11, 13, 17, 19, 23, 29};
        painter.setPen(QPen(Alpha(color, qRound(85 * intensity)), 1.2,
                            Qt::SolidLine, Qt::RoundCap));
        for (int residue : wheelResidues) {
            const double angle = (-90.0 + residue * 12.0) *
                3.14159265358979323846 / 180.0;
            const QPointF direction(std::cos(angle), std::sin(angle));
            painter.drawLine(point + direction * outer, point + direction * (outer+3));
        }
        painter.setPen(QPen(Alpha(color, qRound(255 * intensity)), locked ? 2.8 : 2));
        painter.setBrush(QColor(4, 23, 27, 245));
        painter.drawEllipse(point, 14, 14);
        if (locked) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(color, 2, Qt::SolidLine, Qt::SquareCap));
            constexpr qreal inner = 21;
            constexpr qreal outer = 29;
            painter.drawLine(QPointF(point.x() - outer, point.y() - inner),
                             QPointF(point.x() - outer, point.y() - outer));
            painter.drawLine(QPointF(point.x() - outer, point.y() - outer),
                             QPointF(point.x() - inner, point.y() - outer));
            painter.drawLine(QPointF(point.x() + inner, point.y() - outer),
                             QPointF(point.x() + outer, point.y() - outer));
            painter.drawLine(QPointF(point.x() + outer, point.y() - outer),
                             QPointF(point.x() + outer, point.y() - inner));
        }
        painter.setFont(Font(17, QFont::DemiBold, true));
        painter.setPen(Alpha(color, qRound(255 * intensity)));
        painter.drawText(QRectF(point.x() - 14, point.y() - 14, 28, 28), Qt::AlignCenter, symbol);
        painter.setFont(Font(compact?7:9, QFont::DemiBold));
        const qreal labelWidth=painter.fontMetrics().horizontalAdvance(label)+8;
        painter.drawText(QRectF(point.x()-labelWidth/2,point.y()+(compact?32:37),labelWidth,14),Qt::AlignCenter,label);
        painter.restore();
    }

    void drawRail(QPainter& painter,
                  qreal left,
                  qreal right,
                  qreal y,
                  const QColor& color,
                  const QString& name,
                  const QString& state,
                  bool active,
                  qreal phase,
                  bool annotationBelow)
    {
        painter.save();
        painter.setPen(QPen(Alpha(color, active ? 160 : 72), 2));
        painter.drawLine(QPointF(left + 35, y), QPointF(right - 35, y));
        painter.setPen(QPen(Alpha(color, 95), 1));
        for (int i = 1; i < 24; ++i) {
            const qreal x = left + 35 + i * ((right - left - 70) / 24.0);
            painter.drawLine(QPointF(x, y - 4), QPointF(x, y + 4));
        }
        if (active) {
            const qreal x = left + 35 + phase * (right - left - 70);
            painter.setPen(QPen(Alpha(color, 38), 13));
            painter.drawEllipse(QPointF(x, y), 4, 4);
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawEllipse(QPointF(x, y), 4, 4);
        } else {
            painter.setPen(QPen(Alpha(color, 125), 2));
            painter.drawLine(QPointF(left + 45, y - 6), QPointF(left + 57, y + 6));
            painter.drawLine(QPointF(left + 57, y - 6), QPointF(left + 45, y + 6));
        }
        const qreal annotationLeft = left + (m_legacyR4Mode && height()<900 ? 64 : 210);
        const qreal annotationRight = right - (m_legacyR4Mode && height()<900 ? 64 : 210);
        const qreal annotationY = m_legacyR4Mode && height()<900 ? y+6 : (annotationBelow ? y + 6 : y - 30);
        painter.setFont(Font(9, QFont::DemiBold));
        painter.setPen(color);
        painter.drawText(QRectF(annotationLeft, annotationY, 155, 15),
                         Qt::AlignLeft | Qt::AlignVCenter, name);
        painter.setFont(Font(8, QFont::Normal, true));
        painter.setPen(Alpha(color, 165));
        const QRectF stateRect(annotationLeft+165,annotationY,std::max<qreal>(0,annotationRight-annotationLeft-165),15);
        const auto shown=painter.fontMetrics().elidedText(state,Qt::ElideMiddle,int(stateRect.width()));
        painter.drawText(stateRect,Qt::AlignRight|Qt::AlignVCenter,shown);
        if(shown!=state||state.startsWith(QStringLiteral("LOCAL "))){
            const auto hit=stateRect.width()>0?stateRect:QRectF(annotationLeft,annotationY,155,15);
            m_uiTooltipHits.append({hit,name+"\n"+state+(state.startsWith(QStringLiteral("LOCAL "))?
                QStringLiteral("\nLocal supplied-pair proofs only. No interval coverage, ticket, network acceptance or reward."):QString{})});
        }
        painter.restore();
    }

    void drawProducerRail(
        QPainter& painter,
        qreal left,
        qreal right,
        qreal y,
        const QVector<const FrontierTelemetrySnapshot*>& lanes,
        const QString& state)
    {
        const QColor mint(QStringLiteral("#69efc5"));
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const qint64 elapsedMs = m_lastProducerAnimationMs > 0
            ? std::clamp<qint64>(nowMs - m_lastProducerAnimationMs, 0, 100)
            : 0;
        m_lastProducerAnimationMs = nowMs;

        struct ProducerDot {
            const FrontierTelemetrySnapshot* lane{};
            QString id;
            QColor color;
            std::optional<double> rate;
        };
        QVector<ProducerDot> dots;
        double maxRate = 0.0;
        bool benchmarkOnly = true;
        for (const FrontierTelemetrySnapshot* lane : lanes) {
            if (!minerLaneLive(*lane)) continue;
            const bool benchmark = lane->sourceMode == QLatin1String("benchmark");
            benchmarkOnly = benchmarkOnly && benchmark;
            const std::optional<double> rate = benchmark
                ? FrontierFirstEmittedProducerRate({lane->gapPairsTestedPerSecond})
                : (m_legacyR4Mode
                       ? FrontierFirstEmittedProducerRate(
                             {lane->gapPairsWindowPerSecond,
                              lane->gapPairsTestedPerSecond,
                              lane->powmPerSecond})
                       : FrontierFirstEmittedProducerRate(
                             {lane->e2eWindowPositionsPerSecond}));
            if (rate && std::isfinite(*rate) && *rate > 0.0) maxRate = std::max(maxRate, *rate);
            const bool cpu = lane->engineId.contains(QStringLiteral("cpu"),
                                                     Qt::CaseInsensitive);
            const QString identity = lane->gpuUuid.isEmpty() ? lane->runId : lane->gpuUuid;
            dots.append({lane,
                         identity,
                         deviceIdentityColor(identity,
                             cpu ? QStringLiteral("CPU") : QStringLiteral("GPU")),
                         rate && std::isfinite(*rate) && *rate > 0.0 ? rate : std::nullopt});
        }

        painter.save();
        painter.setPen(QPen(Alpha(mint, dots.isEmpty() ? 72 : 160), 2));
        painter.drawLine(QPointF(left + 35, y), QPointF(right - 35, y));
        painter.setPen(QPen(Alpha(mint, 95), 1));
        for (int i = 1; i < 24; ++i) {
            const qreal x = left + 35 + i * ((right - left - 70) / 24.0);
            painter.drawLine(QPointF(x, y - 4), QPointF(x, y + 4));
        }
        for (int index = 0; index < dots.size(); ++index) {
            const ProducerDot& dot = dots.at(index);
            if (!m_producerPhases.contains(dot.id)) {
                m_producerPhases.insert(dot.id, dots.size() > 1
                    ? static_cast<qreal>(index) / dots.size() : 0.0);
            }
            const double ratio = dot.rate
                ? FrontierProducerRelativeSpeed(*dot.rate, maxRate) : 0.0;
            qreal phase = m_producerPhases.value(dot.id);
            phase = FrontierAdvanceProducerPhase(phase, ratio, elapsedMs);
            m_producerPhases.insert(dot.id, phase);
            const qreal x = left + 35 + phase * (right - left - 70);
            const bool eventPulse = deviceHasFreshGapPulse(dot.id, nowMs);
            if (eventPulse) {
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(Alpha(dot.color, 80 + qRound(90 *
                    (0.5 + 0.5 * std::sin(m_phase * 18.84955592)))), 7));
                painter.drawEllipse(QPointF(x, y), 8, 8);
            }
            painter.setPen(QPen(QColor(2, 12, 20, 230), 1));
            painter.setBrush(dot.rate ? dot.color : Alpha(dot.color, 95));
            const bool secondary = dot.lane == &m_secondaryTelemetry;
            if (secondary) {
                QPolygonF diamond;
                diamond << QPointF(x, y - 5) << QPointF(x + 5, y)
                        << QPointF(x, y + 5) << QPointF(x - 5, y);
                painter.drawPolygon(diamond);
            } else {
                painter.drawEllipse(QPointF(x, y), 5, 5);
            }
            const bool benchmark = dot.lane->sourceMode == QLatin1String("benchmark");
            const QString rateText = dot.rate
                ? (benchmark
                       ? QStringLiteral("%1 K pair checks/s").arg(*dot.rate / 1.0e3, 0, 'f', 1)
                       : QStringLiteral("%1 M pos/s").arg(*dot.rate / 1.0e6, 0, 'f', 2))
                : QStringLiteral("AWAITING RATE");
            m_uiTooltipHits.append({QRectF(x - 11, y - 11, 22, 22),
                QStringLiteral("%1 · %2 · RELATIVE SPEED %3×")
                    .arg(dot.lane->gpuName.isEmpty() ? dot.id : dot.lane->gpuName,
                         rateText)
                    .arg(ratio, 0, 'f', 2)});
        }
        if (dots.isEmpty()) {
            painter.setPen(QPen(Alpha(mint, 125), 2));
            painter.drawLine(QPointF(left + 45, y - 6), QPointF(left + 57, y + 6));
            painter.drawLine(QPointF(left + 57, y - 6), QPointF(left + 45, y + 6));
        }
        const qreal annotationLeft = left + (m_legacyR4Mode && height()<900 ? 64 : 210);
        const qreal annotationRight = right - (m_legacyR4Mode && height()<900 ? 64 : 210);
        const bool compactAnnotation=m_pgnBookingModel||(m_legacyR4Mode && height()<900);
        const QRectF annotationBand(annotationLeft - 8, y - (compactAnnotation ? 9 : 27),
                                    annotationRight - annotationLeft + 16, 19);
        if(m_pgnBookingModel)setProperty("pgn_booking_producer_annotation",annotationBand);
        painter.setPen(QPen(Alpha(mint, 72), 1));
        painter.setBrush(QColor(1, 14, 21, 238));
        painter.drawRoundedRect(annotationBand, 7, 7);
        painter.setFont(Font(9, QFont::DemiBold));
        painter.setPen(mint);
        painter.drawText(QRectF(annotationLeft, y - (compactAnnotation ? 7 : 25), 155, 15),
                         Qt::AlignLeft | Qt::AlignVCenter,
                          benchmarkOnly && !dots.isEmpty()
                              ? QStringLiteral("BENCHMARK RAIL")
                              : QStringLiteral("PRODUCER RAIL"));
        painter.setFont(Font(8, QFont::Normal, true));
        painter.setPen(Alpha(mint, 165));
        painter.drawText(QRectF(annotationLeft + 165, y - (compactAnnotation ? 7 : 25),
                                annotationRight - annotationLeft - 165, 15),
                         Qt::AlignRight | Qt::AlignVCenter, state);
        painter.restore();
    }

    void drawVaultMaterial(QPainter& painter,
                           const QRectF& scene,
                           qreal left,
                           qreal right,
                           qreal top)
    {
        const QColor mint(QStringLiteral("#69efc5"));
        // This retained presentation has no qualified PGN capability backend.
        // Unavailable is not a claim that the eventual wallet lacks a feature.
        const bool helpOk=false;
        const bool mentionsVault=false;
        const qreal bottom = scene.bottom() - 17;
        painter.save();
        painter.setClipRect(QRectF(left, top, right - left, std::max(0.0, bottom - top)));
        painter.setPen(QPen(QColor(62, 143, 129, 65), 1));
        painter.setBrush(Qt::NoBrush);
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 56; ++column) {
                const qreal x = left + 22 + column * ((right - left - 44) / 55.0);
                const qreal y = top + 19 + row * 12;
                QPolygonF grain;
                grain << QPointF(x, y - 2.2) << QPointF(x + 2.2, y) << QPointF(x, y + 2.2)
                      << QPointF(x - 2.2, y);
                painter.drawPolygon(grain);
            }
        }
        painter.setClipping(false);
        painter.setFont(Font(8, QFont::DemiBold));
        painter.setPen(Alpha(mint, 160));
        const bool telemetryLive = m_hasTelemetry && m_telemetry.authoritative && !m_telemetry.stale;
        const QString state = telemetryLive
                                  ? QStringLiteral("SCIENTIFIC MATERIAL / BEST SPAN %1 / EXACT QUEUE %2 / CERTIFICATE %3")
                                        .arg(m_telemetry.bestSpan.isEmpty() ? QStringLiteral("NOT EMITTED")
                                                                           : m_telemetry.bestSpan,
                                             m_telemetry.exactPending.isEmpty() ? QStringLiteral("NOT EMITTED")
                                                                                : m_telemetry.exactPending,
                                             m_telemetry.certificateStatus.toUpper())
                                  : (mentionsVault
                                         ? QStringLiteral("VAULT COMMAND DETECTED / MATERIAL COUNTS NOT YET BOUND")
                                         : (helpOk
                                                ? QStringLiteral("VAULT RPC NOT EXPOSED / EMPTY LATTICE, NO FABRICATED COUNT")
                                                : QStringLiteral("VAULT SURFACE UNKNOWN / RPC OFFLINE")));
        painter.drawText(QRectF(left + 35, top + 54, right - left - 70, 15), Qt::AlignCenter, state);
        painter.restore();
    }

    void drawBottomTelemetry(QPainter& painter)
    {
        const QRectF strip(0, height() - 18, width(), 18);
        painter.fillRect(strip, QColor(1, 8, 13, 235));
        painter.setPen(QPen(QColor(36, 87, 96, 155), 1));
        painter.drawLine(QPointF(0, strip.top() + 0.5), QPointF(width(), strip.top() + 0.5));
        painter.setFont(Font(8, QFont::Normal, true));
        painter.setPen(QColor(QStringLiteral("#6f918f")));
        const QString cliName = QFileInfo(m_cliPath).fileName().isEmpty()
            ? QStringLiteral("NOT CONFIGURED")
            : QFileInfo(m_cliPath).fileName();
        // The unified app does not require the retired legacy CLI. This is
        // LOCAL runtime state, never a peer/wallet/GPU connectivity assertion.
        const QString executionCaption=property("pgn_relay_runtime_source").toString()==QStringLiteral("native-in-process")
            ? (property("pgn_native_runtime_started").toBool()?QStringLiteral("UNIFIED NATIVE APP"):QStringLiteral("NATIVE APP UNAVAILABLE"))
            : QStringLiteral("CLI %1").arg(cliName);
        setProperty("pgn_execution_footer",executionCaption);
        QString verifierFooter;
        bool footerStructured=m_hasVerifierTelemetry;
        if (m_legacyR4Mode) {
            int validated = 0;
            int pending = 0;
            int chainErrors = 0;
            bool liveMiner = false;
            for (const FrontierTelemetrySnapshot* lane : minerLanes()) {
                if (!lane) continue;
                liveMiner = liveMiner || minerLaneLive(*lane);
                validated += lane->raw.value(
                    QStringLiteral("legacy_verifier_host_validated")).toInt(0);
                pending += lane->raw.value(
                    QStringLiteral("legacy_verifier_pending_canonical")).toInt(0);
                chainErrors += lane->raw.value(
                    QStringLiteral("legacy_verifier_chain_errors")).toInt(0);
            }
            verifierFooter = chainErrors > 0
                // Existing real chain/host evidence always takes precedence.
                ? QStringLiteral("ERROR %1").arg(chainErrors)
                : validated > 0
                    ? QStringLiteral("HOST EXACT %1%2")
                          .arg(validated)
                          .arg(pending > 0
                                   ? QStringLiteral(" · NODE PENDING %1").arg(pending)
                                   : QString{})
                    : liveMiner ? QStringLiteral("WAITING FOR FIRST GAP")
                                : QStringLiteral("IDLE");
            footerStructured=footerStructured||validated>0||pending>0||chainErrors>0;
        } else {
            verifierFooter = m_hasVerifierTelemetry &&
                    verifierDisplayState(m_verifierTelemetry) ==
                        FrontierVerifierDisplayState::Online
                ? QStringLiteral("ONLINE")
                : (m_hasVerifierTelemetry &&
                           verifierDisplayState(m_verifierTelemetry) ==
                               FrontierVerifierDisplayState::SealedArchive
                       ? QStringLiteral("ARCHIVE 2/2")
                       : QStringLiteral("OFFLINE"));
        }
        QSet<QString> localActiveRuns;for(const auto* lane:minerLanes())if(lane&&minerLaneLive(*lane))localActiveRuns.insert(lane->runId);
        const auto localProof=pgn::ui::localSourceProofPresentation(property("pgn_local_source_proof").value<QJsonObject>(),localActiveRuns);
        if(!footerStructured&&localProof.present)verifierFooter=localProof.caption;
        const bool labCrtTest = m_telemetry.raw.value(
            QStringLiteral("lab_highshift")).toBool(false);
        if (labCrtTest)
            verifierFooter = m_hasTelemetry && minerLaneLive(m_telemetry)
                ? QStringLiteral("LAB EXACT GAP WAITING")
                : QStringLiteral("LAB TEST WAITING");
        // The footer and rail share one PGN historical-evidence state. Do not
        // overwrite real verifier errors or live node/host acknowledgements.
        const auto historicalProof=pgn::ui::historicalProofPresentation(
            property("pgn_independent_verification_receipt").value<QJsonObject>(),
            property("pgn_independent_verification_error").toString());
        // The archived receipt stays available in Exact records only.
        const FrontierGpuControllerUi::Snapshot& controller = m_gpuControllerSnapshot;
        const QString controllerFooter = m_deviceControlHandler
            ? QStringLiteral("NATIVE GPU ON/OFF CONNECTED") : labCrtTest
            ? QStringLiteral("EXTERNAL CRT TEST · GPU READ-ONLY")
            : !controller.configured
            ? QStringLiteral("GPU CTRL OFF")
            : !controller.configValid || controller.liveMutationViolation
                ? QStringLiteral("GPU CTRL FAIL-CLOSED")
                : QStringLiteral("GPU CTRL 82–84 C DRY-RUN");
        const QString footer = QStringLiteral(
            "HOST %1  ·  %2  ·  %3 ACTIVE STREAMS  ·  VERIFIER %4  ·  %5")
            .arg(m_host.isEmpty() ? QStringLiteral("UNAVAILABLE") : m_host,
                 executionCaption,
                 QString::number(minerLanes().size()),
                 verifierFooter,
                 controllerFooter);
        painter.drawText(QRectF(20, strip.top() + 1, width() - 40, 16),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetrics(painter.font()).elidedText(
                             footer, Qt::ElideRight, qFloor(width() - 40)));
        m_uiTooltipHits.append({strip, historicalProof.tooltip +
            (historicalProof.tooltip.isEmpty()?QString{}:QStringLiteral(" · "))+controller.controllerSummary() +
            (controller.error.isEmpty() ? QString{} : QStringLiteral(" · %1").arg(controller.error))});
    }

    QString m_cliPath;
    QString m_host;
    QPixmap m_logo;
    QTimer m_animation;
    std::optional<pgn::wallet::ChainObservation> m_chainObservation;
    FrontierTelemetrySnapshot m_telemetry;
    bool m_hasTelemetry{false};
    FrontierTelemetrySnapshot m_secondaryTelemetry;
    bool m_hasSecondaryTelemetry{false};
    QHash<QString, FrontierTelemetrySnapshot> m_additionalTelemetry;
    FrontierTelemetrySnapshot m_verifierTelemetry;
    bool m_hasVerifierTelemetry{false};
    FrontierResourceTelemetrySnapshot m_resourceTelemetry;
    bool m_hasResourceTelemetry{false};
    FrontierTargetCurveLoadResult m_targetCurve;
    QHash<QString, QVector<LaneSample>> m_resourceHistories;
    HorizonDifficultyMode* m_difficultyMode{};
    QVector<QPair<qint64, double>> m_legacyDifficulty;
    QVector<QPair<qint64, qint64>> m_legacyDifficultyCoverage;
    QHash<QString, PendingGapEvent> m_pendingGapEvents;
    bool m_resolvingPendingGapEvents{false};
    int m_graphDataBatchDepth{0};
    bool m_graphDataBatchDirty{false};
    bool m_graphDataBatchNeedsSort{false};
    QHash<QString, qint64> m_legacyRunStartedUtcMs;
    qint64 m_lastLegacyRunStartedUtcMs{-1};
    QHash<QString,pgn::ui::RateGaugeScale> m_rateGaugeScales;QHash<QString,QString> m_rateGaugeEpochs;QVector<QPair<QString,QRectF>> m_totalGroupHits;
    QHash<QString, double> m_devicePositionRatePeaks;
    QHash<QString, double> m_devicePairRatePeaks;
    QHash<QString, pgn::ui::GapDisplayFilter> m_gapDisplayFilters;
    pgn::ui::BlockEtaEstimator m_blockEta;
    pgn::ui::TotalCardRateWindow m_pgnTotalRateWindow;
    mutable QHash<QString, QColor> m_deviceIdentityColors;
    QHash<QString, DeviceControlState> m_deviceControls;
    QHash<QString, bool> m_collapsedDevices;
    QHash<QString, qulonglong> m_sessionCandidateBaselines;
    qulonglong m_exactReceiptInventory{0};
    QVector<DeviceControlHit> m_deviceControlHits;
    QVector<DeviceCollapseHit> m_deviceCollapseHits;
    QVector<UiTooltipHit> m_uiTooltipHits;
    std::function<void(const FrontierDeviceControlCommand&)> m_deviceControlHandler;
    FrontierGpuControllerUi::Snapshot m_gpuControllerSnapshot;
    std::function<void(qint64, qint64)> m_legacyDifficultyRangeHandler;
    qint64 m_lastDifficultyRequestMinMs{std::numeric_limits<qint64>::min() / 2};
    qint64 m_lastDifficultyRequestMaxMs{std::numeric_limits<qint64>::min() / 2};
    QString m_draggingBudgetDevice;
    QVector<LaneSample> m_primaryHistory;
    QVector<LaneSample> m_secondaryHistory;
    FrontierLosslessCandidateArchive<CandidatePoint> m_candidates;
    QHash<QString, QPair<QString, QDateTime>> m_gapNetworkOutcomes;
    FrontierGraphProjectionWorker m_graphProjectionWorker;
    std::uint64_t m_graphProjectionGeneration{0};
    mutable std::uint64_t m_cachedExactGeneration{std::numeric_limits<std::uint64_t>::max()};
    mutable qulonglong m_cachedExactCount{};
    std::uint64_t m_headerGeneration{std::numeric_limits<std::uint64_t>::max()};
    QSet<QString> m_headerRuns;bool m_headerFullHistory{};
    std::optional<CandidatePoint> m_headerLatest;
    std::uint64_t m_intervalCacheGeneration{std::numeric_limits<std::uint64_t>::max()},m_laneExactCacheGeneration{std::numeric_limits<std::uint64_t>::max()};
    QSet<QString> m_intervalCacheRuns;
    std::optional<CandidatePoint> m_intervalCacheEvent;
    QHash<QString,qsizetype> m_laneExactCache;
    QHash<QString,int> m_laneExactCountCache;
    QString m_plotImageKey;QImage m_plotImage;QRect m_plotImageBounds;
    QVector<UiTooltipHit> m_plotTooltipCache;
    pgn::ui::DensePointLayer m_denseLayer;
    pgn::ui::CatalogLayer* m_observationCatalog{};
    pgn::ui::CatalogLayer* m_nativeAllCatalog{};
    pgn::ui::CatalogLayer* m_nativeCurrentCatalog{};
    pgn::booking::BookingModel* m_pgnBookingModel{};
    QVector<QPair<QRectF,QString>> m_pgnBookingHits;
    QRectF m_bookingRailArea;
    QVector<QPair<QRectF,int>> m_bookingRailNavigation;
    int m_bookingRailOffset{},m_bookingRailCapacity{1};
    QString m_bookingRailFocusedRun;
    QElapsedTimer m_bookingVisualClock;
    pgn::ui::WorkProgressTransition m_bookingWorkTransition;
    pgn::ui::WorkTextureMotion m_bookingTextureMotion;
    QVector<QPair<QRectF,QString>> m_pgnSummaryPages;QString m_pgnSummaryPage;QRectF m_pgnCentralArea;QSet<QString> m_pgnFinishedRuns;
    struct BookingRailSegment{pgn::booking::Booking job;double first{},last{};QColor color;};
    QVector<BookingRailSegment> m_bookingRailSegments;
    quint64 m_bookingRailRevision{std::numeric_limits<quint64>::max()};
    QString m_bookingRailFirst,m_bookingRailLast;int m_bookingRailBits{};
    std::vector<FrontierGraphProjectionRecord> m_denseViewportRecords;
    FrontierGraphVerticalBounds m_denseVisibleBounds;
    std::uint64_t m_denseViewportGeneration{std::numeric_limits<std::uint64_t>::max()},m_denseViewportRevision{};
    qint64 m_denseViewportMinX{},m_denseViewportMaxX{};
    bool m_denseViewportTimeAxis{},m_denseViewportFullHistory{};
    std::vector<std::string> m_denseViewportRuns;
    std::uint64_t m_graphLodGeneration{0};
    std::uint64_t m_graphProjectionRequestGeneration{0};
    std::uint64_t m_graphLodRequestGeneration{0};
    bool m_graphLodPending{false};
    std::vector<std::size_t> m_graphLodArchiveIndices;
    std::size_t m_graphLodVisibleCount{0};
    bool m_graphLodSampled{false};
    std::size_t m_graphLodSourceCount{0};
    double m_graphLodMinimumX{0.0};
    double m_graphLodMaximumX{0.0};
    double m_graphLodMinimumY{0.0};
    double m_graphLodMaximumY{0.0};
    int m_graphLodViewportPixels{0};
    std::size_t m_graphLodMaximumPoints{0};
    FrontierGraphXAxis m_graphLodXAxisMode{FrontierGraphXAxis::Length};
    bool m_graphLodFullHistory{false};
    bool m_graphLodMinimumMeritFilter{false};
    bool m_graphProjectionInputValid{false};
    std::uint64_t m_graphProjectionInputGeneration{0};
    std::vector<std::string> m_graphProjectionInputActiveRunIds;
    FrontierGraphXAxis m_graphProjectionInputXAxisMode{FrontierGraphXAxis::Length};
    bool m_graphProjectionInputFullHistory{false};
    bool m_graphProjectionInputMinimumMeritFilter{false};
    std::vector<FrontierCandidateProjectionRecord> m_graphProjectionInput;
    int m_graphProjectionCurrentEventCount{0};
    int m_graphProjectionHistoryEventCount{0};
    qulonglong m_graphProjectionCurrentAcceptedCount{0};
    qulonglong m_graphProjectionCurrentRejectedCount{0};
    qulonglong m_graphProjectionCurrentStaleCount{0};
    qulonglong m_graphProjectionCurrentPendingCount{0};
    qint64 m_graphProjectionMinimumX{0};
    qint64 m_graphProjectionMaximumX{0};
    double m_graphProjectionMinimumMerit{0.0};
    double m_graphProjectionMaximumMerit{0.0};
    bool m_graphProjectionMeritBoundsValid{false};
    QHash<QString, qsizetype> m_candidateIdentityIndex;
    QHash<QString, QString> m_candidateTerminalDispositions;
    QVector<CandidateHit> m_candidateHits;
    QString m_hoveredEventId;
    QString m_hoveredUiTooltip;
    QPointF m_mousePosition;
    QString m_pulsingEventId;
    bool m_showFullHistory{true};
    FrontierMinerSelection m_minerSelection;
    QRectF m_previousMinerHit;
    QRectF m_nextMinerHit;
    QRectF m_currentRunToggleHit;
    QRectF m_fullHistoryToggleHit;
    QRectF m_graphResetHit;
    QRectF m_graphTimeAxisHit;
    QRectF m_graphLengthAxisHit;
    QVector<QPair<QRectF, FrontierGraphRange>> m_graphRangeHits;
    FrontierGraphRange m_graphRange{FrontierGraphRange::All};
    QVector<QPair<QRectF, int>> m_legacyTimeScaleHits;
    int m_legacyIntervalHours{4}; // compatibility with older QA state readers
    int m_legacyTimeRangeIndex{static_cast<int>(FrontierTimeRangePresetCount)-1}; // ALL: startup renders retained history
    bool m_legacyShowAllHistory{true};
    QVector<QPair<QRectF, FrontierGraphXAxis>> m_graphXAxisModeHits;
    FrontierGraphXAxis m_graphXAxisMode{FrontierGraphXAxis::Length};
    QRectF m_graphXAxisHit;
    QRectF m_graphYAxisHit;
    QRectF m_candidateGraphRect;
    FrontierGraphView m_graphView;
    FrontierGraphView m_graphPresentedView; // complete displayed axes, never the pending target
    bool m_graphZoomActive{false};
    bool m_graphBoundsValid{false};
    int m_qaPendingZoomSteps{0};
    GraphDragMode m_graphDragMode{GraphDragMode::None};
    QPointF m_graphDragStart;
    FrontierGraphView m_graphDragStartView;
    bool m_graphDragMoved{false};
    bool m_minimumMeritFilterMode{false};
    QRectF m_deviceStripRect;
    int m_devicePage{0};
    int m_responsiveSection{0};
    QVector<QPair<QRectF,int>> m_devicePageHits;
    QVector<QPair<QRectF,int>> m_responsiveSectionHits;
    QVariantAnimation m_deviceSpring;
    qreal m_deviceScrollX{0.0};
    qreal m_deviceScrollMaximum{0.0};
    bool m_qaDeviceScrollEnd{false};
    qreal m_deviceDragAnchor{0.0};
    bool m_draggingDevices{false};
    qreal m_phase{0.0};
    double m_activityAnimationRatio{0.0};
    bool m_legacyR4Mode{false};
    QHash<QString, qreal> m_producerPhases;
    qint64 m_lastProducerAnimationMs{0};
    qint64 m_uiSessionStartedUtcMs{QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()};
};


}
namespace pgn::ui {
QWidget* createMiningCanvas(QWidget* parent) { return new ObservatoryCanvas({}, parent, true); }
QWidget* createNumberLensCanvas(QWidget* parent) {
    auto* canvas=new ObservatoryCanvas({},parent,true);
    canvas->setObjectName("pgn-number-lens-graph");canvas->configureNumberLens();
    return canvas;
}
void presentNetwork(QWidget* canvas,const std::optional<pgn::wallet::ChainObservation>& observation){
    if(auto* value=dynamic_cast<ObservatoryCanvas*>(canvas))value->setNetworkObservation(observation);
}
void presentLocalSourceProofStatus(QWidget* canvas,const QJsonObject& data){
    if(!canvas||canvas->property("pgn_local_source_proof").value<QJsonObject>()==data)return;
    canvas->setProperty("pgn_local_source_proof",data);canvas->update();
}
void presentVerificationReceipt(QWidget* canvas,const pgn::verification::LatestReceipt& result){
    if(!canvas)return;
    auto object=canvas->property("pgn_independent_verification_receipt").value<QJsonObject>();
    QString error=result.error;
    if(result.receipt&&error.isEmpty()){
        const auto& r=*result.receipt;
        object=QJsonObject{{"receipt_id",QString::number(r.id)},{"source_id",QString::number(r.sourceId)},
            {"p",r.lower},{"q",r.upper},{"validator_sha256",r.validatorSha256},
            {"verdict",int(r.verdict)},{"gap_size",r.verdict==pgn::verification::GapVerdict::VerifiedWithinBound?QString::number(r.verifiedGapSize):QString()},
            {"checked_interior",QString::number(r.checkedInterior)},{"elapsed_ns",QString::number(r.elapsedNs)},
            {"original_checked_utc",r.originalCheckedUtc.toString(Qt::ISODateWithMs)},
            {"network_credit",false},{"range_coverage",false},{"scope","historical_local_qualified_validator_receipt"}};
    }else if(!object.isEmpty()&&error.isEmpty()){
        error=QStringLiteral("Current journal contains no receipt; last historical receipt retained");
    }
    if(object==canvas->property("pgn_independent_verification_receipt").value<QJsonObject>()&&
       error==canvas->property("pgn_independent_verification_error").toString())return;
    canvas->setProperty("pgn_independent_verification_receipt",object);
    canvas->setProperty("pgn_independent_verification_error",error);
    canvas->update();
}
void bindObservationCatalog(QWidget* canvas,const QString& database) {
    if(auto* value=dynamic_cast<ObservatoryCanvas*>(canvas))value->bindObservationCatalog(database);
}
void configureMiningDashboard(QWidget* canvas){
    if(!canvas)return;canvas->setProperty("pgn_compact_mining",true);canvas->setFixedHeight(128);
    canvas->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);canvas->update();
}
void bindNativeCatalog(QWidget* canvas,const QString& database) {
    if(auto* value=dynamic_cast<ObservatoryCanvas*>(canvas))value->bindNativeCatalog(database);
}
QJsonObject catalogDiagnostics(QWidget* canvas) {
    if(auto* value=dynamic_cast<ObservatoryCanvas*>(canvas))return value->catalogDiagnostics();
    return {{"error","Not a scientific canvas"}};
}
void presentNativeIndex(QWidget* canvas,const QJsonObject& packet) {
    if(auto* value=dynamic_cast<ObservatoryCanvas*>(canvas))value->presentNativeIndex(packet);
}
void bindBookingModel(QWidget* canvas,pgn::booking::BookingModel* model) {
    if(auto* value=dynamic_cast<ObservatoryCanvas*>(canvas))value->bindBookingModel(model);
}
QRectF reservationSegmentRect(QWidget* canvas,const QString& id) {
    if(auto* value=dynamic_cast<ObservatoryCanvas*>(canvas))return value->reservationSegmentRect(id);
    return {};
}
QColor miningDeviceAccent(QWidget* canvas,const QString& id) {
    if(auto* value=dynamic_cast<ObservatoryCanvas*>(canvas))return value->deviceAccentFor(id);
    return QColor("#69efc5");
}
void presentDevice(QWidget* widget, const FrontierTelemetrySnapshot& sample, bool secondary) {
static_cast<ObservatoryCanvas*>(widget)->setTelemetrySnapshot(sample, secondary); }
void presentTestDevice(QWidget* widget,const FrontierTelemetrySnapshot& sample){
 if(!sample.raw.value("pgn_test_only").toBool()||sample.gpuUuid!="GPU-33f44d5c-91e4-6ab6-1e45-8c93135fd265")return;
 static_cast<ObservatoryCanvas*>(widget)->setAdditionalTelemetrySnapshot(sample.gpuUuid,sample);
}
void presentCpuInventory(QWidget* widget,const QJsonObject& inventory) {
    const QString identity="CPU/"+inventory.value("host").toString().toUpper()+"/"+QString::number(inventory.value("processor_group").toInt());
    if(widget->property("pgn_cpu_control_devices").toStringList().contains(identity))return;
    if(inventory.value("schema")!="PGN_CPU_INVENTORY_ONLY_V1"||inventory.value("compute_started").toBool(true)||inventory.value("work_supported").toBool(true))return;
    const auto host=inventory.value("host").toString();if(host.isEmpty())return;
    FrontierTelemetrySnapshot lane;
    lane.gpuUuid="CPU/"+host.toUpper()+"/"+QString::number(inventory.value("processor_group").toInt());
    lane.gpuName=host+" · "+inventory.value("model").toString();lane.engineId="pgn-cpu-normal-executor";
    lane.state="idle";lane.producerStatus="idle";lane.authoritative=true;lane.stale=false;
    lane.observedUtc=QDateTime::currentDateTimeUtc();
    lane.raw={{"pgn_cpu_inventory",inventory},{"pgn_cpu_host",host},{"pgn_cpu_control",false},{"pgn_process_running",false},{"pgn_device_enabled",false}};
    static_cast<ObservatoryCanvas*>(widget)->setAdditionalTelemetrySnapshot(lane.gpuUuid,lane);
}
void presentCpuController(QWidget* widget,const QJsonObject& inventory,const QJsonObject& status,bool connected) {
    const auto host=inventory.value("host").toString();if(host.isEmpty())return;
    FrontierTelemetrySnapshot lane;lane.gpuUuid="CPU/"+host.toUpper()+"/"+QString::number(inventory.value("processor_group").toInt());
    lane.gpuName=host+" · "+inventory.value("model").toString();lane.engineId="pgn-cpu-normal-executor";lane.sourceMode="live";
    const bool computing=pgn::mining::cpuCalculationActive(status,connected);
    lane.authoritative=connected;lane.stale=!connected;lane.observedUtc=QDateTime::currentDateTimeUtc();lane.state=computing?"running":"idle";
    lane.producerStatus=computing?"running":"idle";lane.runId=status.value("request_sha256").toString(status.value("session").toString());
    lane.rowsCompleted=status.value("completed_positions").toString();lane.rowsTotal=status.value("total_positions").toString();
    if(computing&&status.value("rate_positions_per_second").isDouble())lane.e2ePositionsPerSecond=status.value("rate_positions_per_second").toDouble();
    if(connected&&status.value("machine_load_percent").isDouble())lane.gpuSmUtilPercent=status.value("machine_load_percent").toDouble();
    lane.raw={{"pgn_cpu_inventory",inventory},{"pgn_cpu_host",host},{"pgn_cpu_control",connected},{"pgn_cpu_state",status.value("state")},
        {"pgn_cpu_maximum_workers",inventory.value("maximum_workers")},{"pgn_cpu_requested_threads",status.value("requested_threads")},
        {"pgn_cpu_active_threads",status.value("threads_active")},{"pgn_cpu_worker_pids",status.value("worker_pids")},
        {"pgn_device_enabled",connected&&status.value("resource_selected").toBool(status.value("enabled").toBool())},{"pgn_process_running",computing},
        {"pgn_cpu_status",status}};
    const auto science=pgn::ui::cpuMiningRates(status,connected);
    if(computing&&!science.isEmpty()){
        lane.powmPerSecond=pgn::ui::cpuMetricNumber(science,"tests_per_second");
        lane.raw.insert("pgn_processed_gaps_per_second",science.value("gaps_per_second"));
        lane.raw.insert("pgn_retained_gaps_per_billion_positions",science.value("gaps_per_billion_positions"));
    }
    auto devices=widget->property("pgn_cpu_control_devices").toStringList();if(!devices.contains(lane.gpuUuid))devices.append(lane.gpuUuid);widget->setProperty("pgn_cpu_control_devices",devices);
    auto statuses=widget->property("pgn_cpu_controller_states").toJsonObject();statuses.insert(lane.gpuUuid,status);widget->setProperty("pgn_cpu_controller_states",statuses);
    auto inventories=widget->property("pgn_cpu_controller_inventory").toJsonObject();inventories.insert(lane.gpuUuid,inventory);widget->setProperty("pgn_cpu_controller_inventory",inventories);
    static_cast<ObservatoryCanvas*>(widget)->setCpuControllerSnapshot(lane);
}
void presentResources(QWidget* widget, const FrontierResourceTelemetrySnapshot& sample) {
widget->setProperty("pgn_diagnostic_hardware",pgn::ui::diagnosticResourceSnapshot(sample));
static_cast<ObservatoryCanvas*>(widget)->setResourceTelemetry(sample); }
void presentGap(QWidget* widget, const FrontierGapEvent& event, bool secondary) {
static_cast<ObservatoryCanvas*>(widget)->addGapEvent(event, secondary); }
void bindMiningResourceControls(QWidget* widget,std::function<void(const QString&,const QString&,bool,int)> handler){
 static_cast<ObservatoryCanvas*>(widget)->setDeviceControlHandler([handler=std::move(handler)](const auto& c){handler(c.deviceId,c.requestId,c.enabled,c.budgetPercent);});
}
void bindMiningControls(QWidget* widget, std::function<void(const QString&, const QString&, bool)> handler) {
static_cast<ObservatoryCanvas*>(widget)->setDeviceControlHandler(
 [handler=std::move(handler)](const auto& c) { handler(c.deviceId,c.requestId,c.enabled); }); }
void resolveDeviceControl(QWidget* widget, const QString& deviceId, const QString& requestId,
                          bool applied, const QString& detail) {
static_cast<ObservatoryCanvas*>(widget)->resolveDeviceControl(deviceId, requestId, applied, detail); }
QVariantList miningCardStates(QWidget* widget) {
return static_cast<ObservatoryCanvas*>(widget)->miningCardStates(); }
QJsonArray miningDeviceRates(QWidget* widget){return static_cast<ObservatoryCanvas*>(widget)->miningDeviceRates();}
QRectF miningToggleRect(QWidget* widget,const QString& id) {return static_cast<ObservatoryCanvas*>(widget)->miningToggleRect(id);}
QRectF miningBudgetRect(QWidget* widget,const QString& id){return static_cast<ObservatoryCanvas*>(widget)->miningBudgetRect(id);}
void scrollMiningDevicesEnd(QWidget* widget){static_cast<ObservatoryCanvas*>(widget)->setQaDeviceScrollEnd(true);widget->repaint();}
QStringList miningTooltips(QWidget* widget) {return static_cast<ObservatoryCanvas*>(widget)->miningTooltips();}
void beginHistoryBatch(QWidget* widget) { static_cast<ObservatoryCanvas*>(widget)->beginGraphDataBatch(); }
void endHistoryBatch(QWidget* widget) { static_cast<ObservatoryCanvas*>(widget)->endGraphDataBatch(); }
bool selectNativeGraphAxis(QWidget* widget,const QString& axis) {
 if(auto* canvas=dynamic_cast<ObservatoryCanvas*>(widget))return canvas->selectNativeGraphAxis(axis);
 return false;
}
}
