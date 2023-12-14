// Copyright (c) 2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util.h>

#include <chainparams.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <key_io.h>
#include <miner.h>
#include <pow.h>
#include <scheduler.h>
#include <script/standard.h>
#include <txdb.h>
#include <validation.h>
#include <validationinterface.h>
#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#endif

const std::string ADDRESS_B58T_UNSPENDABLE = "yXXXXXXXXXXXXXXXXXXXXXXXXXXXVd2rXU";
const std::string ADDRESS_BCRT1_UNSPENDABLE = "bcrt1qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq3xueyj";

#ifdef ENABLE_WALLET
std::string getnewaddress(CWallet& w)
{
    CPubKey new_key;
    if (!w.GetKeyFromPool(new_key, false)) assert(false);

    CKeyID keyID = new_key.GetID();
    w.SetAddressBook(keyID, /* label */ "", "receive");

    return EncodeDestination(keyID);
}

void importaddress(CWallet& wallet, const std::string& address)
{
    LOCK(wallet.cs_wallet);
    const auto dest = DecodeDestination(address);
    assert(IsValidDestination(dest));
    const auto script = GetScriptForDestination(dest);
    wallet.MarkDirty();
    assert(!wallet.HaveWatchOnly(script));
    if (!wallet.AddWatchOnly(script, 0 /* nCreateTime */)) assert(false);
    wallet.SetAddressBook(dest, /* label */ "", "receive");
}
#endif // ENABLE_WALLET

CTxIn generatetoaddress(const std::string& address)
{
    const auto dest = DecodeDestination(address);
    assert(IsValidDestination(dest));
    const auto coinbase_script = GetScriptForDestination(dest);

    return MineBlock(coinbase_script);
}

CTxIn MineBlock(const CScript& coinbase_scriptPubKey)
{
    auto block = PrepareBlock(coinbase_scriptPubKey);

    while (!CheckProofOfWork(block->GetHash(), block->nBits, Params().GetConsensus())) {
        ++block->nNonce;
        assert(block->nNonce);
    }

    bool processed{ProcessNewBlock(Params(), block, true, nullptr)};
    assert(processed);

    return CTxIn{block->vtx[0]->GetHash(), 0};
}

std::shared_ptr<CBlock> PrepareBlock(const CScript& coinbase_scriptPubKey)
{
    auto ptemplate = BlockAssembler(Params()).CreateNewBlock(coinbase_scriptPubKey, GetWallets()[0]);
    auto block = std::make_shared<CBlock>(*ptemplate->block);

//    auto block = std::make_shared<CBlock>(
//        BlockAssembler{Params()}
//            .CreateNewBlock(coinbase_scriptPubKey, GetWallets()[0])
//            ->block);

    block->nTime = ::ChainActive().Tip()->GetMedianTimePast() + 1;
    block->hashMerkleRoot = BlockMerkleRoot(*block);

    return block;
}
