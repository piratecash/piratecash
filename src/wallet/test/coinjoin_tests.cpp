// Copyright (c) 2020 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/setup_common.h>

#include <amount.h>
#include <coinjoin/util.h>
#include <coinjoin/coinjoin.h>
#include <coinjoin/options.h>
#include <util/translation.h>
#include <validation.h>
#include <wallet/wallet.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(coinjoin_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(coinjoin_collateral_tests)
{
    // Good collateral values
    static_assert(CCoinJoin::IsCollateralAmount(0.00010000 * COIN));
    static_assert(CCoinJoin::IsCollateralAmount(0.00012345 * COIN));
    static_assert(CCoinJoin::IsCollateralAmount(0.00032123 * COIN));
    static_assert(CCoinJoin::IsCollateralAmount(0.00019000 * COIN));

    // Bad collateral values
    static_assert(!CCoinJoin::IsCollateralAmount(0.00009999 * COIN));
    static_assert(!CCoinJoin::IsCollateralAmount(0.00040001 * COIN));
    static_assert(!CCoinJoin::IsCollateralAmount(0.00100000 * COIN));
    static_assert(!CCoinJoin::IsCollateralAmount(0.00100001 * COIN));
}

class CTransactionBuilderTestSetup : public TestChain100Setup
{
public:
    CTransactionBuilderTestSetup()
    {
        CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));
        chain = interfaces::MakeChain();
        wallet = MakeUnique<CWallet>(*chain, WalletLocation(), CreateMockWalletDatabase());
        bool firstRun;
        wallet->LoadWallet(firstRun);
        AddWallet(wallet);
        {
            LOCK(wallet->cs_wallet);
            wallet->AddKeyPubKey(coinbaseKey, coinbaseKey.GetPubKey());
        }
        WalletRescanReserver reserver(wallet.get());
        reserver.reserve();

        CWallet::ScanResult result = wallet->ScanForWalletTransactions(::ChainActive().Genesis()->GetBlockHash(), {} /* stop_block */, reserver, true /* fUpdate */);
        BOOST_CHECK_EQUAL(result.status, CWallet::ScanResult::SUCCESS);
    }

    ~CTransactionBuilderTestSetup()
    {
        RemoveWallet(wallet);
    }

    std::shared_ptr<interfaces::Chain> chain;
    std::shared_ptr<CWallet> wallet;

    CWalletTx& AddTxToChain(uint256 nTxHash)
    {
        std::map<uint256, CWalletTx>::iterator it;
        CMutableTransaction blocktx;
        {
            LOCK(wallet->cs_wallet);
            it = wallet->mapWallet.find(nTxHash);
            assert(it != wallet->mapWallet.end());
            blocktx = CMutableTransaction(*it->second.tx);
        }
        CreateAndProcessBlock({blocktx}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));
        auto locked_chain = wallet->chain().lock();
        LOCK(wallet->cs_wallet);
        it->second.SetMerkleBranch(::ChainActive().Tip()->GetBlockHash(), 1);
        return it->second;
    }
    CompactTallyItem GetTallyItem(const std::vector<CAmount>& vecAmounts)
    {
        CompactTallyItem tallyItem;
        CTransactionRef tx;
        CReserveKey destKey(wallet.get());
        CAmount nFeeRet;
        int nChangePosRet = -1;
        bilingual_str strError;
        CCoinControl coinControl;
        CPubKey pubKey;
        BOOST_CHECK(destKey.GetReservedKey(pubKey, false));
        tallyItem.txdest = pubKey.GetID();
        for (CAmount nAmount : vecAmounts) {
            {
                auto locked_chain = chain->lock();
                BOOST_CHECK(wallet->CreateTransaction(*locked_chain, {{GetScriptForDestination(tallyItem.txdest), nAmount, false}}, tx, nFeeRet, nChangePosRet, strError, coinControl));
            }
            wallet->CommitTransaction(tx, {}, {});
            AddTxToChain(tx->GetHash());
            for (size_t n = 0; n < tx->vout.size(); ++n) {
                if (nChangePosRet != -1 && int(n) == nChangePosRet) {
                    // Skip the change output to only return the requested coins
                    continue;
                }
                tallyItem.vecInputCoins.emplace_back(tx, n);
                tallyItem.nAmount += tx->vout[n].nValue;
            }
        }
        assert(tallyItem.vecInputCoins.size() == vecAmounts.size());
        destKey.KeepKey();
        return tallyItem;
    }
};

BOOST_FIXTURE_TEST_CASE(CTransactionBuilderTest, CTransactionBuilderTestSetup)
{
    // NOTE: Mock wallet version is FEATURE_BASE which means that it uses uncompressed pubkeys
    // (65 bytes instead of 33 bytes) and we use Low R signatures, so CTxIn size is 179 bytes.
    // Each output is 34 bytes, vin and vout compact sizes are 1 byte each.
    // Therefore base size (i.e. for a tx with 1 input, 0 outputs) is expected to be
    // 4(n32bitVersion) + 1(vin size) + 179(vin[0]) + 1(vout size) + 4(nLockTime) = 189 bytes.

    minRelayTxFee = CFeeRate(DEFAULT_MIN_RELAY_TX_FEE);
    // Tests with single outpoint tallyItem
    {
        CompactTallyItem tallyItem = GetTallyItem({4999});
        CTransactionBuilder txBuilder(wallet, tallyItem);

        BOOST_CHECK_EQUAL(txBuilder.CountOutputs(), 0);
        BOOST_CHECK_EQUAL(txBuilder.GetAmountInitial(), tallyItem.nAmount);
        BOOST_CHECK_EQUAL(txBuilder.GetAmountLeft(), 4810);         // 4999 - 189

        BOOST_CHECK(txBuilder.CouldAddOutput(4776));                // 4810 - 34
        BOOST_CHECK(!txBuilder.CouldAddOutput(4777));

        BOOST_CHECK(txBuilder.CouldAddOutput(0));
        BOOST_CHECK(!txBuilder.CouldAddOutput(-1));

        BOOST_CHECK(txBuilder.CouldAddOutputs({1000, 1000, 2708})); // (4810 - 34 * 3) split in 3 outputs
        BOOST_CHECK(!txBuilder.CouldAddOutputs({1000, 1000, 2709}));

        BOOST_CHECK_EQUAL(txBuilder.AddOutput(4999), nullptr);
        BOOST_CHECK_EQUAL(txBuilder.AddOutput(-1), nullptr);

        CTransactionBuilderOutput* output = txBuilder.AddOutput();
        BOOST_CHECK(output->UpdateAmount(txBuilder.GetAmountLeft()));
        BOOST_CHECK(output->UpdateAmount(1));
        BOOST_CHECK(output->UpdateAmount(output->GetAmount() + txBuilder.GetAmountLeft()));
        BOOST_CHECK(!output->UpdateAmount(output->GetAmount() + 1));
        BOOST_CHECK(!output->UpdateAmount(0));
        BOOST_CHECK(!output->UpdateAmount(-1));
        BOOST_CHECK_EQUAL(txBuilder.CountOutputs(), 1);

        bilingual_str strResult;
        BOOST_CHECK(txBuilder.Commit(strResult));
        CWalletTx& wtx = AddTxToChain(uint256S(strResult.original));
        BOOST_CHECK_EQUAL(wtx.tx->vout.size(), txBuilder.CountOutputs()); // should have no change output
        BOOST_CHECK_EQUAL(wtx.tx->vout[0].nValue, output->GetAmount());
        BOOST_CHECK(wtx.tx->vout[0].scriptPubKey == output->GetScript());
    }
    // Tests with multiple outpoint tallyItem
    {
        CompactTallyItem tallyItem = GetTallyItem({10000, 20000, 30000, 40000, 50000});
        CTransactionBuilder txBuilder(wallet, tallyItem);
        std::vector<CTransactionBuilderOutput*> vecOutputs;
        bilingual_str strResult;

        auto output = txBuilder.AddOutput(100);
        BOOST_CHECK(output != nullptr);
        BOOST_CHECK(!txBuilder.Commit(strResult));

        if (output != nullptr) {
            output->UpdateAmount(1000);
            vecOutputs.push_back(output);
        }
        while (vecOutputs.size() < 100) {
            output = txBuilder.AddOutput(1000 + vecOutputs.size());
            if (output == nullptr) {
                break;
            }
            vecOutputs.push_back(output);
        }
        BOOST_CHECK_EQUAL(vecOutputs.size(), 100);
        BOOST_CHECK_EQUAL(txBuilder.CountOutputs(), vecOutputs.size());
        BOOST_CHECK(txBuilder.Commit(strResult));
        CWalletTx& wtx = AddTxToChain(uint256S(strResult.original));
        BOOST_CHECK_EQUAL(wtx.tx->vout.size(), txBuilder.CountOutputs() + 1); // should have change output
        for (const auto& out : wtx.tx->vout) {
            auto it = std::find_if(vecOutputs.begin(), vecOutputs.end(), [&](CTransactionBuilderOutput* output) -> bool {
                return output->GetAmount() == out.nValue && output->GetScript() == out.scriptPubKey;
            });
            if (it != vecOutputs.end()) {
                vecOutputs.erase(it);
            } else {
                // change output
                BOOST_CHECK_EQUAL(txBuilder.GetAmountLeft() - 34, out.nValue);
            }
        }
        BOOST_CHECK(vecOutputs.size() == 0);
    }
}

BOOST_AUTO_TEST_SUITE_END()
