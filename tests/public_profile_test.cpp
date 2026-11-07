#include "ui/public_profile_button.h"
#include "ui/public_profile_control.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QFile>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv){QApplication app(argc,argv);QCoreApplication::setOrganizationName("PGN-profile-test");QCoreApplication::setApplicationName("isolated-profile");QTemporaryDir dir;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());int checks=0;const auto check=[&](bool ok){++checks;if(!ok)throw std::runtime_error("Profile check "+std::to_string(checks));};
 auto* button=pgn::ui::createPublicProfileButton(nullptr);auto* field=button->findChild<QLineEdit*>("pgn-public-profile-name");auto* save=button->findChild<QPushButton*>("pgn-public-profile-save");check(field&&save);check(field->text()=="PGN"&&!field->text().isEmpty());check(QSettings().value("public/profileName").toString()=="PGN");
 field->clear();for(const auto ch:QString("Wizz_31")){field->insert(QString(ch));check(QSettings().value("public/profileDraft").toString()==field->text());save->click();check(QSettings().value("public/profileName").toString()==field->text());}
 for(int i=0;i<20;++i)save->click();check(QSettings().value("public/profileName").toString()=="Wizz_31");delete button;
 button=pgn::ui::createPublicProfileButton(nullptr);field=button->findChild<QLineEdit*>("pgn-public-profile-name");check(field->text()=="Wizz_31");field->setText(QString(200,'A'));check(field->text().size()==48);check(QSettings().value("public/profileName").toString().size()==48);
 field->setText("Valid-name");const auto valid=QSettings().value("public/profileName");field->setText("bad<script>");check(!button->property("pgn_public_profile_saved").toBool());check(QSettings().value("public/profileName")==valid);
 for(const auto invalid:{QString("bad\nname"),QString("bad\\name"),QString(QChar(0x202e))+"hidden",QString("---")})check(!pgn::ui::canonicalPublicName(invalid));
 field->setText("Other");button->findChild<QPushButton*>("pgn-public-profile-default")->click();check(field->text()=="PGN");check(QSettings().value("public/profileName").toString()=="PGN");
 const auto blocked=dir.path()+"/blocked";QFile f(blocked);check(f.open(QIODevice::WriteOnly));f.close();QSettings failing(blocked+"/profile.ini",QSettings::IniFormat);check(!pgn::ui::savePublicProfileDraft(failing,"Valid"));check(pgn::ui::canonicalPublicName(QString::fromUtf8("Émile-31"))==QString::fromUtf8("Émile-31"));
 check(!pgn::ui::canonicalPublicName(QString("A")+QString(47,QChar(0x0344))));
 const auto command=pgn::ui::publicProfileControl(QString::fromUtf8("Émile-31"));check(command.endsWith('\n'));const auto object=QJsonDocument::fromJson(command).object();check(object.value("action")=="public_profile"&&object.value("name")==QString::fromUtf8("Émile-31"));check(pgn::ui::publicProfileControl("<b>bad</b>").isEmpty());check(button->property("pgn_public_profile_saved").toBool());
 check(pgn::ui::presentPublicProfileStatus(button,{{"schema","PGN_PUBLIC_PROFILE_STATUS_V1"},{"applied",true},{"name","PGN"}}));check(button->property("pgn_public_name_controller_ready").toBool());
 check(pgn::ui::presentPublicProfileStatus(button,{{"schema","PGN_PUBLIC_PROFILE_STATUS_V1"},{"applied",true},{"name","Old-name"}}));check(button->property("pgn_public_name_controller_ready").toBool());
 check(!pgn::ui::presentPublicProfileStatus(button,{{"schema","PGN_HORIZON_MINING_VIEW_V1"},{"phase","Searching"}}));
 check(pgn::ui::presentPublicProfileStatus(button,{{"schema","PGN_PUBLIC_PROFILE_STATUS_V1"},{"applied",false}}));check(!button->property("pgn_public_name_controller_ready").toBool());
 const QString address="tpgnda2MFZmioAjtHMaWkxAdXut9Xp4Zjpvon";pgn::ui::presentPublicAuditIdentity(button,address);check(button->findChild<QLineEdit*>("pgn-public-audit-address")->text()==address);
 button->setProperty("pgn_public_mining_genesis",QString(64,'a'));
 QJsonObject access{{"schema","PGN_NORMAL_MINER_JOIN_STATUS_V1"},{"genesis",QString(64,'a')},{"signing_address",address},{"registered",false},{"audit_permission",false},{"normal_cycle_permission",false},{"own_report_membership_quorum",false}};
 QJsonObject observation{{"schema","PGN_NORMAL_MINER_ADMISSION_OBSERVATION_V1"},{"state","ready"},{"available",true},{"status",access}};
 auto* accessLabel=button->findChild<QLabel*>("pgn-public-mining-access");check(accessLabel);pgn::ui::presentPublicMiningAccess(button,observation);check(accessLabel->text().contains("Registration needed"));
 access.insert("registered",true);access.insert("audit_permission",true);access.insert("normal_cycle_permission",true);observation.insert("status",access);pgn::ui::presentPublicMiningAccess(button,observation);check(accessLabel->text().contains("two independent auditors"));
 access.insert("own_report_membership_quorum",true);observation.insert("status",access);pgn::ui::presentPublicMiningAccess(button,observation);check(accessLabel->text().contains("Rewards follow independent validation"));
 observation.insert("available",false);pgn::ui::presentPublicMiningAccess(button,observation);check(accessLabel->text().contains("unavailable"));
 access.insert("genesis",QString(64,'b'));observation.insert("available",true);observation.insert("status",access);pgn::ui::presentPublicMiningAccess(button,observation);check(accessLabel->text().contains("unavailable"));
 delete button;std::cout<<"PASS "<<checks<<" profile persistence/validation/access checks\n";}
