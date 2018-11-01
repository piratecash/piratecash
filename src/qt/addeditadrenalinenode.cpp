#include "addeditadrenalinenode.h"
#include "ui_addeditadrenalinenode.h"
#include "masternodeconfig.h"
#include "masternodemanager.h"
#include "ui_masternodemanager.h"

#include "walletdb.h"
#include "wallet.h"
#include "ui_interface.h"
#include "util.h"
#include "key.h"
#include "script.h"
#include "init.h"
#include "base58.h"
#include <QMessageBox>
#include <QClipboard>

AddEditAdrenalineNode::AddEditAdrenalineNode(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AddEditAdrenalineNode)
{
    ui->setupUi(this);

    //Labels
    ui->aliasLineEdit->setPlaceholderText("Enter your Masternode alias");
    ui->addressLineEdit->setPlaceholderText("Enter your IP & port");
    ui->privkeyLineEdit->setPlaceholderText("Enter your Masternode private key");
    ui->txhashLineEdit->setPlaceholderText("Enter your 10000 PIRATE TXID");
    ui->outputindexLineEdit->setPlaceholderText("Enter your transaction output index");
    ui->rewardaddressLineEdit->setPlaceholderText("Enter a reward recive address");
    ui->rewardpercentageLineEdit->setPlaceholderText("Input the % for the reward");
}

AddEditAdrenalineNode::~AddEditAdrenalineNode()
{
    delete ui;
}


void AddEditAdrenalineNode::on_okButton_clicked()
{
    if(ui->aliasLineEdit->text() == "")
    {
        QMessageBox msg;
        msg.setText("Please enter an alias.");
        msg.exec();
        return;
    }
    else if(ui->addressLineEdit->text() == "")
    {
        QMessageBox msg;
        msg.setText("Please enter an ip address and port");
        msg.exec();
        return;
    }
    else if(ui->privkeyLineEdit->text() == "")
    {
        QMessageBox msg;
        msg.setText("Please enter a masternode private key. This can be found using the \"masternode genkey\" command in the console.");
        msg.exec();
        return;
    }
    else if(ui->txhashLineEdit->text() == "")
    {
        QMessageBox msg;
        msg.setText("Please enter the transaction hash for the transaction that has 10000 coins");
        msg.exec();
        return;
    }
    else if(ui->outputindexLineEdit->text() == "")
    {
        QMessageBox msg;
        msg.setText("Please enter a transaction output index. This can be found using the \"masternode outputs\" command in the console.");
        msg.exec();
        return;
    }
    else
    {
        std::string sAlias = ui->aliasLineEdit->text().toStdString();
        std::string sAddress = ui->addressLineEdit->text().toStdString();
        std::string sMasternodePrivKey = ui->privkeyLineEdit->text().toStdString();
        std::string sTxHash = ui->txhashLineEdit->text().toStdString();
        std::string sOutputIndex = ui->outputindexLineEdit->text().toStdString();
        std::string sRewardAddress = ui->rewardaddressLineEdit->text().toStdString();
        std::string sRewardPercentage = ui->rewardpercentageLineEdit->text().toStdString();

        boost::filesystem::path pathConfigFile = GetDataDir() / "masternode.conf";
        boost::filesystem::ofstream stream (pathConfigFile.string(), ios::out | ios::app);
        if (stream.is_open())
        {
            stream << sAlias << " " << sAddress << " " << sMasternodePrivKey << " " << sTxHash << " " << sOutputIndex;
            if (sRewardAddress != "" && sRewardPercentage != ""){
                stream << " " << sRewardAddress << ":" << sRewardPercentage << std::endl;
            } else {
                stream << std::endl;
            }
            stream.close();
        }
        masternodeConfig.add(sAlias, sAddress, sMasternodePrivKey, sTxHash, sOutputIndex, sRewardAddress, sRewardPercentage);
        accept();
    }
}

void AddEditAdrenalineNode::on_cancelButton_clicked()
{
    reject();
}

void AddEditAdrenalineNode::on_AddEditAddressPasteButton_clicked()
{
    // Paste text from clipboard into recipient field
    ui->addressLineEdit->setText(QApplication::clipboard()->text());
}

void AddEditAdrenalineNode::on_AddEditPrivkeyPasteButton_clicked()
{
    // Paste text from clipboard into recipient field
    ui->privkeyLineEdit->setText(QApplication::clipboard()->text());
}

void AddEditAdrenalineNode::on_AddEditTxhashPasteButton_clicked()
{
    // Paste text from clipboard into recipient field
    ui->txhashLineEdit->setText(QApplication::clipboard()->text());
}
