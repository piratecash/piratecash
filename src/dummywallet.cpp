// Copyright (c) 2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <logging.h>
#include <util/system.h>
#include <walletinitinterface.h>
#include <support/allocators/secure.h>

#include <stdio.h>

class CWallet;
enum class WalletCreationStatus;
struct bilingual_str;

namespace interfaces {
class Chain;
class Handler;
class Wallet;
}

class DummyWalletInit : public WalletInitInterface {
public:

    bool HasWalletSupport() const override {return false;}
    void AddWalletOptions() const override;
    bool ParameterInteraction() const override {return true;}
    void Construct(InitInterfaces& interfaces) const override {LogPrintf("No wallet support compiled in!\n");}

    // Dash Specific WalletInitInterface InitCoinJoinSettings
    void AutoLockMasternodeCollaterals() const override {}
    void InitCoinJoinSettings() const override {}
    bool InitAutoBackup() const override {return true;}
};

void DummyWalletInit::AddWalletOptions() const
{
    gArgs.AddHiddenArgs({
        "-avoidpartialspends",
        "-createwalletbackups=<n>",
        "-disablewallet",
        "-instantsendnotify=<cmd>",
        "-keypool=<n>",
        "-maxtxfee=<amt>",
        "-rescan=<mode>",
        "-salvagewallet",
        "-spendzeroconfchange",
        "-upgradewallet",
        "-wallet=<path>",
        "-walletbackupsdir=<dir>",
        "-walletbroadcast",
        "-walletdir=<dir>",
        "-walletnotify=<cmd>",
        "-zapwallettxes=<mode>",
        "-discardfee=<amt>",
        "-fallbackfee=<amt>",
        "-mintxfee=<amt>",
        "-paytxfee=<amt>",
        "-txconfirmtarget=<n>",
        "-hdseed=<hex>",
        "-mnemonic=<text>",
        "-mnemonicpassphrase=<text>",
        "-usehd",
        "-enablecoinjoin",
        "-coinjoinamount=<n>",
        "-coinjoinautostart",
        "-coinjoindenomsgoal=<n>",
        "-coinjoindenomshardcap=<n>",
        "-coinjoinmultisession",
        "-coinjoinrounds=<n>",
        "-coinjoinsessions=<n>",
        "-dblogsize=<n>",
        "-flushwallet",
        "-privdb",
        "-walletrejectlongchains"
    });
}

const WalletInitInterface& g_wallet_init_interface = DummyWalletInit();

fs::path GetWalletDir()
{
    throw std::logic_error("Wallet function called in non-wallet build.");
}

std::vector<fs::path> ListWalletDir()
{
    throw std::logic_error("Wallet function called in non-wallet build.");
}

std::vector<std::shared_ptr<CWallet>> GetWallets()
{
    throw std::logic_error("Wallet function called in non-wallet build.");
}

std::shared_ptr<CWallet> LoadWallet(interfaces::Chain& chain, const std::string& name, bilingual_str& error, std::vector<bilingual_str>& warnings)
{
    throw std::logic_error("Wallet function called in non-wallet build.");
}

WalletCreationStatus CreateWallet(interfaces::Chain& chain, const SecureString& passphrase, uint64_t wallet_creation_flags, const std::string& name, bilingual_str& error, std::vector<bilingual_str>& warnings, std::shared_ptr<CWallet>& result)
{
    throw std::logic_error("Wallet function called in non-wallet build.");
}

using LoadWalletFn = std::function<void(std::unique_ptr<interfaces::Wallet> wallet)>;
std::unique_ptr<interfaces::Handler> HandleLoadWallet(LoadWalletFn load_wallet)
{
    throw std::logic_error("Wallet function called in non-wallet build.");
}

namespace interfaces {

std::unique_ptr<Wallet> MakeWallet(const std::shared_ptr<CWallet>& wallet)
{
    throw std::logic_error("Wallet function called in non-wallet build.");
}

} // namespace interfaces
