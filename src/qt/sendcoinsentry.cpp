// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2022 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/piratecash-config.h>
#endif

#include <qt/sendcoinsentry.h>
#include <qt/forms/ui_sendcoinsentry.h>

#include <qt/addressbookpage.h>
#include <qt/addresstablemodel.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>

#include <QApplication>
#include <QClipboard>

SendCoinsEntry::SendCoinsEntry(QWidget* parent) :
    QStackedWidget(parent),
    ui(new Ui::SendCoinsEntry),
    model(nullptr)
{
    ui->setupUi(this);

    GUIUtil::disableMacFocusRect(this);

    setCurrentWidget(ui->SendCoins);

    ui->addAsLabel->setPlaceholderText(tr("Enter a label for this address to add it to your address book"));

    setButtonIcons();

    // normal pirate address field
    GUIUtil::setupAddressWidget(ui->payTo, this, true);

    GUIUtil::setFont({ui->payToLabel,
                     ui->labellLabel,
                     ui->amountLabel,
                     ui->messageLabel}, GUIUtil::FontWeight::Normal, 15);

    GUIUtil::updateFonts();

    // Connect signals
    connect(ui->payAmount, &BitcoinAmountField::valueChanged, this, &SendCoinsEntry::payAmountChanged);
    connect(ui->checkboxSubtractFeeFromAmount, &QCheckBox::toggled, this, &SendCoinsEntry::subtractFeeFromAmountChanged);
    connect(ui->deleteButton, &QPushButton::clicked, this, &SendCoinsEntry::deleteClicked);
    connect(ui->deleteButton_is, &QPushButton::clicked, this, &SendCoinsEntry::deleteClicked);
    connect(ui->deleteButton_s, &QPushButton::clicked, this, &SendCoinsEntry::deleteClicked);
    connect(ui->useAvailableBalanceButton, &QPushButton::clicked, this, &SendCoinsEntry::useAvailableBalanceClicked);
}

SendCoinsEntry::~SendCoinsEntry()
{
    delete ui;
}

void SendCoinsEntry::on_pasteButton_clicked()
{
    // Paste text from clipboard into recipient field
    ui->payTo->setText(QApplication::clipboard()->text());
}

void SendCoinsEntry::on_addressBookButton_clicked()
{
    if(!model)
        return;
    AddressBookPage dlg(AddressBookPage::ForSelection, AddressBookPage::SendingTab, this);
    dlg.setModel(model->getAddressTableModel());
    if(dlg.exec())
    {
        ui->payTo->setText(dlg.getReturnValue());
        ui->payAmount->setFocus();
    }
}

void SendCoinsEntry::on_payTo_textChanged(const QString &address)
{
    SendCoinsRecipient rcp;
    if (GUIUtil::parseBitcoinURI(address, &rcp)) {
        ui->payTo->blockSignals(true);
        setValue(rcp);
        ui->payTo->blockSignals(false);
    } else {
        updateLabel(address);
    }
}

void SendCoinsEntry::setModel(WalletModel *_model)
{
    this->model = _model;

    if (_model && _model->getOptionsModel())
        connect(_model->getOptionsModel(), &OptionsModel::displayUnitChanged, this, &SendCoinsEntry::updateDisplayUnit);

    clear();
}

void SendCoinsEntry::clear()
{
    // clear UI elements for normal payment
    ui->payTo->clear();
    ui->addAsLabel->clear();
    ui->payAmount->clear();
    ui->checkboxSubtractFeeFromAmount->setCheckState(Qt::Unchecked);
    ui->messageTextLabel->clear();
    ui->messageTextLabel->hide();
    ui->messageLabel->hide();
    // clear UI elements for unauthenticated payment request
    ui->payTo_is->clear();
    ui->memoTextLabel_is->clear();
    ui->payAmount_is->clear();
    // clear UI elements for authenticated payment request
    ui->payTo_s->clear();
    ui->memoTextLabel_s->clear();
    ui->payAmount_s->clear();

    // update the display unit, to not use the default ("BTC")
    updateDisplayUnit();
}

void SendCoinsEntry::checkSubtractFeeFromAmount()
{
    ui->checkboxSubtractFeeFromAmount->setChecked(true);
}

void SendCoinsEntry::deleteClicked()
{
    Q_EMIT removeEntry(this);
}

void SendCoinsEntry::useAvailableBalanceClicked()
{
    Q_EMIT useAvailableBalance(this);
}

bool SendCoinsEntry::validate(interfaces::Node& node)
{
    if (!model)
        return false;

    // Check input validity
    bool retval = true;

    if (!model->validateAddress(ui->payTo->text()))
    {
        ui->payTo->setValid(false);
        retval = false;
    }

    if (!ui->payAmount->validate())
    {
        retval = false;
    }

    // Sending a zero amount is invalid
    if (ui->payAmount->value(nullptr) <= 0)
    {
        ui->payAmount->setValid(false);
        retval = false;
    }

    // Reject dust outputs:
    if (retval && GUIUtil::isDust(node, ui->payTo->text(), ui->payAmount->value())) {
        ui->payAmount->setValid(false);
        retval = false;
    }

    return retval;
}

SendCoinsRecipient SendCoinsEntry::getValue()
{
    // Normal payment
    recipient.address = ui->payTo->text();
    recipient.label = ui->addAsLabel->text();
    recipient.amount = ui->payAmount->value();
    recipient.message = ui->messageTextLabel->text();
    recipient.fSubtractFeeFromAmount = (ui->checkboxSubtractFeeFromAmount->checkState() == Qt::Checked);

    return recipient;
}

QWidget *SendCoinsEntry::setupTabChain(QWidget *prev)
{
    QWidget::setTabOrder(prev, ui->payTo);
    QWidget::setTabOrder(ui->payTo, ui->addAsLabel);
    QWidget *w = ui->payAmount->setupTabChain(ui->addAsLabel);
    QWidget::setTabOrder(w, ui->checkboxSubtractFeeFromAmount);
    QWidget::setTabOrder(ui->checkboxSubtractFeeFromAmount, ui->addressBookButton);
    QWidget::setTabOrder(ui->addressBookButton, ui->pasteButton);
    QWidget::setTabOrder(ui->pasteButton, ui->deleteButton);
    return ui->deleteButton;
}

void SendCoinsEntry::setValue(const SendCoinsRecipient &value)
{
    recipient = value;
    {
        // message
        ui->messageTextLabel->setText(recipient.message);
        ui->messageTextLabel->setVisible(!recipient.message.isEmpty());
        ui->messageLabel->setVisible(!recipient.message.isEmpty());

        ui->payTo->setText(recipient.address);
        ui->addAsLabel->setText(recipient.label);
        ui->payAmount->setValue(recipient.amount);
    }

    updateLabel(recipient.address);
}

void SendCoinsEntry::setAddress(const QString &address)
{
    ui->payTo->setText(address);
    ui->payAmount->setFocus();
}

void SendCoinsEntry::setAmount(const CAmount &amount)
{
    ui->payAmount->setValue(amount);
}

bool SendCoinsEntry::isClear()
{
    return ui->payTo->text().isEmpty() && ui->payTo_is->text().isEmpty() && ui->payTo_s->text().isEmpty();
}

void SendCoinsEntry::setFocus()
{
    ui->payTo->setFocus();
}

void SendCoinsEntry::updateDisplayUnit()
{
    if(model && model->getOptionsModel())
    {
        // Update payAmount with the current unit
        ui->payAmount->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
        ui->payAmount_is->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
        ui->payAmount_s->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
    }
}

void SendCoinsEntry::changeEvent(QEvent* e)
{
    QStackedWidget::changeEvent(e);
    if (e->type() == QEvent::StyleChange) {
        // Adjust button icon colors on theme changes
        setButtonIcons();
    }
}

void SendCoinsEntry::setButtonIcons()
{
    GUIUtil::setIcon(ui->addressBookButton, "address-book");
    GUIUtil::setIcon(ui->pasteButton, "editpaste");
    GUIUtil::setIcon(ui->deleteButton, "remove", GUIUtil::ThemedColor::RED);
    GUIUtil::setIcon(ui->deleteButton_is, "remove", GUIUtil::ThemedColor::RED);
    GUIUtil::setIcon(ui->deleteButton_s, "remove", GUIUtil::ThemedColor::RED);
}

bool SendCoinsEntry::updateLabel(const QString &address)
{
    if(!model)
        return false;

    // Fill in label from address book, if address has an associated label
    QString associatedLabel = model->getAddressTableModel()->labelForAddress(address);
    if(!associatedLabel.isEmpty())
    {
        ui->addAsLabel->setText(associatedLabel);
        return true;
    }

    return false;
}
