#include "ui/researcher_profile_view.h"
#include "booking/booking_model.h"
#include "booking/researcher_name.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace pgn::ui {
QDialog* createResearcherProfileDialog(booking::BookingModel* model,QWidget* parent){
    auto* dialog=new QDialog(parent);
    dialog->setObjectName("pgn-researcher-profile");
    dialog->setWindowTitle("PGN · local display name");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    auto* layout=new QVBoxLayout(dialog);
    auto* description=new QLabel("Local display name only — not a wallet address, verified identity or public reservation.\n1–24 characters: A–Z, a–z, 0–9, _ and -. Start with a letter or digit.");
    description->setTextFormat(Qt::PlainText);description->setWordWrap(true);layout->addWidget(description);
    auto* name=new QLineEdit(dialog);name->setObjectName("pgn-researcher-name");
    name->setMaxLength(booking::researcherNameMaxLength);
    if(booking::validResearcherName(model->researcherName()))name->setText(model->researcherName());
    name->setPlaceholderText("Your local display name");name->setAccessibleName("Local display name");layout->addWidget(name);
    auto* error=new QLabel(dialog);error->setObjectName("pgn-researcher-name-error");error->setTextFormat(Qt::PlainText);error->setWordWrap(true);layout->addWidget(error);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,dialog);layout->addWidget(buttons);
    auto* save=buttons->button(QDialogButtonBox::Save);save->setObjectName("pgn-researcher-name-save");
    save->setEnabled(booking::validResearcherName(name->text()));
    QObject::connect(name,&QLineEdit::textChanged,dialog,[save,error](const QString& value){
        const bool valid=booking::validResearcherName(value);save->setEnabled(valid);
        error->setText(valid?QString{}:QString("Use 1–24 ASCII letters, digits, _ or -; no spaces. Start with a letter or digit."));
    });
    QObject::connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
    QObject::connect(buttons,&QDialogButtonBox::accepted,dialog,[dialog,model,name,error]{
        const auto failure=model->setResearcherName(name->text());
        if(!failure.isEmpty()){error->setText(failure);return;}
        dialog->accept();
    });
    return dialog;
}
}
