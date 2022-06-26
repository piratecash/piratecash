// Copyright (c) 2011-2014 The Bitcoin developers
// Copyright (c) 2018-2022 The PirateCash developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_WALLETMODEL_H
#define BITCOIN_QT_WALLETMODEL_H

#include "walletmodeltransaction.h"

#include "allocators.h" /* for SecureString */
#include "instantx.h"
#include "wallet/wallet.h"

#include <map>
#include <vector>

#include <QObject>

class AddressTableModel;
class OptionsModel;
class TransactionTableModel;
class WalletModelTransaction;

class CWallet;
class CKeyID;
class CPubKey;
class COutput;
class COutPoint;
class uint256;
class CCoinControl;

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

class SendCoinsRecipient
{
public:
    explicit SendCoinsRecipient() : amount(0), nVersion(SendCoinsRecipient::CURRENT_VERSION) { }
    explicit SendCoinsRecipient(const QString &addr, const QString &label, const CAmount& amount, const QString &message):
        address(addr), label(label), amount(amount), message(message), nVersion(SendCoinsRecipient::CURRENT_VERSION) {}

    // If from an insecure payment request, this is used for storing
    // the addresses, e.g. address-A<br />address-B<br />address-C.
    // Info: As we don't need to process addresses in here when using
    // payment requests, we can abuse it for displaying an address list.
    // Todo: This is a hack, should be replaced with a cleaner solution!
    QString address;
    QString label;
    QString narration;
    AvailableCoinsType inputType;
    bool useInstantX;
    CAmount amount;
    // If from a payment request, this is used for storing the memo
    QString message;

    int typeInd;

    // Empty if no authentication or invalid signature/cert/etc.
    QString authenticatedMerchant;

    static const int CURRENT_VERSION = 1;
    int nVersion;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        std::string sAddress = address.toStdString();
        std::string sLabel = label.toStdString();
        std::string sMessage = message.toStdString();

        READWRITE(this->nVersion);
        nVersion = this->nVersion;
        READWRITE(sAddress);
        READWRITE(sLabel);
        READWRITE(amount);
        READWRITE(sMessage);

        if (ser_action.ForRead())
        {
            address = QString::fromStdString(sAddress);
            label = QString::fromStdString(sLabel);
            message = QString::fromStdString(sMessage);
        }
    }
};

/** Interface to Bitcoin wallet from Qt view code. */
class WalletModel : public QObject
{
    Q_OBJECT

public:
    explicit WalletModel(CWallet *wallet, OptionsModel *optionsModel, QObject *parent = 0);
    ~WalletModel();

    enum StatusCode // Returned by sendCoins
    {
        OK,
        InvalidAmount,
        InvalidAddress,
        AmountExceedsBalance,
        AmountWithFeeExceedsBalance,
        DuplicateAddress,
        TransactionCreationFailed, // Error returned when wallet is still locked
        IXTransactionCreationFailed, // Error returned when InstantX fails in prepareTransaction
        PrepareTransactionFailed, // Error returned when InstantX fails in prepareTransaction
        TransactionCommitFailed,
        NarrationTooLong,
        Aborted,
        AnonymizeOnlyUnlocked,
        InsaneFee
    };

    enum EncryptionStatus
    {
        Unencrypted,  // !wallet->IsCrypted()
        Locked,       // wallet->IsCrypted() && wallet->IsLocked()
        Unlocked,      // wallet->IsCrypted() && !wallet->IsLocked()
        UnlockedForAnonymizationOnly     // wallet->IsCrypted() && !wallet->IsLocked() && wallet->fWalletUnlockAnonymizeOnly
    };

    OptionsModel *getOptionsModel();
    AddressTableModel *getAddressTableModel();
    TransactionTableModel *getTransactionTableModel();

    CAmount getBalance(const CCoinControl *coinControl=NULL) const;
    CAmount getStake() const;
    CAmount getUnconfirmedBalance() const;
    CAmount getImmatureBalance() const;
    CAmount getAnonymizedBalance() const;
    bool haveWatchOnly() const;
    CAmount getWatchBalance() const;
    CAmount getWatchStake() const;
    CAmount getWatchUnconfirmedBalance() const;
    CAmount getWatchImmatureBalance() const;
    EncryptionStatus getEncryptionStatus() const;

    // Check address for validity
    bool validateAddress(const QString &address);

    // Return status record for SendCoins, contains error id + information
    struct SendCoinsReturn
    {
        SendCoinsReturn(StatusCode status = OK):
            status(status) {}
        StatusCode status;
    };

    // prepare transaction for getting txfee before sending coins
    SendCoinsReturn prepareTransaction(WalletModelTransaction &transaction, const CCoinControl *coinControl = NULL);

    // Send coins to a list of recipients
    SendCoinsReturn sendCoins(WalletModelTransaction &transaction, const CCoinControl *coinControl);

    // Wallet encryption
    bool setWalletEncrypted(bool encrypted, const SecureString &passphrase);
    // Passphrase only needed when unlocking
    bool setWalletLocked(bool locked, const SecureString &passPhrase=SecureString(), bool anonymizeOnly=false);
    bool changePassphrase(const SecureString &oldPass, const SecureString &newPass);
    // Is wallet unlocked for anonymization only?
    bool isAnonymizeOnlyUnlocked();
    // Wallet backup
    bool backupWallet(const QString &filename);

    // RAI object for unlocking wallet, returned by requestUnlock()
    class UnlockContext
    {
    public:
        UnlockContext(WalletModel *wallet, bool valid, bool relock);
        ~UnlockContext();

        bool isValid() const { return valid; }

        // Copy operator and constructor transfer the context
        UnlockContext(const UnlockContext& obj) { CopyFrom(obj); }
        UnlockContext& operator=(const UnlockContext& rhs) { CopyFrom(rhs); return *this; }
    private:
        WalletModel *wallet;
        bool valid;
        mutable bool relock; // mutable, as it can be set to false by copying

        void CopyFrom(const UnlockContext& rhs);
    };

    UnlockContext requestUnlock();

    bool getPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const;
    bool havePrivKey(const CKeyID &address) const;
    void getOutputs(const std::vector<COutPoint>& vOutpoints, std::vector<COutput>& vOutputs);
    bool isSpent(const COutPoint& outpoint) const;
    void listCoins(std::map<QString, std::vector<COutput> >& mapCoins) const;

    bool isLockedCoin(uint256 hash, unsigned int n) const;
    void lockCoin(COutPoint& output);
    void unlockCoin(COutPoint& output);
    void listLockedCoins(std::vector<COutPoint>& vOutpts);
    bool processingQueuedTransactions() { return fProcessingQueuedTransactions; }

private:
    CWallet *wallet;
    bool fHaveWatchOnly;
    bool fForceCheckBalanceChanged;
    bool fProcessingQueuedTransactions;

    // Wallet has an options model for wallet-specific options
    // (transaction fee, for example)
    OptionsModel *optionsModel;

    AddressTableModel *addressTableModel;
    TransactionTableModel *transactionTableModel;

    // Cache some values to be able to detect changes
    CAmount cachedBalance;
    CAmount cachedStake;
    CAmount cachedUnconfirmedBalance;
    CAmount cachedImmatureBalance;
    CAmount cachedAnonymizedBalance;
    CAmount cachedWatchOnlyBalance;
    CAmount cachedWatchOnlyStake;
    CAmount cachedWatchUnconfBalance;
    CAmount cachedWatchImmatureBalance;
    CAmount cachedNumTransactions;
    EncryptionStatus cachedEncryptionStatus;
    int cachedNumBlocks;
    int cachedTxLocks;
    int cachedDarksendRounds;

    QTimer *pollTimer;

    void subscribeToCoreSignals();
    void unsubscribeFromCoreSignals();
    void checkBalanceChanged();

signals:
    // Signal that balance in wallet changed
    void balanceChanged(const CAmount& balance, const CAmount& stake, const CAmount& unconfirmedBalance, const CAmount& immatureBalance, const CAmount& anonymizedBalance,
        const CAmount& watchOnlyBalance, const CAmount& watchOnlyStake, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance);

    // Encryption status of wallet changed
    void encryptionStatusChanged(int status);

    // Signal emitted when wallet needs to be unlocked
    // It is valid behaviour for listeners to keep the wallet locked after this signal;
    // this means that the unlocking failed or was cancelled.
    void requireUnlock();

    // Asynchronous message notification
    void message(const QString &title, const QString &message, bool modal, unsigned int style);

    // Coins sent: from wallet, to recipient, in (serialized) transaction:
    void coinsSent(CWallet* wallet, SendCoinsRecipient recipient, QByteArray transaction);

    // Show progress dialog e.g. for rescan
    void showProgress(const QString &title, int nProgress);

    // Watch-only address added
    void notifyWatchonlyChanged(bool fHaveWatchonly);

public slots:
    /* Wallet status might have changed */
    void updateStatus();
    /* New transaction, or transaction changed status */
    void updateTransaction();
    /* New, updated or removed address book entry */
    void updateAddressBook(const QString &address, const QString &label, bool isMine, int status);
    /* Watch-only added */
    void updateWatchOnlyFlag(bool fHaveWatchonly);
    /* Current, immature or unconfirmed balance might have changed - emit 'balanceChanged' if so */
    void pollBalanceChanged();
    /* Needed to update fProcessingQueuedTransactions through a QueuedConnection */
    void setProcessingQueuedTransactions(bool value) { fProcessingQueuedTransactions = value; }

};

#endif // BITCOIN_QT_WALLETMODEL_H
