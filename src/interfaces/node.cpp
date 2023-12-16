// Copyright (c) 2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <interfaces/node.h>

#include <addrdb.h>
#include <amount.h>
#include <banman.h>
#include <chain.h>
#include <chainparams.h>
#include <evo/deterministicmns.h>
#include <governance/governance.h>
#include <governance/object.h>
#include <init.h>
#include <interfaces/chain.h>
#include <interfaces/handler.h>
#include <interfaces/wallet.h>
#include <llmq/instantsend.h>
#include <mapport.h>
#include <masternode/sync.h>
#include <net.h>
#include <net_processing.h>
#include <netaddress.h>
#include <netbase.h>
#include <policy/feerate.h>
#include <policy/fees.h>
#include <policy/policy.h>
#include <policy/settings.h>
#include <primitives/block.h>
#include <rpc/server.h>
#include <scheduler.h>
#include <shutdown.h>
#include <support/allocators/secure.h>
#include <sync.h>
#include <txmempool.h>
#include <ui_interface.h>
#include <util/system.h>
#include <util/translation.h>
#include <validation.h>
#include <warnings.h>

#if defined(HAVE_CONFIG_H)
#include <config/piratecash-config.h>
#endif

#include <coinjoin/coinjoin.h>
#include <coinjoin/options.h>

#include <atomic>
#include <boost/thread/thread.hpp>
#include <univalue.h>

class CWallet;
fs::path GetWalletDir();
std::vector<fs::path> ListWalletDir();
std::vector<std::shared_ptr<CWallet>> GetWallets();
std::shared_ptr<CWallet> LoadWallet(interfaces::Chain& chain, const std::string& name, bilingual_str& error, std::vector<bilingual_str>& warnings);
WalletCreationStatus CreateWallet(interfaces::Chain& chain, const SecureString& passphrase, uint64_t wallet_creation_flags, const std::string& name, bilingual_str& error, std::vector<bilingual_str>& warnings, std::shared_ptr<CWallet>& result);
std::unique_ptr<interfaces::Handler> HandleLoadWallet(interfaces::Node::LoadWalletFn load_wallet);

namespace interfaces {

namespace {

class EVOImpl : public EVO
{
public:
    CDeterministicMNList getListAtChainTip() override
    {
        return deterministicMNManager->GetListAtChainTip();
    }
};

class GOVImpl : public GOV
{
public:
    std::vector<CGovernanceObject> getAllNewerThan(int64_t nMoreThanTime) override
    {
        return governance.GetAllNewerThan(nMoreThanTime);
    }
};

class LLMQImpl : public LLMQ
{
public:
    size_t getInstantSentLockCount() override
    {
        if (!llmq::quorumInstantSendManager) {
            return 0;
        }
        return llmq::quorumInstantSendManager->GetInstantSendLockCount();
    }
};

class MasternodeSyncImpl : public Masternode::Sync
{
public:
    bool isSynced() override
    {
        return masternodeSync.IsSynced();
    }
    bool isBlockchainSynced() override
    {
        return masternodeSync.IsBlockchainSynced();
    }
    std::string getSyncStatus() override
    {
        return masternodeSync.GetSyncStatus();
    }
};

class CoinJoinOptionsImpl : public CoinJoin::Options
{
public:
    int getRounds() override
    {
        return CCoinJoinClientOptions::GetRounds();
    }
    int getAmount() override
    {
        return CCoinJoinClientOptions::GetAmount();
    }
    void setEnabled(bool fEnabled) override
    {
        return CCoinJoinClientOptions::SetEnabled(fEnabled);
    }
    void setMultiSessionEnabled(bool fEnabled) override
    {
        CCoinJoinClientOptions::SetMultiSessionEnabled(fEnabled);
    }
    void setRounds(int nRounds) override
    {
        CCoinJoinClientOptions::SetRounds(nRounds);
    }
    void setAmount(CAmount amount) override
    {
        CCoinJoinClientOptions::SetAmount(amount);
    }
    bool isEnabled() override
    {
        return CCoinJoinClientOptions::IsEnabled();
    }
    bool isMultiSessionEnabled() override
    {
        return CCoinJoinClientOptions::IsMultiSessionEnabled();
    }
    bool isCollateralAmount(CAmount nAmount) override
    {
        return CCoinJoin::IsCollateralAmount(nAmount);
    }
    CAmount getMinCollateralAmount() override
    {
        return CCoinJoin::GetCollateralAmount();
    }
    CAmount getMaxCollateralAmount() override
    {
        return CCoinJoin::GetMaxCollateralAmount();
    }
    CAmount getSmallestDenomination() override
    {
        return CCoinJoin::GetSmallestDenomination();
    }
    bool isDenominated(CAmount nAmount) override
    {
        return CCoinJoin::IsDenominatedAmount(nAmount);
    }
    std::array<CAmount, 5> getStandardDenominations() override
    {
        return CCoinJoin::GetStandardDenominations();
    }
};

class NodeImpl : public Node
{
public:
    NodeImpl() { m_interfaces.chain = MakeChain(); }

    EVOImpl m_evo;
    GOVImpl m_gov;
    LLMQImpl m_llmq;
    MasternodeSyncImpl m_masternodeSync;
    CoinJoinOptionsImpl m_coinjoin;

    void initError(const bilingual_str& message) override { InitError(message); }
    bool parseParameters(int argc, const char* const argv[], std::string& error) override
    {
        return gArgs.ParseParameters(argc, argv, error);
    }
    bool readConfigFiles(std::string& error) override { return gArgs.ReadConfigFiles(error, true); }
    bool softSetArg(const std::string& arg, const std::string& value) override { return gArgs.SoftSetArg(arg, value); }
    bool softSetBoolArg(const std::string& arg, bool value) override { return gArgs.SoftSetBoolArg(arg, value); }
    void selectParams(const std::string& network) override { SelectParams(network); }
    uint64_t getAssumedBlockchainSize() override { return Params().AssumedBlockchainSize(); }
    uint64_t getAssumedChainStateSize() override { return Params().AssumedChainStateSize(); }
    std::string getNetwork() override { return Params().NetworkIDString(); }
    void initLogging() override { InitLogging(); }
    void initParameterInteraction() override { InitParameterInteraction(); }
    std::string getWarnings(const std::string& type) override { return GetWarnings(type); }
    uint64_t getLogCategories() override { return LogInstance().GetCategoryMask(); }
    bool baseInitialize() override
    {
        return AppInitBasicSetup() && AppInitParameterInteraction() && AppInitSanityChecks() &&
               AppInitLockDataDirectory();
    }
    bool appInitMain() override { return AppInitMain(m_interfaces); }
    void appShutdown() override
    {
        Interrupt();
        Shutdown(m_interfaces);
    }
    void appPrepareShutdown() override
    {
        Interrupt();
        StartRestart();
        PrepareShutdown(m_interfaces);
    }
    void startShutdown() override { StartShutdown(); }
    bool shutdownRequested() override { return ShutdownRequested(); }
    void mapPort(bool use_upnp, bool use_natpmp) override { StartMapPort(use_upnp, use_natpmp); }
    void setupServerArgs() override { return SetupServerArgs(); }
    bool getProxy(Network net, proxyType& proxy_info) override { return GetProxy(net, proxy_info); }
    size_t getNodeCount(CConnman::NumConnections flags) override
    {
        return g_connman ? g_connman->GetNodeCount(flags) : 0;
    }
    bool getNodesStats(NodesStats& stats) override
    {
        stats.clear();

        if (g_connman) {
            std::vector<CNodeStats> stats_temp;
            g_connman->GetNodeStats(stats_temp);

            stats.reserve(stats_temp.size());
            for (auto& node_stats_temp : stats_temp) {
                stats.emplace_back(std::move(node_stats_temp), false, CNodeStateStats());
            }

            // Try to retrieve the CNodeStateStats for each node.
            TRY_LOCK(::cs_main, lockMain);
            if (lockMain) {
                for (auto& node_stats : stats) {
                    std::get<1>(node_stats) =
                        GetNodeStateStats(std::get<0>(node_stats).nodeid, std::get<2>(node_stats));
                }
            }
            return true;
        }
        return false;
    }
    bool getBanned(banmap_t& banmap) override
    {
        if (g_banman) {
            g_banman->GetBanned(banmap);
            return true;
        }
        return false;
    }
    bool ban(const CNetAddr& net_addr, BanReason reason, int64_t ban_time_offset) override
    {
        if (g_banman) {
            g_banman->Ban(net_addr, reason, ban_time_offset);
            return true;
        }
        return false;
    }
    bool unban(const CSubNet& ip) override
    {
        if (g_banman) {
            g_banman->Unban(ip);
            return true;
        }
        return false;
    }
    bool disconnect(const CNetAddr& net_addr) override
    {
        if (g_connman) {
            return g_connman->DisconnectNode(net_addr);
        }
        return false;
    }
    bool disconnect(NodeId id) override
    {
        if (g_connman) {
            return g_connman->DisconnectNode(id);
        }
        return false;
    }
    int64_t getTotalBytesRecv() override { return g_connman ? g_connman->GetTotalBytesRecv() : 0; }
    int64_t getTotalBytesSent() override { return g_connman ? g_connman->GetTotalBytesSent() : 0; }
    size_t getMempoolSize() override { return ::mempool.size(); }
    size_t getMempoolDynamicUsage() override { return ::mempool.DynamicMemoryUsage(); }
    bool getHeaderTip(int& height, int64_t& block_time) override
    {
        LOCK(::cs_main);
        if (::pindexBestHeader) {
            height = ::pindexBestHeader->nHeight;
            block_time = ::pindexBestHeader->GetBlockTime();
            return true;
        }
        return false;
    }
    int getNumBlocks() override
    {
        LOCK(::cs_main);
        return ::ChainActive().Height();
    }
    int64_t getLastBlockTime() override
    {
        LOCK(::cs_main);
        if (::ChainActive().Tip()) {
            return ::ChainActive().Tip()->GetBlockTime();
        }
        return Params().GenesisBlock().GetBlockTime(); // Genesis block's time of current network
    }
    std::string getLastBlockHash() override
    {
        LOCK(::cs_main);
        if (::ChainActive().Tip()) {
            return ::ChainActive().Tip()->GetBlockHash().ToString();
        }
        return Params().GenesisBlock().GetHash().ToString(); // Genesis block's hash of current network
    }
    double getVerificationProgress() override
    {
        const CBlockIndex* tip;
        {
            LOCK(::cs_main);
            tip = ::ChainActive().Tip();
        }
        return GuessVerificationProgress(Params().TxData(), tip);
    }
    bool isInitialBlockDownload() override { return ::ChainstateActive().IsInitialBlockDownload(); }
    bool getReindex() override { return ::fReindex; }
    bool getImporting() override { return ::fImporting; }
    void setNetworkActive(bool active) override
    {
        if (g_connman) {
            g_connman->SetNetworkActive(active);
        }
    }
    bool getNetworkActive() override { return g_connman && g_connman->GetNetworkActive(); }
    CFeeRate estimateSmartFee(int num_blocks, bool conservative, int* returned_target = nullptr) override
    {
        FeeCalculation fee_calc;
        CFeeRate result = ::feeEstimator.estimateSmartFee(num_blocks, &fee_calc, conservative);
        if (returned_target) {
            *returned_target = fee_calc.returnedTarget;
        }
        return result;
    }
    CFeeRate getDustRelayFee() override { return ::dustRelayFee; }
    UniValue executeRpc(const std::string& command, const UniValue& params, const std::string& uri) override
    {
        JSONRPCRequest req;
        req.params = params;
        req.strMethod = command;
        req.URI = uri;
        return ::tableRPC.execute(req);
    }
    std::vector<std::string> listRpcCommands() override { return ::tableRPC.listCommands(); }
    void rpcSetTimerInterfaceIfUnset(RPCTimerInterface* iface) override { RPCSetTimerInterfaceIfUnset(iface); }
    void rpcUnsetTimerInterface(RPCTimerInterface* iface) override { RPCUnsetTimerInterface(iface); }
    bool getUnspentOutput(const COutPoint& output, Coin& coin) override
    {
        LOCK(::cs_main);
        return ::ChainstateActive().CoinsTip().GetCoin(output, coin);
    }
    std::string getWalletDir() override
    {
        return GetWalletDir().string();
    }
    std::vector<std::string> listWalletDir() override
    {
        std::vector<std::string> paths;
        for (auto& path : ListWalletDir()) {
            paths.push_back(path.string());
        }
        return paths;
    }
    std::vector<std::unique_ptr<Wallet>> getWallets() override
    {
        std::vector<std::unique_ptr<Wallet>> wallets;
        for (const std::shared_ptr<CWallet>& wallet : GetWallets()) {
            wallets.emplace_back(MakeWallet(wallet));
        }
        return wallets;
    }
    std::unique_ptr<Wallet> loadWallet(const std::string& name, bilingual_str& error, std::vector<bilingual_str>& warnings) override
    {
        return MakeWallet(LoadWallet(*m_interfaces.chain, name, error, warnings));
    }

    EVO& evo() override { return m_evo; }
    GOV& gov() override { return m_gov; }
    LLMQ& llmq() override { return m_llmq; }
    Masternode::Sync& masternodeSync() override { return m_masternodeSync; }
    CoinJoin::Options& coinJoinOptions() override { return m_coinjoin; }

    WalletCreationStatus createWallet(const SecureString& passphrase, uint64_t wallet_creation_flags, const std::string& name, bilingual_str& error, std::vector<bilingual_str>& warnings, std::unique_ptr<Wallet>& result) override
    {
        std::shared_ptr<CWallet> wallet;
        WalletCreationStatus status = CreateWallet(*m_interfaces.chain, passphrase, wallet_creation_flags, name, error, warnings, wallet);
        result = MakeWallet(wallet);
        return status;
    }
    std::unique_ptr<Handler> handleInitMessage(InitMessageFn fn) override
    {
        return MakeHandler(::uiInterface.InitMessage_connect(fn));
    }
    std::unique_ptr<Handler> handleMessageBox(MessageBoxFn fn) override
    {
        return MakeHandler(::uiInterface.ThreadSafeMessageBox_connect(fn));
    }
    std::unique_ptr<Handler> handleQuestion(QuestionFn fn) override
    {
        return MakeHandler(::uiInterface.ThreadSafeQuestion_connect(fn));
    }
    std::unique_ptr<Handler> handleShowProgress(ShowProgressFn fn) override
    {
        return MakeHandler(::uiInterface.ShowProgress_connect(fn));
    }
    std::unique_ptr<Handler> handleLoadWallet(LoadWalletFn fn) override
    {
        return HandleLoadWallet(std::move(fn));
    }
    std::unique_ptr<Handler> handleNotifyNumConnectionsChanged(NotifyNumConnectionsChangedFn fn) override
    {
        return MakeHandler(::uiInterface.NotifyNumConnectionsChanged_connect(fn));
    }
    std::unique_ptr<Handler> handleNotifyNetworkActiveChanged(NotifyNetworkActiveChangedFn fn) override
    {
        return MakeHandler(::uiInterface.NotifyNetworkActiveChanged_connect(fn));
    }
    std::unique_ptr<Handler> handleNotifyAlertChanged(NotifyAlertChangedFn fn) override
    {
        return MakeHandler(::uiInterface.NotifyAlertChanged_connect(fn));
    }
    std::unique_ptr<Handler> handleBannedListChanged(BannedListChangedFn fn) override
    {
        return MakeHandler(::uiInterface.BannedListChanged_connect(fn));
    }
    std::unique_ptr<Handler> handleNotifyBlockTip(NotifyBlockTipFn fn) override
    {
        return MakeHandler(::uiInterface.NotifyBlockTip_connect([fn](bool initial_download, const CBlockIndex* block) {
            fn(initial_download, block->nHeight, block->GetBlockTime(), block->GetBlockHash().ToString(),
                GuessVerificationProgress(Params().TxData(), block));
        }));
    }
    std::unique_ptr<Handler> handleNotifyChainLock(NotifyChainLockFn fn) override
    {
        return MakeHandler(::uiInterface.NotifyChainLock_connect([fn](const std::string& bestChainLockHash, int bestChainLockHeight) {
            fn(bestChainLockHash, bestChainLockHeight);
        }));
    }
    std::unique_ptr<Handler> handleNotifyHeaderTip(NotifyHeaderTipFn fn) override
    {
        return MakeHandler(
            ::uiInterface.NotifyHeaderTip_connect([fn](bool initial_download, const CBlockIndex* block) {
                fn(initial_download, block->nHeight, block->GetBlockTime(), block->GetBlockHash().ToString(),
                    GuessVerificationProgress(Params().TxData(), block));
            }));
    }
    std::unique_ptr<Handler> handleNotifyMasternodeListChanged(NotifyMasternodeListChangedFn fn) override
    {
        return MakeHandler(
            ::uiInterface.NotifyMasternodeListChanged_connect([fn](const CDeterministicMNList& newList) {
                fn(newList);
            }));
    }
    std::unique_ptr<Handler> handleNotifyAdditionalDataSyncProgressChanged(NotifyAdditionalDataSyncProgressChangedFn fn) override
    {
        return MakeHandler(
            ::uiInterface.NotifyAdditionalDataSyncProgressChanged_connect([fn](double nSyncProgress) {
                fn(nSyncProgress);
            }));
    }
    InitInterfaces m_interfaces;
};

} // namespace

std::unique_ptr<Node> MakeNode() { return MakeUnique<NodeImpl>(); }

} // namespace interfaces
