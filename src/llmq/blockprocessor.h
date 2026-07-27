// Copyright (c) 2018-2025 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_LLMQ_BLOCKPROCESSOR_H
#define BITCOIN_LLMQ_BLOCKPROCESSOR_H

#include <bls/bls.h>
#include <llmq/params.h>
#include <llmq/utils.h>
#include <msg_result.h>
#include <unordered_lru_cache.h>

#include <checkqueue.h>
#include <protocol.h>
#include <saltedhasher.h>
#include <sync.h>

#include <gsl/pointers.h>

#include <optional>

class BlockValidationState;
class CBlock;
class CBlockIndex;
class CBLSSignature;
class CChain;
class CChainState;
class CDataStream;
class CDeterministicMNManager;
class CEvoDB;
class CNode;

extern RecursiveMutex cs_main; // NOLINT(readability-redundant-declaration)

namespace llmq
{
class CFinalCommitment;
class CQuorumSnapshotManager;

//! Serialized hashes of the commitments mined for the active quorums, by LLMQ type.
using QcHashMap = std::map<Consensus::LLMQType, std::vector<uint256>>;
//! As above, but keyed by quorumIndex, for rotation-enabled types.
using QcIndexedHashMap = std::map<Consensus::LLMQType, std::map<int16_t, uint256>>;

class CQuorumBlockProcessor
{
private:
    CChainState& m_chainstate;
    CDeterministicMNManager& m_dmnman;
    CEvoDB& m_evoDb;
    CQuorumSnapshotManager& m_qsnapman;

    CCheckQueue<utils::BlsCheck> m_bls_queue{4};

    mutable Mutex minableCommitmentsCs;
    std::map<std::pair<Consensus::LLMQType, uint256>, uint256> minableCommitmentsByQuorum GUARDED_BY(minableCommitmentsCs);
    std::map<uint256, CFinalCommitment> minableCommitments GUARDED_BY(minableCommitmentsCs);

    mutable std::map<Consensus::LLMQType, Uint256LruHashMap<bool>> mapHasMinedCommitmentCache GUARDED_BY(minableCommitmentsCs);

    // Memoizes GetQcHashes(). The whole-result cache is keyed on the set of active
    // quorum base blocks, the LRU on those base-block hashes; neither key identifies
    // which CFinalCommitment was mined for a base, so both are dropped whenever mined
    // commitment state changes (see DropQcHashesCache). Owning them here keeps that
    // invalidation next to the writes it has to follow, and ties their lifetime to the
    // block index whose CBlockIndex* the outer cache stores.
    mutable Mutex m_qc_hashes_cache_mutex;
    mutable std::map<Consensus::LLMQType, std::vector<const CBlockIndex*>> m_quorums_cached GUARDED_BY(m_qc_hashes_cache_mutex);
    mutable std::map<Consensus::LLMQType, Uint256LruHashMap<std::pair<uint256, int>>> m_qc_hashes_lru GUARDED_BY(m_qc_hashes_cache_mutex);
    mutable QcHashMap m_qc_hashes_cached GUARDED_BY(m_qc_hashes_cache_mutex);
    mutable QcIndexedHashMap m_qc_indexed_hashes_cached GUARDED_BY(m_qc_hashes_cache_mutex);

public:
    CQuorumBlockProcessor() = delete;
    CQuorumBlockProcessor(const CQuorumBlockProcessor&) = delete;
    CQuorumBlockProcessor& operator=(const CQuorumBlockProcessor&) = delete;
    explicit CQuorumBlockProcessor(CChainState& chainstate, CDeterministicMNManager& dmnman, CEvoDB& evoDb,
                                   CQuorumSnapshotManager& qsnapman, int8_t bls_threads);
    ~CQuorumBlockProcessor();

    [[nodiscard]] MessageProcessingResult ProcessMessage(const CNode& peer, std::string_view msg_type, CDataStream& vRecv)
        EXCLUSIVE_LOCKS_REQUIRED(!minableCommitmentsCs);

    bool ProcessBlock(const CBlock& block, gsl::not_null<const CBlockIndex*> pindex, BlockValidationState& state,
                      bool fJustCheck, bool fBLSChecks) EXCLUSIVE_LOCKS_REQUIRED(::cs_main, !minableCommitmentsCs, !m_qc_hashes_cache_mutex);
    bool UndoBlock(const CBlock& block, gsl::not_null<const CBlockIndex*> pindex)
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main, !minableCommitmentsCs, !m_qc_hashes_cache_mutex);

    //! it returns hash of commitment if it should be relay, otherwise nullopt
    std::optional<CInv> AddMineableCommitment(const CFinalCommitment& fqc) EXCLUSIVE_LOCKS_REQUIRED(!minableCommitmentsCs);
    bool HasMineableCommitment(const uint256& hash) const EXCLUSIVE_LOCKS_REQUIRED(!minableCommitmentsCs);
    bool GetMineableCommitmentByHash(const uint256& commitmentHash, CFinalCommitment& ret) const
        EXCLUSIVE_LOCKS_REQUIRED(!minableCommitmentsCs);
    std::optional<std::vector<CFinalCommitment>> GetMineableCommitments(const Consensus::LLMQParams& llmqParams,
                                                                        int nHeight) const
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main, !minableCommitmentsCs);
    bool GetMineableCommitmentsTx(const Consensus::LLMQParams& llmqParams, int nHeight, std::vector<CTransactionRef>& ret) const
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main, !minableCommitmentsCs);
    bool HasMinedCommitment(Consensus::LLMQType llmqType, const uint256& quorumHash) const
        EXCLUSIVE_LOCKS_REQUIRED(!minableCommitmentsCs);
    std::pair<CFinalCommitment, uint256> GetMinedCommitment(Consensus::LLMQType llmqType, const uint256& quorumHash) const;

    /**
     * Serialized hashes of the commitments mined for the quorums active as of pindexPrev.
     *
     * Memoized; returns nullopt if a commitment recorded as mined could not be read back,
     * which should never happen.
     */
    std::optional<std::pair<QcHashMap, QcIndexedHashMap>> GetQcHashes(const CBlockIndex* pindexPrev) const
        EXCLUSIVE_LOCKS_REQUIRED(!m_qc_hashes_cache_mutex);

    std::vector<const CBlockIndex*> GetMinedCommitmentsUntilBlock(Consensus::LLMQType llmqType, gsl::not_null<const CBlockIndex*> pindex, size_t maxCount) const;
    std::map<Consensus::LLMQType, std::vector<const CBlockIndex*>> GetMinedAndActiveCommitmentsUntilBlock(gsl::not_null<const CBlockIndex*> pindex) const;

    std::vector<const CBlockIndex*> GetMinedCommitmentsIndexedUntilBlock(Consensus::LLMQType llmqType, const CBlockIndex* pindex, size_t maxCount) const;
    std::vector<const CBlockIndex*> GetLastMinedCommitmentsPerQuorumIndexUntilBlock(Consensus::LLMQType llmqType,
                                                                                    const CBlockIndex* pindex,
                                                                                    size_t cycle) const;
    std::optional<const CBlockIndex*> GetLastMinedCommitmentsByQuorumIndexUntilBlock(Consensus::LLMQType llmqType, const CBlockIndex* pindex, int quorumIndex, size_t cycle) const;
private:
    //! Called from every site that writes or erases mined commitment state.
    void DropQcHashesCache() EXCLUSIVE_LOCKS_REQUIRED(!m_qc_hashes_cache_mutex);

    static bool GetCommitmentsFromBlock(const CBlock& block, gsl::not_null<const CBlockIndex*> pindex, std::multimap<Consensus::LLMQType, CFinalCommitment>& ret, BlockValidationState& state) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    bool ProcessCommitment(int nHeight, const uint256& blockHash, const CFinalCommitment& qc, BlockValidationState& state,
                           bool fJustCheck) EXCLUSIVE_LOCKS_REQUIRED(::cs_main, !minableCommitmentsCs, !m_qc_hashes_cache_mutex);
    size_t GetNumCommitmentsRequired(const Consensus::LLMQParams& llmqParams, int nHeight) const
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main, !minableCommitmentsCs);
    static uint256 GetQuorumBlockHash(const Consensus::LLMQParams& llmqParams, const CChain& active_chain, int nHeight, int quorumIndex) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
};
} // namespace llmq

#endif // BITCOIN_LLMQ_BLOCKPROCESSOR_H
