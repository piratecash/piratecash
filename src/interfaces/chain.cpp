// Copyright (c) 2018-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <interfaces/chain.h>

#include <chain.h>
#include <chainparams.h>
#include <coinjoin/coinjoin.h>
#include <interfaces/handler.h>
#include <interfaces/wallet.h>
#include <net.h>
#include <node/coin.h>
#include <node/transaction.h>
#include <policy/fees.h>
#include <policy/policy.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <rpc/protocol.h>
#include <rpc/server.h>
#include <shutdown.h>
#include <policy/settings.h>
#include <sync.h>
#include <timedata.h>
#include <txmempool.h>
#include <ui_interface.h>
#include <uint256.h>
#include <univalue.h>
#include <util/system.h>
#include <validation.h>
#include <validationinterface.h>

#include <memory>
#include <utility>

namespace interfaces {
namespace {

class LockImpl : public Chain::Lock, public UniqueLock<CCriticalSection>
{
    Optional<int> getHeight() override
    {
        LockAnnotation lock(::cs_main);
        int height = ::ChainActive().Height();
        if (height >= 0) {
            return height;
        }
        return nullopt;
    }
    Optional<int> getBlockHeight(const uint256& hash) override
    {
        LockAnnotation lock(::cs_main);
        CBlockIndex* block = LookupBlockIndex(hash);
        if (block && ::ChainActive().Contains(block)) {
            return block->nHeight;
        }
        return nullopt;
    }
    int getBlockDepth(const uint256& hash) override
    {
        const Optional<int> tip_height = getHeight();
        const Optional<int> height = getBlockHeight(hash);
        return tip_height && height ? *tip_height - *height + 1 : 0;
    }
    uint256 getBlockHash(int height) override
    {
        LockAnnotation lock(::cs_main);
        CBlockIndex* block = ::ChainActive()[height];
        assert(block != nullptr);
        return block->GetBlockHash();
    }
    int64_t getBlockTime(int height) override
    {
        LockAnnotation lock(::cs_main);
        CBlockIndex* block = ::ChainActive()[height];
        assert(block != nullptr);
        return block->GetBlockTime();
    }
    int64_t getBlockMedianTimePast(int height) override
    {
        LockAnnotation lock(::cs_main);
        CBlockIndex* block = ::ChainActive()[height];
        assert(block != nullptr);
        return block->GetMedianTimePast();
    }
    bool haveBlockOnDisk(int height) override
    {
        LockAnnotation lock(::cs_main);
        CBlockIndex* block = ::ChainActive()[height];
        return block && ((block->nStatus & BLOCK_HAVE_DATA) != 0) && block->nTx > 0;
    }
    Optional<int> findFirstBlockWithTimeAndHeight(int64_t time, int height, uint256* hash) override
    {
        LockAnnotation lock(::cs_main);
        CBlockIndex* block = ::ChainActive().FindEarliestAtLeast(time, height);
        if (block) {
            if (hash) *hash = block->GetBlockHash();
            return block->nHeight;
        }
        return nullopt;
    }
    Optional<int> findPruned(int start_height, Optional<int> stop_height) override
    {
        LockAnnotation lock(::cs_main);
        if (::fPruneMode) {
            CBlockIndex* block = stop_height ? ::ChainActive()[*stop_height] : ::ChainActive().Tip();
            while (block && block->nHeight >= start_height) {
                if ((block->nStatus & BLOCK_HAVE_DATA) == 0) {
                    return block->nHeight;
                }
                block = block->pprev;
            }
        }
        return nullopt;
    }
    Optional<int> findFork(const uint256& hash, Optional<int>* height) override
    {
        LockAnnotation lock(::cs_main);
        const CBlockIndex* block = LookupBlockIndex(hash);
        const CBlockIndex* fork = block ? ::ChainActive().FindFork(block) : nullptr;
        if (height) {
            if (block) {
                *height = block->nHeight;
            } else {
                height->reset();
            }
        }
        if (fork) {
            return fork->nHeight;
        }
        return nullopt;
    }
    bool isPotentialTip(const uint256& hash) override
    {
        LockAnnotation lock(::cs_main);
        if (::ChainActive().Tip()->GetBlockHash() == hash) return true;
        CBlockIndex* block = LookupBlockIndex(hash);
        return block && block->GetAncestor(::ChainActive().Height()) == ::ChainActive().Tip();
    }
    CBlockLocator getTipLocator() override
    {
        LockAnnotation lock(::cs_main);
        return ::ChainActive().GetLocator();
    }
    Optional<int> findLocatorFork(const CBlockLocator& locator) override
    {
        LockAnnotation lock(::cs_main);
        if (CBlockIndex* fork = FindForkInGlobalIndex(::ChainActive(), locator)) {
            return fork->nHeight;
        }
        return nullopt;
    }
    bool checkFinalTx(const CTransaction& tx) override
    {
        LockAnnotation lock(::cs_main);
        return CheckFinalTx(tx);
    }

    using UniqueLock::UniqueLock;
};

class NotificationsHandlerImpl : public Handler, CValidationInterface
{
public:
    explicit NotificationsHandlerImpl(Chain& chain, Chain::Notifications& notifications)
        : m_chain(chain), m_notifications(&notifications)
    {
        RegisterValidationInterface(this);
    }
    ~NotificationsHandlerImpl() override { disconnect(); }
    void disconnect() override
    {
        if (m_notifications) {
            m_notifications = nullptr;
            UnregisterValidationInterface(this);
        }
    }
    void TransactionAddedToMempool(const CTransactionRef& tx, int64_t nAcceptTime) override
    {
        m_notifications->TransactionAddedToMempool(tx, nAcceptTime);
    }
    void TransactionRemovedFromMempool(const CTransactionRef& tx, MemPoolRemovalReason reason) override
    {
        m_notifications->TransactionRemovedFromMempool(tx, reason);
    }
    void BlockConnected(const std::shared_ptr<const CBlock>& block,
        const CBlockIndex* index,
        const std::vector<CTransactionRef>& tx_conflicted) override
    {
        m_notifications->BlockConnected(*block, tx_conflicted);
    }
    void BlockDisconnected(const std::shared_ptr<const CBlock>& block, const CBlockIndex* pindexDisconnected) override
    {
        m_notifications->BlockDisconnected(*block);
    }
    void UpdatedBlockTip(const CBlockIndex* index, const CBlockIndex* fork_index, bool is_ibd) override
    {
        m_notifications->UpdatedBlockTip();
    }
    void ChainStateFlushed(const CBlockLocator& locator) override { m_notifications->ChainStateFlushed(locator); }
    void NotifyChainLock(const CBlockIndex* pindexChainLock, const std::shared_ptr<const llmq::CChainLockSig>& clsig) override
    {
        m_notifications->NotifyChainLock(pindexChainLock, clsig);
    }
    void NotifyTransactionLock(const CTransactionRef &tx, const std::shared_ptr<const llmq::CInstantSendLock>& islock) override
    {
        m_notifications->NotifyTransactionLock(tx, islock);
    }
    Chain& m_chain;
    Chain::Notifications* m_notifications;
};

class RpcHandlerImpl : public Handler
{
public:
    explicit RpcHandlerImpl(const CRPCCommand& command) : m_command(command), m_wrapped_command(&command)
    {
        m_command.actor = [this](const JSONRPCRequest& request, UniValue& result, bool last_handler) {
            if (!m_wrapped_command) return false;
            try {
                return m_wrapped_command->actor(request, result, last_handler);
            } catch (const UniValue& e) {
                // If this is not the last handler and a wallet not found
                // exception was thrown, return false so the next handler can
                // try to handle the request. Otherwise, reraise the exception.
                if (!last_handler) {
                    const UniValue& code = e["code"];
                    if (code.isNum() && code.get_int() == RPC_WALLET_NOT_FOUND) {
                        return false;
                    }
                }
                throw;
            }
        };
        ::tableRPC.appendCommand(m_command.name, &m_command);
    }

    void disconnect() override final
    {
        if (m_wrapped_command) {
            m_wrapped_command = nullptr;
            ::tableRPC.removeCommand(m_command.name, &m_command);
        }
    }

    ~RpcHandlerImpl() override { disconnect(); }

    CRPCCommand m_command;
    const CRPCCommand* m_wrapped_command;
};

class ChainImpl : public Chain
{
public:
    std::unique_ptr<Chain::Lock> lock(bool try_lock) override
    {
        auto lock = MakeUnique<LockImpl>(::cs_main, "cs_main", __FILE__, __LINE__, try_lock);
        if (try_lock && lock && !*lock) return {};
        std::unique_ptr<Chain::Lock> result = std::move(lock); // Temporary to avoid CWG 1579
        return result;
    }
    bool findBlock(const uint256& hash, CBlock* block, int64_t* time, int64_t* time_max) override
    {
        CBlockIndex* index;
        {
            LOCK(cs_main);
            index = LookupBlockIndex(hash);
            if (!index) {
                return false;
            }
            if (time) {
                *time = index->GetBlockTime();
            }
            if (time_max) {
                *time_max = index->GetBlockTimeMax();
            }
        }
        if (block && !ReadBlockFromDisk(*block, index, Params().GetConsensus())) {
            block->SetNull();
        }
        return true;
    }
    void findCoins(std::map<COutPoint, Coin>& coins) override { return FindCoins(coins); }
    double guessVerificationProgress(const uint256& block_hash) override
    {
        LOCK(cs_main);
        return GuessVerificationProgress(Params().TxData(), LookupBlockIndex(block_hash));
    }
    bool hasDescendantsInMempool(const uint256& txid) override
    {
        LOCK(::mempool.cs);
        auto it = ::mempool.GetIter(txid);
        return it && (*it)->GetCountWithDescendants() > 1;
    }
    bool broadcastTransaction(const CTransactionRef& tx, std::string& err_string, const CAmount& max_tx_fee, bool relay) override
    {
        const TransactionError err = BroadcastTransaction(tx, err_string, max_tx_fee, relay, /*wait_callback*/ false);
        // Chain clients only care about failures to accept the tx to the mempool. Disregard non-mempool related failures.
        // Note: this will need to be updated if BroadcastTransactions() is updated to return other non-mempool failures
        // that Chain clients do not need to know about.
        return TransactionError::OK == err;
    }
    void getTransactionAncestry(const uint256& txid, size_t& ancestors, size_t& descendants) override
    {
        ::mempool.GetTransactionAncestry(txid, ancestors, descendants);
    }
    bool checkChainLimits(const CTransactionRef& tx) override
    {
        LockPoints lp;
        CTxMemPoolEntry entry(tx, 0, 0, 0, false, 0, lp);
        CTxMemPool::setEntries ancestors;
        auto limit_ancestor_count = gArgs.GetArg("-limitancestorcount", DEFAULT_ANCESTOR_LIMIT);
        auto limit_ancestor_size = gArgs.GetArg("-limitancestorsize", DEFAULT_ANCESTOR_SIZE_LIMIT) * 1000;
        auto limit_descendant_count = gArgs.GetArg("-limitdescendantcount", DEFAULT_DESCENDANT_LIMIT);
        auto limit_descendant_size = gArgs.GetArg("-limitdescendantsize", DEFAULT_DESCENDANT_SIZE_LIMIT) * 1000;
        std::string unused_error_string;
        LOCK(::mempool.cs);
        return ::mempool.CalculateMemPoolAncestors(entry, ancestors, limit_ancestor_count, limit_ancestor_size,
            limit_descendant_count, limit_descendant_size, unused_error_string);
    }
    CFeeRate estimateSmartFee(int num_blocks, bool conservative, FeeCalculation* calc) override
    {
        return ::feeEstimator.estimateSmartFee(num_blocks, calc, conservative);
    }
    unsigned int estimateMaxBlocks() override
    {
        return ::feeEstimator.HighestTargetTracked(FeeEstimateHorizon::LONG_HALFLIFE);
    }
    CFeeRate mempoolMinFee() override
    {
        return ::mempool.GetMinFee(gArgs.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000);
    }
    CFeeRate relayMinFee() override { return ::minRelayTxFee; }
    CFeeRate relayIncrementalFee() override { return ::incrementalRelayFee; }
    CFeeRate relayDustFee() override { return ::dustRelayFee; }
    bool havePruned() override
    {
        LOCK(cs_main);
        return ::fHavePruned;
    }
    bool p2pEnabled() override { return g_connman != nullptr; }
    bool isReadyToBroadcast() override { return !::fImporting && !::fReindex && !::ChainstateActive().IsInitialBlockDownload(); }
    bool isInitialBlockDownload() override { return ::ChainstateActive().IsInitialBlockDownload(); }
    bool shutdownRequested() override { return ShutdownRequested(); }
    int64_t getAdjustedTime() override { return GetAdjustedTime(); }
    void initMessage(const std::string& message) override { ::uiInterface.InitMessage(message); }
    void initWarning(const bilingual_str& message) override { InitWarning(message); }
    void initError(const bilingual_str& message) override { InitError(message); }
    void showProgress(const std::string& title, int progress, bool resume_possible) override
    {
        ::uiInterface.ShowProgress(title, progress, resume_possible);
    }
    std::unique_ptr<Handler> handleNotifications(Notifications& notifications) override
    {
        return MakeUnique<NotificationsHandlerImpl>(*this, notifications);
    }
    void waitForNotifications() override { SyncWithValidationInterfaceQueue(); }
    std::unique_ptr<Handler> handleRpc(const CRPCCommand& command) override
    {
        return MakeUnique<RpcHandlerImpl>(command);
    }
    void requestMempoolTransactions(Notifications& notifications) override
    {
        LOCK2(::cs_main, ::mempool.cs);
        for (const CTxMemPoolEntry& entry : ::mempool.mapTx) {
            notifications.TransactionAddedToMempool(entry.GetSharedTx(), 0);
        }
    }
};
} // namespace

std::unique_ptr<Chain> MakeChain() { return MakeUnique<ChainImpl>(); }

} // namespace interfaces
