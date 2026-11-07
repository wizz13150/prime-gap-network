#pragma once
#include "ui/audit_assignment_display.h"
#include "ui/audit_status_presentation.h"
#include "ui/position_range_item.h"
#include "wallet/core_runtime.h"
#include <QWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QPointer>
#include <QJsonArray>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSet>
#include <QShowEvent>
#include <QToolButton>
#include <functional>

namespace pgn::ui {
// Public commitments only. Expected counts and unrevealed answers never enter this view.
class AuditExplorerView final : public QWidget {
public:
    using Result = std::function<void(QJsonObject, QString)>;
    using PageReader = std::function<void(int, Result)>;

private:
    PageReader reader_, participantReader_;
    QLabel* registrySummary_;
    QWidget* registryPanel_;
    QTreeWidget* registry_;
    QPushButton *registryPrevious_, *registryNext_;
    int registryOffset_{}, renderedRegistryOffset_{-1};
    bool registryBusy_{};
    QJsonArray participants_;
    QLabel *status_, *summary_;
    QTableWidget* table_;
    QTreeWidget* detail_;
    QPushButton *previous_, *next_;
    QTimer timer_;
    QJsonArray reports_, audits_;
    QJsonObject inspectedReport_, inspectedAudit_;
    QString selectedRequest_;
    int offset_{}, renderedOffset_{-1}, detailRebuilds_{};
    bool busy_{};

    QJsonObject audit(const QString& id) const {
        for (const auto& entry : audits_) {
            const auto value = entry.toObject();
            if (value.value("request_id") == id) return value;
        }
        return {};
    }

    void inspect(int row) {
        if (row < 0 || row >= reports_.size()) return;
        const auto report = reports_[row].toObject();
        const auto checks = audit(report.value("request_id").toString());
        if (report == inspectedReport_ && checks == inspectedAudit_) return;
        const bool same = selectedRequest_ == report.value("request_id").toString();
        const int scroll = same ? detail_->verticalScrollBar()->value() : 0;
        QSet<QString> expanded;
        if (same) for (int i = 0; i < detail_->topLevelItemCount(); ++i) {
            const auto* item = detail_->topLevelItem(i);
            if (item->isExpanded()) expanded.insert(item->data(0, Qt::UserRole).toString());
            for(int j=0;j<item->childCount();++j)if(item->child(j)->isExpanded())
                expanded.insert(item->child(j)->data(0,Qt::UserRole).toString());
        }
        const bool hadDetails = !inspectedReport_.isEmpty() && same;
        selectedRequest_ = report.value("request_id").toString();
        inspectedReport_ = report;
        inspectedAudit_ = checks;
        detail_->setUpdatesEnabled(false);
        detail_->clear();
        const auto field = [this](const QString& name, const QString& value) {
            return new QTreeWidgetItem(detail_, QStringList{name, value});
        };
        field("Report commitment", report.value("report_root").toString());
        field("Confirming block", report.value("block").toString());
        field("Confirmation height", QString::number(report.value("height").toInteger()));
        field("Owner signing script", report.value("miner_script").toString());
        presentAuditStatus(field("Sampling verdict", {}),auditReportPresentation(checks));
        if (checks.contains("details_root"))
            field("Larger-gap details commitment", checks.value("details_root").toString());
        const auto jobs = checks.value("jobs").toArray();
        if (jobs.isEmpty()) field("Audit allocation", "Two independent auditors are still required");
        for (const auto& value : jobs) {
            const auto job = value.toObject();
            const auto key = QString::number(job.value("slot").toInt()) + ":" + QString::number(job.value("attempt").toInt());
            auto* parent = field(QString("Audit %1 · attempt %2").arg(job.value("slot").toInt() + 1)
                .arg(job.value("attempt").toInt() + 1), job.value("state").toString());
            presentAuditStatus(parent,auditStatusPresentation(job.value("state").toString()));
            parent->setData(0, Qt::UserRole, key);
            new QTreeWidgetItem(parent, {"Auditor signing script", job.value("actor").toString()});
            const auto leaseLine = auditAssignmentLeaseLine(job);
            new QTreeWidgetItem(parent, {leaseLine.first, leaseLine.second});
            for (const auto& segment : job.value("ranges").toArray()) {
                const auto range = segment.toObject();
                auto* partition=positionRangeItem(parent,QString("Partition %1").arg(range.value("index").toInt()),
                    range.value("start").toString(),range.value("end").toString());
                const auto partitionKey=key+":partition:"+QString::number(range.value("index").toInt());
                partition->setData(0,Qt::UserRole,partitionKey);
                partition->setExpanded(same&&expanded.contains(partitionKey));
            }
            parent->setExpanded(!hadDetails || expanded.contains(key));
        }
        field("Report retention", "Pending until processed; the submitted report does not expire");
        field("Acceptance boundary", "Sampling agreement, exact gap proof and monetary acceptance are separate checks.");
        detail_->verticalScrollBar()->setValue(scroll);
        detail_->setUpdatesEnabled(true);
        setProperty("pgn_audit_selected_request", selectedRequest_);
        setProperty("pgn_audit_detail_rebuilds", ++detailRebuilds_);
    }

    void render() {
        const QSignalBlocker blocker(table_);
        table_->setUpdatesEnabled(false);
        table_->setRowCount(reports_.size());
        int unassigned = 0, active = 0, passed = 0, failed = 0, complete = 0;
        int selectedRow = -1;
        for (int i = 0; i < reports_.size(); ++i) {
            const auto report = reports_[i].toObject();
            const auto request = report.value("request_id").toString();
            const auto checks = audit(request);
            QStringList states{"awaiting", "awaiting"};
            bool occupied[2]{};
            for (const auto& entry : checks.value("jobs").toArray()) {
                const auto job = entry.toObject();
                const int slot = job.value("slot").toInt(-1);
                if (slot < 0 || slot > 1) continue;
                occupied[slot] = true;
                const auto state = job.value("state").toString();
                states[slot] = state;
                if (state == "passed") ++passed;
                else if (state == "failed") ++failed;
                else ++active;
            }
            for (bool present : occupied) if (!present) ++unassigned;
            if (checks.value("audited").toBool()) ++complete;
            const QStringList cells{QString("Report %1").arg(offset_ + i + 1),
                QString::number(report.value("height").toInteger()), states[0], states[1],
                checks.value("audited").toBool() ? "Sampling passed" : "Awaiting independent audits"};
            for (int col = 0; col < cells.size(); ++col) {
                auto* item = new QTableWidgetItem(cells[col]);
                item->setData(Qt::UserRole, request);
                item->setToolTip("Request: " + request + "\nReport: " + report.value("report_root").toString());
                if(col==2||col==3)presentAuditStatus(item,auditStatusPresentation(states[col-2]));
                if(col==4)presentAuditStatus(item,auditReportPresentation(checks));
                table_->setItem(i, col, item);
            }
            if (request == selectedRequest_) selectedRow = i;
        }
        summary_->setText(QString("This page: %1 reports · %2 slots awaiting auditor · %3 active audits · %4 passed · %5 failed · %6 fully audited reports")
            .arg(reports_.size()).arg(unassigned).arg(active).arg(passed).arg(failed).arg(complete));
        setProperty("pgn_audit_waiting_slots", unassigned);
        setProperty("pgn_audit_active_jobs", active);
        if (selectedRow < 0 && !reports_.isEmpty()) selectedRow = 0;
        if (selectedRow >= 0) {
            table_->selectRow(selectedRow);
            inspect(selectedRow);
        } else {
            detail_->clear();
            selectedRequest_.clear();
            inspectedReport_ = inspectedAudit_ = {};
            setProperty("pgn_audit_selected_request", QString{});
        }
        table_->setUpdatesEnabled(true);
    }

    void refreshRegistry() {
        if (!participantReader_ || registryBusy_) return;
        registryBusy_ = true;
        const int page = registryOffset_;
        const QPointer<AuditExplorerView> guard(this);
        participantReader_(page, [guard, page](QJsonObject snapshot, QString error) {
            if (!guard) return;
            guard->registryBusy_ = false;
            if (page != guard->registryOffset_) { guard->refreshRegistry(); return; }
            if (!error.isEmpty()) {
                guard->registrySummary_->setText("Operator registry unavailable; report evidence is retained");
                guard->registrySummary_->setToolTip(error);
                return;
            }
            const auto participants = snapshot.value("participants").toArray();
            if (participants.size() > 100 || !snapshot.value("participants").isArray()) {
                guard->registrySummary_->setText("Operator registry response exceeded its page limit");
                return;
            }
            QSet<QString> operators;
            int enabled = 0;
            for (const auto& entry : participants) {
                const auto row = entry.toObject();
                operators.insert(row.value("operator_id").toString());
                if (row.value("may_audit").toBool()) ++enabled;
            }
            if (participants != guard->participants_ || page != guard->renderedRegistryOffset_) {
                guard->registry_->setUpdatesEnabled(false);
                guard->registry_->clear();
                for (const auto& entry : participants) {
                    const auto row = entry.toObject();
                    auto* item = new QTreeWidgetItem(guard->registry_, {row.value("operator_id").toString(),
                        row.value("signing_script").toString(), row.value("may_audit").toBool() ? "Allowed" : "Disabled"});
                    item->setToolTip(0, item->text(0));
                    item->setToolTip(1, item->text(1));
                }
                const int rowHeight = std::max(guard->registry_->fontMetrics().height() + 6,
                    guard->registry_->sizeHintForRow(0));
                const int headerHeight = guard->registry_->header()->sizeHint().height() +
                    guard->registry_->frameWidth() * 2;
                guard->registry_->setFixedHeight(std::min(150,
                    headerHeight + std::max(1, int(participants.size())) * rowHeight));
                guard->registry_->setUpdatesEnabled(true);
                guard->participants_ = participants;
                guard->renderedRegistryOffset_ = page;
            }
            guard->registrySummary_->setText(QString("Signing identities: %1 · operators on this page: %2 · audit-enabled on this page: %3")
                .arg(snapshot.value("total").toInteger()).arg(operators.size()).arg(enabled));
            guard->registrySummary_->setToolTip(QString("Registry at ledger %1. Address aliases share an operator ID. Admission does not prove independence or that a worker is online.")
                .arg(snapshot.value("height").toInteger()));
            guard->registryPrevious_->setEnabled(page > 0);
            guard->registryNext_->setEnabled(participants.size() == 100);
            guard->setProperty("pgn_audit_registry_snapshot", snapshot);
            guard->setProperty("pgn_audit_registry_page_offset", page);
        });
    }

    void refresh() {
        if (busy_) return;
        busy_ = true;
        const int page = offset_;
        const QPointer<AuditExplorerView> guard(this);
        reader_(page, [guard, page](QJsonObject snapshot, QString error) {
            if (!guard) return;
            guard->busy_ = false;
            // A delayed reply must never relabel a different page's reports.
            if (page != guard->offset_) { guard->refresh(); return; }
            if (!error.isEmpty()) {
                guard->status_->setText("Audit state unavailable · " + error);
                guard->status_->setStyleSheet("color:#ffbd69");
                return;
            }
            const auto reports = snapshot.value("reports").toArray();
            const auto audits = snapshot.value("audits").toArray();
            if (reports != guard->reports_ || audits != guard->audits_ || guard->renderedOffset_ != page) {
                guard->reports_ = reports;
                guard->audits_ = audits;
                guard->renderedOffset_ = page;
                guard->render();
            }
            guard->status_->setText(QString("Verified Core · ledger height %1 · observed %2")
                .arg(snapshot.value("height").toInteger())
                .arg(QDateTime::fromString(snapshot.value("observed_utc").toString(), Qt::ISODateWithMs).toLocalTime().toString("HH:mm:ss")));
            guard->status_->setStyleSheet("color:#69efc5");
            guard->previous_->setEnabled(page > 0);
            guard->next_->setEnabled(reports.size() == 100);
            guard->setProperty("pgn_audit_reports", reports.size());
            guard->setProperty("pgn_audit_snapshot", snapshot);
        });
    }

protected:
    void showEvent(QShowEvent* event) override {
        QWidget::showEvent(event);
        QTimer::singleShot(0, this, [this] { refresh(); refreshRegistry(); });
    }

public:
    explicit AuditExplorerView(PageReader reader, PageReader participantReader = {}, QWidget* parent = nullptr)
        : QWidget(parent), reader_(std::move(reader)), participantReader_(std::move(participantReader)) {
        setObjectName("pgn-audit-explorer");
        auto* box = new QVBoxLayout(this);
        box->setContentsMargins(20, 14, 20, 14);
        status_ = new QLabel("Waiting for the verified Core audit state");
        summary_ = new QLabel;
        summary_->setWordWrap(true);
        box->addWidget(status_);
        box->addWidget(summary_);
        box->addWidget(auditStatusLegend(this));
        auto* registryHeader = new QHBoxLayout;
        auto* registryToggle = new QToolButton(this);
        registryToggle->setObjectName("pgn-audit-registry-toggle");
        registryToggle->setText("Auditor registry");
        registryToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        registryToggle->setArrowType(Qt::RightArrow);
        registryToggle->setCheckable(true);
        registrySummary_ = new QLabel("Reading admitted audit identities…", this);
        registrySummary_->setObjectName("pgn-audit-registry-summary");
        registrySummary_->setWordWrap(true);
        registryHeader->addWidget(registryToggle);
        registryHeader->addWidget(registrySummary_, 1);
        if (participantReader_) box->addLayout(registryHeader);
        else { delete registryHeader; registryToggle->hide(); registrySummary_->hide(); }
        registryPanel_ = new QWidget(this);
        auto* registryBox = new QVBoxLayout(registryPanel_);
        registryBox->setContentsMargins(0, 0, 0, 0);
        registry_ = new QTreeWidget;
        registry_->setObjectName("pgn-audit-registry-table");
        registry_->setHeaderLabels({"Operator ID", "Signing script", "Audit permission"});
        registry_->setRootIsDecorated(false);
        registry_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        registry_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
        registry_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        registry_->setMaximumHeight(150);
        registryBox->addWidget(registry_);
        auto* registryPages = new QHBoxLayout;
        registryPrevious_ = new QPushButton("Previous identities");
        registryNext_ = new QPushButton("Next identities");
        registryPrevious_->setObjectName("pgn-audit-registry-previous");
        registryNext_->setObjectName("pgn-audit-registry-next");
        registryPrevious_->setEnabled(false);
        registryNext_->setEnabled(false);
        registryPages->addWidget(registryPrevious_);
        registryPages->addWidget(registryNext_);
        auto* registryHint = new QLabel("Permission does not imply an online or independent auditor.");
        registryHint->setWordWrap(true);
        registryPages->addWidget(registryHint, 1);
        registryBox->addLayout(registryPages);
        box->addWidget(registryPanel_);
        registryPanel_->hide();
        connect(registryToggle, &QToolButton::toggled, this, [this, registryToggle](bool expanded) {
            registryToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
            registryPanel_->setVisible(expanded);
        });
        connect(registryPrevious_, &QPushButton::clicked, this, [this] {
            registryOffset_ = std::max(0, registryOffset_ - 100); refreshRegistry();
        });
        connect(registryNext_, &QPushButton::clicked, this, [this] {
            registryOffset_ += 100; refreshRegistry();
        });
        table_ = new QTableWidget(0, 5);
        table_->setObjectName("pgn-audit-report-table");
        table_->setHorizontalHeaderLabels({"Report", "Confirmed at", "Audit 1", "Audit 2", "Validation"});
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table_->horizontalHeader()->setStretchLastSection(true);
        table_->verticalHeader()->hide();
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->setStyleSheet("QTableWidget#pgn-audit-report-table::item:selected,QTableWidget#pgn-audit-report-table::item:selected:!active{background:#245f68;color:#ffffff;}");
        box->addWidget(table_, 1);
        detail_ = new QTreeWidget;
        detail_->setObjectName("pgn-audit-report-details");
        detail_->setHeaderLabels({"Evidence", "Value"});
        detail_->setColumnWidth(0, 270);
        box->addWidget(detail_, 1);
        auto* controls = new QHBoxLayout;
        previous_ = new QPushButton("Previous reports");
        next_ = new QPushButton("Next reports");
        auto* reload = new QPushButton("Refresh audits");
        previous_->setObjectName("pgn-audit-previous");
        next_->setObjectName("pgn-audit-next");
        reload->setObjectName("pgn-audit-refresh");
        controls->addWidget(previous_);
        controls->addWidget(next_);
        controls->addStretch();
        controls->addWidget(reload);
        box->addLayout(controls);
        previous_->setEnabled(false);
        next_->setEnabled(false);
        connect(table_, &QTableWidget::itemSelectionChanged, this, [this] { inspect(table_->currentRow()); });
        connect(previous_, &QPushButton::clicked, this, [this] { offset_ = std::max(0, offset_ - 100); refresh(); });
        connect(next_, &QPushButton::clicked, this, [this] { offset_ += 100; refresh(); });
        connect(reload, &QPushButton::clicked, this, [this] { refresh(); refreshRegistry(); });
        timer_.setInterval(5000);
        connect(&timer_, &QTimer::timeout, this, [this] { if (isVisible()) { refresh(); refreshRegistry(); } });
        timer_.start();
    }

    explicit AuditExplorerView(pgn::wallet::CoreRuntime* runtime, QWidget* parent = nullptr)
        : AuditExplorerView([runtime](int offset, Result consume) {
            if (!runtime->connected()) { consume({}, "Verified Core connection required"); return; }
            runtime->readAuditSnapshot(offset, std::move(consume));
        }, [runtime](int offset, Result consume) {
            runtime->readParticipantPage(offset, std::move(consume));
        }, parent) {}
};
}
