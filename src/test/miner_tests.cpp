// Copyright (c) 2011-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <consensus/tx_verify.h>
#include <node/miner.h>
#include <policy/policy.h>
#include <pow.h>
#include <script/standard.h>
#include <test/util/random.h>
#include <test/util/txmempool.h>
#include <timedata.h>
#include <uint256.h>
#include <util/strencodings.h>
#include <util/system.h>
#include <util/time.h>
#include <validation.h>
#include <versionbits.h>

#include <evo/evodb.h>
#include <chainlock/handler.h>
#include <governance/governance.h>
#include <instantsend/instantsend.h>
#include <llmq/blockprocessor.h>
#include <llmq/context.h>

#include <test/util/setup_common.h>

#include <memory>

#include <boost/test/unit_test.hpp>

using node::BlockAssembler;
using node::CBlockTemplate;

namespace miner_tests {
struct MinerTestingSetup : public TestingSetup {
    // Exercise transaction validation after the PirateCash fork, while preserving
    // the pre-DIP1 sigop limit and pre-BIP68 template-selection scenarios below.
    MinerTestingSetup() : TestingSetup(CBaseChainParams::REGTEST,
                                      {"-testactivationheight=csv@1000", "-testactivationheight=dip0001@1000"}) {}
    void TestPackageSelection(const CScript& scriptPubKey, const std::vector<CTransactionRef>& txFirst) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    void TestBasicMining(const CScript& scriptPubKey, const std::vector<CTransactionRef>& txFirst, int baseheight) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    void TestPrioritisedMining(const CScript& scriptPubKey, const std::vector<CTransactionRef>& txFirst) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    bool TestSequenceLocks(const CTransaction& tx, CTxMemPool& tx_mempool) EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
    {
        CCoinsViewMemPool view_mempool{&m_node.chainman->ActiveChainstate().CoinsTip(), tx_mempool};
        CBlockIndex* tip{m_node.chainman->ActiveChain().Tip()};
        const std::optional<LockPoints> lock_points{CalculateLockPointsAtTip(tip, view_mempool, tx)};
        return lock_points.has_value() && CheckSequenceLocksAtTip(tip, *lock_points);
    }
    CTxMemPool& MakeMempool()
    {
        // Delete the previous mempool to ensure with valgrind that the old
        // pointer is not accessed, when the new one should be accessed
        // instead.
        m_node.mempool.reset();
        m_node.mempool = std::make_unique<CTxMemPool>(MemPoolOptionsForTest(m_node));
        return *m_node.mempool;
    }
    BlockAssembler AssemblerForTest(CTxMemPool& tx_mempool);
};

static constexpr int WAIT_FOR_ISLOCK_TIMEOUT{601};
} // namespace miner_tests

BOOST_FIXTURE_TEST_SUITE(miner_tests, MinerTestingSetup)

static CFeeRate blockMinFeeRate = CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);

BlockAssembler MinerTestingSetup::AssemblerForTest(CTxMemPool& tx_mempool)
{
    BlockAssembler::Options options;

    options.nBlockMaxSize = DEFAULT_BLOCK_MAX_SIZE;
    options.blockMinFeeRate = blockMinFeeRate;
    return BlockAssembler(m_node.chainman->ActiveChainstate(), m_node, &tx_mempool, options);
}

// Precomputed scrypt nonces for the PirateCash regtest chain with SHA256d block IDs.
constexpr static struct {
    unsigned char extranonce;
    unsigned int nonce;
} BLOCKINFO[]{{0, 0}, {0, 0}, {0, 1}, {0, 1}, {0, 4}, {0, 1},
              {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 1}, {0, 0},
              {0, 2}, {0, 0}, {0, 1}, {0, 0}, {0, 5}, {0, 0},
              {0, 0}, {0, 5}, {0, 0}, {0, 0}, {0, 1}, {0, 0},
              {0, 0}, {0, 2}, {0, 0}, {0, 0}, {0, 2}, {0, 1},
              {0, 0}, {0, 1}, {0, 2}, {0, 1}, {0, 0}, {0, 0},
              {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 2}, {0, 0},
              {0, 1}, {0, 0}, {0, 3}, {0, 0}, {0, 0}, {0, 0},
              {0, 5}, {0, 0}, {0, 1}, {0, 1}, {0, 0}, {0, 6},
              {0, 1}, {0, 1}, {0, 1}, {0, 0}, {0, 1}, {0, 0},
              {0, 1}, {0, 0}, {0, 0}, {0, 0}, {0, 2}, {0, 0},
              {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 3},
              {0, 7}, {0, 1}, {0, 2}, {0, 1}, {0, 2}, {0, 0},
              {0, 0}, {0, 0}, {0, 2}, {0, 0}, {0, 0}, {0, 0},
              {0, 0}, {0, 0}, {0, 0}, {0, 1}, {0, 1}, {0, 0},
              {0, 0}, {0, 2}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
              {0, 3}, {0, 0}, {0, 0}, {0, 1}, {0, 0}, {0, 0},
              {0, 2}, {0, 0}, {0, 0}, {0, 0}, {0, 1}, {0, 1},
              {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 2},
              {0, 4}, {0, 0}, {0, 3}, {0, 1}, {0, 0}, {0, 0},
              {0, 1}, {0, 2}, {0, 0}, {0, 3}, {0, 0}, {0, 0},
              {0, 0}, {0, 2}, {0, 4}, {0, 0}, {0, 0}, {0, 1},
              {0, 1}, {0, 0}, {0, 0}, {0, 0}, {0, 2}, {0, 0},
              {0, 2}};

static std::unique_ptr<CBlockIndex> CreateBlockIndex(int nHeight, CBlockIndex* active_chain_tip) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    auto index{std::make_unique<CBlockIndex>()};
    index->nHeight = nHeight;
    index->pprev = active_chain_tip;
    return index;
}

// Test suite for ancestor feerate transaction selection.
// Implemented as an additional function, rather than a separate test case,
// to allow reusing the blockchain created in CreateNewBlock_validity.
void MinerTestingSetup::TestPackageSelection(const CScript& scriptPubKey, const std::vector<CTransactionRef>& txFirst)
{
    // Disable free transactions, otherwise TX selection is non-deterministic
    gArgs.SoftSetArg("-blockprioritysize", "0");

    CTxMemPool& tx_mempool{MakeMempool()};
    LOCK(tx_mempool.cs);
    // Test the ancestor feerate transaction selection.
    TestMemPoolEntryHelper entry;

    // Test that a medium fee transaction will be selected after a higher fee
    // rate package with a low fee rate parent.
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = txFirst[0]->vout[0].nValue - 1000;
    // This tx has a low fee: 1000 satoshis
    uint256 hashParentTx = tx.GetHash(); // save this txid for later use
    tx_mempool.addUnchecked(entry.Fee(1000).Time(Now<NodeSeconds>()).SpendsCoinbase(true).FromTx(tx));

    // This tx has a medium fee: 10000 satoshis
    tx.vin[0].prevout.hash = txFirst[1]->GetHash();
    tx.vout[0].nValue = txFirst[1]->vout[0].nValue - 10000;
    uint256 hashMediumFeeTx = tx.GetHash();
    tx_mempool.addUnchecked(entry.Fee(10000).Time(Now<NodeSeconds>()).SpendsCoinbase(true).FromTx(tx));

    // This tx has a high fee, but depends on the first transaction
    tx.vin[0].prevout.hash = hashParentTx;
    tx.vout[0].nValue = txFirst[0]->vout[0].nValue - 1000 - 50000; // 50k satoshi fee
    uint256 hashHighFeeTx = tx.GetHash();
    tx_mempool.addUnchecked(entry.Fee(50000).Time(Now<NodeSeconds>()).SpendsCoinbase(false).FromTx(tx));

    // Age transaction for InstantSend
    m_node.clhandler->UpdateTxFirstSeenMap({hashParentTx, hashMediumFeeTx, hashHighFeeTx}, std::chrono::duration_cast<std::chrono::seconds>(Now<NodeSeconds>().time_since_epoch()).count() - WAIT_FOR_ISLOCK_TIMEOUT);
    std::unique_ptr<CBlockTemplate> pblocktemplate = AssemblerForTest(tx_mempool).CreateNewBlock(scriptPubKey);
    BOOST_REQUIRE_EQUAL(pblocktemplate->block.vtx.size(), 4U);
    BOOST_CHECK(pblocktemplate->block.vtx[1]->GetHash() == hashParentTx);
    BOOST_CHECK(pblocktemplate->block.vtx[2]->GetHash() == hashHighFeeTx);
    BOOST_CHECK(pblocktemplate->block.vtx[3]->GetHash() == hashMediumFeeTx);

    // Test that a package below the block min tx fee doesn't get included
    tx.vin[0].prevout.hash = hashHighFeeTx;
    tx.vout[0].nValue = txFirst[0]->vout[0].nValue - 1000 - 50000; // 0 fee
    uint256 hashFreeTx = tx.GetHash();
    // Age transaction for InstantSend
    m_node.clhandler->UpdateTxFirstSeenMap({hashFreeTx}, std::chrono::duration_cast<std::chrono::seconds>(Now<NodeSeconds>().time_since_epoch()).count() - WAIT_FOR_ISLOCK_TIMEOUT);
    tx_mempool.addUnchecked(entry.Fee(0).FromTx(tx));
    size_t freeTxSize = GetVirtualTransactionSize(CTransaction(tx));

    // Calculate a fee on child transaction that will put the package just
    // below the block min tx fee (assuming 1 child tx of the same size).
    CAmount feeToUse = blockMinFeeRate.GetFee(2*freeTxSize) - 1;

    tx.vin[0].prevout.hash = hashFreeTx;
    tx.vout[0].nValue = txFirst[0]->vout[0].nValue - 1000 - 50000 - feeToUse;
    uint256 hashLowFeeTx = tx.GetHash();
    tx_mempool.addUnchecked(entry.Fee(feeToUse).FromTx(tx));
    pblocktemplate = AssemblerForTest(tx_mempool).CreateNewBlock(scriptPubKey);
    // Verify that the free tx and the low fee tx didn't get selected
    for (size_t i=0; i<pblocktemplate->block.vtx.size(); ++i) {
        BOOST_CHECK(pblocktemplate->block.vtx[i]->GetHash() != hashFreeTx);
        BOOST_CHECK(pblocktemplate->block.vtx[i]->GetHash() != hashLowFeeTx);
    }

    // Test that packages above the min relay fee do get included, even if one
    // of the transactions is below the min relay fee
    // Remove the low fee transaction and replace with a higher fee transaction
    tx_mempool.removeRecursive(CTransaction(tx), MemPoolRemovalReason::MANUAL);
    tx.vout[0].nValue -= 2; // Now we should be just over the min relay fee
    hashLowFeeTx = tx.GetHash();
    tx_mempool.addUnchecked(entry.Fee(feeToUse+2).FromTx(tx));
    // Age transaction for InstantSend
    m_node.clhandler->UpdateTxFirstSeenMap({hashLowFeeTx}, std::chrono::duration_cast<std::chrono::seconds>(Now<NodeSeconds>().time_since_epoch()).count() - WAIT_FOR_ISLOCK_TIMEOUT);
    pblocktemplate = AssemblerForTest(tx_mempool).CreateNewBlock(scriptPubKey);
    BOOST_REQUIRE_EQUAL(pblocktemplate->block.vtx.size(), 6U);
    BOOST_CHECK(pblocktemplate->block.vtx[4]->GetHash() == hashFreeTx);
    BOOST_CHECK(pblocktemplate->block.vtx[5]->GetHash() == hashLowFeeTx);

    // Test that transaction selection properly updates ancestor fee
    // calculations as ancestor transactions get included in a block.
    // Add a 0-fee transaction that has 2 outputs.
    tx.vin[0].prevout.hash = txFirst[2]->GetHash();
    tx.vout.resize(2);
    tx.vout[0].nValue = txFirst[2]->vout[0].nValue - 100000000;
    tx.vout[1].nValue = 100000000; // 1BTC output
    uint256 hashFreeTx2 = tx.GetHash();
    tx_mempool.addUnchecked(entry.Fee(0).SpendsCoinbase(true).FromTx(tx));
    // Age transaction for InstantSend
    m_node.clhandler->UpdateTxFirstSeenMap({tx.GetHash()}, std::chrono::duration_cast<std::chrono::seconds>(Now<NodeSeconds>().time_since_epoch()).count() - WAIT_FOR_ISLOCK_TIMEOUT);

    // This tx can't be mined by itself
    tx.vin[0].prevout.hash = hashFreeTx2;
    tx.vout.resize(1);
    feeToUse = blockMinFeeRate.GetFee(freeTxSize);
    tx.vout[0].nValue = txFirst[2]->vout[0].nValue - 100000000 - feeToUse;
    uint256 hashLowFeeTx2 = tx.GetHash();
    tx_mempool.addUnchecked(entry.Fee(feeToUse).SpendsCoinbase(false).FromTx(tx));
    // Age transaction for InstantSend
    m_node.clhandler->UpdateTxFirstSeenMap({tx.GetHash()}, std::chrono::duration_cast<std::chrono::seconds>(Now<NodeSeconds>().time_since_epoch()).count() - WAIT_FOR_ISLOCK_TIMEOUT);
    pblocktemplate = AssemblerForTest(tx_mempool).CreateNewBlock(scriptPubKey);

    // Verify that this tx isn't selected.
    for (size_t i=0; i<pblocktemplate->block.vtx.size(); ++i) {
        BOOST_CHECK(pblocktemplate->block.vtx[i]->GetHash() != hashFreeTx2);
        BOOST_CHECK(pblocktemplate->block.vtx[i]->GetHash() != hashLowFeeTx2);
    }

    // This tx will be mineable, and should cause hashLowFeeTx2 to be selected
    // as well.
    tx.vin[0].prevout.n = 1;
    tx.vout[0].nValue = 100000000 - 10000; // 10k satoshi fee
    tx_mempool.addUnchecked(entry.Fee(10000).FromTx(tx));
    // Age transaction for InstantSend
    m_node.clhandler->UpdateTxFirstSeenMap({tx.GetHash()}, std::chrono::duration_cast<std::chrono::seconds>(Now<NodeSeconds>().time_since_epoch()).count() - WAIT_FOR_ISLOCK_TIMEOUT);
    pblocktemplate = AssemblerForTest(tx_mempool).CreateNewBlock(scriptPubKey);
    BOOST_REQUIRE_EQUAL(pblocktemplate->block.vtx.size(), 9U);
    BOOST_CHECK(pblocktemplate->block.vtx[8]->GetHash() == hashLowFeeTx2);
}

void MinerTestingSetup::TestBasicMining(const CScript& scriptPubKey, const std::vector<CTransactionRef>& txFirst, int baseheight)
{
    uint256 hash;
    CMutableTransaction tx;
    TestMemPoolEntryHelper entry;
    entry.nFee = 11;
    entry.nHeight = 11;

    const CAmount BLOCKSUBSIDY = txFirst[0]->vout[0].nValue;
    const CAmount LOWFEE = CENT;
    const CAmount HIGHFEE = COIN;
    const CAmount HIGHERFEE = 4 * COIN;

    {
        CTxMemPool& tx_mempool{MakeMempool()};
        LOCK(tx_mempool.cs);

        // Just to make sure we can still make simple blocks
        auto pblocktemplate = AssemblerForTest(tx_mempool).CreateNewBlock(scriptPubKey);
        BOOST_CHECK(pblocktemplate);

        // Exceed the pre-DIP1 block sigop limit by one CHECKMULTISIG.
        tx.vin.resize(1);
        // NOTE: OP_NOP is used to force 20 SigOps for the CHECKMULTISIG
        tx.vin[0].scriptSig = CScript() << OP_0 << OP_0 << OP_0 << OP_NOP << OP_CHECKMULTISIG << OP_1;
        tx.vin[0].prevout.hash = txFirst[0]->GetHash();
        tx.vin[0].prevout.n = 0;
        tx.vout.resize(1);
        tx.vout[0].nValue = BLOCKSUBSIDY;
        for (unsigned int i = 0; i < MaxBlockSigOps(/*fDIP0001Active=*/false) / 20 + 1; ++i) {
            tx.vout[0].nValue -= LOWFEE;
            hash = tx.GetHash();
            // Age transaction for InstantSend
            m_node.clhandler->UpdateTxFirstSeenMap({hash}, pblocktemplate->block.nTime - WAIT_FOR_ISLOCK_TIMEOUT);
            bool spendsCoinbase = i == 0; // only first tx spends coinbase
            // If we don't set the # of sig ops in the CTxMemPoolEntry, template creation fails
            tx_mempool.addUnchecked(entry.Fee(LOWFEE).Time(Now<NodeSeconds>()).SpendsCoinbase(spendsCoinbase).FromTx(tx));
            tx.vin[0].prevout.hash = hash;
        }

        BOOST_CHECK_EXCEPTION(AssemblerForTest(tx_mempool).CreateNewBlock(scriptPubKey), std::runtime_error, HasReason("bad-blk-sigops"));
    }

    {
        CTxMemPool& tx_mempool{MakeMempool()};
        LOCK(tx_mempool.cs);

        auto pblocktemplate = AssemblerForTest(tx_mempool).CreateNewBlock(scriptPubKey);
        BOOST_CHECK(pblocktemplate);

        tx.vin[0].prevout.hash = txFirst[0]->GetHash();
        tx.vout[0].nValue = BLOCKSUBSIDY;
        for (unsigned int i = 0; i < MaxBlockSigOps(/*fDIP0001Active=*/false) / 20 + 1; ++i) {
            tx.vout[0].nValue -= LOWFEE;
            hash = tx.GetHash();
            bool spendsCoinbase = i == 0; // only first tx spends coinbase
            // If we do set the # of sig ops in the CTxMemPoolEntry, template creation passes
            // Age transaction for InstantSend
            m_node.clhandler->UpdateTxFirstSeenMap({hash}, pblocktemplate->block.nTime - WAIT_FOR_ISLOCK_TIMEOUT);
            tx_mempool.addUnchecked(entry.Fee(LOWFEE).Time(Now<NodeSeconds>()).SpendsCoinbase(spendsCoinbase).SigOps(20).FromTx(tx));
            tx.vin[0].prevout.hash = hash;
        }
        BOOST_CHECK(AssemblerForTest(tx_mempool).CreateNewBlock(scriptPubKey));
    }

    {
        CTxMemPool& tx_mempool{MakeMempool()};
        LOCK(tx_mempool.cs);

        // block size > limit
        tx.vin[0].scriptSig = CScript();
        // 18 * (520char + DROP) + OP_1 = 9433 bytes
        std::vector<unsigned char> vchData(520);
        for (unsigned int i = 0; i < 18; ++i) {
            tx.vin[0].scriptSig << vchData << OP_DROP;
        }
        tx.vin[0].scriptSig << OP_1;
        tx.vin[0].prevout.hash = txFirst[0]->GetHash();
        tx.vout[0].nValue = BLOCKSUBSIDY;
        for (unsigned int i = 0; i < MAX_LEGACY_BLOCK_SIZE / 9433 + 1; ++i) {
            tx.vout[0].nValue -= LOWFEE;
            hash = tx.GetHash();
            bool spendsCoinbase = i == 0; // only first tx spends coinbase
            tx_mempool.addUnchecked(entry.Fee(LOWFEE).Time(Now<NodeSeconds>()).SpendsCoinbase(spendsCoinbase).FromTx(tx));
            tx.vin[0].prevout.hash = hash;
        }
        BOOST_CHECK(AssemblerForTest(tx_mempool).CreateNewBlock(scriptPubKey));
    }

    {
        CTxMemPool& tx_mempool{MakeMempool()};
        LOCK(tx_mempool.cs);

        auto pblocktemplate = AssemblerForTest(tx_mempool).CreateNewBlock(scriptPubKey);
        BOOST_CHECK(pblocktemplate);

        // orphan in tx_mempool, template creation fails
        hash = tx.GetHash();
        tx_mempool.addUnchecked(entry.Fee(LOWFEE).Time(Now<NodeSeconds>()).FromTx(tx));
        // Age transaction for InstantSend
        m_node.clhandler->UpdateTxFirstSeenMap({hash}, pblocktemplate->block.nTime - WAIT_FOR_ISLOCK_TIMEOUT);
        BOOST_CHECK_EXCEPTION(AssemblerForTest(tx_mempool).CreateNewBlock(scriptPubKey), std::runtime_error, HasReason("bad-txns-inputs-missingorspent"));
    }

    {
        CTxMemPool& tx_mempool{MakeMempool()};
        LOCK(tx_mempool.cs);

        auto pblocktemplate = AssemblerForTest(tx_mempool).CreateNewBlock(scriptPubKey);
        BOOST_CHECK(pblocktemplate);

        // child with higher feerate than parent
        tx.vin[0].scriptSig = CScript() << OP_1;
        tx.vin[0].prevout.hash = txFirst[1]->GetHash();
        tx.vout[0].nValue = BLOCKSUBSIDY - HIGHFEE;
        hash = tx.GetHash();
        tx_mempool.addUnchecked(entry.Fee(HIGHFEE).Time(Now<NodeSeconds>()).SpendsCoinbase(true).FromTx(tx));
        tx.vin[0].prevout.hash = hash;
        tx.vin.resize(2);
        tx.vin[1].scriptSig = CScript() << OP_1;
        tx.vin[1].prevout.hash = txFirst[0]->GetHash();
        tx.vin[1].prevout.n = 0;
        tx.vout[0].nValue = tx.vout[0].nValue + BLOCKSUBSIDY - HIGHERFEE; // First txn output + fresh coinbase - new txn fee
        hash = tx.GetHash();
        tx_mempool.addUnchecked(entry.Fee(HIGHERFEE).Time(Now<NodeSeconds>()).SpendsCoinbase(true).FromTx(tx));
        BOOST_CHECK(AssemblerForTest(tx_mempool).CreateNewBlock(scriptPubKey));
    }

    {
        CTxMemPool& tx_mempool{MakeMempool()};
        LOCK(tx_mempool.cs);

        auto pblocktemplate = AssemblerForTest(tx_mempool).CreateNewBlock(scriptPubKey);
        BOOST_CHECK(pblocktemplate);

        // coinbase in tx_mempool, template creation fails
        tx.vin.resize(1);
        tx.vin[0].prevout.SetNull();
        tx.vin[0].scriptSig = CScript() << OP_0 << OP_1;
        tx.vout[0].nValue = 0;
        hash = tx.GetHash();
        // Age transaction for InstantSend
        m_node.clhandler->UpdateTxFirstSeenMap({hash}, pblocktemplate->block.nTime - WAIT_FOR_ISLOCK_TIMEOUT);
        // give it a fee so it'll get mined
        tx_mempool.addUnchecked(entry.Fee(LOWFEE).Time(Now<NodeSeconds>()).SpendsCoinbase(false).FromTx(tx));
        // Should throw bad-cb-multiple
        BOOST_CHECK_EXCEPTION(AssemblerForTest(tx_mempool).CreateNewBlock(scriptPubKey), std::runtime_error, HasReason("bad-cb-multiple"));
    }

    {
        CTxMemPool& tx_mempool{MakeMempool()};
        LOCK(tx_mempool.cs);

        int64_t mocked_time_for_is = m_node.chainman->ActiveChain().Tip()->GetMedianTimePast() - WAIT_FOR_ISLOCK_TIMEOUT;

        // double spend txn pair in tx_mempool, template creation fails
        tx.vin[0].prevout.hash = txFirst[0]->GetHash();
        tx.vin[0].scriptSig = CScript() << OP_1;
        tx.vout[0].nValue = BLOCKSUBSIDY - HIGHFEE;
        tx.vout[0].scriptPubKey = CScript() << OP_1;
        hash = tx.GetHash();
        tx_mempool.addUnchecked(entry.Fee(HIGHFEE).Time(Now<NodeSeconds>()).SpendsCoinbase(true).FromTx(tx));
        tx.vout[0].scriptPubKey = CScript() << OP_2;
        hash = tx.GetHash();
        // Age transaction for InstantSend
        m_node.clhandler->UpdateTxFirstSeenMap({hash}, mocked_time_for_is);
        tx_mempool.addUnchecked(entry.Fee(HIGHFEE).Time(Now<NodeSeconds>()).SpendsCoinbase(true).FromTx(tx));
        BOOST_CHECK_EXCEPTION(AssemblerForTest(tx_mempool).CreateNewBlock(scriptPubKey), std::runtime_error, HasReason("bad-txns-inputs-missingorspent"));
    }

    {
        CTxMemPool& tx_mempool{MakeMempool()};
        LOCK(tx_mempool.cs);

        int nHeight = m_node.chainman->ActiveChain().Height();
        constexpr bool is_subsidy_changing{false};
        if constexpr (is_subsidy_changing) {
        // subsidy changing
        // Create an actual 209999-long block chain (without valid blocks).
        while (m_node.chainman->ActiveChain().Tip()->nHeight < 209999) {
            CBlockIndex* prev = m_node.chainman->ActiveChain().Tip();
            CBlockIndex* next = new CBlockIndex();
            next->phashBlock = new uint256(InsecureRand256());
            m_node.chainman->ActiveChainstate().CoinsTip().SetBestBlock(next->GetBlockHash());
            next->pprev = prev;
            next->nHeight = prev->nHeight + 1;
            next->BuildSkip();
            m_node.chainman->ActiveChain().SetTip(*next);
        }
        BOOST_CHECK(AssemblerForTest(tx_mempool).CreateNewBlock(scriptPubKey));
        // Extend to a 210000-long block chain.
        while (m_node.chainman->ActiveChain().Tip()->nHeight < 210000) {
            CBlockIndex* prev = m_node.chainman->ActiveChain().Tip();
            CBlockIndex* next = new CBlockIndex();
            next->phashBlock = new uint256(InsecureRand256());
            m_node.chainman->ActiveChainstate().CoinsTip().SetBestBlock(next->GetBlockHash());
            next->pprev = prev;
            next->nHeight = prev->nHeight + 1;
            next->BuildSkip();
            m_node.chainman->ActiveChain().SetTip(*next);
        }
        BOOST_CHECK(AssemblerForTest(tx_mempool).CreateNewBlock(scriptPubKey));
        } // is_subsidy_changing

        auto pblocktemplate = AssemblerForTest(tx_mempool).CreateNewBlock(scriptPubKey);
        BOOST_CHECK(pblocktemplate);

        // invalid p2sh txn in tx_mempool, template creation fails
        tx.vin[0].prevout.hash = txFirst[0]->GetHash();
        tx.vin[0].prevout.n = 0;
        tx.vin[0].scriptSig = CScript() << OP_1;
        tx.vout[0].nValue = BLOCKSUBSIDY - LOWFEE;
        CScript script = CScript() << OP_0;
        tx.vout[0].scriptPubKey = GetScriptForDestination(ScriptHash(script));
        hash = tx.GetHash();
        // Age transaction for InstantSend
        m_node.clhandler->UpdateTxFirstSeenMap({hash}, pblocktemplate->block.nTime - WAIT_FOR_ISLOCK_TIMEOUT);
        tx_mempool.addUnchecked(entry.Fee(LOWFEE).Time(Now<NodeSeconds>()).SpendsCoinbase(true).FromTx(tx));
        tx.vin[0].prevout.hash = hash;
        tx.vin[0].scriptSig = CScript() << std::vector<unsigned char>(script.begin(), script.end());
        tx.vout[0].nValue -= LOWFEE;
        hash = tx.GetHash();
        // Age transaction for InstantSend
        m_node.clhandler->UpdateTxFirstSeenMap({hash}, pblocktemplate->block.nTime - WAIT_FOR_ISLOCK_TIMEOUT);
        tx_mempool.addUnchecked(entry.Fee(LOWFEE).Time(Now<NodeSeconds>()).SpendsCoinbase(false).FromTx(tx));
        // Should throw block-validation-failed
        BOOST_CHECK_EXCEPTION(AssemblerForTest(tx_mempool).CreateNewBlock(scriptPubKey), std::runtime_error, HasReason("block-validation-failed"));

        if constexpr (is_subsidy_changing) {
        // Delete the dummy blocks again.
        while (m_node.chainman->ActiveChain().Tip()->nHeight > nHeight) {
            CBlockIndex* del = m_node.chainman->ActiveChain().Tip();
            m_node.chainman->ActiveChain().SetTip(*Assert(del->pprev));
            m_node.chainman->ActiveChainstate().CoinsTip().SetBestBlock(del->pprev->GetBlockHash());
            delete del->phashBlock;
            delete del;
        }
        } // is_subsidy_changing
        // }
    }

    CTxMemPool& tx_mempool{MakeMempool()};
    LOCK(tx_mempool.cs);

    // non-final txs in mempool
    SetMockTime(m_node.chainman->ActiveChain().Tip()->GetMedianTimePast() + 1);
    int64_t mocked_time_for_is = m_node.chainman->ActiveChain().Tip()->GetMedianTimePast() - WAIT_FOR_ISLOCK_TIMEOUT;
    const int flags{LOCKTIME_VERIFY_SEQUENCE};
    // height map
    std::vector<int> prevheights;

    // relative height locked
    tx.nVersion = 2;
    tx.vin.resize(1);
    prevheights.resize(1);
    tx.vin[0].prevout.hash = txFirst[0]->GetHash(); // only 1 transaction
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vin[0].nSequence = m_node.chainman->ActiveChain().Tip()->nHeight - baseheight + 1;
    prevheights[0] = baseheight + 1;
    tx.vout.resize(1);
    tx.vout[0].nValue = BLOCKSUBSIDY-HIGHFEE;
    tx.vout[0].scriptPubKey = CScript() << OP_1;
    tx.nLockTime = 0;
    hash = tx.GetHash();
    // Age transaction for InstantSend
    m_node.clhandler->UpdateTxFirstSeenMap({hash}, mocked_time_for_is);
    tx_mempool.addUnchecked(entry.Fee(HIGHFEE).Time(Now<NodeSeconds>()).SpendsCoinbase(true).FromTx(tx));
    BOOST_CHECK(CheckFinalTxAtTip(*Assert(m_node.chainman->ActiveChain().Tip()), CTransaction{tx})); // Locktime passes
    BOOST_CHECK(!TestSequenceLocks(CTransaction{tx}, tx_mempool)); // Sequence locks fail

    {
        CBlockIndex* active_chain_tip = m_node.chainman->ActiveChain().Tip();
        BOOST_CHECK(SequenceLocks(CTransaction(tx), flags, prevheights, *CreateBlockIndex(active_chain_tip->nHeight + 2, active_chain_tip))); // Sequence locks pass on 2nd block
    }

    // relative time locked
    tx.vin[0].prevout.hash = txFirst[1]->GetHash();
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG | (((m_node.chainman->ActiveChain().Tip()->GetMedianTimePast()+1-m_node.chainman->ActiveChain()[baseheight + 1]->GetMedianTimePast()) >> CTxIn::SEQUENCE_LOCKTIME_GRANULARITY) + 1);
    prevheights[0] = baseheight + 2;
    hash = tx.GetHash();
    // Age transaction for InstantSend
    m_node.clhandler->UpdateTxFirstSeenMap({hash}, mocked_time_for_is);
    tx_mempool.addUnchecked(entry.Time(Now<NodeSeconds>()).FromTx(tx));
    BOOST_CHECK(CheckFinalTxAtTip(*Assert(m_node.chainman->ActiveChain().Tip()), CTransaction{tx})); // Locktime passes
    BOOST_CHECK(!TestSequenceLocks(CTransaction{tx}, tx_mempool)); // Sequence locks fail

    const int SEQUENCE_LOCK_TIME = 512; // Sequence locks pass 512 seconds later
    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; ++i)
        m_node.chainman->ActiveChain().Tip()->GetAncestor(m_node.chainman->ActiveChain().Tip()->nHeight - i)->nTime += SEQUENCE_LOCK_TIME; // Trick the MedianTimePast
    {
        CBlockIndex* active_chain_tip = m_node.chainman->ActiveChain().Tip();
        BOOST_CHECK(SequenceLocks(CTransaction(tx), flags, prevheights, *CreateBlockIndex(active_chain_tip->nHeight + 1, active_chain_tip)));
    }

    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; ++i) {
        CBlockIndex* ancestor{Assert(m_node.chainman->ActiveChain().Tip()->GetAncestor(m_node.chainman->ActiveChain().Tip()->nHeight - i))};
        ancestor->nTime -= SEQUENCE_LOCK_TIME; // undo tricked MTP
    }

    // absolute height locked
    tx.vin[0].prevout.hash = txFirst[2]->GetHash();
    tx.vin[0].nSequence = CTxIn::MAX_SEQUENCE_NONFINAL;
    prevheights[0] = baseheight + 3;
    tx.nLockTime = m_node.chainman->ActiveChain().Tip()->nHeight + 1;
    hash = tx.GetHash();
    // Age transaction for InstantSend
    m_node.clhandler->UpdateTxFirstSeenMap({hash}, mocked_time_for_is);
    tx_mempool.addUnchecked(entry.Time(Now<NodeSeconds>()).FromTx(tx));
    BOOST_CHECK(!CheckFinalTxAtTip(*Assert(m_node.chainman->ActiveChain().Tip()), CTransaction{tx})); // Locktime fails
    BOOST_CHECK(TestSequenceLocks(CTransaction{tx}, tx_mempool)); // Sequence locks pass
    BOOST_CHECK(IsFinalTx(CTransaction(tx), m_node.chainman->ActiveChain().Tip()->nHeight + 2, m_node.chainman->ActiveChain().Tip()->GetMedianTimePast())); // Locktime passes on 2nd block

    // absolute time locked
    tx.vin[0].prevout.hash = txFirst[3]->GetHash();
    tx.nLockTime = m_node.chainman->ActiveChain().Tip()->GetMedianTimePast();
    prevheights.resize(1);
    prevheights[0] = baseheight + 4;
    hash = tx.GetHash();
    // Age transaction for InstantSend
    m_node.clhandler->UpdateTxFirstSeenMap({hash}, mocked_time_for_is);
    tx_mempool.addUnchecked(entry.Time(Now<NodeSeconds>()).FromTx(tx));
    BOOST_CHECK(!CheckFinalTxAtTip(*Assert(m_node.chainman->ActiveChain().Tip()), CTransaction{tx})); // Locktime fails
    BOOST_CHECK(TestSequenceLocks(CTransaction{tx}, tx_mempool)); // Sequence locks pass
    BOOST_CHECK(IsFinalTx(CTransaction(tx), m_node.chainman->ActiveChain().Tip()->nHeight + 2, m_node.chainman->ActiveChain().Tip()->GetMedianTimePast() + 1)); // Locktime passes 1 second later

    // mempool-dependent transactions (not added)
    tx.vin[0].prevout.hash = hash;
    prevheights[0] = m_node.chainman->ActiveChain().Tip()->nHeight + 1;
    tx.nLockTime = 0;
    tx.vin[0].nSequence = 0;
    BOOST_CHECK(CheckFinalTxAtTip(*Assert(m_node.chainman->ActiveChain().Tip()), CTransaction{tx})); // Locktime passes
    BOOST_CHECK(TestSequenceLocks(CTransaction{tx}, tx_mempool)); // Sequence locks pass
    tx.vin[0].nSequence = 1;
    BOOST_CHECK(!TestSequenceLocks(CTransaction{tx}, tx_mempool)); // Sequence locks fail
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG;
    BOOST_CHECK(TestSequenceLocks(CTransaction{tx}, tx_mempool)); // Sequence locks pass
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG | 1;
    BOOST_CHECK(!TestSequenceLocks(CTransaction{tx}, tx_mempool)); // Sequence locks fail

    auto pblocktemplate = AssemblerForTest(tx_mempool).CreateNewBlock(scriptPubKey);
    BOOST_CHECK(pblocktemplate);

    // None of the of the absolute height/time locked tx should have made
    // it into the template because we still check IsFinalTx in CreateNewBlock,
    // but relative locked txs will if inconsistently added to mempool.
    // For now these will still generate a valid template until BIP68 soft fork
    BOOST_CHECK_EQUAL(pblocktemplate->block.vtx.size(), 3U);
    // However if we advance height by 1 and time by SEQUENCE_LOCK_TIME, all of them should be mined
    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; ++i) {
        CBlockIndex* ancestor{Assert(m_node.chainman->ActiveChain().Tip()->GetAncestor(m_node.chainman->ActiveChain().Tip()->nHeight - i))};
        ancestor->nTime += SEQUENCE_LOCK_TIME; // Trick the MedianTimePast
    }
    // Advance the template's tip without breaking the ancestry used by BIP34 checks.
    CChain& chain = m_node.chainman->ActiveChain();
    CCoinsViewCache& coins = m_node.chainman->ActiveChainstate().CoinsTip();
    CBlockIndex* const original_tip = chain.Tip();
    CBlockIndex next_tip{original_tip->GetBlockHeader()};
    next_tip.pprev = original_tip;
    next_tip.nHeight = original_tip->nHeight + 1;
    next_tip.BuildSkip();
    const uint256 next_hash = next_tip.GetBlockHeader().GetHash();
    next_tip.phashBlock = &next_hash;
    struct RestoreChainTip {
        CChain& chain;
        CCoinsViewCache& coins;
        CBlockIndex* tip;
        ~RestoreChainTip()
        {
            chain.SetTip(*tip);
            coins.SetBestBlock(tip->GetBlockHash());
        }
    } restore_tip{chain, coins, original_tip};
    chain.SetTip(next_tip);
    coins.SetBestBlock(next_hash);
    SetMockTime(chain.Tip()->GetMedianTimePast() + 1);

    BOOST_CHECK(pblocktemplate = AssemblerForTest(tx_mempool).CreateNewBlock(scriptPubKey));
    BOOST_CHECK_EQUAL(pblocktemplate->block.vtx.size(), 5U);
}

void MinerTestingSetup::TestPrioritisedMining(const CScript& scriptPubKey, const std::vector<CTransactionRef>& txFirst)
{
    CTxMemPool& tx_mempool{MakeMempool()};
    LOCK(tx_mempool.cs);

    TestMemPoolEntryHelper entry;

    // Test that a tx below min fee but prioritised is included
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vout.resize(1);
    tx.vout[0].nValue = txFirst[0]->vout[0].nValue; // 0 fee
    uint256 hashFreePrioritisedTx = tx.GetHash();
    tx_mempool.addUnchecked(entry.Fee(0).Time(Now<NodeSeconds>()).SpendsCoinbase(true).FromTx(tx));
    tx_mempool.PrioritiseTransaction(hashFreePrioritisedTx, 5 * COIN);

    tx.vin[0].prevout.hash = txFirst[1]->GetHash();
    tx.vin[0].prevout.n = 0;
    tx.vout[0].nValue = txFirst[1]->vout[0].nValue - 1000;
    // This tx has a low fee: 1000 satoshis
    uint256 hashParentTx = tx.GetHash(); // save this txid for later use
    tx_mempool.addUnchecked(entry.Fee(1000).Time(Now<NodeSeconds>()).SpendsCoinbase(true).FromTx(tx));

    // This tx has a medium fee: 10000 satoshis
    tx.vin[0].prevout.hash = txFirst[2]->GetHash();
    tx.vout[0].nValue = txFirst[2]->vout[0].nValue - 10000;
    uint256 hashMediumFeeTx = tx.GetHash();
    tx_mempool.addUnchecked(entry.Fee(10000).Time(Now<NodeSeconds>()).SpendsCoinbase(true).FromTx(tx));
    tx_mempool.PrioritiseTransaction(hashMediumFeeTx, -5 * COIN);

    // This tx also has a low fee, but is prioritised
    tx.vin[0].prevout.hash = hashParentTx;
    tx.vout[0].nValue = txFirst[1]->vout[0].nValue - 1000 - 1000; // 1000 satoshi fee
    uint256 hashPrioritsedChild = tx.GetHash();
    tx_mempool.addUnchecked(entry.Fee(1000).Time(Now<NodeSeconds>()).SpendsCoinbase(false).FromTx(tx));
    tx_mempool.PrioritiseTransaction(hashPrioritsedChild, 2 * COIN);

    // Test that transaction selection properly updates ancestor fee calculations as prioritised
    // parents get included in a block. Create a transaction with two prioritised ancestors, each
    // included by itself: FreeParent <- FreeChild <- FreeGrandchild.
    // When FreeParent is added, a modified entry will be created for FreeChild + FreeGrandchild
    // FreeParent's prioritisation should not be included in that entry.
    // When FreeChild is included, FreeChild's prioritisation should also not be included.
    tx.vin[0].prevout.hash = txFirst[3]->GetHash();
    tx.vout[0].nValue = txFirst[3]->vout[0].nValue; // 0 fee
    uint256 hashFreeParent = tx.GetHash();
    tx_mempool.addUnchecked(entry.Fee(0).SpendsCoinbase(true).FromTx(tx));
    tx_mempool.PrioritiseTransaction(hashFreeParent, 10 * COIN);

    tx.vin[0].prevout.hash = hashFreeParent;
    tx.vout[0].nValue = txFirst[3]->vout[0].nValue; // 0 fee
    uint256 hashFreeChild = tx.GetHash();
    tx_mempool.addUnchecked(entry.Fee(0).SpendsCoinbase(false).FromTx(tx));
    tx_mempool.PrioritiseTransaction(hashFreeChild, 1 * COIN);

    tx.vin[0].prevout.hash = hashFreeChild;
    tx.vout[0].nValue = txFirst[3]->vout[0].nValue; // 0 fee
    uint256 hashFreeGrandchild = tx.GetHash();
    tx_mempool.addUnchecked(entry.Fee(0).SpendsCoinbase(false).FromTx(tx));

    // Age transaction for InstantSend
    m_node.clhandler->UpdateTxFirstSeenMap({hashMediumFeeTx, hashPrioritsedChild, hashParentTx, hashFreeParent, hashFreeChild, hashFreeGrandchild, hashFreePrioritisedTx}, std::chrono::duration_cast<std::chrono::seconds>(Now<NodeSeconds>().time_since_epoch()).count() - WAIT_FOR_ISLOCK_TIMEOUT);
    auto pblocktemplate = AssemblerForTest(tx_mempool).CreateNewBlock(scriptPubKey);
    BOOST_REQUIRE_EQUAL(pblocktemplate->block.vtx.size(), 6U);
    BOOST_CHECK(pblocktemplate->block.vtx[1]->GetHash() == hashFreeParent);
    BOOST_CHECK(pblocktemplate->block.vtx[2]->GetHash() == hashFreePrioritisedTx);
    BOOST_CHECK(pblocktemplate->block.vtx[3]->GetHash() == hashParentTx);
    BOOST_CHECK(pblocktemplate->block.vtx[4]->GetHash() == hashPrioritsedChild);
    BOOST_CHECK(pblocktemplate->block.vtx[5]->GetHash() == hashFreeChild);
    for (size_t i=0; i<pblocktemplate->block.vtx.size(); ++i) {
        // The FreeParent and FreeChild's prioritisations should not impact the child.
        BOOST_CHECK(pblocktemplate->block.vtx[i]->GetHash() != hashFreeGrandchild);
        // De-prioritised transaction should not be included.
        BOOST_CHECK(pblocktemplate->block.vtx[i]->GetHash() != hashMediumFeeTx);
    }
}

// NOTE: These tests rely on CreateNewBlock doing its own self-validation!
BOOST_AUTO_TEST_CASE(CreateNewBlock_validity)
{
    // Note that by default, these tests run with size accounting enabled.
    CScript scriptPubKey = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
    std::unique_ptr<CBlockTemplate> pblocktemplate;

    CTxMemPool& tx_mempool{*m_node.mempool};
    // Simple block creation, nothing special yet:
    BOOST_CHECK(pblocktemplate = AssemblerForTest(tx_mempool).CreateNewBlock(scriptPubKey));

    // We can't make transactions until we have inputs
    // Pre-mine enough blocks to mature all four full-reward coinbases.
    int baseheight = 0;
    std::vector<CTransactionRef> txFirst;
    for (const auto& bi : BLOCKINFO) {
        CBlock *pblock = &pblocktemplate->block; // pointer for convenience
        {
            LOCK(cs_main);
            pblock->nVersion = VERSIONBITS_TOP_BITS;
            pblock->nTime = m_node.chainman->ActiveChain().Tip()->GetMedianTimePast()+1;
            CMutableTransaction txCoinbase(*pblock->vtx[0]);
            txCoinbase.nVersion = 1;
            txCoinbase.vin[0].scriptSig = CScript{} << (m_node.chainman->ActiveChain().Height() + 1) << bi.extranonce;
            txCoinbase.vout[0].scriptPubKey = CScript();
            const CBlockIndex* tip = m_node.chainman->ActiveChain().Tip();
            txCoinbase.vout[0].nValue = GetBlockSubsidyInner(tip->nBits, tip->nHeight, Params().GetConsensus(), /*fV20Active=*/false);
            pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));
            // Use consecutive full-reward coinbases after regtest's 150-corsar block.
            if (tip->nHeight > Params().GetConsensus().nRestoreRewardV18 && txFirst.size() < 4) {
                if (txFirst.empty()) baseheight = tip->nHeight;
                txFirst.push_back(pblock->vtx[0]);
            }
            pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
            pblock->nNonce = bi.nonce;

            // This will usually succeed in the first round as we take the nonce from BLOCKINFO
            // It's however useful when adding new blocks with unknown nonces (you should add the found block to BLOCKINFO)
            while (!CheckProofOfWork(pblock->GetPoWHash(), pblock->nBits, Assert(m_node.chainman)->GetParams().GetConsensus())) {
                pblock->nNonce++;
            }
        }
        std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
        BOOST_CHECK(Assert(m_node.chainman)->ProcessNewBlock(shared_pblock, true, nullptr));
        pblock->hashPrevBlock = pblock->GetHash();
    }

    LOCK(cs_main);

    TestBasicMining(scriptPubKey, txFirst, baseheight);

    SetMockTime(0);

    TestPackageSelection(scriptPubKey, txFirst);

    SetMockTime(0);

    TestPrioritisedMining(scriptPubKey, txFirst);
}

BOOST_AUTO_TEST_SUITE_END()
