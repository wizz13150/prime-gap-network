#include "ui/researcher_profile_view.h"
#include "ui/community_view.h"
#include "booking/booking_model.h"
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QLockFile>
#include <QPluginLoader>
#include <QPushButton>
#include <QTemporaryDir>
#include <iostream>
#include <memory>

int main(int argc,char** argv){
    if(argc!=2)return 2;
    const QString plugin=QString::fromLocal8Bit(argv[1]);
    if(!QFileInfo(plugin).isFile())return 3;
    QPluginLoader preflight(plugin);if(!preflight.load()){std::cerr<<preflight.errorString().toStdString()<<'\n';return 4;}
    qputenv("QT_QPA_PLATFORM","offscreen");qputenv("QT_QPA_PLATFORM_PLUGIN_PATH",QFileInfo(plugin).absolutePath().toUtf8());
    QApplication app(argc,argv);if(QGuiApplication::platformName()!="offscreen")return 5;
    QTemporaryDir isolated;if(!isolated.isValid()||!QDir().mkpath(isolated.path()+"/data/runtime"))return 6;
    pgn::booking::BookingModel model(isolated.path());
    std::unique_ptr<QDialog> dialog(pgn::ui::createResearcherProfileDialog(&model));
    dialog->setAttribute(Qt::WA_DeleteOnClose,false);
    auto* name=dialog->findChild<QLineEdit*>("pgn-researcher-name");
    auto* save=dialog->findChild<QPushButton*>("pgn-researcher-name-save");
    if(!name||!save||name->maxLength()!=24||save->isEnabled())return 7;
    for(const auto* text:{"A space","_invalid","a/b","éclair",""}){name->setText(QString::fromUtf8(text));if(save->isEnabled())return 8;}
    name->setText("Benxy_42");if(!save->isEnabled())return 9;
    save->click();if(dialog->result()!=QDialog::Accepted||model.researcherName()!="Benxy_42")return 10;
    std::unique_ptr<QDialog> retry(pgn::ui::createResearcherProfileDialog(&model));retry->setAttribute(Qt::WA_DeleteOnClose,false);
    if(retry->findChild<QLineEdit*>("pgn-researcher-name")->text()!="Benxy_42")return 11;
    retry->findChild<QLineEdit*>("pgn-researcher-name")->setText("New_name");
    {QLockFile held(isolated.path()+"/data/runtime/community-profile.json.lock");held.setStaleLockTime(0);if(!held.tryLock(0))return 14;
     retry->findChild<QPushButton*>("pgn-researcher-name-save")->click();
     if(retry->result()!=0||model.researcherName()!="Benxy_42"||retry->findChild<QLabel*>("pgn-researcher-name-error")->text().isEmpty())return 15;}
    retry->findChild<QPushButton*>("pgn-researcher-name-save")->click();
    if(retry->result()!=QDialog::Accepted||model.researcherName()!="New_name")return 16;
    // Production community button reaches the same editor, without a desktop window.
    std::unique_ptr<QWidget> community(pgn::ui::createCommunityView(&model));
    auto* open=community->findChild<QPushButton*>("pgn-researcher-profile-open");if(!open)return 12;
    open->click();app.processEvents();
    auto* live=community->findChild<QDialog*>("pgn-researcher-profile");if(!live)return 13;
    open->click();app.processEvents();if(community->findChildren<QDialog*>("pgn-researcher-profile").size()!=1)return 17;
    community->resize(1200,540);community->show();app.processEvents();
    auto* panel=community->findChild<QWidget*>("pgn-booking-choice-panel");
    if(!panel||!panel->rect().contains(QRect(open->mapTo(panel,QPoint()),open->size())))return 18;
    live->reject();app.processEvents();
    std::cout<<"researcher profile view: offscreen validation, save, reload, busy refusal/retry, singleton and production button layout PASS\n";return 0;
}
