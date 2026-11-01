#include "ui/verification_receipt_view.h"
#include "ui/mining_canvas.h"
#include <QFutureWatcher>
#include <QTimer>
#include <QFileInfo>
#include <QtConcurrent/QtConcurrentRun>
#include <memory>

namespace pgn::ui {
void bindVerificationReceipts(QWidget* canvas,const QString& journal,const QStringList& qualifiedValidators){
    auto* watcher=new QFutureWatcher<pgn::verification::LatestReceipt>(canvas);
    auto* timer=new QTimer(canvas);timer->setInterval(1000);
    const auto lastStat=std::make_shared<QString>();
    QObject::connect(watcher,&QFutureWatcher<pgn::verification::LatestReceipt>::finished,canvas,[canvas,watcher,lastStat]{
        const auto result=watcher->result();
        if(!result.error.isEmpty())lastStat->clear(); // Retry transient failure, not a fabricated fresh receipt.
        presentVerificationReceipt(canvas,result);
    });
    const auto poll=[canvas,watcher,journal,qualifiedValidators,lastStat]{
        // No hidden-tab queries, queued overlapping jobs or full-history scan.
        // The worker captures values only, never a GUI/engine/wallet pointer.
        if(!canvas->isVisible()||watcher->isRunning())return;
        const QFileInfo database(journal),wal(journal+"-wal");
        const auto signature=QStringLiteral("%1/%2/%3/%4/%5").arg(database.exists())
            .arg(database.size()).arg(database.lastModified().toMSecsSinceEpoch())
            .arg(wal.size()).arg(wal.lastModified().toMSecsSinceEpoch());
        if(*lastStat==signature)return; // No repeated SQL read of unchanged immutable evidence.
        *lastStat=signature;
        watcher->setFuture(QtConcurrent::run([journal,qualifiedValidators]{
            return pgn::verification::readLatestReceipt(journal,qualifiedValidators);
        }));
    };
    QObject::connect(timer,&QTimer::timeout,canvas,poll);
    timer->start();QTimer::singleShot(0,canvas,poll);
}
}
