// Copyright (c) 2014-2022 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <spork.h>

#include <chainparams.h>
#include <consensus/params.h>
#include <key_io.h>
#include <logging.h>
#include <messagesigner.h>
#include <net.h>
#include <net_processing.h>
#include <netmessagemaker.h>
#include <primitives/block.h>
#include <protocol.h>
#include <script/standard.h>
#include <timedata.h>
#include <util/ranges.h>
#include <util/validation.h> // for strMessageMagic

#include <string>

CSporkManager sporkManager;

bool CSporkManager::SporkValueIsActive(SporkId nSporkID, int64_t& nActiveValueRet) const
{
    AssertLockHeld(cs);

    if (!mapSporksActive.count(nSporkID)) return false;

    {
        LOCK(cs_mapSporksCachedValues);
        if (auto it = mapSporksCachedValues.find(nSporkID); it != mapSporksCachedValues.end()) {
            nActiveValueRet = it->second;
            return true;
        }
    }

    // calc how many values we have and how many signers vote for every value
    std::unordered_map<int64_t, int> mapValueCounts;
    for (const auto& [_, spork] : mapSporksActive.at(nSporkID)) {
        mapValueCounts[spork.nValue]++;
        if (mapValueCounts.at(spork.nValue) >= nMinSporkKeys) {
            // nMinSporkKeys is always more than the half of the max spork keys number,
            // so there is only one such value and we can stop here
            nActiveValueRet = spork.nValue;
            WITH_LOCK(cs_mapSporksCachedValues, mapSporksCachedValues[nSporkID] = nActiveValueRet);
            return true;
        }
    }

    return false;
}

void CSporkManager::Clear()
{
    LOCK(cs);
    mapSporksActive.clear();
    mapSporksByHash.clear();
    // sporkPubKeyID and sporkPrivKey should be set in init.cpp,
    // we should not alter them here.
}

void CSporkManager::CheckAndRemove()
{
    LOCK(cs);
    assert(!setSporkPubKeyIDs.empty());

    for (auto itActive = mapSporksActive.begin(); itActive != mapSporksActive.end();) {
        auto itSignerPair = itActive->second.begin();
        while (itSignerPair != itActive->second.end()) {
            bool fHasValidSig = setSporkPubKeyIDs.find(itSignerPair->first) != setSporkPubKeyIDs.end() &&
                                itSignerPair->second.CheckSignature(itSignerPair->first);
            if (!fHasValidSig) {
                mapSporksByHash.erase(itSignerPair->second.GetHash());
                itActive->second.erase(itSignerPair++);
                continue;
            }
            ++itSignerPair;
        }
        if (itActive->second.empty()) {
            mapSporksActive.erase(itActive++);
            continue;
        }
        ++itActive;
    }

    for (auto itByHash = mapSporksByHash.begin(); itByHash != mapSporksByHash.end();) {
        bool found = false;
        for (const auto& signer : setSporkPubKeyIDs) {
            if (itByHash->second.CheckSignature(signer)) {
                found = true;
                break;
            }
        }
        if (!found) {
            mapSporksByHash.erase(itByHash++);
            continue;
        }
        ++itByHash;
    }
}

void CSporkManager::ProcessSporkMessages(CNode* pfrom, std::string_view strCommand, CDataStream& vRecv, CConnman& connman)
{
    ProcessSpork(pfrom, strCommand, vRecv, connman);
    ProcessGetSporks(pfrom, strCommand, connman);
}

void CSporkManager::ProcessSpork(const CNode* pfrom, std::string_view strCommand, CDataStream& vRecv, CConnman& connman)
{
    if (strCommand != NetMsgType::SPORK) return;
    // TODO: Remove it after fork
    if (pfrom->nVersion < MIN_SPORK_PROTO_VERSION) return; // Don't acceps spork messages from outdated network clients

    CSporkMessage spork;
    vRecv >> spork;

    uint256 hash = spork.GetHash();

    std::string strLogMsg;
    {
        LOCK(cs_main);
        EraseObjectRequest(pfrom->GetId(), CInv(MSG_SPORK, hash));
        if (!::ChainActive().Tip()) return;
        strLogMsg = strprintf("SPORK -- hash: %s id: %d value: %10d bestHeight: %d peer=%d", hash.ToString(), spork.nSporkID, spork.nValue, ::ChainActive().Height(), pfrom->GetId());
    }

    if (spork.nTimeSigned > GetAdjustedTime() + 2 * 60 * 60) {
        LOCK(cs_main);
        LogPrint(BCLog::SPORK, "CSporkManager::ProcessSpork -- ERROR: too far into the future\n");
        Misbehaving(pfrom->GetId(), 100);
        return;
    }

    CKeyID keyIDSigner;

    if (!spork.GetSignerKeyID(keyIDSigner) || WITH_LOCK(cs, return !setSporkPubKeyIDs.count(keyIDSigner))) {
        LOCK(cs_main);
        LogPrint(BCLog::SPORK, "CSporkManager::ProcessSpork -- ERROR: invalid signature\n");
        Misbehaving(pfrom->GetId(), 100);
        return;
    }

    {
        LOCK(cs); // make sure to not lock this together with cs_main
        if (mapSporksActive.count(spork.nSporkID)) {
            if (mapSporksActive[spork.nSporkID].count(keyIDSigner)) {
                if (mapSporksActive[spork.nSporkID][keyIDSigner].nTimeSigned >= spork.nTimeSigned) {
                    LogPrint(BCLog::SPORK, "%s seen\n", strLogMsg);
                    return;
                } else {
                    LogPrintf("%s updated\n", strLogMsg);
                }
            } else {
                LogPrintf("%s new signer\n", strLogMsg);
            }
        } else {
            LogPrintf("%s new\n", strLogMsg);
        }
    }


    {
        LOCK(cs); // make sure to not lock this together with cs_main
        mapSporksByHash[hash] = spork;
        mapSporksActive[spork.nSporkID][keyIDSigner] = spork;
        // Clear cached values on new spork being processed
        WITH_LOCK(cs_mapSporksCachedActive, mapSporksCachedActive.erase(spork.nSporkID));
        WITH_LOCK(cs_mapSporksCachedValues, mapSporksCachedValues.erase(spork.nSporkID));
    }
    spork.Relay(connman);
}

void CSporkManager::ProcessGetSporks(CNode* pfrom, std::string_view strCommand, CConnman& connman)
{
    if (strCommand != NetMsgType::GETSPORKS) return;

    LOCK(cs); // make sure to not lock this together with cs_main
    for (const auto& pair : mapSporksActive) {
        for (const auto& signerSporkPair : pair.second) {
            connman.PushMessage(pfrom, CNetMsgMaker(pfrom->GetSendVersion()).Make(NetMsgType::SPORK, signerSporkPair.second));
        }
    }
}


bool CSporkManager::UpdateSpork(SporkId nSporkID, int64_t nValue, CConnman& connman)
{
    CSporkMessage spork(nSporkID, nValue, GetAdjustedTime());

    {
        LOCK(cs);

        if (!spork.Sign(sporkPrivKey)) {
            LogPrintf("CSporkManager::%s -- ERROR: signing failed for spork %d\n", __func__, nSporkID);
            return false;
        }

        CKeyID keyIDSigner;
        if (!spork.GetSignerKeyID(keyIDSigner) || !setSporkPubKeyIDs.count(keyIDSigner)) {
            LogPrintf("CSporkManager::UpdateSpork: failed to find keyid for private key\n");
            return false;
        }

        LogPrintf("CSporkManager::%s -- signed %d %s\n", __func__, nSporkID, spork.GetHash().ToString());

        mapSporksByHash[spork.GetHash()] = spork;
        mapSporksActive[nSporkID][keyIDSigner] = spork;
        // Clear cached values on new spork being processed
        WITH_LOCK(cs_mapSporksCachedActive, mapSporksCachedActive.erase(spork.nSporkID));
        WITH_LOCK(cs_mapSporksCachedValues, mapSporksCachedValues.erase(spork.nSporkID));
    }

    spork.Relay(connman);
    return true;
}

bool CSporkManager::IsSporkActive(SporkId nSporkID) const
{
    // If nSporkID is cached, and the cached value is true, then return early true
    {
        LOCK(cs_mapSporksCachedActive);
        if (auto it = mapSporksCachedActive.find(nSporkID); it != mapSporksCachedActive.end() && it->second) {
            return true;
        }
    }

    int64_t nSporkValue = GetSporkValue(nSporkID);
    // Get time is somewhat costly it looks like
    bool ret = nSporkValue < GetAdjustedTime();
    // Only cache true values
    if (ret) {
        LOCK(cs_mapSporksCachedActive);
        mapSporksCachedActive[nSporkID] = ret;
    }
    return ret;
}

int64_t CSporkManager::GetSporkValue(SporkId nSporkID) const
{
    LOCK(cs);

    if (int64_t nSporkValue = -1; SporkValueIsActive(nSporkID, nSporkValue)) {
        return nSporkValue;
    }


    if (auto optSpork = ranges::find_if_opt(sporkDefs,
                                            [&nSporkID](const auto& sporkDef){return sporkDef.sporkId == nSporkID;})) {
        return optSpork->defaultValue;
    } else {
        LogPrint(BCLog::SPORK, "CSporkManager::GetSporkValue -- Unknown Spork ID %d\n", nSporkID);
        return -1;
    }
}

SporkId CSporkManager::GetSporkIDByName(std::string_view strName)
{
    if (auto optSpork = ranges::find_if_opt(sporkDefs,
                                            [&strName](const auto& sporkDef){return sporkDef.name == strName;})) {
        return optSpork->sporkId;
    }

    LogPrint(BCLog::SPORK, "CSporkManager::GetSporkIDByName -- Unknown Spork name '%s'\n", strName);
    return SPORK_INVALID;
}

bool CSporkManager::GetSporkByHash(const uint256& hash, CSporkMessage& sporkRet) const
{
    LOCK(cs);

    const auto it = mapSporksByHash.find(hash);

    if (it == mapSporksByHash.end())
        return false;

    sporkRet = it->second;

    return true;
}

bool CSporkManager::SetSporkAddress(const std::string& strAddress)
{
    LOCK(cs);
    CTxDestination dest = DecodeDestination(strAddress);
    const CKeyID* keyID = boost::get<CKeyID>(&dest);
    if (!keyID) {
        LogPrintf("CSporkManager::SetSporkAddress -- Failed to parse spork address\n");
        return false;
    }
    setSporkPubKeyIDs.insert(*keyID);
    return true;
}

bool CSporkManager::SetMinSporkKeys(int minSporkKeys)
{
    LOCK(cs);
    if (int maxKeysNumber = setSporkPubKeyIDs.size(); (minSporkKeys <= maxKeysNumber / 2) || (minSporkKeys > maxKeysNumber)) {
        LogPrintf("CSporkManager::SetMinSporkKeys -- Invalid min spork signers number: %d\n", minSporkKeys);
        return false;
    }
    nMinSporkKeys = minSporkKeys;
    return true;
}

bool CSporkManager::SetPrivKey(const std::string& strPrivKey)
{
    CKey key;
    CPubKey pubKey;
    if (!CMessageSigner::GetKeysFromSecret(strPrivKey, key, pubKey)) {
        LogPrintf("CSporkManager::SetPrivKey -- Failed to parse private key\n");
        return false;
    }

    LOCK(cs);
    if (setSporkPubKeyIDs.find(pubKey.GetID()) == setSporkPubKeyIDs.end()) {
        LogPrintf("CSporkManager::SetPrivKey -- New private key does not belong to spork addresses\n");
        return false;
    }

    if (!CSporkMessage().Sign(key)) {
        LogPrintf("CSporkManager::SetPrivKey -- Test signing failed\n");
        return false;
    }

    // Test signing successful, proceed
    LogPrintf("CSporkManager::SetPrivKey -- Successfully initialized as spork signer\n");
    sporkPrivKey = key;
    return true;
}

std::string CSporkManager::ToString() const
{
    LOCK(cs);
    return strprintf("Sporks: %llu", mapSporksActive.size());
}

uint256 CSporkMessage::GetHash() const
{
    return SerializeHash(*this);
}

uint256 CSporkMessage::GetSignatureHash() const
{
    CHashWriter s(SER_GETHASH, 0);
    s << nSporkID;
    s << nValue;
    s << nTimeSigned;
    return s.GetHash();
}

bool CSporkMessage::Sign(const CKey& key)
{
    if (!key.IsValid()) {
        LogPrintf("CSporkMessage::Sign -- signing key is not valid\n");
        return false;
    }

    CKeyID pubKeyId = key.GetPubKey().GetID();

    // Harden Spork6 so that it is active on testnet and no other networks
    if (std::string strError; Params().NetworkIDString() == CBaseChainParams::TESTNET) {
        uint256 hash = GetSignatureHash();

        if (!CHashSigner::SignHash(hash, key, vchSig)) {
            LogPrintf("CSporkMessage::Sign -- SignHash() failed\n");
            return false;
        }

        if (!CHashSigner::VerifyHash(hash, pubKeyId, vchSig, strError)) {
            LogPrintf("CSporkMessage::Sign -- VerifyHash() failed, error: %s\n", strError);
            return false;
        }
    } else {
        std::string strMessage = std::to_string(nSporkID) + std::to_string(nValue) + std::to_string(nTimeSigned);

        if (!CMessageSigner::SignMessage(strMessage, vchSig, key)) {
            LogPrintf("CSporkMessage::Sign -- SignMessage() failed\n");
            return false;
        }

        if (!CMessageSigner::VerifyMessage(pubKeyId, vchSig, strMessage, strError)) {
            LogPrintf("CSporkMessage::Sign -- VerifyMessage() failed, error: %s\n", strError);
            return false;
        }
    }

    return true;
}

bool CSporkMessage::CheckSignature(const CKeyID& pubKeyId) const
{
    // Harden Spork6 so that it is active on testnet and no other networks
    if (std::string strError; Params().NetworkIDString() == CBaseChainParams::TESTNET) {
        uint256 hash = GetSignatureHash();

        if (!CHashSigner::VerifyHash(hash, pubKeyId, vchSig, strError)) {
            LogPrint(BCLog::SPORK, "CSporkMessage::CheckSignature -- VerifyHash() failed, error: %s\n", strError);
            return false;
        }
    } else {
        std::string strMessage = std::to_string(nSporkID) + std::to_string(nValue) + std::to_string(nTimeSigned);

        if (!CMessageSigner::VerifyMessage(pubKeyId, vchSig, strMessage, strError)) {
            LogPrint(BCLog::SPORK, "CSporkMessage::CheckSignature -- VerifyMessage() failed, error: %s\n", strError);
            return false;
        }
    }

    return true;
}

bool CSporkMessage::GetSignerKeyID(CKeyID& retKeyidSporkSigner) const
{
    CPubKey pubkeyFromSig;
    // Harden Spork6 so that it is active on testnet and no other networks
    if (Params().NetworkIDString() == CBaseChainParams::DEVNET) { // PirateCash replaced testnet to devnet
        if (!pubkeyFromSig.RecoverCompact(GetSignatureHash(), vchSig)) {
            return false;
        }
    } else {
        std::string strMessage = std::to_string(nSporkID) + std::to_string(nValue) + std::to_string(nTimeSigned);
        CHashWriter ss(SER_GETHASH, 0);
        ss << strMessageMagic;
        ss << strMessage;
        if (!pubkeyFromSig.RecoverCompact(ss.GetHash(), vchSig)) {
            return false;
        }
    }

    retKeyidSporkSigner = pubkeyFromSig.GetID();
    return true;
}

void CSporkMessage::Relay(CConnman& connman) const
{
    CInv inv(MSG_SPORK, GetHash());
    connman.RelayInv(inv);
}
