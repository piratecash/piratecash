// Copyright (c) 2011-2013 The Bitcoin developers
// Copyright (c) 2018-2022 The PirateCash developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OPTIONSMODEL_H
#define OPTIONSMODEL_H

#include <QAbstractListModel>

extern bool fUseBlackTheme;

QT_BEGIN_NAMESPACE
class QNetworkProxy;
QT_END_NAMESPACE

/** Interface from Qt to configuration data structure for Bitcoin client.
   To Qt, the options are presented as a list with the different options
   laid out vertically.
   This can be changed to a tree once the settings become sufficiently
   complex.
 */
class OptionsModel : public QAbstractListModel
{
    Q_OBJECT

public:
    explicit OptionsModel(QObject *parent = 0);

    enum OptionID {
        StartAtStartup,         // bool
        MinimizeToTray,         // bool
        MapPortUPnP,            // bool
        MinimizeOnClose,        // bool
        ProxyUse,               // bool
        ProxyIP,                // QString
        ProxyPort,              // int
        ProxySocksVersion,      // int
        Fee,                    // qint64
        ReserveBalance,         // qint64
        DisplayUnit,            // BitcoinUnits::Unit
        Language,               // QString
        CoinControlFeatures,    // bool
        UseBlackTheme,     // bool
        DarksendRounds,    // int
        AnonymizepiratecashAmount, //int
        OptionIDRowCount,
    };

    void Init();
    void Reset();

    int rowCount(const QModelIndex & parent = QModelIndex()) const;
    QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const;
    bool setData(const QModelIndex & index, const QVariant & value, int role = Qt::EditRole);

    /* Explicit getters */
    qint64 getReserveBalance();
    bool getMinimizeToTray() { return fMinimizeToTray; }
    bool getMinimizeOnClose() { return fMinimizeOnClose; }
    int getDisplayUnit() { return nDisplayUnit; }
    bool getProxySettings(QNetworkProxy& proxy) const;
    bool getCoinControlFeatures() { return fCoinControlFeatures; }
    const QString& getOverriddenByCommandLine() { return strOverriddenByCommandLine; }

    /* Restart flag helper */
    void setRestartRequired(bool fRequired);
    bool isRestartRequired();
private:
    /* Qt-only settings */
    bool fMinimizeToTray;
    bool fMinimizeOnClose;
    QString language;
    int nDisplayUnit;
    bool fCoinControlFeatures;
    /* settings that were overriden by command-line */
    QString strOverriddenByCommandLine;

    /// Add option to list of GUI options overridden through command line/config file
    void addOverriddenOption(const std::string &option);

signals:
    void displayUnitChanged(int unit);
    void transactionFeeChanged(qint64);
    void reserveBalanceChanged(qint64);
    void coinControlFeaturesChanged(bool);
    void darksendRoundsChanged(int);
    void AnonymizepiratecashAmountChanged(int);
};

#endif // OPTIONSMODEL_H
