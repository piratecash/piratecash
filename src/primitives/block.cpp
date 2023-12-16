// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/block.h>

#include <script/standard.h>
#include <script/sign.h>
#include <keystore.h>
#include <pos_kernel.h>
#include <hash.h>
#include <streams.h>
#include <tinyformat.h>
#include <util/strencodings.h>
#include <crypto/common.h>

uint256 CBlockHeader::GetHash() const
{
    if (nVersion < 4)
    {
        uint256 thash;
        scrypt_1024_1_1_256(BEGIN(nVersion), BEGIN(thash));
        return thash;
    }
    return SerializeHash(*this);
}

uint256 CBlockHeader::GetPoWHash() const
{
    uint256 thash;
    scrypt_1024_1_1_256(BEGIN(nVersion), BEGIN(thash));
    return thash;
}

std::string CBlock::ToString() const
{
    std::stringstream s;

    if (IsProofOfStake()) {
         s << strprintf("CBlock(hash=%s, ver=0x%08x, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, posStakeHash=%s, posStakeN=%u, posPubKey=%u, posBlockSig=%u vtx=%u)\n",
                        GetHash().ToString(),
                        nVersion,
                        hashPrevBlock.ToString(),
                        hashMerkleRoot.ToString(),
                        nTime, nBits,
                        nNonce,
                        posStakeHash.ToString(),
                        posStakeN,
                        posPubKey.size(),
                        vchBlockSig.size(),
                        vtx.size());
    }else{
        s << strprintf("CBlock(hash=%s, ver=0x%08x, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, vtx=%u)\n",
            GetHash().ToString(),
            nVersion,
            hashPrevBlock.ToString(),
            hashMerkleRoot.ToString(),
            nTime, nBits, nNonce,
            vtx.size());
    }

    for (const auto& tx : vtx) {
        s << "  " << tx->ToString() << "\n";
    }
    return s.str();
}


// ppcoin: sign block
bool CBlockHeader::SignBlock(const CKeyStore& keystore)
{
    if (IsProofOfStake())
    {
        if (!posPubKey.IsValid())
            return false;

        CKey key;

        if (!keystore.GetKey(posPubKey.GetID(), key)) {
            return false;
        }

        if (!key.SignCompact(GetHash(), vchBlockSig)) {
            return false;
        }

        return true;
    }

    return true;
}

bool CBlockHeader::CheckBlockSignature(const CKeyID& key_id) const
{
    if (!IsProofOfStake()) {
        return true;
    }

    if (vchBlockSig.empty()) {
        return false;
    }

    auto hash = GetHash();
    posPubKey.RecoverCompact(hash, vchBlockSig);

    if (!posPubKey.IsValid()) {
        return false;
    }

    return posPubKey.GetID() == key_id;
}

const CPubKey& CBlockHeader::BlockPubKey() const
{
    // In case it's read from disk
    if (!posPubKey.IsValid() && !vchBlockSig.empty()) {
        posPubKey.RecoverCompact(GetHash(), vchBlockSig);
    }

    return posPubKey;
}

bool CBlock::HasCoinBase() const {
    return (!vtx.empty() && CoinBase()->IsCoinBase());
}

bool CBlock::HasStake() const {
    if (!IsProofOfStake() || (vtx.size() < 2)) {
        return false;
    }

    BlockPubKey();

    if (!posPubKey.IsValid()) {
        return false;
    }

    const auto spk = GetScriptForDestination(posPubKey.GetID());
    const auto& cb_vout = CoinBase()->vout;
    const auto& stake = Stake();

    if (cb_vout.empty() || stake->vin.empty() || stake->vout.empty()) {
        return false;
    }

    // Check it's the same stake
    if (stake->vin[0].prevout != COutPoint(posStakeHash, posStakeN)) {
        return false;
    }

    // Check primary coinbase output
    if (cb_vout[0].scriptPubKey != spk) {
        return false;
    }

    // Check stake outputs
    CAmount total_amt = 0;

    for (const auto &so : stake->vout) {
        if (so.IsEmpty()){
            continue;
        }
        if (so.scriptPubKey != spk) {
            return false;
        }

        total_amt += so.nValue;
    }

    if (total_amt < MIN_STAKE_AMOUNT) {
        return false;
    }

    return true;
}
