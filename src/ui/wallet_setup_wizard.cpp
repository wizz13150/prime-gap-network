#include "ui/wallet_setup_wizard.h"
#include <QWizardPage>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QRadioButton>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QFileDialog>
#include <QFontDatabase>
namespace pgn::ui {
WalletSetupWizard::WalletSetupWizard(QString applicationDirectory,QString network,Commit commit,QWidget* parent):
    QWizard(parent),applicationDirectory_(std::move(applicationDirectory)),network_(std::move(network)),commit_(std::move(commit)){
    setObjectName("pgn-wallet-setup-wizard");setWindowTitle("Set up PGN Core wallet");setWizardStyle(QWizard::ModernStyle);setOption(QWizard::NoBackButtonOnStartPage);resize(620,420);
    auto textFont=QFontDatabase::systemFont(QFontDatabase::GeneralFont);
#ifdef Q_OS_WIN
    textFont.setFamily("Segoe UI");
#endif
    textFont.setPointSize(10);setFont(textFont);
    auto* welcome=new QWizardPage;welcome->setTitle("Your PGN wallet");auto* welcomeBox=new QVBoxLayout(welcome);
    auto* description=new QLabel("Choose where PGN Core stores the blockchain and wallet. Existing wallet data stays in its selected folder.");description->setWordWrap(true);welcomeBox->addWidget(description);
    create_=new QRadioButton("Create a new wallet");existing_=new QRadioButton("Open an existing Core data folder");create_->setChecked(true);welcomeBox->addWidget(create_);welcomeBox->addWidget(existing_);welcomeBox->addStretch();addPage(welcome);
    auto* location=new QWizardPage;location->setTitle("Data location");auto* locationBox=new QVBoxLayout(location);auto* form=new QFormLayout;
    directory_=new QLineEdit(pgn::wallet::defaultCoreDataDirectory());directory_->setObjectName("pgn-wallet-data-directory");auto* pathRow=new QHBoxLayout;pathRow->addWidget(directory_,1);auto* browse=new QPushButton("Browse…");pathRow->addWidget(browse);form->addRow("PGN Core data folder",pathRow);
    wallet_=new QLineEdit("PGN");wallet_->setObjectName("pgn-wallet-name");wallet_->setMaxLength(64);form->addRow("Wallet name",wallet_);locationBox->addLayout(form);
    auto* note=new QLabel("The default uses your operating system's user data location. You can select another dedicated folder.");note->setWordWrap(true);locationBox->addWidget(note);locationBox->addStretch();addPage(location);
    auto* confirmation=new QWizardPage;confirmation->setTitle("Review wallet setup");auto* confirmationBox=new QVBoxLayout(confirmation);review_=new QLabel;review_->setTextFormat(Qt::PlainText);review_->setWordWrap(true);review_->setTextInteractionFlags(Qt::TextSelectableByMouse);confirmationBox->addWidget(review_);
    auto* finishNote=new QLabel("Finish applies this selection and starts the wallet setup. Cancel leaves it unapplied.");finishNote->setWordWrap(true);confirmationBox->addWidget(finishNote);confirmationBox->addStretch();addPage(confirmation);
    error_=new QLabel;error_->setObjectName("pgn-wallet-setup-error");error_->setTextFormat(Qt::PlainText);error_->setWordWrap(true);error_->setStyleSheet("color:#ff6b79");setSideWidget(error_);
    connect(browse,&QPushButton::clicked,this,[this]{const auto chosen=QFileDialog::getExistingDirectory(this,"Choose PGN Core data folder",directory_->text());if(!chosen.isEmpty())directory_->setText(chosen);});
    connect(this,&QWizard::currentIdChanged,this,[this]{error_->clear();review();});review();
}
pgn::wallet::WalletSetupSelection WalletSetupWizard::selection()const{return {QDir::cleanPath(directory_->text().trimmed()),wallet_->text().trimmed(),existing_->isChecked()};}
void WalletSetupWizard::setSelection(const pgn::wallet::WalletSetupSelection& value){directory_->setText(value.dataDirectory);wallet_->setText(value.walletName);existing_->setChecked(value.existing);create_->setChecked(!value.existing);review();}
void WalletSetupWizard::review(){const auto value=selection();review_->setText(QString("Network: %1\nAction: %2\nData folder: %3\nWallet: %4").arg(network_,value.existing?"Open existing":"Create new",value.dataDirectory,value.walletName));}
bool WalletSetupWizard::validateCurrentPage(){if(currentId()==0)return true;const auto error=pgn::wallet::validateWalletSetup(selection(),applicationDirectory_);error_->setText(error);return error.isEmpty();}
void WalletSetupWizard::accept(){const auto invalid=pgn::wallet::validateWalletSetup(selection(),applicationDirectory_);if(!invalid.isEmpty()){error_->setText(invalid);return;}if(!commit_){error_->setText("Wallet setup integration is unavailable.");return;}const auto error=commit_(selection());if(!error.isEmpty()){error_->setText(error);return;}QWizard::accept();}
}
