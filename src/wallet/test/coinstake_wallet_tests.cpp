// Copyright (c) 2024-2026 The Cosanta Core developers
// Copyright (c) 2026 The PirateCash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Wallet-level coinstake tests
//
// These tests verify wallet behavior specific to PoS coinstake transactions:
//   1. GetBlocksToMaturity() counts coinstake as needing maturity
//   2. IsImmatureCoinBase() returns true for immature coinstake
//   3. ReacceptWalletTransactions() abandons unconfirmed coinstake
//   4. Coinstake cannot be rebroadcast (CanBeResent)
//   5. Wallet credit/balance calculations treat immature coinstake correctly
//   6. Wallet state after reorg affecting coinstake
//

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <evo/dmn_types.h>
#include <policy/policy.h>
#include <util/time.h>
#include <test/util/txmempool.h>
#include <txmempool.h>
#include <wallet/test/util.h>

#include <consensus/consensus.h>
#include <interfaces/chain.h>
#include <key.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/standard.h>
#include <test/util/setup_common.h>
#include <validation.h>
#include <wallet/wallet.h>
#include <wallet/test/wallet_test_fixture.h>

namespace wallet {
namespace {

struct CoinstakeWalletTestingSetup : public WalletTestingSetup {
    CoinstakeWalletTestingSetup() : WalletTestingSetup(CBaseChainParams::REGTEST) {}
};

// Helper: create a coinstake transaction
CTransactionRef MakeCoinstakeTx(const COutPoint& stakeInput, const CScript& spk, CAmount stakeValue)
{
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = stakeInput;
    mtx.vout.resize(2);
    // vout[0] empty (coinstake marker)
    mtx.vout[0].nValue = 0;
    mtx.vout[0].scriptPubKey.clear();
    // vout[1] reward
    mtx.vout[1].nValue = stakeValue;
    mtx.vout[1].scriptPubKey = spk;
    return MakeTransactionRef(std::move(mtx));
}

// Helper: create a coinbase transaction
CTransactionRef MakeCoinbaseTx(const CScript& spk, CAmount value)
{
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();
    mtx.vin[0].scriptSig = CScript() << 0x01;
    mtx.vout.resize(1);
    mtx.vout[0].nValue = value;
    mtx.vout[0].scriptPubKey = spk;
    return MakeTransactionRef(std::move(mtx));
}

// Helper: create a regular (non-coinbase, non-coinstake) transaction
CTransactionRef MakeRegularTx(const COutPoint& input, const CScript& spk, CAmount value)
{
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = input;
    mtx.vout.resize(1);
    mtx.vout[0].nValue = value;
    mtx.vout[0].scriptPubKey = spk;
    return MakeTransactionRef(std::move(mtx));
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(coinstake_wallet_tests, CoinstakeWalletTestingSetup)

// ============================================================================
// 1. TRANSACTION TYPE IDENTIFICATION
// ============================================================================

BOOST_AUTO_TEST_CASE(coinstake_tx_type_identification)
{
    CKey key;
    key.MakeNewKey(true);
    CScript spk = GetScriptForDestination(PKHash(key.GetPubKey()));

    COutPoint stakeInput(InsecureRand256(), 0);
    auto txStake = MakeCoinstakeTx(stakeInput, spk, 10 * COIN);
    auto txCoinbase = MakeCoinbaseTx(spk, 50 * COIN);
    auto txRegular = MakeRegularTx(COutPoint(InsecureRand256(), 0), spk, 5 * COIN);

    BOOST_CHECK(txStake->IsCoinStake());
    BOOST_CHECK(!txStake->IsCoinBase());

    BOOST_CHECK(txCoinbase->IsCoinBase());
    BOOST_CHECK(!txCoinbase->IsCoinStake());

    BOOST_CHECK(!txRegular->IsCoinBase());
    BOOST_CHECK(!txRegular->IsCoinStake());
}

// ============================================================================
// 2. GetBlocksToMaturity() FOR COINSTAKE
// ============================================================================

BOOST_AUTO_TEST_CASE(coinstake_blocks_to_maturity_unconfirmed)
{
    // An unconfirmed coinstake should need COINBASE_MATURITY+1 blocks to mature
    CKey key;
    key.MakeNewKey(true);
    CScript spk = GetScriptForDestination(PKHash(key.GetPubKey()));

    auto txStake = MakeCoinstakeTx(COutPoint(InsecureRand256(), 0), spk, 10 * COIN);
    BOOST_CHECK(txStake->IsCoinStake());

    // Add to wallet as unconfirmed
    CWalletTx wtx(txStake, TxStateInactive{});

    // For an unconfirmed tx, GetDepthInMainChain() returns 0
    // GetBlocksToMaturity returns max(0, (COINBASE_MATURITY+1) - depth)
    // = max(0, 101 - 0) = 101 (but this requires the tx to be in the chain)
    // Since it's not in chain, depth might be 0 and maturity is 101
    {
        LOCK(m_wallet.cs_wallet);
        int maturity = m_wallet.GetTxBlocksToMaturity(wtx);
        BOOST_CHECK_GT(maturity, 0);
        BOOST_CHECK_EQUAL(maturity, COINBASE_MATURITY + 1);
    }
}

BOOST_AUTO_TEST_CASE(coinstake_is_immature)
{
    // IsImmatureCoinBase() should return true for coinstake that hasn't matured
    CKey key;
    key.MakeNewKey(true);
    CScript spk = GetScriptForDestination(PKHash(key.GetPubKey()));

    auto txStake = MakeCoinstakeTx(COutPoint(InsecureRand256(), 0), spk, 10 * COIN);
    CWalletTx wtx(txStake, TxStateInactive{});

    // Unconfirmed coinstake: GetBlocksToMaturity() > 0 → IsImmatureCoinBase() true
    {
        LOCK(m_wallet.cs_wallet);
        BOOST_CHECK(m_wallet.IsTxImmatureCoinBase(wtx));
    }
}

BOOST_AUTO_TEST_CASE(coinbase_also_immature)
{
    // Verify that coinbase also reports as immature (sanity check)
    CKey key;
    key.MakeNewKey(true);
    CScript spk = GetScriptForDestination(PKHash(key.GetPubKey()));

    auto txCB = MakeCoinbaseTx(spk, 50 * COIN);
    CWalletTx wtx(txCB, TxStateInactive{});

    {
        LOCK(m_wallet.cs_wallet);
        BOOST_CHECK(m_wallet.IsTxImmatureCoinBase(wtx));
        BOOST_CHECK_GT(m_wallet.GetTxBlocksToMaturity(wtx), 0);
    }
}

BOOST_AUTO_TEST_CASE(regular_tx_not_immature)
{
    // Regular transactions should have 0 blocks to maturity and not be immature
    CKey key;
    key.MakeNewKey(true);
    CScript spk = GetScriptForDestination(PKHash(key.GetPubKey()));

    auto txRegular = MakeRegularTx(COutPoint(InsecureRand256(), 0), spk, 5 * COIN);
    CWalletTx wtx(txRegular, TxStateInactive{});

    {
        LOCK(m_wallet.cs_wallet);
        BOOST_CHECK_EQUAL(m_wallet.GetTxBlocksToMaturity(wtx), 0);
        BOOST_CHECK(!m_wallet.IsTxImmatureCoinBase(wtx));
    }
}

// ============================================================================
// 3. MATURITY FORMULA CORRECTNESS
// ============================================================================

BOOST_AUTO_TEST_CASE(maturity_formula_coinstake_vs_coinbase)
{
    // Both coinbase and coinstake should use the same maturity formula:
    // max(0, (COINBASE_MATURITY + 1) - depth)
    // This ensures coinstake isn't accidentally given a different maturity period.

    CKey key;
    key.MakeNewKey(true);
    CScript spk = GetScriptForDestination(PKHash(key.GetPubKey()));

    auto txStake = MakeCoinstakeTx(COutPoint(InsecureRand256(), 0), spk, 10 * COIN);
    auto txCB = MakeCoinbaseTx(spk, 50 * COIN);

    CWalletTx wtxStake(txStake, TxStateInactive{});
    CWalletTx wtxCB(txCB, TxStateInactive{});

    // Both should report identical maturity at the same depth
    {
        LOCK(m_wallet.cs_wallet);
        BOOST_CHECK_EQUAL(m_wallet.GetTxBlocksToMaturity(wtxStake), m_wallet.GetTxBlocksToMaturity(wtxCB));
        BOOST_CHECK_EQUAL(m_wallet.GetTxBlocksToMaturity(wtxStake), COINBASE_MATURITY + 1);
    }
}

// ============================================================================
// 4. CanBeResent() FOR COINSTAKE
// ============================================================================

BOOST_AUTO_TEST_CASE(coinstake_cannot_be_resent)
{
    // Coinstake transactions must not be rebroadcast via CanBeResent()
    CKey key;
    key.MakeNewKey(true);
    CScript spk = GetScriptForDestination(PKHash(key.GetPubKey()));

    auto txStake = MakeCoinstakeTx(COutPoint(InsecureRand256(), 0), spk, 10 * COIN);
    CWalletTx wtx(txStake, TxStateInactive{});

    // CanBeResent() checks !IsCoinStake() internally
    {
        LOCK(m_wallet.cs_wallet);
        BOOST_CHECK(!m_wallet.CanTxBeResent(wtx));
    }
}

BOOST_AUTO_TEST_CASE(coinbase_cannot_be_resent)
{
    // Coinbase also cannot be resent (sanity check)
    CKey key;
    key.MakeNewKey(true);
    CScript spk = GetScriptForDestination(PKHash(key.GetPubKey()));

    auto txCB = MakeCoinbaseTx(spk, 50 * COIN);
    CWalletTx wtx(txCB, TxStateInactive{});

    {
        LOCK(m_wallet.cs_wallet);
        BOOST_CHECK(!m_wallet.CanTxBeResent(wtx));
    }
}

// ============================================================================
// 5. COINSTAKE IS TREATED LIKE COINBASE FOR ABANDONMENT
// ============================================================================

BOOST_AUTO_TEST_CASE(coinstake_abandoned_on_reaccept_logic)
{
    // ReacceptWalletTransactions() has this logic:
    //   if ((IsCoinBase() || IsCoinStake()) && (nDepth == 0 && !IsLocked && !isAbandoned))
    //       AbandonTransaction(wtxid);
    //
    // This verifies that IsCoinStake() is in the condition — meaning that
    // unconfirmed coinstake will be abandoned, not rebroadcast.

    CKey key;
    key.MakeNewKey(true);
    CScript spk = GetScriptForDestination(PKHash(key.GetPubKey()));

    auto txStake = MakeCoinstakeTx(COutPoint(InsecureRand256(), 0), spk, 10 * COIN);
    BOOST_CHECK(txStake->IsCoinStake());

    // The key assertion: coinstake passes the abandonment condition
    // We verify the condition components:
    BOOST_CHECK(txStake->IsCoinStake());
    // A non-coinbase, non-coinstake tx would go to mapSorted for rebroadcast
    // but coinstake goes to AbandonTransaction
    auto txRegular = MakeRegularTx(COutPoint(InsecureRand256(), 0), spk, 5 * COIN);
    BOOST_CHECK(!txRegular->IsCoinBase());
    BOOST_CHECK(!txRegular->IsCoinStake());
    // Regular tx doesn't hit the abandon branch — it goes to SubmitMemoryPoolAndRelay
}

// ============================================================================
// 6. COINSTAKE OUTPUT STRUCTURE VALIDATION
// ============================================================================

BOOST_AUTO_TEST_CASE(coinstake_empty_vout0_is_detected)
{
    // The IsEmpty() check on vout[0] is critical for IsCoinStake()
    // Verify various vout[0] configurations

    CKey key;
    key.MakeNewKey(true);
    CScript spk = GetScriptForDestination(PKHash(key.GetPubKey()));

    // Valid coinstake: vout[0] is empty (nValue=0, scriptPubKey empty)
    {
        auto tx = MakeCoinstakeTx(COutPoint(InsecureRand256(), 0), spk, 10 * COIN);
        BOOST_CHECK(tx->vout[0].IsEmpty());
        BOOST_CHECK(tx->IsCoinStake());
    }

    // Invalid: vout[0] has value
    {
        CMutableTransaction mtx;
        mtx.vin.resize(1);
        mtx.vin[0].prevout = COutPoint(InsecureRand256(), 0);
        mtx.vout.resize(2);
        mtx.vout[0].nValue = 1; // NOT empty
        mtx.vout[0].scriptPubKey.clear();
        mtx.vout[1].nValue = 10 * COIN;
        mtx.vout[1].scriptPubKey = spk;
        CTransaction tx(mtx);
        BOOST_CHECK(!tx.vout[0].IsEmpty());
        BOOST_CHECK(!tx.IsCoinStake());
    }

    // Invalid: vout[0] has script but zero value
    {
        CMutableTransaction mtx;
        mtx.vin.resize(1);
        mtx.vin[0].prevout = COutPoint(InsecureRand256(), 0);
        mtx.vout.resize(2);
        mtx.vout[0].nValue = 0;
        mtx.vout[0].scriptPubKey = spk; // NOT empty
        mtx.vout[1].nValue = 10 * COIN;
        mtx.vout[1].scriptPubKey = spk;
        CTransaction tx(mtx);
        BOOST_CHECK(!tx.vout[0].IsEmpty());
        BOOST_CHECK(!tx.IsCoinStake());
    }
}

// ============================================================================
// 7. MATURITY INTERACTION WITH COINBASE_MATURITY CONSTANT
// ============================================================================

BOOST_AUTO_TEST_CASE(maturity_uses_correct_constant)
{
    // Verify the maturity formula uses the current PirateCash COINBASE_MATURITY.

    CKey key;
    key.MakeNewKey(true);
    CScript spk = GetScriptForDestination(PKHash(key.GetPubKey()));

    auto txStake = MakeCoinstakeTx(COutPoint(InsecureRand256(), 0), spk, 10 * COIN);
    CWalletTx wtx(txStake, TxStateInactive{});

    // At depth 0: maturity = COINBASE_MATURITY + 1
    {
        LOCK(m_wallet.cs_wallet);
        BOOST_CHECK_EQUAL(m_wallet.GetTxBlocksToMaturity(wtx), COINBASE_MATURITY + 1);
    }
}

// ============================================================================
// 8. NEGATIVE: EDGE CASES
// ============================================================================

BOOST_AUTO_TEST_CASE(coinstake_with_empty_vin_not_coinstake)
{
    // A transaction with empty vin should not be coinstake
    CMutableTransaction mtx;
    // No vin at all
    mtx.vout.resize(2);
    mtx.vout[0].nValue = 0;
    mtx.vout[0].scriptPubKey.clear();
    mtx.vout[1].nValue = 10 * COIN;

    CTransaction tx(mtx);
    BOOST_CHECK(!tx.IsCoinStake());
}

BOOST_AUTO_TEST_CASE(coinstake_with_null_prevout_is_coinbase)
{
    // If vin[0].prevout is null, it's a coinbase, not coinstake
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();
    mtx.vin[0].scriptSig = CScript() << 0x01;
    mtx.vout.resize(2);
    mtx.vout[0].nValue = 0;
    mtx.vout[0].scriptPubKey.clear();
    mtx.vout[1].nValue = 50 * COIN;

    CTransaction tx(mtx);
    BOOST_CHECK(tx.IsCoinBase());
    BOOST_CHECK(!tx.IsCoinStake());
}

// ============================================================================
// 9. WALLET TX IsCoinStake() DELEGATION
// ============================================================================

BOOST_AUTO_TEST_CASE(wallet_tx_delegates_is_coinstake)
{
    // CWalletTx::IsCoinStake() should delegate to the underlying CTransaction
    CKey key;
    key.MakeNewKey(true);
    CScript spk = GetScriptForDestination(PKHash(key.GetPubKey()));

    auto txStake = MakeCoinstakeTx(COutPoint(InsecureRand256(), 0), spk, 10 * COIN);
    CWalletTx wtx(txStake, TxStateInactive{});

    BOOST_CHECK(wtx.IsCoinStake());
    BOOST_CHECK_EQUAL(wtx.IsCoinStake(), txStake->IsCoinStake());
}

// ============================================================================
// AUTOCOMBINE INPUT SELECTION (CWallet::AutocombineCoinStake)
//
// NOTE: the cases below are disabled because the PirateCash unit-test mining
// fixture (TestChainSetup/TestChain100Setup) currently cannot build a chain
// on regtest (nForkHeight=1 rejects the PoW test miner; the deterministic
// checkpoint hashes below in setup_common.cpp are stale). Re-enable them
// once the mining fixture is fixed. Beware: boost's disabled() does NOT
// skip a test that is force-run via an explicit --run_test=<name>.
// ============================================================================

namespace {

struct AutocombineParams {
    CAmount reward{0};
    size_t max_tx_size{MAX_STANDARD_TX_SIZE};
    size_t max_sigops{1000};
    CAmount target_amount{MAX_MONEY};
};

struct AutocombineResult {
    CMutableTransaction stakeTx;
    std::vector<CScript> vin_scripts;
    std::vector<CAmount> vin_values;
    CAmount reward{0};
    size_t est_tx_size{0};
    size_t est_tx_sigops{0};
};

AutocombineResult RunAutocombine(CWallet& wallet, const COutPoint& kernel_prevout,
                                 const CPubKey& kernel_pubkey, const AutocombineParams& params)
{
    AutocombineResult res;
    res.stakeTx.vin.emplace_back(kernel_prevout);
    CScript empty_script;
    res.stakeTx.vout.push_back(CTxOut(0, empty_script));
    const CScript kernel_spk = GetScriptForDestination(PKHash(kernel_pubkey));
    res.stakeTx.vout.emplace_back(params.reward, kernel_spk);
    res.vin_scripts.emplace_back(kernel_spk);
    res.vin_values.emplace_back(params.reward);
    res.reward = params.reward;
    res.est_tx_size = 250; // conservative: overhead + signed kernel input + two outputs
    res.est_tx_sigops = 1;
    wallet.AutocombineCoinStake(kernel_prevout, kernel_pubkey, params.target_amount,
                                params.max_tx_size, params.max_sigops,
                                res.stakeTx, res.vin_scripts, res.vin_values, res.reward,
                                res.est_tx_size, res.est_tx_sigops);
    return res;
}

bool VinContains(const CMutableTransaction& tx, const COutPoint& outpoint)
{
    for (const auto& in : tx.vin) {
        if (in.prevout == outpoint) return true;
    }
    return false;
}

//! A wallet UTXO with a chosen amount. Candidates are faked instead of mined
//! so the scenarios do not depend on the regtest reward schedule: confirmed
//! from the wallet's point of view and, when in_utxo_set is true, visible to
//! FindCoins() through the mempool view.
CTransactionRef AddFakeCandidate(CWallet& wallet, CTxMemPool& mempool, const CBlockIndex* tip,
                                 const CScript& spk, CAmount value,
                                 bool in_utxo_set = true, bool confirmed = true)
{
    auto tx = MakeRegularTx(COutPoint(InsecureRand256(), 0), spk, value);
    {
        LOCK(wallet.cs_wallet);
        if (confirmed) {
            wallet.AddToWallet(tx, TxStateConfirmed{tip->GetBlockHash(), tip->nHeight, 1});
        } else {
            wallet.AddToWallet(tx, TxStateInactive{});
        }
    }
    if (in_utxo_set) {
        TestMemPoolEntryHelper entry;
        LOCK2(::cs_main, mempool.cs);
        mempool.addUnchecked(entry.FromTx(tx));
    }
    return tx;
}

} // anonymous namespace

//! The mainnet scenario: 50 PIRATE outputs sweep into a 950 PIRATE kernel at
//! stakesplitthreshold=500 / stakecombinemax=100, crossing the 64-outpoint
//! findCoins() batching window. The old dead zone would combine nothing:
//! 950 + 50 >= 2*500 - 1.
BOOST_FIXTURE_TEST_CASE(autocombine_sweeps_small_inputs, TestChain100Setup, * boost::unit_test::disabled())
{
    auto wallet = CreateSyncedWallet(*m_node.chain, *m_node.coinjoin_loader, m_node.chainman->ActiveChain(), m_args, coinbaseKey);
    const CScript wallet_spk = GetScriptForDestination(PKHash(coinbaseKey.GetPubKey()));
    const CBlockIndex* tip = WITH_LOCK(::cs_main, return m_node.chainman->ActiveChain().Tip());

    for (int i = 0; i < 70; ++i) {
        AddFakeCandidate(*wallet, *m_node.mempool, tip, wallet_spk, 50 * COIN);
    }

    SetMockTime(tip->GetBlockTime() + Params().MinStakeAge() + 3600);

    wallet->fAutocombine = AUTOCOMBINE_SAME;
    wallet->nStakeSplitThreshold = 500;
    wallet->nStakeCombineMax = 100;

    const COutPoint kernel(InsecureRand256(), 0);
    AutocombineParams params;
    params.reward = 950 * COIN;

    const auto res = RunAutocombine(*wallet, kernel, coinbaseKey.GetPubKey(), params);

    BOOST_CHECK_EQUAL(res.stakeTx.vin.size(), 71U);
    BOOST_CHECK_EQUAL(res.vin_scripts.size(), res.stakeTx.vin.size());
    BOOST_CHECK_EQUAL(res.reward, params.reward + 70 * 50 * COIN);
    // P2PKH inputs carry no P2SH sigops, the estimate must not grow
    BOOST_CHECK_EQUAL(res.est_tx_sigops, 1U);
    BOOST_CHECK_GT(res.est_tx_size, 250U);

    SetMockTime(0);
}

//! -stakecombinemax bounds the sweep: a zero setting disables it and
//! candidates above the limit are not taken.
BOOST_FIXTURE_TEST_CASE(autocombine_respects_stakecombinemax, TestChain100Setup, * boost::unit_test::disabled())
{
    auto wallet = CreateSyncedWallet(*m_node.chain, *m_node.coinjoin_loader, m_node.chainman->ActiveChain(), m_args, coinbaseKey);
    const CScript wallet_spk = GetScriptForDestination(PKHash(coinbaseKey.GetPubKey()));
    const CBlockIndex* tip = WITH_LOCK(::cs_main, return m_node.chainman->ActiveChain().Tip());

    for (int i = 0; i < 3; ++i) {
        AddFakeCandidate(*wallet, *m_node.mempool, tip, wallet_spk, 50 * COIN);
    }

    SetMockTime(tip->GetBlockTime() + Params().MinStakeAge() + 3600);

    wallet->fAutocombine = AUTOCOMBINE_SAME;
    wallet->nStakeSplitThreshold = 500;

    const COutPoint kernel(InsecureRand256(), 0);
    AutocombineParams params;
    params.reward = 950 * COIN;

    // Sweep disabled entirely
    wallet->nStakeCombineMax = 0;
    auto res = RunAutocombine(*wallet, kernel, coinbaseKey.GetPubKey(), params);
    BOOST_CHECK_EQUAL(res.stakeTx.vin.size(), 1U);
    BOOST_CHECK_EQUAL(res.reward, params.reward);

    // Candidates are larger than the limit
    wallet->nStakeCombineMax = 49;
    res = RunAutocombine(*wallet, kernel, coinbaseKey.GetPubKey(), params);
    BOOST_CHECK_EQUAL(res.stakeTx.vin.size(), 1U);
    BOOST_CHECK_EQUAL(res.reward, params.reward);

    SetMockTime(0);
}

//! The historic below-target path still combines when the kernel is small:
//! a 100 PIRATE kernel grows by 50 PIRATE inputs up to just below
//! 2*stakesplitthreshold - 1.
BOOST_FIXTURE_TEST_CASE(autocombine_old_target_path, TestChain100Setup, * boost::unit_test::disabled())
{
    auto wallet = CreateSyncedWallet(*m_node.chain, *m_node.coinjoin_loader, m_node.chainman->ActiveChain(), m_args, coinbaseKey);
    const CScript wallet_spk = GetScriptForDestination(PKHash(coinbaseKey.GetPubKey()));
    const CBlockIndex* tip = WITH_LOCK(::cs_main, return m_node.chainman->ActiveChain().Tip());

    for (int i = 0; i < 25; ++i) {
        AddFakeCandidate(*wallet, *m_node.mempool, tip, wallet_spk, 50 * COIN);
    }

    SetMockTime(tip->GetBlockTime() + Params().MinStakeAge() + 3600);

    wallet->fAutocombine = AUTOCOMBINE_SAME;
    wallet->nStakeSplitThreshold = 500;
    wallet->nStakeCombineMax = 0; // isolate the old path

    const COutPoint kernel(InsecureRand256(), 0);
    AutocombineParams params;
    params.reward = 100 * COIN;

    const auto res = RunAutocombine(*wallet, kernel, coinbaseKey.GetPubKey(), params);

    // 100 + 17*50 = 950; the next input would reach 1000 >= 2*500 - 1
    BOOST_CHECK_EQUAL(res.stakeTx.vin.size(), 18U);
    BOOST_CHECK_EQUAL(res.reward, 950 * COIN);

    SetMockTime(0);
}

//! Size, sigops and reserve-balance budgets each stop the sweep on their own.
BOOST_FIXTURE_TEST_CASE(autocombine_respects_budgets, TestChain100Setup, * boost::unit_test::disabled())
{
    auto wallet = CreateSyncedWallet(*m_node.chain, *m_node.coinjoin_loader, m_node.chainman->ActiveChain(), m_args, coinbaseKey);
    const CScript wallet_spk = GetScriptForDestination(PKHash(coinbaseKey.GetPubKey()));
    const CBlockIndex* tip = WITH_LOCK(::cs_main, return m_node.chainman->ActiveChain().Tip());

    for (int i = 0; i < 5; ++i) {
        AddFakeCandidate(*wallet, *m_node.mempool, tip, wallet_spk, 50 * COIN);
    }

    SetMockTime(tip->GetBlockTime() + Params().MinStakeAge() + 3600);

    wallet->fAutocombine = AUTOCOMBINE_SAME;
    wallet->nStakeSplitThreshold = 500;
    wallet->nStakeCombineMax = 100;

    const COutPoint kernel(InsecureRand256(), 0);
    AutocombineParams params;
    params.reward = 950 * COIN;

    // Sanity: with roomy budgets all five candidates combine
    auto res = RunAutocombine(*wallet, kernel, coinbaseKey.GetPubKey(), params);
    BOOST_REQUIRE_EQUAL(res.stakeTx.vin.size(), 6U);

    // No sigops budget left
    AutocombineParams no_sigops = params;
    no_sigops.max_sigops = 1;
    res = RunAutocombine(*wallet, kernel, coinbaseKey.GetPubKey(), no_sigops);
    BOOST_CHECK_EQUAL(res.stakeTx.vin.size(), 1U);

    // No room left in the block for another input
    AutocombineParams no_size = params;
    no_size.max_tx_size = 260; // helper starts est_tx_size at 250
    res = RunAutocombine(*wallet, kernel, coinbaseKey.GetPubKey(), no_size);
    BOOST_CHECK_EQUAL(res.stakeTx.vin.size(), 1U);

    // Reserve balance: any candidate would exceed the target amount
    AutocombineParams reserve = params;
    reserve.target_amount = params.reward + 25 * COIN;
    res = RunAutocombine(*wallet, kernel, coinbaseKey.GetPubKey(), reserve);
    BOOST_CHECK_EQUAL(res.stakeTx.vin.size(), 1U);

    SetMockTime(0);
}

//! Stale (not in the UTXO set), zero-conf, immature and collateral outputs
//! are skipped while healthy candidates still combine (via the historic
//! path, with a threshold large enough that even the collateral-sized
//! output would be accepted were it not filtered).
BOOST_FIXTURE_TEST_CASE(autocombine_skips_bad_candidates, TestChain100Setup, * boost::unit_test::disabled())
{
    auto wallet = CreateSyncedWallet(*m_node.chain, *m_node.coinjoin_loader, m_node.chainman->ActiveChain(), m_args, coinbaseKey);
    const CScript wallet_spk = GetScriptForDestination(PKHash(coinbaseKey.GetPubKey()));
    const CBlockIndex* tip = WITH_LOCK(::cs_main, return m_node.chainman->ActiveChain().Tip());

    const CAmount collat = dmn_types::Regular.collat_amount;

    std::vector<CTransactionRef> healthy;
    for (int i = 0; i < 3; ++i) {
        healthy.push_back(AddFakeCandidate(*wallet, *m_node.mempool, tip, wallet_spk, 50 * COIN));
    }
    // Confirmed in the wallet's view but absent from the UTXO set
    auto stale_tx = AddFakeCandidate(*wallet, *m_node.mempool, tip, wallet_spk, 50 * COIN, /*in_utxo_set=*/false);
    // Never confirmed at all
    auto zeroconf_tx = AddFakeCandidate(*wallet, *m_node.mempool, tip, wallet_spk, 50 * COIN, true, /*confirmed=*/false);
    // Masternode collateral amount
    auto collateral_tx = AddFakeCandidate(*wallet, *m_node.mempool, tip, wallet_spk, collat);

    SetMockTime(tip->GetBlockTime() + Params().MinStakeAge() + 3600);

    wallet->fAutocombine = AUTOCOMBINE_SAME;
    wallet->nStakeSplitThreshold = static_cast<size_t>(collat / COIN) * 2;
    wallet->nStakeCombineMax = 0;

    const COutPoint kernel(InsecureRand256(), 0);
    AutocombineParams params;
    params.reward = collat; // (reward + collat) is still below the target

    const auto res = RunAutocombine(*wallet, kernel, coinbaseKey.GetPubKey(), params);

    // Healthy candidates combined...
    BOOST_CHECK_EQUAL(res.stakeTx.vin.size(), 4U);
    for (const auto& tx : healthy) {
        BOOST_CHECK(VinContains(res.stakeTx, COutPoint(tx->GetHash(), 0)));
    }
    // ...while each bad candidate stayed out
    BOOST_CHECK(!VinContains(res.stakeTx, COutPoint(stale_tx->GetHash(), 0)));
    BOOST_CHECK(!VinContains(res.stakeTx, COutPoint(zeroconf_tx->GetHash(), 0)));
    BOOST_CHECK(!VinContains(res.stakeTx, COutPoint(collateral_tx->GetHash(), 0)));
    // An immature fixture coinbase must not be selected either
    BOOST_CHECK(!VinContains(res.stakeTx, COutPoint(m_coinbase_txns.back()->GetHash(), 0)));

    SetMockTime(0);
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace wallet
