#include "ui/public_profile_button.h"
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
 const auto blocked=dir.path()+"/blocked";QFile f(blocked);check(f.open(QIODevice::WriteOnly));f.close();QSettings failing(blocked+"/profile.ini",QSettings::IniFormat);check(!pgn::ui::savePublicProfileDraft(failing,"Valid"));check(pgn::ui::canonicalPublicName(QString::fromUtf8("Émile-31"))==QString::fromUtf8("Émile-31"));delete button;std::cout<<"PASS "<<checks<<" profile persistence/validation checks\n";}
