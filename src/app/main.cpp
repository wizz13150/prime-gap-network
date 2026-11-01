#include "ui/mining_canvas.h"
#include "ui/public_profile_button.h"
#include "ui/mining_results_view.h"
#include "ui/mining_ticket_progress.h"
#include "ui/mining_worker_close_guard.h"
#include "ui/record_submission_view.h"
#include "ui/diagnostic_capture_view.h"
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
 if((argc==4&&(QString::fromLocal8Bit(argv[1])=="--show-owned"||QString::fromLocal8Bit(argv[1])=="--start-owned"))||(argc==3&&(QString::fromLocal8Bit(argv[1])=="--close-owned"||QString::fromLocal8Bit(argv[1])=="--maintenance-close-owned"||QString::fromLocal8Bit(argv[1])=="--inspect-owned"||QString::fromLocal8Bit(argv[1])=="--audit-graph-owned"||QString::fromLocal8Bit(argv[1])=="--audit-values-owned"))){
  // IPC clients need no GUI/platform plugin. This is still the same program;
  // only the actual window owner initializes QApplication below.
  QCoreApplication app(argc,argv);
  bool ok=false;const auto pid=QString::fromLocal8Bit(argv[2]).toLongLong(&ok);if(!ok||pid<=0)return 2;
  const bool start=QString::fromLocal8Bit(argv[1])=="--start-owned";
  const bool maintenance=QString::fromLocal8Bit(argv[1])=="--maintenance-close-owned";
  if(start){const auto id=QString::fromLocal8Bit(argv[3]);const QUuid parsed(id);if(parsed.isNull()||parsed.toString(QUuid::WithoutBraces)!=id)return 2;}
  QLocalSocket socket;socket.connectToServer("pgn-horizon-"+QString::number(pid));
  if(!socket.waitForConnected(2000))return 3;
  const bool inspect=QString::fromLocal8Bit(argv[1])=="--inspect-owned";
  const bool audit=QString::fromLocal8Bit(argv[1])=="--audit-graph-owned";
  const bool valuesAudit=QString::fromLocal8Bit(argv[1])=="--audit-values-owned";
  const bool show=QString::fromLocal8Bit(argv[1])=="--show-owned";
  const QByteArray command=start?QByteArray("start:")+QByteArray(argv[3])+"\n":show?QByteArray("show:")+QByteArray(argv[3])+"\n":maintenance?QByteArray("maintenance_close\n"):valuesAudit?QByteArray("audit_values\n"):audit?QByteArray("audit_graph\n"):inspect?QByteArray("inspect\n"):QByteArray("close\n");
  if(command.size()>63)return 2;socket.write(command);socket.flush();if(!socket.waitForReadyRead(start?15000:(audit||valuesAudit)?8000:2000))return 4;
  auto response=socket.readAll();
  while(!response.endsWith('\n')&&response.size()<131072){if(!socket.waitForReadyRead(2000))return 4;response+=socket.readAll();}
  if(inspect||audit||valuesAudit||show||start||maintenance){QFile out;if(!out.open(stdout,QIODevice::WriteOnly))return 7;out.write(response);return response.startsWith('{')?0:5;}
  return response.startsWith("closing")?0:5;
 }
#ifdef _WIN32
 SetCurrentProcessExplicitAppUserModelID(L"PGN.Horizon.Desktop");
#endif
 QApplication app(argc,argv);QCoreApplication::setOrganizationName("PGN");QCoreApplication::setApplicationName("PGN Horizon");
 QApplication::setWindowIcon(QIcon(":/frontier/app-icon.png"));
  QWidget* inspectCanvas=nullptr;QWidget* inspectArchive=nullptr;QWidget* inspectWallet=nullptr;QWidget* inspectEngineConfiguration=nullptr;QWidget* inspectRecords=nullptr;
 QLabel* inspectRuntimeStatus=nullptr;
 std::function<QJsonObject(const QString&)> showOwned;
 std::function<QJsonObject(const QString&)> startOwned;
 std::function<QJsonObject()> maintenanceOwned;
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
   }else if(command=="close\n"){
    if(!closeOwned){socket->write("not-ready\n");socket->flush();return;}
    socket->write("closing\n");socket->flush();QTimer::singleShot(50,&app,[&closeOwned]{closeOwned();});
   }else if(command.startsWith("start:")&&startOwned){
    const auto id=QString::fromUtf8(command.mid(6)).trimmed();const QUuid parsed(id);
    const auto result=parsed.isNull()||parsed.toString(QUuid::WithoutBraces)!=id
        ?QJsonObject{{"request_delivered",false},{"error","Canonical request UUID required"}}:startOwned(id);
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
    if(auto* rail=inspectCanvas->window()->findChild<QWidget*>("pgn-ticket-progress-rail")){receipt.insert("ticket_progress",QJsonValue::fromVariant(rail->property("pgn_ticket_progress_fraction")));receipt.insert("ticket_animation_active",rail->property("pgn_ticket_animation_active").toBool());receipt.insert("ticket_animation_phase",QJsonValue::fromVariant(rail->property("pgn_ticket_animation_phase")));}
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
    for(const auto* name:{"pgn_cpu_validation_result","pgn_cpu_validation_result_saved","pgn_cpu_audit_network_status","pgn_worker_stream_state","pgn_worker_stream_bytes","pgn_worker_stream_packets","pgn_worker_stream_phase","pgn_worker_stream_notice","pgn_worker_stream_error","pgn_total_expanded","pgn_total_gpu_state","pgn_total_cpu_state","pgn_cpu_inventory_error","pgn_cpu_card_metrics","pgn_cpu_control_devices"})receipt.insert(QString::fromLatin1(name),QJsonValue::fromVariant(inspectCanvas->property(name)));
    if(inspectRuntimeStatus)receipt.insert("pgn_runtime_status",inspectRuntimeStatus->text());
    for(const auto* name:{"pgn_native_runtime_started","pgn_native_inventory_type","pgn_native_inventory_accepted","pgn_native_inventory_error","pgn_native_inventory_lineage","pgn_native_hardware_availability","pgn_native_hardware_notice","pgn_native_source_attempt","pgn_application_close_ready","pgn_application_close_status","pgn_application_start_status","pgn_native_timing_events","pgn_native_timing_bytes","pgn_native_timing_dropped"})
     receipt.insert(QString::fromLatin1(name),QJsonValue::fromVariant(inspectCanvas->property(name)));
    for(const auto* name:{"pgn_independent_verification_receipt","pgn_independent_verification_error","pgn_native_display_revision","pgn_native_target_revision","pgn_native_frame_caption","pgn_native_display_min_x","pgn_native_display_max_x","pgn_native_display_min_y","pgn_native_display_max_y","pgn_graph_x_ticks","pgn_graph_y_ticks"})
     receipt.insert(QString::fromLatin1(name),QJsonValue::fromVariant(inspectCanvas->property(name)));
    for(const auto* name:{"pgn_catalog_total_points","pgn_catalog_time_points","pgn_catalog_undated_points","pgn_native_history_caption","pgn_requested_graph_axis"})
     receipt.insert(QString::fromLatin1(name),QJsonValue::fromVariant(inspectCanvas->property(name)));
    if(inspectArchive)for(const auto* name:{"pgn_archive_native_rows","pgn_archive_native_total","pgn_archive_native_origins"})
     receipt.insert(QString::fromLatin1(name),QJsonValue::fromVariant(inspectArchive->property(name)));
     if(auto* results=inspectCanvas->window()->findChild<QWidget*>("pgn-mining-results"))for(const auto* name:{"pgn_results_points","pgn_results_runs","pgn_results_best_rows","pgn_results_recent_rows","pgn_results_scope","pgn_results_loading","pgn_results_error"})receipt.insert(QString::fromLatin1(name),QJsonValue::fromVariant(results->property(name)));
     if(auto* summary=inspectCanvas->window()->findChild<QWidget*>("pgn-session-statistics"))for(const auto* name:{"pgn_ticket_productivity","pgn_productivity_regions","pgn_summary_scope","pgn_summary_range_regions"})receipt.insert(QString::fromLatin1(name),QJsonValue::fromVariant(summary->property(name)));
     if(auto* network=inspectCanvas->window()->findChild<QLabel*>("pgn-global-network-status"))receipt.insert("pgn_global_network_status",network->text());
    for(const auto* name:{"pgn_wallet_connected","pgn_wallet_height","pgn_wallet_genesis","pgn_wallet_observed_utc"})
     receipt.insert(QString::fromLatin1(name),QJsonValue::fromVariant(inspectCanvas->property(name)));
    if(inspectWallet)for(const auto* name:{"pgn_core_link_state","pgn_core_link_detail","pgn_core_link_transitions","pgn_wallet_frontend_loaded","pgn_wallet_frontend_error","pgn_wallet_genuine_page_count","pgn_wallet_embedded","pgn_wallet_embedded_pid","pgn_network_block_rows","pgn_network_leases","pgn_network_height","pgn_network_selected_transactions","pgn_network_ticket_width"})
     receipt.insert(QString::fromLatin1(name),QJsonValue::fromVariant(inspectWallet->property(name)));
    if(inspectWallet){
     const auto* history=inspectWallet->findChild<QWidget*>("pgn-wallet-difficulty-history");receipt.insert("pgn_wallet_difficulty_view_present",history!=nullptr);
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
 const QString root=argc>1&&!QString::fromLocal8Bit(argv[1]).startsWith("--")?QString::fromLocal8Bit(argv[1]):
  QFileInfo::exists(applicationDirectory+"/Core/core-template.json")?applicationDirectory:applicationDirectory+"/..";
 QString config=argc>2&&!QString::fromLocal8Bit(argv[2]).startsWith("--")?QString::fromLocal8Bit(argv[2]):QString{};
 // Old service/viewer coordination is retired, not silently redirected to a
 // writable runtime. A historical service receipt is never a live backend.
 QFile installedCore(root+"/Core/core-template.json");
 const bool installedTestnet=installedCore.open(QIODevice::ReadOnly)&&installedCore.size()<=32768&&
  QJsonDocument::fromJson(installedCore.readAll()).object().value("deployment").toObject().value("chain")=="test";
 installedCore.close();
 const bool testnetMode=QCoreApplication::arguments().contains("--testnet")||installedTestnet;
 const bool legacyResidentViewer=QCoreApplication::arguments().contains("--resident-viewer")||QCoreApplication::arguments().contains("--observe-only");
 if(config.isEmpty()&&QFile::exists(root+"/evidence/native-transport-recovered-20261012.json"))
  config=root+"/evidence/native-transport-recovered-20261012.json";
 QMainWindow window;window.setWindowTitle(testnetMode?"PGN Horizon · TESTNET":"PGN Horizon · Prime Gap Network");window.setWindowIcon(QIcon(":/frontier/app-icon.png"));
 closeOwned=[&window,&app]{if(window.close())QTimer::singleShot(0,&app,&QCoreApplication::quit);};
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
   walletButton->setText("WALLET\nPGN CORE CLIENT");walletButton->setIcon(online?green:red);walletButton->setIconSize(QSize(14,14));
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
   const auto state=!anchor->synchronizing?QStringLiteral("SYNC UNKNOWN"):*anchor->synchronizing?QStringLiteral("SYNCING"):anchor->peers&&*anchor->peers==0?QStringLiteral("NO PEERS"):!anchor->peers?QStringLiteral("PEER STATUS UNAVAILABLE"):QStringLiteral("UP TO DATE");
   networkSummary->setStyleSheet(QString("QLabel{color:%1;font:12px Consolas;padding:3px 8px;}").arg(anchor->synchronizing&&*anchor->synchronizing?"#8dccff":"#69efc5"));
   networkSummary->setText(QString(testnetMode?"%4 TESTNET QA · TECHNICAL HEIGHT %1 · %2 PEERS · %3":"%4 NETWORK · HEIGHT %1 · %2 PEERS · %3").arg(anchor->height).arg(peers,state).arg(anchor->synchronizing&&*anchor->synchronizing?QStringList{"◴","◷","◶","◵"}.at((QDateTime::currentMSecsSinceEpoch()/500)%4):QString("●")));
   if(testnetMode&&inspectWallet&&inspectWallet->property("pgn_network_gold_blocks").isValid())networkSummary->setText(QString("%5 TESTNET · GOLD %1 · LEDGER %2 · %3 PEERS · %4").arg(inspectWallet->property("pgn_network_gold_blocks").toLongLong()).arg(anchor->height).arg(peers,state).arg(anchor->synchronizing&&*anchor->synchronizing?QStringList{"◴","◷","◶","◵"}.at((QDateTime::currentMSecsSinceEpoch()/500)%4):QString("●")));
   networkSummary->setToolTip(QString("Verified Core observation: %1\nChain: %2\nTip: %3").arg(anchor->observedUtc.toUTC().toString(Qt::ISODateWithMs),anchor->genesis,anchor->tipHash));
  };
  auto* networkClock=new QTimer(&window);networkClock->setInterval(1000);QObject::connect(networkClock,&QTimer::timeout,&window,displayNetwork);networkClock->start();
 walletButton->setToolTip("Native PGN wallet integrated here, with verified chain identity and protected local keys.");
  walletButton->setFixedHeight(33);topBox->addWidget(walletButton);
 auto* identity=new QLabel("PRIME GAP NETWORK · GPU SOURCE / INDEPENDENT PROOF · WALLET SERVICE NOT CONNECTED");identity->setStyleSheet("color:#7dacb5;font:8px monospace;");identity->setWordWrap(true);identity->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);topBox->addWidget(identity,1);
  auto* tabs=new QTabWidget;auto* canvas=pgn::ui::createMiningCanvas();
  auto* miningPage=new QWidget;miningPage->setObjectName("pgn-mining-page");auto* miningLayout=new QVBoxLayout(miningPage);miningLayout->setContentsMargins(0,0,0,0);miningLayout->setSpacing(0);
  pgn::ui::configureMiningDashboard(canvas);miningLayout->addWidget(canvas);auto* ticketProgressRail=new pgn::ui::MiningTicketProgress;miningLayout->addWidget(ticketProgressRail);
  auto* testnetSummary=new QLabel("TESTNET · connecting to the owned WIZZ engine",miningPage);testnetSummary->setObjectName("pgn-testnet-mining-summary");testnetSummary->setStyleSheet("font:12px monospace;color:#69efc5;padding:14px 18px;");testnetSummary->setWordWrap(true);auto* miningToggle=new QPushButton("Start mining",miningPage);miningToggle->setObjectName("pgn-testnet-mining-toggle");miningToggle->setCheckable(true);miningToggle->setMinimumHeight(36);miningToggle->hide();auto* testnetProgress=new QProgressBar(miningPage);testnetProgress->setObjectName("pgn-testnet-ticket-progress");testnetProgress->setRange(0,1000);testnetProgress->setFormat("Ticket progress · %p%");testnetSummary->hide();testnetProgress->hide();
  auto* miningResults=pgn::ui::createMiningResultsView(root);auto* resultScroll=new QScrollArea;resultScroll->setObjectName("pgn-mining-results-scroll");resultScroll->setWidgetResizable(true);resultScroll->setFrameShape(QFrame::NoFrame);resultScroll->setWidget(miningResults);miningLayout->addWidget(resultScroll,1);
  auto* records=pgn::ui::createRecordSubmissionView(root,nullptr,testnetMode);inspectRecords=records;pgn::ui::bindRecordEligibility(records,[miningResults](const QJsonArray& pairs){pgn::ui::presentMiningRecordEligibility(miningResults,pairs);});miningLayout->addWidget(records);tabs->addTab(miningPage,"Mining");
 auto* productivityTimer=new QTimer(&window);productivityTimer->setInterval(1000);QObject::connect(productivityTimer,&QTimer::timeout,&window,[canvas,miningResults,&inspectWallet]{const auto width=inspectWallet?inspectWallet->property("pgn_network_ticket_width").toString():QString{};pgn::ui::presentMiningProductivity(miningResults,pgn::ui::miningDeviceRates(canvas),width);});productivityTimer->start();
 auto* bookings=new pgn::booking::BookingModel(root,&window);
 if(bookings->source().localValidation)identity->setText("PGN HORIZON · LOCAL ENGINE VALIDATION · NO NETWORK ACCEPTANCE");
 pgn::ui::bindBookingModel(canvas,bookings);
 inspectCanvas=canvas;
 if(!testnetMode)pgn::ui::bindObservationCatalog(canvas,root+"/data/runtime/prototype-observations.sqlite");
 if(!testnetMode)pgn::ui::bindNativeCatalog(canvas,root+"/data/runtime/scientific-history.sqlite");
 // New source gaps have no UTC dates. LENGTH opens the whole retained archive;
 // TIME remains available explicitly without inventing dates for those gaps.
 pgn::ui::selectNativeGraphAxis(canvas,"length");
 QWidget* community=nullptr;QTableWidget* testnetTickets=nullptr;
 pgn::ui::NetworkWorkView* networkWork=nullptr;
 if(testnetMode){networkWork=new pgn::ui::NetworkWorkView([&inspectWallet](int offset,auto consume){pgn::ui::readWalletTicketPage(inspectWallet,offset,std::move(consume));},[&inspectWallet](const QString& id,auto consume){pgn::ui::readWalletTicketDetails(inspectWallet,id,std::move(consume));});community=networkWork;}else community=pgn::ui::createCommunityView(bookings,nullptr,[canvas](const QString& id){return pgn::ui::miningDeviceAccent(canvas,id);});tabs->addTab(community,testnetMode?"Network work":"Community");
 bookings->onSelect=[tabs,community](const QString& id){tabs->setCurrentWidget(community);pgn::ui::selectCommunityBooking(community,id);};
  bookings->onMining=[tabs,miningPage]{tabs->setCurrentWidget(miningPage);};
 auto* lensGraph=pgn::ui::createNumberLensCanvas();
 pgn::ui::bindObservationCatalog(lensGraph,root+"/data/runtime/prototype-observations.sqlite");
 pgn::ui::bindNativeCatalog(lensGraph,root+"/data/runtime/scientific-history.sqlite");
 // Read the existing ALL archive once even if SSH/control never connects.
 // This display-only publication cannot open a run or satisfy SOURCE gates.
  pgn::ui::loadNativeArchiveIndex(root+"/data/runtime/scientific-history.sqlite",window,[canvas,lensGraph,miningResults](const QJsonObject& packet){
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
 QFile cpuInventoryProfile(root+"/data/testnet/ui-runtime/cpu-inventory.json");
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
 auto* cpuFleet=new pgn::mining::CpuFleet(&window);
 const bool localCpuOnly=!QFileInfo::exists(root+"/data/testnet/ui-runtime/cpu-controllers.json");
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
     if(QFileInfo::exists(program)){pgn::mining::CpuControlEndpoint control;control.program=program;control.host=host;control.deviceId="CPU/"+host.toUpper()+"/"+QString::number(inventory.value("processor_group").toInt());control.arguments={"--workspace",root+"/data/testnet/cpu-auditor/local","--piece-positions","100000000"};cpuFleet->add(control);}
    }
   },
   [canvas](const QString& error){canvas->setProperty("pgn_cpu_inventory_error",error);});
 };
 QTimer::singleShot(0,&window,discoverCpus);
 auto* cpuDiscoveryTimer=new QTimer(&window);cpuDiscoveryTimer->setInterval(300000);
 QObject::connect(cpuDiscoveryTimer,&QTimer::timeout,&window,discoverCpus);cpuDiscoveryTimer->start();
 auto cpuAuditBroker=std::make_shared<QPointer<pgn::mining::CpuAuditBroker>>();
 auto cpuValidationCase=std::make_shared<QJsonObject>();
 cpuFleet->onTelemetry=[canvas,pgn](const QString&,const QJsonObject& inventory,const QJsonObject& status,bool connected){pgn::ui::presentCpuController(canvas,inventory,status,connected);pgn->present(pgn::ui::miningCardStates(canvas));};
 cpuFleet->onControl=[canvas,pgn,cpuFleet,cpuValidationCase](const QString& device,const QString& request,bool applied,const QString& detail){pgn::ui::resolveDeviceControl(canvas,device,request,applied,detail);pgn->present(pgn::ui::miningCardStates(canvas));if(applied&&cpuValidationCase->value("completed_device")==device&&cpuValidationCase->value("completed_pass")==true&&cpuFleet->status(device).value("worker_pids").toArray().isEmpty()){if(cpuFleet->acknowledgeAudit(device,cpuValidationCase->value("completed_digest").toString()))cpuValidationCase->remove("completed_digest");}};
 cpuFleet->onAuditComputation=[cpuAuditBroker](const QString& device,const pgn::mining::CpuAuditWork& work,const QJsonObject& result){if(*cpuAuditBroker)(*cpuAuditBroker)->computed(device,work,result);};
 cpuFleet->onValidationComputation=[root,canvas,cpuFleet,cpuValidationCase](const QString& device,const pgn::mining::CpuAuditWork& work,const QJsonObject& result){
  const bool matches=result.value("counts")==cpuValidationCase->value("reference_counts");
  const auto native=cpuFleet->status(device);QJsonObject receipt{{"schema","PGN_CPU_LOCAL_VALIDATION_RESULT_V1"},{"PASS",matches},{"scope","Local full audit-sized computation against a known GPU reference; not a blind network audit"},{"network_accepted",false},{"source_proof_pending",true},{"request_sha256",work.requestSha256},{"reference_report_root",cpuValidationCase->value("reference_report_root")},{"counts",result.value("counts")},{"expected_counts",cpuValidationCase->value("reference_counts")},{"completed_positions",result.value("completed_positions")},{"native_status",native},{"observed_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
  const auto bytes=QJsonDocument(receipt).toJson(QJsonDocument::Compact);QSaveFile file(root+"/data/testnet/ui-runtime/cpu-audit-validation-result.json");
  const bool saved=bytes.size()<=32768&&file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit();
  canvas->setProperty("pgn_cpu_validation_result",receipt);canvas->setProperty("pgn_cpu_validation_result_saved",saved);
  cpuValidationCase->insert("completed_device",device);cpuValidationCase->insert("completed_digest",work.requestSha256);cpuValidationCase->insert("completed_pass",matches&&saved);
  const auto maximum=canvas->property("pgn_cpu_controller_inventory").toJsonObject().value(device).toObject().value("maximum_workers").toInt(1);
  cpuFleet->control(device,"validation-complete",false,qRound(100.*std::max(1,native.value("requested_threads").toInt(1))/maximum));
 };
 cpuFleet->onAuditError=[canvas](const QString& device,const QString& error){canvas->setProperty("pgn_cpu_audit_network_status",device+": "+error);};
 cpuFleet->onCloseReady=[&window,closeOwned]{QTimer::singleShot(0,&window,closeOwned);};
 new pgn::ui::MiningWorkerCloseGuard(&window,[cpuFleet]{return cpuFleet->requestClose();});
 QFile cpuControllers(root+"/data/testnet/ui-runtime/cpu-controllers.json");
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
 miningBox->addWidget(HorizonCoinNavigation(tabs,{{"01  MINING","LIVE SEARCH"},{testnetMode?"02  NETWORK WORK":"02  COMMUNITY",testnetMode?"TICKETS + AUDITS":"SHARED ORBIT"},{"03  NUMBER LENS","SCIENCE + EXACT RECORDS"},{"04  BENCHMARK","HARDWARE LAB"}},miningWorkspace));miningBox->addWidget(tabs,1);
 auto* major=new QStackedWidget;major->setObjectName("pgn-primary-workspaces");major->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);major->addWidget(miningWorkspace);
  inspectWallet=pgn::ui::createWalletCoreView(root,[canvas,identity,networkObservation,displayNetwork](const auto& observation){
   *networkObservation=observation;displayNetwork();
  pgn::ui::presentNetwork(canvas,observation);
  identity->setText(observation?"PRIME GAP NETWORK · VERIFIED WALLET CORE · GPU SOURCE / INDEPENDENT PROOF":
   "PRIME GAP NETWORK · GPU SOURCE / INDEPENDENT PROOF · WALLET SERVICE NOT CONNECTED");
 },testnetMode);major->addWidget(inspectWallet);
 const auto attachAuditIdentity=[root,inspectWallet,networkObservation,cpuFleet,cpuAuditBroker,canvas,toolsButton,&window,&app](const QJsonObject& identityData){
  if(*cpuAuditBroker)return;
  const auto address=identityData.value("address").toString(),genesis=identityData.value("genesis").toString();
  if(address.isEmpty()||!QRegularExpression("^[0-9a-f]{64}$").match(genesis).hasMatch())return;
  pgn::ui::presentPublicAuditIdentity(toolsButton,address);
    const QPointer<QWidget> wallet=inspectWallet;pgn::mining::CpuAuditBroker::Services services;
    services.rpc=[wallet](auto method,auto params,auto consume){pgn::ui::requestWalletAudit(wallet,method,params,std::move(consume));};
    services.sign=[wallet](auto address,auto challenge,auto consume){pgn::ui::signWalletAudit(wallet,address,challenge,std::move(consume));};
    services.identity=[wallet](auto address,auto consume){pgn::ui::readWalletSigningIdentity(wallet,address,std::move(consume));};
    services.details=[wallet](auto id,auto consume){pgn::ui::readWalletTicketDetails(wallet,id,std::move(consume));};
    services.auditDetails=[wallet](auto id,auto consume){pgn::ui::readWalletAuditDetails(wallet,id,std::move(consume));};
    services.observation=[networkObservation]{return *networkObservation;};
    auto* broker=new pgn::mining::CpuAuditBroker(root+"/data/testnet/ui-runtime/cpu-audit-outbox.json",genesis,address,cpuFleet,std::move(services),&window);*cpuAuditBroker=broker;
    broker->onStatus=[canvas](const QString& text){canvas->setProperty("pgn_cpu_audit_network_status",text);};
    QObject::connect(&app,&QCoreApplication::aboutToQuit,broker,&pgn::mining::CpuAuditBroker::stop);
 };
 if(testnetMode){
  QFile identityFile(root+"/data/testnet/ui-runtime/mining-address.json");
  if(identityFile.open(QIODevice::ReadOnly)&&identityFile.size()<=8192)attachAuditIdentity(QJsonDocument::fromJson(identityFile.readAll()).object());
 }
 if(testnetMode){
  const auto exporter=QCoreApplication::applicationDirPath()+"/EngineRuntime/network/horizon-engine-host"
#ifdef _WIN32
   ".exe"
#endif
   ;
  auto* diagnostics=pgn::ui::createDiagnosticCaptureView(exporter,[canvas,inspectWallet,networkObservation]{
   const auto select=[](const QJsonObject& source,std::initializer_list<const char*> keys){QJsonObject value;for(const auto* key:keys)if(source.contains(key))value.insert(key,source.value(key));return value;};
   QJsonObject network{{"status",canvas->property("pgn_global_network_status").toString()},{"core_state",inspectWallet->property("pgn_core_link_state").toString()}};
   if(*networkObservation){const auto& observation=**networkObservation;network.insert("height",observation.height);network.insert("genesis",observation.genesis);network.insert("tip_hash",observation.tipHash);network.insert("peers",observation.peers?QJsonValue(*observation.peers):QJsonValue{});network.insert("synchronizing",observation.synchronizing?QJsonValue(*observation.synchronizing):QJsonValue{});}
   QJsonArray cpus;const auto controllers=canvas->property("pgn_cpu_controller_states").toJsonObject();
   for(auto it=controllers.begin();it!=controllers.end();++it){if(cpus.size()>=8)break;cpus.append(select(it.value().toObject(),{"state","host","device_id","enabled","controller_connected","requested_threads","threads_active","active_workers","resident_workers","machine_load_percent","positions_per_second","rate_positions_per_second","completed_positions","total_positions","inflight_pieces","buffered_closed_pieces","wall_active_seconds","error","cpu_telemetry"}));}
   return QJsonObject{{"network",network},{"hardware",canvas->property("pgn_diagnostic_hardware").toJsonObject()},{"cpu",cpus},
    {"ticket",select(canvas->property("pgn_network_ticket_frame").toJsonObject(),{"ticket_number","ticket_id","work_role","phase","bits","start","end","positions","positions_total","current","minimum_gap","speed_rate","average_rate","compute_tests_rate","source_age_ns","network_notice"})}};
  });
  addTool("Diagnostics",diagnostics);
 }

 if(networkWork){networkWork->onOpenBlock=[major,walletButton,toolsButton,inspectWallet](const QString& hash){major->setCurrentIndex(1);walletButton->setChecked(true);toolsButton->show();if(auto* pages=inspectWallet->findChild<QTabWidget*>("pgn-wallet-subtabs"))pages->setCurrentIndex(1);pgn::ui::inspectWalletBlock(inspectWallet,hash);};networkWork->onOpenAudits=[major,walletButton,toolsButton,inspectWallet]{major->setCurrentIndex(1);walletButton->setChecked(true);toolsButton->show();if(auto* pages=inspectWallet->findChild<QTabWidget*>("pgn-wallet-subtabs"))pages->setCurrentIndex(4);};}
 auto* primaryGroup=new QButtonGroup(&window);primaryGroup->setExclusive(true);primaryGroup->addButton(pgn,0);primaryGroup->addButton(walletButton,1);
 QObject::connect(primaryGroup,&QButtonGroup::idClicked,major,[major,toolsButton](int id){major->setCurrentIndex(id);toolsButton->show();});
  showOwned=[&window,root,testnetMode,cpuFleet,cpuValidationCase,major,tabs,pgn,walletButton,toolsButton,canvas,miningPage,records,community,lens,lensGraph,benchmark,engineConfiguration](const QString& section){
  if(section=="MiningCpuAuditValidation"||section=="MiningCpuAttachValidation"){
   if(!testnetMode)return QJsonObject{{"error","Local validation requires the private testnet UI"}};
   QFile file(root+"/data/testnet/ui-runtime/cpu-audit-validation.json");if(!file.open(QIODevice::ReadOnly)||file.size()>32768)return QJsonObject{{"error","Bounded CPU validation case is unavailable"}};
   const auto data=QJsonDocument::fromJson(file.readAll()).object();const auto request=data.value("request").toObject();const auto device=data.value("device_id").toString();
   if(data.value("schema")!="PGN_CPU_AUDIT_VALIDATION_CASE_V1"||data.value("reference_counts").toArray().size()!=5||request.value("schema")!="PGN_EXPLICIT_CPU_AUDIT_REQUEST_V1"||!pgn::mining::cpuAuditProposalSupported(QJsonObject{{"ranges",request.value("partitions")}}))return QJsonObject{{"error","CPU validation case is invalid or unsupported"}};
   pgn::mining::CpuAuditWork work;work.genesis=data.value("genesis").toString();work.requestId=data.value("reference_request_id").toString();work.reportRoot=data.value("reference_report_root").toString();work.actor="LOCAL_VALIDATION";work.slot=0;work.attempt=0;work.serviceRequest=request;work.requestSha256=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(request).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
   const auto maximum=canvas->property("pgn_cpu_controller_inventory").toJsonObject().value(device).toObject().value("maximum_workers").toInt();const auto threads=data.value("threads").toInt();if(threads<1||threads>maximum)return QJsonObject{{"error","CPU validation thread budget is unavailable"}};
   if(!cpuFleet->attachValidation(device,work))return QJsonObject{{"error","CPU retains another assignment or its controller is not ready"}};
   *cpuValidationCase=data;
   if(section=="MiningCpuAttachValidation")return QJsonObject{{"device",device},{"validation_attached",true},{"compute_enabled",cpuFleet->status(device).value("enabled")},{"budget_changed",false},{"network_accepted",false}};
   cpuFleet->control(device,"validation-start",true,qRound(100.*threads/maximum));
   major->setCurrentIndex(0);tabs->setCurrentWidget(miningPage);window.showMaximized();window.raise();window.activateWindow();
   return QJsonObject{{"validation_queued",true},{"device",device},{"threads",threads},{"positions","10000000000000"},{"network_accepted",false}};
  }
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
   if(!cpuAction.hasMatch()&&!resizeAction&&!lensAction&&!miningGraphAction&&!recordsAction&&!communityAction&&!reservationActions.contains(section)&&!QStringList{"Mining","MiningMaximize","MiningOn","MiningOff","MiningExpand","MiningCollapse","MiningStatus","MiningTotalExpand","MiningTotalCollapse","MiningGroupGpuOn","MiningGroupGpuOff","MiningGroupCpuOn","MiningGroupCpuOff","Community","TicketFirst","CommunityStatus","PublicRegister","NumberLens","NumberLensStatus","Benchmark","BenchmarkMeasure","BenchmarkStatus","Wallet","Audits","Difficulty","Vanity","VanityCheck","VanityStatus","VanityGenerateCPU","Network","NetworkInspectFirst","Configuration"}.contains(section))return QJsonObject{{"error","Unknown owned QA section"}};

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
   if(section=="Wallet"||section=="Audits"||section=="Difficulty"||section=="Vanity"||section=="Network"){major->setCurrentIndex(1);walletButton->setChecked(true);toolsButton->show();if(auto* pages=major->widget(1)->findChild<QTabWidget*>("pgn-wallet-subtabs"))pages->setCurrentIndex(section=="Audits"?4:section=="Difficulty"?3:section=="Vanity"?2:section=="Network"?1:0);}
   else{major->setCurrentIndex(0);pgn->setChecked(true);toolsButton->show();if(section!="Configuration")engineConfiguration->window()->hide();if(cpuAction.hasMatch()||section.startsWith("MiningTotal")||section.startsWith("MiningGroup")||section=="Mining"||section=="MiningMaximize"||section=="MiningOn"||section=="MiningOff"||section=="MiningExpand"||section=="MiningCollapse"||miningGraphAction||resizeAction||recordsAction)tabs->setCurrentWidget(miningPage);else if(section=="Community"||section=="TicketFirst"||communityAction)tabs->setCurrentWidget(community);else if(section=="NumberLens"||lensAction)tabs->setCurrentWidget(lens);else if(section=="Benchmark"||section=="BenchmarkMeasure")tabs->setCurrentWidget(benchmark);
   else if(auto* dialog=qobject_cast<QDialog*>(engineConfiguration->parentWidget())){dialog->show();dialog->raise();}}
  if(section.startsWith("MiningTotal")||section.startsWith("MiningGroup")){QRectF target;bool click=false;if(section.startsWith("MiningTotal")){target=canvas->property("pgn_total_expand_rectangle").toRectF();click=canvas->property("pgn_total_expanded").toBool()!=(section=="MiningTotalExpand");}else{const QString kind=section.contains("Gpu")?"gpu":"cpu";const QByteArray prefix=("pgn_total_"+kind).toLatin1();target=canvas->property((prefix+"_toggle_rect").constData()).toRectF();const auto state=canvas->property((prefix+"_state").constData()).toString();if(state=="unavailable"||state=="pending"||state.isEmpty())return QJsonObject{{"error","Group control is not ready"}};click=state!=(section.endsWith("On")?"on":"off");if(state=="mixed")return QJsonObject{{"error","Mixed group requires individual transition before this scoped test"}};}if(target.isEmpty())return QJsonObject{{"error","Total card is not rendered"}};if(click){const auto point=target.center();QMouseEvent press(QEvent::MouseButtonPress,point,canvas->mapToGlobal(point.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(canvas,&press);QMouseEvent release(QEvent::MouseButtonRelease,point,canvas->mapToGlobal(point.toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QCoreApplication::sendEvent(canvas,&release);}}
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
 auto* status=new QLabel("SOURCE CONNECTION PENDING · historical data preserved separately · no CPU miner");inspectRuntimeStatus=status;status->setStyleSheet("font:8px monospace;color:#78b9c6;padding:3px 12px;");
 status->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
 auto* hardwareNotice=new QLabel("GPU OBSERVATION PENDING");hardwareNotice->setObjectName("pgn-native-hardware-notice");
 hardwareNotice->setStyleSheet("font:8px monospace;color:#f6c566;padding:3px 12px;");
 auto* footer=new QWidget;auto* footerBox=new QHBoxLayout(footer);footerBox->setContentsMargins(0,0,0,0);footerBox->setSpacing(0);
  footerBox->addWidget(status,1);footerBox->addWidget(hardwareNotice);footerBox->addWidget(networkSummary);box->addWidget(footer);window.setCentralWidget(shell);
 // A reversible graph-only safety switch. Source journals and control paths
 // belong to the engine/bridge, never to the expensive visualization consumer.
 QFile graphPolicy(root+"/data/runtime/graph-policy.json");
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
   const auto directory=QFileInfo(root+"/data/runtime").absoluteFilePath();
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
  const QString runtimeDirectory=QFileInfo(root+"/data/runtime").absoluteFilePath();
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
   proofRuntime=std::make_unique<pgn::verification::SourceProofRuntime>(root+"/data/runtime/scientific-history.sqlite",
       root+"/data/runtime/native-source-certificates.sqlite",pgn::verification::SourceProofPolicy{4,{{128,32,32},10000},{10002,1048576,32,1}});
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
    applicationStart->control("GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac",id,true);
    const auto message=applicationStart->status();
    return QJsonObject{{"request_id",id},{"request_delivered",true},
        {"status",message},{"scope","DELIVERED_TO_APPLICATION_START_NOT_ADMISSION_OR_PHYSICAL_OWNERSHIP"}};
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
  pgn::ui::bindMiningResourceControls(canvas,[canvas,cpuFleet,gpuControls](const QString& device,const QString& id,bool on,int percent){
   if(cpuFleet->control(device,id,on,percent))return;
   const auto found=gpuControls->constFind(device.toLower());
   if(found==gpuControls->cend()){pgn::ui::resolveDeviceControl(canvas,device,id,false,"No qualified local worker is attached");return;}
   (*found)(id,on);
  });
   const auto attachWorker=[&window,closeOwned,
#ifdef _WIN32
   &bridge,&hardwareMonitor,
#endif
   canvas,hardwareNotice,cpuFleet,ticketProgressRail,miningResults,records,testnetSummary,testnetProgress,pgn,status,gpuControls](const QJsonObject& profile,QPushButton* miningToggle){
   const auto device=profile.value("gpu_uuid").toString();
   if(device.isEmpty()||gpuControls->contains(device.toLower()))return;
   auto* stream=new QProcess(&window);QStringList arguments;for(const auto& value:profile.value("arguments").toArray())arguments.append(value.toString());
#ifdef _WIN32
   const auto hardware=profile.value("hardware_endpoint").toObject();
   if(!hardware.isEmpty()){
    pgn::mining::SshObservationEndpoint endpoint;
    endpoint.executable=profile.value("program").toString();endpoint.keyFile=hardware.value("key_file").toString();
    endpoint.host=hardware.value("host").toString();endpoint.user=hardware.value("user").toString();endpoint.port=quint16(hardware.value("port").toInt());endpoint.hostKeyAlias=hardware.value("host_key_alias").toString();
    canvas->setProperty("pgn_managed_source_device",profile.value("gpu_uuid").toString());
    hardwareMonitor=std::make_unique<pgn::mining::NativeHardwareMonitor>(endpoint,&bridge);
    hardwareMonitor->onAvailability=[hardwareNotice](bool available,const QString& error){hardwareNotice->setText(available?"WIZZ · GPU TELEMETRY CONNECTED":"WIZZ · GPU TELEMETRY UNAVAILABLE");hardwareNotice->setToolTip(error);};
    hardwareMonitor->start();
   }
#endif
   auto networkMode=std::make_shared<bool>(false);auto pendingNetwork=std::make_shared<bool>(false);auto stopRequested=std::make_shared<bool>(false);auto pendingCard=std::make_shared<QStringList>();auto presentationSequence=std::make_shared<qint64>(0);auto gapRates=std::make_shared<pgn::mining::GapRateWindow>();
   canvas->setProperty("pgn_source_only_control",true);
   gpuControls->insert(device.toLower(),[canvas,device,miningToggle,pendingCard,ticketProgressRail](const QString& id,bool on){
    *pendingCard={device,id,on?QString("on"):QString("off")};canvas->setProperty("pgn_network_desired_on",on);
    auto frame=canvas->property("pgn_network_ticket_frame").toJsonObject();frame.insert("phase",on?"Starting":"Stopping");ticketProgressRail->present(frame);miningToggle->setChecked(on);
   });
   stream->setProgram(profile.value("program").toString());stream->setArguments(arguments);stream->setProcessChannelMode(QProcess::SeparateChannels);
   auto buffer=std::make_shared<QByteArray>();
   QObject::connect(stream,&QProcess::stateChanged,&window,[canvas](QProcess::ProcessState state){canvas->setProperty("pgn_worker_stream_state",int(state));});
   QObject::connect(stream,&QProcess::errorOccurred,&window,[canvas,stream](QProcess::ProcessError){canvas->setProperty("pgn_worker_stream_error",stream->errorString().left(400));});
   QObject::connect(stream,&QProcess::readyReadStandardOutput,&window,[stream,buffer,profile,miningResults,records,testnetSummary,testnetProgress,pgn,miningToggle,networkMode,stopRequested,canvas,pendingCard,presentationSequence,ticketProgressRail,gapRates]{
    const auto received=stream->readAllStandardOutput();canvas->setProperty("pgn_worker_stream_bytes",canvas->property("pgn_worker_stream_bytes").toLongLong()+received.size());buffer->append(received);if(buffer->size()>4194304){buffer->clear();testnetSummary->setText("Worker presentation exceeded its admitted size");return;}
    while(buffer->contains('\n')){const auto newline=buffer->indexOf('\n');const auto line=buffer->left(newline);buffer->remove(0,newline+1);auto packet=QJsonDocument::fromJson(line).object();
     canvas->setProperty("pgn_worker_stream_packets",canvas->property("pgn_worker_stream_packets").toLongLong()+1);canvas->setProperty("pgn_worker_stream_phase",packet.value("phase").toString());canvas->setProperty("pgn_worker_stream_notice",packet.value("message").toString().left(400));
     if(packet.value("schema")!="PGN_HORIZON_MINING_VIEW_V1"){auto phaseFrame=canvas->property("pgn_network_ticket_frame").toJsonObject();phaseFrame.insert("phase",packet.value("phase").toString("Starting"));ticketProgressRail->present(phaseFrame);if(packet.value("phase")=="Paused"){*stopRequested=false;const QSignalBlocker block(miningToggle);miningToggle->setChecked(false);miningToggle->setEnabled(true);if(!pendingCard->isEmpty()){pgn::ui::resolveDeviceControl(canvas,pendingCard->at(0),pendingCard->at(1),true,"GPU released after checkpoint");pendingCard->clear();}}testnetSummary->setText(packet.value("message").toString("Starting · waiting for the first coherent worker checkpoint"));if(packet.value("phase")=="Needs attention"){const QSignalBlocker block(miningToggle);miningToggle->setChecked(false);miningToggle->setEnabled(true);miningToggle->setText("Start mining");*stopRequested=false;if(!pendingCard->isEmpty()){pgn::ui::resolveDeviceControl(canvas,pendingCard->at(0),pendingCard->at(1),false,packet.value("message").toString());pendingCard->clear();}}continue;}
     if(packet.value("title").toString().isEmpty())packet.insert("title","Network ticket");
     pgn::ui::presentMiningTicketData(miningResults,packet);pgn::ui::presentRecordTicketData(records,packet);const auto phase=packet.value("phase").toString();const bool active=phase=="Searching";if(*networkMode){const QSignalBlocker block(miningToggle);if(*stopRequested&&phase!="Paused"){miningToggle->setChecked(false);miningToggle->setEnabled(false);miningToggle->setText("Stopping…");}else{*stopRequested=false;miningToggle->setEnabled(true);const bool on=phase!="Paused"&&phase!="Needs attention";miningToggle->setChecked(on);miningToggle->setText(on?"Stop mining":"Start mining");}}const auto rate=[](QJsonValue value){return value.isDouble()?QString::number(value.toDouble()/1e9,'f',0)+" Gpos/s":QString("—");};
     canvas->setProperty("pgn_network_ticket_frame",packet);ticketProgressRail->present(packet);
     FrontierTelemetrySnapshot lane;lane.authoritative=true;lane.stale=packet.value("source_stale").toBool();lane.sequence=++*presentationSequence;lane.observedUtc=QDateTime::fromString(packet.value("observed_utc").toString(),Qt::ISODateWithMs);lane.runId=packet.value("ticket_id").toString();lane.sourceMode="live";lane.state=active?"running":phase=="Report ready"?"complete":"stopped";lane.terminalSealed=phase=="Report ready";lane.gpuUuid=profile.value("gpu_uuid").toString();lane.gpuName=profile.value("device_name").toString();lane.domainStart=packet.value("start").toString();lane.domainEnd=packet.value("end").toString();lane.domainCurrent=packet.value("current").toString();lane.rowsCompleted=packet.value("positions").toString();lane.rowsTotal=packet.value("positions_total").toString();lane.numberBits=packet.value("bits").toInt();lane.cumulativeGapCandidates=QString::number(packet.value("scopes").toObject().value("ticket").toObject().value("statistics").toObject().value("count_total").toInteger());
     if(active){if(packet.value("speed_rate").isDouble())lane.e2ePositionsPerSecond=packet.value("speed_rate").toDouble();if(packet.value("average_rate").isDouble())lane.e2eWindowPositionsPerSecond=packet.value("average_rate").toDouble();if(packet.value("compute_tests_rate").isDouble())lane.powmPerSecond=packet.value("compute_tests_rate").toDouble();}lane.raw.insert("pgn_work_role",packet.value("work_role"));lane.raw.insert("pgn_cumulative_tests",packet.value("tests"));lane.raw.insert("pgn_source_time_ns",packet.value("compute_time_ns"));lane.raw.insert("pgn_clock_namespace",packet.value("ticket_id"));lane.raw.insert("pgn_compute_epoch",packet.value("compute_epoch"));bool ageOk=false;const auto sourceAge=packet.value("source_age_ns").toString().toULongLong(&ageOk);if(ageOk)pgn::mining::stampSourceAge(lane.raw,sourceAge,0,pgn::mining::receiptMonotonicNs());bool positionOk=false,timeOk=false;const auto position=packet.value("positions").toString().toULongLong(&positionOk),computeNs=packet.value("compute_time_ns").toString().toULongLong(&timeOk);const auto count=packet.value("scopes").toObject().value("ticket").toObject().value("statistics").toObject().value("count_total").toInteger();const auto mean=gapRates->observe(packet.value("ticket_id").toString().toStdString(),active&&positionOk&&timeOk,computeNs,position,quint64(std::max(qint64(0),count)));lane.raw.insert("pgn_cumulative_processed_gaps",QString::number(count));if(mean){lane.gapPairsTestedPerSecond=mean->perSecond;lane.raw.insert("pgn_retained_gaps_per_billion_positions",mean->perBillionPositions);lane.raw.insert("pgn_processed_gaps_per_second",mean->perSecond);lane.raw.insert("pgn_retained_gaps_per_second",mean->perSecond);lane.raw.insert("pgn_gap_window_positions_per_second",mean->positionsPerSecond);lane.raw.insert("pgn_gap_window_ns",QString::number(mean->spanNs));lane.raw.insert("pgn_source_interval_ns",QString::number(mean->spanNs));}lane.raw.insert("pgn_device_enabled",*networkMode&&miningToggle->isChecked()&&phase!="Paused"&&phase!="Needs attention");lane.raw.insert("pgn_process_running",active);lane.raw.insert("pgn_native_min_gap",QString::number(packet.value("minimum_gap").toInt()));pgn::ui::presentDevice(canvas,lane);canvas->update();
     if(!pendingCard->isEmpty()&&((pendingCard->at(2)=="on"&&active)||(pendingCard->at(2)=="off"&&phase=="Paused"))){pgn::ui::resolveDeviceControl(canvas,pendingCard->at(0),pendingCard->at(1),true,phase);pendingCard->clear();}
     testnetSummary->setText(QString("%1 · %2    |    Speed %3    |    60 s average %4").arg(profile.value("device_name").toString(),*stopRequested?QString("Stopping · saving progress"):phase,rate(packet.value("speed_rate")),rate(packet.value("average_rate"))));
     const double width=packet.value("end").toString().toDouble()-packet.value("start").toString().toDouble();const double positions=packet.value("positions").toString().toDouble();if(width>0)testnetProgress->setValue(std::clamp(int(1000*positions/width),0,1000));
     pgn->present(pgn::ui::miningCardStates(canvas));
    }
   });
   QObject::connect(stream,&QProcess::readyReadStandardError,&window,[stream,status]{const auto error=stream->readAllStandardError();if(!error.isEmpty())status->setText(QString::fromUtf8(error).right(600));});
   const auto startNetwork=[stream,buffer,profile,networkMode,pendingNetwork,testnetSummary]{buffer->clear();QStringList args;for(const auto& value:profile.value("network_arguments").toArray())args.append(value.toString());if(args.isEmpty()){testnetSummary->setText("Mining controller is not configured");return;}*networkMode=true;*pendingNetwork=false;stream->setArguments(args);stream->start();testnetSummary->setText("Starting · requesting an automatic network ticket");};
   QObject::connect(stream,&QProcess::finished,&window,[testnetSummary,pgn,canvas,pendingNetwork,startNetwork,miningToggle](int,QProcess::ExitStatus){if(*pendingNetwork){startNetwork();return;}testnetSummary->setText("Worker connection closed · mining state unavailable");pgn->present(pgn::ui::miningCardStates(canvas));const QSignalBlocker block(miningToggle);miningToggle->setChecked(false);miningToggle->setText("Start mining");});
   QObject::connect(miningToggle,&QPushButton::toggled,&window,[stream,networkMode,pendingNetwork,startNetwork,testnetSummary,miningToggle,stopRequested](bool on){
    miningToggle->setText(on?"Stop mining":"Stopping…");if(on)*stopRequested=false;if(!on){*stopRequested=true;*pendingNetwork=false;miningToggle->setEnabled(false);}
    if(on&&!*networkMode){*pendingNetwork=true;if(stream->state()==QProcess::NotRunning)startNetwork();else stream->kill();return;}
    if(*networkMode&&stream->state()==QProcess::Running){stream->write(on?"{\"action\":\"resume\"}\n":"{\"action\":\"pause\"}\n");testnetSummary->setText(on?"Starting · resuming the saved checkpoint":"Stopping · saving the current checkpoint");}
   });
   auto closeRequested=std::make_shared<bool>(false),closeAllowed=std::make_shared<bool>(false);
   new pgn::ui::MiningWorkerCloseGuard(&window,[stream,networkMode,pendingNetwork,closeRequested,closeAllowed,testnetSummary,ticketProgressRail,canvas]{
    if(*closeAllowed||!*networkMode||stream->state()==QProcess::NotRunning)return true;
    if(!*closeRequested){*closeRequested=true;*pendingNetwork=false;stream->write("{\"action\":\"quit\"}\n");testnetSummary->setText("Closing · saving checkpoint and releasing GPU");auto frame=canvas->property("pgn_network_ticket_frame").toJsonObject();frame.insert("phase","Stopping");ticketProgressRail->present(frame);}return false;
   });
   QObject::connect(stream,&QProcess::finished,&window,[&window,closeOwned,closeRequested,closeAllowed](int code,QProcess::ExitStatus status){if(*closeRequested&&code==0&&status==QProcess::NormalExit){*closeAllowed=true;QTimer::singleShot(0,&window,closeOwned);}});
   QObject::connect(stream,&QProcess::started,&window,[stream,networkMode,stopRequested,canvas]{canvas->setProperty("pgn_worker_stream_error",QVariant{});if(*networkMode&&*stopRequested)stream->write("{\"action\":\"pause\"}\n");});
   stream->start();hardwareNotice->setText(profile.value("local").toBool()?"LOCAL · OPTIMIZED ENGINE":"WIZZ · OWNED OPTIMIZED ENGINE");status->setText("TESTNET · live worker presentation · Core verifies network state");
  };
  QFile streamProfile(root+"/data/testnet/ui-runtime/worker-stream.json");
  if(streamProfile.open(QIODevice::ReadOnly)&&streamProfile.size()<=32768){
   const auto profile=QJsonDocument::fromJson(streamProfile.readAll()).object();streamProfile.close();attachWorker(profile,miningToggle);
  }else{
   testnetSummary->setText("Complete wallet setup to prepare local mining and auditing");
   const auto bundle=QCoreApplication::applicationDirPath()+"/EngineRuntime";
   const auto host=bundle+"/network/horizon-engine-host"
#ifdef _WIN32
    ".exe"
#endif
    ;
   auto* prepareTimer=new QTimer(&window);prepareTimer->setInterval(3000);auto preparing=std::make_shared<bool>(false);
   const auto prepareLocal=[preparing,prepareTimer,host,bundle,root,inspectWallet,canvas,testnetSummary,miningToggle,attachWorker,attachAuditIdentity,&window]{
    if(*preparing||!QFileInfo::exists(host))return;*preparing=true;
    pgn::ui::readWalletLocalConnection(inspectWallet,[preparing,prepareTimer,host,bundle,root,canvas,testnetSummary,miningToggle,attachWorker,attachAuditIdentity,&window](QJsonObject context,QString error){
     if(!error.isEmpty()){*preparing=false;canvas->setProperty("pgn_local_runtime_status",error);return;}
     pgn::engine::localRuntimeQuery(&window,host,{"--prepare-local","--bundle",bundle,"--state-root",root+"/data/testnet/local-engine"},context,
      [preparing,prepareTimer,canvas,testnetSummary,miningToggle,attachWorker,attachAuditIdentity,&window](QJsonObject value,QString error){
       *preparing=false;
       if(!error.isEmpty()||value.value("schema")!="PGN_LOCAL_RUNTIME_PROFILES_V1"||value.value("mining_started")!=false||value.value("allocation_requested")!=false||value.value("profiles").toArray().size()>32){canvas->setProperty("pgn_local_runtime_status",error.isEmpty()?QString("Local preparation was refused"):error);return;}
       prepareTimer->stop();attachAuditIdentity(value.value("audit_identity").toObject());
       int index=0;for(const auto& item:value.value("profiles").toArray()){
        auto* toggle=index++?new QPushButton(&window):miningToggle;toggle->setCheckable(true);toggle->hide();attachWorker(item.toObject(),toggle);
       }
       canvas->setProperty("pgn_local_runtime_status","Prepared; computation waits for user ON");canvas->setProperty("pgn_local_runtime_unsupported",value.value("unsupported"));
       if(!index)testnetSummary->setText("CPU audit controls ready · no qualified local NVIDIA engine detected");
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
 pgn::ui::bindVerificationReceipts(canvas,root+"/data/runtime/independent-gap-verifications.sqlite",
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
 return app.exec();
}
