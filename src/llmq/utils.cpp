// Copyright (c) 2018-2022 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <llmq/utils.h>

#include <llmq/quorums.h>
//#include <llmq/blockprocessor.h>
#include <llmq/commitment.h>
#include <llmq/snapshot.h>

#include <bls/bls.h>
#include <chainparams.h>
#include <evo/deterministicmns.h>
#include <evo/evodb.h>
#include <masternode/meta.h>
#include <net.h>
#include <random.h>
#include <spork.h>
#include <timedata.h>
#include <util/ranges.h>
#include <validation.h>
#include <versionbits.h>

#include <optional>

namespace llmq
{

CCriticalSection cs_llmq_vbc;
VersionBitsCache llmq_versionbitscache;

void CLLMQUtils::PreComputeQuorumMembers(const CBlockIndex* pQuorumBaseBlockIndex, bool reset_cache)
{
    for (const Consensus::LLMQParams& params : CLLMQUtils::GetEnabledQuorumParams(pQuorumBaseBlockIndex->pprev)) {
        if (llmq::CLLMQUtils::IsQuorumRotationEnabled(params.type, pQuorumBaseBlockIndex) && (pQuorumBaseBlockIndex->nHeight % params.dkgInterval == 0)) {
            CLLMQUtils::GetAllQuorumMembers(params.type, pQuorumBaseBlockIndex, reset_cache);
        }
    }
}

std::vector<CDeterministicMNCPtr> CLLMQUtils::GetAllQuorumMembers(Consensus::LLMQType llmqType, const CBlockIndex* pQuorumBaseBlockIndex, bool reset_cache)
{
    static CCriticalSection cs_members;
    static std::map<Consensus::LLMQType, unordered_lru_cache<uint256, std::vector<CDeterministicMNCPtr>, StaticSaltedHasher>> mapQuorumMembers GUARDED_BY(cs_members);
    static CCriticalSection cs_indexed_members;
    static std::map<Consensus::LLMQType, unordered_lru_cache<std::pair<uint256, int>, std::vector<CDeterministicMNCPtr>, StaticSaltedHasher>> mapIndexedQuorumMembers GUARDED_BY(cs_indexed_members);
    if (!IsQuorumTypeEnabled(llmqType, pQuorumBaseBlockIndex->pprev)) {
        return {};
    }
    std::vector<CDeterministicMNCPtr> quorumMembers;
    {
        LOCK(cs_members);
        if (mapQuorumMembers.empty()) {
            InitQuorumsCache(mapQuorumMembers);
        }
        if (reset_cache) {
            mapQuorumMembers[llmqType].clear();
        } else if (mapQuorumMembers[llmqType].get(pQuorumBaseBlockIndex->GetBlockHash(), quorumMembers)) {
            return quorumMembers;
        }
    }

    if (CLLMQUtils::IsQuorumRotationEnabled(llmqType, pQuorumBaseBlockIndex)) {
        if (LOCK(cs_indexed_members); mapIndexedQuorumMembers.empty()) {
            InitQuorumsCache(mapIndexedQuorumMembers);
        }
        /*
         * Quorums created with rotation are now created in a different way. All signingActiveQuorumCount are created during the period of dkgInterval.
         * But they are not created exactly in the same block, they are spreaded overtime: one quorum in each block until all signingActiveQuorumCount are created.
         * The new concept of quorumIndex is introduced in order to identify them.
         * In every dkgInterval blocks (also called CycleQuorumBaseBlock), the spreaded quorum creation starts like this:
         * For quorumIndex = 0 : signingActiveQuorumCount
         * Quorum Q with quorumIndex is created at height CycleQuorumBaseBlock + quorumIndex
         */

        const Consensus::LLMQParams& llmqParams = GetLLMQParams(llmqType);
        int quorumIndex = pQuorumBaseBlockIndex->nHeight % llmqParams.dkgInterval;
        if (quorumIndex >= llmqParams.signingActiveQuorumCount) {
            return {};
        }
        int cycleQuorumBaseHeight = pQuorumBaseBlockIndex->nHeight - quorumIndex;
        const CBlockIndex* pCycleQuorumBaseBlockIndex = pQuorumBaseBlockIndex->GetAncestor(cycleQuorumBaseHeight);

        /*
         * Since mapQuorumMembers stores Quorum members per block hash, and we don't know yet the block hashes of blocks for all quorumIndexes (since these blocks are not created yet)
         * We store them in a second cache mapIndexedQuorumMembers which stores them by {CycleQuorumBaseBlockHash, quorumIndex}
         */
        if (reset_cache) {
            LOCK(cs_indexed_members);
            mapIndexedQuorumMembers[llmqType].clear();
        } else if (LOCK(cs_indexed_members); mapIndexedQuorumMembers[llmqType].get(std::pair(pCycleQuorumBaseBlockIndex->GetBlockHash(), quorumIndex), quorumMembers)) {
            LOCK(cs_members);
            mapQuorumMembers[llmqType].insert(pQuorumBaseBlockIndex->GetBlockHash(), quorumMembers);
            return quorumMembers;
        }

        auto q = ComputeQuorumMembersByQuarterRotation(llmqType, pCycleQuorumBaseBlockIndex);
        LOCK(cs_indexed_members);
        for (int i = 0; i < static_cast<int>(q.size()); ++i) {
            mapIndexedQuorumMembers[llmqType].insert(std::make_pair(pCycleQuorumBaseBlockIndex->GetBlockHash(), i), q[i]);
        }

        quorumMembers = q[quorumIndex];
    } else {
        quorumMembers = ComputeQuorumMembers(llmqType, pQuorumBaseBlockIndex);
    }

    LOCK(cs_members);
    mapQuorumMembers[llmqType].insert(pQuorumBaseBlockIndex->GetBlockHash(), quorumMembers);
    return quorumMembers;
}

std::vector<CDeterministicMNCPtr> CLLMQUtils::ComputeQuorumMembers(Consensus::LLMQType llmqType, const CBlockIndex* pQuorumBaseBlockIndex)
{
    auto allMns = deterministicMNManager->GetListForBlock(pQuorumBaseBlockIndex);
    auto modifier = ::SerializeHash(std::make_pair(llmqType, pQuorumBaseBlockIndex->GetBlockHash()));
    return allMns.CalculateQuorum(GetLLMQParams(llmqType).size, modifier);
}

std::vector<std::vector<CDeterministicMNCPtr>> CLLMQUtils::ComputeQuorumMembersByQuarterRotation(Consensus::LLMQType llmqType, const CBlockIndex* pCycleQuorumBaseBlockIndex)
{
    const Consensus::LLMQParams& llmqParams = GetLLMQParams(llmqType);

    const int cycleLength = llmqParams.dkgInterval;
    assert(pCycleQuorumBaseBlockIndex->nHeight % cycleLength == 0);

    const CBlockIndex* pBlockHMinusCIndex = pCycleQuorumBaseBlockIndex->GetAncestor(pCycleQuorumBaseBlockIndex->nHeight - cycleLength);
    const CBlockIndex* pBlockHMinus2CIndex = pCycleQuorumBaseBlockIndex->GetAncestor(pCycleQuorumBaseBlockIndex->nHeight - 2 * cycleLength);
    const CBlockIndex* pBlockHMinus3CIndex = pCycleQuorumBaseBlockIndex->GetAncestor(pCycleQuorumBaseBlockIndex->nHeight - 3 * cycleLength);
    LOCK(deterministicMNManager->cs);
    const CBlockIndex* pWorkBlockIndex = pCycleQuorumBaseBlockIndex->GetAncestor(pCycleQuorumBaseBlockIndex->nHeight - 8);
    auto allMns = deterministicMNManager->GetListForBlock(pWorkBlockIndex);
    LogPrint(BCLog::LLMQ, "CLLMQUtils::ComputeQuorumMembersByQuarterRotation llmqType[%d] nHeight[%d] allMns[%d]\n", static_cast<int>(llmqType), pCycleQuorumBaseBlockIndex->nHeight, allMns.GetValidMNsCount());

    PreviousQuorumQuarters previousQuarters = GetPreviousQuorumQuarterMembers(llmqParams, pBlockHMinusCIndex, pBlockHMinus2CIndex, pBlockHMinus3CIndex, pCycleQuorumBaseBlockIndex->nHeight);

    auto nQuorums = size_t(llmqParams.signingActiveQuorumCount);
    std::vector<std::vector<CDeterministicMNCPtr>> quorumMembers(nQuorums);

    auto newQuarterMembers = CLLMQUtils::BuildNewQuorumQuarterMembers(llmqParams, pCycleQuorumBaseBlockIndex, previousQuarters);
    //TODO Check if it is triggered from outside (P2P, block validation). Throwing an exception is probably a wiser choice
    //assert (!newQuarterMembers.empty());

    if (LogAcceptCategory(BCLog::LLMQ)) {
        for (auto i = 0; i < nQuorums; ++i) {
            std::stringstream ss;

            ss << " 3Cmns[";
            for (auto &m: previousQuarters.quarterHMinus3C[i]) {
                ss << m->proTxHash.ToString().substr(0, 4) << "|";
            }
            ss << " ] 2Cmns[";
            for (auto &m: previousQuarters.quarterHMinus2C[i]) {
                ss << m->proTxHash.ToString().substr(0, 4) << "|";
            }
            ss << " ] Cmns[";
            for (auto &m: previousQuarters.quarterHMinusC[i]) {
                ss << m->proTxHash.ToString().substr(0, 4) << "|";
            }
            ss << " ] new[";
            for (auto &m: newQuarterMembers[i]) {
                ss << m->proTxHash.ToString().substr(0, 4) << "|";
            }
            ss << " ]";
            LogPrint(BCLog::LLMQ, "QuarterComposition h[%d] i[%d]:%s\n", pCycleQuorumBaseBlockIndex->nHeight, i,
                     ss.str());
        }
    }

    for (auto i = 0; i < nQuorums; ++i) {
        for (auto &m: previousQuarters.quarterHMinus3C[i]) {
            quorumMembers[i].push_back(std::move(m));
        }
        for (auto &m: previousQuarters.quarterHMinus2C[i]) {
            quorumMembers[i].push_back(std::move(m));
        }
        for (auto &m: previousQuarters.quarterHMinusC[i]) {
            quorumMembers[i].push_back(std::move(m));
        }
        for (auto &m: newQuarterMembers[i]) {
            quorumMembers[i].push_back(std::move(m));
        }

        if (LogAcceptCategory(BCLog::LLMQ)) {
            std::stringstream ss;
            ss << " [";
            for (auto &m: quorumMembers[i]) {
                ss << m->proTxHash.ToString().substr(0, 4) << "|";
            }
            ss << "]";
            LogPrint(BCLog::LLMQ, "QuorumComposition h[%d] i[%d]:%s\n", pCycleQuorumBaseBlockIndex->nHeight, i,
                     ss.str());
        }
    }

    return quorumMembers;
}

PreviousQuorumQuarters CLLMQUtils::GetPreviousQuorumQuarterMembers(const Consensus::LLMQParams& llmqParams, const CBlockIndex* pBlockHMinusCIndex, const CBlockIndex* pBlockHMinus2CIndex, const CBlockIndex* pBlockHMinus3CIndex, int nHeight)
{
    auto nQuorums = size_t(llmqParams.signingActiveQuorumCount);
    PreviousQuorumQuarters quarters(nQuorums);

    std::optional<llmq::CQuorumSnapshot> quSnapshotHMinusC = quorumSnapshotManager->GetSnapshotForBlock(llmqParams.type, pBlockHMinusCIndex);
    if (quSnapshotHMinusC.has_value()) {
        quarters.quarterHMinusC = CLLMQUtils::GetQuorumQuarterMembersBySnapshot(llmqParams, pBlockHMinusCIndex, quSnapshotHMinusC.value(), nHeight);
        //TODO Check if it is triggered from outside (P2P, block validation). Throwing an exception is probably a wiser choice
        //assert (!quarterHMinusC.empty());

        std::optional<llmq::CQuorumSnapshot> quSnapshotHMinus2C = quorumSnapshotManager->GetSnapshotForBlock(llmqParams.type, pBlockHMinus2CIndex);
        if (quSnapshotHMinus2C.has_value()) {
            quarters.quarterHMinus2C = CLLMQUtils::GetQuorumQuarterMembersBySnapshot(llmqParams, pBlockHMinus2CIndex, quSnapshotHMinus2C.value(), nHeight);
            //TODO Check if it is triggered from outside (P2P, block validation). Throwing an exception is probably a wiser choice
            //assert (!quarterHMinusC.empty());

            std::optional<llmq::CQuorumSnapshot> quSnapshotHMinus3C = quorumSnapshotManager->GetSnapshotForBlock(llmqParams.type, pBlockHMinus3CIndex);
            if (quSnapshotHMinus3C.has_value()) {
                quarters.quarterHMinus3C = CLLMQUtils::GetQuorumQuarterMembersBySnapshot(llmqParams, pBlockHMinus3CIndex, quSnapshotHMinus3C.value(), nHeight);
                //TODO Check if it is triggered from outside (P2P, block validation). Throwing an exception is probably a wiser choice
                //assert (!quarterHMinusC.empty());
            }
        }
    }

    return quarters;
}

std::vector<std::vector<CDeterministicMNCPtr>> CLLMQUtils::BuildNewQuorumQuarterMembers(const Consensus::LLMQParams& llmqParams, const CBlockIndex* pQuorumBaseBlockIndex, const PreviousQuorumQuarters& previousQuarters)
{
    auto nQuorums = size_t(llmqParams.signingActiveQuorumCount);
    std::vector<std::vector<CDeterministicMNCPtr>> quarterQuorumMembers(nQuorums);

    auto quorumSize = size_t(llmqParams.size);
    auto quarterSize = quorumSize / 4;
    const CBlockIndex* pWorkBlockIndex = pQuorumBaseBlockIndex->GetAncestor(pQuorumBaseBlockIndex->nHeight - 8);
    auto modifier = ::SerializeHash(std::make_pair(llmqParams.type, pWorkBlockIndex->GetBlockHash()));

    LOCK(deterministicMNManager->cs);
    auto allMns = deterministicMNManager->GetListForBlock(pWorkBlockIndex);

    if (allMns.GetValidMNsCount() < quarterSize) return quarterQuorumMembers;

    auto MnsUsedAtH = CDeterministicMNList();
    auto MnsNotUsedAtH = CDeterministicMNList();
    std::vector<CDeterministicMNList> MnsUsedAtHIndexed(nQuorums);

    for (auto i = 0; i < nQuorums; ++i) {
        for (const auto& mn : previousQuarters.quarterHMinusC[i]) {
            if (allMns.IsMNPoSeBanned(mn->proTxHash)) {
                continue;
            }
            try {
                MnsUsedAtH.AddMN(mn);
            } catch (std::runtime_error& e) {
            }
            try {
                MnsUsedAtHIndexed[i].AddMN(mn);
            } catch (std::runtime_error& e) {
            }
        }
        for (const auto& mn : previousQuarters.quarterHMinus2C[i]) {
            if (allMns.IsMNPoSeBanned(mn->proTxHash)) {
                continue;
            }
            try {
                MnsUsedAtH.AddMN(mn);
            } catch (std::runtime_error& e) {
            }
            try {
                MnsUsedAtHIndexed[i].AddMN(mn);
            } catch (std::runtime_error& e) {
            }
        }
        for (const auto& mn : previousQuarters.quarterHMinus3C[i]) {
            if (allMns.IsMNPoSeBanned(mn->proTxHash)) {
                continue;
            }
            try {
                MnsUsedAtH.AddMN(mn);
            } catch (std::runtime_error& e) {
            }
            try {
                MnsUsedAtHIndexed[i].AddMN(mn);
            } catch (std::runtime_error& e) {
            }
        }
    }

    allMns.ForEachMNShared(false, [&MnsUsedAtH, &MnsNotUsedAtH](const CDeterministicMNCPtr& dmn) {
        if (!MnsUsedAtH.HasMN(dmn->proTxHash)) {
            if (!dmn->pdmnState->IsBanned()) {
                try {
                    MnsNotUsedAtH.AddMN(dmn);
                } catch (std::runtime_error &e) {
                }
            }
        }
    });

    auto sortedMnsUsedAtHM = MnsUsedAtH.CalculateQuorum(MnsUsedAtH.GetAllMNsCount(), modifier);
    auto sortedMnsNotUsedAtH = MnsNotUsedAtH.CalculateQuorum(MnsNotUsedAtH.GetAllMNsCount(), modifier);
    auto sortedCombinedMnsList = std::move(sortedMnsNotUsedAtH);
    for (auto& m : sortedMnsUsedAtHM) {
        sortedCombinedMnsList.push_back(std::move(m));
    }

    if (LogAcceptCategory(BCLog::LLMQ)) {
        std::stringstream ss;
        ss << " [";
        for (auto &m: sortedCombinedMnsList) {
            ss << m->proTxHash.ToString().substr(0, 4) << "|";
        }
        ss << "]";
        LogPrint(BCLog::LLMQ, "BuildNewQuorumQuarterMembers h[%d] sortedCombinedMnsList[%s]\n",
                 pQuorumBaseBlockIndex->nHeight, ss.str());
    }

    std::vector<int> skipList;
    int firstSkippedIndex = 0;
    auto idx = 0;
    for (auto i = 0; i < nQuorums; ++i) {
        auto usedMNsCount = MnsUsedAtHIndexed[i].GetAllMNsCount();
        while (quarterQuorumMembers[i].size() < quarterSize && (usedMNsCount + quarterQuorumMembers[i].size() < sortedCombinedMnsList.size())) {
            if (!MnsUsedAtHIndexed[i].HasMN(sortedCombinedMnsList[idx]->proTxHash)) {
                quarterQuorumMembers[i].push_back(sortedCombinedMnsList[idx]);
            } else {
                if (firstSkippedIndex == 0) {
                    firstSkippedIndex = idx;
                    skipList.push_back(idx);
                } else {
                    skipList.push_back(idx - firstSkippedIndex);
                }
            }
            if (++idx == sortedCombinedMnsList.size()) {
                idx = 0;
            }
        }
    }

    CQuorumSnapshot quorumSnapshot = {};

    CLLMQUtils::BuildQuorumSnapshot(llmqParams, allMns, MnsUsedAtH, sortedCombinedMnsList, quorumSnapshot, pQuorumBaseBlockIndex->nHeight, skipList, pQuorumBaseBlockIndex);

    quorumSnapshotManager->StoreSnapshotForBlock(llmqParams.type, pQuorumBaseBlockIndex, quorumSnapshot);

    return quarterQuorumMembers;
}

void CLLMQUtils::BuildQuorumSnapshot(const Consensus::LLMQParams& llmqParams, const CDeterministicMNList& allMns, const CDeterministicMNList& mnUsedAtH, std::vector<CDeterministicMNCPtr>& sortedCombinedMns, CQuorumSnapshot& quorumSnapshot, int nHeight, std::vector<int>& skipList, const CBlockIndex* pQuorumBaseBlockIndex)
{
    quorumSnapshot.activeQuorumMembers.resize(allMns.GetAllMNsCount());
    const CBlockIndex* pWorkBlockIndex = pQuorumBaseBlockIndex->GetAncestor(pQuorumBaseBlockIndex->nHeight - 8);
    auto modifier = ::SerializeHash(std::make_pair(llmqParams.type, pWorkBlockIndex->GetBlockHash()));
    auto sortedAllMns = allMns.CalculateQuorum(allMns.GetAllMNsCount(), modifier);

    LogPrint(BCLog::LLMQ, "BuildQuorumSnapshot h[%d] numMns[%d]\n", pQuorumBaseBlockIndex->nHeight, allMns.GetAllMNsCount());

    std::fill(quorumSnapshot.activeQuorumMembers.begin(),
              quorumSnapshot.activeQuorumMembers.end(),
              false);
    size_t index = {};
    for (const auto& dmn : sortedAllMns) {
        if (mnUsedAtH.HasMN(dmn->proTxHash)) {
            quorumSnapshot.activeQuorumMembers[index] = true;
        }
        index++;
    }

    if (skipList.empty()) {
        quorumSnapshot.mnSkipListMode = SnapshotSkipMode::MODE_NO_SKIPPING;
        quorumSnapshot.mnSkipList.clear();
    } else {
        quorumSnapshot.mnSkipListMode = SnapshotSkipMode::MODE_SKIPPING_ENTRIES;
        quorumSnapshot.mnSkipList = std::move(skipList);
    }
}

std::vector<std::vector<CDeterministicMNCPtr>> CLLMQUtils::GetQuorumQuarterMembersBySnapshot(const Consensus::LLMQParams& llmqParams, const CBlockIndex* pQuorumBaseBlockIndex, const llmq::CQuorumSnapshot& snapshot, int nHeight)
{
    std::vector<CDeterministicMNCPtr> sortedCombinedMns;
    {
        const CBlockIndex* pWorkBlockIndex = pQuorumBaseBlockIndex->GetAncestor(pQuorumBaseBlockIndex->nHeight - 8);
        const auto modifier = ::SerializeHash(std::make_pair(llmqParams.type, pWorkBlockIndex->GetBlockHash()));
        const auto [MnsUsedAtH, MnsNotUsedAtH] = CLLMQUtils::GetMNUsageBySnapshot(llmqParams.type, pQuorumBaseBlockIndex, snapshot, nHeight);
        // the list begins with all the unused MNs
        auto sortedMnsNotUsedAtH = MnsNotUsedAtH.CalculateQuorum(MnsNotUsedAtH.GetAllMNsCount(), modifier);
        sortedCombinedMns = std::move(sortedMnsNotUsedAtH);
        // Now add the already used MNs to the end of the list
        auto sortedMnsUsedAtH = MnsUsedAtH.CalculateQuorum(MnsUsedAtH.GetAllMNsCount(), modifier);
        std::move(sortedMnsUsedAtH.begin(), sortedMnsUsedAtH.end(), std::back_inserter(sortedCombinedMns));
    }

    if (LogAcceptCategory(BCLog::LLMQ)) {
        std::stringstream ss;
        ss << " [";
        for (auto &m: sortedCombinedMns) {
            ss << m->proTxHash.ToString().substr(0, 4) << "|";
        }
        ss << "]";
        LogPrint(BCLog::LLMQ, "GetQuorumQuarterMembersBySnapshot h[%d] from[%d] sortedCombinedMns[%s]\n",
                 pQuorumBaseBlockIndex->nHeight, nHeight, ss.str());
    }

    auto numQuorums = size_t(llmqParams.signingActiveQuorumCount);
    auto quorumSize = size_t(llmqParams.size);
    auto quarterSize = quorumSize / 4;

    std::vector<std::vector<CDeterministicMNCPtr>> quarterQuorumMembers(numQuorums);

    if (sortedCombinedMns.empty()) return quarterQuorumMembers;

    switch (snapshot.mnSkipListMode) {
        case SnapshotSkipMode::MODE_NO_SKIPPING:
        {
            auto itm = sortedCombinedMns.begin();
            for (auto i = 0; i < llmqParams.signingActiveQuorumCount; ++i) {
                while (quarterQuorumMembers[i].size() < quarterSize) {
                    quarterQuorumMembers[i].push_back(*itm);
                    itm++;
                    if (itm == sortedCombinedMns.end()) {
                        itm = sortedCombinedMns.begin();
                    }
                }
            }
            return quarterQuorumMembers;
        }
        case SnapshotSkipMode::MODE_SKIPPING_ENTRIES: // List holds entries to be skipped
        {
            size_t first_entry_index{0};
            std::vector<int> processesdSkipList;
            for (const auto& s : snapshot.mnSkipList) {
                if (first_entry_index == 0) {
                    first_entry_index = s;
                    processesdSkipList.push_back(s);
                } else {
                    processesdSkipList.push_back(first_entry_index + s);
                }
            }

            auto idx = 0;
            auto itsk = processesdSkipList.begin();
            for (auto i = 0; i < llmqParams.signingActiveQuorumCount; ++i) {
                while (quarterQuorumMembers[i].size() < quarterSize) {
                    if (itsk != processesdSkipList.end() && idx == *itsk) {
                        itsk++;
                    } else {
                        quarterQuorumMembers[i].push_back(sortedCombinedMns[idx]);
                    }
                    idx++;
                    if (idx == sortedCombinedMns.size()) {
                        idx = 0;
                    }
                }
            }
            return quarterQuorumMembers;
        }
        case SnapshotSkipMode::MODE_NO_SKIPPING_ENTRIES: // List holds entries to be kept
        case SnapshotSkipMode::MODE_ALL_SKIPPED: // Every node was skipped. Returning empty quarterQuorumMembers
        default:
            return quarterQuorumMembers;
    }
}

std::pair<CDeterministicMNList, CDeterministicMNList> CLLMQUtils::GetMNUsageBySnapshot(Consensus::LLMQType llmqType, const CBlockIndex* pQuorumBaseBlockIndex, const llmq::CQuorumSnapshot& snapshot, int nHeight)
{
    CDeterministicMNList usedMNs;
    CDeterministicMNList nonUsedMNs;
    LOCK(deterministicMNManager->cs);

    const CBlockIndex* pWorkBlockIndex = pQuorumBaseBlockIndex->GetAncestor(pQuorumBaseBlockIndex->nHeight - 8);
    auto modifier = ::SerializeHash(std::make_pair(llmqType, pWorkBlockIndex->GetBlockHash()));

    auto allMns = deterministicMNManager->GetListForBlock(pWorkBlockIndex);
    auto sortedAllMns = allMns.CalculateQuorum(allMns.GetAllMNsCount(), modifier);

    size_t i{0};
    for (const auto& dmn : sortedAllMns) {
        if (snapshot.activeQuorumMembers[i]) {
            try {
                usedMNs.AddMN(dmn);
            } catch (std::runtime_error &e) {
            }
        } else {
            if (!dmn->pdmnState->IsBanned()) {
                try {
                    nonUsedMNs.AddMN(dmn);
                } catch (std::runtime_error &e) {
                }
            }
        }
        i++;
    }

    return std::make_pair(usedMNs, nonUsedMNs);
}

uint256 CLLMQUtils::BuildCommitmentHash(Consensus::LLMQType llmqType, const uint256& blockHash, const std::vector<bool>& validMembers, const CBLSPublicKey& pubKey, const uint256& vvecHash)
{
    CHashWriter hw(SER_NETWORK, 0);
    hw << llmqType;
    hw << blockHash;
    hw << DYNBITSET(validMembers);
    hw << pubKey;
    hw << vvecHash;
    return hw.GetHash();
}

uint256 CLLMQUtils::BuildSignHash(Consensus::LLMQType llmqType, const uint256& quorumHash, const uint256& id, const uint256& msgHash)
{
    CHashWriter h(SER_GETHASH, 0);
    h << llmqType;
    h << quorumHash;
    h << id;
    h << msgHash;
    return h.GetHash();
}

static bool EvalSpork(Consensus::LLMQType llmqType, int64_t spork_value)
{
    if (spork_value == 0) {
        return true;
    }
    if (spork_value == 1 && llmqType != Consensus::LLMQType::LLMQ_100_67 && llmqType != Consensus::LLMQType::LLMQ_400_60 && llmqType != Consensus::LLMQType::LLMQ_400_85) {
        return true;
    }
    return false;
}

bool CLLMQUtils::IsAllMembersConnectedEnabled(Consensus::LLMQType llmqType)
{
    return EvalSpork(llmqType, sporkManager.GetSporkValue(SPORK_21_QUORUM_ALL_CONNECTED));
}

bool CLLMQUtils::IsQuorumPoseEnabled(Consensus::LLMQType llmqType)
{
    return EvalSpork(llmqType, sporkManager.GetSporkValue(SPORK_23_QUORUM_POSE));
}

bool CLLMQUtils::IsQuorumRotationEnabled(Consensus::LLMQType llmqType, const CBlockIndex* pindex)
{
    assert(pindex);

    if (!GetLLMQParams(llmqType).useRotation) {
        return false;
    }

    LOCK(cs_llmq_vbc);
    int cycleQuorumBaseHeight = pindex->nHeight - (pindex->nHeight % GetLLMQParams(llmqType).dkgInterval);
    if (cycleQuorumBaseHeight < 1) {
        return false;
    }
    // It should activate at least 1 block prior to the cycle start
    return CLLMQUtils::IsDIP0024Active(pindex->GetAncestor(cycleQuorumBaseHeight - 1));
}

Consensus::LLMQType CLLMQUtils::GetInstantSendLLMQType(const CBlockIndex* pindex)
{
    if (IsDIP0024Active(pindex) && !quorumManager->ScanQuorums(Params().GetConsensus().llmqTypeDIP0024InstantSend, pindex, 1).empty()) {
        return Params().GetConsensus().llmqTypeDIP0024InstantSend;
    }
    return Params().GetConsensus().llmqTypeInstantSend;
}

Consensus::LLMQType CLLMQUtils::GetInstantSendLLMQType(bool deterministic)
{
    return deterministic ? Params().GetConsensus().llmqTypeDIP0024InstantSend : Params().GetConsensus().llmqTypeInstantSend;
}

bool CLLMQUtils::IsDIP0024Active(const CBlockIndex* pindex)
{
    assert(pindex);

    LOCK(cs_llmq_vbc);
    return VersionBitsState(pindex, Params().GetConsensus(), Consensus::DEPLOYMENT_DIP0024, llmq_versionbitscache) == ThresholdState::ACTIVE;
}

bool CLLMQUtils::IsInstantSendLLMQTypeShared()
{
    if (Params().GetConsensus().llmqTypeInstantSend == Params().GetConsensus().llmqTypeChainLocks ||
        Params().GetConsensus().llmqTypeInstantSend == Params().GetConsensus().llmqTypePlatform ||
        Params().GetConsensus().llmqTypeInstantSend == Params().GetConsensus().llmqTypeMnhf) {
        return true;
    }

    return false;
}

uint256 CLLMQUtils::DeterministicOutboundConnection(const uint256& proTxHash1, const uint256& proTxHash2)
{
    // We need to deterministically select who is going to initiate the connection. The naive way would be to simply
    // return the min(proTxHash1, proTxHash2), but this would create a bias towards MNs with a numerically low
    // hash. To fix this, we return the proTxHash that has the lowest value of:
    //   hash(min(proTxHash1, proTxHash2), max(proTxHash1, proTxHash2), proTxHashX)
    // where proTxHashX is the proTxHash to compare
    uint256 h1;
    uint256 h2;
    if (proTxHash1 < proTxHash2) {
        h1 = ::SerializeHash(std::make_tuple(proTxHash1, proTxHash2, proTxHash1));
        h2 = ::SerializeHash(std::make_tuple(proTxHash1, proTxHash2, proTxHash2));
    } else {
        h1 = ::SerializeHash(std::make_tuple(proTxHash2, proTxHash1, proTxHash1));
        h2 = ::SerializeHash(std::make_tuple(proTxHash2, proTxHash1, proTxHash2));
    }
    if (h1 < h2) {
        return proTxHash1;
    }
    return proTxHash2;
}

std::set<uint256> CLLMQUtils::GetQuorumConnections(const Consensus::LLMQParams& llmqParams, const CBlockIndex* pQuorumBaseBlockIndex, const uint256& forMember, bool onlyOutbound)
{
    if (IsAllMembersConnectedEnabled(llmqParams.type)) {
        auto mns = GetAllQuorumMembers(llmqParams.type, pQuorumBaseBlockIndex);
        std::set<uint256> result;

        for (const auto& dmn : mns) {
            if (dmn->proTxHash == forMember) {
                continue;
            }
            // Determine which of the two MNs (forMember vs dmn) should initiate the outbound connection and which
            // one should wait for the inbound connection. We do this in a deterministic way, so that even when we
            // end up with both connecting to each other, we know which one to disconnect
            uint256 deterministicOutbound = DeterministicOutboundConnection(forMember, dmn->proTxHash);
            if (!onlyOutbound || deterministicOutbound == dmn->proTxHash) {
                result.emplace(dmn->proTxHash);
            }
        }
        return result;
    } else {
        return GetQuorumRelayMembers(llmqParams, pQuorumBaseBlockIndex, forMember, onlyOutbound);
    }
}

std::set<uint256> CLLMQUtils::GetQuorumRelayMembers(const Consensus::LLMQParams& llmqParams, const CBlockIndex* pQuorumBaseBlockIndex, const uint256& forMember, bool onlyOutbound)
{
    auto mns = GetAllQuorumMembers(llmqParams.type, pQuorumBaseBlockIndex);
    std::set<uint256> result;

    if (sporkManager.IsSporkActive(SPORK_21_QUORUM_ALL_CONNECTED)) {
        for (auto& dmn : mns) {
            // this will cause deterministic behaviour between incoming and outgoing connections.
            // Each member needs a connection to all other members, so we have each member paired. The below check
            // will be true on one side and false on the other side of the pairing, so we avoid having both members
            // initiating the connection.
            if (dmn->proTxHash < forMember) {
                result.emplace(dmn->proTxHash);
            }
        }
        return result;
    }

    // TODO remove this after activation of SPORK_21_QUORUM_ALL_CONNECTED

    auto calcOutbound = [&](size_t i, const uint256& proTxHash) {
        if (mns.size() == 1) {
            // No outbound connections are needed when there is one MN only.
            // Also note that trying to calculate results via the algorithm below
            // would result in an endless loop.
            return std::set<uint256>();
        }
        // Relay to nodes at indexes (i+2^k)%n, where
        //   k: 0..max(1, floor(log2(n-1))-1)
        //   n: size of the quorum/ring
        std::set<uint256> r;
        int gap = 1;
        int gap_max = (int)mns.size() - 1;
        int k = 0;
        while ((gap_max >>= 1) || k <= 1) {
            size_t idx = (i + gap) % mns.size();
            // It doesn't matter if this node is going to be added to the resulting set or not,
            // we should always bump the gap and the k (step count) regardless.
            // Refusing to bump the gap results in an incomplete set in the best case scenario
            // (idx won't ever change again once we hit `==`). Not bumping k guarantees an endless
            // loop when the first or the second node we check is the one that should be skipped
            // (k <= 1 forever).
            gap <<= 1;
            k++;
            const auto& otherDmn = mns[idx];
            if (otherDmn->proTxHash == proTxHash) {
                continue;
            }
            r.emplace(otherDmn->proTxHash);
        }
        return r;
    };

    for (size_t i = 0; i < mns.size(); i++) {
        const auto& dmn = mns[i];
        if (dmn->proTxHash == forMember) {
            auto r = calcOutbound(i, dmn->proTxHash);
            result.insert(r.begin(), r.end());
        } else if (!onlyOutbound) {
            auto r = calcOutbound(i, dmn->proTxHash);
            if (r.count(forMember)) {
                result.emplace(dmn->proTxHash);
            }
        }
    }

    return result;
}

std::set<size_t> CLLMQUtils::CalcDeterministicWatchConnections(Consensus::LLMQType llmqType, const CBlockIndex* pQuorumBaseBlockIndex, size_t memberCount, size_t connectionCount)
{
    static uint256 qwatchConnectionSeed;
    static std::atomic<bool> qwatchConnectionSeedGenerated{false};
    static CCriticalSection qwatchConnectionSeedCs;
    if (!qwatchConnectionSeedGenerated) {
        LOCK(qwatchConnectionSeedCs);
        qwatchConnectionSeed = GetRandHash();
        qwatchConnectionSeedGenerated = true;
    }

    std::set<size_t> result;
    uint256 rnd = qwatchConnectionSeed;
    for (size_t i = 0; i < connectionCount; i++) {
        rnd = ::SerializeHash(std::make_pair(rnd, std::make_pair(llmqType, pQuorumBaseBlockIndex->GetBlockHash())));
        result.emplace(rnd.GetUint64(0) % memberCount);
    }
    return result;
}

bool CLLMQUtils::EnsureQuorumConnections(const Consensus::LLMQParams& llmqParams, const CBlockIndex* pQuorumBaseBlockIndex, const uint256& myProTxHash)
{
    if (!fMasternodeMode && !CLLMQUtils::IsWatchQuorumsEnabled()) {
        return false;
    }

    auto members = GetAllQuorumMembers(llmqParams.type, pQuorumBaseBlockIndex);
    if (members.empty()) {
        return false;
    }

    bool isMember = std::find_if(members.begin(), members.end(), [&](const auto& dmn) { return dmn->proTxHash == myProTxHash; }) != members.end();

    if (!isMember && !CLLMQUtils::IsWatchQuorumsEnabled()) {
        return false;
    }

    LogPrint(BCLog::NET_NETCONN, "CLLMQUtils::%s -- isMember=%d for quorum %s:\n",
            __func__, isMember, pQuorumBaseBlockIndex->GetBlockHash().ToString());

    std::set<uint256> connections;
    std::set<uint256> relayMembers;
    if (isMember) {
        connections = CLLMQUtils::GetQuorumConnections(llmqParams, pQuorumBaseBlockIndex, myProTxHash, true);
        relayMembers = CLLMQUtils::GetQuorumRelayMembers(llmqParams, pQuorumBaseBlockIndex, myProTxHash, true);
    } else {
        auto cindexes = CLLMQUtils::CalcDeterministicWatchConnections(llmqParams.type, pQuorumBaseBlockIndex, members.size(), 1);
        for (auto idx : cindexes) {
            connections.emplace(members[idx]->proTxHash);
        }
        relayMembers = connections;
    }
    if (!connections.empty()) {
        if (!g_connman->HasMasternodeQuorumNodes(llmqParams.type, pQuorumBaseBlockIndex->GetBlockHash()) && LogAcceptCategory(BCLog::LLMQ)) {
            auto mnList = deterministicMNManager->GetListAtChainTip();
            std::string debugMsg = strprintf("CLLMQUtils::%s -- adding masternodes quorum connections for quorum %s:\n", __func__, pQuorumBaseBlockIndex->GetBlockHash().ToString());
            for (auto& c : connections) {
                auto dmn = mnList.GetValidMN(c);
                if (!dmn) {
                    debugMsg += strprintf("  %s (not in valid MN set anymore)\n", c.ToString());
                } else {
                    debugMsg += strprintf("  %s (%s)\n", c.ToString(), dmn->pdmnState->addr.ToString(false));
                }
            }
            LogPrint(BCLog::NET_NETCONN, debugMsg.c_str()); /* Continued */
        }
        g_connman->SetMasternodeQuorumNodes(llmqParams.type, pQuorumBaseBlockIndex->GetBlockHash(), connections);
    }
    if (!relayMembers.empty()) {
        g_connman->SetMasternodeQuorumRelayMembers(llmqParams.type, pQuorumBaseBlockIndex->GetBlockHash(), relayMembers);
    }
    return true;
}

void CLLMQUtils::AddQuorumProbeConnections(const Consensus::LLMQParams& llmqParams, const CBlockIndex *pQuorumBaseBlockIndex, const uint256 &myProTxHash)
{
    if (!CLLMQUtils::IsQuorumPoseEnabled(llmqParams.type)) {
        return;
    }

    auto members = GetAllQuorumMembers(llmqParams.type, pQuorumBaseBlockIndex);
    auto curTime = GetAdjustedTime();

    std::set<uint256> probeConnections;
    for (const auto& dmn : members) {
        if (dmn->proTxHash == myProTxHash) {
            continue;
        }
        auto lastOutbound = mmetaman.GetMetaInfo(dmn->proTxHash)->GetLastOutboundSuccess();
        if (curTime - lastOutbound < 10 * 60) {
            // avoid re-probing nodes too often
            continue;
        }
        probeConnections.emplace(dmn->proTxHash);
    }

    if (!probeConnections.empty()) {
        if (LogAcceptCategory(BCLog::LLMQ)) {
            auto mnList = deterministicMNManager->GetListAtChainTip();
            std::string debugMsg = strprintf("CLLMQUtils::%s -- adding masternodes probes for quorum %s:\n", __func__, pQuorumBaseBlockIndex->GetBlockHash().ToString());
            for (auto& c : probeConnections) {
                auto dmn = mnList.GetValidMN(c);
                if (!dmn) {
                    debugMsg += strprintf("  %s (not in valid MN set anymore)\n", c.ToString());
                } else {
                    debugMsg += strprintf("  %s (%s)\n", c.ToString(), dmn->pdmnState->addr.ToString(false));
                }
            }
            LogPrint(BCLog::NET_NETCONN, debugMsg.c_str()); /* Continued */
        }
        g_connman->AddPendingProbeConnections(probeConnections);
    }
}

bool CLLMQUtils::IsQuorumActive(Consensus::LLMQType llmqType, const uint256& quorumHash)
{
    // sig shares and recovered sigs are only accepted from recent/active quorums
    // we allow one more active quorum as specified in consensus, as otherwise there is a small window where things could
    // fail while we are on the brink of a new quorum
    auto quorums = quorumManager->ScanQuorums(llmqType, GetLLMQParams(llmqType).keepOldConnections);
    return ranges::any_of(quorums, [&quorumHash](const auto& q){ return q->qc->quorumHash == quorumHash; });
}

bool CLLMQUtils::IsQuorumTypeEnabled(Consensus::LLMQType llmqType, const CBlockIndex* pindex)
{
    return IsQuorumTypeEnabledInternal(llmqType, pindex, std::nullopt, std::nullopt);
}

bool CLLMQUtils::IsQuorumTypeEnabledInternal(Consensus::LLMQType llmqType, const CBlockIndex* pindex, std::optional<bool> optDIP0024IsActive, std::optional<bool> optHaveDIP0024Quorums)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();

    switch (llmqType)
    {
        case Consensus::LLMQType::LLMQ_TEST_INSTANTSEND:
        case Consensus::LLMQType::LLMQ_DEVNET:
        case Consensus::LLMQType::LLMQ_50_60: {
            if (IsInstantSendLLMQTypeShared()) {
                break;
            }
            bool fDIP0024IsActive = optDIP0024IsActive.has_value() ? *optDIP0024IsActive : CLLMQUtils::IsDIP0024Active(pindex);
            if (fDIP0024IsActive) {
                bool fHaveDIP0024Quorums = optHaveDIP0024Quorums.has_value() ? *optHaveDIP0024Quorums
                                                                             : !quorumManager->ScanQuorums(
                                consensusParams.llmqTypeDIP0024InstantSend, pindex, 1).empty();
                if (fHaveDIP0024Quorums) {
                    return false;
                }
            }
            break;
        }
        case Consensus::LLMQType::LLMQ_TEST:
        case Consensus::LLMQType::LLMQ_400_60:
        case Consensus::LLMQType::LLMQ_400_85:
            break;
        case Consensus::LLMQType::LLMQ_100_67:
        case Consensus::LLMQType::LLMQ_TEST_V17:
            if (LOCK(cs_llmq_vbc); VersionBitsState(pindex, consensusParams, Consensus::DEPLOYMENT_DIP0020, llmq_versionbitscache) != ThresholdState::ACTIVE) {
                return false;
            }
            break;
        case Consensus::LLMQType::LLMQ_60_75:
        case Consensus::LLMQType::LLMQ_DEVNET_DIP0024:
        case Consensus::LLMQType::LLMQ_TEST_DIP0024: {
            bool fDIP0024IsActive = optDIP0024IsActive.has_value() ? *optDIP0024IsActive : CLLMQUtils::IsDIP0024Active(pindex);
            if (!fDIP0024IsActive) {
                return false;
            }
            break;
        }
        default:
            throw std::runtime_error(strprintf("%s: Unknown LLMQ type %d", __func__, static_cast<uint8_t>(llmqType)));
    }

    return true;
}

std::vector<Consensus::LLMQType> CLLMQUtils::GetEnabledQuorumTypes(const CBlockIndex* pindex)
{
    std::vector<Consensus::LLMQType> ret;
    ret.reserve(Params().GetConsensus().llmqs.size());
    for (const auto& params : Params().GetConsensus().llmqs) {
        if (IsQuorumTypeEnabled(params.type, pindex)) {
            ret.push_back(params.type);
        }
    }
    return ret;
}

std::vector<std::reference_wrapper<const Consensus::LLMQParams>> CLLMQUtils::GetEnabledQuorumParams(const CBlockIndex* pindex)
{
    std::vector<std::reference_wrapper<const Consensus::LLMQParams>> ret;
    ret.reserve(Params().GetConsensus().llmqs.size());

    std::copy_if(Params().GetConsensus().llmqs.begin(), Params().GetConsensus().llmqs.end(), std::back_inserter(ret),
                 [&pindex](const auto& params){return IsQuorumTypeEnabled(params.type, pindex);});

    return ret;
}

bool CLLMQUtils::QuorumDataRecoveryEnabled()
{
    return gArgs.GetBoolArg("-llmq-data-recovery", DEFAULT_ENABLE_QUORUM_DATA_RECOVERY);
}

bool CLLMQUtils::IsWatchQuorumsEnabled()
{
    static bool fIsWatchQuroumsEnabled = gArgs.GetBoolArg("-watchquorums", DEFAULT_WATCH_QUORUMS);
    return fIsWatchQuroumsEnabled;
}

std::map<Consensus::LLMQType, QvvecSyncMode> CLLMQUtils::GetEnabledQuorumVvecSyncEntries()
{
    std::map<Consensus::LLMQType, QvvecSyncMode> mapQuorumVvecSyncEntries;
    for (const auto& strEntry : gArgs.GetArgs("-llmq-qvvec-sync")) {
        Consensus::LLMQType llmqType = Consensus::LLMQType::LLMQ_NONE;
        QvvecSyncMode mode{QvvecSyncMode::Invalid};
        std::istringstream ssEntry(strEntry);
        std::string strLLMQType, strMode, strTest;
        const bool fLLMQTypePresent = std::getline(ssEntry, strLLMQType, ':') && strLLMQType != "";
        const bool fModePresent = std::getline(ssEntry, strMode, ':') && strMode != "";
        const bool fTooManyEntries = static_cast<bool>(std::getline(ssEntry, strTest, ':'));
        if (!fLLMQTypePresent || !fModePresent || fTooManyEntries) {
            throw std::invalid_argument(strprintf("Invalid format in -llmq-qvvec-sync: %s", strEntry));
        }

        if (auto optLLMQParams = ranges::find_if_opt(Params().GetConsensus().llmqs,
                                                     [&strLLMQType](const auto& params){return params.name == strLLMQType;})) {
            llmqType = optLLMQParams->type;
        } else {
            throw std::invalid_argument(strprintf("Invalid llmqType in -llmq-qvvec-sync: %s", strEntry));
        }
        if (mapQuorumVvecSyncEntries.count(llmqType) > 0) {
            throw std::invalid_argument(strprintf("Duplicated llmqType in -llmq-qvvec-sync: %s", strEntry));
        }

        int32_t nMode;
        if (ParseInt32(strMode, &nMode)) {
            switch (nMode) {
            case (int32_t)QvvecSyncMode::Always:
                mode = QvvecSyncMode::Always;
                break;
            case (int32_t)QvvecSyncMode::OnlyIfTypeMember:
                mode = QvvecSyncMode::OnlyIfTypeMember;
                break;
            default:
                mode = QvvecSyncMode::Invalid;
                break;
            }
        }
        if (mode == QvvecSyncMode::Invalid) {
            throw std::invalid_argument(strprintf("Invalid mode in -llmq-qvvec-sync: %s", strEntry));
        }
        mapQuorumVvecSyncEntries.emplace(llmqType, mode);
    }
    return mapQuorumVvecSyncEntries;
}

template <typename CacheType>
void CLLMQUtils::InitQuorumsCache(CacheType& cache)
{
    for (auto& llmq : Params().GetConsensus().llmqs) {
        cache.emplace(std::piecewise_construct, std::forward_as_tuple(llmq.type),
                      std::forward_as_tuple(llmq.keepOldConnections));
    }
}
template void CLLMQUtils::InitQuorumsCache<std::map<Consensus::LLMQType, unordered_lru_cache<uint256, bool, StaticSaltedHasher>>>(std::map<Consensus::LLMQType, unordered_lru_cache<uint256, bool, StaticSaltedHasher>>& cache);
template void CLLMQUtils::InitQuorumsCache<std::map<Consensus::LLMQType, unordered_lru_cache<uint256, std::vector<CQuorumCPtr>, StaticSaltedHasher>>>(std::map<Consensus::LLMQType, unordered_lru_cache<uint256, std::vector<CQuorumCPtr>, StaticSaltedHasher>>& cache);
template void CLLMQUtils::InitQuorumsCache<std::map<Consensus::LLMQType, unordered_lru_cache<uint256, std::shared_ptr<llmq::CQuorum>, StaticSaltedHasher, 0ul, 0ul>, std::less<Consensus::LLMQType>, std::allocator<std::pair<Consensus::LLMQType const, unordered_lru_cache<uint256, std::shared_ptr<llmq::CQuorum>, StaticSaltedHasher, 0ul, 0ul>>>>>(std::map<Consensus::LLMQType, unordered_lru_cache<uint256, std::shared_ptr<llmq::CQuorum>, StaticSaltedHasher, 0ul, 0ul>, std::less<Consensus::LLMQType>, std::allocator<std::pair<Consensus::LLMQType const, unordered_lru_cache<uint256, std::shared_ptr<llmq::CQuorum>, StaticSaltedHasher, 0ul, 0ul>>>>&);
template void CLLMQUtils::InitQuorumsCache<std::map<Consensus::LLMQType, unordered_lru_cache<uint256, int, StaticSaltedHasher>>>(std::map<Consensus::LLMQType, unordered_lru_cache<uint256, int, StaticSaltedHasher>>& cache);

const Consensus::LLMQParams& GetLLMQParams(Consensus::LLMQType llmqType)
{
    return Params().GetLLMQ(llmqType);
}

} // namespace llmq
