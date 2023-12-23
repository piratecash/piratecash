//
// Copyright (c) 2019 The Energi Core developers
//
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "spork.h"
#include "utilstrencodings.h"
#include "validation.h"
#include "wallet/wallet.h"

#include "test/test_energi.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(PoS_tests, TestChain100Setup)

BOOST_AUTO_TEST_CASE(PoS_transition_test)
{
#if 0
    const char* args[] = {
        "",
        "-debug",
        "-printcoinstake",
        "-printstakemodifier",
    };
    ParseParameters(ARRAYLEN(args), args);
    fDebug = true;
    fPrintToConsole = true;
#endif

    CScript scriptPubKey = CScript() <<  ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;

    CWallet wallet;
    pwalletMain = &wallet;
    pwalletMain->AddKeyPubKey(coinbaseKey, coinbaseKey.GetPubKey());
    pwalletMain->ScanForWalletTransactions(chainActive.Genesis(), true);
    pwalletMain->ReacceptWalletTransactions();
    pwalletMain->nStakeSplitThreshold = 1;

    CBitcoinAddress spork_address;
    spork_address.Set(coinbaseKey.GetPubKey().GetID());
    BOOST_CHECK(spork_address.IsValid());
    
    BOOST_CHECK(sporkManager.SetSporkAddress(spork_address.ToString()));
    BOOST_CHECK(sporkManager.SetPrivKey(CBitcoinSecret(coinbaseKey).ToString()));
    BOOST_CHECK(sporkManager.UpdateSpork(SPORK_15_FIRST_POS_BLOCK, 103, *connman));
    //int last_pow_height;

    // PoW mode
    //---
    for (auto i = 2; i > 0; --i) {
        auto blk = CreateAndProcessBlock(CMutableTransactionList(), scriptPubKey);
        BOOST_CHECK(blk.IsProofOfWork());
        //last_pow_height = blk.nHeight;
    }

    // PoS mode by spork
    //---
    for (auto i = 10; i > 0; --i) {
        auto blk = CreateAndProcessBlock(CMutableTransactionList(), CScript());
        BOOST_CHECK(blk.IsProofOfStake());
        BOOST_CHECK(blk.HasStake());
    }

    // Still, it must continue PoS even after Spork change
    //---
    BOOST_CHECK(sporkManager.UpdateSpork(SPORK_15_FIRST_POS_BLOCK, 999999ULL, *connman));
    //PruneBlockFilesManual(last_pow_height);
    
    {
        auto blk = CreateAndProcessBlock(CMutableTransactionList(), CScript());
        BOOST_CHECK(blk.IsProofOfStake());
        BOOST_CHECK(blk.HasStake());
    }

    // end
    pwalletMain = nullptr;
}

BOOST_AUTO_TEST_SUITE_END()
