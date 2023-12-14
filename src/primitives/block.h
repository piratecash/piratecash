// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PRIMITIVES_BLOCK_H
#define BITCOIN_PRIMITIVES_BLOCK_H

#include "pubkey.h"

class CKeyStore;
#include <primitives/transaction.h>
#include <serialize.h>
#include <uint256.h>

#define BEGIN(a)	((char*)&(a))

/** Nodes collect new transactions into a block, hash them into a hash tree,
 * and scan through nonce values to make the block's hash satisfy proof-of-work
 * requirements.  When they solve the proof-of-work, they broadcast the block
 * to everyone and the block is added to the block chain.  The first transaction
 * in the block is a special one that creates a new coin owned by the creator
 * of the block.
 */
class CBlockHeader
{
public:
    static constexpr uint32_t POS_BIT = 0x10000000UL;
    static constexpr uint32_t POSV2_BITS = POS_BIT | 0x08000000UL;

    // header
    int32_t nVersion;
    uint256 hashPrevBlock;
    uint256 hashMerkleRoot;
    uint32_t nTime;
    uint32_t nBits;
    // Mix of PoW & PoS
    // NOTE: Proof & Modifier are not strictly required in PoS block,
    //       but it should aid debugging issues in field.
    uint32_t nNonce;
    // PoS only
    uint256 posStakeHash; // stake primary input tx
    uint32_t posStakeN; // stake primary input tx output
    std::vector<unsigned char> vchBlockSig; // to be signed by coinbase/coinstake primary out
    // PirateCash: A copy from CBlockIndex.nFlags from other clients. We need this information because we are using headers-first syncronization.
    uint32_t nFlags;

    // Memory-only
    mutable CPubKey posPubKey;

    CBlockHeader()
    {
        SetNull();
    }

    SERIALIZE_METHODS(CBlockHeader, obj) {
        READWRITE(obj.nVersion, obj.hashPrevBlock, obj.hashMerkleRoot, obj.nTime, obj.nBits, obj.nNonce);
        if (obj.IsProofOfStakeV2()) {
            READWRITE(obj.posStakeHash);
            READWRITE(obj.posStakeN);

            if (!(s.GetType() & SER_GETHASH)) {
                READWRITE(obj.vchBlockSig);
            }

            if (ser_action.ForRead()) {
                obj.posPubKey = CPubKey();
            }
        }
        // piratecash: do not serialize nFlags when computing hash
        if (!(s.GetType() & SER_GETHASH) && s.GetType() & SER_POSMARKER)
            READWRITE(obj.nFlags);
    }

    void SetNull()
    {
        nVersion = 0;
        hashPrevBlock.SetNull();
        hashMerkleRoot.SetNull();
        nTime = 0;
        nBits = 0;
        nNonce = 0;
        posStakeHash.SetNull();
        posStakeN = 0;
        vchBlockSig.clear();
        nFlags = 0;
        posPubKey = CPubKey();
    }

    bool IsNull() const
    {
        return (nBits == 0);
    }

    uint256 GetHash() const;

    uint256 GetPoWHash() const;

    int64_t GetBlockTime() const
    {
        return (int64_t)nTime;
    }

    uint32_t& nStakeModifier() {
        return nNonce;
    }
    const uint32_t& nStakeModifier() const {
        return nNonce;
    }

    bool IsProofOfStake() const
    {
        return ((nFlags & 1) || ((nVersion & CBlockHeader::POS_BIT) != 0));
    }

    bool IsProofOfStakeV2() const
    {
        return (nVersion & CBlockHeader::POSV2_BITS) == CBlockHeader::POSV2_BITS;
    }

    bool IsProofOfWork() const
    {
        return !IsProofOfStake();
    }

    bool SignBlock(const CKeyStore& keystore);
    bool CheckBlockSignature(const CKeyID&) const;
    const CPubKey& BlockPubKey() const;
    COutPoint StakeInput() const {
        return COutPoint(posStakeHash, posStakeN);
    }
};


class CBlock : public CBlockHeader
{
public:
    static constexpr size_t COINBASE_INDEX = 0;
    static constexpr size_t STAKE_INDEX = 1;

    // network and disk
    std::vector<CTransactionRef> vtx;

    // PirateCash: block signature - signed by coin base txout[0]'s owner
    std::vector<unsigned char> vchBlockSig;

    // memory only
    mutable bool fChecked;

    CBlock()
    {
        SetNull();
    }

    CBlock(const CBlockHeader &header)
    {
        SetNull();
        *(static_cast<CBlockHeader*>(this)) = header;
    }

    SERIALIZE_METHODS(CBlock, obj)
    {
        READWRITEAS(CBlockHeader, obj);
        READWRITE(obj.vtx);
        READWRITE(obj.vchBlockSig);
    }

    void SetNull()
    {
        CBlockHeader::SetNull();
        vtx.clear();
        fChecked = false;
        vchBlockSig.clear();
    }

    CBlockHeader GetBlockHeader() const
    {
        CBlockHeader block;
        block.nVersion       = nVersion;
        block.hashPrevBlock  = hashPrevBlock;
        block.hashMerkleRoot = hashMerkleRoot;
        block.nTime          = nTime;
        block.nBits          = nBits;
        block.nNonce         = nNonce;
        block.nFlags         = nFlags;
        return block;
    }

    bool HasCoinBase() const;
    bool HasStake() const;

    bool IsProofOfStakeTX() const {
        return (vtx.size() > 1 && vtx[1]->IsCoinStake());
    }

    const CTransactionRef& CoinBase() const {
        return vtx[COINBASE_INDEX];
    }

    CTransactionRef& CoinBase() {
        return vtx[COINBASE_INDEX];
    }

    const CTransactionRef& Stake() const {
        return vtx[STAKE_INDEX];
    }

    CTransactionRef& Stake() {
        return vtx[STAKE_INDEX];
    }

    std::string ToString() const;
};


/** Describes a place in the block chain to another node such that if the
 * other node doesn't have the same branch, it can find a recent common trunk.
 * The further back it is, the further before the fork it may be.
 */
struct CBlockLocator
{
    std::vector<uint256> vHave;

    CBlockLocator() {}

    explicit CBlockLocator(const std::vector<uint256>& vHaveIn) : vHave(vHaveIn) {}

    SERIALIZE_METHODS(CBlockLocator, obj)
    {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(obj.vHave);
    }

    void SetNull()
    {
        vHave.clear();
    }

    bool IsNull() const
    {
        return vHave.empty();
    }
};

#endif // BITCOIN_PRIMITIVES_BLOCK_H
