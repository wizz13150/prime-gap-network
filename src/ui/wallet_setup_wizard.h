#pragma once
#include "wallet/setup_selection.h"
#include <QWizard>
#include <functional>
class QLineEdit;
class QRadioButton;
class QLabel;
namespace pgn::ui {
class WalletSetupWizard final:public QWizard {
public:
    // Called only by Finish. Return an error to keep the wizard open.
    using Commit=std::function<QString(const pgn::wallet::WalletSetupSelection&)>;
    WalletSetupWizard(QString applicationDirectory,QString network,Commit,QWidget* parent=nullptr);
    pgn::wallet::WalletSetupSelection selection()const;
    void setSelection(const pgn::wallet::WalletSetupSelection&);
protected:
    void accept()override;
    bool validateCurrentPage()override;
private:
    QString applicationDirectory_,network_;Commit commit_;
    QLineEdit *directory_{},*wallet_{};QRadioButton *create_{},*existing_{};QLabel *review_{},*error_{};
    void review();
};
}
