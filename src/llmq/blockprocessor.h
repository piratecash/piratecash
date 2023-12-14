// Copyright (c) 2018-2022 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_LLMQ_BLOCKPROCESSOR_H
#define BITCOIN_LLMQ_BLOCKPROCESSOR_H

#include <unordered_lru_cache.h>

#include <chain.h>
#include <consensus/params.h>
#include <primitives/block.h>
#include <saltedhasher.h>
#include <streams.h>
#include <sync.h>
#include <optional>

#include <unordered_map>

class CNode;
class CConnman;
class CValidationState;
class CEvoDB;

extern CCriticalSection cs_main;

namespace llmq
{

class CFinalCommitment;
using CFinalCommitmentPtr = std::unique_ptr<CFinalCommitment>;

class CQuorumBlockProcessor
{
private:
    CEvoDB& evoDb;

    // TODO cleanup
    mutable CCriticalSection minableCommitmentsCs;
    std::map<std::pair<Consensus::LLMQType, uint256>, uint256> minableCommitmentsByQuorum GUARDED_BY(minableCommitmentsCs);
    std::map<uint256, CFinalCommitment> minableCommitments GUARDED_BY(minableCommitmentsCs);

    mutable std::map<Consensus::LLMQType, unordered_lru_cache<uint256, bool, StaticSaltedHasher>> mapHasMinedCommitmentCache GUARDED_BY(minableCommitmentsCs);

public:
    explicit CQuorumBlockProcessor(CEvoDB& _evoDb);

    bool UpgradeDB();

    void ProcessMessage(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv);

    bool ProcessBlock(const CBlock& block, const CBlockIndex* pindex, CValidationState& state, bool fJustCheck, bool fBLSChecks) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    bool UndoBlock(const CBlock& block, const CBlockIndex* pindex) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    void AddMineableCommitment(const CFinalCommitment& fqc);
    bool HasMineableCommitment(const uint256& hash) const;
    bool GetMineableCommitmentByHash(const uint256& commitmentHash, CFinalCommitment& ret) const;
    std::optional<std::vector<CFinalCommitment>> GetMineableCommitments(const Consensus::LLMQParams& llmqParams, int nHeight) const EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    bool GetMineableCommitmentsTx(const Consensus::LLMQParams& llmqParams, int nHeight, std::vector<CTransactionRef>& ret) const EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    bool HasMinedCommitment(Consensus::LLMQType llmqType, const uint256& quorumHash) const;
    CFinalCommitmentPtr GetMinedCommitment(Consensus::LLMQType llmqType, const uint256& quorumHash, uint256& retMinedBlockHash) const;

    std::vector<const CBlockIndex*> GetMinedCommitmentsUntilBlock(Consensus::LLMQType llmqType, const CBlockIndex* pindex, size_t maxCount) const;
    std::map<Consensus::LLMQType, std::vector<const CBlockIndex*>> GetMinedAndActiveCommitmentsUntilBlock(const CBlockIndex* pindex) const;

    std::vector<const CBlockIndex*> GetMinedCommitmentsIndexedUntilBlock(Consensus::LLMQType llmqType, const CBlockIndex* pindex, size_t maxCount) const;
    std::vector<std::pair<int, const CBlockIndex*>> GetLastMinedCommitmentsPerQuorumIndexUntilBlock(Consensus::LLMQType llmqType, const CBlockIndex* pindex, size_t cycle) const;
    std::optional<const CBlockIndex*> GetLastMinedCommitmentsByQuorumIndexUntilBlock(Consensus::LLMQType llmqType, const CBlockIndex* pindex, int quorumIndex, size_t cycle) const;
private:
    static bool GetCommitmentsFromBlock(const CBlock& block, const CBlockIndex* pindex, std::multimap<Consensus::LLMQType, CFinalCommitment>& ret, CValidationState& state) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    bool ProcessCommitment(int nHeight, const uint256& blockHash, const CFinalCommitment& qc, CValidationState& state, bool fJustCheck, bool fBLSChecks) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    bool IsMiningPhase(const Consensus::LLMQParams& llmqParams, int nHeight) const EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    size_t GetNumCommitmentsRequired(const Consensus::LLMQParams& llmqParams, int nHeight) const EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    static uint256 GetQuorumBlockHash(const Consensus::LLMQParams& llmqParams, int nHeight, int quorumIndex) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
};

extern CQuorumBlockProcessor* quorumBlockProcessor;

} // namespace llmq

#endif // BITCOIN_LLMQ_BLOCKPROCESSOR_H
