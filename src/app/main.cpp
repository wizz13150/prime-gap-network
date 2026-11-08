#include "app/horizon_user_paths.h"
#include "app/horizon_startup.h"
#include "app/horizon_update_selector.h"
#include "app/horizon_ui_lease.h"
#include "update/update_paths.h"
#include "update/mining_update_resume.h"
#include "mining/cpu_normal_resources.h"
#include "ui/mining_frame_selection.h"
#include "mining/network_controller_recovery.h"
#include "mining/network_control_phase.h"
#include <QElapsedTimer>
#include "ui/vanity_view.h"
#include "ui/mining_canvas.h"
#include "ui/public_profile_button.h"
#include "ui/public_profile_control.h"
#include "ui/mining_results_view.h"
#include "ui/mining_ticket_progress.h"
#include "ui/mining_worker_close_guard.h"
#include "ui/record_submission_view.h"
#include "ui/diagnostic_capture_view.h"
#include "ui/mining_startup_status.h"
#include "ui/mining_control_binding.h"
#include "ui/startup_window.h"
#include <QSplitter>
#include <QProcess>
#include <QProgressBar>
#include <QSignalBlocker>
#include <QUuid>
#include "ui/catalog_layer.h"
#include "ui/render/mining_layout.h"
#include "ui/render/window_screen_fit.h"
#include "ui/render/horizon_native_frame.h"
#include "ui/render/horizon_primary_navigation.h"
#include "ui/archive_browser.h"
#include "ui/wallet_core_view.h"
#include "wallet/core_link_state.h"
#include "wallet/runtime_paths.h"
#include "ui/component_status_strip.h"
#include "ui/community_view.h"
#include "ui/network_work_view.h"
#include "ui/engine_tools_view.h"
#include "ui/verification_receipt_view.h"
#include "ui/local_source_proof_presentation.h"
#include "verification/local_validator_pins.h"
#include "verification/source_proof_runtime.h"
#include "booking/booking_model.h"
#include "mining/native_bridge.h"
#include "mining/cpu_inventory_monitor.h"
#include "engine/local_runtime_client.h"
#include "mining/cpu_fleet.h"
#include "mining/cpu_audit_broker.h"
#include "mining/source_age.h"
#include "mining/gap_rate_window.h"
#include "mining/application_mining_close.h"
#include "mining/application_mining_start.h"
#ifdef _WIN32
#include "mining/native_application_runtime.h"
#include "mining/native_hardware_monitor.h"
#endif
#include "mining_status_button.h"
#include "horizon_coin_navigation.h"
#include "horizon_graph_controls.h"
#include "horizon_workspace_style.h"
#include <QApplication>
#include <QPainter>
#include <QPixmap>
#include <QScrollArea>
#include <QMouseEvent>
#include <QMainWindow>
#include <QScreen>
#include <QTabWidget>
#include <QTabBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include "ui/range_calendar.h"
#include <QSpinBox>
#include <QFormLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QToolButton>
#include <QCheckBox>
#include <QTableWidget>
#include <QMessageBox>
#include <QFile>
#include <QSaveFile>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <stdexcept>
#include <QDateTime>
#include <QJsonDocument>
#include <iostream>
#include <QJsonArray>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QTimer>
#include <QIcon>
#include <QDialog>
#include <QMenu>
#include <QStackedWidget>
#include <QButtonGroup>
#include <QLocalServer>
#include <QLocalSocket>
#ifdef _WIN32
#include <windows.h>
#include <shobjidl.h>
#endif
int main(int argc,char** argv) {
 // Owned-process lifecycle endpoint. Normal close preserves the user's STOP
 // contract; developer maintenance is a DISTINCT observer-only drain, never
 // an implicit normal Close/Quit interception during UI replacement.
 const auto ownedVerb=argc>1?QString::fromLocal8Bit(argv[1]):QString{};
 const bool ownedArgument=QStringList{"--show-owned","--start-owned","--stop-owned"}.contains(ownedVerb);
 const bool ownedSimple=QStringList{"--close-owned","--update-close-owned","--maintenance-close-owned","--inspect-owned","--audit-graph-owned","--audit-values-owned"}.contains(ownedVerb);
 if(ownedArgument||ownedSimple){
  // Malformed control commands must not fall through into a normal GUI launch.
  if(argc!=(ownedArgument?4:3))return 2;
  // IPC clients need no GUI/platform plugin. This is still the same program;
  // only the actual window owner initializes QApplication below.
  QCoreApplication app(argc,argv);
  bool ok=false;const auto pid=QString::fromLocal8Bit(argv[2]).toLongLong(&ok);if(!ok||pid<=0)return 2;
  const bool start=QString::fromLocal8Bit(argv[1])=="--start-owned";
  const bool stop=QString::fromLocal8Bit(argv[1])=="--stop-owned";
  const bool maintenance=QString::fromLocal8Bit(argv[1])=="--maintenance-close-owned";
  const bool updateClose=QString::fromLocal8Bit(argv[1])=="--update-close-owned";
  if(start||stop){const auto id=QString::fromLocal8Bit(argv[3]);const QUuid parsed(id);if(parsed.isNull()||parsed.toString(QUuid::WithoutBraces)!=id)return 2;}
  QLocalSocket socket;socket.connectToServer("pgn-horizon-"+QString::number(pid));
  if(!socket.waitForConnected(2000))return 3;
  const bool inspect=QString::fromLocal8Bit(argv[1])=="--inspect-owned";
  const bool audit=QString::fromLocal8Bit(argv[1])=="--audit-graph-owned";
  const bool valuesAudit=QString::fromLocal8Bit(argv[1])=="--audit-values-owned";
  const bool show=QString::fromLocal8Bit(argv[1])=="--show-owned";
  const QByteArray command=(start||stop)?QByteArray(start?"start:":"stop:")+QByteArray(argv[3])+"\n":show?QByteArray("show:")+QByteArray(argv[3])+"\n":maintenance?QByteArray("maintenance_close\n"):updateClose?QByteArray("update_close\n"):valuesAudit?QByteArray("audit_values\n"):audit?QByteArray("audit_graph\n"):inspect?QByteArray("inspect\n"):QByteArray("close\n");
  if(command.size()>63)return 2;socket.write(command);socket.flush();if(!socket.waitForReadyRead((start||stop)?15000:(audit||valuesAudit)?8000:2000))return 4;
  auto response=socket.readAll();
  while(!response.endsWith('\n')&&response.size()<131072){if(!socket.waitForReadyRead(2000))return 4;response+=socket.readAll();}
  if(inspect||audit||valuesAudit||show||start||stop||maintenance||updateClose){QFile out;if(!out.open(stdout,QIODevice::WriteOnly))return 7;out.write(response);return response.startsWith('{')?0:5;}
  return response.startsWith("closing")?0:5;
 }
#ifdef _WIN32
 SetCurrentProcessExplicitAppUserModelID(L"PGN.Horizon.Desktop");
#endif
 QApplication app(argc,argv);QCoreApplication::setOrganizationName("PGN");QCoreApplication::setApplicationName("PGN Horizon");
 QApplication::setWindowIcon(QIcon(":/frontier/app-icon.png"));
 pgn::ui::StartupWindow startupWindow(app.arguments().contains("--start-minimized"));startupWindow.step(1,"Reading configuration");app.processEvents(QEventLoop::ExcludeUserInputEvents);
  QWidget* inspectCanvas=nullptr;QWidget* inspectArchive=nullptr;QWidget* inspectWallet=nullptr;QWidget* inspectEngineConfiguration=nullptr;QWidget* inspectRecords=nullptr;
 QLabel* inspectRuntimeStatus=nullptr;
 std::function<QJsonObject(const QString&)> showOwned;
 std::function<QJsonObject(const QString&)> startOwned,stopOwned;
 std::function<QJsonObject()> maintenanceOwned,updateOwned;
 std::function<QJsonArray()> captureMiningUpdate;
 std::function<void()> closeOwned;
 QLocalServer lifecycle;lifecycle.setSocketOptions(QLocalServer::UserAccessOption);
 struct OwnedIpcAudit {unsigned events{};qsizetype bytes{};quint64 connections{};} ownedIpcAudit;
 const bool ownedIpcTiming=app.arguments().contains("--native-timing-audit");
 const auto traceOwnedIpc=[&](const char* stage,quint64 connection,QLocalSocket* socket){
  if(!ownedIpcTiming||ownedIpcAudit.events>=32)return;
  const QJsonObject event{{"type","owned_ipc_timing"},{"stage",QString::fromLatin1(stage)},
   {"connection",QString::number(connection)},{"pid",QCoreApplication::applicationPid()},
   {"utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
   {"available_bytes",socket->bytesAvailable()},{"complete_line",socket->canReadLine()}};
  const auto line=QJsonDocument(event).toJson(QJsonDocument::Compact);
  if(ownedIpcAudit.bytes+line.size()+1>4096)return;
  ++ownedIpcAudit.events;ownedIpcAudit.bytes+=line.size()+1;
  std::cout<<line.constData()<<std::endl;
 };
 QObject::connect(&lifecycle,&QLocalServer::newConnection,&app,[&]{
  // Windows may accept synchronously inside listen(). Register first and
  // drain the whole pending queue, not one stale peer per later signal.
  while(lifecycle.hasPendingConnections()){
  auto* socket=lifecycle.nextPendingConnection();
  if(!socket)break;const auto connection=++ownedIpcAudit.connections;
  traceOwnedIpc("accepted",connection,socket);
  const auto readOwnedCommand=[&,socket,connection]{
   if(!socket->canReadLine()){traceOwnedIpc("partial",connection,socket);return;}
   traceOwnedIpc("command",connection,socket);
   const auto command=socket->readLine(64);
   if(command=="maintenance_close\n"&&maintenanceOwned){
    const auto response=maintenanceOwned();socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact)+"\n");socket->flush();
   }else if(command=="update_close\n"&&updateOwned){
    socket->write(QJsonDocument(updateOwned()).toJson(QJsonDocument::Compact)+"\n");socket->flush();
   }else if(command=="close\n"){
    if(!closeOwned){socket->write("not-ready\n");socket->flush();return;}
    socket->write("closing\n");socket->flush();QTimer::singleShot(50,&app,[&closeOwned]{closeOwned();});
   }else if(command.startsWith("start:")||command.startsWith("stop:")){
    const bool start=command.startsWith("start:");const auto id=QString::fromUtf8(command.mid(start?6:5)).trimmed();const QUuid parsed(id);
    const auto& action=start?startOwned:stopOwned;
    const auto result=parsed.isNull()||parsed.toString(QUuid::WithoutBraces)!=id
        ?QJsonObject{{"request_delivered",false},{"error","Canonical request UUID required"}}
        :!action?QJsonObject{{"request_delivered",false},{"error","Mining controller is not ready"}}:action(id);
    socket->write(QJsonDocument(result).toJson(QJsonDocument::Compact)+"\n");socket->flush();
   }else if(command.startsWith("show:")&&showOwned){
    const auto result=showOwned(QString::fromUtf8(command.mid(5)).trimmed());socket->write(QJsonDocument(result).toJson(QJsonDocument::Compact)+"\n");socket->flush();
   }else if(command=="audit_values\n"&&inspectCanvas){
    if(inspectCanvas->property("pgn_values_audit_enabled").toBool()){socket->disconnectFromServer();return;}
    inspectCanvas->setProperty("pgn_values_audit_enabled",true);
    auto* sampling=new QTimer(socket);sampling->setInterval(250);
    const auto started=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QObject::connect(sampling,&QTimer::timeout,socket,[socket,inspectCanvas,sampling,started,frames=QJsonArray{}]()mutable{
     const auto image=inspectCanvas->grab().toImage();const auto dpr=image.devicePixelRatio();QJsonArray values;
     for(const auto entry:inspectCanvas->property("pgn_values_audit_rects").toJsonArray()){
      auto row=entry.toObject();const auto pixels=image.copy(QRect(qRound(row["x"].toDouble()*dpr),qRound(row["y"].toDouble()*dpr),qRound(row["w"].toDouble()*dpr),qRound(row["h"].toDouble()*dpr)));
      row.remove("x");row.remove("y");row.remove("w");row.remove("h");
      row.insert("pixel_sha256",QString::fromLatin1(QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char*>(pixels.constBits()),pixels.sizeInBytes()),QCryptographicHash::Sha256).toHex()));values.append(row);
     }
     frames.append(QJsonObject{{"utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},{"values",values},
      {"run",QJsonValue::fromVariant(inspectCanvas->property("pgn_booking_run"))},{"source_utc",QJsonValue::fromVariant(inspectCanvas->property("pgn_raw_source_utc"))}});
     if(frames.size()<24)return;
     sampling->stop();inspectCanvas->setProperty("pgn_values_audit_enabled",false);
     const QJsonObject receipt{{"gui_pid",QCoreApplication::applicationPid()},{"started_utc",started},{"frames",frames}};
     socket->write(QJsonDocument(receipt).toJson(QJsonDocument::Compact)+"\n");socket->flush();sampling->deleteLater();
    });
    QObject::connect(socket,&QLocalSocket::disconnected,inspectCanvas,[inspectCanvas]{inspectCanvas->setProperty("pgn_values_audit_enabled",false);});sampling->start();
   }else if(command=="audit_graph\n"&&inspectCanvas){
    if(inspectCanvas->property("pgn_graph_audit_enabled").toBool()){socket->disconnectFromServer();return;}
    inspectCanvas->setProperty("pgn_graph_audit_frames",QJsonArray{});
    inspectCanvas->setProperty("pgn_graph_audit_enabled",true);
    const auto started=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QTimer::singleShot(6000,socket,[socket,inspectCanvas,started]{
     inspectCanvas->setProperty("pgn_graph_audit_enabled",false);
     const QJsonObject receipt{{"gui_pid",QCoreApplication::applicationPid()},{"started_utc",started},
      {"finished_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
      {"frames",inspectCanvas->property("pgn_graph_audit_frames").toJsonArray()}};
     socket->write(QJsonDocument(receipt).toJson(QJsonDocument::Compact)+"\n");socket->flush();
     inspectCanvas->setProperty("pgn_graph_audit_frames",QJsonArray{});
    });
   }else if(command=="inspect\n"&&inspectCanvas){
    inspectCanvas->setProperty("pgn_card_layout_audit",true);inspectCanvas->repaint();inspectCanvas->setProperty("pgn_card_layout_audit",false);
    QJsonObject receipt{{"gui_pid",QCoreApplication::applicationPid()},{"utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                        {"window_minimized",inspectCanvas->window()->isMinimized()},
                        {"window_visible",inspectCanvas->window()->isVisible()},{"window_icon_available",!inspectCanvas->window()->windowIcon().isNull()},{"taskbar_app_id","PGN.Horizon.Desktop"}};
    if(auto* results=inspectCanvas->window()->findChild<QWidget*>("pgn-mining-results")){
     QJsonArray tables;for(const auto* name:{"pgn-results-best","pgn-results-recent","pgn-results-gap-statistics","pgn-results-merit-statistics"})if(auto* table=results->findChild<QTableWidget*>(name)){int pixels=0;for(int i=0;i<table->rowCount();++i)pixels+=table->rowHeight(i);int columns=0;for(int i=0;i<table->columnCount();++i)columns+=table->columnWidth(i);tables.append(QJsonObject{{"name",name},{"rows",table->rowCount()},{"viewport_height",table->viewport()->height()},{"row_pixels",pixels},{"viewport_width",table->viewport()->width()},{"column_pixels",columns}});}receipt.insert("responsive_tables",tables);receipt.insert("auto_rows",results->property("pgn_results_fitted_rows").toInt());
    }
    if(auto* rail=inspectCanvas->window()->findChild<QWidget*>("pgn-ticket-progress-rail")){receipt.insert("ticket_progress",QJsonValue::fromVariant(rail->property("pgn_ticket_progress_fraction")));receipt.insert("ticket_animation_active",rail->property("pgn_ticket_animation_active").toBool());receipt.insert("ticket_animation_phase",QJsonValue::fromVariant(rail->property("pgn_ticket_animation_phase")));for(const auto* name:{"pgn_ticket_flow_received","pgn_ticket_flow_coalesced","pgn_ticket_animation_paint_ns","pgn_ticket_animation_paint_max_ns","pgn_ticket_collection_size","pgn_ticket_collection_departing","pgn_ticket_deliveries","pgn_ticket_landing_error"})receipt.insert(QString::fromLatin1(name),QJsonValue::fromVariant(rail->property(name)));}
    if(auto* work=inspectCanvas->window()->findChild<QWidget*>("pgn-network-work-explorer"))receipt.insert("selected_ticket_evidence",work->property("pgn_selected_ticket_evidence").toJsonObject());
    if(auto* explorer=inspectCanvas->window()->findChild<QWidget*>("pgn-block-explorer"))receipt.insert("selected_explorer_block",explorer->property("pgn_explorer_selected_block").toJsonObject());
    const auto frame=inspectCanvas->window()->frameGeometry();
    const auto screen=inspectCanvas->window()->screen();
    if(screen){const auto available=screen->availableGeometry();
     receipt.insert("window_frame_fits_available_screen",available.contains(frame));
     receipt.insert("window_frame",QJsonArray{frame.x(),frame.y(),frame.width(),frame.height()});
     receipt.insert("window_available_screen",QJsonArray{available.x(),available.y(),available.width(),available.height()});}
    for(const auto* name:{"pgn_hardware_observed_utc","pgn_hardware_devices","pgn_hardware_stale","pgn_responsive_layout","pgn_relay_program","pgn_relay_runtime_source","pgn_relay_error","pgn_paint_ns","pgn_dashboard_ns","pgn_plot_ns","pgn_plot_cache_hit","pgn_search_space_ns","pgn_live_miner_ns","pgn_archive_pairs","pgn_graph_history_points","pgn_graph_current_points","pgn_imported_visible_points","pgn_imported_render_ns","pgn_imported_busy","pgn_imported_error","pgn_reservation_segments","pgn_booking_run","pgn_booking_positions","pgn_booking_start","pgn_booking_end","pgn_booking_observed_utc","pgn_rich_cache_points","pgn_native_visible_points","pgn_native_render_ns","pgn_native_rows_read","pgn_native_incremental","pgn_native_cache_hits","pgn_native_cache_frames","pgn_native_cache_bytes","pgn_native_loading","pgn_native_error","pgn_raw_age_lower_bound_ns","pgn_raw_source_utc"})
     receipt.insert(QString::fromLatin1(name),QJsonValue::fromVariant(inspectCanvas->property(name)));
    for(const auto* name:{"pgn_display_rate_caption","pgn_display_rate_span_ns","pgn_display_positions_per_second","pgn_display_tests_per_second","pgn_display_gaps_per_second","pgn_total_only_mean","pgn_run_elapsed_seconds","pgn_full_block_seconds","pgn_central_metric_rectangles","pgn_central_footer_rectangle","pgn_block_eta_seconds","pgn_block_eta_window_seconds","pgn_block_eta_caption","pgn_cpu_audit_enabled","pgn_local_source_proof","pgn_live_verifier_state","pgn_execution_footer"})
     receipt.insert(QString::fromLatin1(name),QJsonValue::fromVariant(inspectCanvas->property(name)));
    receipt.insert("card_layout",inspectCanvas->property("pgn_card_layout").toJsonArray());
    if(auto* tab=inspectCanvas->window()->findChild<QPushButton*>("pgn-primary-mining-tab"))receipt.insert("pgn_tab_device_states",QJsonValue::fromVariant(tab->property("miningCardStates")));
    receipt.insert("cpu_controllers",inspectCanvas->property("pgn_cpu_controller_states").toJsonObject());
    receipt.insert("mining_control",pgn::ui::diagnosticMiningStartup(inspectCanvas));
    if(auto* capture=inspectCanvas->window()->findChild<QWidget*>("pgn-test-diagnostics"))receipt.insert("diagnostic_capture",QJsonObject{{"samples",capture->property("pgn_capture_samples").toInt()},{"bytes",capture->property("pgn_capture_bytes").toInt()},{"saved",QJsonValue::fromVariant(capture->property("pgn_capture_saved"))},{"status",capture->property("pgn_capture_status").toString()}});
    if(auto* summary=inspectCanvas->window()->findChild<QWidget*>("pgn-session-statistics"))receipt.insert("mining_network_summary",summary->property("pgn_network_summary").toJsonObject());
    for(const auto* name:{"pgn_cpu_validation_result","pgn_cpu_validation_result_saved","pgn_cpu_audit_network_status","pgn_worker_stream_state","pgn_worker_stream_pid","pgn_worker_reconnect_count","pgn_network_desired_on","pgn_worker_stream_bytes","pgn_worker_stream_packets","pgn_worker_stream_phase","pgn_worker_stream_notice","pgn_worker_stream_error","pgn_total_expanded","pgn_total_gpu_state","pgn_total_cpu_state","pgn_cpu_inventory_error","pgn_cpu_card_metrics","pgn_cpu_footer_geometry","pgn_gauge_label_rects","pgn_cpu_memory_pages","pgn_cpu_control_devices"})receipt.insert(QString::fromLatin1(name),QJsonValue::fromVariant(inspectCanvas->property(name)));
    if(inspectRuntimeStatus)receipt.insert("pgn_runtime_status",inspectRuntimeStatus->text());
    for(const auto* name:{"pgn_native_runtime_started","pgn_native_inventory_type","pgn_native_inventory_accepted","pgn_native_inventory_error","pgn_native_inventory_lineage","pgn_native_hardware_availability","pgn_native_hardware_notice","pgn_native_source_attempt","pgn_application_close_ready","pgn_application_close_status","pgn_application_start_status","pgn_native_timing_events","pgn_native_timing_bytes","pgn_native_timing_dropped"})
     receipt.insert(QString::fromLatin1(name),QJsonValue::fromVariant(inspectCanvas->property(name)));
    for(const auto* name:{"pgn_independent_verification_receipt","pgn_independent_verification_error","pgn_native_display_revision","pgn_native_target_revision","pgn_native_frame_caption","pgn_native_display_min_x","pgn_native_display_max_x","pgn_native_display_min_y","pgn_native_display_max_y","pgn_graph_x_ticks","pgn_graph_y_ticks"})
     receipt.insert(QString::fromLatin1(name),QJsonValue::fromVariant(inspectCanvas->property(name)));
    for(const auto* name:{"pgn_catalog_total_points","pgn_catalog_time_points","pgn_catalog_undated_points","pgn_native_history_caption","pgn_requested_graph_axis"})
     receipt.insert(QString::fromLatin1(name),QJsonValue::fromVariant(inspectCanvas->property(name)));
    if(inspectArchive)for(const auto* name:{"pgn_archive_native_rows","pgn_archive_native_total","pgn_archive_native_origins"})
     receipt.insert(QString::fromLatin1(name),QJsonValue::fromVariant(inspectArchive->property(name)));
     if(auto* results=inspectCanvas->window()->findChild<QWidget*>("pgn-mining-results"))for(const auto* name:{"pgn_results_points","pgn_results_runs","pgn_results_best_rows","pgn_results_recent_rows","pgn_results_scope","pgn_results_loading","pgn_results_error","pgn_visible_best"})receipt.insert(QString::fromLatin1(name),QJsonValue::fromVariant(results->property(name)));
     if(auto* summary=inspectCanvas->window()->findChild<QWidget*>("pgn-session-statistics"))for(const auto* name:{"pgn_ticket_productivity","pgn_productivity_regions","pgn_summary_scope","pgn_summary_range_regions"})receipt.insert(QString::fromLatin1(name),QJsonValue::fromVariant(summary->property(name)));
     if(auto* network=inspectCanvas->window()->findChild<QLabel*>("pgn-global-network-status"))receipt.insert("pgn_global_network_status",network->text());
     receipt.insert("pgn_update_shutdown_error",inspectCanvas->window()->property("pgn_update_shutdown_error").toString());
     for(const auto* name:{"pgn_native_small_icon","pgn_native_large_icon","pgn_update_resume_error"})receipt.insert(name,QJsonValue::fromVariant(inspectCanvas->window()->property(name)));
     receipt.insert("pgn_update_resume_pending",QJsonValue::fromVariant(inspectCanvas->property("pgn_update_resume_pending")));
     for(const auto* name:{"pgn_update_application_root","pgn_update_data_root","pgn_update_genesis","pgn_update_explicit_root"})receipt.insert(name,QJsonValue::fromVariant(inspectCanvas->window()->property(name)));
    for(const auto* name:{"pgn_wallet_connected","pgn_wallet_height","pgn_wallet_genesis","pgn_wallet_observed_utc"})
     receipt.insert(QString::fromLatin1(name),QJsonValue::fromVariant(inspectCanvas->property(name)));
    if(inspectWallet)for(const auto* name:{"pgn_core_link_state","pgn_core_link_detail","pgn_core_link_transitions","pgn_wallet_frontend_loaded","pgn_wallet_frontend_error","pgn_wallet_genuine_page_count","pgn_wallet_embedded","pgn_wallet_embedded_pid","pgn_network_block_rows","pgn_network_leases","pgn_network_height","pgn_network_selected_transactions","pgn_network_ticket_width"})
     receipt.insert(QString::fromLatin1(name),QJsonValue::fromVariant(inspectWallet->property(name)));
    if(inspectWallet){
     const auto* history=inspectWallet->findChild<QWidget*>("pgn-wallet-difficulty-history");if(!history)history=inspectWallet->findChild<QWidget*>("pgn-network-metrics-history");
     if(history){auto source=history->property("pgn_network_metrics_snapshot").toJsonObject();const auto points=source.take("points").toArray();receipt.insert("pgn_network_metrics_summary",source);receipt.insert("pgn_network_metrics_points",points.size());receipt.insert("pgn_network_metrics_last",points.isEmpty()?QJsonObject{}:points.last().toObject());receipt.insert("pgn_network_metrics_error",history->property("pgn_network_metrics_error").toString());receipt.insert("pgn_merit_plot_bounds",QJsonValue::fromVariant(history->property("pgn_merit_plot_bounds")));receipt.insert("pgn_rate_plot_bounds",QJsonValue::fromVariant(history->property("pgn_rate_plot_bounds")));}
     receipt.insert("pgn_wallet_difficulty_view_present",history!=nullptr);
     if(history)for(const auto* name:{"pgn_wallet_difficulty_points","pgn_wallet_difficulty_missing","pgn_wallet_difficulty_frame_points","pgn_wallet_difficulty_observed_utc","pgn_wallet_difficulty_source_error"})receipt.insert(QString::fromLatin1(name),QJsonValue::fromVariant(history->property(name)));
    }
    if(inspectEngineConfiguration){
      if(inspectRecords){for(const auto* name:{"pgn_record_status_text","pgn_record_status_revision","pgn_record_check_busy","pgn_record_submission_count","pgn_record_submission_reference","pgn_record_submission_error","pgn_record_reference_update_error","pgn_record_reference_checked_utc","pgn_record_submissions_expanded"})receipt.insert(QString::fromLatin1(name),QJsonValue::fromVariant(inspectRecords->property(name)));
       receipt.insert("pgn_record_copy_line_present",inspectRecords->findChild<QPushButton*>("pgn-copy-record-line")!=nullptr);
       receipt.insert("pgn_record_panel_height",inspectRecords->height());if(const auto* check=inspectRecords->findChild<QPushButton*>("pgn-check-record-submissions"))receipt.insert("pgn_record_check_enabled",check->isEnabled());}
     for(const auto* name:{"pgn_engine_module_catalogue_loaded","pgn_engine_module_page_size","pgn_engine_module_selection_request_id"})receipt.insert(QString::fromLatin1(name),QJsonValue::fromVariant(inspectEngineConfiguration->property(name)));
     if(const auto* label=inspectEngineConfiguration->findChild<QLabel*>("pgn-engine-module-status"))receipt.insert("pgn_engine_module_status",label->text());
    }
    socket->write(QJsonDocument(receipt).toJson(QJsonDocument::Compact)+"\n");socket->flush();
   }else socket->disconnectFromServer();
  };
  QObject::connect(socket,&QLocalSocket::readyRead,&app,readOwnedCommand);
  // readyRead is not replayed when acceptance already buffered a full line.
  // The socket context also cancels this queued drain if the peer disappears.
  if(socket->canReadLine())QTimer::singleShot(0,socket,readOwnedCommand);
  QObject::connect(socket,&QLocalSocket::disconnected,socket,&QObject::deleteLater);
  }
 });
 if(!lifecycle.listen("pgn-horizon-"+QString::number(QCoreApplication::applicationPid())))return 6;
 const auto applicationDirectory=QCoreApplication::applicationDirPath();
 const auto startupArguments=QCoreApplication::arguments();
 const QString root=pgn::app::resolveHorizonApplicationRoot(startupArguments,applicationDirectory);
 QFile installedCore(root+"/Core/core-template.json");std::optional<QByteArray> installedTemplate;
 if(installedCore.exists())installedTemplate=installedCore.open(QIODevice::ReadOnly)?installedCore.read(32769):QByteArray{};
 const auto startup=pgn::app::resolveHorizonStartup(startupArguments,installedTemplate);installedCore.close();
 const bool testnetMode=startup.testnet;
 const bool explicitLegacyRoot=startupArguments.size()>1&&!startupArguments[1].startsWith("--");
 if(!pgn::app::bindMutableDataRoot(root,pgn::wallet::installedCoreGenesis(root),explicitLegacyRoot)){std::cerr<<"User-data path binding unavailable\n";return 7;}
 const auto updateState=pgn::update::updateStateDirectory(pgn::wallet::installedCoreGenesis(root));
 startupWindow.step(2,"Verifying the selected application components");
 const auto updateSelection=updateState.isEmpty()?pgn::app::SelectedCohort{}:pgn::app::selectedUpdateCohort(applicationDirectory,updateState+"/installation",testnetMode?"testnet":"main",pgn::wallet::installedCoreGenesis(root),startupArguments.contains("--update-healthcheck"));
 if(!updateSelection.executable.isEmpty()){
  if(QProcess::startDetached(updateSelection.executable,startupArguments.mid(1),QFileInfo(updateSelection.executable).absolutePath()))return 0;
  std::cerr<<"Selected update could not be launched; opening the previous client\n";
 }
 // The instance identity follows the user's data/network, not the replaceable
 // executable directory. Old shortcuts must never open a second wallet owner.
 QLocalServer scopeLifecycle;scopeLifecycle.setSocketOptions(QLocalServer::UserAccessOption);
 const auto scopeName="pgn-horizon-scope-"+QString::fromLatin1(QCryptographicHash::hash((pgn::app::mutableDataRoot(root)+"/"+pgn::wallet::installedCoreGenesis(root)).toUtf8(),QCryptographicHash::Sha256).toHex());
 QPointer<QWidget> scopeWindow;
 pgn::app::HorizonUiLease scopeLease(pgn::app::mutableDataRoot(root)+"/.horizon-ui-"+pgn::wallet::installedCoreGenesis(root)+".lock");
 if(!scopeLease.error().isEmpty()){std::cerr<<scopeLease.error().toStdString()<<"\n";return 8;}
 if(!scopeLease.owned()){
  QLocalSocket existing;existing.connectToServer(scopeName);
  if(!existing.waitForConnected(1500)){std::cerr<<"The existing Horizon session could not be contacted\n";return 8;}
  existing.write(startupArguments.contains("--start-minimized")?"running\n":"activate\n");existing.flush();if(existing.bytesToWrite())existing.waitForBytesWritten(1000);return 0;
 }
 #ifndef Q_OS_WIN
 QLocalServer::removeServer(scopeName); // Safe only after exclusive ownership.
 #endif
 if(!scopeLifecycle.listen(scopeName)){std::cerr<<"Application activation listener is unavailable\n";return 8;}
 QObject::connect(&scopeLifecycle,&QLocalServer::newConnection,&app,[&]{
  while(scopeLifecycle.hasPendingConnections()){
   auto* socket=scopeLifecycle.nextPendingConnection();socket->setReadBufferSize(16);QTimer::singleShot(2000,socket,&QLocalSocket::disconnectFromServer);
   const auto activate=[socket,&scopeWindow]{if(!socket->canReadLine())return;const auto line=socket->readLine(16);if(line=="activate\n"&&scopeWindow){scopeWindow->showNormal();scopeWindow->raise();scopeWindow->activateWindow();}socket->disconnectFromServer();};
   QObject::connect(socket,&QLocalSocket::readyRead,&app,activate);QObject::connect(socket,&QLocalSocket::disconnected,socket,&QObject::deleteLater);if(socket->canReadLine())QTimer::singleShot(0,socket,activate);
  }
 });
 startupWindow.step(3,"Building the interface");
 const bool legacyResidentViewer=startup.developerLegacy&&(startupArguments.contains("--resident-viewer")||startupArguments.contains("--observe-only"));
 QString config=startup.developerLegacy&&argc>2&&!QString::fromLocal8Bit(argv[2]).startsWith("--")?QString::fromLocal8Bit(argv[2]):QString{};
 if(startup.developerLegacy&&config.isEmpty()&&QFile::exists(root+"/evidence/native-transport-recovered-20261012.json"))
  config=root+"/evidence/native-transport-recovered-20261012.json";
 QMainWindow window;window.setProperty("pgn_startup_profile_state",startup.profileState);window.setProperty("pgn_startup_testnet",testnetMode);window.setProperty("pgn_startup_developer_legacy",startup.developerLegacy);window.setProperty("pgn_startup_root",root);window.setWindowTitle(testnetMode?"PGN Horizon · TESTNET":"PGN Horizon · Prime Gap Network");window.setWindowIcon(QIcon(":/frontier/app-icon.png"));
 closeOwned=[&window,&app]{if(window.close())QTimer::singleShot(0,&app,&QCoreApplication::quit);};
 scopeWindow=&window;
 window.setProperty("pgn_update_application_root",pgn::app::cleanApplicationRoot(root));
 window.setProperty("pgn_update_data_root",pgn::app::mutableDataRoot(root));
 window.setProperty("pgn_update_genesis",pgn::wallet::installedCoreGenesis(root));
 window.setProperty("pgn_update_explicit_root",explicitLegacyRoot);
 struct CoreUpdateDrain {bool pending{},ready{};};
 const auto coreUpdate=std::make_shared<CoreUpdateDrain>();
 new pgn::ui::MiningWorkerCloseGuard(&window,[&window,&inspectWallet,closeOwned,coreUpdate]{
  if(!window.property("pgn_update_shutdown").toBool()||coreUpdate->ready)return true;
  if(!coreUpdate->pending){
   coreUpdate->pending=true;const QPointer<QWidget> alive(&window);
   pgn::ui::stopWalletCoreForUpdate(inspectWallet,[alive,closeOwned,coreUpdate](QString error){
    coreUpdate->pending=false;if(!alive)return;
    if(!error.isEmpty()){alive->setProperty("pgn_update_shutdown_error",error);alive->setProperty("pgn_update_shutdown",false);return;}
    coreUpdate->ready=true;QTimer::singleShot(0,alive.data(),closeOwned);
   });
  }
  return false;
 });
 updateOwned=[&window,&inspectWallet,&captureMiningUpdate,closeOwned,coreUpdate]{
  if(!inspectWallet)return QJsonObject{{"accepted",false},{"error","Wallet lifecycle is not ready"}};
  if(!window.property("pgn_update_shutdown").toBool()){
   try{pgn::update::saveMiningUpdateResume(window.property("pgn_update_data_root").toString(),window.property("pgn_update_genesis").toString(),captureMiningUpdate?captureMiningUpdate():QJsonArray{});}
   catch(const std::exception& error){return QJsonObject{{"accepted",false},{"error",QString::fromUtf8(error.what())}};}
   coreUpdate->ready=false;window.setProperty("pgn_update_shutdown_error",QString{});window.setProperty("pgn_update_shutdown",true);
   QTimer::singleShot(50,&window,closeOwned);
  }
  return QJsonObject{{"accepted",true},{"scope","ORDERED_MINING_DRAIN_THEN_OWNED_CORE_STOP"}};
 };

 if(const auto* screen=window.screen()){const auto size=screen->availableGeometry().size();window.resize(qMin(1380,qMax(800,size.width()-32)),qMin(970,qMax(600,size.height()-48)));}
 else window.resize(1380,970);
 window.setStyleSheet("QMainWindow,QWidget{background:#010b10;color:#c9eee7;}"+HorizonWorkspaceControlStyle());
 auto* shell=new QWidget;auto* box=new QVBoxLayout(shell);box->setContentsMargins(0,0,0,0);box->setSpacing(0);
  new HorizonNativeFrameTheme(&window);
  auto* top=new QFrame;top->setObjectName("horizonCoinNavigation");top->setStyleSheet(HorizonPrimaryNavigationStyle());auto* topBox=new QHBoxLayout(top);topBox->setContentsMargins(14,5,14,5);topBox->setSpacing(8);top->setFixedHeight(43);
 auto* pgn=new MiningStatusButton("PGN\nPRIME GAP SEARCH");pgn->setFixedWidth(260);pgn->setCheckable(true);pgn->setChecked(true);
  pgn->setObjectName("pgn-primary-mining-tab");pgn->setFixedHeight(33);topBox->addWidget(pgn);
 auto* walletButton=new QPushButton("WALLET\nPGN CORE CLIENT",top);walletButton->setObjectName("pgn-primary-wallet-tab");walletButton->setCheckable(true);walletButton->setFixedWidth(210);walletButton->setCursor(Qt::PointingHandCursor);
  auto* networkSummary=new QLabel("NETWORK · CONNECTING");networkSummary->setObjectName("pgn-global-network-status");networkSummary->setStyleSheet("font:9px monospace;color:#f6c566;padding:3px 12px;");
  auto networkObservation=std::make_shared<std::optional<pgn::wallet::ChainObservation>>();
  const auto displayNetwork=[walletButton,networkSummary,networkObservation,testnetMode,&inspectWallet]{
   const auto& anchor=*networkObservation;
   const auto link=inspectWallet?pgn::wallet::coreLinkState(inspectWallet->property("pgn_core_link_state").toString()):pgn::wallet::CoreLinkState::Starting;
   const auto now=QDateTime::currentDateTimeUtc();
   const auto condition=pgn::wallet::networkLinkCondition(link,anchor,now);
   const bool online=pgn::wallet::verifiedCoreOnline(link,anchor,now);
   const auto light=[](const QColor& color){QPixmap pixels(14,14);pixels.fill(Qt::transparent);QPainter p(&pixels);p.setRenderHint(QPainter::Antialiasing);p.setPen(Qt::NoPen);p.setBrush(color);p.drawEllipse(QRectF(3,3,8,8));return QIcon(pixels);};
   static const auto green=light(QColor("#39dc8a")),red=light(QColor("#ff5d6c"));
   walletButton->setText("WALLET\nPGN CORE CLIENT");const auto& walletLight=online?green:red;if(walletButton->icon().cacheKey()!=walletLight.cacheKey())walletButton->setIcon(walletLight);walletButton->setIconSize(QSize(14,14));
   walletButton->setToolTip(online?"Verified Core connected":"Waiting for a fresh, verified Core response");
   walletButton->setProperty("pgn_core_online",online);
   if(!online){
    using Condition=pgn::wallet::NetworkLinkCondition;
    const auto caption=condition==Condition::Connecting?QString("◷ NETWORK · CONNECTING"):
     condition==Condition::Disconnected?QString("● NETWORK · DISCONNECTED"):
     condition==Condition::Unverified?QString("● CORE · IDENTITY NOT VERIFIED"):
     condition==Condition::Unavailable?QString("◷ CORE READ UNAVAILABLE · RETRYING"):QString("◷ NETWORK STATUS · REFRESHING");
    const bool stopped=condition==Condition::Disconnected||condition==Condition::Unverified;
    networkSummary->setStyleSheet(QString("QLabel{color:%1;font:12px Consolas;padding:3px 8px;}").arg(stopped?"#ff6474":"#8dccff"));
    networkSummary->setText(caption);
    networkSummary->setToolTip(inspectWallet?inspectWallet->property("pgn_core_link_detail").toString():QString("Waiting for the verified Core"));
    return;
   }
   const auto peers=anchor->peers?QString::number(*anchor->peers):QStringLiteral("—");
   const bool idleChain=anchor->nativePeerCaughtUp&&anchor->oldTipAt(QDateTime::currentDateTimeUtc());
   const auto state=!anchor->synchronizing?QStringLiteral("SYNC UNKNOWN"):*anchor->synchronizing?QStringLiteral("SYNCING"):anchor->peers&&*anchor->peers==0?QStringLiteral("NO PEERS"):!anchor->peers?QStringLiteral("PEER STATUS UNAVAILABLE"):idleChain?QStringLiteral("CHAIN IDLE"):QStringLiteral("UP TO DATE");
   const auto networkStyle=QString("QLabel{color:%1;font:12px Consolas;padding:3px 8px;}").arg(idleChain?"#ffbd69":anchor->synchronizing&&*anchor->synchronizing?"#8dccff":"#69efc5");
   if(networkSummary->styleSheet()!=networkStyle)networkSummary->setStyleSheet(networkStyle);
   networkSummary->setText(QString(testnetMode?"%4 TESTNET QA · TECHNICAL HEIGHT %1 · %2 PEERS · %3":"%4 NETWORK · HEIGHT %1 · %2 PEERS · %3").arg(anchor->height).arg(peers,state).arg(anchor->synchronizing&&*anchor->synchronizing?QStringList{"◴","◷","◶","◵"}.at((QDateTime::currentMSecsSinceEpoch()/500)%4):QString("●")));
   if(testnetMode&&inspectWallet&&inspectWallet->property("pgn_network_gold_blocks").isValid())networkSummary->setText(QString("%5 TESTNET · GOLD BLOCKS %1 · LEDGER HEIGHT %2 · %3 PEERS · %4").arg(inspectWallet->property("pgn_network_gold_blocks").toLongLong()).arg(anchor->height).arg(peers,state).arg(anchor->synchronizing&&*anchor->synchronizing?QStringList{"◴","◷","◶","◵"}.at((QDateTime::currentMSecsSinceEpoch()/500)%4):QString("●")));
   networkSummary->setToolTip(QString("Verified Core observation: %1\nChain: %2\nTip: %3").arg(anchor->observedUtc.toUTC().toString(Qt::ISODateWithMs),anchor->genesis,anchor->tipHash));
  };
  auto* networkClock=new QTimer(&window);networkClock->setInterval(1000);QObject::connect(networkClock,&QTimer::timeout,&window,displayNetwork);networkClock->start();
 walletButton->setToolTip("Native PGN wallet integrated here, with verified chain identity and protected local keys.");
  walletButton->setFixedHeight(33);topBox->addWidget(walletButton);
 auto* identity=new QLabel("PRIME GAP NETWORK · GPU SOURCE / INDEPENDENT PROOF · WALLET SERVICE NOT CONNECTED");identity->setStyleSheet("color:#7dacb5;font:8px monospace;");identity->setWordWrap(true);identity->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);topBox->addWidget(identity,1);
  auto* tabs=new QTabWidget;auto* canvas=pgn::ui::createMiningCanvas();
  auto* miningPage=new QWidget;miningPage->setObjectName("pgn-mining-page");auto* miningLayout=new QVBoxLayout(miningPage);miningLayout->setContentsMargins(0,0,0,0);miningLayout->setSpacing(0);
  pgn::ui::configureMiningDashboard(canvas);miningLayout->addWidget(canvas);auto* ticketProgressRail=new pgn::ui::MiningTicketProgress;miningLayout->addWidget(ticketProgressRail);ticketProgressRail->bindSourceCanvas(canvas);
  auto* miningStartupNotice=new QLabel(miningPage);miningStartupNotice->setObjectName("pgn-mining-startup-notice");miningStartupNotice->setTextFormat(Qt::PlainText);miningStartupNotice->setWordWrap(true);miningStartupNotice->setStyleSheet("QLabel{color:#ffbd69;font-size:13px;padding:8px 14px;}");miningStartupNotice->hide();miningLayout->addWidget(miningStartupNotice);
  auto* testnetSummary=new QLabel("TESTNET · connecting to the mining engine",miningPage);testnetSummary->setObjectName("pgn-testnet-mining-summary");testnetSummary->setStyleSheet("font:12px monospace;color:#69efc5;padding:14px 18px;");testnetSummary->setWordWrap(true);auto* miningToggle=new QPushButton("Start mining",miningPage);miningToggle->setObjectName("pgn-testnet-mining-toggle");miningToggle->setCheckable(true);miningToggle->setMinimumHeight(36);miningToggle->hide();auto* testnetProgress=new QProgressBar(miningPage);testnetProgress->setObjectName("pgn-testnet-ticket-progress");testnetProgress->setRange(0,1000);testnetProgress->setFormat("Ticket progress · %p%");testnetSummary->hide();testnetProgress->hide();
  auto* miningResults=pgn::ui::createMiningResultsView(root);ticketProgressRail->bindResultsView(miningResults);auto* resultScroll=new QScrollArea;resultScroll->setObjectName("pgn-mining-results-scroll");resultScroll->setWidgetResizable(true);resultScroll->setFrameShape(QFrame::NoFrame);resultScroll->setWidget(miningResults);
  // In a short window the content keeps its readable minimum and scrolls;
  // the viewport itself may shrink so expanded Records never crosses footer.
  resultScroll->setMinimumHeight(0);resultScroll->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Ignored);miningLayout->addWidget(resultScroll,1);
  auto* records=pgn::ui::createRecordSubmissionView(root,nullptr,testnetMode);inspectRecords=records;pgn::ui::bindRecordEligibility(records,[miningResults,ticketProgressRail](const QJsonArray& pairs){pgn::ui::presentMiningRecordEligibility(miningResults,pairs);ticketProgressRail->setRecordEligibility(pairs);});miningLayout->addWidget(records);tabs->addTab(miningPage,"Mining");
 auto* productivityTimer=new QTimer(&window);productivityTimer->setInterval(1000);QObject::connect(productivityTimer,&QTimer::timeout,&window,[canvas,miningResults,&inspectWallet]{const auto width=inspectWallet?inspectWallet->property("pgn_network_ticket_width").toString():QString{};pgn::ui::presentMiningProductivity(miningResults,pgn::ui::miningDeviceRates(canvas),width);});productivityTimer->start();
 auto* bookings=new pgn::booking::BookingModel(root,&window);
 if(bookings->source().localValidation)identity->setText("PGN HORIZON · LOCAL ENGINE VALIDATION · NO NETWORK ACCEPTANCE");
 pgn::ui::bindBookingModel(canvas,bookings);
 inspectCanvas=canvas;
 if(!testnetMode)pgn::ui::bindObservationCatalog(canvas,pgn::app::mutableDataRoot(root)+"/runtime/prototype-observations.sqlite");
 if(!testnetMode)pgn::ui::bindNativeCatalog(canvas,pgn::app::mutableDataRoot(root)+"/runtime/scientific-history.sqlite");
 // New source gaps have no UTC dates. LENGTH opens the whole retained archive;
 // TIME remains available explicitly without inventing dates for those gaps.
 pgn::ui::selectNativeGraphAxis(canvas,"length");
 QWidget* community=nullptr;QTableWidget* testnetTickets=nullptr;
 pgn::ui::NetworkWorkView* networkWork=nullptr;
 if(testnetMode){networkWork=new pgn::ui::NetworkWorkView([&inspectWallet](int offset,auto consume){pgn::ui::readWalletTicketPage(inspectWallet,offset,std::move(consume));},[&inspectWallet](const QString& id,auto consume){pgn::ui::readWalletTicketDetails(inspectWallet,id,std::move(consume));},nullptr,[&inspectWallet](const QString& start,const QString& end,int bins,auto consume){pgn::ui::readWalletTicketAtlas(inspectWallet,start,end,bins,std::move(consume));});community=networkWork;}else community=pgn::ui::createCommunityView(bookings,nullptr,[canvas](const QString& id){return pgn::ui::miningDeviceAccent(canvas,id);});tabs->addTab(community,testnetMode?"Network work":"Community");
 bookings->onSelect=[tabs,community](const QString& id){tabs->setCurrentWidget(community);pgn::ui::selectCommunityBooking(community,id);};
  bookings->onMining=[tabs,miningPage]{tabs->setCurrentWidget(miningPage);};
 auto* lensGraph=pgn::ui::createNumberLensCanvas();
 pgn::ui::bindObservationCatalog(lensGraph,pgn::app::mutableDataRoot(root)+"/runtime/prototype-observations.sqlite");
 pgn::ui::bindNativeCatalog(lensGraph,pgn::app::mutableDataRoot(root)+"/runtime/scientific-history.sqlite");
 // Read the existing ALL archive once even if SSH/control never connects.
 // This display-only publication cannot open a run or satisfy SOURCE gates.
  pgn::ui::loadNativeArchiveIndex(pgn::app::mutableDataRoot(root)+"/runtime/scientific-history.sqlite",window,[canvas,lensGraph,miningResults](const QJsonObject& packet){
  pgn::ui::presentNativeIndex(canvas,packet);pgn::ui::presentNativeIndex(lensGraph,packet);
   pgn::ui::presentMiningResultsIndex(miningResults,packet);
 });
 auto* lensArchive=pgn::ui::createArchiveBrowser(root,lensGraph);
 inspectArchive=lensArchive->findChild<QWidget*>("pgn-native-archive-view");
 auto* lensTabs=new QTabWidget;lensTabs->setObjectName("pgn-number-lens-subtabs");lensTabs->tabBar()->hide();
 lensTabs->addTab(lensGraph,"Distribution");lensTabs->addTab(lensArchive,"Exact records");
 auto* lens=new QWidget;auto* lensBox=new QVBoxLayout(lens);lensBox->setContentsMargins(0,0,0,0);lensBox->setSpacing(0);
 lensBox->addWidget(HorizonCoinNavigation(lensTabs,{{"01  DISTRIBUTION","TIME / LENGTH / MERIT"},{"02  EXACT RECORDS","SOURCE + EXPORT"}},lens));lensBox->addWidget(lensTabs,1);
 tabs->addTab(lens,"Number lens");
 auto* benchmark=pgn::ui::createBenchmarkView(root);tabs->addTab(benchmark,"Benchmark");
 // Removed inherited navigation does not erase the underlying PGN tools.
 // Persistent nonmodal dialogs preserve selections/history across closing.
 auto* toolsButton=pgn::ui::createPublicProfileButton(top);auto* toolsMenu=toolsButton->menu();topBox->addWidget(toolsButton);
 const auto addTool=[&](const QString& title,QWidget* contents){auto* dialog=new QDialog(&window);dialog->setWindowTitle(title);dialog->resize(1180,820);
  auto* layout=new QVBoxLayout(dialog);layout->setContentsMargins(0,0,0,0);layout->addWidget(contents);
  QObject::connect(toolsMenu->addAction(title),&QAction::triggered,dialog,[dialog]{dialog->show();dialog->raise();dialog->activateWindow();});};
 auto* engineConfiguration=pgn::ui::createEngineConfigurationView(root);addTool("Engine configuration",engineConfiguration);
 inspectEngineConfiguration=engineConfiguration;
 QObject::connect(toolsMenu->addAction("Scientific archive · exact source records"),&QAction::triggered,lens,[tabs,lens,lensTabs]{tabs->setCurrentWidget(lens);lensTabs->setCurrentIndex(1);});
 // CPU inventory is observational. A discovered host cannot start work through this probe.
 auto cpuEndpoints=std::make_shared<QList<pgn::mining::CpuInventoryEndpoint>>();
 QFile cpuInventoryProfile(pgn::app::mutableDataRoot(root)+"/testnet/ui-runtime/cpu-inventory.json");
 if(testnetMode&&cpuInventoryProfile.open(QIODevice::ReadOnly)&&cpuInventoryProfile.size()<=32768){
  const auto endpoints=QJsonDocument::fromJson(cpuInventoryProfile.readAll()).object().value("endpoints").toArray();
  if(endpoints.size()<=8)for(const auto& entry:endpoints){const auto object=entry.toObject();pgn::mining::CpuInventoryEndpoint endpoint;
   endpoint.program=object.value("program").toString();endpoint.expectedHost=object.value("expected_host").toString();
   for(const auto& argument:object.value("arguments").toArray())endpoint.arguments.append(argument.toString());
   if(!endpoint.program.isEmpty()&&endpoint.arguments.size()<=32)cpuEndpoints->append(endpoint);
  }
 }else{
  const QString localProbe=QCoreApplication::applicationDirPath()+"/EngineRuntime/cpu/_internal/native/horizon-cpu-inventory"
#ifdef _WIN32
   ".exe"
#endif
   ;
  if(QFileInfo::exists(localProbe))cpuEndpoints->append({localProbe,{"--inventory"},{}});
 }
 cpuInventoryProfile.close();
 auto* cpuFleet=new pgn::mining::CpuNormalResources(&window);
 auto pendingUpdateResume=std::make_shared<QJsonArray>();
 try{*pendingUpdateResume=pgn::update::takeMiningUpdateResume(pgn::app::mutableDataRoot(root),pgn::wallet::installedCoreGenesis(root),startupArguments.contains("--update-healthcheck"));}
 catch(const std::exception& error){window.setProperty("pgn_update_resume_error",QString::fromUtf8(error.what()));}
 captureMiningUpdate=[canvas,cpuFleet]{
  auto result=cpuFleet->updateResumeChoices();const auto devices=canvas->property("pgn_update_gpu_intents").toJsonObject();
  for(auto i=devices.begin();i!=devices.end();++i){const auto entry=i.value().toObject();if(entry.value("wanted").toBool()&&entry.value("running").toBool()&&entry.value("phase")!="Needs attention"&&entry.value("phase")!="Paused")result.append(QJsonObject{{"kind","gpu"},{"device",i.key()}});}
  return result;
 };
 const bool localCpuOnly=!QFileInfo::exists(pgn::app::mutableDataRoot(root)+"/testnet/ui-runtime/cpu-controllers.json");
 const auto discoverCpus=[cpuEndpoints,canvas,pgn,cpuFleet,localCpuOnly,root,&window]{
  for(const auto& endpoint:*cpuEndpoints)pgn::mining::probeCpuInventory(endpoint,&window,
   [canvas,pgn,cpuFleet,localCpuOnly,root](const QJsonObject& inventory){
    pgn::ui::presentCpuInventory(canvas,inventory);pgn->present(pgn::ui::miningCardStates(canvas));
    if(localCpuOnly){
     const auto host=inventory.value("host").toString();
     const auto program=QCoreApplication::applicationDirPath()+"/EngineRuntime/cpu/horizon-cpu-controller"
#ifdef _WIN32
      ".exe"
#endif
      ;
     const auto engineState=pgn::wallet::normalEngineStateDirectory(root,pgn::wallet::installedCoreGenesis(root));
     if(QFileInfo::exists(program)&&!engineState.isEmpty()){pgn::mining::CpuControlEndpoint control;control.program=program;control.host=host;control.deviceId="CPU/"+host.toUpper()+"/"+QString::number(inventory.value("processor_group").toInt());control.arguments={"--workspace",engineState+"/cpu-controller/"+QString::number(inventory.value("processor_group").toInt()),"--piece-positions","100000000"};cpuFleet->add(control);}
    }
   },
   [canvas](const QString& error){canvas->setProperty("pgn_cpu_inventory_error",error);});
 };
 QTimer::singleShot(0,&window,discoverCpus);
 auto* cpuDiscoveryTimer=new QTimer(&window);cpuDiscoveryTimer->setInterval(300000);
 QObject::connect(cpuDiscoveryTimer,&QTimer::timeout,&window,discoverCpus);cpuDiscoveryTimer->start();
 cpuFleet->onTelemetry=[canvas,pgn](const QString&,const QJsonObject& inventory,const QJsonObject& status,bool connected){pgn::ui::presentCpuController(canvas,inventory,status,connected);pgn->present(pgn::ui::miningCardStates(canvas));};
 cpuFleet->onControl=[canvas,pgn](const QString& device,const QString& request,bool applied,const QString& detail){pgn::ui::resolveDeviceControl(canvas,device,request,applied,detail);pgn->present(pgn::ui::miningCardStates(canvas));};
 cpuFleet->onCloseReady=[&window,closeOwned]{QTimer::singleShot(0,&window,closeOwned);};
 new pgn::ui::MiningWorkerCloseGuard(&window,[cpuFleet]{return cpuFleet->requestClose();});
 QFile cpuControllers(pgn::app::mutableDataRoot(root)+"/testnet/ui-runtime/cpu-controllers.json");
 if(testnetMode&&cpuControllers.open(QIODevice::ReadOnly)&&cpuControllers.size()<=32768){
  const auto endpoints=QJsonDocument::fromJson(cpuControllers.readAll()).object().value("endpoints").toArray();
  if(endpoints.size()<=8)for(const auto& value:endpoints){const auto item=value.toObject();pgn::mining::CpuControlEndpoint endpoint;
   endpoint.deviceId=item.value("device_id").toString();endpoint.host=item.value("host").toString();endpoint.program=item.value("program").toString();
   for(const auto& argument:item.value("arguments").toArray())endpoint.arguments.append(argument.toString());
   if(!endpoint.program.isEmpty()&&endpoint.arguments.size()<=32)cpuFleet->add(endpoint);
  }
 }
 cpuControllers.close();
 auto* miningWorkspace=new QWidget;auto* miningBox=new QVBoxLayout(miningWorkspace);miningBox->setContentsMargins(0,0,0,0);miningBox->setSpacing(0);
 miningBox->addWidget(tabs,1);
 auto* major=new QStackedWidget;major->setObjectName("pgn-primary-workspaces");major->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);major->addWidget(miningWorkspace);
  startupWindow.step(4,"Opening the wallet connection and device controls");
  inspectWallet=pgn::ui::createWalletCoreView(root,[canvas,identity,networkObservation,displayNetwork](const auto& observation){
   *networkObservation=observation;displayNetwork();
  pgn::ui::presentNetwork(canvas,observation);
  identity->setText(observation?"PRIME GAP NETWORK · VERIFIED WALLET CORE · GPU SOURCE / INDEPENDENT PROOF":
   "PRIME GAP NETWORK · GPU SOURCE / INDEPENDENT PROOF · WALLET SERVICE NOT CONNECTED");
 },testnetMode);
  pgn::ui::bindWalletNetworkMetrics(inspectWallet,miningResults,[miningResults,networkObservation](const QJsonObject& metrics,const QString& error){const auto& anchor=*networkObservation;const bool same=error.isEmpty()&&anchor&&anchor->freshAt(QDateTime::currentDateTimeUtc())&&metrics.value("genesis")==anchor->genesis&&metrics.value("bestblockhash")==anchor->tipHash&&metrics.value("height").toInteger(-1)==anchor->height;pgn::ui::presentMiningNetworkMetrics(miningResults,same?metrics:QJsonObject{},same?anchor->genesis:QString{});});
  if(!startup.diagnostic.isEmpty()){auto* notice=new QLabel(startup.diagnostic,inspectWallet);notice->setObjectName("pgn-startup-network-notice");notice->setTextFormat(Qt::PlainText);notice->setWordWrap(true);if(auto* box=qobject_cast<QBoxLayout*>(inspectWallet->layout()))box->insertWidget(0,notice);}
  major->addWidget(inspectWallet);ticketProgressRail->bindTicketLookup([&inspectWallet](const QString& request,auto consume){pgn::ui::readWalletTicketDetails(inspectWallet,request,std::move(consume));});
 const auto attachAuditIdentity=[canvas,toolsButton](const QJsonObject& identityData){
  const auto address=identityData.value("address").toString(),genesis=identityData.value("genesis").toString();
  if(address.isEmpty()||!QRegularExpression("^[0-9a-f]{64}$").match(genesis).hasMatch())return;
  toolsButton->setProperty("pgn_public_mining_genesis",genesis);pgn::ui::presentPublicAuditIdentity(toolsButton,address);
  canvas->setProperty("pgn_cpu_audit_network_status","CPU qualification belongs to the normal ticket cycle; standalone audit allocation is disabled");
 };
 if(testnetMode){
  QFile identityFile(pgn::app::mutableDataRoot(root)+"/testnet/ui-runtime/mining-address.json");
  if(identityFile.open(QIODevice::ReadOnly)&&identityFile.size()<=8192)attachAuditIdentity(QJsonDocument::fromJson(identityFile.readAll()).object());
 }
 if(testnetMode){
  const auto exporter=QCoreApplication::applicationDirPath()+"/EngineRuntime/network/horizon-engine-host"
#ifdef _WIN32
   ".exe"
#endif
   ;
  auto* diagnostics=pgn::ui::createDiagnosticCaptureView(exporter,[canvas,inspectWallet,networkObservation,&window]{
   const auto select=[](const QJsonObject& source,std::initializer_list<const char*> keys){QJsonObject value;for(const auto* key:keys)if(source.contains(key))value.insert(key,source.value(key));return value;};
   QJsonObject network{{"status",canvas->property("pgn_global_network_status").toString()},{"core_state",inspectWallet->property("pgn_core_link_state").toString()}};
   if(*networkObservation){const auto& observation=**networkObservation;network.insert("height",observation.height);network.insert("genesis",observation.genesis);network.insert("tip_hash",observation.tipHash);network.insert("peers",observation.peers?QJsonValue(*observation.peers):QJsonValue{});network.insert("synchronizing",observation.synchronizing?QJsonValue(*observation.synchronizing):QJsonValue{});}
   QJsonArray cpus;const auto controllers=canvas->property("pgn_cpu_controller_states").toJsonObject();
   for(auto it=controllers.begin();it!=controllers.end();++it){if(cpus.size()>=8)break;cpus.append(select(it.value().toObject(),{"state","host","device_id","enabled","controller_connected","requested_threads","threads_active","active_workers","resident_workers","machine_load_percent","positions_per_second","rate_positions_per_second","completed_positions","total_positions","inflight_pieces","buffered_closed_pieces","wall_active_seconds","error","cpu_telemetry","memory_pages"}));}
   const auto* components=window.findChild<QWidget*>("pgn-component-status-strip");
   const auto software=pgn::ui::diagnosticComponentSnapshot(components?components->property("pgn_component_local_snapshot").toJsonObject():QJsonObject{});
   return QJsonObject{{"network",network},{"software",software},{"hardware",canvas->property("pgn_diagnostic_hardware").toJsonObject()},{"cpu",cpus},{"mining_control",pgn::ui::diagnosticMiningStartup(canvas)},
    {"ticket",select(canvas->property("pgn_network_ticket_frame").toJsonObject(),{"ticket_number","ticket_id","work_role","phase","bits","start","end","positions","positions_total","current","minimum_gap","speed_rate","average_rate","compute_tests_rate","source_age_ns","network_notice"})}};
  });
  tabs->addTab(diagnostics,"Diagnostics");
 }
 // Build the shared navigation after every page is present. The temporary
 // testnet recorder has its own page and remains idle until explicitly started.
 auto* miningNavigation=testnetMode
  ? HorizonCoinNavigation(tabs,{{"01  MINING","LIVE SEARCH"},{"02  NETWORK WORK","TICKETS + AUDITS"},{"03  NUMBER LENS","SCIENCE + EXACT RECORDS"},{"04  BENCHMARK","HARDWARE LAB"},{"05  DIAGNOSTICS","RECORD + EXPORT"}},miningWorkspace)
  : HorizonCoinNavigation(tabs,{{"01  MINING","LIVE SEARCH"},{"02  COMMUNITY","SHARED ORBIT"},{"03  NUMBER LENS","SCIENCE + EXACT RECORDS"},{"04  BENCHMARK","HARDWARE LAB"}},miningWorkspace);
 miningBox->insertWidget(0,miningNavigation);

 if(networkWork){networkWork->onOpenBlock=[major,walletButton,toolsButton,inspectWallet](const QString& hash){major->setCurrentIndex(1);walletButton->setChecked(true);toolsButton->show();if(auto* pages=inspectWallet->findChild<QTabWidget*>("pgn-wallet-subtabs"))pages->setCurrentIndex(1);pgn::ui::inspectWalletBlock(inspectWallet,hash);};networkWork->onOpenAudits=[major,walletButton,toolsButton,inspectWallet]{major->setCurrentIndex(1);walletButton->setChecked(true);toolsButton->show();if(auto* pages=inspectWallet->findChild<QTabWidget*>("pgn-wallet-subtabs"))pages->setCurrentIndex(4);};}
 auto* primaryGroup=new QButtonGroup(&window);primaryGroup->setExclusive(true);primaryGroup->addButton(pgn,0);primaryGroup->addButton(walletButton,1);
 QObject::connect(primaryGroup,&QButtonGroup::idClicked,major,[major,toolsButton](int id){major->setCurrentIndex(id);toolsButton->show();});
  showOwned=[&window,root,testnetMode,major,tabs,pgn,walletButton,toolsButton,canvas,miningPage,records,community,lens,lensGraph,benchmark,engineConfiguration](const QString& section){
  if(section=="MiningCpuAuditValidation"||section=="MiningCpuAttachValidation")return QJsonObject{{"error","Standalone CPU audit execution is retired; CPU qualification uses the normal-ticket cycle"},{"network_accepted",false}};
   const auto resizeMatch=QRegularExpression("^Mining-([0-9]{3,4})x([0-9]{3,4})$").match(section);
  const auto cpuAction=QRegularExpression("^MiningCpu([0-7])(On|Off|Threads([0-9]{1,2}))$").match(section);
  const bool resizeAction=resizeMatch.hasMatch()&&resizeMatch.captured(1).toInt()>=640&&resizeMatch.captured(2).toInt()>=480;
  const QStringList graphActions{"15MIN","1H","6H","1D","1W","1M","1Y","ALL","TIME","LENGTH","CURRENT","HISTORY","RESET"};
  const bool lensAction=section.startsWith("NumberLens-")&&graphActions.contains(section.mid(11));
   const bool miningGraphAction=section.startsWith("Mining-")&&graphActions.contains(section.mid(7));
   const bool recordsAction=QStringList{"RecordsExpand","RecordsCollapse","RecordsCheck"}.contains(section);
  const QStringList communityActions{"FIRSTOPEN","OUR","RESET","ZONE100","MININGZONE","EXACTINVALID","EXACTVALID"};
  const bool communityAction=section.startsWith("Community-")&&communityActions.contains(section.mid(10));
  const QStringList reservationActions{"Reservations","Reservations-Robert","Reservations-Benxy","Reservations-Wizz"};
   if(!cpuAction.hasMatch()&&!resizeAction&&!lensAction&&!miningGraphAction&&!recordsAction&&!communityAction&&!reservationActions.contains(section)&&!QStringList{"Mining","MiningMaximize","MiningOn","MiningOff","MiningExpand","MiningCollapse","MiningStatus","MiningTotalExpand","MiningTotalCollapse","MiningGroupGpuOn","MiningGroupGpuOff","MiningGroupCpuOn","MiningGroupCpuOff","Community","TicketFirst","TicketDomain","TicketActive","CommunityStatus","PublicRegister","NumberLens","NumberLensStatus","Benchmark","BenchmarkMeasure","BenchmarkStatus","Wallet","Audits","AuditsRegistry","Difficulty","Diagnostics","DiagnosticsStart","DiagnosticsStop","DiagnosticsExport","Vanity","VanityCheck","VanityStatus","VanityGenerateCPU","Network","NetworkInspectFirst","Configuration"}.contains(section))return QJsonObject{{"error","Unknown owned QA section"}};

  if(resizeAction){window.showNormal();window.resize(resizeMatch.captured(1).toInt(),resizeMatch.captured(2).toInt());}

   if(section=="PublicRegister"){
   major->setCurrentIndex(0);tabs->setCurrentWidget(community);
   auto* open=community->findChild<QPushButton*>("pgn-public-register-open");if(!open)return QJsonObject{{"error","Public register button unavailable"}};open->click();
   auto* dialog=community->findChild<QWidget*>("pgn-public-register-dialog");if(!dialog)return QJsonObject{{"error","Public register dialog unavailable"}};
   QApplication::processEvents();const auto capture=root+"/evidence/owned-"+section+"-"+QString::number(QCoreApplication::applicationPid())+".png";
   const bool saved=dialog->grab().save(capture);return QJsonObject{{"gui_pid",QCoreApplication::applicationPid()},{"section",section},{"capture",capture},{"saved",saved},{"mining_control_changed",false}};
  }

  if(section.startsWith("Vanity")&&section!="Vanity"){
   auto* view=window.findChild<QWidget*>("pgn-vanity-generator");if(!view)return QJsonObject{{"error","Vanity workspace unavailable"}};
   if(section=="VanityCheck"){auto* button=view->findChild<QPushButton*>("pgn-vanity-check");if(button&&button->isEnabled())button->click();}
   if(section=="VanityGenerateCPU"){
    if(view->property("pgn_vanity_busy").toBool())return QJsonObject{{"error","A real generation is already running"}};
    for(auto* box:view->findChildren<QCheckBox*>())box->setChecked(false);
    const auto boxes=view->findChildren<QCheckBox*>(QRegularExpression("^pgn-vanity-cpu-local-"));if(boxes.isEmpty())return QJsonObject{{"error","Actual local CPU inventory not ready"}};boxes.front()->setChecked(true);
    auto* prefix=view->findChild<QLineEdit*>("pgn-vanity-prefix");prefix->setText("PGNWizz");auto* button=view->findChild<QPushButton*>("pgn-vanity-generate");if(!button||!button->isEnabled())return QJsonObject{{"error","Actual generation unavailable"}};button->click();
   }
   QJsonArray boxes;for(auto* box:view->findChildren<QCheckBox*>())boxes.append(QJsonObject{{"name",box->objectName()},{"label",box->text()},{"checked",box->isChecked()},{"enabled",box->isEnabled()}});
   auto* state=view->findChild<QLabel*>("pgn-vanity-status");auto* address=view->findChild<QLineEdit*>("pgn-vanity-address");return QJsonObject{{"section",section},{"status",state?state->text():QString{}},{"address",address?address->text():QString{}},{"devices",boxes},{"busy",view->property("pgn_vanity_busy").toBool()},{"mining_control_changed",false}};
  }
  if(section=="NetworkInspectFirst"){
   auto* table=window.findChild<QTableWidget*>("pgn-native-network-blocks");if(!table||!table->rowCount())return QJsonObject{{"error","Actual native block table unavailable"}};table->setCurrentCell(0,0);QMetaObject::invokeMethod(table,"cellClicked",Qt::DirectConnection,Q_ARG(int,0),Q_ARG(int,0));return QJsonObject{{"section",section},{"selected_hash",table->item(0,5)->text()},{"mining_control_changed",false}};
  }
  if(reservationActions.contains(section)){
   major->setCurrentIndex(0);tabs->setCurrentWidget(community);
   auto* open=community->findChild<QPushButton*>("pgn-owner-directory-open");if(!open)return QJsonObject{{"error","Reservation directory button unavailable"}};open->click();
   auto* directory=community->findChild<QWidget*>("pgn-owner-directory-dialog");if(!directory)return QJsonObject{{"error","Reservation directory unavailable"}};
   if(auto* search=directory->findChild<QLineEdit*>("pgn-owner-search"))search->setText(section=="Reservations"?QString{}:section.mid(13));
   QApplication::processEvents();
   const auto capture=root+"/evidence/owned-"+section+"-"+QString::number(QCoreApplication::applicationPid())+".png";
   const bool saved=directory->grab().save(capture);
   return QJsonObject{{"gui_pid",QCoreApplication::applicationPid()},{"section",section},{"capture",capture},{"saved",saved},{"mining_control_changed",false}};
  }
  if(section=="CommunityStatus"){
   QJsonObject receipt{{"gui_pid",QCoreApplication::applicationPid()},{"mining_control_changed",false}};
   for(const auto* name:{"pgn-range-calendar-grid","pgn-source-atlas"}){
    auto* view=community->findChild<QWidget*>(QString::fromLatin1(name));if(!view)continue;
    QJsonObject values;
    for(const auto* property:{"pgn_calendar_parent","pgn_calendar_slots","pgn_calendar_first","pgn_calendar_last","pgn_calendar_processed","pgn_calendar_allocated","pgn_first_open_found","pgn_atlas_zoom_blocks","pgn_atlas_zoom_zones","pgn_atlas_current_visible","pgn_atlas_render_ns","pgn_catalog_page_zones"}){
     const auto value=view->property(property);if(value.isValid())values.insert(QString::fromLatin1(property),QJsonValue::fromVariant(value));
    }receipt.insert(QString::fromLatin1(name),values);
   }return receipt;
  }
   if(section=="NumberLensStatus"||section=="MiningStatus"){
    auto* graph=section=="MiningStatus"?canvas:lensGraph;
    auto receipt=pgn::ui::catalogDiagnostics(graph);
   receipt.insert("gui_pid",QCoreApplication::applicationPid());
    for(const auto* name:{"pgn_paint_ns","pgn_plot_cache_hit","pgn_native_visible_points","pgn_native_loading","pgn_native_error","pgn_native_target_revision","pgn_native_display_revision","pgn_native_frame_caption","pgn_catalog_time_points","pgn_catalog_total_points","pgn_graph_history_points","pgn_graph_current_points","pgn_native_history_caption","pgn_graph_x_ticks","pgn_graph_y_ticks","pgn_native_rows_read","pgn_native_render_ns"})receipt.insert(QString::fromLatin1(name),QJsonValue::fromVariant(graph->property(name)));
   receipt.insert("mining_control_changed",section=="MiningOn"||section=="MiningOff");return receipt;
  }
  if(section=="BenchmarkStatus"){
   const auto* memory=benchmark->findChild<QWidget*>("pgn-benchmark-memory-workspace");
   return QJsonObject{{"gui_pid",QCoreApplication::applicationPid()},{"receipt",benchmark->property("pgn_benchmark_last_receipt").toJsonObject()},{"error",benchmark->property("pgn_benchmark_error").toString()},{"saved_count",benchmark->property("pgn_benchmark_saved_count").toInt()},
    {"memory_executor_available",memory&&memory->property("pgn_benchmark_memory_available").toBool()},
    {"memory_search_running",memory&&memory->property("pgn_benchmark_memory_running").toBool()},
    {"memory_search_receipt",memory?memory->property("pgn_benchmark_memory_receipt").toJsonObject():QJsonObject{}},
    {"memory_search_error",memory?memory->property("pgn_benchmark_memory_error").toString():QString{}},{"mining_control_changed",false}};
  }
  // Showing a page is not showing the window. Hidden replacements must leave
  // the hidden/minimized state before acknowledging the owned show command.
  if(window.isMinimized())window.showNormal();if(section=="MiningMaximize")window.showMaximized();else if(!window.isMaximized())window.showNormal();window.raise();window.activateWindow();
   if(section=="Wallet"||section=="Audits"||section=="AuditsRegistry"||section=="Difficulty"||section=="Vanity"||section=="Network"){major->setCurrentIndex(1);walletButton->setChecked(true);toolsButton->show();if(auto* pages=major->widget(1)->findChild<QTabWidget*>("pgn-wallet-subtabs"))pages->setCurrentIndex((section=="Audits"||section=="AuditsRegistry")?4:section=="Difficulty"?3:section=="Vanity"?2:section=="Network"?1:0);}
   else{major->setCurrentIndex(0);pgn->setChecked(true);toolsButton->show();if(section!="Configuration")engineConfiguration->window()->hide();if(cpuAction.hasMatch()||section.startsWith("MiningTotal")||section.startsWith("MiningGroup")||section=="Mining"||section=="MiningMaximize"||section=="MiningOn"||section=="MiningOff"||section=="MiningExpand"||section=="MiningCollapse"||miningGraphAction||resizeAction||recordsAction)tabs->setCurrentWidget(miningPage);else if(section=="Community"||section=="TicketFirst"||section=="TicketDomain"||section=="TicketActive"||communityAction)tabs->setCurrentWidget(community);else if(section=="NumberLens"||lensAction)tabs->setCurrentWidget(lens);else if(section=="Benchmark"||section=="BenchmarkMeasure")tabs->setCurrentWidget(benchmark);
   else if(section.startsWith("Diagnostics")){auto* capture=window.findChild<QWidget*>("pgn-test-diagnostics");if(!capture)return QJsonObject{{"error","Testnet diagnostics are unavailable"}};tabs->setCurrentWidget(capture);if(section=="DiagnosticsStart"||section=="DiagnosticsStop"){const auto name=section=="DiagnosticsStart"?"pgn-diagnostics-start":"pgn-diagnostics-stop";if(auto* button=capture->findChild<QPushButton*>(name))button->click();}else if(section=="DiagnosticsExport"){QDir().mkpath(root+"/evidence");pgn::ui::saveDiagnosticCapture(capture,root+"/evidence/diagnostic-"+QString::number(QCoreApplication::applicationPid())+".zip");}}
   else if(auto* dialog=qobject_cast<QDialog*>(engineConfiguration->parentWidget())){dialog->show();dialog->raise();}}
  if(section=="AuditsRegistry"){auto* toggle=window.findChild<QToolButton*>("pgn-audit-registry-toggle");if(!toggle)return QJsonObject{{"error","Audit registry control is unavailable"}};if(!toggle->isChecked())toggle->click();}
  if(section.startsWith("MiningTotal")||section.startsWith("MiningGroup")){QRectF target;bool click=false;if(section.startsWith("MiningTotal")){target=canvas->property("pgn_total_expand_rectangle").toRectF();click=canvas->property("pgn_total_expanded").toBool()!=(section=="MiningTotalExpand");}else{const QString kind=section.contains("Gpu")?"gpu":"cpu";const QByteArray prefix=("pgn_total_"+kind).toLatin1();target=canvas->property((prefix+"_toggle_rect").constData()).toRectF();const auto state=canvas->property((prefix+"_state").constData()).toString();if(state=="unavailable"||state=="pending"||state.isEmpty())return QJsonObject{{"error","Group control is not ready"}};click=state!=(section.endsWith("On")?"on":"off");if(state=="mixed"&&!section.endsWith("On"))return QJsonObject{{"error","A mixed group starts all devices on its first click; request On before Off"}};}if(target.isEmpty())return QJsonObject{{"error","Total card is not rendered"}};if(click){const auto point=target.center();QMouseEvent press(QEvent::MouseButtonPress,point,canvas->mapToGlobal(point.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(canvas,&press);QMouseEvent release(QEvent::MouseButtonRelease,point,canvas->mapToGlobal(point.toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QCoreApplication::sendEvent(canvas,&release);}}
  if(section=="MiningExpand"||section=="MiningCollapse"){
   const auto device=canvas->property("pgn_managed_source_device").toString();const auto toggle=pgn::ui::miningToggleRect(canvas,device);if(toggle.isEmpty())return QJsonObject{{"error","Managed card is not yet rendered"}};const bool expanded=toggle.width()>100;
   if(expanded!=(section=="MiningExpand")){const auto point=toggle.center()+QPointF(expanded?77.5:51.5,0);QMouseEvent press(QEvent::MouseButtonPress,point,canvas->mapToGlobal(point.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(canvas,&press);QMouseEvent release(QEvent::MouseButtonRelease,point,canvas->mapToGlobal(point.toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QCoreApplication::sendEvent(canvas,&release);}
  }
  if(cpuAction.hasMatch()){
   const auto ids=canvas->property("pgn_cpu_control_devices").toStringList();const int index=cpuAction.captured(1).toInt();if(index>=ids.size())return QJsonObject{{"error","CPU controller is not ready"}};const auto id=ids[index];pgn::ui::scrollMiningDevicesEnd(canvas);const auto state=canvas->property("pgn_cpu_controller_states").toJsonObject().value(id).toObject();QPointF point;
   if(cpuAction.captured(2).startsWith("Threads")){const auto inventory=canvas->property("pgn_cpu_controller_inventory").toJsonObject().value(id).toObject();const int maximum=inventory.value("maximum_workers").toInt(),workers=cpuAction.captured(3).toInt();if(workers<1||workers>maximum)return QJsonObject{{"error","CPU thread request exceeds inventory"}};const auto slider=pgn::ui::miningBudgetRect(canvas,id);if(slider.isEmpty())return QJsonObject{{"error","CPU budget is not rendered"}};point=QPointF(slider.left()+slider.width()*(maximum>1?qreal(workers-1)/(maximum-1):0.),slider.center().y());
   }else{const bool wanted=cpuAction.captured(2)=="On";if(state.value("enabled").toBool()==wanted)return QJsonObject{{"section",section},{"already_applied",true},{"mining_control_changed",false}};const auto toggle=pgn::ui::miningToggleRect(canvas,id);if(toggle.isEmpty())return QJsonObject{{"error","CPU toggle is not rendered"}};point=toggle.center();}
   QMouseEvent press(QEvent::MouseButtonPress,point,canvas->mapToGlobal(point.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(canvas,&press);QMouseEvent release(QEvent::MouseButtonRelease,point,canvas->mapToGlobal(point.toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QCoreApplication::sendEvent(canvas,&release);
  }
  if(section=="MiningOn"||section=="MiningOff"){
   const auto device=canvas->property("pgn_managed_source_device").toString();const auto target=pgn::ui::miningToggleRect(canvas,device).center();if(target.isNull())return QJsonObject{{"error","Managed card toggle is unavailable"}};
   QMouseEvent press(QEvent::MouseButtonPress,target,canvas->mapToGlobal(target.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(canvas,&press);QMouseEvent release(QEvent::MouseButtonRelease,target,canvas->mapToGlobal(target.toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QCoreApplication::sendEvent(canvas,&release);
  }
  if(section=="TicketDomain"||section=="TicketActive"){auto* button=community->findChild<QPushButton*>(section=="TicketDomain"?"pgn-atlas-overview":"pgn-atlas-active");if(!button)return QJsonObject{{"error","Ticket atlas control unavailable"}};button->click();}
  if(section=="TicketFirst"){auto* table=community->findChild<QTableWidget*>("pgn-network-ticket-table");if(!table||!table->rowCount())return QJsonObject{{"error","Network tickets not available"}};table->setCurrentCell(0,0);QMetaObject::invokeMethod(table,"cellClicked",Qt::DirectConnection,Q_ARG(int,0),Q_ARG(int,0));}
  if(communityAction){
   const auto action=section.mid(10);
   if(action=="ZONE100"){
    auto* jump=community->findChild<QSpinBox*>("pgn-calendar-jump");
    if(!jump||jump->maximum()<100)return QJsonObject{{"error","Actual zone100 unavailable"}};
    jump->setValue(100);
   }else if(action=="EXACTINVALID"||action=="EXACTVALID"){
    auto* grid=community->findChild<QWidget*>("pgn-range-calendar-grid");
    auto* first=community->findChild<QLineEdit*>("pgn_booking_start");
    auto* last=community->findChild<QLineEdit*>("pgn_booking_end");
    auto* open=community->findChild<QPushButton*>("pgn-booking-exact-toggle");
    auto* apply=community->findChild<QPushButton*>("pgn-booking-exact-apply");
    if(!grid||!first||!last||!open||!apply)return QJsonObject{{"error","Exact selection controls unavailable"}};
    const auto cells=pgn::ui::rangeCalendarSlots(grid->property("pgn_calendar_first").toString(),grid->property("pgn_calendar_last").toString());
    if(cells.size()<2)return QJsonObject{{"error","Exact selection fixture unavailable"}};
    open->click();first->setText(action=="EXACTVALID"?cells[0].start:"not-an-integer");last->setText(cells[1].end);apply->click();
   }else{
    const auto name=action=="FIRSTOPEN"?"pgn-calendar-first-open":action=="OUR"?"pgn-source-atlas-focus":action=="RESET"?"pgn-source-atlas-reset":"pgn-calendar-active";
    auto* button=community->findChild<QPushButton*>(QString::fromLatin1(name));
    if(!button||!button->isEnabled())return QJsonObject{{"error","Actual community navigation unavailable"}};
    button->click();
   }
  }
  if(section=="BenchmarkMeasure"){auto* measure=benchmark->findChild<QPushButton*>("pgn-benchmark-measure");if(!measure||!measure->isEnabled())return QJsonObject{{"error","Actual source measurement unavailable or already running"},{"mining_control_changed",false}};measure->click();}
   if(recordsAction){
    if(section=="RecordsCheck"){if(auto* button=records->findChild<QPushButton*>("pgn-check-record-submissions"))button->click();}
    else if(auto* button=records->findChild<QToolButton*>("pgn-expand-record-submissions"))button->setChecked(section=="RecordsExpand");
   }
   if(lensAction||miningGraphAction){
    auto* graphWidget=miningGraphAction?canvas:lensGraph;
    QApplication::processEvents();
    const QRectF plot=graphWidget->property("pgn_graph_plot_rectangle").toRect();
   if(plot.isEmpty())return QJsonObject{{"error","Scientific graph not painted yet"}};
    const auto action=section.mid(miningGraphAction?7:11);QRectF hit;
   const auto preset=graphActions.indexOf(action);
   if(preset<8)hit=HorizonGraphControls::timeRangeButton(plot,preset);
   else if(action=="TIME"||action=="LENGTH")hit=HorizonGraphControls::axis(plot,action=="TIME"?0:1);
   else if(action=="CURRENT")hit=HorizonGraphControls::current(plot);
   else if(action=="HISTORY")hit=HorizonGraphControls::history(plot);
   else hit=HorizonGraphControls::reset(plot);
    QMouseEvent click(QEvent::MouseButtonPress,hit.center(),graphWidget->mapToGlobal(hit.center().toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(graphWidget,&click);
  }
  const auto path=root+"/evidence/owned-"+section+"-"+QString::number(QCoreApplication::applicationPid())+".png";
  QTimer::singleShot(500,&window,[&window,engineConfiguration,community,section,path]{
   if(section=="Configuration")engineConfiguration->window()->grab().save(path);
   else if(section=="Community-EXACTINVALID"){if(auto* dialog=community->findChild<QWidget*>("pgn-booking-exact-dialog"))dialog->grab().save(path);}
   else window.grab().save(path);
  });
  QJsonObject receipt{{"gui_pid",QCoreApplication::applicationPid()},{"section",section},{"capture",path},{"mining_control_changed",false},
   {"window_visible",window.isVisible()},{"window_minimized",window.isMinimized()}};
  if(section=="Community-EXACTINVALID"||section=="Community-EXACTVALID"){
   const auto* dialog=community->findChild<QWidget*>("pgn-booking-exact-dialog");receipt.insert("dialog_visible",dialog&&dialog->isVisible());
   const auto selected=pgn::ui::rangeCalendarSelection(community->findChild<QWidget*>("pgn-range-calendar-grid"));
   if(selected){receipt.insert("selected_start",selected->start);receipt.insert("selected_end",selected->end);}
  }
  return receipt;
 };
 box->addWidget(top);box->addWidget(major,1);
 auto* status=new QLabel("Connecting to the mining controller");inspectRuntimeStatus=status;status->setStyleSheet("font-size:12px;color:#9ab6c1;");
 status->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
 auto* hardwareNotice=new QLabel("GPU OBSERVATION PENDING");hardwareNotice->setObjectName("pgn-native-hardware-notice");
 hardwareNotice->setStyleSheet("font-size:11px;color:#9ab6c1;");
 auto* footer=new QWidget;footer->setObjectName("pgn-application-status-footer");footer->setStyleSheet("#pgn-application-status-footer{background:#071820;border-top:1px solid #23424c;}");
 auto* footerBox=new QVBoxLayout(footer);footerBox->setContentsMargins(12,5,12,5);footerBox->setSpacing(3);
 auto* connectionRow=new QHBoxLayout;connectionRow->setSpacing(12);networkSummary->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);connectionRow->addWidget(networkSummary,1);
 auto* componentStatus=new pgn::ui::ComponentStatusStrip(pgn::ui::walletComponentVersionsContext(root,pgn::wallet::coreRuntimeDirectory(root,testnetMode),testnetMode),footer);connectionRow->addWidget(componentStatus);footerBox->addLayout(connectionRow);
 auto* detailsRow=new QHBoxLayout;detailsRow->addWidget(status,1);detailsRow->addWidget(hardwareNotice);footerBox->addLayout(detailsRow);box->addWidget(footer);window.setCentralWidget(shell);
 // A reversible graph-only safety switch. Source journals and control paths
 // belong to the engine/bridge, never to the expensive visualization consumer.
 QFile graphPolicy(pgn::app::mutableDataRoot(root)+"/runtime/graph-policy.json");
 const bool graphsPaused=!testnetMode&&graphPolicy.open(QIODevice::ReadOnly) &&
     QJsonDocument::fromJson(graphPolicy.readAll()).object().value("inputs_paused").toBool();
 canvas->setProperty("pgn_graph_inputs_paused",graphsPaused);
 if(graphsPaused){auto* notice=new QLabel("GRAPH INPUT PAUSED · exact results still archived · mining and controls remain live");
  notice->setStyleSheet("font:8px monospace;color:#ffad4a;padding:3px 12px;");box->insertWidget(box->count()-1,notice);}
 pgn->present(pgn::ui::miningCardStates(canvas));
 pgn::mining::NativeBridge bridge;
#ifdef _WIN32
 std::unique_ptr<pgn::mining::NativeApplicationRuntime> nativeRuntime;
 std::unique_ptr<pgn::mining::NativeHardwareMonitor> hardwareMonitor;
#endif
 std::unique_ptr<pgn::mining::ApplicationMiningClose> applicationClose;
 std::unique_ptr<pgn::mining::ApplicationMiningStart> applicationStart;
 std::unique_ptr<pgn::verification::SourceProofRuntime> proofRuntime;
 const auto proofDisplayRun=std::make_shared<QString>();
 bookings->onDispatch=[&bridge](const QString& id){bridge.dispatch(id);};
 bridge.onDispatch=[bookings,status](const QString&,bool accepted,const QString& detail){bookings->reloadDispatchState();status->setText((accepted?"NEXT LOCAL RANGE SCHEDULED · ":"NEXT RANGE NOT CONFIRMED · ")+detail);};
  bridge.onDevice=[canvas,lensGraph,miningResults,bookings,pgn,benchmark,engineConfiguration](const auto& sample){pgn::ui::presentDevice(canvas,sample);pgn::ui::presentDevice(lensGraph,sample);pgn::ui::presentMiningResultsSource(miningResults,sample);pgn::ui::presentEngineTools(benchmark,sample);pgn::ui::presentEngineTools(engineConfiguration,sample);pgn->present(pgn::ui::miningCardStates(canvas));
  bookings->observeRuntime(sample.runId,sample.domainStart,sample.domainEnd,sample.gpuUuid,sample.gpuName,
      sample.rowsCompleted,sample.observedUtc,sample.raw.value("pgn_process_running").toBool());};
 bridge.onResources=[canvas,pgn,benchmark,bookings](const auto& sample){
  canvas->setProperty("pgn_hardware_observed_utc",sample.observedUtc.toUTC().toString(Qt::ISODateWithMs));
  canvas->setProperty("pgn_hardware_devices",sample.devices.size());
  canvas->setProperty("pgn_hardware_stale",sample.stale);
  if(sample.valid&&!sample.stale){
   const auto managed=canvas->property("pgn_managed_source_device").toString();
   for(const auto& device:sample.devices)if(device.kind=="GPU"&&device.deviceId.compare(managed,Qt::CaseInsensitive)==0)
    bookings->observeDeviceIdentity(device.deviceId,device.name,sample.observedUtc);
  }
  pgn::ui::presentResources(canvas,sample);pgn::ui::presentBenchmarkResources(benchmark,sample);pgn->present(pgn::ui::miningCardStates(canvas));};
 bridge.onTestDevice=[canvas,pgn](const auto& sample){pgn::ui::presentTestDevice(canvas,sample);pgn->present(pgn::ui::miningCardStates(canvas));};
 bridge.onGap=[canvas](const auto& event){pgn::ui::presentGap(canvas,event);};
 bridge.onControl=[canvas,pgn](const auto& device,const auto& id,bool applied,const auto& detail){pgn::ui::resolveDeviceControl(canvas,device,id,applied,detail);pgn->present(pgn::ui::miningCardStates(canvas));};
 bridge.onStatus=[status](const auto& text){status->setText(text);};
  bridge.onNativeIndex=[canvas,lensGraph,miningResults,&proofRuntime,proofDisplayRun](const auto& data){
  pgn::ui::presentNativeIndex(canvas,data);pgn::ui::presentNativeIndex(lensGraph,data);
   pgn::ui::presentMiningResultsIndex(miningResults,data);
  const auto run=data.value("current").toObject().value("scope").toString();
  static const QRegularExpression sourceRun("^[0-9a-f]{32}$");
  if(data.value("complete").toBool()&&!data.value("archive_only").toBool()&&sourceRun.match(run).hasMatch()&&run!=*proofDisplayRun){
   *proofDisplayRun=run;pgn::ui::presentLocalSourceProofStatus(canvas,{});
  }
  if(proofRuntime)proofRuntime->observeNativeIndex(data);
 };
 bridge.onSourceCompletion=[bookings](const auto& receipt,bool reload){bookings->observeSourceCompletion(receipt,reload);};
 bridge.onModuleRegistry=[engineConfiguration](const QJsonObject& packet){pgn::ui::presentEngineModuleRegistry(engineConfiguration,packet);};
 bridge.onModuleCapabilities=[engineConfiguration](const auto& declaration,const auto& observed,const auto& artifact){
  pgn::ui::presentEngineModuleCapabilities(engineConfiguration,declaration,observed,artifact);
 };
 bridge.onBatchBegin=[canvas]{pgn::ui::beginHistoryBatch(canvas);};
 bridge.onBatchEnd=[canvas]{pgn::ui::endHistoryBatch(canvas);};
 pgn::ui::bindMiningControls(canvas,[&bridge,&applicationStart](const auto& device,const auto& id,bool enabled){
  if(applicationStart)applicationStart->control(device,id,enabled);else bridge.control(device,id,enabled);
 });
 if(legacyResidentViewer&&!testnetMode){
  status->setText("OBSERVATION ONLY · saved history preserved · mining controls not attached");
  pgn::ui::bindMiningControls(canvas,{});
 #ifdef _WIN32
  if(!config.isEmpty()){
   const auto directory=QFileInfo(pgn::app::mutableDataRoot(root)+"/runtime").absoluteFilePath();
   const pgn::mining::NativeRuntimePaths paths{QFileInfo(bookings->historyDatabase()).absoluteFilePath(),directory+"/scientific-history.sqlite",directory+"/source-journals",directory+"/engine-modules.sqlite",QStringLiteral("C:\\PGN\\runtime\\current-job\\checkpoint")};
   const auto systemRoot=qEnvironmentVariable("SystemRoot");
   const auto transport=pgn::mining::loadNativeRuntimeConfiguration(QFileInfo(config).absoluteFilePath(),QDir::fromNativeSeparators(systemRoot)+"/System32/OpenSSH/ssh.exe",paths);
   if(transport.available()){
    hardwareMonitor=std::make_unique<pgn::mining::NativeHardwareMonitor>(transport.configuration->endpoint,&bridge);
     hardwareMonitor->onAvailability=[hardwareNotice,canvas](bool available,const QString& error){
     hardwareNotice->setText(available?"REMOTE GPU CONNECTED · READ ONLY":"REMOTE GPU OBSERVATION UNAVAILABLE");
     hardwareNotice->setToolTip(error);
      canvas->setProperty("pgn_native_hardware_availability",available);
      canvas->setProperty("pgn_native_hardware_notice",error);
    };
    if(!hardwareMonitor->start())hardwareNotice->setText("REMOTE GPU OBSERVATION NOT ATTACHED");
   }else hardwareNotice->setText("REMOTE GPU CONFIGURATION UNAVAILABLE · "+transport.error.left(120));
  }
 #endif
 }
 else if(!testnetMode&&!config.isEmpty()){
#ifdef _WIN32
  const QString runtimeDirectory=QFileInfo(pgn::app::mutableDataRoot(root)+"/runtime").absoluteFilePath();
  const QString checkpointLabel=QStringLiteral("C:\\PGN\\runtime\\current-job\\checkpoint");
  pgn::mining::NativeRuntimePaths paths{QFileInfo(bookings->historyDatabase()).absoluteFilePath(),
      runtimeDirectory+"/scientific-history.sqlite",runtimeDirectory+"/source-journals",runtimeDirectory+"/engine-modules.sqlite",checkpointLabel};
  const auto systemRoot=qEnvironmentVariable("SystemRoot");
  const auto transport=pgn::mining::loadNativeRuntimeConfiguration(QFileInfo(config).absoluteFilePath(),
      systemRoot.isEmpty()?QString{}:QDir::fromNativeSeparators(systemRoot)+"/System32/OpenSSH/ssh.exe",paths);
  canvas->setProperty("pgn_relay_runtime_source","native-in-process");
  canvas->setProperty("pgn_relay_program",QString{}); // No Node/service fallback.
  canvas->setProperty("pgn_relay_error",transport.error);
  if(transport.available()){
  paths=transport.configuration->paths;
  canvas->setProperty("pgn_source_only_control",transport.configuration->sourceOnly);
  try{
   pgn::mining::NativeApplicationRuntimeConfig runtimeConfig;
   runtimeConfig.transport=*transport.configuration;runtimeConfig.sourceRegistryDirectory=runtimeDirectory;
   runtimeConfig.timingAudit=QCoreApplication::arguments().contains("--native-timing-audit");
   const bool timingAudit=runtimeConfig.timingAudit;
   nativeRuntime=std::make_unique<pgn::mining::NativeApplicationRuntime>(std::move(runtimeConfig),&bridge);
   canvas->setProperty("pgn_managed_source_device",nativeRuntime->managedSourceDevice());
   // Same local resource policy as the qualified finite producer. It does not
   // define any chain rule. Private one-thread actor, not a backend/process.
   proofRuntime=std::make_unique<pgn::verification::SourceProofRuntime>(pgn::app::mutableDataRoot(root)+"/runtime/scientific-history.sqlite",
       pgn::app::mutableDataRoot(root)+"/runtime/native-source-certificates.sqlite",pgn::verification::SourceProofPolicy{4,{{128,32,32},10000},{10002,1048576,32,1}});
   proofRuntime->setEnabled(false);canvas->setProperty("pgn_cpu_audit_enabled",false);
  const QPointer<QWidget> proofCanvas(canvas);
   proofRuntime->onPage=[proofCanvas,proofDisplayRun](const pgn::verification::SourceProofPage& page){
    if(!proofCanvas)return;
    const QJsonObject value{{"scope","LOCAL_CERTIFIED_PAIRS_ONLY"},{"run",page.run},
     {"declared_identity_sha256",page.identitySha256},{"policy_sha256",page.policySha256},
     {"total",QString::number(page.total)},{"verified",QString::number(page.verifiedTotal)},{"rejected",QString::number(page.rejectedTotal)},
     {"after_gap_id",QString::number(page.after)},{"source_watermark",QString::number(page.through)},{"more",page.more},
     {"error",page.cancelled&&page.error.isEmpty()?QStringLiteral("Local proof cancelled without a complete outcome"):page.error},
     {"worker_observed_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},{"network_credit",false},{"range_coverage",false}};
    pgn::ui::presentLocalSourceProofStatus(proofCanvas,pgn::ui::projectLocalSourceProofStatus(*proofDisplayRun,value,proofCanvas->property("pgn_local_source_proof").value<QJsonObject>()));
   };
   QObject::connect(toolsMenu->addAction("Retry held local proof worker"),&QAction::triggered,&window,[&proofRuntime]{if(proofRuntime)proofRuntime->retry();});
   auto* cpuAudit=toolsMenu->addAction("Independent CPU audit");cpuAudit->setCheckable(true);cpuAudit->setChecked(false);
   QObject::connect(cpuAudit,&QAction::toggled,&window,[&proofRuntime,canvas](bool enabled){if(proofRuntime)proofRuntime->setEnabled(enabled);canvas->setProperty("pgn_cpu_audit_enabled",enabled);canvas->update();});
   if(timingAudit){
    struct AuditBudget {qsizetype bytes{};int events{},dropped{};};
    const auto budget=std::make_shared<AuditBudget>();
    nativeRuntime->onTiming=[canvas,budget](const QJsonObject& packet){
     const auto line=QJsonDocument(packet).toJson(QJsonDocument::Compact);
     // Diagnostic metadata only. Neither this byte budget nor a dropped
     // diagnostic event changes RAW admission, source age or close authority.
     if(budget->events>=64||line.size()>2048||budget->bytes+line.size()+1>49152){
      ++budget->dropped;canvas->setProperty("pgn_native_timing_dropped",budget->dropped);return;
     }
     ++budget->events;budget->bytes+=line.size()+1;
     canvas->setProperty("pgn_native_timing_events",budget->events);
     canvas->setProperty("pgn_native_timing_bytes",budget->bytes);
     canvas->setProperty("pgn_native_timing_dropped",budget->dropped);
     std::cout<<line.toStdString()<<std::endl;
    };
   }
   nativeRuntime->onStatus=[status](const QString& text){status->setText(text);};
   nativeRuntime->onPacket=[canvas,hardwareNotice](const QJsonObject& packet){
    const auto type=packet.value("type").toString();
    if(type=="native_hardware_availability"){
     const auto state=packet.value("state").toString();
     const bool heldDriver=canvas->property("pgn_native_driver_hold").toBool();
     if(state=="available")canvas->setProperty("pgn_native_driver_hold",false);
     const auto text=state=="available"?QStringLiteral("GPU DRIVER AVAILABLE"):
         state=="driver_unavailable"||heldDriver?QStringLiteral("GPU DRIVER UNAVAILABLE"):QStringLiteral("GPU OBSERVATION UNKNOWN");
     hardwareNotice->setText(text);hardwareNotice->setToolTip(text+'\n'+packet.value("observed_utc").toString()+'\n'+packet.value("error").toString().left(400));
     hardwareNotice->setStyleSheet(state=="available"?"font:8px monospace;color:#78b9c6;padding:3px 12px;":"font:8px monospace;color:#f6c566;padding:3px 12px;");
     canvas->setProperty("pgn_native_hardware_availability",packet.toVariantMap());canvas->setProperty("pgn_native_hardware_notice",text);
    }
    // Keep the real read-attempt outcome independent of both the shared
    // status label and the optional, bounded timing trace. It is not Ready.
    if(type=="native_source_attempt")canvas->setProperty("pgn_native_source_attempt",packet.toVariantMap());
    if(type=="application_start_inventory"||type=="native_work_boundary_inventory"||type=="application_close_inventory"){
     canvas->setProperty("pgn_native_inventory_type",type);
     canvas->setProperty("pgn_native_inventory_accepted",packet.value("accepted").toBool());
     canvas->setProperty("pgn_native_inventory_error",packet.value("error").toString().left(400));
     // Retain the bounded verifier receipt, including a precise refusal reason.
     // Inventory acquisition alone never establishes conserved resume lineage.
     canvas->setProperty("pgn_native_inventory_lineage",packet.value("snapshot").toObject().value("resume_lineage").toObject().toVariantMap());
     if(packet.value("retry_read_after_driver_recovery")==true){
      // Validated full physical inventory, not a rejected vendor CSV frame.
      // Subsequent UNKNOWN reads do not erase this retained failure reason;
      // only an actual positive vendor observation clears it.
      canvas->setProperty("pgn_native_driver_hold",true);hardwareNotice->setText("GPU DRIVER UNAVAILABLE");
      hardwareNotice->setToolTip(packet.value("error").toString().left(400));
      hardwareNotice->setStyleSheet("font:8px monospace;color:#f6c566;padding:3px 12px;");
      canvas->setProperty("pgn_native_hardware_notice",hardwareNotice->text());
     }
    }
   };
    // The explicit remote runtime determines the checkpoint label; it is never
    // opened locally or inferred from a packet.
   applicationClose=std::make_unique<pgn::mining::ApplicationMiningClose>(&window,&app,&bridge,
        paths.bookingDatabase,paths.checkpointLabel,nullptr,!transport.configuration->sourceOnly);
   applicationClose->onStatus=[status,canvas,&applicationClose](const QString& text){
    status->setText(text);
    canvas->setProperty("pgn_application_close_ready",applicationClose->readyToQuit());
    // SOURCE refreshes may replace the shared status label. Preserve the
    // independent close barrier's reason; a fresh inventory is not CloseReady.
    canvas->setProperty("pgn_application_close_status",text.left(512));
   };
   applicationStart=std::make_unique<pgn::mining::ApplicationMiningStart>(&bridge,
        paths.bookingDatabase,runtimeDirectory,nullptr,paths.checkpointLabel);
   applicationStart->onStatus=[status,bookings,canvas](const QString& text){bookings->reloadDispatchState();status->setText(text);canvas->setProperty("pgn_application_start_status",text.left(512));};
   // Same user-only local endpoint and same admitted application start path as
   // the painted control. No new daemon, SSH shortcut or physical ACK fiction.
   startOwned=[&](const QString& id){
    if(!nativeRuntime||nativeRuntime->stopped()||!applicationStart)return QJsonObject{{"request_delivered",false},{"error","Native application unavailable"}};
    applicationStart->control(nativeRuntime->managedSourceDevice(),id,true);
    const auto message=applicationStart->status();
    return QJsonObject{{"request_id",id},{"request_delivered",true},
        {"status",message},{"scope","DELIVERED_TO_APPLICATION_START_NOT_ADMISSION_OR_PHYSICAL_OWNERSHIP"}};
   };
   stopOwned=[&](const QString& id){
    if(!nativeRuntime||nativeRuntime->stopped()||!applicationStart)return QJsonObject{{"request_delivered",false},{"error","Native application unavailable"}};
    applicationStart->control(nativeRuntime->managedSourceDevice(),id,false);
    return QJsonObject{{"request_id",id},{"request_delivered",true},{"status",applicationStart->status()},
        {"scope","DELIVERED_TO_APPLICATION_STOP_NOT_PHYSICAL_ACK"}};
   };
   const QPointer<pgn::mining::NativeApplicationRuntime> runtimeOwner(nativeRuntime.get());
   const QPointer<pgn::verification::SourceProofRuntime> proofOwner(proofRuntime.get());
   applicationClose->onDrainBeforeQuit=[runtimeOwner,proofOwner,status](auto done){
    if(!runtimeOwner)throw std::runtime_error("Native runtime unavailable during observer cleanup");
    const auto drainNative=[runtimeOwner,status,done=std::move(done)]{
     if(!runtimeOwner){status->setText("Close held: native owner disappeared during proof drain");return;}
     if(runtimeOwner->stopped()){done();return;}
     const auto previous=runtimeOwner->onStopped;
     runtimeOwner->onStopped=[previous,done]{if(previous)previous();done();};runtimeOwner->stop();
    };
    if(!proofOwner||proofOwner->stopped()){drainNative();return;}
    const auto previous=proofOwner->onStopped;
    proofOwner->onStopped=[previous,drainNative]{if(previous)previous();drainNative();};proofOwner->stop();
   };
   maintenanceOwned=[runtimeOwner,&applicationClose]{
    // Distinct developer endpoint: stop ONLY this UI's observers/actuation,
    // never send the normal physical AppClose command. Drain before Quit.
    if(!runtimeOwner||!applicationClose)return QJsonObject{{"accepted",false},{"error","Owned native observer drain unavailable"}};
    const auto normalDrain=applicationClose->onDrainBeforeQuit;
    if(!normalDrain)return QJsonObject{{"accepted",false},{"error","Owned observer drain hook unavailable"}};
    // Freeze ONLY if requestMaintenanceDetach admits this Idle-phase request.
    // A refused maintenance request must not disrupt a normal close in flight.
    applicationClose->onDrainBeforeQuit=[runtimeOwner,normalDrain](auto done){
     if(!runtimeOwner)throw std::runtime_error("Native observer owner disappeared before maintenance freeze");
     runtimeOwner->stop();normalDrain(std::move(done));
    };
    const bool accepted=applicationClose->requestMaintenanceDetach();
    applicationClose->onDrainBeforeQuit=normalDrain;
    return QJsonObject{{"accepted",accepted},{"scope","DEVELOPER_UI_OBSERVER_DETACH_NOT_PHYSICAL_CLOSE_READY"}};
   };
   const bool attached=nativeRuntime->start();canvas->setProperty("pgn_native_runtime_started",attached);
   if(!attached)status->setText("Native runtime attachment refused · no Node/service fallback · no mining started");
  }
  catch(const std::exception& error){status->setText("Native runtime unavailable · "+QString::fromUtf8(error.what()).left(300));}
  }
  else status->setText("Native transport unavailable · "+transport.error.left(300));
#else
  status->setText("Native SOURCE runtime is not qualified on this platform · no Node/service fallback");
#endif
 }
 if(testnetMode){
  using GpuControl=std::function<void(const QString&,bool)>;
  auto gpuControls=std::make_shared<QHash<QString,GpuControl>>();
  auto* updateResumeTimer=new QTimer(&window);updateResumeTimer->setInterval(500);
  const auto updateResumeStarted=std::make_shared<QElapsedTimer>();updateResumeStarted->start();
  QObject::connect(updateResumeTimer,&QTimer::timeout,&window,[pendingUpdateResume,gpuControls,cpuFleet,canvas,status,updateResumeTimer,updateResumeStarted,&window]{
   if(window.property("pgn_closing").toBool()){updateResumeTimer->stop();return;}
   for(qsizetype index=pendingUpdateResume->size();index-->0;){const auto row=pendingUpdateResume->at(index).toObject();const auto device=row.value("device").toString();
    if(row.value("kind")=="cpu"){if(cpuFleet->restoreAfterUpdate(device,row.value("threads").toInt()))pendingUpdateResume->removeAt(index);}
    else{const auto control=gpuControls->constFind(device.toLower());if(control!=gpuControls->cend()&&canvas->property("pgn_vanity_gpu_lease").toString().isEmpty()){pendingUpdateResume->removeAt(index);(*control)("update-resume:"+QUuid::createUuid().toString(QUuid::Id128),true);}}
   }
   canvas->setProperty("pgn_update_resume_pending",*pendingUpdateResume);
   if(pendingUpdateResume->isEmpty())updateResumeTimer->stop();
   else if(updateResumeStarted->elapsed()>120000){updateResumeTimer->stop();const QString error="Some devices could not resume after the update. Check their cards and start them manually.";status->setText(error);window.setProperty("pgn_update_resume_error",error);}
  });
  if(!pendingUpdateResume->isEmpty())updateResumeTimer->start();
  auto ownedGpuSelected=std::make_shared<bool>(false);
  auto frameSelection=std::make_shared<pgn::ui::MiningFrameSelection>();
  pgn::ui::bindMiningResourceControls(canvas,[canvas,cpuFleet,gpuControls,pendingUpdateResume](const QString& device,const QString& id,bool on,int percent){
   for(qsizetype i=pendingUpdateResume->size();i-->0;)if(pendingUpdateResume->at(i).toObject().value("device").toString().compare(device,Qt::CaseInsensitive)==0)pendingUpdateResume->removeAt(i);
   if(cpuFleet->control(device,id,on,percent))return;
   if(on&&!canvas->property("pgn_vanity_gpu_lease").toString().isEmpty()){pgn::ui::resolveDeviceControl(canvas,device,id,false,"GPU reserved by the vanity search");return;}
   const auto found=gpuControls->constFind(device.toLower());
   if(found==gpuControls->cend()){pgn::ui::resolveDeviceControl(canvas,device,id,false,"No qualified local worker is attached");return;}
   (*found)(id,on);
  });
   const auto attachWorker=[&window,closeOwned,&startOwned,&stopOwned,ownedGpuSelected,
#ifdef _WIN32
   &bridge,&hardwareMonitor,
#endif
   canvas,miningStartupNotice,hardwareNotice,cpuFleet,ticketProgressRail,miningResults,records,testnetSummary,testnetProgress,pgn,status,gpuControls,inspectWallet,toolsButton,frameSelection](const QJsonObject& profile,QPushButton* miningToggle){
   const auto device=profile.value("gpu_uuid").toString();
   if(device.isEmpty()||gpuControls->contains(device.toLower()))return;
   pgn::ui::registerMiningSourceDevice(canvas,device);
   auto* stream=new QProcess(&window);QStringList arguments;for(const auto& value:profile.value("arguments").toArray())arguments.append(value.toString());
#ifdef _WIN32
   const auto hardware=profile.value("hardware_endpoint").toObject();
   if(!hardware.isEmpty()){
    pgn::mining::SshObservationEndpoint endpoint;
    endpoint.executable=profile.value("program").toString();endpoint.keyFile=hardware.value("key_file").toString();
    endpoint.host=hardware.value("host").toString();endpoint.user=hardware.value("user").toString();endpoint.port=quint16(hardware.value("port").toInt());endpoint.hostKeyAlias=hardware.value("host_key_alias").toString();
    hardwareMonitor=std::make_unique<pgn::mining::NativeHardwareMonitor>(endpoint,&bridge);
    hardwareMonitor->onAvailability=[hardwareNotice](bool available,const QString& error){hardwareNotice->setText(available?"GPU TELEMETRY CONNECTED":"GPU TELEMETRY UNAVAILABLE");hardwareNotice->setToolTip(error);};
    hardwareMonitor->start();
   }
#endif
   auto* connectionRecovery=new pgn::mining::NetworkControllerRecovery(stream,&window);
   auto applyMiningIntent=std::make_shared<std::function<void(bool)>>();
   auto networkMode=std::make_shared<bool>(false);auto pendingNetwork=std::make_shared<bool>(false);auto stopRequested=std::make_shared<bool>(false);auto pendingCard=std::make_shared<QStringList>();auto presentationSequence=std::make_shared<qint64>(0);auto gapRates=std::make_shared<pgn::mining::GapRateWindow>();
   gpuControls->insert(device.toLower(),[canvas,device,miningToggle,pendingCard,ticketProgressRail,applyMiningIntent](const QString& id,bool on){
    if(on&&!canvas->property("pgn_vanity_gpu_lease").toString().isEmpty()){pgn::ui::resolveDeviceControl(canvas,device,id,false,"GPU reserved by the vanity search");return;}
    *pendingCard={device,id,on?QString("on"):QString("off")};canvas->setProperty("pgn_network_desired_on",on);
    auto frame=canvas->property("pgn_network_ticket_frame").toJsonObject();frame.insert("phase",on?"Starting":"Stopping");ticketProgressRail->present(frame);(*applyMiningIntent)(on);
   });
   canvas->setProperty("pgn_local_gpu_profiles",gpuControls->size());
   pgn::ui::presentMiningStartup(canvas,miningStartupNotice,"ready","Prepared; computation waits for user ON");
   stream->setProgram(profile.value("program").toString());stream->setArguments(arguments);stream->setProcessChannelMode(QProcess::SeparateChannels);
   const auto sendPublicProfile=[stream,networkMode,toolsButton]{
    if(!*networkMode||stream->state()!=QProcess::Running||!toolsButton->property("pgn_public_profile_saved").toBool())return;
    const auto command=pgn::ui::publicProfileControl(toolsButton->property("pgn_public_name").toString());
    if(!command.isEmpty())stream->write(command);
   };
   QObject::connect(stream,&QProcess::started,stream,sendPublicProfile);
   if(auto* field=toolsButton->findChild<QLineEdit*>("pgn-public-profile-name"))QObject::connect(field,&QLineEdit::textChanged,stream,[sendPublicProfile](const QString&){sendPublicProfile();});
   if(auto* save=toolsButton->findChild<QPushButton*>("pgn-public-profile-save"))QObject::connect(save,&QPushButton::clicked,stream,sendPublicProfile);
   auto buffer=std::make_shared<QByteArray>();
    QObject::connect(stream,&QProcess::stateChanged,&window,[canvas,stream,device](QProcess::ProcessState state){canvas->setProperty("pgn_worker_stream_state",int(state));canvas->setProperty("pgn_worker_stream_pid",qint64(stream->processId()));auto intents=canvas->property("pgn_update_gpu_intents").toJsonObject();auto entry=intents.value(device).toObject();entry.insert("running",state==QProcess::Running);intents.insert(device,entry);canvas->setProperty("pgn_update_gpu_intents",intents);});
   QObject::connect(stream,&QProcess::errorOccurred,&window,[canvas,stream](QProcess::ProcessError){canvas->setProperty("pgn_worker_stream_error",stream->errorString().left(400));});
    QObject::connect(stream,&QProcess::readyReadStandardOutput,&window,[stream,buffer,profile,cpuFleet,miningResults,records,testnetSummary,testnetProgress,pgn,miningToggle,networkMode,stopRequested,canvas,pendingCard,presentationSequence,ticketProgressRail,gapRates,connectionRecovery,status,toolsButton,frameSelection]{
    const auto received=stream->readAllStandardOutput();canvas->setProperty("pgn_worker_stream_bytes",canvas->property("pgn_worker_stream_bytes").toLongLong()+received.size());buffer->append(received);if(buffer->size()>4194304){buffer->clear();testnetSummary->setText("Worker presentation exceeded its admitted size");return;}
    while(buffer->contains('\n')){const auto newline=buffer->indexOf('\n');const auto line=buffer->left(newline);buffer->remove(0,newline+1);auto packet=QJsonDocument::fromJson(line).object();if(pgn::ui::presentPublicProfileStatus(toolsButton,packet))continue;
     pgn::ui::presentPublicMiningAccess(toolsButton,packet.value("network_admission").toObject());if(cpuFleet->packet(stream,packet))continue;cpuFleet->minerReady(stream);
     canvas->setProperty("pgn_worker_stream_packets",canvas->property("pgn_worker_stream_packets").toLongLong()+1);canvas->setProperty("pgn_worker_stream_phase",packet.value("phase").toString());canvas->setProperty("pgn_worker_stream_notice",packet.value("message").toString().left(400));
     {auto intents=canvas->property("pgn_update_gpu_intents").toJsonObject();const auto device=profile.value("gpu_uuid").toString();auto entry=intents.value(device).toObject();entry.insert("phase",packet.value("phase"));intents.insert(device,entry);canvas->setProperty("pgn_update_gpu_intents",intents);}
     const bool sourceFresh=packet.value("source_stale").isBool()&&!packet.value("source_stale").toBool();
     connectionRecovery->phase(packet.value("phase").toString(),sourceFresh);
     const auto controlPhase=pgn::mining::networkControlPhase(packet.value("phase").toString().toStdString(),connectionRecovery->wanted(),connectionRecovery->needsAttention(),sourceFresh);
     if(controlPhase.suppressPresentation)continue;
     const bool showTicket=frameSelection->gpu(canvas->property("pgn_network_desired_on").toBool());
     if(packet.value("schema")!="PGN_HORIZON_MINING_VIEW_V1"){if(showTicket){auto phaseFrame=canvas->property("pgn_network_ticket_frame").toJsonObject();phaseFrame.insert("phase",packet.value("phase").toString("Starting"));ticketProgressRail->present(phaseFrame);}if(controlPhase.acknowledgeOff){*stopRequested=false;const QSignalBlocker block(miningToggle);miningToggle->setChecked(false);miningToggle->setEnabled(true);if(!pendingCard->isEmpty()&&pendingCard->at(2)=="off"){pgn::ui::resolveDeviceControl(canvas,pendingCard->at(0),pendingCard->at(1),true,"GPU released after checkpoint");pendingCard->clear();}}if(showTicket)testnetSummary->setText(packet.value("message").toString("Starting · waiting for the first coherent worker checkpoint"));if(packet.value("phase")=="Needs attention"){const QSignalBlocker block(miningToggle);miningToggle->setChecked(false);miningToggle->setEnabled(true);miningToggle->setText("Start mining");*stopRequested=false;if(!pendingCard->isEmpty()){pgn::ui::resolveDeviceControl(canvas,pendingCard->at(0),pendingCard->at(1),false,packet.value("message").toString());pendingCard->clear();}}continue;}
     if(packet.value("title").toString().isEmpty())packet.insert("title","Network ticket");
     if(showTicket){pgn::ui::presentMiningTicketData(miningResults,packet);pgn::ui::presentRecordTicketData(records,packet);}const auto phase=packet.value("phase").toString();const bool active=phase=="Searching";if(active&&!packet.value("source_stale").toBool()){const QString connected="Mining controller connected · Core verifies network state";if(status->text()!=connected)status->setText(connected);}if(*networkMode){const QSignalBlocker block(miningToggle);if(*stopRequested&&phase!="Paused"){miningToggle->setChecked(false);miningToggle->setEnabled(false);miningToggle->setText("Stopping…");}else{*stopRequested=false;miningToggle->setEnabled(true);const bool on=phase!="Paused"&&phase!="Needs attention";miningToggle->setChecked(on);miningToggle->setText(on?"Stop mining":"Start mining");}}const auto rate=[](QJsonValue value){return value.isDouble()?QString::number(value.toDouble()/1e9,'f',0)+" Gpos/s":QString("—");};
     if(showTicket){canvas->setProperty("pgn_network_ticket_frame",packet);ticketProgressRail->present(packet);}
     FrontierTelemetrySnapshot lane;lane.authoritative=true;lane.stale=packet.value("source_stale").toBool();lane.sequence=++*presentationSequence;lane.observedUtc=QDateTime::fromString(packet.value("observed_utc").toString(),Qt::ISODateWithMs);lane.runId=packet.value("ticket_id").toString();lane.sourceMode="live";lane.state=active?"running":phase=="Report ready"?"complete":"stopped";lane.terminalSealed=phase=="Report ready";lane.gpuUuid=profile.value("gpu_uuid").toString();lane.gpuName=profile.value("device_name").toString();lane.domainStart=packet.value("start").toString();lane.domainEnd=packet.value("end").toString();lane.domainCurrent=packet.value("current").toString();lane.rowsCompleted=packet.value("positions").toString();lane.rowsTotal=packet.value("positions_total").toString();lane.numberBits=packet.value("bits").toInt();lane.cumulativeGapCandidates=QString::number(packet.value("scopes").toObject().value("ticket").toObject().value("statistics").toObject().value("count_total").toInteger());
     if(active){if(packet.value("speed_rate").isDouble())lane.e2ePositionsPerSecond=packet.value("speed_rate").toDouble();if(packet.value("average_rate").isDouble())lane.e2eWindowPositionsPerSecond=packet.value("average_rate").toDouble();if(packet.value("compute_tests_rate").isDouble())lane.powmPerSecond=packet.value("compute_tests_rate").toDouble();}lane.raw.insert("pgn_work_role",packet.value("work_role"));lane.raw.insert("pgn_cumulative_tests",packet.value("tests"));lane.raw.insert("pgn_source_time_ns",packet.value("compute_time_ns"));lane.raw.insert("pgn_clock_namespace",packet.value("ticket_id"));lane.raw.insert("pgn_compute_epoch",packet.value("compute_epoch"));bool ageOk=false;const auto sourceAge=packet.value("source_age_ns").toString().toULongLong(&ageOk);if(ageOk)pgn::mining::stampSourceAge(lane.raw,sourceAge,0,pgn::mining::receiptMonotonicNs());bool positionOk=false,timeOk=false;const auto position=packet.value("positions").toString().toULongLong(&positionOk),computeNs=packet.value("compute_time_ns").toString().toULongLong(&timeOk);const auto count=packet.value("scopes").toObject().value("ticket").toObject().value("statistics").toObject().value("count_total").toInteger();const auto mean=gapRates->observe(packet.value("ticket_id").toString().toStdString(),active&&positionOk&&timeOk,computeNs,position,quint64(std::max(qint64(0),count)));lane.raw.insert("pgn_cumulative_processed_gaps",QString::number(count));if(mean){lane.gapPairsTestedPerSecond=mean->perSecond;lane.raw.insert("pgn_retained_gaps_per_billion_positions",mean->perBillionPositions);lane.raw.insert("pgn_processed_gaps_per_second",mean->perSecond);lane.raw.insert("pgn_retained_gaps_per_second",mean->perSecond);lane.raw.insert("pgn_gap_window_positions_per_second",mean->positionsPerSecond);lane.raw.insert("pgn_gap_window_ns",QString::number(mean->spanNs));lane.raw.insert("pgn_source_interval_ns",QString::number(mean->spanNs));}lane.raw.insert("pgn_device_enabled",*networkMode&&miningToggle->isChecked()&&phase!="Paused"&&phase!="Needs attention");lane.raw.insert("pgn_process_running",active);lane.raw.insert("pgn_native_min_gap",QString::number(packet.value("minimum_gap").toInt()));pgn::ui::presentDevice(canvas,lane);canvas->update();
     if(!pendingCard->isEmpty()&&((pendingCard->at(2)=="on"&&controlPhase.acknowledgeOn)||(pendingCard->at(2)=="off"&&controlPhase.acknowledgeOff))){pgn::ui::resolveDeviceControl(canvas,pendingCard->at(0),pendingCard->at(1),true,phase);pendingCard->clear();}
     if(showTicket)testnetSummary->setText(QString("%1 · %2    |    Speed %3    |    60 s average %4").arg(profile.value("device_name").toString(),*stopRequested?QString("Stopping · saving progress"):phase,rate(packet.value("speed_rate")),rate(packet.value("average_rate"))));
     const double width=packet.value("end").toString().toDouble()-packet.value("start").toString().toDouble();const double positions=packet.value("positions").toString().toDouble();if(width>0)testnetProgress->setValue(std::clamp(int(1000*positions/width),0,1000));
     pgn->present(pgn::ui::miningCardStates(canvas));
    }
   });
   QObject::connect(stream,&QProcess::readyReadStandardError,&window,[stream,status]{const auto error=stream->readAllStandardError();if(!error.isEmpty())status->setText(QString::fromUtf8(error).right(600));});
   const auto startNetwork=[stream,buffer,profile,cpuFleet,networkMode,pendingNetwork,testnetSummary,connectionRecovery,stopRequested]{if(stream->state()!=QProcess::NotRunning)return;connectionRecovery->launching();*stopRequested=!connectionRecovery->wanted();buffer->clear();QStringList args;for(const auto& value:profile.value("network_arguments").toArray())args.append(value.toString());if(args.isEmpty()){testnetSummary->setText("Mining controller is not configured");return;}*networkMode=true;*pendingNetwork=false;cpuFleet->attachMiner(stream);stream->setArguments(args);stream->start();testnetSummary->setText("Starting · requesting an automatic network ticket");};
   QObject::connect(stream,&QProcess::finished,&window,[testnetSummary,pgn,canvas,pendingNetwork,startNetwork,miningToggle,networkMode,connectionRecovery,cpuFleet,stream](int,QProcess::ExitStatus){cpuFleet->disconnected(stream);*networkMode=false;if(*pendingNetwork){startNetwork();return;}testnetSummary->setText(connectionRecovery->needsAttention()?QString("Mining controller needs attention · checkpoint retained"):QString("Reconnecting · saved ticket retained"));pgn->present(pgn::ui::miningCardStates(canvas));const QSignalBlocker block(miningToggle);miningToggle->setChecked(false);miningToggle->setEnabled(true);miningToggle->setText("Start mining");});
   *applyMiningIntent=[stream,networkMode,pendingNetwork,startNetwork,testnetSummary,miningToggle,stopRequested,canvas,connectionRecovery,device](bool on){
    if(on&&!canvas->property("pgn_vanity_gpu_lease").toString().isEmpty()){const QSignalBlocker block(miningToggle);miningToggle->setChecked(false);testnetSummary->setText("GPU reserved by the vanity search");return;}
    {const QSignalBlocker blocked(miningToggle);miningToggle->setChecked(on);}
    connectionRecovery->intent(on);canvas->setProperty("pgn_network_desired_on",on);
    {auto intents=canvas->property("pgn_update_gpu_intents").toJsonObject();auto entry=intents.value(device).toObject();entry.insert("wanted",on);intents.insert(device,entry);canvas->setProperty("pgn_update_gpu_intents",intents);}
    miningToggle->setText(on?"Stop mining":"Stopping…");if(on)*stopRequested=false;if(!on){*stopRequested=true;*pendingNetwork=false;miningToggle->setEnabled(false);}
    if(stream->state()==QProcess::NotRunning){connectionRecovery->restartIfClosed();return;}
    if(on&&!*networkMode){*pendingNetwork=true;if(stream->state()==QProcess::NotRunning)startNetwork();else stream->kill();return;}
    if(*networkMode&&stream->state()==QProcess::Running){stream->write(on?"{\"action\":\"resume\"}\n":"{\"action\":\"pause\"}\n");testnetSummary->setText(on?"Starting · resuming the saved checkpoint":"Stopping · saving the current checkpoint");}
   };
   QObject::connect(miningToggle,&QPushButton::toggled,&window,[applyMiningIntent](bool on){(*applyMiningIntent)(on);});
   if(!*ownedGpuSelected){
    *ownedGpuSelected=true;
    // The same normal controller as the visible ON/OFF control, with no
    // navigation, synthetic mouse event, focus change or CPU control.
    const auto requestMining=[&window,canvas,miningToggle,applyMiningIntent,device](const QString& id,bool on){
     if(window.property("pgn_closing").toBool())return QJsonObject{{"request_delivered",false},{"error","Application is closing"}};
     if(on&&!canvas->property("pgn_vanity_gpu_lease").toString().isEmpty())return QJsonObject{{"request_delivered",false},{"error","GPU reserved by vanity search"}};
     if(on&&!miningToggle->isEnabled())return QJsonObject{{"request_delivered",false},{"error","Mining control transition is pending"}};
     (*applyMiningIntent)(on);
     return QJsonObject{{"request_id",id},{"request_delivered",canvas->property("pgn_network_desired_on").toBool()==on},
       {"device_id",device},{"requested_enabled",on},{"scope","DELIVERED_TO_NORMAL_MINER_NOT_PHYSICAL_ACK"}};
    };
    startOwned=[requestMining](const QString& id){return requestMining(id,true);};
    stopOwned=[requestMining](const QString& id){return requestMining(id,false);};
   }
   connectionRecovery->mayReconnect=[&window,canvasGuard=QPointer<QWidget>(canvas)]{return canvasGuard&&!window.property("pgn_closing").toBool()&&canvasGuard->property("pgn_vanity_gpu_lease").toString().isEmpty();};
   connectionRecovery->reconnect=[startNetwork,canvas](bool wanted){canvas->setProperty("pgn_network_desired_on",wanted);canvas->setProperty("pgn_worker_reconnect_count",canvas->property("pgn_worker_reconnect_count").toInt()+1);startNetwork();};
   connectionRecovery->onWaiting=[canvas,testnetSummary,ticketProgressRail](int delay){canvas->setProperty("pgn_worker_reconnect_delay_ms",delay);testnetSummary->setText("Reconnecting · saved ticket retained");auto frame=canvas->property("pgn_network_ticket_frame").toJsonObject();frame.insert("phase","Reconnecting");canvas->setProperty("pgn_network_ticket_frame",frame);ticketProgressRail->present(frame);};
   connectionRecovery->disconnected=[canvas,profile,miningToggle,pendingCard,ticketProgressRail,connectionRecovery,presentationSequence]{
    canvas->setProperty("pgn_worker_stream_phase","Disconnected");auto frame=canvas->property("pgn_network_ticket_frame").toJsonObject();frame.insert("phase","Disconnected");frame.insert("source_stale",true);canvas->setProperty("pgn_network_ticket_frame",frame);ticketProgressRail->present(frame);
    FrontierTelemetrySnapshot stale;stale.sequence=++*presentationSequence;stale.observedUtc=QDateTime::currentDateTimeUtc();stale.sourceMode="live";stale.runId=frame.value("ticket_id").toString();stale.authoritative=true;stale.stale=true;stale.gpuUuid=profile.value("gpu_uuid").toString();stale.gpuName=profile.value("device_name").toString();stale.state="stopped";stale.raw.insert("pgn_device_enabled",false);stale.raw.insert("pgn_process_running",false);pgn::ui::presentDevice(canvas,stale);
    const QSignalBlocker blocked(miningToggle);miningToggle->setChecked(false);miningToggle->setEnabled(true);if(connectionRecovery->needsAttention()&&!pendingCard->isEmpty()){pgn::ui::resolveDeviceControl(canvas,pendingCard->at(0),pendingCard->at(1),false,"Control connection could not restart; checkpoint retained");pendingCard->clear();}
   };
   const QPointer<QWidget> gpuCanvas(canvas);const QPointer<QPushButton> gpuToggle(miningToggle);
   const auto controlState=[gpuCanvas,gpuToggle]{
    if(!gpuCanvas||!gpuToggle)return QString("unavailable");
    const auto phase=gpuCanvas->property("pgn_network_ticket_frame").toJsonObject().value("phase").toString();
    if(phase=="Paused"&&!gpuToggle->isChecked())return QString("off");
    if(phase=="Starting"||phase=="Stopping"||!gpuToggle->isEnabled())return QString("pending");
    if(phase=="Searching"||phase=="Report ready")return gpuToggle->isChecked()?QString("on"):QString("pending");
    return QString("unavailable");
   };
   const auto waitForGpu=[&window,gpuCanvas,controlState](bool wanted,const QString& lease,bool releaseOnFailure,std::function<void(QString)> done){
    auto* timer=new QTimer(&window);timer->setInterval(100);auto elapsed=std::make_shared<QElapsedTimer>();elapsed->start();
    QObject::connect(timer,&QTimer::timeout,timer,[timer,elapsed,gpuCanvas,controlState,wanted,lease,releaseOnFailure,done=std::move(done)]{
     const auto state=controlState();const bool reached=state==(wanted?QString("on"):QString("off"));
     if(!reached&&elapsed->elapsed()<45000)return;
     timer->stop();timer->deleteLater();
     if(!reached&&releaseOnFailure&&gpuCanvas&&gpuCanvas->property("pgn_vanity_gpu_lease").toString()==lease)gpuCanvas->setProperty("pgn_vanity_gpu_lease",QVariant{});
     done(reached?QString{}:QString("GPU transition was not confirmed; review the mining card before retrying"));
    });timer->start();
   };
   pgn::ui::VanityGpuServices vanityGpu;vanityGpu.device=device;vanityGpu.label=profile.value("device_name").toString("Selected GPU");vanityGpu.host=profile.value("local").toBool()?"local":"remote";
   vanityGpu.sshProgram=profile.value("program").toString();vanityGpu.remoteEndpoint=profile.value("hardware_endpoint").toObject();
   vanityGpu.state=controlState;
   vanityGpu.pause=[canvas,miningToggle,waitForGpu,controlState](const QString& lease,std::function<void(QString)> done){
    const auto held=canvas->property("pgn_vanity_gpu_lease").toString(),state=controlState();
    if(!held.isEmpty()||state!="on"&&state!="off"){done("GPU control is busy; no vanity worker started");return;}
    canvas->setProperty("pgn_vanity_gpu_lease",lease);
    if(state=="off"){done({});return;}
    canvas->setProperty("pgn_network_desired_on",false);miningToggle->setChecked(false);waitForGpu(false,lease,true,std::move(done));
   };
   vanityGpu.release=[&window,canvas,miningToggle,waitForGpu](const QString& lease,bool resume,std::function<void(QString)> done){
    if(canvas->property("pgn_vanity_gpu_lease").toString()!=lease){done("GPU reservation changed; automatic mining resume withheld");return;}
    canvas->setProperty("pgn_vanity_gpu_lease",QVariant{});
    if(!resume||window.property("pgn_closing").toBool()){done({});return;}
    canvas->setProperty("pgn_network_desired_on",true);miningToggle->setEnabled(true);miningToggle->setChecked(true);waitForGpu(true,lease,false,std::move(done));
   };
   pgn::ui::bindVanityGpu(inspectWallet,vanityGpu);
   auto closeRequested=std::make_shared<bool>(false),closeAllowed=std::make_shared<bool>(false);
   new pgn::ui::MiningWorkerCloseGuard(&window,[stream,networkMode,pendingNetwork,closeRequested,closeAllowed,testnetSummary,ticketProgressRail,canvas,inspectWallet,&window]{
    window.setProperty("pgn_closing",true);if(pgn::ui::drainVanityForClose(inspectWallet,[&window]{window.close();}))return false;
    if(*closeAllowed||!*networkMode||stream->state()==QProcess::NotRunning)return true;
    if(!*closeRequested){*closeRequested=true;*pendingNetwork=false;stream->write("{\"action\":\"quit\"}\n");testnetSummary->setText("Closing · saving checkpoint and releasing GPU");auto frame=canvas->property("pgn_network_ticket_frame").toJsonObject();frame.insert("phase","Stopping");ticketProgressRail->present(frame);}return false;
   });
   QObject::connect(stream,&QProcess::finished,&window,[&window,closeOwned,closeRequested,closeAllowed](int code,QProcess::ExitStatus status){if(*closeRequested&&code==0&&status==QProcess::NormalExit){*closeAllowed=true;QTimer::singleShot(0,&window,closeOwned);}});
   QObject::connect(stream,&QProcess::started,&window,[stream,networkMode,stopRequested,canvas]{canvas->setProperty("pgn_worker_stream_error",QVariant{});if(*networkMode&&*stopRequested)stream->write("{\"action\":\"pause\"}\n");});
   stream->start();hardwareNotice->setText(profile.value("local").toBool()?"LOCAL · OPTIMIZED ENGINE":"REMOTE · OWNED OPTIMIZED ENGINE");status->setText("TESTNET · live worker presentation · Core verifies network state");
  };
   cpuFleet->onNormalFrame=[canvas,miningResults,records,ticketProgressRail,frameSelection](QJsonObject frame){
   canvas->setProperty("pgn_cpu_normal_frame",frame);
   if(frame.value("schema")!="PGN_HORIZON_MINING_VIEW_V1"||!frameSelection->cpu(canvas->property("pgn_network_desired_on").toBool()))return;
   pgn::ui::presentMiningTicketData(miningResults,frame);pgn::ui::presentRecordTicketData(records,frame);
   canvas->setProperty("pgn_network_ticket_frame",frame);ticketProgressRail->present(frame);
  };
  const auto cpuBundle=QCoreApplication::applicationDirPath()+"/EngineRuntime";
  const auto cpuHost=cpuBundle+"/network/horizon-engine-host"
#ifdef _WIN32
   ".exe"
#endif
   ;
  auto* cpuPrepareTimer=new QTimer(&window);cpuPrepareTimer->setInterval(3000);auto cpuPreparing=std::make_shared<bool>(false);
  const auto prepareCpu=[cpuPreparing,cpuPrepareTimer,cpuHost,cpuBundle,root,inspectWallet,cpuFleet,canvas,&window]{
   if(*cpuPreparing||!QFileInfo::exists(cpuHost))return;*cpuPreparing=true;
   pgn::ui::readWalletLocalConnection(inspectWallet,[cpuPreparing,cpuPrepareTimer,cpuHost,cpuBundle,root,cpuFleet,canvas,&window](QJsonObject context,QString error){
    if(!error.isEmpty()){*cpuPreparing=false;canvas->setProperty("pgn_cpu_normal_preparation",error);return;}
    const auto stateRoot=pgn::wallet::normalEngineStateDirectory(root,context.value("genesis").toString());
    if(stateRoot.isEmpty()){*cpuPreparing=false;canvas->setProperty("pgn_cpu_normal_preparation","Verified network identity is required");return;}
    pgn::engine::localRuntimeQuery(&window,cpuHost,{"--prepare-local-cpu","--bundle",cpuBundle,"--state-root",stateRoot},context,
     [cpuPreparing,cpuPrepareTimer,cpuFleet,canvas](QJsonObject value,QString error){
      *cpuPreparing=false;
      if(!error.isEmpty()||value.value("schema")!="PGN_LOCAL_RUNTIME_PROFILES_V1"||value.value("mining_started")!=false||value.value("allocation_requested")!=false||!cpuFleet->configureNormalMiner(value.value("cpu_profile").toObject())){canvas->setProperty("pgn_cpu_normal_preparation",error.isEmpty()?QString("CPU normal profile not ready"):error);return;}
      canvas->setProperty("pgn_cpu_normal_preparation","Ready; waiting for CPU ON");cpuPrepareTimer->stop();
     });
   });
  };
  QObject::connect(cpuPrepareTimer,&QTimer::timeout,&window,prepareCpu);cpuPrepareTimer->start();QTimer::singleShot(0,&window,prepareCpu);
  QFile streamProfile(pgn::app::mutableDataRoot(root)+"/testnet/ui-runtime/worker-stream.json");
  if(streamProfile.open(QIODevice::ReadOnly)&&streamProfile.size()<=32768){
   const auto profile=QJsonDocument::fromJson(streamProfile.readAll()).object();streamProfile.close();attachWorker(profile,miningToggle);
  }else{
   testnetSummary->setText("Complete wallet setup to prepare local mining and auditing");
   pgn::ui::presentMiningStartup(canvas,miningStartupNotice,"waiting_wallet","Complete wallet setup to enable mining");
   const auto bundle=QCoreApplication::applicationDirPath()+"/EngineRuntime";
   const auto host=bundle+"/network/horizon-engine-host"
#ifdef _WIN32
    ".exe"
#endif
    ;
   auto* prepareTimer=new QTimer(&window);prepareTimer->setInterval(3000);auto preparing=std::make_shared<bool>(false);
   const auto prepareLocal=[preparing,prepareTimer,host,bundle,root,inspectWallet,canvas,miningStartupNotice,testnetSummary,miningToggle,attachWorker,attachAuditIdentity,&window]{
    if(*preparing)return;
    if(!QFileInfo::exists(host)){pgn::ui::presentMiningStartup(canvas,miningStartupNotice,"host_missing","The mining controller is missing from the extracted application folder");return;}
    *preparing=true;
    pgn::ui::readWalletLocalConnection(inspectWallet,[preparing,prepareTimer,host,bundle,root,canvas,miningStartupNotice,testnetSummary,miningToggle,attachWorker,attachAuditIdentity,&window](QJsonObject context,QString error){
     if(!error.isEmpty()){*preparing=false;pgn::ui::presentMiningStartup(canvas,miningStartupNotice,"wallet_unavailable",error);return;}
     const auto stateRoot=pgn::wallet::normalEngineStateDirectory(root,context.value("genesis").toString());
     if(stateRoot.isEmpty()){*preparing=false;pgn::ui::presentMiningStartup(canvas,miningStartupNotice,"identity_unverified","Verified network identity is required");return;}
     pgn::ui::presentMiningStartup(canvas,miningStartupNotice,"preparing","Checking the local mining controller and GPU files");
     pgn::engine::localRuntimeQuery(&window,host,{"--prepare-local","--bundle",bundle,"--state-root",stateRoot},context,
      [preparing,prepareTimer,canvas,miningStartupNotice,testnetSummary,miningToggle,attachWorker,attachAuditIdentity,&window](QJsonObject value,QString error){
       *preparing=false;
       if(!error.isEmpty()||value.value("schema")!="PGN_LOCAL_RUNTIME_PROFILES_V1"||value.value("mining_started")!=false||value.value("allocation_requested")!=false||!value.value("profiles").isArray()||value.value("profiles").toArray().size()>32){pgn::ui::presentMiningStartup(canvas,miningStartupNotice,"prepare_failed",error.isEmpty()?QString("Local preparation was refused"):error);return;}
       attachAuditIdentity(value.value("audit_identity").toObject());
       int index=0;for(const auto& item:value.value("profiles").toArray()){
        auto* toggle=index++?new QPushButton(&window):miningToggle;toggle->setCheckable(true);toggle->hide();attachWorker(item.toObject(),toggle);
       }
       canvas->setProperty("pgn_local_runtime_unsupported",value.value("unsupported"));
       if(canvas->property("pgn_local_gpu_profiles").toInt()>0){prepareTimer->stop();pgn::ui::presentMiningStartup(canvas,miningStartupNotice,"ready","Prepared; computation waits for user ON");}
       else{prepareTimer->setInterval(15000);const auto hardware=value.value("hardware").toObject();const auto unsupported=value.value("unsupported").toArray();const auto reason=!unsupported.isEmpty()?unsupported.first().toObject().value("reason").toString():hardware.value("reason").toString();pgn::ui::presentMiningStartup(canvas,miningStartupNotice,"gpu_unavailable",reason.isEmpty()?QString("No compatible local GPU worker is available; retrying automatically"):reason);}
      });
    });
   };
   QObject::connect(prepareTimer,&QTimer::timeout,&window,prepareLocal);prepareTimer->start();QTimer::singleShot(0,&window,prepareLocal);
   auto* hardwareTimer=new QTimer(&window);hardwareTimer->setInterval(5000);auto readingHardware=std::make_shared<bool>(false);
   const auto readHardware=[host,readingHardware,canvas,pgn,benchmark,hardwareNotice,&window]{
    if(*readingHardware||!QFileInfo::exists(host))return;*readingHardware=true;
    pgn::engine::localRuntimeQuery(&window,host,{"--hardware"},{},[readingHardware,canvas,pgn,benchmark,hardwareNotice](QJsonObject value,QString error){
     *readingHardware=false;const auto sample=pgn::engine::localHardwareSnapshot(value);
     if(sample){pgn::ui::presentResources(canvas,*sample);pgn::ui::presentBenchmarkResources(benchmark,*sample);pgn->present(pgn::ui::miningCardStates(canvas));}
     hardwareNotice->setText(sample?"LOCAL · GPU TELEMETRY CONNECTED":"LOCAL · GPU TELEMETRY UNAVAILABLE");hardwareNotice->setToolTip(error.isEmpty()?value.value("reason").toString():error);
    });
   };
   QObject::connect(hardwareTimer,&QTimer::timeout,&window,readHardware);hardwareTimer->start();QTimer::singleShot(0,&window,readHardware);
  }

 }
 // Binding requests the catalogue immediately, so do it after attachment.
 pgn::ui::bindEngineModuleActions(engineConfiguration,[&bridge](const QJsonObject& command){bridge.moduleAction(command);});
 QObject::connect(&app,&QCoreApplication::aboutToQuit,&window,[canvas,status]{
  // A bounded pre-exit receipt; process exit must be observed separately.
  QJsonObject receipt{{"type","pgn_application_about_to_quit"},{"pid",QCoreApplication::applicationPid()},
      {"utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
      {"native_started",canvas->property("pgn_native_runtime_started").toBool()},
      {"close_ready",canvas->property("pgn_application_close_ready").toBool()},
      {"status",status->text().left(600)}};
  if(QCoreApplication::arguments().contains("--native-timing-audit")){
   for(const auto* name:{"pgn_native_timing_events","pgn_native_timing_bytes","pgn_native_timing_dropped"})
    receipt.insert(QString::fromLatin1(name),QJsonValue::fromVariant(canvas->property(name)));
  }
  QFile out;if(out.open(stdout,QIODevice::WriteOnly))out.write(QJsonDocument(receipt).toJson(QJsonDocument::Compact)+"\n");
 });
 // Native history is admitted in one publication batch and painted through
 pgn::ui::bindVerificationReceipts(canvas,pgn::app::mutableDataRoot(root)+"/runtime/independent-gap-verifications.sqlite",
     pgn::verification::localArchiveValidatorPins());
 // the cached dense layer. The conserved prototype archive remains paginated
 // until its complete bounded-memory projection is connected.
 // A new read-only viewer can be qualified without covering the operator's
 // foreground application; no input or visibility mutation of other windows.
 const bool startHidden=QCoreApplication::arguments().contains("--start-hidden");
 if(!startHidden){
 #ifdef PGN_UI_START_MINIMIZED
 if(testnetMode)window.show();else window.showMinimized();
 #else
 if(QCoreApplication::arguments().contains("--start-minimized"))window.showMinimized();
 else window.show();
 #endif
 }
 // Fit after Qt has resolved every page's minimum and the native title frame.
 // Initial resize alone can be silently enlarged by a hidden tab's layout.
 QTimer::singleShot(0,&window,[&window,miningPage]{if(const auto* screen=window.screen()){
  const auto available=screen->availableGeometry();const auto frame=window.frameGeometry().size()-window.size();
  // The ignored hidden-page hints must not let a user compress Mining below
  // its measured readable section height. Never impose a minimum taller than
  // the actual work area; very small displays remain separately unqualified.
  window.setMinimumHeight(pgn::ui::MiningLayout::minimumWindowHeight(
       window.height()-miningPage->height(),available.height()-frame.height()-16));
  window.resize(qMin(1380,available.width()-frame.width()-16),qMin(970,available.height()-frame.height()-16));
  window.move(available.center()-QPoint(window.frameGeometry().width()/2,window.frameGeometry().height()/2));
 }});
  new pgn::ui::WindowScreenFit(window,[&window,miningPage]{return window.height()-miningPage->height();});
 if(argc>4&&QString::fromLocal8Bit(argv[4])=="--show-community")tabs->setCurrentWidget(community);
 const bool showWallet=argc>4&&QString::fromLocal8Bit(argv[4])=="--show-wallet";
 if(showWallet){major->setCurrentIndex(1);walletButton->setChecked(true);toolsButton->show();}
 // Optional QA capture uses the actual visible window, never a mock model.
 if(argc>3&&!QString::fromLocal8Bit(argv[3]).startsWith("--")){const QString capture=QString::fromLocal8Bit(argv[3]);
  QTimer::singleShot(20000,&window,[&window,canvas,community,capture]{
   window.grab().save(capture);
   const QJsonObject receipt{{"gui_pid",QCoreApplication::applicationPid()},
    {"captured_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
    {"archive_pairs",canvas->property("pgn_archive_pairs").toLongLong()},
    {"time_history_points",canvas->property("pgn_catalog_time_points").toLongLong()},
    {"current_run_points",canvas->property("pgn_catalog_current_time_points").toLongLong()},
    {"graph_inputs_paused",canvas->property("pgn_graph_inputs_paused").toBool()},
    {"window_width",window.width()},{"window_height",window.height()},
    {"window_frame_fits_available_screen",window.screen()&&window.screen()->availableGeometry().contains(window.frameGeometry())},
    {"community_width",community->width()},{"community_height",community->height()},
    {"community_minimum_height",community->minimumSizeHint().height()}};
   QSaveFile file(capture+".json");if(file.open(QIODevice::WriteOnly)){
    file.write(QJsonDocument(receipt).toJson());file.commit();}
  });}
 if(showWallet||(argc>4&&QString::fromLocal8Bit(argv[4])=="--show-community")){
  const QString capture=QString::fromLocal8Bit(argv[3]);
  QTimer::singleShot(21000,&window,[&window,tabs,canvas,community,major,pgn,walletButton,toolsButton,showWallet,capture]{major->setCurrentIndex(0);pgn->setChecked(true);toolsButton->show();tabs->setCurrentWidget(canvas);auto* ready=new QTimer(&window);ready->setInterval(50);
   QObject::connect(ready,&QTimer::timeout,&window,[&window,tabs,canvas,community,major,pgn,walletButton,toolsButton,showWallet,capture,ready,attempt=0]() mutable {
    const bool complete=!canvas->property("pgn_native_loading").toBool()&&
      (canvas->property("pgn_catalog_time_points").toLongLong()==0||canvas->property("pgn_native_visible_points").toLongLong()>0);
    if(!complete&&++attempt<200)return;
    window.grab().save(capture+".mining.png");
    const QJsonObject receipt{{"gui_pid",QCoreApplication::applicationPid()},{"utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
     {"complete_raster",complete},{"visible_points",canvas->property("pgn_native_visible_points").toLongLong()},
     {"archive_time_points",canvas->property("pgn_catalog_time_points").toLongLong()},
     {"render_ns",canvas->property("pgn_native_render_ns").toLongLong()},
     {"cache_bytes",canvas->property("pgn_native_cache_bytes").toLongLong()},
     {"cache_frames",canvas->property("pgn_native_cache_frames").toLongLong()},
     {"cache_hits",canvas->property("pgn_native_cache_hits").toLongLong()}};
    QSaveFile file(capture+".mining.json");if(file.open(QIODevice::WriteOnly)){file.write(QJsonDocument(receipt).toJson());file.commit();}
    ready->stop();ready->deleteLater();if(showWallet){major->setCurrentIndex(1);walletButton->setChecked(true);toolsButton->show();}else{pgn->setChecked(true);tabs->setCurrentWidget(community);}
   });ready->start();});
 }
 startupWindow.step(5,"Interface ready");QTimer::singleShot(0,&window,[&startupWindow]{startupWindow.finish();});
 return app.exec();
}
