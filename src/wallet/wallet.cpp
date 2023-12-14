// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Copyright (c) 2014-2022 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <init.h>
#include <pos_kernel.h>
#include <masternode/node.h>

#include <wallet/wallet.h>

#include <chain.h>
#include <chainparams.h>
#include <consensus/consensus.h>
#include <consensus/validation.h>
#include <crypto/common.h>
#include <fs.h>
#include <interfaces/chain.h>
#include <interfaces/wallet.h>
#include <key.h>
#include <key_io.h>
#include <keystore.h>
#include <net.h>
#include <policy/fees.h>
#include <policy/policy.h>
#include <policy/settings.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/sign.h>
#include <shutdown.h>
#include <timedata.h>
#include <txmempool.h>
#include <util/bip32.h>
#include <util/error.h>
#include <util/fees.h>
#include <util/moneystr.h>
#include <util/string.h>
#include <util/translation.h>
#include <util/validation.h>
#include <validation.h>
#include <wallet/coincontrol.h>
#include <wallet/coinselection.h>
#include <wallet/fees.h>
#include <warnings.h>

#include <coinjoin/client.h>
#include <coinjoin/options.h>
#include <governance/governance.h>
#include <evo/deterministicmns.h>
#include <spork.h>

#include <evo/providertx.h>

#include <llmq/instantsend.h>
#include <llmq/chainlocks.h>

#include <assert.h>
#include <future>

#include <boost/algorithm/string/replace.hpp>

static const size_t OUTPUT_GROUP_MAX_ENTRIES = 10;

static CCriticalSection cs_wallets;
static std::vector<std::shared_ptr<CWallet>> vpwallets GUARDED_BY(cs_wallets);
static std::list<LoadWalletFn> g_load_wallet_fns GUARDED_BY(cs_wallets);

bool AddWallet(const std::shared_ptr<CWallet>& wallet)
{
    LOCK(cs_wallets);
    assert(wallet);
    std::vector<std::shared_ptr<CWallet>>::const_iterator i = std::find(vpwallets.begin(), vpwallets.end(), wallet);
    if (i != vpwallets.end()) return false;
    coinJoinClientManagers.emplace(std::make_pair(wallet->GetName(), std::make_shared<CCoinJoinClientManager>(*wallet)));
    vpwallets.push_back(wallet);
    return true;
}

bool RemoveWallet(const std::shared_ptr<CWallet>& wallet)
{
    LOCK(cs_wallets);
    assert(wallet);
    std::vector<std::shared_ptr<CWallet>>::iterator i = std::find(vpwallets.begin(), vpwallets.end(), wallet);
    if (i == vpwallets.end()) return false;
    vpwallets.erase(i);
    auto it = coinJoinClientManagers.find(wallet->GetName());
    coinJoinClientManagers.erase(it);
    return true;
}

bool HasWallets()
{
    LOCK(cs_wallets);
    return !vpwallets.empty();
}

std::vector<std::shared_ptr<CWallet>> GetWallets()
{
    LOCK(cs_wallets);
    return vpwallets;
}

std::shared_ptr<CWallet> GetWallet(const std::string& name)
{
    LOCK(cs_wallets);
    for (const std::shared_ptr<CWallet>& wallet : vpwallets) {
        if (wallet->GetName() == name) return wallet;
    }
    return nullptr;
}

std::unique_ptr<interfaces::Handler> HandleLoadWallet(LoadWalletFn load_wallet)
{
    LOCK(cs_wallets);
    auto it = g_load_wallet_fns.emplace(g_load_wallet_fns.end(), std::move(load_wallet));
    return interfaces::MakeHandler([it] { LOCK(cs_wallets); g_load_wallet_fns.erase(it); });
}

static Mutex g_wallet_release_mutex;
static std::condition_variable g_wallet_release_cv;
static std::set<std::string> g_unloading_wallet_set;

// Custom deleter for shared_ptr<CWallet>.
static void ReleaseWallet(CWallet* wallet)
{
    // Unregister and delete the wallet right after BlockUntilSyncedToCurrentChain
    // so that it's in sync with the current chainstate.
    const std::string name = wallet->GetName();
    wallet->WalletLogPrintf("Releasing wallet\n");
    wallet->BlockUntilSyncedToCurrentChain();
    wallet->Flush();
    wallet->m_chain_notifications_handler.reset();
    delete wallet;
    // Wallet is now released, notify UnloadWallet, if any.
    {
        LOCK(g_wallet_release_mutex);
        if (g_unloading_wallet_set.erase(name) == 0) {
            // UnloadWallet was not called for this wallet, all done.
            return;
        }
    }
    g_wallet_release_cv.notify_all();
}

void UnloadWallet(std::shared_ptr<CWallet>&& wallet)
{
    // Mark wallet for unloading.
    const std::string name = wallet->GetName();
    {
        LOCK(g_wallet_release_mutex);
        auto it = g_unloading_wallet_set.insert(name);
        assert(it.second);
    }
    // The wallet can be in use so it's not possible to explicitly unload here.
    // Notify the unload intent so that all remaining shared pointers are
    // released.
    wallet->NotifyUnload();
    // Time to ditch our shared_ptr and wait for ReleaseWallet call.
    wallet.reset();
    {
        WAIT_LOCK(g_wallet_release_mutex, lock);
        while (g_unloading_wallet_set.count(name) == 1) {
            g_wallet_release_cv.wait(lock);
        }
    }
}

std::shared_ptr<CWallet> LoadWallet(interfaces::Chain& chain, const WalletLocation& location, bilingual_str& error, std::vector<bilingual_str>& warnings)
{
    if (!CWallet::Verify(chain, location, error, warnings)) {
        error = Untranslated("Wallet file verification failed.") + Untranslated(" ") + error;
        return nullptr;
    }

    std::shared_ptr<CWallet> wallet = CWallet::CreateWalletFromFile(chain, location, error, warnings);
    if (!wallet) {
        error = Untranslated("Wallet loading failed.") + Untranslated(" ") + error;
        return nullptr;
    }
    AddWallet(wallet);
    wallet->postInitProcess();
    return wallet;
}

std::shared_ptr<CWallet> LoadWallet(interfaces::Chain& chain, const std::string& name, bilingual_str& error, std::vector<bilingual_str>& warnings)
{
    return LoadWallet(chain, WalletLocation(name), error, warnings);
}

WalletCreationStatus CreateWallet(interfaces::Chain& chain, const SecureString& passphrase, uint64_t wallet_creation_flags, const std::string& name, bilingual_str& error, std::vector<bilingual_str>& warnings, std::shared_ptr<CWallet>& result)
{
    // Indicate that the wallet is actually supposed to be blank and not just blank to make it encrypted
    bool create_blank = (wallet_creation_flags & WALLET_FLAG_BLANK_WALLET);

    // Born encrypted wallets need to be created blank first.
    if (!passphrase.empty()) {
        wallet_creation_flags |= WALLET_FLAG_BLANK_WALLET;
    }

    // Check the wallet file location
    WalletLocation location(name);
    if (location.Exists()) {
        error = Untranslated(strprintf("Wallet %s already exists.", location.GetName()));
        return WalletCreationStatus::CREATION_FAILED;
    }

    // Wallet::Verify will check if we're trying to create a wallet with a duplicate name.
    if (!CWallet::Verify(chain, location, error, warnings)) {
        error = Untranslated("Wallet file verification failed.") + Untranslated(" ") + error;
        return WalletCreationStatus::CREATION_FAILED;
    }

    // Do not allow a passphrase when private keys are disabled
    if (!passphrase.empty() && (wallet_creation_flags & WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
        error = Untranslated("Passphrase provided but private keys are disabled. A passphrase is only used to encrypt private keys, so cannot be used for wallets with private keys disabled.");
        return WalletCreationStatus::CREATION_FAILED;
    }

    // Make the wallet
    std::shared_ptr<CWallet> wallet = CWallet::CreateWalletFromFile(chain, location, error, warnings, wallet_creation_flags);
    if (!wallet) {
        error = Untranslated("Wallet creation failed.") + Untranslated(" ") + error;
        return WalletCreationStatus::CREATION_FAILED;
    }

    // Encrypt the wallet
    if (!passphrase.empty() && !(wallet_creation_flags & WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
        if (!wallet->EncryptWallet(passphrase)) {
            error = Untranslated("Error: Wallet created but failed to encrypt.");
            return WalletCreationStatus::ENCRYPTION_FAILED;
        }
        if (!create_blank) {
            // Unlock the wallet
            if (!wallet->Unlock(passphrase)) {
                error = Untranslated("Error: Wallet was encrypted but could not be unlocked");
                return WalletCreationStatus::ENCRYPTION_FAILED;
            }

            // Set a HD chain for the wallet
            // TODO: re-enable this and `keypoolsize_hd_internal` check in `wallet_createwallet.py`
            // when HD is the default mode (make sure this actually works!)...
            // if (!wallet->GenerateNewHDChainEncrypted("", "", passphrase)) {
            //     throw JSONRPCError(RPC_WALLET_ERROR, "Failed to generate encrypted HD wallet");
            // }
            // ... and drop this
            wallet->UnsetWalletFlag(WALLET_FLAG_BLANK_WALLET);
            wallet->NewKeyPool();
            // end TODO

            // Relock the wallet
            wallet->Lock();
        }
    }
    AddWallet(wallet);
    wallet->postInitProcess();
    result = wallet;
    return WalletCreationStatus::SUCCESS;
}

const uint256 CWalletTx::ABANDON_HASH(uint256S("0000000000000000000000000000000000000000000000000000000000000001"));

/** @defgroup mapWallet
 *
 * @{
 */

std::string COutput::ToString() const
{
    return strprintf("COutput(%s, %d, %d) [%s]", tx->GetHash().ToString(), i, nDepth, FormatMoney(tx->tx->vout[i].nValue));
}

/** A class to identify which pubkeys a script and a keystore have in common. */
class CAffectedKeysVisitor : public boost::static_visitor<void> {
private:
    const CKeyStore &keystore;
    std::vector<CKeyID> &vKeys;

public:
    /**
     * @param[in] keystoreIn The CKeyStore that is queried for the presence of a pubkey.
     * @param[out] vKeysIn A vector to which a script's pubkey identifiers are appended if they are in the keystore.
     */
    CAffectedKeysVisitor(const CKeyStore &keystoreIn, std::vector<CKeyID> &vKeysIn) : keystore(keystoreIn), vKeys(vKeysIn) {}

    /**
     * Apply the visitor to each destination in a script, recursively to the redeemscript
     * in the case of p2sh destinations.
     * @param[in] script The CScript from which destinations are extracted.
     * @post Any CKeyIDs that script and keystore have in common are appended to the visitor's vKeys.
     */
    void Process(const CScript &script) {
        txnouttype type;
        std::vector<CTxDestination> vDest;
        int nRequired;
        if (ExtractDestinations(script, type, vDest, nRequired)) {
            for (const CTxDestination &dest : vDest)
                boost::apply_visitor(*this, dest);
        }
    }

    void operator()(const CKeyID &keyId) {
        if (keystore.HaveKey(keyId))
            vKeys.push_back(keyId);
    }

    void operator()(const CScriptID &scriptId) {
        CScript script;
        if (keystore.GetCScript(scriptId, script))
            Process(script);
    }

    void operator()(const CNoDestination &none) {}
};

const CWalletTx* CWallet::GetWalletTx(const uint256& hash) const
{
    LOCK(cs_wallet);
    std::map<uint256, CWalletTx>::const_iterator it = mapWallet.find(hash);
    if (it == mapWallet.end())
        return nullptr;
    return &(it->second);
}

CPubKey CWallet::GenerateNewKey(WalletBatch &batch, uint32_t nAccountIndex, bool fInternal)
{
    assert(!IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS));
    assert(!IsWalletFlagSet(WALLET_FLAG_BLANK_WALLET));
    AssertLockHeld(cs_wallet);
    bool fCompressed = CanSupportFeature(FEATURE_COMPRPUBKEY); // default to compressed public keys if we want 0.6.0 wallets

    CKey secret;

    // Create new metadata
    int64_t nCreationTime = GetTime();
    CKeyMetadata metadata(nCreationTime);

    CPubKey pubkey;
    // use HD key derivation if HD was enabled during wallet creation and a non-null HD chain is present
    if (IsHDEnabled()) {
        DeriveNewChildKey(batch, metadata, secret, nAccountIndex, fInternal);
        pubkey = secret.GetPubKey();
    } else {
        secret.MakeNewKey(fCompressed);

        // Compressed public keys were introduced in version 0.6.0
        if (fCompressed) {
            SetMinVersion(FEATURE_COMPRPUBKEY);
        }

        pubkey = secret.GetPubKey();
        assert(secret.VerifyPubKey(pubkey));

        // Create new metadata
        mapKeyMetadata[pubkey.GetID()] = metadata;
        UpdateTimeFirstKey(nCreationTime);

        if (!AddKeyPubKeyWithDB(batch, secret, pubkey)) {
            throw std::runtime_error(std::string(__func__) + ": AddKey failed");
        }
    }
    return pubkey;
}

void CWallet::DeriveNewChildKey(WalletBatch &batch, CKeyMetadata& metadata, CKey& secretRet, uint32_t nAccountIndex, bool fInternal)
{
    CHDChain hdChainTmp;
    if (!GetHDChain(hdChainTmp)) {
        throw std::runtime_error(std::string(__func__) + ": GetHDChain failed");
    }

    if (!DecryptHDChain(hdChainTmp))
        throw std::runtime_error(std::string(__func__) + ": DecryptHDChain failed");
    // make sure seed matches this chain
    if (hdChainTmp.GetID() != hdChainTmp.GetSeedHash())
        throw std::runtime_error(std::string(__func__) + ": Wrong HD chain!");

    CHDAccount acc;
    if (!hdChainTmp.GetAccount(nAccountIndex, acc))
        throw std::runtime_error(std::string(__func__) + ": Wrong HD account!");

    // derive child key at next index, skip keys already known to the wallet
    CExtKey childKey;
    CKeyMetadata metadataTmp;
    uint32_t nChildIndex = fInternal ? acc.nInternalChainCounter : acc.nExternalChainCounter;
    do {
        // NOTE: DeriveChildExtKey updates metadata, use temporary structure to make sure
        // we start with the original (non-updated) data each time.
        metadataTmp = metadata;
        hdChainTmp.DeriveChildExtKey(nAccountIndex, fInternal, nChildIndex, childKey, metadataTmp);
        // increment childkey index
        nChildIndex++;
    } while (HaveKey(childKey.key.GetPubKey().GetID()));
    metadata = metadataTmp;
    secretRet = childKey.key;

    CPubKey pubkey = secretRet.GetPubKey();
    assert(secretRet.VerifyPubKey(pubkey));

    // store metadata
    mapKeyMetadata[pubkey.GetID()] = metadata;
    UpdateTimeFirstKey(metadata.nCreateTime);

    // update the chain model in the database
    CHDChain hdChainCurrent;
    GetHDChain(hdChainCurrent);

    if (fInternal) {
        acc.nInternalChainCounter = nChildIndex;
    }
    else {
        acc.nExternalChainCounter = nChildIndex;
    }

    if (!hdChainCurrent.SetAccount(nAccountIndex, acc))
        throw std::runtime_error(std::string(__func__) + ": SetAccount failed");

    if (IsCrypted()) {
        if (!SetCryptedHDChain(batch, hdChainCurrent, false))
            throw std::runtime_error(std::string(__func__) + ": SetCryptedHDChain failed");
    }
    else {
        if (!SetHDChain(batch, hdChainCurrent, false))
            throw std::runtime_error(std::string(__func__) + ": SetHDChain failed");
    }

    if (!AddHDPubKey(batch, childKey.Neuter(), fInternal))
        throw std::runtime_error(std::string(__func__) + ": AddHDPubKey failed");
}

bool CWallet::GetPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const
{
    LOCK(cs_wallet);
    std::map<CKeyID, CHDPubKey>::const_iterator mi = mapHdPubKeys.find(address);
    if (mi != mapHdPubKeys.end())
    {
        const CHDPubKey &hdPubKey = (*mi).second;
        vchPubKeyOut = hdPubKey.extPubKey.pubkey;
        return true;
    }
    else
        return CCryptoKeyStore::GetPubKey(address, vchPubKeyOut);
}

bool CWallet::GetKey(const CKeyID &address, CKey& keyOut) const
{
    LOCK(cs_wallet);
    std::map<CKeyID, CHDPubKey>::const_iterator mi = mapHdPubKeys.find(address);
    if (mi != mapHdPubKeys.end())
    {
        // if the key has been found in mapHdPubKeys, derive it on the fly
        const CHDPubKey &hdPubKey = (*mi).second;
        CHDChain hdChainCurrent;
        if (!GetHDChain(hdChainCurrent))
            throw std::runtime_error(std::string(__func__) + ": GetHDChain failed");
        if (!DecryptHDChain(hdChainCurrent))
            throw std::runtime_error(std::string(__func__) + ": DecryptHDChain failed");
        // make sure seed matches this chain
        if (hdChainCurrent.GetID() != hdChainCurrent.GetSeedHash())
            throw std::runtime_error(std::string(__func__) + ": Wrong HD chain!");

        CExtKey extkey;
        CKeyMetadata metadataTmp;
        hdChainCurrent.DeriveChildExtKey(hdPubKey.nAccountIndex, hdPubKey.nChangeIndex != 0, hdPubKey.extPubKey.nChild, extkey, metadataTmp);
        keyOut = extkey.key;

        return true;
    }
    else {
        return CCryptoKeyStore::GetKey(address, keyOut);
    }
}

bool CWallet::HaveKey(const CKeyID &address) const
{
    LOCK(cs_wallet);
    if (mapHdPubKeys.count(address) > 0)
        return true;
    return CCryptoKeyStore::HaveKey(address);
}

bool CWallet::LoadHDPubKey(const CHDPubKey &hdPubKey)
{
    AssertLockHeld(cs_wallet);

    mapHdPubKeys[hdPubKey.extPubKey.pubkey.GetID()] = hdPubKey;
    return true;
}

bool CWallet::AddHDPubKey(WalletBatch &batch, const CExtPubKey &extPubKey, bool fInternal)
{
    AssertLockHeld(cs_wallet);

    CHDChain hdChainCurrent;
    GetHDChain(hdChainCurrent);

    CHDPubKey hdPubKey;
    hdPubKey.extPubKey = extPubKey;
    hdPubKey.hdchainID = hdChainCurrent.GetID();
    hdPubKey.nChangeIndex = fInternal ? 1 : 0;
    mapHdPubKeys[extPubKey.pubkey.GetID()] = hdPubKey;

    // check if we need to remove from watch-only
    CScript script;
    script = GetScriptForDestination(extPubKey.pubkey.GetID());
    if (HaveWatchOnly(script))
        RemoveWatchOnly(script);
    script = GetScriptForRawPubKey(extPubKey.pubkey);
    if (HaveWatchOnly(script))
        RemoveWatchOnly(script);

    if (!batch.WriteHDPubKey(hdPubKey, mapKeyMetadata[extPubKey.pubkey.GetID()])) {
        return false;
    }
    UnsetWalletFlag(batch, WALLET_FLAG_BLANK_WALLET);
    return true;
}

bool CWallet::AddKeyPubKeyWithDB(WalletBatch& batch, const CKey& secret, const CPubKey& pubkey)
{
    AssertLockHeld(cs_wallet);

    // Make sure we aren't adding private keys to private key disabled wallets
    assert(!IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS));

    // CCryptoKeyStore has no concept of wallet databases, but calls AddCryptedKey
    // which is overridden below.  To avoid flushes, the database handle is
    // tunneled through to it.
    bool needsDB = !encrypted_batch;
    if (needsDB) {
        encrypted_batch = &batch;
    }
    if (!CCryptoKeyStore::AddKeyPubKey(secret, pubkey)) {
        if (needsDB) encrypted_batch = nullptr;
        return false;
    }
    if (needsDB) encrypted_batch = nullptr;
    // check if we need to remove from watch-only
    CScript script;
    script = GetScriptForDestination(pubkey.GetID());
    if (HaveWatchOnly(script)) {
        RemoveWatchOnly(script);
    }
    script = GetScriptForRawPubKey(pubkey);
    if (HaveWatchOnly(script)) {
        RemoveWatchOnly(script);
    }

    if (!IsCrypted()) {
        return batch.WriteKey(pubkey,
                                 secret.GetPrivKey(),
                                 mapKeyMetadata[pubkey.GetID()]);
    }
    UnsetWalletFlag(batch, WALLET_FLAG_BLANK_WALLET);
    return true;
}


bool CWallet::AddKeyPubKey(const CKey& secret, const CPubKey &pubkey)
{
    WalletBatch batch(*database);

    return CWallet::AddKeyPubKeyWithDB(batch, secret, pubkey);
}

bool CWallet::AddCryptedKey(const CPubKey &vchPubKey,
                            const std::vector<unsigned char> &vchCryptedSecret)
{
    if (!CCryptoKeyStore::AddCryptedKey(vchPubKey, vchCryptedSecret))
        return false;
    {
        LOCK(cs_wallet);
        if (encrypted_batch)
            return encrypted_batch->WriteCryptedKey(vchPubKey,
                                                        vchCryptedSecret,
                                                        mapKeyMetadata[vchPubKey.GetID()]);
        else
            return WalletBatch(*database).WriteCryptedKey(vchPubKey,
                                                            vchCryptedSecret,
                                                            mapKeyMetadata[vchPubKey.GetID()]);
    }
}

void CWallet::LoadKeyMetadata(const CKeyID& keyID, const CKeyMetadata& meta)
{
    AssertLockHeld(cs_wallet);
    UpdateTimeFirstKey(meta.nCreateTime);
    mapKeyMetadata[keyID] = meta;
}

void CWallet::LoadScriptMetadata(const CScriptID& script_id, const CKeyMetadata& meta)
{
    AssertLockHeld(cs_wallet);
    UpdateTimeFirstKey(meta.nCreateTime);
    m_script_metadata[script_id] = meta;
}

// Writes a keymetadata for a public key. overwrite specifies whether to overwrite an existing metadata for that key if there exists one.
bool CWallet::WriteKeyMetadata(const CKeyMetadata& meta, const CPubKey& pubkey, const bool overwrite)
{
    return WalletBatch(*database).WriteKeyMetadata(meta, pubkey, overwrite);
}

void CWallet::UpgradeKeyMetadata()
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    if (IsLocked() || IsWalletFlagSet(WALLET_FLAG_KEY_ORIGIN_METADATA) || !IsHDEnabled()) {
        return;
    }

    CHDChain hdChainCurrent;
    if (!GetHDChain(hdChainCurrent))
        throw std::runtime_error(std::string(__func__) + ": GetHDChain failed");
    if (!DecryptHDChain(hdChainCurrent))
        throw std::runtime_error(std::string(__func__) + ": DecryptHDChain failed");

    CExtKey masterKey;
    SecureVector vchSeed = hdChainCurrent.GetSeed();
    masterKey.SetSeed(vchSeed.data(), vchSeed.size());
    CKeyID master_id = masterKey.key.GetPubKey().GetID();

    std::unique_ptr<WalletBatch> batch = MakeUnique<WalletBatch>(*database);
    size_t cnt = 0;
    for (auto& meta_pair : mapKeyMetadata) {
        const CKeyID& keyid = meta_pair.first;
        CKeyMetadata& meta = meta_pair.second;
        if (!meta.has_key_origin) {
            std::map<CKeyID, CHDPubKey>::const_iterator mi = mapHdPubKeys.find(keyid);
            if (mi == mapHdPubKeys.end()) {
                continue;
            }

            // Add to map
            std::copy(master_id.begin(), master_id.begin() + 4, meta.key_origin.fingerprint);
            if (!ParseHDKeypath(mi->second.GetKeyPath(), meta.key_origin.path)) {
                throw std::runtime_error("Invalid HD keypath");
            }
            meta.has_key_origin = true;
            if (meta.nVersion < CKeyMetadata::VERSION_WITH_KEY_ORIGIN) {
                meta.nVersion = CKeyMetadata::VERSION_WITH_KEY_ORIGIN;
            }

            // Write meta to wallet
            batch->WriteKeyMetadata(meta, mi->second.extPubKey.pubkey, true);
            if (++cnt % 1000 == 0) {
                // avoid creating overlarge in-memory batches in case the wallet contains large amounts of keys
                batch.reset(new WalletBatch(*database));
            }
        }
    }
    batch.reset(); //write before setting the flag
    SetWalletFlag(WALLET_FLAG_KEY_ORIGIN_METADATA);
}

bool CWallet::LoadCryptedKey(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret)
{
    return CCryptoKeyStore::AddCryptedKey(vchPubKey, vchCryptedSecret);
}

/**
 * Update wallet first key creation time. This should be called whenever keys
 * are added to the wallet, with the oldest key creation time.
 */
void CWallet::UpdateTimeFirstKey(int64_t nCreateTime)
{
    AssertLockHeld(cs_wallet);
    if (nCreateTime <= 1) {
        // Cannot determine birthday information, so set the wallet birthday to
        // the beginning of time.
        nTimeFirstKey = 1;
    } else if (!nTimeFirstKey || nCreateTime < nTimeFirstKey) {
        nTimeFirstKey = nCreateTime;
    }
}

int64_t CWallet::GetTimeFirstKey() const
{
    AssertLockHeld(cs_wallet);
    return nTimeFirstKey;
}

bool CWallet::AddCScript(const CScript& redeemScript)
{
    WalletBatch batch(*database);
    return AddCScriptWithDB(batch, redeemScript);
}

bool CWallet::AddCScriptWithDB(WalletBatch& batch, const CScript& redeemScript)
{
    if (!CCryptoKeyStore::AddCScript(redeemScript))
        return false;
    if (batch.WriteCScript(Hash160(redeemScript), redeemScript)) {
        UnsetWalletFlag(batch, WALLET_FLAG_BLANK_WALLET);
        return true;
    }
    return false;
}

bool CWallet::LoadCScript(const CScript& redeemScript)
{
    /* A sanity check was added in pull #3843 to avoid adding redeemScripts
     * that never can be redeemed. However, old wallets may still contain
     * these. Do not add them to the wallet and warn. */
    if (redeemScript.size() > MAX_SCRIPT_ELEMENT_SIZE)
    {
        std::string strAddr = EncodeDestination(CScriptID(redeemScript));
        WalletLogPrintf("%s: Warning: This wallet contains a redeemScript of size %i which exceeds maximum size %i thus can never be redeemed. Do not use address %s.\n", __func__, redeemScript.size(), MAX_SCRIPT_ELEMENT_SIZE, strAddr);
        return true;
    }

    return CCryptoKeyStore::AddCScript(redeemScript);
}

bool CWallet::AddWatchOnlyWithDB(WalletBatch &batch, const CScript& dest)
{
    if (!CCryptoKeyStore::AddWatchOnly(dest))
        return false;
    const CKeyMetadata& meta = m_script_metadata[CScriptID(dest)];
    UpdateTimeFirstKey(meta.nCreateTime);
    NotifyWatchonlyChanged(true);
    if (batch.WriteWatchOnly(dest, meta)) {
        UnsetWalletFlag(batch, WALLET_FLAG_BLANK_WALLET);
        return true;
    }
    return false;
}

bool CWallet::AddWatchOnlyWithDB(WalletBatch &batch, const CScript& dest, int64_t create_time)
{
    m_script_metadata[CScriptID(dest)].nCreateTime = create_time;
    return AddWatchOnlyWithDB(batch, dest);
}

bool CWallet::AddWatchOnly(const CScript& dest)
{
    WalletBatch batch(*database);
    return AddWatchOnlyWithDB(batch, dest);
}

bool CWallet::AddWatchOnly(const CScript& dest, int64_t nCreateTime)
{
    m_script_metadata[CScriptID(dest)].nCreateTime = nCreateTime;
    return AddWatchOnly(dest);
}

bool CWallet::RemoveWatchOnly(const CScript &dest)
{
    AssertLockHeld(cs_wallet);
    if (!CCryptoKeyStore::RemoveWatchOnly(dest))
        return false;
    if (!HaveWatchOnly())
        NotifyWatchonlyChanged(false);
    if (!WalletBatch(*database).EraseWatchOnly(dest))
        return false;

    return true;
}

bool CWallet::LoadWatchOnly(const CScript &dest)
{
    return CCryptoKeyStore::AddWatchOnly(dest);
}

bool CWallet::Unlock(const SecureString& strWalletPassphrase, bool fForMixingOnly, bool accept_no_keys)
{
    if (!IsLocked()) // was already fully unlocked, not only for mixing
        return true;

    CCrypter crypter;
    CKeyingMaterial _vMasterKey;

    {
        LOCK(cs_wallet);
        for (const MasterKeyMap::value_type& pMasterKey : mapMasterKeys)
        {
            if (!crypter.SetKeyFromPassphrase(strWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                return false;
            if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, _vMasterKey))
                continue; // try another master key
            if (CCryptoKeyStore::Unlock(_vMasterKey, fForMixingOnly, accept_no_keys)) {
                // Now that we've unlocked, upgrade the key metadata
                UpgradeKeyMetadata();
                if(nWalletBackups == -2) {
                    TopUpKeyPool();
                    WalletLogPrintf("Keypool replenished, re-initializing automatic backups.\n");
                    nWalletBackups = gArgs.GetArg("-createwalletbackups", 10);
                }
                return true;
            }
        }
    }
    return false;
}

bool CWallet::ChangeWalletPassphrase(const SecureString& strOldWalletPassphrase, const SecureString& strNewWalletPassphrase)
{
    bool fWasLocked = IsLocked(true);

    {
        LOCK(cs_wallet);
        Lock();

        CCrypter crypter;
        CKeyingMaterial _vMasterKey;
        for (MasterKeyMap::value_type& pMasterKey : mapMasterKeys)
        {
            if(!crypter.SetKeyFromPassphrase(strOldWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                return false;
            if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, _vMasterKey))
                return false;
            if (CCryptoKeyStore::Unlock(_vMasterKey))
            {
                int64_t nStartTime = GetTimeMillis();
                crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod);
                pMasterKey.second.nDeriveIterations = static_cast<unsigned int>(pMasterKey.second.nDeriveIterations * (100 / ((double)(GetTimeMillis() - nStartTime))));

                nStartTime = GetTimeMillis();
                crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod);
                pMasterKey.second.nDeriveIterations = (pMasterKey.second.nDeriveIterations + static_cast<unsigned int>(pMasterKey.second.nDeriveIterations * 100 / ((double)(GetTimeMillis() - nStartTime)))) / 2;

                if (pMasterKey.second.nDeriveIterations < 25000)
                    pMasterKey.second.nDeriveIterations = 25000;

                WalletLogPrintf("Wallet passphrase changed to an nDeriveIterations of %i\n", pMasterKey.second.nDeriveIterations);

                if (!crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                    return false;
                if (!crypter.Encrypt(_vMasterKey, pMasterKey.second.vchCryptedKey))
                    return false;
                WalletBatch(*database).WriteMasterKey(pMasterKey.first, pMasterKey.second);
                if (fWasLocked)
                    Lock();

                return true;
            }
        }
    }

    return false;
}

void CWallet::ChainStateFlushed(const CBlockLocator& loc)
{
    WalletBatch batch(*database);
    batch.WriteBestBlock(loc);
}

void CWallet::SetMinVersion(enum WalletFeature nVersion, WalletBatch* batch_in, bool fExplicit)
{
    LOCK(cs_wallet);
    if (nWalletVersion >= nVersion)
        return;

    // when doing an explicit upgrade, if we pass the max version permitted, upgrade all the way
    if (fExplicit && nVersion > nWalletMaxVersion)
            nVersion = FEATURE_LATEST;

    nWalletVersion = nVersion;

    if (nVersion > nWalletMaxVersion)
        nWalletMaxVersion = nVersion;

    {
        WalletBatch* batch = batch_in ? batch_in : new WalletBatch(*database);
        if (nWalletVersion > 40000)
            batch->WriteMinVersion(nWalletVersion);
        if (!batch_in)
            delete batch;
    }
}

bool CWallet::SetMaxVersion(int nVersion)
{
    LOCK(cs_wallet);
    // cannot downgrade below current version
    if (nWalletVersion > nVersion)
        return false;

    nWalletMaxVersion = nVersion;

    return true;
}

std::set<uint256> CWallet::GetConflicts(const uint256& txid) const
{
    std::set<uint256> result;
    AssertLockHeld(cs_wallet);

    std::map<uint256, CWalletTx>::const_iterator it = mapWallet.find(txid);
    if (it == mapWallet.end())
        return result;
    const CWalletTx& wtx = it->second;

    std::pair<TxSpends::const_iterator, TxSpends::const_iterator> range;

    for (const CTxIn& txin : wtx.tx->vin)
    {
        if (mapTxSpends.count(txin.prevout) <= 1)
            continue;  // No conflict if zero or one spends
        range = mapTxSpends.equal_range(txin.prevout);
        for (TxSpends::const_iterator _it = range.first; _it != range.second; ++_it)
            result.insert(_it->second);
    }
    return result;
}

void CWallet::Flush()
{
    database->Flush();
}

void CWallet::Close()
{
    database->Close();
}

void CWallet::SyncMetaData(std::pair<TxSpends::iterator, TxSpends::iterator> range)
{
    // We want all the wallet transactions in range to have the same metadata as
    // the oldest (smallest nOrderPos).
    // So: find smallest nOrderPos:

    int nMinOrderPos = std::numeric_limits<int>::max();
    const CWalletTx* copyFrom = nullptr;
    for (TxSpends::iterator it = range.first; it != range.second; ++it) {
        const CWalletTx* wtx = &mapWallet.at(it->second);
        if (wtx->nOrderPos < nMinOrderPos) {
            nMinOrderPos = wtx->nOrderPos;
            copyFrom = wtx;
        }
    }

    if (!copyFrom) {
        return;
    }

    // Now copy data from copyFrom to rest:
    for (TxSpends::iterator it = range.first; it != range.second; ++it)
    {
        const uint256& hash = it->second;
        CWalletTx* copyTo = &mapWallet.at(hash);
        if (copyFrom == copyTo) continue;
        assert(copyFrom && "Oldest wallet transaction in range assumed to have been found.");
        if (!copyFrom->IsEquivalentTo(*copyTo)) continue;
        copyTo->mapValue = copyFrom->mapValue;
        copyTo->vOrderForm = copyFrom->vOrderForm;
        // fTimeReceivedIsTxTime not copied on purpose
        // nTimeReceived not copied on purpose
        copyTo->nTimeSmart = copyFrom->nTimeSmart;
        copyTo->fFromMe = copyFrom->fFromMe;
        // nOrderPos not copied on purpose
        // cached members not copied on purpose
    }
}

/**
 * Outpoint is spent if any non-conflicted transaction
 * spends it:
 */
bool CWallet::IsSpent(interfaces::Chain::Lock& locked_chain, const uint256& hash, unsigned int n) const
{
    const COutPoint outpoint(hash, n);
    std::pair<TxSpends::const_iterator, TxSpends::const_iterator> range;
    range = mapTxSpends.equal_range(outpoint);

    for (TxSpends::const_iterator it = range.first; it != range.second; ++it)
    {
        const uint256& wtxid = it->second;
        std::map<uint256, CWalletTx>::const_iterator mit = mapWallet.find(wtxid);
        if (mit != mapWallet.end()) {
            int depth = mit->second.GetDepthInMainChain(locked_chain);
            if (depth > 0  || (depth == 0 && !mit->second.isAbandoned()))
                return true; // Spent
        }
    }
    return false;
}

void CWallet::AddToSpends(const COutPoint& outpoint, const uint256& wtxid)
{
    mapTxSpends.insert(std::make_pair(outpoint, wtxid));
    setWalletUTXO.erase(outpoint);

    setLockedCoins.erase(outpoint);

    std::pair<TxSpends::iterator, TxSpends::iterator> range;
    range = mapTxSpends.equal_range(outpoint);
    SyncMetaData(range);
}


void CWallet::AddToSpends(const uint256& wtxid)
{
    auto it = mapWallet.find(wtxid);
    assert(it != mapWallet.end());
    CWalletTx& thisTx = it->second;
    if (thisTx.IsCoinBase()) // Coinbases don't spend anything!
        return;

    for (const CTxIn& txin : thisTx.tx->vin)
        AddToSpends(txin.prevout, wtxid);
}

bool CWallet::EncryptWallet(const SecureString& strWalletPassphrase)
{
    if (IsCrypted())
        return false;

    CKeyingMaterial _vMasterKey;

    _vMasterKey.resize(WALLET_CRYPTO_KEY_SIZE);
    GetStrongRandBytes(&_vMasterKey[0], WALLET_CRYPTO_KEY_SIZE);

    CMasterKey kMasterKey;

    kMasterKey.vchSalt.resize(WALLET_CRYPTO_SALT_SIZE);
    GetStrongRandBytes(&kMasterKey.vchSalt[0], WALLET_CRYPTO_SALT_SIZE);

    CCrypter crypter;
    int64_t nStartTime = GetTimeMillis();
    crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, 25000, kMasterKey.nDerivationMethod);
    kMasterKey.nDeriveIterations = static_cast<unsigned int>(2500000 / ((double)(GetTimeMillis() - nStartTime)));

    nStartTime = GetTimeMillis();
    crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, kMasterKey.nDeriveIterations, kMasterKey.nDerivationMethod);
    kMasterKey.nDeriveIterations = (kMasterKey.nDeriveIterations + static_cast<unsigned int>(kMasterKey.nDeriveIterations * 100 / ((double)(GetTimeMillis() - nStartTime)))) / 2;

    if (kMasterKey.nDeriveIterations < 25000)
        kMasterKey.nDeriveIterations = 25000;

    WalletLogPrintf("Encrypting Wallet with an nDeriveIterations of %i\n", kMasterKey.nDeriveIterations);

    if (!crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, kMasterKey.nDeriveIterations, kMasterKey.nDerivationMethod))
        return false;
    if (!crypter.Encrypt(_vMasterKey, kMasterKey.vchCryptedKey))
        return false;

    {
        LOCK(cs_wallet);
        mapMasterKeys[++nMasterKeyMaxID] = kMasterKey;
        assert(!encrypted_batch);
        encrypted_batch = new WalletBatch(*database);
        if (!encrypted_batch->TxnBegin()) {
            delete encrypted_batch;
            encrypted_batch = nullptr;
            return false;
        }
        encrypted_batch->WriteMasterKey(nMasterKeyMaxID, kMasterKey);

        // must get current HD chain before EncryptKeys
        CHDChain hdChainCurrent;
        GetHDChain(hdChainCurrent);

        if (!EncryptKeys(_vMasterKey))
        {
            encrypted_batch->TxnAbort();
            delete encrypted_batch;
            encrypted_batch = nullptr;
            // We now probably have half of our keys encrypted in memory, and half not...
            // die and let the user reload the unencrypted wallet.
            assert(false);
        }

        if (!hdChainCurrent.IsNull()) {
            assert(EncryptHDChain(_vMasterKey));

            CHDChain hdChainCrypted;
            assert(GetHDChain(hdChainCrypted));

            DBG(
                tfm::format(std::cout, "EncryptWallet -- current seed: '%s'\n", HexStr(hdChainCurrent.GetSeed()));
                tfm::format(std::cout, "EncryptWallet -- crypted seed: '%s'\n", HexStr(hdChainCrypted.GetSeed()));
            );

            // ids should match, seed hashes should not
            assert(hdChainCurrent.GetID() == hdChainCrypted.GetID());
            assert(hdChainCurrent.GetSeedHash() != hdChainCrypted.GetSeedHash());

            assert(SetCryptedHDChain(*encrypted_batch, hdChainCrypted, false));
        }

        // Encryption was introduced in version 0.4.0
        SetMinVersion(FEATURE_WALLETCRYPT, encrypted_batch, true);

        if (!encrypted_batch->TxnCommit()) {
            delete encrypted_batch;
            encrypted_batch = nullptr;
            // We now have keys encrypted in memory, but not on disk...
            // die to avoid confusion and let the user reload the unencrypted wallet.
            assert(false);
        }

        delete encrypted_batch;
        encrypted_batch = nullptr;

        Lock();
        Unlock(strWalletPassphrase);

        // if we are not using HD, generate new keypool
        if(IsHDEnabled()) {
            TopUpKeyPool();
        }
        else {
            NewKeyPool();
        }

        Lock();

        // Need to completely rewrite the wallet file; if we don't, bdb might keep
        // bits of the unencrypted private key in slack space in the database file.
        database->Rewrite();

        // BDB seems to have a bad habit of writing old data into
        // slack space in .dat files; that is bad if the old data is
        // unencrypted private keys. So:
        database->ReloadDbEnv();

    }
    NotifyStatusChanged(this);

    return true;
}

DBErrors CWallet::ReorderTransactions()
{
    LOCK(cs_wallet);
    WalletBatch batch(*database);

    // Old wallets didn't have any defined order for transactions
    // Probably a bad idea to change the output of this

    // First: get all CWalletTx into a sorted-by-time multimap.
    typedef std::multimap<int64_t, CWalletTx*> TxItems;
    TxItems txByTime;

    for (auto& entry : mapWallet)
    {
        CWalletTx* wtx = &entry.second;
        txByTime.insert(std::make_pair(wtx->nTimeReceived, wtx));
    }

    nOrderPosNext = 0;
    std::vector<int64_t> nOrderPosOffsets;
    for (TxItems::iterator it = txByTime.begin(); it != txByTime.end(); ++it)
    {
        CWalletTx *const pwtx = (*it).second;
        int64_t& nOrderPos = pwtx->nOrderPos;

        if (nOrderPos == -1)
        {
            nOrderPos = nOrderPosNext++;
            nOrderPosOffsets.push_back(nOrderPos);

            if (!batch.WriteTx(*pwtx))
                return DBErrors::LOAD_FAIL;
        }
        else
        {
            int64_t nOrderPosOff = 0;
            for (const int64_t& nOffsetStart : nOrderPosOffsets)
            {
                if (nOrderPos >= nOffsetStart)
                    ++nOrderPosOff;
            }
            nOrderPos += nOrderPosOff;
            nOrderPosNext = std::max(nOrderPosNext, nOrderPos + 1);

            if (!nOrderPosOff)
                continue;

            // Since we're changing the order, write it back
            if (!batch.WriteTx(*pwtx))
                return DBErrors::LOAD_FAIL;
        }
    }
    batch.WriteOrderPosNext(nOrderPosNext);

    return DBErrors::LOAD_OK;
}

int64_t CWallet::IncOrderPosNext(WalletBatch* batch)
{
    AssertLockHeld(cs_wallet);
    int64_t nRet = nOrderPosNext++;
    if (batch) {
        batch->WriteOrderPosNext(nOrderPosNext);
    } else {
        WalletBatch(*database).WriteOrderPosNext(nOrderPosNext);
    }
    return nRet;
}

void CWallet::MarkDirty()
{
    {
        LOCK(cs_wallet);
        for (std::pair<const uint256, CWalletTx>& item : mapWallet)
            item.second.MarkDirty();
    }

    fAnonymizableTallyCached = false;
    fAnonymizableTallyCachedNonDenom = false;
}

bool CWallet::AddToWallet(const CWalletTx& wtxIn, bool fFlushOnClose)
{
    AssertLockHeld(cs_main); // detect potential deadlocks which might be caused by GetListAtChainTip and IsSpent below
    LOCK(cs_wallet);

    WalletBatch batch(*database, "r+", fFlushOnClose);

    uint256 hash = wtxIn.GetHash();

    // Inserts only if not already there, returns tx inserted or tx found
    std::pair<std::map<uint256, CWalletTx>::iterator, bool> ret = mapWallet.insert(std::make_pair(hash, wtxIn));
    CWalletTx& wtx = (*ret.first).second;
    wtx.BindWallet(this);
    bool fInsertedNew = ret.second;
    if (fInsertedNew) {
        wtx.nTimeReceived = chain().getAdjustedTime();
        wtx.nOrderPos = IncOrderPosNext(&batch);
        wtx.m_it_wtxOrdered = wtxOrdered.insert(std::make_pair(wtx.nOrderPos, &wtx));
        wtx.nTimeSmart = ComputeTimeSmart(wtx);
        AddToSpends(hash);

        auto mnList = deterministicMNManager->GetListAtChainTip();
        for(unsigned int i = 0; i < wtx.tx->vout.size(); ++i) {
            if (IsMine(wtx.tx->vout[i]) && !IsSpent(*chain().lock(), hash, i)) {
                setWalletUTXO.insert(COutPoint(hash, i));
                if (deterministicMNManager->IsProTxWithCollateral(wtx.tx, i) || mnList.HasMNByCollateral(COutPoint(hash, i))) {
                    LockCoin(COutPoint(hash, i));
                }
            }
        }
    }

    bool fUpdated = false;
    if (!fInsertedNew)
    {
        // Merge
        if (!wtxIn.hashUnset() && wtxIn.hashBlock != wtx.hashBlock)
        {
            wtx.hashBlock = wtxIn.hashBlock;
            fUpdated = true;
        }
        // If no longer abandoned, update
        if (wtxIn.hashBlock.IsNull() && wtx.isAbandoned())
        {
            wtx.hashBlock = wtxIn.hashBlock;
            fUpdated = true;
        }
        if (wtxIn.nIndex != -1 && (wtxIn.nIndex != wtx.nIndex))
        {
            wtx.nIndex = wtxIn.nIndex;
            fUpdated = true;
        }
        if (wtxIn.fFromMe && wtxIn.fFromMe != wtx.fFromMe)
        {
            wtx.fFromMe = wtxIn.fFromMe;
            fUpdated = true;
        }

        auto mnList = deterministicMNManager->GetListAtChainTip();
        for (unsigned int i = 0; i < wtx.tx->vout.size(); ++i) {
            if (IsMine(wtx.tx->vout[i]) && !IsSpent(*chain().lock(), hash, i)) {
                bool new_utxo = setWalletUTXO.insert(COutPoint(hash, i)).second;
                if (new_utxo && (deterministicMNManager->IsProTxWithCollateral(wtx.tx, i) || mnList.HasMNByCollateral(COutPoint(hash, i)))) {
                    LockCoin(COutPoint(hash, i));
                }
                fUpdated |= new_utxo;
            }
        }
    }

    //// debug print
    WalletLogPrintf("AddToWallet %s  %s%s\n", wtxIn.GetHash().ToString(), (fInsertedNew ? "new" : ""), (fUpdated ? "update" : ""));

    // Write to disk
    if (fInsertedNew || fUpdated)
        if (!batch.WriteTx(wtx))
            return false;

    // Break debit/credit balance caches:
    wtx.MarkDirty();

    // Notify UI of new or updated transaction
    NotifyTransactionChanged(this, hash, fInsertedNew ? CT_NEW : CT_UPDATED);

    // notify an external script when a wallet transaction comes in or is updated
    std::string strCmd = gArgs.GetArg("-walletnotify", "");

    if (!strCmd.empty())
    {
        boost::replace_all(strCmd, "%s", wtxIn.GetHash().GetHex());
        std::thread t(runCommand, strCmd);
        t.detach(); // thread runs free
    }

    fAnonymizableTallyCached = false;
    fAnonymizableTallyCachedNonDenom = false;

    return true;
}

void CWallet::LoadToWallet(const CWalletTx& wtxIn)
{
    uint256 hash = wtxIn.GetHash();
    const auto& ins = mapWallet.emplace(hash, wtxIn);
    CWalletTx& wtx = ins.first->second;
    wtx.BindWallet(this);
    if (/* insertion took place */ ins.second) {
        wtx.m_it_wtxOrdered = wtxOrdered.insert(std::make_pair(wtx.nOrderPos, &wtx));
    }
    AddToSpends(hash);
    for (const CTxIn& txin : wtx.tx->vin) {
        auto it = mapWallet.find(txin.prevout.hash);
        if (it != mapWallet.end()) {
            CWalletTx& prevtx = it->second;
            if (prevtx.nIndex == -1 && !prevtx.hashUnset()) {
                MarkConflicted(prevtx.hashBlock, wtx.GetHash());
            }
        }
    }
}

bool CWallet::AddToWalletIfInvolvingMe(const CTransactionRef& ptx, const uint256& block_hash, int posInBlock, bool fUpdate)
{
    const CTransaction& tx = *ptx;
    {
        AssertLockHeld(cs_main); // because of AddToWallet
        AssertLockHeld(cs_wallet);

        if (!block_hash.IsNull()) {
            for (const CTxIn& txin : tx.vin) {
                std::pair<TxSpends::const_iterator, TxSpends::const_iterator> range = mapTxSpends.equal_range(txin.prevout);
                while (range.first != range.second) {
                    if (range.first->second != tx.GetHash()) {
                        WalletLogPrintf("Transaction %s (in block %s) conflicts with wallet transaction %s (both spend %s:%i)\n", tx.GetHash().ToString(), block_hash.ToString(), range.first->second.ToString(), range.first->first.hash.ToString(), range.first->first.n);
                        MarkConflicted(block_hash, range.first->second);
                    }
                    range.first++;
                }
            }
        }

        bool fExisted = mapWallet.count(tx.GetHash()) != 0;
        if (fExisted && !fUpdate) return false;
        if (fExisted || IsMine(tx) || IsFromMe(tx))
        {
            /* Check if any keys in the wallet keypool that were supposed to be unused
             * have appeared in a new transaction. If so, remove those keys from the keypool.
             * This can happen when restoring an old wallet backup that does not contain
             * the mostly recently created transactions from newer versions of the wallet.
             */

            WalletBatch batch(*database);
            // loop though all outputs
            for (const CTxOut& txout: tx.vout) {
                // extract addresses, check if they match with an unused keypool key, update metadata if needed
                std::vector<CKeyID> vAffected;
                CAffectedKeysVisitor(*this, vAffected).Process(txout.scriptPubKey);
                for (const CKeyID &keyid : vAffected) {
                    std::map<CKeyID, int64_t>::const_iterator mi = m_pool_key_to_index.find(keyid);
                    if (mi != m_pool_key_to_index.end()) {
                        WalletLogPrintf("%s: Detected a used keypool key, mark all keypool key up to this key as used\n", __func__);
                        MarkReserveKeysAsUsed(mi->second);

                        if (!TopUpKeyPool()) {
                            WalletLogPrintf("%s: Topping up keypool failed (locked wallet)\n", __func__);
                        }
                    }
                    if (!block_hash.IsNull()) {
                        int64_t block_time;
                        bool found_block = chain().findBlock(block_hash, nullptr /* block */, &block_time);
                        assert(found_block);
                        if (mapKeyMetadata[keyid].nCreateTime > block_time) {
                            WalletLogPrintf("%s: Found a key which appears to be used earlier than we expected, updating metadata\n", __func__);
                            CPubKey vchPubKey;
                            bool res = GetPubKey(keyid, vchPubKey);
                            assert(res); // this should never fail
                            mapKeyMetadata[keyid].nCreateTime = block_time;
                            batch.WriteKeyMetadata(mapKeyMetadata[keyid], vchPubKey, true);
                            UpdateTimeFirstKey(block_time);
                        }
                    }
                }
            }

            CWalletTx wtx(this, ptx);

            // Get merkle branch if transaction was found in a block
            if (!block_hash.IsNull())
                wtx.SetMerkleBranch(block_hash, posInBlock);

            return AddToWallet(wtx, false);
        }
    }
    return false;
}

bool CWallet::TransactionCanBeAbandoned(const uint256& hashTx) const
{
    auto locked_chain = chain().lock();
    LOCK(cs_wallet);
    const CWalletTx* wtx = GetWalletTx(hashTx);
    return wtx && !wtx->isAbandoned() && wtx->GetDepthInMainChain(*locked_chain) == 0 && !wtx->InMempool();
}

void CWallet::MarkInputsDirty(const CTransactionRef& tx)
{
    for (const CTxIn& txin : tx->vin) {
        auto it = mapWallet.find(txin.prevout.hash);
        if (it != mapWallet.end()) {
            it->second.MarkDirty();
        }
    }
}

bool CWallet::AbandonTransaction(interfaces::Chain::Lock& locked_chain, const uint256& hashTx)
{
    auto locked_chain_recursive = chain().lock();  // Temporary. Removed in upcoming lock cleanup
    LOCK(cs_wallet);

    WalletBatch batch(*database, "r+");

    std::set<uint256> todo;
    std::set<uint256> done;

    // Can't mark abandoned if confirmed or in mempool
    auto it = mapWallet.find(hashTx);
    assert(it != mapWallet.end());
    CWalletTx& origtx = it->second;
    if (origtx.GetDepthInMainChain(locked_chain) != 0 || origtx.InMempool() || origtx.IsLockedByInstantSend()) {
        return false;
    }

    todo.insert(hashTx);

    while (!todo.empty()) {
        uint256 now = *todo.begin();
        todo.erase(now);
        done.insert(now);
        auto it = mapWallet.find(now);
        assert(it != mapWallet.end());
        CWalletTx& wtx = it->second;
        int currentconfirm = wtx.GetDepthInMainChain(locked_chain);
        // If the orig tx was not in block, none of its spends can be
        assert(currentconfirm <= 0);
        // if (currentconfirm < 0) {Tx and spends are already conflicted, no need to abandon}
        if (currentconfirm == 0 && !wtx.isAbandoned()) {
            // If the orig tx was not in block/mempool, none of its spends can be in mempool
            assert(!wtx.InMempool());
            wtx.nIndex = -1;
            wtx.setAbandoned();
            wtx.MarkDirty();
            batch.WriteTx(wtx);
            NotifyTransactionChanged(this, wtx.GetHash(), CT_UPDATED);
            // Iterate over all its outputs, and mark transactions in the wallet that spend them abandoned too
            TxSpends::const_iterator iter = mapTxSpends.lower_bound(COutPoint(now, 0));
            while (iter != mapTxSpends.end() && iter->first.hash == now) {
                if (!done.count(iter->second)) {
                    todo.insert(iter->second);
                }
                iter++;
            }
            // If a transaction changes 'conflicted' state, that changes the balance
            // available of the outputs it spends. So force those to be recomputed
            MarkInputsDirty(wtx.tx);
        }
    }

    fAnonymizableTallyCached = false;
    fAnonymizableTallyCachedNonDenom = false;

    return true;
}

void CWallet::MarkConflicted(const uint256& hashBlock, const uint256& hashTx)
{
    auto locked_chain = chain().lock();
    LockAnnotation lock(::cs_main);
    LOCK(cs_wallet); // check WalletBatch::LoadWallet()

    int conflictconfirms = -locked_chain->getBlockDepth(hashBlock);
    // If number of conflict confirms cannot be determined, this means
    // that the block is still unknown or not yet part of the main chain,
    // for example when loading the wallet during a reindex. Do nothing in that
    // case.
    if (conflictconfirms >= 0)
        return;

    // Do not flush the wallet here for performance reasons
    WalletBatch batch(*database, "r+", false);

    std::set<uint256> todo;
    std::set<uint256> done;

    todo.insert(hashTx);

    while (!todo.empty()) {
        uint256 now = *todo.begin();
        todo.erase(now);
        done.insert(now);
        auto it = mapWallet.find(now);
        assert(it != mapWallet.end());
        CWalletTx& wtx = it->second;
        int currentconfirm = wtx.GetDepthInMainChain(*locked_chain);
        if (conflictconfirms < currentconfirm) {
            // Block is 'more conflicted' than current confirm; update.
            // Mark transaction as conflicted with this block.
            wtx.nIndex = -1;
            wtx.hashBlock = hashBlock;
            wtx.MarkDirty();
            batch.WriteTx(wtx);
            // Iterate over all its outputs, and mark transactions in the wallet that spend them conflicted too
            TxSpends::const_iterator iter = mapTxSpends.lower_bound(COutPoint(now, 0));
            while (iter != mapTxSpends.end() && iter->first.hash == now) {
                 if (!done.count(iter->second)) {
                     todo.insert(iter->second);
                 }
                 iter++;
            }
            // If a transaction changes 'conflicted' state, that changes the balance
            // available of the outputs it spends. So force those to be recomputed
            MarkInputsDirty(wtx.tx);
        }
    }

    fAnonymizableTallyCached = false;
    fAnonymizableTallyCachedNonDenom = false;
}

void CWallet::SyncTransaction(const CTransactionRef& ptx, const uint256& block_hash, int posInBlock, bool update_tx) {
    if (!AddToWalletIfInvolvingMe(ptx, block_hash, posInBlock, update_tx))
        return; // Not one of ours

    // If a transaction changes 'conflicted' state, that changes the balance
    // available of the outputs it spends. So force those to be
    // recomputed, also:
    MarkInputsDirty(ptx);

    fAnonymizableTallyCached = false;
    fAnonymizableTallyCachedNonDenom = false;
}

void CWallet::TransactionAddedToMempool(const CTransactionRef& ptx, int64_t nAcceptTime) {
    auto locked_chain = chain().lock();
    LOCK(cs_wallet);
    SyncTransaction(ptx, {} /* block hash */, 0 /* position in block */);

    auto it = mapWallet.find(ptx->GetHash());
    if (it != mapWallet.end()) {
        it->second.fInMempool = true;
    }
}

void CWallet::TransactionRemovedFromMempool(const CTransactionRef &ptx, MemPoolRemovalReason reason) {
    if (reason != MemPoolRemovalReason::CONFLICT) {
        LOCK(cs_wallet);
        auto it = mapWallet.find(ptx->GetHash());
        if (it != mapWallet.end()) {
            it->second.fInMempool = false;
        }
    }
}

void CWallet::BlockConnected(const CBlock& block, const std::vector<CTransactionRef>& vtxConflicted) {
    const uint256& block_hash = block.GetHash();
    auto locked_chain = chain().lock();
    LOCK(cs_wallet);
    // TODO: Temporarily ensure that mempool removals are notified before
    // connected transactions.  This shouldn't matter, but the abandoned
    // state of transactions in our wallet is currently cleared when we
    // receive another notification and there is a race condition where
    // notification of a connected conflict might cause an outside process
    // to abandon a transaction and then have it inadvertently cleared by
    // the notification that the conflicted transaction was evicted.

    for (const CTransactionRef& ptx : vtxConflicted) {
        SyncTransaction(ptx, {} /* block hash */, 0 /* position in block */);
        // MANUAL because it's a manual removal, not using mempool logic
        TransactionRemovedFromMempool(ptx, MemPoolRemovalReason::MANUAL);
    }
    for (size_t i = 0; i < block.vtx.size(); i++) {
        SyncTransaction(block.vtx[i], block_hash, i);
        // MANUAL because it's a manual removal, not using mempool logic
        TransactionRemovedFromMempool(block.vtx[i], MemPoolRemovalReason::MANUAL);
    }

    m_last_block_processed = block_hash;

    // reset cache to make sure no longer immature coins are included
    fAnonymizableTallyCached = false;
    fAnonymizableTallyCachedNonDenom = false;
}

void CWallet::BlockDisconnected(const CBlock& block) {
    auto locked_chain = chain().lock();
    LOCK(cs_wallet);

    for (const CTransactionRef& ptx : block.vtx) {
        // NOTE: do NOT pass pindex here
        SyncTransaction(ptx, {} /* block hash */, 0 /* position in block */);
    }

    // reset cache to make sure no longer mature coins are excluded
    fAnonymizableTallyCached = false;
    fAnonymizableTallyCachedNonDenom = false;
}

void CWallet::UpdatedBlockTip()
{
    m_best_block_time = GetTime();
}


void CWallet::BlockUntilSyncedToCurrentChain() {
    AssertLockNotHeld(cs_main);
    AssertLockNotHeld(cs_wallet);

    {
        // Skip the queue-draining stuff if we know we're caught up with
        // ::ChainActive().Tip()...
        // We could also take cs_wallet here, and call m_last_block_processed
        // protected by cs_wallet instead of cs_main, but as long as we need
        // cs_main here anyway, it's easier to just call it cs_main-protected.
        auto locked_chain = chain().lock();

        if (!m_last_block_processed.IsNull() && locked_chain->isPotentialTip(m_last_block_processed)) {
            return;
        }
    }

    // ...otherwise put a callback in the validation interface queue and wait
    // for the queue to drain enough to execute it (indicating we are caught up
    // at least with the time we entered this function).
    chain().waitForNotifications();
}


isminetype CWallet::IsMine(const CTxIn &txin) const
{
    {
        LOCK(cs_wallet);
        std::map<uint256, CWalletTx>::const_iterator mi = mapWallet.find(txin.prevout.hash);
        if (mi != mapWallet.end())
        {
            const CWalletTx& prev = (*mi).second;
            if (txin.prevout.n < prev.tx->vout.size())
                return IsMine(prev.tx->vout[txin.prevout.n]);
        }
    }
    return ISMINE_NO;
}

// Note that this function doesn't distinguish between a 0-valued input,
// and a not-"is mine" (according to the filter) input.
CAmount CWallet::GetDebit(const CTxIn &txin, const isminefilter& filter) const
{
    {
        LOCK(cs_wallet);
        std::map<uint256, CWalletTx>::const_iterator mi = mapWallet.find(txin.prevout.hash);
        if (mi != mapWallet.end())
        {
            const CWalletTx& prev = (*mi).second;
            if (txin.prevout.n < prev.tx->vout.size())
                if (IsMine(prev.tx->vout[txin.prevout.n]) & filter)
                    return prev.tx->vout[txin.prevout.n].nValue;
        }
    }
    return 0;
}

// Recursively determine the rounds of a given input (How deep is the CoinJoin chain for a given input)
int CWallet::GetRealOutpointCoinJoinRounds(const COutPoint& outpoint, int nRounds) const
{
    LOCK(cs_wallet);

    const int nRoundsMax = MAX_COINJOIN_ROUNDS + CCoinJoinClientOptions::GetRandomRounds();

    if (nRounds >= nRoundsMax) {
        // there can only be nRoundsMax rounds max
        return nRoundsMax - 1;
    }

    auto pair = mapOutpointRoundsCache.emplace(outpoint, -10);
    auto nRoundsRef = &pair.first->second;
    if (!pair.second) {
        // we already processed it, just return what we have
        return *nRoundsRef;
    }

    // TODO wtx should refer to a CWalletTx object, not a pointer, based on surrounding code
    const CWalletTx* wtx = GetWalletTx(outpoint.hash);

    if (wtx == nullptr || wtx->tx == nullptr) {
        // no such tx in this wallet
        *nRoundsRef = -1;
        LogPrint(BCLog::COINJOIN, "%s FAILED    %-70s %3d\n", __func__, outpoint.ToStringShort(), -1);
        return *nRoundsRef;
    }

    // bounds check
    if (outpoint.n >= wtx->tx->vout.size()) {
        // should never actually hit this
        *nRoundsRef = -4;
        LogPrint(BCLog::COINJOIN, "%s FAILED    %-70s %3d\n", __func__, outpoint.ToStringShort(), -4);
        return *nRoundsRef;
    }

    auto txOutRef = &wtx->tx->vout[outpoint.n];

    if (CCoinJoin::IsCollateralAmount(txOutRef->nValue)) {
        *nRoundsRef = -3;
        LogPrint(BCLog::COINJOIN, "%s UPDATED   %-70s %3d\n", __func__, outpoint.ToStringShort(), *nRoundsRef);
        return *nRoundsRef;
    }

    // make sure the final output is non-denominate
    if (!CCoinJoin::IsDenominatedAmount(txOutRef->nValue)) { //NOT DENOM
        *nRoundsRef = -2;
        LogPrint(BCLog::COINJOIN, "%s UPDATED   %-70s %3d\n", __func__, outpoint.ToStringShort(), *nRoundsRef);
        return *nRoundsRef;
    }

    for (const auto& out : wtx->tx->vout) {
        if (!CCoinJoin::IsDenominatedAmount(out.nValue)) {
            // this one is denominated but there is another non-denominated output found in the same tx
            *nRoundsRef = 0;
            LogPrint(BCLog::COINJOIN, "%s UPDATED   %-70s %3d\n", __func__, outpoint.ToStringShort(), *nRoundsRef);
            return *nRoundsRef;
        }
    }

    int nShortest = -10; // an initial value, should be no way to get this by calculations
    bool fDenomFound = false;
    // only denoms here so let's look up
    for (const auto& txinNext : wtx->tx->vin) {
        if (IsMine(txinNext)) {
            int n = GetRealOutpointCoinJoinRounds(txinNext.prevout, nRounds + 1);
            // denom found, find the shortest chain or initially assign nShortest with the first found value
            if(n >= 0 && (n < nShortest || nShortest == -10)) {
                nShortest = n;
                fDenomFound = true;
            }
        }
    }
    *nRoundsRef = fDenomFound
            ? (nShortest >= nRoundsMax - 1 ? nRoundsMax : nShortest + 1) // good, we a +1 to the shortest one but only nRoundsMax rounds max allowed
            : 0;            // too bad, we are the fist one in that chain
    LogPrint(BCLog::COINJOIN, "%s UPDATED   %-70s %3d\n", __func__, outpoint.ToStringShort(), *nRoundsRef);
    return *nRoundsRef;
}

// respect current settings
int CWallet::GetCappedOutpointCoinJoinRounds(const COutPoint& outpoint) const
{
    LOCK(cs_wallet);
    int realCoinJoinRounds = GetRealOutpointCoinJoinRounds(outpoint);
    return realCoinJoinRounds > CCoinJoinClientOptions::GetRounds() ? CCoinJoinClientOptions::GetRounds() : realCoinJoinRounds;
}

bool CWallet::IsDenominated(const COutPoint& outpoint) const
{
    LOCK(cs_wallet);

    const auto it = mapWallet.find(outpoint.hash);
    if (it == mapWallet.end()) {
        return false;
    }

    if (outpoint.n >= it->second.tx->vout.size()) {
        return false;
    }

    return CCoinJoin::IsDenominatedAmount(it->second.tx->vout[outpoint.n].nValue);
}

bool CWallet::IsFullyMixed(const COutPoint& outpoint) const
{
    int nRounds = GetRealOutpointCoinJoinRounds(outpoint);
    // Mix again if we don't have N rounds yet
    if (nRounds < CCoinJoinClientOptions::GetRounds()) return false;

    // Try to mix a "random" number of rounds more than minimum.
    // If we have already mixed N + MaxOffset rounds, don't mix again.
    // Otherwise, we should mix again 50% of the time, this results in an exponential decay
    // N rounds 50% N+1 25% N+2 12.5%... until we reach N + GetRandomRounds() rounds where we stop.
    if (nRounds < CCoinJoinClientOptions::GetRounds() + CCoinJoinClientOptions::GetRandomRounds()) {
        CDataStream ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << outpoint << nCoinJoinSalt;
        uint256 nHash;
        CSHA256().Write((const unsigned char*)ss.data(), ss.size()).Finalize(nHash.begin());
        if (ReadLE64(nHash.begin()) % 2 == 0) {
            return false;
        }
    }

    return true;
}

isminetype CWallet::IsMine(const CTxOut& txout) const
{
    return ::IsMine(*this, txout.scriptPubKey);
}

CAmount CWallet::GetCredit(const CTxOut& txout, const isminefilter& filter) const
{
    if (!MoneyRange(txout.nValue))
        throw std::runtime_error(std::string(__func__) + ": value out of range");
    return ((IsMine(txout) & filter) ? txout.nValue : 0);
}

bool CWallet::IsChange(const CTxOut& txout) const
{
    return IsChange(txout.scriptPubKey);
}

bool CWallet::IsChange(const CScript& script) const
{
    // TODO: fix handling of 'change' outputs. The assumption is that any
    // payment to a script that is ours, but is not in the address book
    // is change. That assumption is likely to break when we implement multisignature
    // wallets that return change back into a multi-signature-protected address;
    // a better way of identifying which outputs are 'the send' and which are
    // 'the change' will need to be implemented (maybe extend CWalletTx to remember
    // which output, if any, was change).
    if (::IsMine(*this, script))
    {
        CTxDestination address;
        if (!ExtractDestination(script, address))
            return true;

        LOCK(cs_wallet);
        if (!mapAddressBook.count(address))
            return true;
    }
    return false;
}

CAmount CWallet::GetChange(const CTxOut& txout) const
{
    if (!MoneyRange(txout.nValue))
        throw std::runtime_error(std::string(__func__) + ": value out of range");
    return (IsChange(txout) ? txout.nValue : 0);
}

void CWallet::GenerateNewHDChain(const SecureString& secureMnemonic, const SecureString& secureMnemonicPassphrase)
{
    assert(!IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS));
    CHDChain newHdChain;

    // NOTE: an empty mnemonic means "generate a new one for me"
    // NOTE: default mnemonic passphrase is an empty string
    if (!newHdChain.SetMnemonic(secureMnemonic, secureMnemonicPassphrase, true)) {
        throw std::runtime_error(std::string(__func__) + ": SetMnemonic failed");
    }

    // add default account
    newHdChain.AddAccount();
    newHdChain.Debug(__func__);

    if (!SetHDChainSingle(newHdChain, false)) {
        throw std::runtime_error(std::string(__func__) + ": SetHDChainSingle failed");
    }

    if (!NewKeyPool()) {
        throw std::runtime_error(std::string(__func__) + ": NewKeyPool failed");
    }
}

bool CWallet::GenerateNewHDChainEncrypted(const SecureString& secureMnemonic, const SecureString& secureMnemonicPassphrase, const SecureString& secureWalletPassphrase)
{
    assert(!IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS));
    LOCK(cs_wallet);

    if (!IsCrypted()) {
        return false;
    }

    CCrypter crypter;
    CKeyingMaterial vMasterKey;
    CHDChain hdChainTmp;

    // NOTE: an empty mnemonic means "generate a new one for me"
    // NOTE: default mnemonic passphrase is an empty string
    if (!hdChainTmp.SetMnemonic(secureMnemonic, secureMnemonicPassphrase, true)) {
        throw std::runtime_error(std::string(__func__) + ": SetMnemonic failed");
    }

    // add default account
    hdChainTmp.AddAccount();
    hdChainTmp.Debug(__func__);

    for (const MasterKeyMap::value_type& pMasterKey : mapMasterKeys) {
        if (!crypter.SetKeyFromPassphrase(secureWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod)) {
            return false;
        }
        // get vMasterKey to encrypt new hdChain
        if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, vMasterKey)) {
            continue; // try another master key
        }

        bool res = EncryptHDChain(vMasterKey, hdChainTmp);
        assert(res);

        CHDChain hdChainCrypted;
        res = GetHDChain(hdChainCrypted);
        assert(res);

        DBG(
            tfm::format(std::cout, "GenerateNewHDChainEncrypted -- current seed: '%s'\n", HexStr(hdChainTmp.GetSeed()));
            tfm::format(std::cout, "GenerateNewHDChainEncrypted -- crypted seed: '%s'\n", HexStr(hdChainCrypted.GetSeed()));
        );

        // ids should match, seed hashes should not
        assert(hdChainTmp.GetID() == hdChainCrypted.GetID());
        assert(hdChainTmp.GetSeedHash() != hdChainCrypted.GetSeedHash());

        hdChainCrypted.Debug(__func__);

        if (SetCryptedHDChainSingle(hdChainCrypted, false)) {
            Lock();
            if (!Unlock(secureWalletPassphrase)) {
                // this should never happen
                throw std::runtime_error(std::string(__func__) + ": Unlock failed");
            }
            if (!NewKeyPool()) {
                throw std::runtime_error(std::string(__func__) + ": NewKeyPool failed");
            }
            Lock();
            return true;
        }
    }

    return false;
}

bool CWallet::SetHDChain(WalletBatch &batch, const CHDChain& chain, bool memonly)
{
    LOCK(cs_wallet);

    if (!CCryptoKeyStore::SetHDChain(chain))
        return false;

    if (!memonly) {
        if (!batch.WriteHDChain(chain)) {
            throw std::runtime_error(std::string(__func__) + ": WriteHDChain failed");
        }

        UnsetWalletFlag(batch, WALLET_FLAG_BLANK_WALLET);
    }

    return true;
}

bool CWallet::SetCryptedHDChain(WalletBatch &batch, const CHDChain& chain, bool memonly)
{
    LOCK(cs_wallet);

    if (!CCryptoKeyStore::SetCryptedHDChain(chain))
        return false;

    if (!memonly) {
        if (encrypted_batch) {
            if (!encrypted_batch->WriteCryptedHDChain(chain))
                throw std::runtime_error(std::string(__func__) + ": WriteCryptedHDChain failed");
        } else {
            if (!batch.WriteCryptedHDChain(chain))
                throw std::runtime_error(std::string(__func__) + ": WriteCryptedHDChain failed");
        }
        UnsetWalletFlag(batch, WALLET_FLAG_BLANK_WALLET);
    }

    return true;
}

bool CWallet::SetHDChainSingle(const CHDChain& chain, bool memonly)
{
    WalletBatch batch(*database);
    return SetHDChain(batch, chain, memonly);
}

bool CWallet::SetCryptedHDChainSingle(const CHDChain& chain, bool memonly)
{
    WalletBatch batch(*database);
    return SetCryptedHDChain(batch, chain, memonly);
}

bool CWallet::GetDecryptedHDChain(CHDChain& hdChainRet)
{
    LOCK(cs_wallet);

    CHDChain hdChainTmp;
    if (!GetHDChain(hdChainTmp)) {
        return false;
    }

    if (!DecryptHDChain(hdChainTmp))
        return false;

    // make sure seed matches this chain
    if (hdChainTmp.GetID() != hdChainTmp.GetSeedHash())
        return false;

    hdChainRet = hdChainTmp;

    return true;
}

bool CWallet::IsHDEnabled() const
{
    CHDChain hdChainCurrent;
    return GetHDChain(hdChainCurrent);
}

bool CWallet::IsMine(const CTransaction& tx) const
{
    for (const CTxOut& txout : tx.vout)
        if (IsMine(txout))
            return true;
    return false;
}

bool CWallet::IsFromMe(const CTransaction& tx) const
{
    return (GetDebit(tx, ISMINE_ALL) > 0);
}

CAmount CWallet::GetDebit(const CTransaction& tx, const isminefilter& filter) const
{
    CAmount nDebit = 0;
    for (const CTxIn& txin : tx.vin)
    {
        nDebit += GetDebit(txin, filter);
        if (!MoneyRange(nDebit))
            throw std::runtime_error(std::string(__func__) + ": value out of range");
    }
    return nDebit;
}

bool CWallet::IsAllFromMe(const CTransaction& tx, const isminefilter& filter) const
{
    LOCK(cs_wallet);

    for (const CTxIn& txin : tx.vin)
    {
        auto mi = mapWallet.find(txin.prevout.hash);
        if (mi == mapWallet.end())
            return false; // any unknown inputs can't be from us

        const CWalletTx& prev = (*mi).second;

        if (txin.prevout.n >= prev.tx->vout.size())
            return false; // invalid input!

        if (!(IsMine(prev.tx->vout[txin.prevout.n]) & filter))
            return false;
    }
    return true;
}

CAmount CWallet::GetCredit(const CTransaction& tx, const isminefilter& filter) const
{
    CAmount nCredit = 0;
    for (const CTxOut& txout : tx.vout)
    {
        nCredit += GetCredit(txout, filter);
        if (!MoneyRange(nCredit))
            throw std::runtime_error(std::string(__func__) + ": value out of range");
    }
    return nCredit;
}

CAmount CWallet::GetChange(const CTransaction& tx) const
{
    CAmount nChange = 0;
    for (const CTxOut& txout : tx.vout)
    {
        nChange += GetChange(txout);
        if (!MoneyRange(nChange))
            throw std::runtime_error(std::string(__func__) + ": value out of range");
    }
    return nChange;
}

bool CWallet::CanGenerateKeys()
{
    LOCK(cs_wallet);
    if (IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS) || IsWalletFlagSet(WALLET_FLAG_BLANK_WALLET)) {
        return false;
    }
    return true;
}

bool CWallet::CanGetAddresses(bool internal)
{
    LOCK(cs_wallet);
    // Check if the keypool has keys
    bool keypool_has_keys;
    if (internal && CanSupportFeature(FEATURE_HD)) {
        keypool_has_keys = setInternalKeyPool.size() > 0;
    } else {
        keypool_has_keys = KeypoolCountExternalKeys() > 0;
    }
    // If the keypool doesn't have keys, check if we can generate them
    if (!keypool_has_keys) {
        return CanGenerateKeys();
    }
    return keypool_has_keys;
}

void CWallet::SetWalletFlag(uint64_t flags)
{
    LOCK(cs_wallet);
    m_wallet_flags |= flags;
    if (!WalletBatch(*database).WriteWalletFlags(m_wallet_flags))
        throw std::runtime_error(std::string(__func__) + ": writing wallet flags failed");
}

void CWallet::UnsetWalletFlag(uint64_t flag)
{
    LOCK(cs_wallet);
    WalletBatch batch(*database);
    UnsetWalletFlag(batch, flag);
}

void CWallet::UnsetWalletFlag(WalletBatch& batch, uint64_t flag)
{
    LOCK(cs_wallet);
    m_wallet_flags &= ~flag;
    if (!batch.WriteWalletFlags(m_wallet_flags))
        throw std::runtime_error(std::string(__func__) + ": writing wallet flags failed");
}

bool CWallet::IsWalletFlagSet(uint64_t flag)
{
    return (m_wallet_flags & flag);
}

bool CWallet::SetWalletFlags(uint64_t overwriteFlags, bool memonly)
{
    LOCK(cs_wallet);
    m_wallet_flags = overwriteFlags;
    if (((overwriteFlags & g_known_wallet_flags) >> 32) ^ (overwriteFlags >> 32)) {
        // contains unknown non-tolerable wallet flags
        return false;
    }
    if (!memonly && !WalletBatch(*database).WriteWalletFlags(m_wallet_flags)) {
        throw std::runtime_error(std::string(__func__) + ": writing wallet flags failed");
    }

    return true;
}

int64_t CWalletTx::GetTxTime() const
{
    int64_t n = nTimeSmart;
    return n ? n : nTimeReceived;
}

// Helper for producing a max-sized low-S low-R signature (eg 71 bytes)
// or a max-sized low-S signature (e.g. 72 bytes) if use_max_sig is true
bool CWallet::DummySignInput(CTxIn &tx_in, const CTxOut &txout, bool use_max_sig) const
{
    // Fill in dummy signatures for fee calculation.
    const CScript& scriptPubKey = txout.scriptPubKey;
    SignatureData sigdata;

    if (!ProduceSignature(*this, use_max_sig ? DUMMY_MAXIMUM_SIGNATURE_CREATOR : DUMMY_SIGNATURE_CREATOR, scriptPubKey, sigdata)) {
        return false;
    }
    UpdateInput(tx_in, sigdata);
    return true;
}

// Helper for producing a bunch of max-sized low-S low-R signatures (eg 71 bytes)
bool CWallet::DummySignTx(CMutableTransaction &txNew, const std::vector<CTxOut> &txouts, bool use_max_sig) const
{
    // Fill in dummy signatures for fee calculation.
    int nIn = 0;
    for (const auto& txout : txouts)
    {
        if (!DummySignInput(txNew.vin[nIn], txout, use_max_sig)) {
            return false;
        }

        nIn++;
    }
    return true;
}

bool CWallet::ImportScripts(const std::set<CScript> scripts)
{
    WalletBatch batch(*database);
    for (const auto& entry : scripts) {
        if (!HaveCScript(CScriptID(entry)) && !AddCScriptWithDB(batch, entry)) {
            return false;
        }
    }
    return true;
}

bool CWallet::ImportPrivKeys(const std::map<CKeyID, CKey>& privkey_map, const int64_t timestamp)
{
    WalletBatch batch(*database);
    for (const auto& entry : privkey_map) {
        const CKey& key = entry.second;
        CPubKey pubkey = key.GetPubKey();
        const CKeyID& id = entry.first;
        assert(key.VerifyPubKey(pubkey));
        mapKeyMetadata[id].nCreateTime = timestamp;
        // If the private key is not present in the wallet, insert it.
        if (!HaveKey(id) && !AddKeyPubKeyWithDB(batch, key, pubkey)) {
            return false;
        }
        UpdateTimeFirstKey(timestamp);
    }
    return true;
}

bool CWallet::ImportPubKeys(const std::vector<CKeyID>& ordered_pubkeys, const std::map<CKeyID, CPubKey>& pubkey_map, const std::map<CKeyID, std::pair<CPubKey, KeyOriginInfo>>& key_origins, const bool add_keypool, const bool internal, const int64_t timestamp)
{
    WalletBatch batch(*database);
    for (const CKeyID& id : ordered_pubkeys) {
        auto entry = pubkey_map.find(id);
        if (entry == pubkey_map.end()) {
            continue;
        }
        const CPubKey& pubkey = entry->second;
        CPubKey temp;
        if (!GetPubKey(id, temp) && !AddWatchOnlyWithDB(batch, GetScriptForRawPubKey(pubkey), timestamp)) {
            return false;
        }
        mapKeyMetadata[id].nCreateTime = timestamp;
        // Add to keypool only works with pubkeys
        if (add_keypool) {
            AddKeypoolPubkeyWithDB(pubkey, internal, batch);
            NotifyCanGetAddressesChanged();
        }
    }
    for (const auto& entry : key_origins) {
        AddKeyOrigin(entry.second.first, entry.second.second);
    }
    return true;
}

bool CWallet::ImportScriptPubKeys(const std::string& label, const std::set<CScript>& script_pub_keys, const bool have_solving_data, const bool internal, const int64_t timestamp)
{
    WalletBatch batch(*database);
    for (const CScript& script : script_pub_keys) {
        if (!have_solving_data || !::IsMine(*this, script)) { // Always call AddWatchOnly for non-solvable watch-only, so that watch timestamp gets updated
            if (!AddWatchOnlyWithDB(batch, script, timestamp)) {
                return false;
            }
        }
        CTxDestination dest;
        ExtractDestination(script, dest);
        if (!internal && IsValidDestination(dest)) {
            SetAddressBookWithDB(batch, dest, label, "receive");
        }
    }
    return true;
}

int64_t CalculateMaximumSignedTxSize(const CTransaction &tx, const CWallet *wallet, bool use_max_sig)
{
    std::vector<CTxOut> txouts;
    for (const CTxIn& input : tx.vin) {
        const auto mi = wallet->mapWallet.find(input.prevout.hash);
        // Can not estimate size without knowing the input details
        if (mi == wallet->mapWallet.end()) {
            return -1;
        }
        assert(input.prevout.n < mi->second.tx->vout.size());
        txouts.emplace_back(mi->second.tx->vout[input.prevout.n]);
    }
    return CalculateMaximumSignedTxSize(tx, wallet, txouts, use_max_sig);
}

// txouts needs to be in the order of tx.vin
int64_t CalculateMaximumSignedTxSize(const CTransaction &tx, const CWallet *wallet, const std::vector<CTxOut>& txouts, bool use_max_sig)
{
    CMutableTransaction txNew(tx);
    if (!wallet->DummySignTx(txNew, txouts, use_max_sig)) {
        return -1;
    }
    return ::GetSerializeSize(txNew, SER_NETWORK, PROTOCOL_VERSION);
}

int CalculateMaximumSignedInputSize(const CTxOut& txout, const CWallet* wallet, bool use_max_sig)
{
    CMutableTransaction txn;
    txn.vin.push_back(CTxIn(COutPoint()));
    if (!wallet->DummySignInput(txn.vin[0], txout, use_max_sig)) {
        // This should never happen, because IsAllFromMe(ISMINE_SPENDABLE)
        // implies that we can sign for every input.
        return -1;
    }
    return ::GetSerializeSize(txn.vin[0], SER_NETWORK, PROTOCOL_VERSION);
}

void CWalletTx::GetAmounts(std::list<COutputEntry>& listReceived,
                           std::list<COutputEntry>& listSent, CAmount& nFee, const isminefilter& filter) const
{
    nFee = 0;
    listReceived.clear();
    listSent.clear();

    // Compute fee:
    CAmount nDebit = GetDebit(filter);
    if (nDebit > 0) // debit>0 means we signed/sent this transaction
    {
        CAmount nValueOut = tx->GetValueOut();
        nFee = nDebit - nValueOut;
    }

    // Sent/received.
    for (unsigned int i = 0; i < tx->vout.size(); ++i)
    {
        const CTxOut& txout = tx->vout[i];
        isminetype fIsMine = pwallet->IsMine(txout);
        // Only need to handle txouts if AT LEAST one of these is true:
        //   1) they debit from us (sent)
        //   2) the output is to us (received)
        if (nDebit > 0)
        {
            // Don't report 'change' txouts
            if (pwallet->IsChange(txout))
                continue;
        }
        else if (!(fIsMine & filter))
            continue;

        // In either case, we need to get the destination address
        CTxDestination address;

        if (!ExtractDestination(txout.scriptPubKey, address) && !txout.scriptPubKey.IsUnspendable())
        {
            pwallet->WalletLogPrintf("CWalletTx::GetAmounts: Unknown transaction type found, txid %s\n",
                                    this->GetHash().ToString());
            address = CNoDestination();
        }

        COutputEntry output = {address, txout.nValue, (int)i};

        // If we are debited by the transaction, add the output as a "sent" entry
        if (nDebit > 0)
            listSent.push_back(output);

        // If we are receiving the output, add it as a "received" entry
        if (fIsMine & filter)
            listReceived.push_back(output);
    }

}

/**
 * Scan active chain for relevant transactions after importing keys. This should
 * be called whenever new keys are added to the wallet, with the oldest key
 * creation time.
 *
 * @return Earliest timestamp that could be successfully scanned from. Timestamp
 * returned will be higher than startTime if relevant blocks could not be read.
 */
int64_t CWallet::RescanFromTime(int64_t startTime, const WalletRescanReserver& reserver, bool update)
{
    // Find starting block. May be null if nCreateTime is greater than the
    // highest blockchain timestamp, in which case there is nothing that needs
    // to be scanned.
    uint256 start_block;
    {
        auto locked_chain = chain().lock();
        const Optional<int> start_height = locked_chain->findFirstBlockWithTimeAndHeight(startTime - TIMESTAMP_WINDOW, 0, &start_block);
        const Optional<int> tip_height = locked_chain->getHeight();
        WalletLogPrintf("%s: Rescanning last %i blocks\n", __func__, tip_height && start_height ? *tip_height - *start_height + 1 : 0);
    }

    if (!start_block.IsNull()) {
        // TODO: this should take into account failure by ScanResult::USER_ABORT
        ScanResult result = ScanForWalletTransactions(start_block, {} /* stop_block */, reserver, update);
        if (result.status == ScanResult::FAILURE) {
            int64_t time_max;
            if (!chain().findBlock(result.last_failed_block, nullptr /* block */, nullptr /* time */, &time_max)) {
                throw std::logic_error("ScanForWalletTransactions returned invalid block hash");
            }
            return time_max + TIMESTAMP_WINDOW + 1;
        }
    }
    return startTime;
}

/**
 * Scan the block chain (starting in start_block) for transactions
 * from or to us. If fUpdate is true, found transactions that already
 * exist in the wallet will be updated.
 *
 * @param[in] start_block Scan starting block. If block is not on the active
 *                        chain, the scan will return SUCCESS immediately.
 * @param[in] stop_block  Scan ending block. If block is not on the active
 *                        chain, the scan will continue until it reaches the
 *                        chain tip.
 *
 * @return ScanResult returning scan information and indicating success or
 *         failure. Return status will be set to SUCCESS if scan was
 *         successful. FAILURE if a complete rescan was not possible (due to
 *         pruning or corruption). USER_ABORT if the rescan was aborted before
 *         it could complete.
 *
 * @pre Caller needs to make sure start_block (and the optional stop_block) are on
 * the main chain after to the addition of any new keys you want to detect
 * transactions for.
 */
CWallet::ScanResult CWallet::ScanForWalletTransactions(const uint256& start_block, const uint256& stop_block, const WalletRescanReserver& reserver, bool fUpdate)
{
    int64_t nNow = GetTime();
    int64_t start_time = GetTimeMillis();

    assert(reserver.isReserved());

    uint256 block_hash = start_block;
    ScanResult result;

    WalletLogPrintf("Rescan started from block %s...\n", start_block.ToString());

    fAbortRescan = false;
    ShowProgress(strprintf("%s " + _("Rescanning...").translated, GetDisplayName()), 0); // show rescan progress in GUI as dialog or on splashscreen, if -rescan on startup
    uint256 tip_hash;
    // The way the 'block_height' is initialized is just a workaround for the gcc bug #47679 since version 4.6.0.
    Optional<int> block_height = MakeOptional(false, int());
    double progress_begin;
    double progress_end;
    {
        auto locked_chain = chain().lock();
        if (Optional<int> tip_height = locked_chain->getHeight()) {
            tip_hash = locked_chain->getBlockHash(*tip_height);
        }
        block_height = locked_chain->getBlockHeight(block_hash);
        progress_begin = chain().guessVerificationProgress(block_hash);
        progress_end = chain().guessVerificationProgress(stop_block.IsNull() ? tip_hash : stop_block);
    }
    double progress_current = progress_begin;
    while (block_height && !fAbortRescan && !chain().shutdownRequested()) {
        m_scanning_progress = (progress_current - progress_begin) / (progress_end - progress_begin);
        if (*block_height % 100 == 0 && progress_end - progress_begin > 0.0) {
            ShowProgress(strprintf("%s " + _("Rescanning...").translated, GetDisplayName()), std::max(1, std::min(99, (int)(m_scanning_progress * 100))));
        }
        if (GetTime() >= nNow + 60) {
            nNow = GetTime();
            WalletLogPrintf("Still rescanning. At block %d. Progress=%f\n", *block_height, progress_current);
        }

        CBlock block;
        if (chain().findBlock(block_hash, &block) && !block.IsNull()) {
            auto locked_chain = chain().lock();
            LOCK(cs_wallet);
            if (!locked_chain->getBlockHeight(block_hash)) {
                // Abort scan if current block is no longer active, to prevent
                // marking transactions as coming from the wrong block.
                // TODO: This should return success instead of failure, see
                // https://github.com/bitcoin/bitcoin/pull/14711#issuecomment-458342518
                result.last_failed_block = block_hash;
                result.status = ScanResult::FAILURE;
                break;
            }
            for (size_t posInBlock = 0; posInBlock < block.vtx.size(); ++posInBlock) {
                SyncTransaction(block.vtx[posInBlock], block_hash, posInBlock, fUpdate);
            }
            // scan succeeded, record block as most recent successfully scanned
            result.last_scanned_block = block_hash;
            result.last_scanned_height = *block_height;
        } else {
            // could not scan block, keep scanning but record this block as the most recent failure
            result.last_failed_block = block_hash;
            result.status = ScanResult::FAILURE;
        }
        if (block_hash == stop_block) {
            break;
        }
        {
            auto locked_chain = chain().lock();
            Optional<int> tip_height = locked_chain->getHeight();
            if (!tip_height || *tip_height <= block_height || !locked_chain->getBlockHeight(block_hash)) {
                // break successfully when rescan has reached the tip, or
                // previous block is no longer on the chain due to a reorg
                break;
            }

            // increment block and verification progress
            block_hash = locked_chain->getBlockHash(++*block_height);
            progress_current = chain().guessVerificationProgress(block_hash);

            // handle updated tip hash
            const uint256 prev_tip_hash = tip_hash;
            tip_hash = locked_chain->getBlockHash(*tip_height);
            if (stop_block.IsNull() && prev_tip_hash != tip_hash) {
                // in case the tip has changed, update progress max
                progress_end = chain().guessVerificationProgress(tip_hash);
            }
        }
    }
    ShowProgress(strprintf("%s " + _("Rescanning...").translated, GetDisplayName()), 100); // hide progress dialog in GUI
    if (block_height && fAbortRescan) {
        WalletLogPrintf("Rescan aborted at block %d. Progress=%f\n", *block_height, progress_current);
        result.status = ScanResult::USER_ABORT;
    } else if (block_height && chain().shutdownRequested()) {
        WalletLogPrintf("Rescan interrupted by shutdown request at block %d. Progress=%f\n", *block_height, progress_current);
        result.status = ScanResult::USER_ABORT;
    } else {
        WalletLogPrintf("Rescan completed in %15dms\n", GetTimeMillis() - start_time);
    }
    return result;
}

void CWallet::ReacceptWalletTransactions(interfaces::Chain::Lock& locked_chain)
{
    // If transactions aren't being broadcasted, don't let them into local mempool either
    if (!fBroadcastTransactions)
        return;
    std::map<int64_t, CWalletTx*> mapSorted;

    // Sort pending wallet transactions based on their initial wallet insertion order
    for (std::pair<const uint256, CWalletTx>& item : mapWallet) {
        const uint256& wtxid = item.first;
        CWalletTx& wtx = item.second;
        assert(wtx.GetHash() == wtxid);

        int nDepth = wtx.GetDepthInMainChain(locked_chain);

        if (!wtx.IsCoinBase() && !wtx.IsCoinStake() && (nDepth == 0 && !wtx.IsLockedByInstantSend() && !wtx.isAbandoned())) {
            mapSorted.insert(std::make_pair(wtx.nOrderPos, &wtx));
        }
    }

    // Try to add wallet transactions to memory pool
    for (const std::pair<const int64_t, CWalletTx*>& item : mapSorted) {
        CWalletTx& wtx = *(item.second);
        std::string unused_err_string;
        wtx.SubmitMemoryPoolAndRelay(unused_err_string, false, locked_chain);
    }
}

bool CWalletTx::SubmitMemoryPoolAndRelay(std::string& err_string, bool relay, interfaces::Chain::Lock& locked_chain)
{
    // Can't relay if wallet is not broadcasting
    if (!pwallet->GetBroadcastTransactions()) return false;
    // Don't relay abandoned transactions
    if (isAbandoned()) return false;
    // Don't try to submit coinbase transactions. These would fail anyway but would
    // cause log spam.
    if (IsCoinBase()) return false;
    // Don't try to submit coinstake transactions.
    if (IsCoinStake()) return false;
    // Don't try to submit conflicted or confirmed transactions.
    if (GetDepthInMainChain(locked_chain) != 0) return false;
    // Don't try to submit transactions locked via InstantSend.
    if (IsLockedByInstantSend()) return false;

    // Submit transaction to mempool for relay
    pwallet->WalletLogPrintf("Submitting wtx %s to mempool for relay\n", GetHash().ToString());
    // We must set fInMempool here - while it will be re-set to true by the
    // entered-mempool callback, if we did not there would be a race where a
    // user could call sendmoney in a loop and hit spurious out of funds errors
    // because we think that this newly generated transaction's change is
    // unavailable as we're not yet aware that it is in the mempool.
    //
    // Irrespective of the failure reason, un-marking fInMempool
    // out-of-order is incorrect - it should be unmarked when
    // TransactionRemovedFromMempool fires.
    bool ret = pwallet->chain().broadcastTransaction(tx, err_string, pwallet->m_default_max_tx_fee, relay);
    fInMempool |= ret;
    return ret;
}

std::set<uint256> CWalletTx::GetConflicts() const
{
    std::set<uint256> result;
    if (pwallet != nullptr)
    {
        AssertLockHeld(pwallet->cs_wallet);
        uint256 myHash = GetHash();
        result = pwallet->GetConflicts(myHash);
        result.erase(myHash);
    }
    return result;
}

CAmount CWalletTx::GetCachableAmount(AmountType type, const isminefilter& filter, bool recalculate) const
{
    auto& amount = m_amounts[type];
    if (recalculate || !amount.m_cached[filter]) {
        amount.Set(filter, type == DEBIT ? pwallet->GetDebit(*tx, filter) : pwallet->GetCredit(*tx, filter));
    }
    return amount.m_value[filter];
}

CAmount CWalletTx::GetDebit(const isminefilter& filter) const
{
    if (tx->vin.empty())
        return 0;

    CAmount debit = 0;
    if (filter & ISMINE_SPENDABLE) {
        debit += GetCachableAmount(DEBIT, ISMINE_SPENDABLE);
    }
    if (filter & ISMINE_WATCH_ONLY) {
        debit += GetCachableAmount(DEBIT, ISMINE_WATCH_ONLY);
    }
    return debit;
}

CAmount CWalletTx::GetCredit(interfaces::Chain::Lock& locked_chain, const isminefilter& filter) const
{
    // Must wait until coinbase is safely deep enough in the chain before valuing it
    if (IsImmatureCoinBase(locked_chain))
        return 0;

    CAmount credit = 0;
    if (filter & ISMINE_SPENDABLE) {
        // GetBalance can assume transactions in mapWallet won't change
        credit += GetCachableAmount(CREDIT, ISMINE_SPENDABLE);
    }
    if (filter & ISMINE_WATCH_ONLY) {
        credit += GetCachableAmount(CREDIT, ISMINE_WATCH_ONLY);
    }
    return credit;
}

CAmount CWalletTx::GetImmatureCredit(interfaces::Chain::Lock& locked_chain, bool fUseCache) const
{
    if (IsImmatureCoinBase(locked_chain) && IsInMainChain(locked_chain)) {
        return GetCachableAmount(IMMATURE_CREDIT, ISMINE_SPENDABLE, !fUseCache);
    }

    return 0;
}

CAmount CWalletTx::GetAvailableCredit(interfaces::Chain::Lock& locked_chain, bool fUseCache, const isminefilter& filter) const
{
    if (pwallet == nullptr)
        return 0;

    // Avoid caching ismine for NO or ALL cases (could remove this check and simplify in the future).
    bool allow_cache = filter == ISMINE_SPENDABLE || filter == ISMINE_WATCH_ONLY;

    // Must wait until coinbase is safely deep enough in the chain before valuing it
    if (IsImmatureCoinBase(locked_chain))
        return 0;

    if (fUseCache && allow_cache && m_amounts[AVAILABLE_CREDIT].m_cached[filter]) {
        return m_amounts[AVAILABLE_CREDIT].m_value[filter];
    }

    CAmount nCredit = 0;
    uint256 hashTx = GetHash();
    for (unsigned int i = 0; i < tx->vout.size(); i++)
    {
        if (!pwallet->IsSpent(locked_chain, hashTx, i))
        {
            const CTxOut &txout = tx->vout[i];
            nCredit += pwallet->GetCredit(txout, filter);
            if (!MoneyRange(nCredit))
                throw std::runtime_error(std::string(__func__) + ": value out of range");
        }
    }

    if (allow_cache) {
        m_amounts[AVAILABLE_CREDIT].Set(filter, nCredit);
    }

    return nCredit;
}

CAmount CWalletTx::GetImmatureWatchOnlyCredit(interfaces::Chain::Lock& locked_chain, const bool fUseCache) const
{
    if (IsImmatureCoinBase(locked_chain) && IsInMainChain(locked_chain)) {
        return GetCachableAmount(IMMATURE_CREDIT, ISMINE_WATCH_ONLY, !fUseCache);
    }

    return 0;
}

CAmount CWalletTx::GetAnonymizedCredit(interfaces::Chain::Lock& locked_chain, const CCoinControl* coinControl) const
{
    if (!pwallet)
        return 0;

    AssertLockHeld(pwallet->cs_wallet);

    // Exclude coinbase and conflicted txes
    if (IsCoinBase() || GetDepthInMainChain(locked_chain) < 0)
        return 0;

    if (coinControl == nullptr && m_amounts[ANON_CREDIT].m_cached[ISMINE_SPENDABLE])
        return m_amounts[ANON_CREDIT].m_value[ISMINE_SPENDABLE];

    CAmount nCredit = 0;
    uint256 hashTx = GetHash();
    for (unsigned int i = 0; i < tx->vout.size(); i++)
    {
        const CTxOut &txout = tx->vout[i];
        const COutPoint outpoint = COutPoint(hashTx, i);

        if (coinControl != nullptr && coinControl->HasSelected() && !coinControl->IsSelected(outpoint)) {
            continue;
        }

        if (pwallet->IsSpent(locked_chain, hashTx, i) || !CCoinJoin::IsDenominatedAmount(txout.nValue)) continue;

        if (pwallet->IsFullyMixed(outpoint)) {
            nCredit += pwallet->GetCredit(txout, ISMINE_SPENDABLE);
            if (!MoneyRange(nCredit))
                throw std::runtime_error(std::string(__func__) + ": value out of range");
        }
    }

    if (coinControl == nullptr) {
        m_amounts[ANON_CREDIT].Set(ISMINE_SPENDABLE, nCredit);
    }

    return nCredit;
}

CAmount CWalletTx::GetDenominatedCredit(interfaces::Chain::Lock& locked_chain, bool unconfirmed, bool fUseCache) const
{
    if (pwallet == 0)
        return 0;

    AssertLockHeld(pwallet->cs_wallet);

    // Must wait until coinbase is safely deep enough in the chain before valuing it
    if (IsCoinBase() && GetBlocksToMaturity(locked_chain) > 0)
        return 0;

    int nDepth = GetDepthInMainChain(locked_chain);
    if (nDepth < 0) return 0;

    bool isUnconfirmed = IsTrusted(locked_chain) && nDepth == 0;
    if (unconfirmed != isUnconfirmed) return 0;

    if (fUseCache) {
        if(unconfirmed && m_amounts[DENOM_UCREDIT].m_cached[ISMINE_SPENDABLE]) {
            return m_amounts[DENOM_UCREDIT].m_value[ISMINE_SPENDABLE];
        } else if (!unconfirmed && m_amounts[DENOM_CREDIT].m_cached[ISMINE_SPENDABLE]) {
            return m_amounts[DENOM_CREDIT].m_value[ISMINE_SPENDABLE];
        }
    }

    CAmount nCredit = 0;
    uint256 hashTx = GetHash();
    for (unsigned int i = 0; i < tx->vout.size(); i++)
    {
        const CTxOut &txout = tx->vout[i];

        if (pwallet->IsSpent(locked_chain, hashTx, i) || !CCoinJoin::IsDenominatedAmount(txout.nValue)) continue;

        nCredit += pwallet->GetCredit(txout, ISMINE_SPENDABLE);
        if (!MoneyRange(nCredit))
            throw std::runtime_error(std::string(__func__) + ": value out of range");
    }

    if (unconfirmed) {
        m_amounts[DENOM_UCREDIT].Set(ISMINE_SPENDABLE, nCredit);
    } else {
        m_amounts[DENOM_CREDIT].Set(ISMINE_SPENDABLE, nCredit);
    }
    return nCredit;
}

CAmount CWalletTx::GetChange() const
{
    if (fChangeCached)
        return nChangeCached;
    nChangeCached = pwallet->GetChange(*tx);
    fChangeCached = true;
    return nChangeCached;
}

bool CWalletTx::InMempool() const
{
    return fInMempool;
}

bool CWalletTx::IsTrusted(interfaces::Chain::Lock& locked_chain) const
{
    // Quick answer in most cases
    if (!locked_chain.checkFinalTx(*tx))
        return false;
    int nDepth = GetDepthInMainChain(locked_chain);
    if (nDepth >= 1)
        return true;
    if (nDepth < 0)
        return false;
    if (IsLockedByInstantSend())
        return true;
    if (!pwallet->m_spend_zero_conf_change || !IsFromMe(ISMINE_ALL)) // using wtx's cached debit
        return false;

    // Don't trust unconfirmed transactions from us unless they are in the mempool.
    if (!InMempool())
        return false;

    // Trusted if all inputs are from us and are in the mempool:
    for (const CTxIn& txin : tx->vin)
    {
        // Transactions not sent by us: not trusted
        const CWalletTx* parent = pwallet->GetWalletTx(txin.prevout.hash);
        if (parent == nullptr)
            return false;
        const CTxOut& parentOut = parent->tx->vout[txin.prevout.n];
        if (pwallet->IsMine(parentOut) != ISMINE_SPENDABLE)
            return false;
    }
    return true;
}

bool CWalletTx::IsEquivalentTo(const CWalletTx& _tx) const
{
        CMutableTransaction tx1 {*this->tx};
        CMutableTransaction tx2 {*_tx.tx};
        for (auto& txin : tx1.vin) txin.scriptSig = CScript();
        for (auto& txin : tx2.vin) txin.scriptSig = CScript();
        return CTransaction(tx1) == CTransaction(tx2);
}

// Rebroadcast transactions from the wallet. We do this on a random timer
// to slightly obfuscate which transactions come from our wallet.
//
// Ideally, we'd only resend transactions that we think should have been
// mined in the most recent block. Any transaction that wasn't in the top
// blockweight of transactions in the mempool shouldn't have been mined,
// and so is probably just sitting in the mempool waiting to be confirmed.
// Rebroadcasting does nothing to speed up confirmation and only damages
// privacy.
void CWallet::ResendWalletTransactions()
{
    // During reindex, importing and IBD, old wallet transactions become
    // unconfirmed. Don't resend them as that would spam other nodes.
    if (!chain().isReadyToBroadcast()) return;

    // Do this infrequently and randomly to avoid giving away
    // that these are our transactions.
    if (GetTime() < nNextResend || !fBroadcastTransactions) return;
    bool fFirst = (nNextResend == 0);
    nNextResend = GetTime() + GetRand(30 * 60);
    if (fFirst) return;

    // Only do it if there's been a new block since last time
    if (m_best_block_time < nLastResend) return;
    nLastResend = GetTime();

    int submitted_tx_count = 0;

    { // locked_chain and cs_wallet scope
        auto locked_chain = chain().lock();
        LOCK(cs_wallet);

        // Relay transactions
        for (std::pair<const uint256, CWalletTx>& item : mapWallet) {
            CWalletTx& wtx = item.second;
            // Attempt to rebroadcast all txes more than 5 minutes older than
            // the last block. SubmitMemoryPoolAndRelay() will not rebroadcast
            // any confirmed or conflicting txs.
            if (wtx.nTimeReceived > m_best_block_time - 5 * 60) continue;
            std::string unused_err_string;
            if (wtx.SubmitMemoryPoolAndRelay(unused_err_string, true, *locked_chain)) ++submitted_tx_count;
        }
    } // locked_chain and cs_wallet

    if (submitted_tx_count > 0) {
        WalletLogPrintf("%s: resubmit %u unconfirmed transactions\n", __func__, submitted_tx_count);
    }
}

/** @} */ // end of mapWallet

void MaybeResendWalletTxs()
{
    for (const std::shared_ptr<CWallet>& pwallet : GetWallets()) {
        pwallet->ResendWalletTransactions();
    }
}


/** @defgroup Actions
 *
 * @{
 */


std::unordered_set<const CWalletTx*, WalletTxHasher> CWallet::GetSpendableTXs() const
{
    AssertLockHeld(cs_wallet);

    std::unordered_set<const CWalletTx*, WalletTxHasher> ret;
    for (auto it = setWalletUTXO.begin(); it != setWalletUTXO.end(); ) {
        const auto& outpoint = *it;
        const auto jt = mapWallet.find(outpoint.hash);
        if (jt != mapWallet.end()) {
            ret.emplace(&jt->second);
        }

        // setWalletUTXO is sorted by COutPoint, which means that all UTXOs for the same TX are neighbors
        // skip entries until we encounter a new TX
        while (it != setWalletUTXO.end() && it->hash == outpoint.hash) {
            ++it;
        }
    }
    return ret;
}

CWallet::Balance CWallet::GetBalance(const int min_depth, const bool fAddLocked, const CCoinControl* coinControl) const
{
    Balance ret;
    {
        auto locked_chain = chain().lock();
        LOCK(cs_wallet);
        for (auto pcoin : GetSpendableTXs()) {
            const bool is_trusted{pcoin->IsTrusted(*locked_chain)};
            const int tx_depth{pcoin->GetDepthInMainChain(*locked_chain)};
            const CAmount tx_credit_mine{pcoin->GetAvailableCredit(*locked_chain, /* fUseCache */ true, ISMINE_SPENDABLE)};
            const CAmount tx_credit_watchonly{pcoin->GetAvailableCredit(*locked_chain, /* fUseCache */ true, ISMINE_WATCH_ONLY)};
            if (is_trusted && ((tx_depth >= min_depth) || (fAddLocked && pcoin->IsLockedByInstantSend()))) {
                ret.m_mine_trusted += tx_credit_mine;
                ret.m_watchonly_trusted += tx_credit_watchonly;
            }
            if (!is_trusted && tx_depth == 0 && pcoin->InMempool()) {
                ret.m_mine_untrusted_pending += tx_credit_mine;
                ret.m_watchonly_untrusted_pending += tx_credit_watchonly;
            }
            ret.m_mine_immature += pcoin->GetImmatureCredit(*locked_chain);
            ret.m_watchonly_immature += pcoin->GetImmatureWatchOnlyCredit(*locked_chain);
            if (CCoinJoinClientOptions::IsEnabled()) {
                ret.m_anonymized += pcoin->GetAnonymizedCredit(*locked_chain, coinControl);
                ret.m_denominated_trusted += pcoin->GetDenominatedCredit(*locked_chain, false);
                ret.m_denominated_untrusted_pending += pcoin->GetDenominatedCredit(*locked_chain, true);
            }
        }
    }
    return ret;
}

CAmount CWallet::GetAnonymizableBalance(bool fSkipDenominated, bool fSkipUnconfirmed) const
{
    if (!CCoinJoinClientOptions::IsEnabled()) return 0;

    std::vector<CompactTallyItem> vecTally = SelectCoinsGroupedByAddresses(fSkipDenominated, true, fSkipUnconfirmed);
    if (vecTally.empty()) return 0;

    CAmount nTotal = 0;

    const CAmount nSmallestDenom = CCoinJoin::GetSmallestDenomination();
    const CAmount nMixingCollateral = CCoinJoin::GetCollateralAmount();
    for (const auto& item : vecTally) {
        bool fIsDenominated = CCoinJoin::IsDenominatedAmount(item.nAmount);
        if(fSkipDenominated && fIsDenominated) continue;
        // assume that the fee to create denoms should be mixing collateral at max
        if(item.nAmount >= nSmallestDenom + (fIsDenominated ? 0 : nMixingCollateral))
            nTotal += item.nAmount;
    }

    return nTotal;
}

// Note: calculated including unconfirmed,
// that's ok as long as we use it for informational purposes only
float CWallet::GetAverageAnonymizedRounds() const
{
    if (!CCoinJoinClientOptions::IsEnabled()) return 0;

    int nTotal = 0;
    int nCount = 0;

    LOCK2(cs_main, cs_wallet);
    for (const auto& outpoint : setWalletUTXO) {
        if(!IsDenominated(outpoint)) continue;

        nTotal += GetCappedOutpointCoinJoinRounds(outpoint);
        nCount++;
    }

    if(nCount == 0) return 0;

    return (float)nTotal/nCount;
}

// Note: calculated including unconfirmed,
// that's ok as long as we use it for informational purposes only
CAmount CWallet::GetNormalizedAnonymizedBalance() const
{
    if (!CCoinJoinClientOptions::IsEnabled()) return 0;

    CAmount nTotal = 0;

    auto locked_chain = chain().lock();
    LOCK(cs_wallet);
    for (const auto& outpoint : setWalletUTXO) {
        const auto it = mapWallet.find(outpoint.hash);
        if (it == mapWallet.end()) continue;

        CAmount nValue = it->second.tx->vout[outpoint.n].nValue;
        if (!CCoinJoin::IsDenominatedAmount(nValue)) continue;
        if (it->second.GetDepthInMainChain(*locked_chain) < 0) continue;

        int nRounds = GetCappedOutpointCoinJoinRounds(outpoint);
        nTotal += nValue * nRounds / CCoinJoinClientOptions::GetRounds();
    }

    return nTotal;
}

CAmount CWallet::GetAvailableBalance(const CCoinControl* coinControl) const
{
    auto locked_chain = chain().lock();
    LOCK(cs_wallet);

    CAmount balance = 0;
    std::vector<COutput> vCoins;
    AvailableCoins(*locked_chain, vCoins, true, coinControl);
    for (const COutput& out : vCoins) {
        if (out.fSpendable) {
            balance += out.tx->tx->vout[out.i].nValue;
        }
    }
    return balance;
}

void CWallet::AvailableCoins(interfaces::Chain::Lock& locked_chain, std::vector<COutput> &vCoins, bool fOnlySafe, const CCoinControl *coinControl, const CAmount &nMinimumAmount, const CAmount &nMaximumAmount, const CAmount &nMinimumSumAmount, const uint64_t nMaximumCount, const int nMinDepth, const int nMaxDepth) const
{
    AssertLockHeld(cs_wallet);

    vCoins.clear();
    CoinType nCoinType = coinControl ? coinControl->nCoinType : CoinType::ALL_COINS;

    CAmount nTotal = 0;

    for (auto pcoin : GetSpendableTXs()) {
        const uint256& wtxid = pcoin->GetHash();

        if (!locked_chain.checkFinalTx(*pcoin->tx))
            continue;

        if (pcoin->IsImmatureCoinBase(locked_chain))
            continue;

        int nDepth = pcoin->GetDepthInMainChain(locked_chain);

        // We should not consider coins which aren't at least in our mempool
        // It's possible for these to be conflicted via ancestors which we may never be able to detect
        if (nDepth == 0 && !pcoin->InMempool())
            continue;

        bool safeTx = pcoin->IsTrusted(locked_chain);

        if (fOnlySafe && !safeTx) {
            continue;
        }

        if (nDepth < nMinDepth || nDepth > nMaxDepth)
            continue;

        for (unsigned int i = 0; i < pcoin->tx->vout.size(); i++) {
            bool found = false;
            if (nCoinType == CoinType::ONLY_FULLY_MIXED) {
                if (!CCoinJoin::IsDenominatedAmount(pcoin->tx->vout[i].nValue)) continue;
                found = IsFullyMixed(COutPoint(wtxid, i));
            } else if(nCoinType == CoinType::ONLY_READY_TO_MIX) {
                if (!CCoinJoin::IsDenominatedAmount(pcoin->tx->vout[i].nValue)) continue;
                found = !IsFullyMixed(COutPoint(wtxid, i));
            } else if(nCoinType == CoinType::ONLY_NONDENOMINATED) {
                if (CCoinJoin::IsCollateralAmount(pcoin->tx->vout[i].nValue)) continue; // do not use collateral amounts
                found = !CCoinJoin::IsDenominatedAmount(pcoin->tx->vout[i].nValue);
            } else if(nCoinType == CoinType::ONLY_MASTERNODE_COLLATERAL) {
                found = pcoin->tx->vout[i].nValue == 10000*COIN;
            } else if(nCoinType == CoinType::ONLY_COINJOIN_COLLATERAL) {
                found = CCoinJoin::IsCollateralAmount(pcoin->tx->vout[i].nValue);
            } else {
                found = true;
            }
            if(!found) continue;

            if (pcoin->tx->vout[i].nValue < nMinimumAmount || pcoin->tx->vout[i].nValue > nMaximumAmount)
                continue;

            if (coinControl && coinControl->HasSelected() && !coinControl->fAllowOtherInputs && !coinControl->IsSelected(COutPoint(wtxid, i)))
                continue;

            if (IsLockedCoin(wtxid, i) && nCoinType != CoinType::ONLY_MASTERNODE_COLLATERAL)
                continue;

            if (IsSpent(locked_chain, wtxid, i))
                continue;

            isminetype mine = IsMine(pcoin->tx->vout[i]);

            if (mine == ISMINE_NO) {
                continue;
            }

            bool solvable = IsSolvable(*this, pcoin->tx->vout[i].scriptPubKey);
            bool spendable = ((mine & ISMINE_SPENDABLE) != ISMINE_NO) || (((mine & ISMINE_WATCH_ONLY) != ISMINE_NO) && (coinControl && coinControl->fAllowWatchOnly && solvable));

            vCoins.push_back(COutput(pcoin, i, nDepth, spendable, solvable, safeTx, (coinControl && coinControl->fAllowWatchOnly)));

            // Checks the sum amount of all UTXO's.
            if (nMinimumSumAmount != MAX_MONEY) {
                nTotal += pcoin->tx->vout[i].nValue;

                if (nTotal >= nMinimumSumAmount) {
                    return;
                }
            }

            // Checks the maximum number of UTXO's.
            if (nMaximumCount > 0 && vCoins.size() >= nMaximumCount) {
                return;
            }
        }
    }
}

std::map<CTxDestination, std::vector<COutput>> CWallet::ListCoins(interfaces::Chain::Lock& locked_chain) const
{
    AssertLockHeld(cs_wallet);

    std::map<CTxDestination, std::vector<COutput>> result;
    std::vector<COutput> availableCoins;

    AvailableCoins(locked_chain, availableCoins);

    for (const COutput& coin : availableCoins) {
        CTxDestination address;
        if (coin.fSpendable &&
            ExtractDestination(FindNonChangeParentOutput(*coin.tx->tx, coin.i).scriptPubKey, address)) {
            result[address].emplace_back(std::move(coin));
        }
    }

    std::vector<COutPoint> lockedCoins;
    ListLockedCoins(lockedCoins);
    for (const COutPoint& output : lockedCoins) {
        auto it = mapWallet.find(output.hash);
        if (it != mapWallet.end()) {
            int depth = it->second.GetDepthInMainChain(locked_chain);
            if (depth >= 0 && output.n < it->second.tx->vout.size() &&
                IsMine(it->second.tx->vout[output.n]) == ISMINE_SPENDABLE) {
                CTxDestination address;
                if (ExtractDestination(FindNonChangeParentOutput(*it->second.tx, output.n).scriptPubKey, address)) {
                    result[address].emplace_back(
                        &it->second, output.n, depth, true /* spendable */, true /* solvable */, false /* safe */);
                }
            }
        }
    }

    return result;
}

const CTxOut& CWallet::FindNonChangeParentOutput(const CTransaction& tx, int output) const
{
    const CTransaction* ptx = &tx;
    int n = output;
    while (IsChange(ptx->vout[n]) && ptx->vin.size() > 0) {
        const COutPoint& prevout = ptx->vin[0].prevout;
        auto it = mapWallet.find(prevout.hash);
        if (it == mapWallet.end() || it->second.tx->vout.size() <= prevout.n ||
            !IsMine(it->second.tx->vout[prevout.n])) {
            break;
        }
        ptx = it->second.tx.get();
        n = prevout.n;
    }
    return ptx->vout[n];
}

void CWallet::InitCoinJoinSalt()
{
    // Avoid fetching it multiple times
    assert(nCoinJoinSalt.IsNull());

    WalletBatch batch(*database);
    if (!batch.ReadCoinJoinSalt(nCoinJoinSalt) && batch.ReadCoinJoinSalt(nCoinJoinSalt, true)) {
        batch.WriteCoinJoinSalt(nCoinJoinSalt);
    }

    while (nCoinJoinSalt.IsNull()) {
        // We never generated/saved it
        nCoinJoinSalt = GetRandHash();
        batch.WriteCoinJoinSalt(nCoinJoinSalt);
    }
}

struct CompareByPriority
{
    bool operator()(const COutput& t1,
                    const COutput& t2) const
    {
        return CCoinJoin::CalculateAmountPriority(t1.GetInputCoin().effective_value) > CCoinJoin::CalculateAmountPriority(t2.GetInputCoin().effective_value);
    }
};

bool CWallet::SelectCoinsMinConf(const CAmount& nTargetValue, const CoinEligibilityFilter& eligibility_filter, std::vector<OutputGroup> groups,
                                 std::set<CInputCoin>& setCoinsRet, CAmount& nValueRet, const CoinSelectionParams& coin_selection_params, bool& bnb_used, CoinType nCoinType) const
{
    setCoinsRet.clear();
    nValueRet = 0;

    std::vector<OutputGroup> utxo_pool;
    if (coin_selection_params.use_bnb) {
        // Get long term estimate
        FeeCalculation feeCalc;
        CCoinControl temp;
        temp.m_confirm_target = 1008;
        CFeeRate long_term_feerate = GetMinimumFeeRate(*this, temp, &feeCalc);

        // Calculate cost of change
        CAmount cost_of_change = GetDiscardRate(*this).GetFee(coin_selection_params.change_spend_size) + coin_selection_params.effective_fee.GetFee(coin_selection_params.change_output_size);

        // Filter by the min conf specs and add to utxo_pool and calculate effective value
        for (OutputGroup& group : groups) {
            if (!group.EligibleForSpending(eligibility_filter)) continue;

            group.fee = 0;
            group.long_term_fee = 0;
            group.effective_value = 0;
            for (auto it = group.m_outputs.begin(); it != group.m_outputs.end(); ) {
                const CInputCoin& coin = *it;
                CAmount effective_value = coin.txout.nValue - (coin.m_input_bytes < 0 ? 0 : coin_selection_params.effective_fee.GetFee(coin.m_input_bytes));
                // Only include outputs that are positive effective value (i.e. not dust)
                if (effective_value > 0) {
                    group.fee += coin.m_input_bytes < 0 ? 0 : coin_selection_params.effective_fee.GetFee(coin.m_input_bytes);
                    group.long_term_fee += coin.m_input_bytes < 0 ? 0 : long_term_feerate.GetFee(coin.m_input_bytes);
                    group.effective_value += effective_value;
                    ++it;
                } else {
                    it = group.Discard(coin);
                }
            }
            if (group.effective_value > 0) utxo_pool.push_back(group);
        }
        // Calculate the fees for things that aren't inputs
        CAmount not_input_fees = coin_selection_params.effective_fee.GetFee(coin_selection_params.tx_noinputs_size);
        bnb_used = true;
        return SelectCoinsBnB(utxo_pool, nTargetValue, cost_of_change, setCoinsRet, nValueRet, not_input_fees);
    } else {
        // Filter by the min conf specs and add to utxo_pool
        for (const OutputGroup& group : groups) {
            if (!group.EligibleForSpending(eligibility_filter)) continue;
            utxo_pool.push_back(group);
        }
        bnb_used = false;
        return KnapsackSolver(nTargetValue, utxo_pool, setCoinsRet, nValueRet, nCoinType == CoinType::ONLY_FULLY_MIXED, m_default_max_tx_fee);
    }
}

bool CWallet::SelectCoins(const std::vector<COutput>& vAvailableCoins, const CAmount& nTargetValue, std::set<CInputCoin>& setCoinsRet, CAmount& nValueRet, const CCoinControl& coin_control, CoinSelectionParams& coin_selection_params, bool& bnb_used) const
{
    // Note: this function should never be used for "always free" tx types like dstx

    std::vector<COutput> vCoins(vAvailableCoins);
    CoinType nCoinType = coin_control.nCoinType;

    // coin control -> return all selected outputs (we want all selected to go into the transaction for sure)
    if (coin_control.HasSelected() && !coin_control.fAllowOtherInputs)
    {
        // We didn't use BnB here, so set it to false.
        bnb_used = false;

        for (const COutput& out : vCoins)
        {
            if(!out.fSpendable)
                continue;

            nValueRet += out.tx->tx->vout[out.i].nValue;
            setCoinsRet.insert(out.GetInputCoin());

            if (!coin_control.fRequireAllInputs && nValueRet >= nTargetValue) {
                // stop when we added at least one input and enough inputs to have at least nTargetValue funds
                return true;
            }
        }

        return (nValueRet >= nTargetValue);
    }

    // calculate value from preset inputs and store them
    std::set<CInputCoin> setPresetCoins;
    CAmount nValueFromPresetInputs = 0;

    std::vector<COutPoint> vPresetInputs;
    coin_control.ListSelected(vPresetInputs);
    for (const COutPoint& outpoint : vPresetInputs)
    {
        // For now, don't use BnB if preset inputs are selected. TODO: Enable this later
        bnb_used = false;
        coin_selection_params.use_bnb = false;

        std::map<uint256, CWalletTx>::const_iterator it = mapWallet.find(outpoint.hash);
        if (it != mapWallet.end())
        {
            const CWalletTx* pcoin = &it->second;
            // Clearly invalid input, fail
            if (pcoin->tx->vout.size() <= outpoint.n)
                return false;
            if (nCoinType == CoinType::ONLY_FULLY_MIXED) {
                // Make sure to include mixed preset inputs only,
                // even if some non-mixed inputs were manually selected via CoinControl
                if (!IsFullyMixed(outpoint)) continue;
            }
            // Just to calculate the marginal byte size
            nValueFromPresetInputs += pcoin->tx->vout[outpoint.n].nValue;
            setPresetCoins.insert(CInputCoin(pcoin->tx, outpoint.n));
        } else
            return false; // TODO: Allow non-wallet inputs
    }

    // remove preset inputs from vCoins
    for (std::vector<COutput>::iterator it = vCoins.begin(); it != vCoins.end() && coin_control.HasSelected();)
    {
        if (setPresetCoins.count(it->GetInputCoin()))
            it = vCoins.erase(it);
        else
            ++it;
    }

    // form groups from remaining coins; note that preset coins will not
    // automatically have their associated (same address) coins included
    if (coin_control.m_avoid_partial_spends && vCoins.size() > OUTPUT_GROUP_MAX_ENTRIES) {
        // Cases where we have 11+ outputs all pointing to the same destination may result in
        // privacy leaks as they will potentially be deterministically sorted. We solve that by
        // explicitly shuffling the outputs before processing
        Shuffle(vCoins.begin(), vCoins.end(), FastRandomContext());
    }
    std::vector<OutputGroup> groups = GroupOutputs(vCoins, !coin_control.m_avoid_partial_spends);

    size_t max_ancestors = (size_t)std::max<int64_t>(1, gArgs.GetArg("-limitancestorcount", DEFAULT_ANCESTOR_LIMIT));
    size_t max_descendants = (size_t)std::max<int64_t>(1, gArgs.GetArg("-limitdescendantcount", DEFAULT_DESCENDANT_LIMIT));
    bool fRejectLongChains = gArgs.GetBoolArg("-walletrejectlongchains", DEFAULT_WALLET_REJECT_LONG_CHAINS);

    bool res = nTargetValue <= nValueFromPresetInputs ||
        SelectCoinsMinConf(nTargetValue - nValueFromPresetInputs, CoinEligibilityFilter(1, 6, 0), groups, setCoinsRet, nValueRet, coin_selection_params, bnb_used, nCoinType) ||
        SelectCoinsMinConf(nTargetValue - nValueFromPresetInputs, CoinEligibilityFilter(1, 1, 0), groups, setCoinsRet, nValueRet, coin_selection_params, bnb_used, nCoinType) ||
        (m_spend_zero_conf_change && SelectCoinsMinConf(nTargetValue - nValueFromPresetInputs, CoinEligibilityFilter(0, 1, 2), groups, setCoinsRet, nValueRet, coin_selection_params, bnb_used, nCoinType)) ||
        (m_spend_zero_conf_change && SelectCoinsMinConf(nTargetValue - nValueFromPresetInputs, CoinEligibilityFilter(0, 1, std::min((size_t)4, max_ancestors/3), std::min((size_t)4, max_descendants/3)), groups, setCoinsRet, nValueRet, coin_selection_params, bnb_used, nCoinType)) ||
        (m_spend_zero_conf_change && SelectCoinsMinConf(nTargetValue - nValueFromPresetInputs, CoinEligibilityFilter(0, 1, max_ancestors/2, max_descendants/2), groups, setCoinsRet, nValueRet, coin_selection_params, bnb_used, nCoinType)) ||
        (m_spend_zero_conf_change && SelectCoinsMinConf(nTargetValue - nValueFromPresetInputs, CoinEligibilityFilter(0, 1, max_ancestors-1, max_descendants-1), groups, setCoinsRet, nValueRet, coin_selection_params, bnb_used, nCoinType)) ||
        (m_spend_zero_conf_change && !fRejectLongChains && SelectCoinsMinConf(nTargetValue - nValueFromPresetInputs, CoinEligibilityFilter(0, 1, std::numeric_limits<uint64_t>::max()), groups, setCoinsRet, nValueRet, coin_selection_params, bnb_used, nCoinType));

    // because SelectCoinsMinConf clears the setCoinsRet, we now add the possible inputs to the coinset
    util::insert(setCoinsRet, setPresetCoins);

    // add preset inputs to the total value selected
    nValueRet += nValueFromPresetInputs;

    return res;
}

bool CWallet::FundTransaction(CMutableTransaction& tx, CAmount& nFeeRet, int& nChangePosInOut, bilingual_str& error, bool lockUnspents, const std::set<int>& setSubtractFeeFromOutputs, CCoinControl coinControl)
{
    std::vector<CRecipient> vecSend;

    // Turn the txout set into a CRecipient vector.
    for (size_t idx = 0; idx < tx.vout.size(); idx++) {
        const CTxOut& txOut = tx.vout[idx];
        CRecipient recipient = {txOut.scriptPubKey, txOut.nValue, setSubtractFeeFromOutputs.count(idx) == 1};
        vecSend.push_back(recipient);
    }

    coinControl.fAllowOtherInputs = true;

    for (const CTxIn& txin : tx.vin) {
        coinControl.Select(txin.prevout);
    }

    // Acquire the locks to prevent races to the new locked unspents between the
    // CreateTransaction call and LockCoin calls (when lockUnspents is true).
    auto locked_chain = chain().lock();
    LOCK(cs_wallet);

    int nExtraPayloadSize = 0;
    if (tx.nVersion == 3 && tx.nType != TRANSACTION_NORMAL)
        nExtraPayloadSize = (int)tx.vExtraPayload.size();

    CTransactionRef tx_new;
    if (!CreateTransaction(*locked_chain, vecSend, tx_new, nFeeRet, nChangePosInOut, error, coinControl, false, nExtraPayloadSize)) {
        return false;
    }

    if (nChangePosInOut != -1) {
        tx.vout.insert(tx.vout.begin() + nChangePosInOut, tx_new->vout[nChangePosInOut]);
    }

    // Copy output sizes from new transaction; they may have had the fee
    // subtracted from them.
    for (unsigned int idx = 0; idx < tx.vout.size(); idx++) {
        tx.vout[idx].nValue = tx_new->vout[idx].nValue;
    }

    // Add new txins while keeping original txin scriptSig/order.
    for (const CTxIn& txin : tx_new->vin) {
        if (!coinControl.IsSelected(txin.prevout)) {
            tx.vin.push_back(txin);

            if (lockUnspents) {
                LockCoin(txin.prevout);
            }
        }
    }

    return true;
}

bool CWallet::SelectTxDSInsByDenomination(int nDenom, CAmount nValueMax, std::vector<CTxDSIn>& vecTxDSInRet)
{
    auto locked_chain = chain().lock();
    LOCK(cs_wallet);

    CAmount nValueTotal{0};

    std::set<uint256> setRecentTxIds;
    std::vector<COutput> vCoins;

    vecTxDSInRet.clear();

    if (!CCoinJoin::IsValidDenomination(nDenom)) {
        return false;
    }
    CAmount nDenomAmount = CCoinJoin::DenominationToAmount(nDenom);

    CCoinControl coin_control;
    coin_control.nCoinType = CoinType::ONLY_READY_TO_MIX;
    AvailableCoins(*locked_chain, vCoins, true, &coin_control);
    LogPrint(BCLog::COINJOIN, "CWallet::%s -- vCoins.size(): %d\n", __func__, vCoins.size());

    Shuffle(vCoins.rbegin(), vCoins.rend(), FastRandomContext());

    for (const auto& out : vCoins) {
        uint256 txHash = out.tx->GetHash();
        CAmount nValue = out.tx->tx->vout[out.i].nValue;
        if (setRecentTxIds.find(txHash) != setRecentTxIds.end()) continue; // no duplicate txids
        if (nValueTotal + nValue > nValueMax) continue;
        if (nValue != nDenomAmount) continue;

        CTxIn txin = CTxIn(txHash, out.i);
        CScript scriptPubKey = out.tx->tx->vout[out.i].scriptPubKey;
        int nRounds = GetRealOutpointCoinJoinRounds(txin.prevout);

        nValueTotal += nValue;
        vecTxDSInRet.emplace_back(CTxDSIn(txin, scriptPubKey, nRounds));
        setRecentTxIds.emplace(txHash);
        LogPrint(BCLog::COINJOIN, "CWallet::%s -- hash: %s, nValue: %d.%08d\n",
                        __func__, txHash.ToString(), nValue / COIN, nValue % COIN);
    }

    LogPrint(BCLog::COINJOIN, "CWallet::%s -- setRecentTxIds.size(): %d\n", __func__, setRecentTxIds.size());

    return nValueTotal > 0;
}

static bool IsCurrentForAntiFeeSniping(interfaces::Chain& chain, interfaces::Chain::Lock& locked_chain)
{
    if (chain.isInitialBlockDownload()) {
        return false;
    }
    constexpr int64_t MAX_ANTI_FEE_SNIPING_TIP_AGE = 8 * 60 * 60; // in seconds
    if (locked_chain.getBlockTime(*locked_chain.getHeight()) < (GetTime() - MAX_ANTI_FEE_SNIPING_TIP_AGE)) {
        return false;
    }
    return true;
}

/**
 * Return a height-based locktime for new transactions (uses the height of the
 * current chain tip unless we are not synced with the current chain
 */
static uint32_t GetLocktimeForNewTransaction(interfaces::Chain& chain, interfaces::Chain::Lock& locked_chain)
{
    uint32_t locktime;
    // Discourage fee sniping.
    //
    // For a large miner the value of the transactions in the best block and
    // the mempool can exceed the cost of deliberately attempting to mine two
    // blocks to orphan the current best block. By setting nLockTime such that
    // only the next block can include the transaction, we discourage this
    // practice as the height restricted and limited blocksize gives miners
    // considering fee sniping fewer options for pulling off this attack.
    //
    // A simple way to think about this is from the wallet's point of view we
    // always want the blockchain to move forward. By setting nLockTime this
    // way we're basically making the statement that we only want this
    // transaction to appear in the next block; we don't want to potentially
    // encourage reorgs by allowing transactions to appear at lower heights
    // than the next block in forks of the best chain.
    //
    // Of course, the subsidy is high enough, and transaction volume low
    // enough, that fee sniping isn't a problem yet, but by implementing a fix
    // now we ensure code won't be written that makes assumptions about
    // nLockTime that preclude a fix later.
    if (IsCurrentForAntiFeeSniping(chain, locked_chain)) {
        locktime = locked_chain.getHeight().get_value_or(-1);

        // Secondly occasionally randomly pick a nLockTime even further back, so
        // that transactions that are delayed after signing for whatever reason,
        // e.g. high-latency mix networks and some CoinJoin implementations, have
        // better privacy.
        if (GetRandInt(10) == 0)
            locktime = std::max(0, (int)locktime - GetRandInt(100));
    } else {
        // If our chain is lagging behind, we can't discourage fee sniping nor help
        // the privacy of high-latency transactions. To avoid leaking a potentially
        // unique "nLockTime fingerprint", set nLockTime to a constant.
        locktime = 0;
    }

    assert(locktime <= (unsigned int)::ChainActive().Height());
    assert(locktime < LOCKTIME_THRESHOLD);
    return locktime;
}

std::vector<CompactTallyItem> CWallet::SelectCoinsGroupedByAddresses(bool fSkipDenominated, bool fAnonymizable, bool fSkipUnconfirmed, int nMaxOupointsPerAddress) const
{
    auto locked_chain = chain().lock();
    LOCK(cs_wallet);

    isminefilter filter = ISMINE_SPENDABLE;

    // Try using the cache for already confirmed mixable inputs.
    // This should only be used if nMaxOupointsPerAddress was NOT specified.
    if(nMaxOupointsPerAddress == -1 && fAnonymizable && fSkipUnconfirmed) {
        if(fSkipDenominated && fAnonymizableTallyCachedNonDenom) {
            LogPrint(BCLog::SELECTCOINS, "SelectCoinsGroupedByAddresses - using cache for non-denom inputs %d\n", vecAnonymizableTallyCachedNonDenom.size());
            return vecAnonymizableTallyCachedNonDenom;
        }
        if(!fSkipDenominated && fAnonymizableTallyCached) {
            LogPrint(BCLog::SELECTCOINS, "SelectCoinsGroupedByAddresses - using cache for all inputs %d\n", vecAnonymizableTallyCached.size());
            return vecAnonymizableTallyCached;
        }
    }

    CAmount nSmallestDenom = CCoinJoin::GetSmallestDenomination();

    // Tally
    std::map<CTxDestination, CompactTallyItem> mapTally;
    std::set<uint256> setWalletTxesCounted;
    for (const auto& outpoint : setWalletUTXO) {

        if (!setWalletTxesCounted.emplace(outpoint.hash).second) continue;

        std::map<uint256, CWalletTx>::const_iterator it = mapWallet.find(outpoint.hash);
        if (it == mapWallet.end()) continue;

        const CWalletTx& wtx = (*it).second;

        if(wtx.IsCoinBase() && wtx.GetBlocksToMaturity(*locked_chain) > 0) continue;
        if(fSkipUnconfirmed && !wtx.IsTrusted(*locked_chain)) continue;
        if (wtx.GetDepthInMainChain(*locked_chain) < 0) continue;

        for (unsigned int i = 0; i < wtx.tx->vout.size(); i++) {
            CTxDestination txdest;
            if (!ExtractDestination(wtx.tx->vout[i].scriptPubKey, txdest)) continue;

            isminefilter mine = ::IsMine(*this, txdest);
            if(!(mine & filter)) continue;

            auto itTallyItem = mapTally.find(txdest);
            if (nMaxOupointsPerAddress != -1 && itTallyItem != mapTally.end() && int64_t(itTallyItem->second.vecInputCoins.size()) >= nMaxOupointsPerAddress) continue;

            if(IsSpent(*locked_chain, outpoint.hash, i) || IsLockedCoin(outpoint.hash, i)) continue;

            if(fSkipDenominated && CCoinJoin::IsDenominatedAmount(wtx.tx->vout[i].nValue)) continue;

            if(fAnonymizable) {
                // ignore collaterals
                if(CCoinJoin::IsCollateralAmount(wtx.tx->vout[i].nValue)) continue;
                if(fMasternodeMode && wtx.tx->vout[i].nValue == 1000*COIN) continue;
                // ignore outputs that are 10 times smaller then the smallest denomination
                // otherwise they will just lead to higher fee / lower priority
                if(wtx.tx->vout[i].nValue <= nSmallestDenom/10) continue;
                // ignore mixed
                if (IsFullyMixed(COutPoint(outpoint.hash, i))) continue;
            }

            if (itTallyItem == mapTally.end()) {
                itTallyItem = mapTally.emplace(txdest, CompactTallyItem()).first;
                itTallyItem->second.txdest = txdest;
            }
            itTallyItem->second.nAmount += wtx.tx->vout[i].nValue;
            itTallyItem->second.vecInputCoins.emplace_back(wtx.tx, i);
        }
    }

    // construct resulting vector
    // NOTE: vecTallyRet is "sorted" by txdest (i.e. address), just like mapTally
    std::vector<CompactTallyItem> vecTallyRet;
    for (const auto& item : mapTally) {
        if(fAnonymizable && item.second.nAmount < nSmallestDenom) continue;
        vecTallyRet.push_back(item.second);
    }

    // Cache already confirmed mixable entries for later use.
    // This should only be used if nMaxOupointsPerAddress was NOT specified.
    if(nMaxOupointsPerAddress == -1 && fAnonymizable && fSkipUnconfirmed) {
        if(fSkipDenominated) {
            vecAnonymizableTallyCachedNonDenom = vecTallyRet;
            fAnonymizableTallyCachedNonDenom = true;
        } else {
            vecAnonymizableTallyCached = vecTallyRet;
            fAnonymizableTallyCached = true;
        }
    }

    // debug
    if (LogAcceptCategory(BCLog::SELECTCOINS)) {
        std::string strMessage = "SelectCoinsGroupedByAddresses - vecTallyRet:\n";
        for (const auto& item : vecTallyRet)
            strMessage += strprintf("  %s %f\n", EncodeDestination(item.txdest), float(item.nAmount)/COIN);
        LogPrint(BCLog::SELECTCOINS, "%s", strMessage); /* Continued */
    }

    return vecTallyRet;
}

bool CWallet::SelectStakeCoins(StakeCandidates& setCoins, CAmount nTargetAmount) const
{
    LOCK2(cs_main, cs_wallet);
    std::vector<COutput> vCoins;
    CCoinControl coin_control;
    coin_control.nCoinType = CoinType::ALL_COINS;
    AvailableCoins(*chain().lock(), vCoins, true, &coin_control, MIN_STAKE_AMOUNT);
    auto curr_time = GetTime();
    auto min_age = Params().MinStakeAge();

    for (const COutput& out : vCoins) {
        CAmount out_value = out.tx->tx->vout[out.i].nValue;

        //make sure not to outrun target amount
        if (out_value > nTargetAmount) {
            continue;
        }

        if (out_value < MIN_STAKE_AMOUNT) {
            continue;
        }

        // Do not touch collaterals
        if (out_value == MASTERNODE_COLLATERAL_AMOUNT) {
            continue;
        }

        //check for min age
        if (curr_time - out.tx->GetTxTime() < min_age) {
            continue;
        }

        //check that it is matured
        if (out.nDepth < (out.tx->IsCoinBase() ? COINBASE_MATURITY : 10)) {
            continue;
        }

        // Ignore already staked, locked or spent
        auto tx_hash = out.tx->tx->GetHash();
        if (IsSpent(*chain().lock(), tx_hash, out.i) || IsLockedCoin(tx_hash, out.i)) {
            continue;
        }

        // Check another way if spent
        if (!ChainstateActive().CoinsTip().HaveCoin(COutPoint(tx_hash, out.i))) {
            continue;
        }

        //add to our stake set
        setCoins.emplace_back(out_value, out.tx, out.i);
    }

    return !setCoins.empty();
}

bool CWallet::MintableCoins()
{
    LOCK2(cs_main, cs_wallet);
    CAmount nBalance = GetBalance().m_mine_trusted;
    if (gArgs.IsArgSet("-reservebalance") && !ParseMoney(gArgs.GetArg("-reservebalance", ""), nReserveBalance))
        return error("MintableCoins() : invalid reserve balance amount");
    if (nBalance <= nReserveBalance)
        return false;

    std::vector<COutput> vCoins;
    AvailableCoins(*chain().lock(), vCoins, true);
    auto min_age = Params().MinStakeAge();

    for (const COutput& out : vCoins) {
        if (out.nDepth < (out.tx->IsCoinBase() ? COINBASE_MATURITY : 10))
            continue;

        CAmount out_value = out.tx->tx->vout[out.i].nValue;

        if (out_value == MASTERNODE_COLLATERAL_AMOUNT) continue;

        if (out_value < MIN_STAKE_AMOUNT)
            continue;

        // Some more filters are possible, but excessive

        if (GetTime() - out.tx->GetTxTime() >= min_age)
            return true;
    }

    return false;
}

bool CWallet::SelectDenominatedAmounts(CAmount nValueMax, std::set<CAmount>& setAmountsRet) const
{
    auto locked_chain = chain().lock();
    LOCK(cs_wallet);

    CAmount nValueTotal{0};
    setAmountsRet.clear();

    std::vector<COutput> vCoins;
    CCoinControl coin_control;
    coin_control.nCoinType = CoinType::ONLY_READY_TO_MIX;
    AvailableCoins(*locked_chain, vCoins, true, &coin_control);
    // larger denoms first
    std::sort(vCoins.rbegin(), vCoins.rend(), CompareByPriority());

    for (const auto& out : vCoins) {
        CAmount nValue = out.tx->tx->vout[out.i].nValue;
        if (nValueTotal + nValue <= nValueMax) {
            nValueTotal += nValue;
            setAmountsRet.emplace(nValue);
        }
    }

    return nValueTotal >= CCoinJoin::GetSmallestDenomination();
}

bool CWallet::GetMasternodeOutpointAndKeys(COutPoint& outpointRet, CPubKey& pubKeyRet, CKey& keyRet, const std::string& strTxHash, const std::string& strOutputIndex)
{
    LOCK2(cs_main, cs_wallet);

    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    // Find possible candidates
    std::vector<COutput> vPossibleCoins;
    CCoinControl coin_control;
    coin_control.nCoinType = CoinType::ONLY_MASTERNODE_COLLATERAL;
    AvailableCoins(*chain().lock(), vPossibleCoins, true, &coin_control);
    if(vPossibleCoins.empty()) {
        LogPrintf("CWallet::GetMasternodeOutpointAndKeys -- Could not locate any valid masternode vin\n");
        return false;
    }

    if(strTxHash.empty()) // No output specified, select the first one
        return GetOutpointAndKeysFromOutput(vPossibleCoins[0], outpointRet, pubKeyRet, keyRet);

    // Find specific outpoint
    uint256 txHash = uint256S(strTxHash);
    int nOutputIndex = atoi(strOutputIndex);

    for (const auto& out : vPossibleCoins)
        if(out.tx->GetHash() == txHash && out.i == nOutputIndex) // found it!
            return GetOutpointAndKeysFromOutput(out, outpointRet, pubKeyRet, keyRet);

    LogPrintf("CWallet::GetMasternodeOutpointAndKeys -- Could not locate specified masternode vin\n");
    return false;
}

bool CWallet::GetOutpointAndKeysFromOutput(const COutput& out, COutPoint& outpointRet, CPubKey& pubKeyRet, CKey& keyRet)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    CScript pubScript;

    outpointRet = COutPoint(out.tx->GetHash(), out.i);
    pubScript = out.tx->tx->vout[out.i].scriptPubKey; // the inputs PubKey

    CTxDestination dest;
    ExtractDestination(pubScript, dest);

    const CKeyID *keyID = boost::get<CKeyID>(&dest);
    if (!keyID) {
        LogPrintf("CWallet::GetOutpointAndKeysFromOutput -- Address does not refer to a key\n");
        return false;
    }

    if (!GetKey(*keyID, keyRet)) {
        LogPrintf ("CWallet::GetOutpointAndKeysFromOutput -- Private key for address is not known\n");
        return false;
    }

    pubKeyRet = keyRet.GetPubKey();
    return true;
}

int CWallet::CountInputsWithAmount(CAmount nInputAmount) const
{
    CAmount nTotal = 0;

    auto locked_chain = chain().lock();
    LOCK(cs_wallet);

    for (const auto& outpoint : setWalletUTXO) {
        const auto it = mapWallet.find(outpoint.hash);
        if (it == mapWallet.end()) continue;
        if (it->second.tx->vout[outpoint.n].nValue != nInputAmount) continue;
        if (it->second.GetDepthInMainChain(*locked_chain) < 0) continue;

        nTotal++;
    }

    return nTotal;
}

bool CWallet::HasCollateralInputs(bool fOnlyConfirmed) const
{
    auto locked_chain = chain().lock();
    LOCK(cs_wallet);

    std::vector<COutput> vCoins;
    CCoinControl coin_control;
    coin_control.nCoinType = CoinType::ONLY_COINJOIN_COLLATERAL;
    AvailableCoins(*locked_chain, vCoins, fOnlyConfirmed, &coin_control);

    return !vCoins.empty();
}

bool CWallet::GetBudgetSystemCollateralTX(interfaces::Chain::Lock& locked_chain, CTransactionRef& tx, uint256 hash, CAmount amount, const COutPoint& outpoint)
{
    CScript scriptChange;
    scriptChange << OP_RETURN << ToByteVector(hash);

    CAmount nFeeRet = 0;
    int nChangePosRet = -1;
    bilingual_str error;
    std::vector< CRecipient > vecSend;
    vecSend.push_back((CRecipient){scriptChange, amount, false});

    CCoinControl coinControl;
    if (!outpoint.IsNull()) {
        coinControl.Select(outpoint);
    }
    bool success = CreateTransaction(locked_chain, vecSend, tx, nFeeRet, nChangePosRet, error, coinControl);
    if(!success){
        WalletLogPrintf("CWallet::GetBudgetSystemCollateralTX -- Error: %s\n", error.original);
        return false;
    }

    return true;
}

bool CWallet::CreateTransaction(interfaces::Chain::Lock& locked_chain, const std::vector<CRecipient>& vecSend, CTransactionRef& tx, CAmount& nFeeRet,
                         int& nChangePosInOut, bilingual_str& error, const CCoinControl& coin_control, bool sign, int nExtraPayloadSize)
{
    CAmount nValue = 0;
    CReserveKey reservekey(this);
    int nChangePosRequest = nChangePosInOut;
    unsigned int nSubtractFeeFromAmount = 0;
    for (const auto& recipient : vecSend)
    {
        if (nValue < 0 || recipient.nAmount < 0)
        {
            error = _("Transaction amounts must not be negative");
            return false;
        }
        nValue += recipient.nAmount;

        if (recipient.fSubtractFeeFromAmount)
            nSubtractFeeFromAmount++;
    }
    if (vecSend.empty())
    {
        error = _("Transaction must have at least one recipient");
        return false;
    }

    CMutableTransaction txNew;
    txNew.nLockTime = GetLocktimeForNewTransaction(chain(), locked_chain);

    FeeCalculation feeCalc;
    CFeeRate discard_rate = coin_control.m_discard_feerate ? *coin_control.m_discard_feerate : GetDiscardRate(*this);
    unsigned int nBytes{0};
    {
        std::vector<CInputCoin> vecCoins;
        auto locked_chain = chain().lock();
        LOCK(cs_wallet);
        {
            CAmount nAmountAvailable{0};
            std::vector<COutput> vAvailableCoins;
            AvailableCoins(*locked_chain, vAvailableCoins, true, &coin_control, 1, MAX_MONEY, MAX_MONEY, 0, coin_control.m_min_depth);
            CoinSelectionParams coin_selection_params; // Parameters for coin selection, init with dummy
            coin_selection_params.use_bnb = false; // never use BnB

            for (auto out : vAvailableCoins) {
                if (out.fSpendable) {
                    nAmountAvailable += out.tx->tx->vout[out.i].nValue;
                }
            }

            // Create change script that will be used if we need change
            // TODO: pass in scriptChange instead of reservekey so
            // change transaction isn't always pay-to-bitcoin-address
            CScript scriptChange;

            // coin control: send change to custom address
            if (!boost::get<CNoDestination>(&coin_control.destChange)) {
                scriptChange = GetScriptForDestination(coin_control.destChange);
            } else { // no coin control: send change to newly generated address
                // Note: We use a new key here to keep it from being obvious which side is the change.
                //  The drawback is that by not reusing a previous key, the change may be lost if a
                //  backup is restored, if the backup doesn't have the new private key for the change.
                //  If we reused the old key, it would be possible to add code to look for and
                //  rediscover unknown transactions that were written with keys of ours to recover
                //  post-backup change.

                // Reserve a new key pair from key pool
                if (!CanGetAddresses(true)) {
                    error = _("Can't generate a change-address key. No keys in the internal keypool and can't generate any keys.");
                    return false;
                }
                CPubKey vchPubKey;
                bool ret;
                ret = reservekey.GetReservedKey(vchPubKey, true);
                if (!ret)
                {
                    error = _("Keypool ran out, please call keypoolrefill first");
                    return false;
                }

                scriptChange = GetScriptForDestination(vchPubKey.GetID());
            }

            nFeeRet = 0;
            bool pick_new_inputs = true;
            CAmount nValueIn = 0;
            CAmount nAmountToSelectAdditional{0};
            // Start with nAmountToSelectAdditional=0 and loop until there is enough to cover the request + fees, try it 500 times.
            int nMaxTries = 500;
            while (--nMaxTries > 0)
            {
                nChangePosInOut = std::numeric_limits<int>::max();
                txNew.vin.clear();
                txNew.vout.clear();
                bool fFirst = true;

                CAmount nValueToSelect = nValue;
                if (nSubtractFeeFromAmount == 0) {
                    assert(nAmountToSelectAdditional >= 0);
                    nValueToSelect += nAmountToSelectAdditional;
                }
                // vouts to the payees
                for (const auto& recipient : vecSend)
                {
                    CTxOut txout(recipient.nAmount, recipient.scriptPubKey);

                    if (recipient.fSubtractFeeFromAmount)
                    {
                        assert(nSubtractFeeFromAmount != 0);
                        txout.nValue -= nFeeRet / nSubtractFeeFromAmount; // Subtract fee equally from each selected recipient

                        if (fFirst) // first receiver pays the remainder not divisible by output count
                        {
                            fFirst = false;
                            txout.nValue -= nFeeRet % nSubtractFeeFromAmount;
                        }
                    }

                    if (IsDust(txout, chain().relayDustFee()))
                    {
                        if (recipient.fSubtractFeeFromAmount && nFeeRet > 0)
                        {
                            if (txout.nValue < 0)
                                error = _("The transaction amount is too small to pay the fee");
                            else
                                error = _("The transaction amount is too small to send after the fee has been deducted");
                        }
                        else
                            error = _("Transaction amount too small");
                        return false;
                    }
                    txNew.vout.push_back(txout);
                }

                // Choose coins to use
                bool bnb_used;
                if (pick_new_inputs) {
                    nValueIn = 0;
                    std::set<CInputCoin> setCoinsTmp;
                    if (!SelectCoins(vAvailableCoins, nValueToSelect, setCoinsTmp, nValueIn, coin_control, coin_selection_params, bnb_used)) {
                        if (coin_control.nCoinType == CoinType::ONLY_NONDENOMINATED) {
                            error = _("Unable to locate enough non-denominated funds for this transaction.");
                        } else if (coin_control.nCoinType == CoinType::ONLY_FULLY_MIXED) {
                            error = _("Unable to locate enough mixed funds for this transaction.");
                            error = error + Untranslated(" ") + strprintf(_("%s uses exact denominated amounts to send funds, you might simply need to mix some more coins."), gCoinJoinName);
                        } else if (nValueIn < nValueToSelect) {
                            error = _("Insufficient funds.");
                        }
                        return false;
                    }
                    vecCoins.assign(setCoinsTmp.begin(), setCoinsTmp.end());
                }

                // Fill vin
                //
                // Note how the sequence number is set to max()-1 so that the
                // nLockTime set above actually works.
                txNew.vin.clear();
                for (const auto& coin : vecCoins) {
                    txNew.vin.emplace_back(coin.outpoint, CScript(), CTxIn::SEQUENCE_FINAL - 1);
                }

                auto calculateFee = [&](CAmount& nFee) -> bool {
                    AssertLockHeld(cs_wallet);
                    nBytes = CalculateMaximumSignedTxSize(CTransaction(txNew), this, coin_control.fAllowWatchOnly);
                    if (nBytes < 0) {
                        error = _("Signing transaction failed");
                        return false;
                    }

                    if (nExtraPayloadSize != 0) {
                        // account for extra payload in fee calculation
                        nBytes += GetSizeOfCompactSize(nExtraPayloadSize) + nExtraPayloadSize;
                    }

                    if (nBytes > MAX_STANDARD_TX_SIZE) {
                        // Do not create oversized transactions (bad-txns-oversize).
                        error = _("Transaction too large");
                        return false;
                    }

                    // Remove scriptSigs to eliminate the fee calculation dummy signatures
                    for (auto& txin : txNew.vin) {
                        txin.scriptSig = CScript();
                    }

                    nFee = GetMinimumFee(*this, nBytes, coin_control, &feeCalc);

                    return true;
                };

                if (!calculateFee(nFeeRet)) {
                    return false;
                }

                CTxOut newTxOut;
                const CAmount nAmountLeft = nValueIn - nValue;
                auto getChange = [&]() {
                    if (nSubtractFeeFromAmount > 0) {
                        return nAmountLeft;
                    } else {
                        return nAmountLeft - nFeeRet;
                    }
                };

                if (getChange() > 0)
                {
                    //over pay for denominated transactions
                    if (coin_control.nCoinType == CoinType::ONLY_FULLY_MIXED) {
                        nChangePosInOut = -1;
                        nFeeRet += getChange();
                    } else {
                        // Fill a vout to ourself with zero amount until we know the correct change
                        newTxOut = CTxOut(0, scriptChange);
                        txNew.vout.push_back(newTxOut);

                        // Calculate the fee with the change output added, store the
                        // current fee to reset it in case the remainder is dust and we
                        // don't need to fee with change output added.
                        CAmount nFeePrev = nFeeRet;
                        if (!calculateFee(nFeeRet)) {
                            return false;
                        }

                        // Remove the change output again, it will be added later again if required
                        txNew.vout.pop_back();

                        // Set the change amount properly
                        newTxOut.nValue = getChange();

                        // Never create dust outputs; if we would, just
                        // add the dust to the fee.
                        if (IsDust(newTxOut, discard_rate))
                        {
                            nFeeRet = nFeePrev;
                            nChangePosInOut = -1;
                            nFeeRet += getChange();
                        }
                        else
                        {
                            if (nChangePosRequest == -1)
                            {
                                // Insert change txn at random position:
                                nChangePosInOut = GetRandInt(txNew.vout.size()+1);
                            }
                            else if ((unsigned int)nChangePosRequest > txNew.vout.size())
                            {
                                error = _("Change index out of range");
                                return false;
                            } else {
                                nChangePosInOut = nChangePosRequest;
                            }

                            std::vector<CTxOut>::iterator position = txNew.vout.begin()+nChangePosInOut;
                            txNew.vout.insert(position, newTxOut);
                        }
                    }
                } else {
                    nChangePosInOut = -1;
                }

                if (getChange() < 0) {
                    if (nSubtractFeeFromAmount == 0) {
                        // nValueIn is not enough to cover nValue + nFeeRet. Add the missing amount abs(nChange) to the fee
                        // and try to select other inputs in the next loop step to cover the full required amount.
                        nAmountToSelectAdditional += abs(getChange());
                    } else if (nAmountToSelectAdditional > 0 && nValueToSelect == nAmountAvailable) {
                        // We tried selecting more and failed. We have no extra funds left,
                        // so just add 1 duff to fail in the next loop step with a correct reason
                        nAmountToSelectAdditional += 1;
                    }
                    continue;
                }

                // If no specific change position was requested, apply BIP69
                if (nChangePosRequest == -1) {
                    std::sort(vecCoins.begin(), vecCoins.end(), CompareInputCoinBIP69());
                    std::sort(txNew.vin.begin(), txNew.vin.end(), CompareInputBIP69());
                    std::sort(txNew.vout.begin(), txNew.vout.end(), CompareOutputBIP69());

                    // If there was a change output added before, we must update its position now
                    if (nChangePosInOut != -1) {
                        int i = 0;
                        for (const CTxOut& txOut : txNew.vout)
                        {
                            if (txOut == newTxOut)
                            {
                                nChangePosInOut = i;
                                break;
                            }
                            i++;
                        }
                    }
                }

                if (feeCalc.reason == FeeReason::FALLBACK && !m_allow_fallback_fee) {
                    // eventually allow a fallback fee
                    error = _("Fee estimation failed. Fallbackfee is disabled. Wait a few blocks or enable -fallbackfee.");
                    return false;
                }

                if (nAmountLeft == nFeeRet) {
                    // We either added the change amount to nFeeRet because the change amount was considered
                    // to be dust or the input exactly matches output + fee.
                    // Either way, we used the total amount of the inputs we picked and the transaction is ready.
                    break;
                }

                // We have a change output and we don't need to subtruct fees, which means the transaction is ready.
                if (nChangePosInOut != -1 && nSubtractFeeFromAmount == 0) {
                    break;
                }

                // If subtracting fee from recipients, we now know what fee we
                // need to subtract, we have no reason to reselect inputs
                if (nSubtractFeeFromAmount > 0) {
                    // If we are in here the second time it means we already subtracted the fee from the
                    // output(s) and there weren't any issues while doing that. So the transaction is ready now
                    // and we can break.
                    if (!pick_new_inputs) {
                        break;
                    }
                    pick_new_inputs = false;
                }
            }

            if (nMaxTries == 0) {
                error = _("Exceeded max tries.");
                return false;
            }
        }

        // Make sure change position was updated one way or another
        assert(nChangePosInOut != std::numeric_limits<int>::max());

        if (sign)
        {
            int nIn = 0;
            for(const auto& coin : vecCoins)
            {
                const CScript& scriptPubKey = coin.txout.scriptPubKey;
                SignatureData sigdata;

                if (!ProduceSignature(*this, MutableTransactionSignatureCreator(&txNew, nIn, coin.txout.nValue, SIGHASH_ALL), scriptPubKey, sigdata))
                {
                    error = _("Signing transaction failed");
                    return false;
                } else {
                    UpdateInput(txNew.vin.at(nIn), sigdata);
                }

                nIn++;
            }
        }

        // Return the constructed transaction data.
        tx = MakeTransactionRef(std::move(txNew));
    }

    if (nFeeRet > m_default_max_tx_fee) {
        error = TransactionErrorString(TransactionError::MAX_FEE_EXCEEDED);
        return false;
    }

    if (gArgs.GetBoolArg("-walletrejectlongchains", DEFAULT_WALLET_REJECT_LONG_CHAINS)) {
        // Lastly, ensure this tx will pass the mempool's chain limits
        if (!chain().checkChainLimits(tx)) {
            error = _("Transaction has too long of a mempool chain");
            return false;
        }
    }

    // Before we return success, we assume any change key will be used to prevent
    // accidental re-use.
    reservekey.KeepKey();

    WalletLogPrintf("Fee Calculation: Fee:%d Bytes:%u Tgt:%d (requested %d) Reason:\"%s\" Decay %.5f: Estimation: (%g - %g) %.2f%% %.1f/(%.1f %d mem %.1f out) Fail: (%g - %g) %.2f%% %.1f/(%.1f %d mem %.1f out)\n",
              nFeeRet, nBytes, feeCalc.returnedTarget, feeCalc.desiredTarget, StringForFeeReason(feeCalc.reason), feeCalc.est.decay,
              feeCalc.est.pass.start, feeCalc.est.pass.end,
              (feeCalc.est.pass.totalConfirmed + feeCalc.est.pass.inMempool + feeCalc.est.pass.leftMempool) > 0.0 ? 100 * feeCalc.est.pass.withinTarget / (feeCalc.est.pass.totalConfirmed + feeCalc.est.pass.inMempool + feeCalc.est.pass.leftMempool) : 0.0,
              feeCalc.est.pass.withinTarget, feeCalc.est.pass.totalConfirmed, feeCalc.est.pass.inMempool, feeCalc.est.pass.leftMempool,
              feeCalc.est.fail.start, feeCalc.est.fail.end,
              (feeCalc.est.fail.totalConfirmed + feeCalc.est.fail.inMempool + feeCalc.est.fail.leftMempool) > 0.0 ? 100 * feeCalc.est.fail.withinTarget / (feeCalc.est.fail.totalConfirmed + feeCalc.est.fail.inMempool + feeCalc.est.fail.leftMempool) : 0.0,
              feeCalc.est.fail.withinTarget, feeCalc.est.fail.totalConfirmed, feeCalc.est.fail.inMempool, feeCalc.est.fail.leftMempool);
    return true;
}

// ppcoin: create coin stake transaction
bool CWallet::CreateCoinStake(const CBlockIndex *pindex_prev, CBlock &curr_block, CMutableTransaction& coinbaseTx)
{
    // Choose coins to use
    CAmount nBalance = GetBalance().m_mine_trusted;

    if (gArgs.IsArgSet("-reservebalance") && !ParseMoney(gArgs.GetArg("-reservebalance", ""), nReserveBalance))
        return error("CreateCoinStake : invalid reserve balance amount");

    if (nBalance <= nReserveBalance)
        return error("CreateCoinStake : balance is less than required to reserve");

    // presstab HyperStake - Initialize as static and don't update the set on every run of CreateCoinStake() in order to lighten resource use
    CAmount nTargetAmount = nBalance - nReserveBalance;

    if (setStakeCoins.empty() ||GetTime() - nLastStakeSetUpdate > nStakeSetUpdateTime) {
        setStakeCoins.clear();

        if (!SelectStakeCoins(setStakeCoins, nTargetAmount)) {
            LogPrint(BCLog::STAKING, "%s : no inputs eligable for staking\n", __func__);
            return false;
        }
        std::sort(setStakeCoins.begin(), setStakeCoins.end(),[](const auto& lhs, const auto& rhs) {
            return std::get<0>(lhs) > std::get<0>(rhs);
        });

        nLastStakeSetUpdate = GetTime();
    }

    LogPrint(BCLog::STAKING, "%s : found %u possible stake inputs\n", __func__, setStakeCoins.size());

    // NOTE: go from smaller amounts to bigger to increase chance, unlike it was before
    for (auto iter = setStakeCoins.begin(); iter != setStakeCoins.end(); ++iter) {
        if (ShutdownRequested()) {
            break;
        }

        CBlockIndex* pcoin_index = NULL;
        auto pWalletTxIn = std::get<1>(*iter);

        COutPoint prevoutStake = COutPoint(pWalletTxIn->GetHash(), std::get<2>(*iter));
        LogPrint(BCLog::STAKING, "%s : trying tx=%s n=%u\n", __func__,
                 __func__, pWalletTxIn->hashBlock.ToString().c_str());

        // Read block header
        BlockMap::iterator it = ::BlockIndex().find(pWalletTxIn->hashBlock);
        if (it != ::BlockIndex().end())
            pcoin_index = it->second;
        else {
            LogPrintf("%s : failed to find block index for %s \n",
                        pWalletTxIn->hashBlock.ToString().c_str());
            continue;
        }

        uint256 hashProofOfStake = uint256();

        //iterates each utxo inside of CheckStakeKernelHash()
        bool fKernelFound = CheckStakeKernelHash(
                curr_block,
                *pindex_prev,
                *pcoin_index,
                *pWalletTxIn->tx,
                prevoutStake,
                nHashDrift,
                false,
                hashProofOfStake,
                true);

        if (fKernelFound) {
            //Double check that this will pass time requirements
            if (curr_block.nTime <= ::ChainActive().Tip()->GetMedianTimePast()) {
                LogPrint(BCLog::STAKING, "%s kernel found, but it is too far in the past \n", __func__);
                continue;
            }

            // Found a kernel
            LogPrint(BCLog::STAKING, "%s  : kernel found\n", __func__ );

            std::vector<std::vector<unsigned char>> vSolutions;
            txnouttype whichType;
            CScript scriptPubKeyOut;
            const auto &tx_in = pWalletTxIn->tx->vout[prevoutStake.n];
            const auto &scriptPubKeyKernel = tx_in.scriptPubKey;

            whichType = Solver(scriptPubKeyKernel, vSolutions);

            if (whichType == TX_NONSTANDARD) {
                LogPrint(BCLog::STAKING, "%s : failed to parse kernel\n", __func__);
                continue;
            }

            LogPrint(BCLog::STAKING, "%s  : parsed kernel type=%d\n", __func__, whichType);

            if (whichType == TX_PUBKEYHASH) // pay to address type
            {
                // convert to pay to address type
                if (!GetPubKey(CKeyID(uint160(vSolutions[0])), curr_block.posPubKey)) {
                    LogPrint(BCLog::STAKING, "%s : failed to get key for kernel type=%d\n", __func__, whichType);
                    continue;
                }
            }
            else if (whichType == TX_PUBKEY) // pay to public key
            {
                curr_block.posPubKey = CPubKey(vSolutions[0]);
            }
            else
            {
                LogPrint(BCLog::STAKING, "%s : no support for kernel type=%d\n", __func__, whichType);
                continue;
            }

            assert(curr_block.posPubKey.IsValid());
            scriptPubKeyOut = GetScriptForDestination(curr_block.posPubKey.GetID());

            // Require the same miner's output in CoinBase
            coinbaseTx.vout[0].scriptPubKey = scriptPubKeyOut;

            CMutableTransaction stakeTx;
            stakeTx.vin.emplace_back(prevoutStake);
            // Mark coin stake transaction
            CScript scriptEmpty;
            scriptEmpty.clear();
            stakeTx.vout.push_back(CTxOut(0, scriptEmpty));
            stakeTx.vout.emplace_back(tx_in.nValue, scriptPubKeyOut);

            CAmount reward = stakeTx.vout[1].nValue;
            const CAmount split_threshold = nStakeSplitThreshold * COIN;
            const CAmount autocombine_target = split_threshold * 2 - 1;

            std::vector<CScript> vin_scripts;
            vin_scripts.emplace_back(scriptPubKeyKernel);

            if (fAutocombine != AUTOCOMBINE_DISABLE) {
                CKeyID scriptPubKeyID = curr_block.posPubKey.GetID();
                std::vector<CInputCoin> ac_candidates;
                // find candidates
                {
                    std::vector<CompactTallyItem> vecTallyRet = SelectCoinsGroupedByAddresses();
                    if (fAutocombine == AUTOCOMBINE_SAME) {
                        // Autocombine same
                        CTxDestination reqdest{scriptPubKeyID};
                        for (auto titer = vecTallyRet.begin(); titer != vecTallyRet.end(); ++titer) {
                            if (titer->txdest == reqdest) {
                                 ac_candidates.swap(titer->vecInputCoins);
                                 break;
                            }
                        }
                    } else if (fAutocombine == AUTOCOMBINE_ANY) {
                        for (auto titer = vecTallyRet.begin(); titer != vecTallyRet.end(); ++titer) {
                            ac_candidates.insert(
                                        ac_candidates.end(),
                                        titer->vecInputCoins.begin(), titer->vecInputCoins.end());
                        }
                    }
                }

                // Automatically combine
                auto ac_iter = ac_candidates.begin();
                auto min_age = Params().MinStakeAge();
                auto ac_len = ac_candidates.size();
                int inputs_included = 0;
                for (auto i = 0;
                     (i < ac_len) && (inputs_included < nStakeMaxSplit);
                     //(i < 500) && (reward < autocombine_target);
                     ++i
                ){
                     const CTxOut *ac_in = nullptr;
                     CAmount ac_amt = 0;
                     for (; ac_iter != ac_candidates.end(); ++ac_iter) {
                        if (ac_iter->outpoint == prevoutStake) {
                                continue;
                            }
                        const CWalletTx* wtx = GetWalletTx(ac_iter->outpoint.hash);
                        if (wtx == nullptr){
                            continue;
                        }

                        ac_in = &(wtx->tx->vout[ac_iter->outpoint.n]);
                        ac_amt = ac_in->nValue;

                        int64_t nTimeInput = (int64_t)wtx->GetTxTime() + (int64_t)min_age;
                        int64_t nCurrentTime = (int64_t)GetTime();
                        if (nTimeInput > nCurrentTime )
                        {
                            continue;
                        }
                        if (ac_amt < MIN_STAKE_AMOUNT){
                            break;
                        }
                        if ((reward + ac_amt) < autocombine_target)
                        {
                            break;
                        }
                     }

                    if (ac_iter == ac_candidates.end()) {
                        break;
                    }

                     inputs_included++;
                     stakeTx.vin.emplace_back(ac_iter->outpoint);
                     vin_scripts.emplace_back(ac_in->scriptPubKey);
                     reward += ac_amt;
                     LogPrint(BCLog::STAKING, "%s : auto-combining tx=%s n=%u amount=%llu total=%llu\n", __func__,
                              ac_iter->outpoint.hash.ToString().c_str(), ac_iter->outpoint.n, ac_amt, reward);
                     ++ac_iter;
                }
            }

            // Automatically split
            for (int i = 0; (i < nStakeMaxSplit) && (reward > split_threshold * 2); ++i) {
                stakeTx.vout.emplace_back(split_threshold, scriptPubKeyOut);
                reward -= split_threshold;
            }

            stakeTx.vout[1].nValue = reward;

            LogPrint(BCLog::STAKING, "%s : split stake vout into %llu pieces\n", __func__,
                     stakeTx.vout.size());

            for (size_t i = 0; i < vin_scripts.size(); ++i) {
                if (!SignSignature(*this, vin_scripts[i], stakeTx, i, reward, SIGHASH_ALL)) {
                    return error("CreateCoinStake : failed to sign coinstake");
                }
            }

            curr_block.posStakeHash = prevoutStake.hash;
            curr_block.posStakeN = prevoutStake.n;
            curr_block.Stake() = MakeTransactionRef(std::move(stakeTx));

            LogPrint(BCLog::STAKING, "%s : added kernel type=%d\n", __func__, whichType);

            // Force update
            nLastStakeSetUpdate = 0;
            return true;
        }
    }

    LogPrint(BCLog::STAKING, "%s : no stakes found\n", __func__);

    return false;
}

void CWallet::CommitTransaction(CTransactionRef tx, mapValue_t mapValue, std::vector<std::pair<std::string, std::string>> orderForm)
{
    auto locked_chain = chain().lock();
    LOCK(cs_wallet);

    CWalletTx wtxNew(this, std::move(tx));
    wtxNew.mapValue = std::move(mapValue);
    wtxNew.vOrderForm = std::move(orderForm);
    wtxNew.fTimeReceivedIsTxTime = true;
    wtxNew.fFromMe = true;

    WalletLogPrintf("CommitTransaction:\n%s", wtxNew.tx->ToString()); /* Continued */
    // Add tx to wallet, because if it has change it's also ours,
    // otherwise just for transaction history.
    AddToWallet(wtxNew);

    // Notify that old coins are spent
    std::set<uint256> updated_hahes;
    for (const CTxIn& txin : wtxNew.tx->vin){
        // notify only once
        if(updated_hahes.find(txin.prevout.hash) != updated_hahes.end()) continue;

        CWalletTx &coin = mapWallet.at(txin.prevout.hash);
        coin.BindWallet(this);
        NotifyTransactionChanged(this, txin.prevout.hash, CT_UPDATED);
        updated_hahes.insert(txin.prevout.hash);
    }
    // Get the inserted-CWalletTx from mapWallet so that the
    // fInMempool flag is cached properly
    CWalletTx& wtx = mapWallet.at(wtxNew.GetHash());

    if (!fBroadcastTransactions) {
        // Don't submit tx to the mempool
        return;
    }

    std::string err_string;
    if (!wtx.SubmitMemoryPoolAndRelay(err_string, true, *locked_chain)) {
        WalletLogPrintf("CommitTransaction(): Transaction cannot be broadcast immediately, %s\n", err_string);
        // TODO: if we expect the failure to be long term or permanent, instead delete wtx from the wallet and return failure.
    }
}

DBErrors CWallet::LoadWallet(bool& fFirstRunRet)
{
    auto locked_chain = chain().lock();
    LockAnnotation lock(::cs_main);
    LOCK(cs_wallet);

    fFirstRunRet = false;
    DBErrors nLoadWalletRet = WalletBatch(*database,"cr+").LoadWallet(this);
    if (nLoadWalletRet == DBErrors::NEED_REWRITE)
    {
        if (database->Rewrite("\x04pool"))
        {
            setInternalKeyPool.clear();
            setExternalKeyPool.clear();
            nKeysLeftSinceAutoBackup = 0;
            m_pool_key_to_index.clear();
            // Note: can't top-up keypool here, because wallet is locked.
            // User will be prompted to unlock wallet the next operation
            // that requires a new key.
        }
    }

    {
        LOCK(cs_KeyStore);
        // This wallet is in its first run if all of these are empty
        fFirstRunRet = mapKeys.empty() && mapHdPubKeys.empty() && mapCryptedKeys.empty() && mapWatchKeys.empty() && setWatchOnly.empty() && mapScripts.empty() && !IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS) && !IsWalletFlagSet(WALLET_FLAG_BLANK_WALLET);
    }

    for (auto& pair : mapWallet) {
        for(unsigned int i = 0; i < pair.second.tx->vout.size(); ++i) {
            if (IsMine(pair.second.tx->vout[i]) && !IsSpent(*locked_chain, pair.first, i)) {
                setWalletUTXO.insert(COutPoint(pair.first, i));
            }
        }
    }

    InitCoinJoinSalt();

    if (nLoadWalletRet != DBErrors::LOAD_OK)
        return nLoadWalletRet;

    return DBErrors::LOAD_OK;
}

// Goes through all wallet transactions and checks if they are masternode collaterals, in which case these are locked
// This avoids accidental spending of collaterals. They can still be unlocked manually if a spend is really intended.
void CWallet::AutoLockMasternodeCollaterals()
{
    auto mnList = deterministicMNManager->GetListAtChainTip();

    auto locked_chain = chain().lock();
    LOCK(cs_wallet);
    for (const auto& pair : mapWallet) {
        for (unsigned int i = 0; i < pair.second.tx->vout.size(); ++i) {
            if (IsMine(pair.second.tx->vout[i]) && !IsSpent(*locked_chain, pair.first, i)) {
                if (deterministicMNManager->IsProTxWithCollateral(pair.second.tx, i) || mnList.HasMNByCollateral(COutPoint(pair.first, i))) {
                    LockCoin(COutPoint(pair.first, i));
                }
            }
        }
    }
}

DBErrors CWallet::ZapSelectTx(std::vector<uint256>& vHashIn, std::vector<uint256>& vHashOut)
{
    AssertLockHeld(cs_wallet);
    DBErrors nZapSelectTxRet = WalletBatch(*database, "cr+").ZapSelectTx(vHashIn, vHashOut);
    for (uint256 hash : vHashOut) {
        const auto& it = mapWallet.find(hash);
        wtxOrdered.erase(it->second.m_it_wtxOrdered);
        mapWallet.erase(it);
        NotifyTransactionChanged(this, hash, CT_DELETED);
    }

    if (nZapSelectTxRet == DBErrors::NEED_REWRITE)
    {
        if (database->Rewrite("\x04pool"))
        {
            setInternalKeyPool.clear();
            setExternalKeyPool.clear();
            m_pool_key_to_index.clear();
            // Note: can't top-up keypool here, because wallet is locked.
            // User will be prompted to unlock wallet the next operation
            // that requires a new key.
        }
    }

    if (nZapSelectTxRet != DBErrors::LOAD_OK)
        return nZapSelectTxRet;

    MarkDirty();

    return DBErrors::LOAD_OK;
}

DBErrors CWallet::ZapWalletTx(std::vector<CWalletTx>& vWtx)
{
    DBErrors nZapWalletTxRet = WalletBatch(*database,"cr+").ZapWalletTx(vWtx);
    if (nZapWalletTxRet == DBErrors::NEED_REWRITE)
    {
        if (database->Rewrite("\x04pool"))
        {
            LOCK(cs_wallet);
            setInternalKeyPool.clear();
            setExternalKeyPool.clear();
            nKeysLeftSinceAutoBackup = 0;
            m_pool_key_to_index.clear();
            // Note: can't top-up keypool here, because wallet is locked.
            // User will be prompted to unlock wallet the next operation
            // that requires a new key.
        }
    }

    if (nZapWalletTxRet != DBErrors::LOAD_OK)
        return nZapWalletTxRet;

    return DBErrors::LOAD_OK;
}

bool CWallet::SetAddressBookWithDB(WalletBatch& batch, const CTxDestination& address, const std::string& strName, const std::string& strPurpose)
{
    bool fUpdated = false;
    {
        LOCK(cs_wallet);
        std::map<CTxDestination, CAddressBookData>::iterator mi = mapAddressBook.find(address);
        fUpdated = mi != mapAddressBook.end();
        mapAddressBook[address].name = strName;
        if (!strPurpose.empty()) /* update purpose only if requested */
            mapAddressBook[address].purpose = strPurpose;
    }
    NotifyAddressBookChanged(this, address, strName, ::IsMine(*this, address) != ISMINE_NO,
                             strPurpose, (fUpdated ? CT_UPDATED : CT_NEW) );
    if (!strPurpose.empty() && !batch.WritePurpose(EncodeDestination(address), strPurpose))
        return false;
    return batch.WriteName(EncodeDestination(address), strName);
}

bool CWallet::SetAddressBook(const CTxDestination& address, const std::string& strName, const std::string& strPurpose)
{
    WalletBatch batch(*database);
    return SetAddressBookWithDB(batch, address, strName, strPurpose);
}

bool CWallet::DelAddressBook(const CTxDestination& address)
{
    {
        LOCK(cs_wallet);

        // Delete destdata tuples associated with address
        std::string strAddress = EncodeDestination(address);
        for (const std::pair<const std::string, std::string> &item : mapAddressBook[address].destdata)
        {
            WalletBatch(*database).EraseDestData(strAddress, item.first);
        }
        mapAddressBook.erase(address);
    }

    NotifyAddressBookChanged(this, address, "", ::IsMine(*this, address) != ISMINE_NO, "", CT_DELETED);

    WalletBatch(*database).ErasePurpose(EncodeDestination(address));
    return WalletBatch(*database).EraseName(EncodeDestination(address));
}

const std::string& CWallet::GetLabelName(const CScript& scriptPubKey) const
{
    CTxDestination address;
    if (ExtractDestination(scriptPubKey, address) && !scriptPubKey.IsUnspendable()) {
        auto mi = mapAddressBook.find(address);
        if (mi != mapAddressBook.end()) {
            return mi->second.name;
        }
    }
    // A scriptPubKey that doesn't have an entry in the address book is
    // associated with the default label ("").
    const static std::string DEFAULT_LABEL_NAME;
    return DEFAULT_LABEL_NAME;
}

/**
 * Mark old keypool keys as used,
 * and generate all new keys
 */
bool CWallet::NewKeyPool()
{
    if (IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
        return false;
    }
    {
        LOCK(cs_wallet);
        WalletBatch batch(*database);
        for (const int64_t nIndex : setInternalKeyPool) {
            batch.ErasePool(nIndex);
        }
        setInternalKeyPool.clear();
        for (const int64_t nIndex : setExternalKeyPool) {
            batch.ErasePool(nIndex);
        }
        setExternalKeyPool.clear();
        auto it = coinJoinClientManagers.find(GetName());
        if (it != coinJoinClientManagers.end()) {
            it->second->StopMixing();
        }
        nKeysLeftSinceAutoBackup = 0;

        m_pool_key_to_index.clear();

        if (!TopUpKeyPool())
            return false;

        WalletLogPrintf("CWallet::NewKeyPool rewrote keypool\n");
    }
    return true;
}

size_t CWallet::KeypoolCountExternalKeys()
{
    AssertLockHeld(cs_wallet);
    return setExternalKeyPool.size();
}

void CWallet::LoadKeyPool(int64_t nIndex, const CKeyPool &keypool)
{
    AssertLockHeld(cs_wallet);
    if (keypool.fInternal) {
        setInternalKeyPool.insert(nIndex);
    } else {
        setExternalKeyPool.insert(nIndex);
    }
    m_max_keypool_index = std::max(m_max_keypool_index, nIndex);
    m_pool_key_to_index[keypool.vchPubKey.GetID()] = nIndex;

    // If no metadata exists yet, create a default with the pool key's
    // creation time. Note that this may be overwritten by actually
    // stored metadata for that key later, which is fine.
    CKeyID keyid = keypool.vchPubKey.GetID();
    if (mapKeyMetadata.count(keyid) == 0)
        mapKeyMetadata[keyid] = CKeyMetadata(keypool.nTime);
}

size_t CWallet::KeypoolCountInternalKeys()
{
    AssertLockHeld(cs_wallet); // setInternalKeyPool
    return setInternalKeyPool.size();
}

bool CWallet::TopUpKeyPool(unsigned int kpSize)
{
    if (!CanGenerateKeys()) {
        return false;
    }
    {
        LOCK(cs_wallet);

        if (IsLocked(true)) return false;

        // Top up key pool
        unsigned int nTargetSize;
        if (kpSize > 0)
            nTargetSize = kpSize;
        else
            nTargetSize = std::max(gArgs.GetArg("-keypool", DEFAULT_KEYPOOL_SIZE), (int64_t) 0);

        // count amount of available keys (internal, external)
        // make sure the keypool of external and internal keys fits the user selected target (-keypool)
        int64_t amountExternal = setExternalKeyPool.size();
        int64_t amountInternal = setInternalKeyPool.size();
        int64_t missingExternal = std::max(std::max((int64_t) nTargetSize, (int64_t) 1) - amountExternal, (int64_t) 0);
        int64_t missingInternal = std::max(std::max((int64_t) nTargetSize, (int64_t) 1) - amountInternal, (int64_t) 0);

        if (!IsHDEnabled())
        {
            // don't create extra internal keys
            missingInternal = 0;
        } else {
            nTargetSize *= 2;
        }
        bool fInternal = false;
        WalletBatch batch(*database);
        for (int64_t i = missingInternal + missingExternal; i--;)
        {
            if (i < missingInternal) {
                fInternal = true;
            }

            // TODO: implement keypools for all accounts?
            CPubKey pubkey(GenerateNewKey(batch, 0, fInternal));
            AddKeypoolPubkeyWithDB(pubkey, fInternal, batch);

            if (missingInternal + missingExternal > 0) {
                WalletLogPrintf("keypool added %d keys (%d internal), size=%u (%u internal)\n",
                          missingInternal + missingExternal, missingInternal,
                          setInternalKeyPool.size() + setExternalKeyPool.size(), setInternalKeyPool.size());
            }

            double dProgress = 100.f * m_max_keypool_index / (nTargetSize + 1);
            std::string strMsg = strprintf(_("Loading wallet... (%3.2f %%)").translated, dProgress);
            uiInterface.InitMessage(strMsg);
        }
    }
    NotifyCanGetAddressesChanged();
    return true;
}

void CWallet::AddKeypoolPubkey(const CPubKey& pubkey, const bool internal)
{
    WalletBatch batch(*database);
    AddKeypoolPubkeyWithDB(pubkey, internal, batch);
    NotifyCanGetAddressesChanged();
}

void CWallet::AddKeypoolPubkeyWithDB(const CPubKey& pubkey, const bool internal, WalletBatch& batch)
{
    LOCK(cs_wallet);
    assert(m_max_keypool_index < std::numeric_limits<int64_t>::max()); // How in the hell did you use so many keys?
    int64_t index = ++m_max_keypool_index;
    if (!batch.WritePool(index, CKeyPool(pubkey, internal))) {
        throw std::runtime_error(std::string(__func__) + ": writing imported pubkey failed");
    }
    if (internal) {
        setInternalKeyPool.insert(index);
    } else {
        setExternalKeyPool.insert(index);
    }
    m_pool_key_to_index[pubkey.GetID()] = index;
}

bool CWallet::ReserveKeyFromKeyPool(int64_t& nIndex, CKeyPool& keypool, bool fRequestedInternal)
{
    nIndex = -1;
    keypool.vchPubKey = CPubKey();
    {
        LOCK(cs_wallet);

        TopUpKeyPool();

        bool fReturningInternal = fRequestedInternal;
        fReturningInternal &= IsHDEnabled() || IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS);
        std::set<int64_t>& setKeyPool = fReturningInternal ? setInternalKeyPool : setExternalKeyPool;

        // Get the oldest key
        if (setKeyPool.empty()) {
            return false;
        }

        WalletBatch batch(*database);

        nIndex = *setKeyPool.begin();
        setKeyPool.erase(nIndex);
        if (!batch.ReadPool(nIndex, keypool)) {
            throw std::runtime_error(std::string(__func__) + ": read failed");
        }
        CPubKey pk;
        if (!GetPubKey(keypool.vchPubKey.GetID(), pk)) {
            throw std::runtime_error(std::string(__func__) + ": unknown key in key pool");
        }
        if (keypool.fInternal != fReturningInternal) {
            throw std::runtime_error(std::string(__func__) + ": keypool entry misclassified");
        }
        if (!keypool.vchPubKey.IsValid()) {
            throw std::runtime_error(std::string(__func__) + ": keypool entry invalid");
        }

        m_pool_key_to_index.erase(keypool.vchPubKey.GetID());
        WalletLogPrintf("keypool reserve %d\n", nIndex);
    }
    NotifyCanGetAddressesChanged();
    return true;
}

void CWallet::KeepKey(int64_t nIndex)
{
    // Remove from key pool
    {
        LOCK(cs_wallet);
        WalletBatch batch(*database);
        if (batch.ErasePool(nIndex))
            --nKeysLeftSinceAutoBackup;
        if (!nWalletBackups)
            nKeysLeftSinceAutoBackup = 0;
    }
    WalletLogPrintf("keypool keep %d\n", nIndex);
}

void CWallet::ReturnKey(int64_t nIndex, bool fInternal, const CPubKey& pubkey)
{
    // Return to key pool
    {
        LOCK(cs_wallet);
        if (fInternal) {
            setInternalKeyPool.insert(nIndex);
        } else {
            setExternalKeyPool.insert(nIndex);
        }
        m_pool_key_to_index[pubkey.GetID()] = nIndex;
        NotifyCanGetAddressesChanged();
    }
    WalletLogPrintf("keypool return %d\n", nIndex);
}

bool CWallet::GetKeyFromPool(CPubKey& result, bool internal)
{
    if (!CanGetAddresses(internal)) {
        return false;
    }

    CKeyPool keypool;
    {
        LOCK(cs_wallet);
        int64_t nIndex;
        if (!ReserveKeyFromKeyPool(nIndex, keypool, internal) && !IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
            if (IsLocked(true)) return false;
            // TODO: implement keypool for all accouts?
            WalletBatch batch(*database);
            result = GenerateNewKey(batch, 0, internal);
            return true;
        }
        KeepKey(nIndex);
        result = keypool.vchPubKey;
    }
    return true;
}

static int64_t GetOldestKeyInPool(const std::set<int64_t>& setKeyPool, WalletBatch& batch) {
    CKeyPool keypool;
    int64_t nIndex = *(setKeyPool.begin());
    if (!batch.ReadPool(nIndex, keypool)) {
        throw std::runtime_error(std::string(__func__) + ": read oldest key in keypool failed");
    }
    assert(keypool.vchPubKey.IsValid());
    return keypool.nTime;
}

int64_t CWallet::GetOldestKeyPoolTime()
{
    LOCK(cs_wallet);

    // if the keypool is empty, return <NOW>
    if (setExternalKeyPool.empty() && setInternalKeyPool.empty())
        return GetTime();

    WalletBatch batch(*database);
    int64_t oldestKey = -1;

    // load oldest key from keypool, get time and return
    if (!setInternalKeyPool.empty()) {
        oldestKey = std::max(GetOldestKeyInPool(setInternalKeyPool, batch), oldestKey);
    }
    if (!setExternalKeyPool.empty()) {
        oldestKey = std::max(GetOldestKeyInPool(setExternalKeyPool, batch), oldestKey);
    }
    return oldestKey;
}

std::map<CTxDestination, CAmount> CWallet::GetAddressBalances(interfaces::Chain::Lock& locked_chain)
{
    std::map<CTxDestination, CAmount> balances;

    {
        LOCK(cs_wallet);
        for (const auto& walletEntry : mapWallet)
        {
            const CWalletTx *pcoin = &walletEntry.second;

            if (!pcoin->IsTrusted(locked_chain))
                continue;

            if (pcoin->IsImmatureCoinBase(locked_chain))
                continue;

            int nDepth = pcoin->GetDepthInMainChain(locked_chain);
            if ((nDepth < (pcoin->IsFromMe(ISMINE_ALL) ? 0 : 1)) && !pcoin->IsLockedByInstantSend())
                continue;

            for (unsigned int i = 0; i < pcoin->tx->vout.size(); i++)
            {
                CTxDestination addr;
                if (!IsMine(pcoin->tx->vout[i]))
                    continue;
                if(!ExtractDestination(pcoin->tx->vout[i].scriptPubKey, addr))
                    continue;

                CAmount n = IsSpent(locked_chain, walletEntry.first, i) ? 0 : pcoin->tx->vout[i].nValue;

                if (!balances.count(addr))
                    balances[addr] = 0;
                balances[addr] += n;
            }
        }
    }

    return balances;
}

std::set< std::set<CTxDestination> > CWallet::GetAddressGroupings()
{
    AssertLockHeld(cs_wallet);
    std::set< std::set<CTxDestination> > groupings;
    std::set<CTxDestination> grouping;

    for (const auto& walletEntry : mapWallet)
    {
        const CWalletTx *pcoin = &walletEntry.second;

        if (pcoin->tx->vin.size() > 0)
        {
            bool any_mine = false;
            // group all input addresses with each other
            for (const CTxIn& txin : pcoin->tx->vin)
            {
                CTxDestination address;
                if(!IsMine(txin)) /* If this input isn't mine, ignore it */
                    continue;
                if(!ExtractDestination(mapWallet.at(txin.prevout.hash).tx->vout[txin.prevout.n].scriptPubKey, address))
                    continue;
                grouping.insert(address);
                any_mine = true;
            }

            // group change with input addresses
            if (any_mine)
            {
               for (const CTxOut& txout : pcoin->tx->vout)
                   if (IsChange(txout))
                   {
                       CTxDestination txoutAddr;
                       if(!ExtractDestination(txout.scriptPubKey, txoutAddr))
                           continue;
                       grouping.insert(txoutAddr);
                   }
            }
            if (grouping.size() > 0)
            {
                groupings.insert(grouping);
                grouping.clear();
            }
        }

        // group lone addrs by themselves
        for (const auto& txout : pcoin->tx->vout)
            if (IsMine(txout))
            {
                CTxDestination address;
                if(!ExtractDestination(txout.scriptPubKey, address))
                    continue;
                grouping.insert(address);
                groupings.insert(grouping);
                grouping.clear();
            }
    }

    std::set< std::set<CTxDestination>* > uniqueGroupings; // a set of pointers to groups of addresses
    std::map< CTxDestination, std::set<CTxDestination>* > setmap;  // map addresses to the unique group containing it
    for (std::set<CTxDestination> _grouping : groupings)
    {
        // make a set of all the groups hit by this new group
        std::set< std::set<CTxDestination>* > hits;
        std::map< CTxDestination, std::set<CTxDestination>* >::iterator it;
        for (const CTxDestination& address : _grouping)
            if ((it = setmap.find(address)) != setmap.end())
                hits.insert((*it).second);

        // merge all hit groups into a new single group and delete old groups
        std::set<CTxDestination>* merged = new std::set<CTxDestination>(_grouping);
        for (std::set<CTxDestination>* hit : hits)
        {
            merged->insert(hit->begin(), hit->end());
            uniqueGroupings.erase(hit);
            delete hit;
        }
        uniqueGroupings.insert(merged);

        // update setmap
        for (const CTxDestination& element : *merged)
            setmap[element] = merged;
    }

    std::set< std::set<CTxDestination> > ret;
    for (const std::set<CTxDestination>* uniqueGrouping : uniqueGroupings)
    {
        ret.insert(*uniqueGrouping);
        delete uniqueGrouping;
    }

    return ret;
}

std::set<CTxDestination> CWallet::GetLabelAddresses(const std::string& label) const
{
    LOCK(cs_wallet);
    std::set<CTxDestination> result;
    for (const std::pair<const CTxDestination, CAddressBookData>& item : mapAddressBook)
    {
        const CTxDestination& address = item.first;
        const std::string& strName = item.second.name;
        if (strName == label)
            result.insert(address);
    }
    return result;
}

bool CReserveKey::GetReservedKey(CPubKey& pubkey, bool fInternalIn)
{
    if (!pwallet->CanGetAddresses(fInternalIn)) {
        return false;
    }

    if (nIndex == -1)
    {
        CKeyPool keypool;
        if (!pwallet->ReserveKeyFromKeyPool(nIndex, keypool, fInternalIn)) {
            return false;
        }
        vchPubKey = keypool.vchPubKey;
        fInternal = keypool.fInternal;
    }
    assert(vchPubKey.IsValid());
    pubkey = vchPubKey;
    return true;
}

void CReserveKey::KeepKey()
{
    if (nIndex != -1) {
        pwallet->KeepKey(nIndex);
    }
    nIndex = -1;
    vchPubKey = CPubKey();
}

void CReserveKey::ReturnKey()
{
    if (nIndex != -1) {
        pwallet->ReturnKey(nIndex, fInternal, vchPubKey);
    }
    nIndex = -1;
    vchPubKey = CPubKey();
}

void CWallet::MarkReserveKeysAsUsed(int64_t keypool_id)
{
    AssertLockHeld(cs_wallet);
    bool internal = setInternalKeyPool.count(keypool_id);
    if (!internal) assert(setExternalKeyPool.count(keypool_id));
    std::set<int64_t> *setKeyPool = internal ? &setInternalKeyPool : &setExternalKeyPool;
    auto it = setKeyPool->begin();

    WalletBatch batch(*database);
    while (it != std::end(*setKeyPool)) {
        const int64_t& index = *(it);
        if (index > keypool_id) break; // set*KeyPool is ordered

        CKeyPool keypool;
        if (batch.ReadPool(index, keypool)) { //TODO: This should be unnecessary
            m_pool_key_to_index.erase(keypool.vchPubKey.GetID());
        }
        batch.ErasePool(index);
        WalletLogPrintf("keypool index %d removed\n", index);
        it = setKeyPool->erase(it);
    }
}

void CWallet::GetScriptForMining(std::shared_ptr<CReserveScript> &script)
{
    std::shared_ptr<CReserveKey> rKey = std::make_shared<CReserveKey>(this);
    CPubKey pubkey;
    if (!rKey->GetReservedKey(pubkey, false))
        return;

    script = rKey;
    script->reserveScript = CScript() << ToByteVector(pubkey) << OP_CHECKSIG;
}

void CWallet::LockCoin(const COutPoint& output)
{
    AssertLockHeld(cs_wallet);
    setLockedCoins.insert(output);
    std::map<uint256, CWalletTx>::iterator it = mapWallet.find(output.hash);
    if (it != mapWallet.end()) it->second.MarkDirty(); // recalculate all credits for this tx

    fAnonymizableTallyCached = false;
    fAnonymizableTallyCachedNonDenom = false;
}

void CWallet::UnlockCoin(const COutPoint& output)
{
    AssertLockHeld(cs_wallet);
    setLockedCoins.erase(output);
    std::map<uint256, CWalletTx>::iterator it = mapWallet.find(output.hash);
    if (it != mapWallet.end()) it->second.MarkDirty(); // recalculate all credits for this tx

    fAnonymizableTallyCached = false;
    fAnonymizableTallyCachedNonDenom = false;
}

void CWallet::UnlockAllCoins()
{
    AssertLockHeld(cs_wallet);
    setLockedCoins.clear();
}

bool CWallet::IsLockedCoin(uint256 hash, unsigned int n) const
{
    AssertLockHeld(cs_wallet);
    COutPoint outpt(hash, n);

    return (setLockedCoins.count(outpt) > 0);
}

void CWallet::ListLockedCoins(std::vector<COutPoint>& vOutpts) const
{
    AssertLockHeld(cs_wallet);
    for (std::set<COutPoint>::iterator it = setLockedCoins.begin();
         it != setLockedCoins.end(); it++) {
        COutPoint outpt = (*it);
        vOutpts.push_back(outpt);
    }
}

void CWallet::ListProTxCoins(std::vector<COutPoint>& vOutpts) const
{
    auto mnList = deterministicMNManager->GetListAtChainTip();

    AssertLockHeld(cs_wallet);
    for (const auto &o : setWalletUTXO) {
        auto it = mapWallet.find(o.hash);
        if (it != mapWallet.end()) {
            const auto &p = it->second;
            if (deterministicMNManager->IsProTxWithCollateral(p.tx, o.n) || mnList.HasMNByCollateral(o)) {
                vOutpts.emplace_back(o);
            }
        }
    }
}

/** @} */ // end of Actions

void CWallet::GetKeyBirthTimes(interfaces::Chain::Lock& locked_chain, std::map<CTxDestination, int64_t>& mapKeyBirth) const {
    AssertLockHeld(::cs_main); // LookupBlockIndex
    AssertLockHeld(cs_wallet);
    mapKeyBirth.clear();

    // get birth times for keys with metadata
    for (const auto& entry : mapKeyMetadata) {
        if (entry.second.nCreateTime) {
            mapKeyBirth[entry.first] = entry.second.nCreateTime;
        }
    }

    // map in which we'll infer heights of other keys
    const Optional<int> tip_height = locked_chain.getHeight();
    const int max_height = tip_height && *tip_height > 144 ? *tip_height - 144 : 0; // the tip can be reorganized; use a 144-block safety margin
    std::map<CKeyID, int> mapKeyFirstBlock;
    for (const CKeyID &keyid : GetKeys()) {
        if (mapKeyBirth.count(keyid) == 0)
            mapKeyFirstBlock[keyid] = max_height;
    }

    // if there are no such keys, we're done
    if (mapKeyFirstBlock.empty())
        return;

    // find first block that affects those keys, if there are any left
    std::vector<CKeyID> vAffected;
    for (const auto& entry : mapWallet) {
        // iterate over all wallet transactions...
        const CWalletTx &wtx = entry.second;
        if (Optional<int> height = locked_chain.getBlockHeight(wtx.hashBlock)) {
            // ... which are already in a block
            for (const CTxOut &txout : wtx.tx->vout) {
                // iterate over all their outputs
                CAffectedKeysVisitor(*this, vAffected).Process(txout.scriptPubKey);
                for (const CKeyID &keyid : vAffected) {
                    // ... and all their affected keys
                    std::map<CKeyID, int>::iterator rit = mapKeyFirstBlock.find(keyid);
                    if (rit != mapKeyFirstBlock.end() && *height < rit->second)
                        rit->second = *height;
                }
                vAffected.clear();
            }
        }
    }

    // Extract block timestamps for those keys
    for (const auto& entry : mapKeyFirstBlock)
        mapKeyBirth[entry.first] = locked_chain.getBlockTime(entry.second) - TIMESTAMP_WINDOW; // block times can be 2h off
}

/**
 * Compute smart timestamp for a transaction being added to the wallet.
 *
 * Logic:
 * - If sending a transaction, assign its timestamp to the current time.
 * - If receiving a transaction outside a block, assign its timestamp to the
 *   current time.
 * - If receiving a block with a future timestamp, assign all its (not already
 *   known) transactions' timestamps to the current time.
 * - If receiving a block with a past timestamp, before the most recent known
 *   transaction (that we care about), assign all its (not already known)
 *   transactions' timestamps to the same timestamp as that most-recent-known
 *   transaction.
 * - If receiving a block with a past timestamp, but after the most recent known
 *   transaction, assign all its (not already known) transactions' timestamps to
 *   the block time.
 *
 * For more information see CWalletTx::nTimeSmart,
 * https://bitcointalk.org/?topic=54527, or
 * https://github.com/bitcoin/bitcoin/pull/1393.
 */
unsigned int CWallet::ComputeTimeSmart(const CWalletTx& wtx) const
{
    AssertLockHeld(::cs_main);
    unsigned int nTimeSmart = wtx.nTimeReceived;
    if (!wtx.hashUnset()) {
        int64_t blocktime;
        if (chain().findBlock(wtx.hashBlock, nullptr /* block */, &blocktime)) {
            int64_t latestNow = wtx.nTimeReceived;
            int64_t latestEntry = 0;

            // Tolerate times up to the last timestamp in the wallet not more than 5 minutes into the future
            int64_t latestTolerated = latestNow + 300;
            const TxItems& txOrdered = wtxOrdered;
            for (auto it = txOrdered.rbegin(); it != txOrdered.rend(); ++it) {
                CWalletTx* const pwtx = it->second;
                if (pwtx == &wtx) {
                    continue;
                }
                int64_t nSmartTime;
                nSmartTime = pwtx->nTimeSmart;
                if (!nSmartTime) {
                    nSmartTime = pwtx->nTimeReceived;
                }
                if (nSmartTime <= latestTolerated) {
                    latestEntry = nSmartTime;
                    if (nSmartTime > latestNow) {
                        latestNow = nSmartTime;
                    }
                    break;
                }
            }

            nTimeSmart = std::max(latestEntry, std::min(blocktime, latestNow));
        } else {
            WalletLogPrintf("%s: found %s in block %s not in index\n", __func__, wtx.GetHash().ToString(), wtx.hashBlock.ToString());
        }
    }
    return nTimeSmart;
}

bool CWallet::AddDestData(const CTxDestination &dest, const std::string &key, const std::string &value)
{
    if (boost::get<CNoDestination>(&dest))
        return false;

    mapAddressBook[dest].destdata.insert(std::make_pair(key, value));
    return WalletBatch(*database).WriteDestData(EncodeDestination(dest), key, value);
}

bool CWallet::EraseDestData(const CTxDestination &dest, const std::string &key)
{
    if (!mapAddressBook[dest].destdata.erase(key))
        return false;
    return WalletBatch(*database).EraseDestData(EncodeDestination(dest), key);
}

void CWallet::LoadDestData(const CTxDestination &dest, const std::string &key, const std::string &value)
{
    mapAddressBook[dest].destdata.insert(std::make_pair(key, value));
}

bool CWallet::GetDestData(const CTxDestination &dest, const std::string &key, std::string *value) const
{
    std::map<CTxDestination, CAddressBookData>::const_iterator i = mapAddressBook.find(dest);
    if(i != mapAddressBook.end())
    {
        CAddressBookData::StringMap::const_iterator j = i->second.destdata.find(key);
        if(j != i->second.destdata.end())
        {
            if(value)
                *value = j->second;
            return true;
        }
    }
    return false;
}

std::vector<std::string> CWallet::GetDestValues(const std::string& prefix) const
{
    std::vector<std::string> values;
    for (const auto& address : mapAddressBook) {
        for (const auto& data : address.second.destdata) {
            if (!data.first.compare(0, prefix.size(), prefix)) {
                values.emplace_back(data.second);
            }
        }
    }
    return values;
}

bool CWallet::Verify(interfaces::Chain& chain, const WalletLocation& location, bilingual_str& error_string, std::vector<bilingual_str>& warnings)
{
    // Do some checking on wallet path. It should be either a:
    //
    // 1. Path where a directory can be created.
    // 2. Path to an existing directory.
    // 3. Path to a symlink to a directory.
    // 4. For backwards compatibility, the name of a data file in -walletdir.
    LOCK(cs_wallets);
    const fs::path& wallet_path = location.GetPath();
    fs::file_type path_type = fs::symlink_status(wallet_path).type();
    if (!(path_type == fs::file_not_found || path_type == fs::directory_file ||
          (path_type == fs::symlink_file && fs::is_directory(wallet_path)) ||
          (path_type == fs::regular_file && fs::path(location.GetName()).filename() == location.GetName()))) {
        error_string = Untranslated(strprintf(
              "Invalid -wallet path '%s'. -wallet path should point to a directory where wallet.dat and "
              "database/log.?????????? files can be stored, a location where such a directory could be created, "
              "or (for backwards compatibility) the name of an existing data file in -walletdir (%s)",
              location.GetName(), GetWalletDir()));
        return false;
    }

    // Make sure that the wallet path doesn't clash with an existing wallet path
    if (IsWalletLoaded(wallet_path)) {
        error_string = Untranslated(strprintf("Error loading wallet %s. Duplicate -wallet filename specified.", location.GetName()));
        return false;
    }

    // Keep same database environment instance across Verify/Recover calls below.
    std::unique_ptr<WalletDatabase> database = CreateWalletDatabase(wallet_path);

    try {
        if (!database->Verify(error_string)) {
            return false;
        }
    } catch (const fs::filesystem_error& e) {
        error_string = Untranslated(strprintf("Error loading wallet %s. %s", location.GetName(), fsbridge::get_filesystem_error_message(e)));
        return false;
    }

    // Let tempWallet hold the pointer to the corresponding wallet database.
    std::unique_ptr<CWallet> tempWallet = MakeUnique<CWallet>(chain, location, std::move(database));
    if (!tempWallet->AutoBackupWallet(wallet_path, error_string, warnings) && !error_string.original.empty()) {
        return false;
    }

    return true;
}

std::shared_ptr<CWallet> CWallet::CreateWalletFromFile(interfaces::Chain& chain, const WalletLocation& location, bilingual_str& error, std::vector<bilingual_str>& warnings, uint64_t wallet_creation_flags)
{
    const std::string walletFile = WalletDataFilePath(location.GetPath()).string();

    // needed to restore wallet transaction meta data after -zapwallettxes
    std::vector<CWalletTx> vWtx;

    if (gArgs.GetBoolArg("-zapwallettxes", false)) {
        chain.initMessage(_("Zapping all transactions from wallet...").translated);

        std::unique_ptr<CWallet> tempWallet = MakeUnique<CWallet>(chain, location, CreateWalletDatabase(location.GetPath()));
        DBErrors nZapWalletRet = tempWallet->ZapWalletTx(vWtx);
        if (nZapWalletRet != DBErrors::LOAD_OK) {
            error = strprintf(_("Error loading %s: Wallet corrupted"), walletFile);
            return nullptr;
        }
    }

    chain.initMessage(_("Loading wallet...").translated);

    int64_t nStart = GetTimeMillis();
    bool fFirstRun = true;
    // TODO: Can't use std::make_shared because we need a custom deleter but
    // should be possible to use std::allocate_shared.
    std::shared_ptr<CWallet> walletInstance(new CWallet(chain, location, CreateWalletDatabase(location.GetPath())), ReleaseWallet);
    AddWallet(walletInstance);
    auto unload_wallet = [&](const bilingual_str& strError) {
        RemoveWallet(walletInstance);
        error = strError;
        return nullptr;
    };
    DBErrors nLoadWalletRet;
    try {
        nLoadWalletRet = walletInstance->LoadWallet(fFirstRun);
    } catch (const std::exception& e) {
        RemoveWallet(walletInstance);
        throw;
    }
    if (nLoadWalletRet != DBErrors::LOAD_OK)
    {
        if (nLoadWalletRet == DBErrors::CORRUPT) {
            return unload_wallet(strprintf(_("Error loading %s: Wallet corrupted"), walletFile));
        }
        else if (nLoadWalletRet == DBErrors::NONCRITICAL_ERROR)
        {
            warnings.push_back(strprintf(_("Error reading %s! All keys read correctly, but transaction data"
                                           " or address book entries might be missing or incorrect."),
                walletFile));
        }
        else if (nLoadWalletRet == DBErrors::TOO_NEW) {
            return unload_wallet(strprintf(_("Error loading %s: Wallet requires newer version of %s"), walletFile, PACKAGE_NAME));
        }
        else if (nLoadWalletRet == DBErrors::NEED_REWRITE)
        {
            return unload_wallet(strprintf(_("Wallet needed to be rewritten: restart %s to complete"), PACKAGE_NAME));
        }
        else {
            return unload_wallet(strprintf(_("Error loading %s"), walletFile));
        }
    }

    if (gArgs.GetBoolArg("-upgradewallet", fFirstRun))
    {
        int nMaxVersion = gArgs.GetArg("-upgradewallet", 0);
        auto nMinVersion = DEFAULT_USE_HD_WALLET ? FEATURE_LATEST : FEATURE_COMPRPUBKEY;
        if (nMaxVersion == 0) // the -upgradewallet without argument case
        {
            walletInstance->WalletLogPrintf("Performing wallet upgrade to %i\n", nMinVersion);
            nMaxVersion = FEATURE_LATEST;
            walletInstance->SetMinVersion(nMinVersion); // permanently upgrade the wallet immediately
        }
        else
            walletInstance->WalletLogPrintf("Allowing wallet upgrade up to %i\n", nMaxVersion);
        if (nMaxVersion < walletInstance->GetVersion())
        {
            return unload_wallet(_("Cannot downgrade wallet"));
        }
        walletInstance->SetMaxVersion(nMaxVersion);
    }

    if (fFirstRun)
    {
        if ((wallet_creation_flags & WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
            //selective allow to set flags
            walletInstance->SetWalletFlag(WALLET_FLAG_DISABLE_PRIVATE_KEYS);
        } else if (wallet_creation_flags & WALLET_FLAG_BLANK_WALLET) {
            walletInstance->SetWalletFlag(WALLET_FLAG_BLANK_WALLET);
        } else {
            // Create new HD chain
            if (gArgs.GetBoolArg("-usehd", DEFAULT_USE_HD_WALLET) && !walletInstance->IsHDEnabled()) {
                std::string strSeed = gArgs.GetArg("-hdseed", "not hex");

                if (gArgs.IsArgSet("-hdseed") && IsHex(strSeed)) {
                    CHDChain newHdChain;
                    std::vector<unsigned char> vchSeed = ParseHex(strSeed);
                    if (!newHdChain.SetSeed(SecureVector(vchSeed.begin(), vchSeed.end()), true)) {
                        return unload_wallet(strprintf(_("%s failed"), "SetSeed"));
                    }
                    if (!walletInstance->SetHDChainSingle(newHdChain, false)) {
                        return unload_wallet(strprintf(_("%s failed"), "SetHDChainSingle"));
                    }
                    // add default account
                    newHdChain.AddAccount();
                    newHdChain.Debug(__func__);
                } else {
                    if (gArgs.IsArgSet("-hdseed") && !IsHex(strSeed)) {
                        walletInstance->WalletLogPrintf("%s -- Incorrect seed, generating a random mnemonic instead\n", __func__);
                    }
                    SecureString secureMnemonic = gArgs.GetArg("-mnemonic", "").c_str();
                    SecureString secureMnemonicPassphrase = gArgs.GetArg("-mnemonicpassphrase", "").c_str();
                    walletInstance->GenerateNewHDChain(secureMnemonic, secureMnemonicPassphrase);
                }

                // ensure this wallet.dat can only be opened by clients supporting HD
                walletInstance->WalletLogPrintf("Upgrading wallet to HD\n");
                walletInstance->SetMinVersion(FEATURE_HD);

                // clean up
                gArgs.ForceRemoveArg("-hdseed");
                gArgs.ForceRemoveArg("-mnemonic");
                gArgs.ForceRemoveArg("-mnemonicpassphrase");
            }
        } // Otherwise, do not create a new HD chain

        // Top up the keypool
        if (walletInstance->CanGenerateKeys() && !walletInstance->TopUpKeyPool()) {
            return unload_wallet(_("Unable to generate initial keys"));
        }

        auto locked_chain = chain.lock();
        walletInstance->ChainStateFlushed(locked_chain->getTipLocator());

        // Try to create wallet backup right after new wallet was created
        bilingual_str strBackupError;
        if(!walletInstance->AutoBackupWallet("", strBackupError, warnings)) {
            if (!strBackupError.original.empty()) {
                return unload_wallet(strBackupError);
            }
        }
    } else if (wallet_creation_flags & WALLET_FLAG_DISABLE_PRIVATE_KEYS) {
        // Make it impossible to disable private keys after creation
        error = strprintf(_("Error loading %s: Private keys can only be disabled during creation"), walletFile);
        return NULL;
    } else if (walletInstance->IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
        LOCK(walletInstance->cs_KeyStore);
        if (!walletInstance->mapKeys.empty() || !walletInstance->mapCryptedKeys.empty()) {
            warnings.push_back(strprintf(_("Warning: Private keys detected in wallet {%s} with disabled private keys"), walletFile));
        }
    }
    else if (gArgs.IsArgSet("-usehd")) {
        bool useHD = gArgs.GetBoolArg("-usehd", DEFAULT_USE_HD_WALLET);
        if (walletInstance->IsHDEnabled() && !useHD) {
            return unload_wallet(strprintf(_("Error loading %s: You can't disable HD on an already existing HD wallet"),
                                walletInstance->GetName()));
        }
        if (!walletInstance->IsHDEnabled() && useHD) {
            return unload_wallet(strprintf(_("Error loading %s: You can't enable HD on an already existing non-HD wallet"),
                                walletInstance->GetName()));
        }
    }

    // Warn user every time a non-encrypted HD wallet is started
    if (walletInstance->IsHDEnabled() && !walletInstance->IsLocked()) {
        SetMiscWarning(_("Make sure to encrypt your wallet and delete all non-encrypted backups after you have verified that the wallet works!").translated);
    }

    if (gArgs.IsArgSet("-mintxfee")) {
        CAmount n = 0;
        if (!ParseMoney(gArgs.GetArg("-mintxfee", ""), n) || 0 == n) {
            error = AmountErrMsg("mintxfee", gArgs.GetArg("-mintxfee", ""));
            return nullptr;
        }
        if (n > HIGH_TX_FEE_PER_KB) {
            warnings.push_back(AmountHighWarn("-mintxfee") + Untranslated(" ") +
                              _("This is the minimum transaction fee you pay on every transaction."));
        }
        walletInstance->m_min_fee = CFeeRate(n);
    }

    walletInstance->m_allow_fallback_fee = Params().IsTestChain();
    if (gArgs.IsArgSet("-fallbackfee")) {
        CAmount nFeePerK = 0;
        if (!ParseMoney(gArgs.GetArg("-fallbackfee", ""), nFeePerK)) {
            error = strprintf(_("Invalid amount for -fallbackfee=<amount>: '%s'"), gArgs.GetArg("-fallbackfee", ""));
            return nullptr;
        }
        if (nFeePerK > HIGH_TX_FEE_PER_KB) {
            warnings.push_back(AmountHighWarn("-fallbackfee") + Untranslated(" ") +
                              _("This is the transaction fee you may pay when fee estimates are not available."));
        }
        walletInstance->m_fallback_fee = CFeeRate(nFeePerK);
        walletInstance->m_allow_fallback_fee = nFeePerK != 0; //disable fallback fee in case value was set to 0, enable if non-null value
    } else {
        // TODO: Enable fallback fee by default not only for test chains
        walletInstance->m_allow_fallback_fee = true;
    }
    if (gArgs.IsArgSet("-discardfee")) {
        CAmount nFeePerK = 0;
        if (!ParseMoney(gArgs.GetArg("-discardfee", ""), nFeePerK)) {
            error = strprintf(_("Invalid amount for -discardfee=<amount>: '%s'"), gArgs.GetArg("-discardfee", ""));
            return nullptr;
        }
        if (nFeePerK > HIGH_TX_FEE_PER_KB) {
            warnings.push_back(AmountHighWarn("-discardfee") + Untranslated(" ") +
                              _("This is the transaction fee you may discard if change is smaller than dust at this level"));
        }
        walletInstance->m_discard_rate = CFeeRate(nFeePerK);
    }
    if (gArgs.IsArgSet("-paytxfee")) {
        CAmount nFeePerK = 0;
        if (!ParseMoney(gArgs.GetArg("-paytxfee", ""), nFeePerK)) {
            error = AmountErrMsg("paytxfee", gArgs.GetArg("-paytxfee", ""));
            return nullptr;
        }
        if (nFeePerK > HIGH_TX_FEE_PER_KB) {
            warnings.push_back(AmountHighWarn("-paytxfee") + Untranslated(" ") +
                              _("This is the transaction fee you will pay if you send a transaction."));
        }
        walletInstance->m_pay_tx_fee = CFeeRate(nFeePerK, 1000);
        if (walletInstance->m_pay_tx_fee < chain.relayMinFee()) {
            error = strprintf(_("Invalid amount for -paytxfee=<amount>: '%s' (must be at least %s)"),
                gArgs.GetArg("-paytxfee", ""), chain.relayMinFee().ToString());
            return nullptr;
        }
    }

    if (gArgs.IsArgSet("-maxtxfee")) {
        CAmount nMaxFee = 0;
        if (!ParseMoney(gArgs.GetArg("-maxtxfee", ""), nMaxFee)) {
            error = AmountErrMsg("maxtxfee", gArgs.GetArg("-maxtxfee", ""));
            return nullptr;
        }
        if (nMaxFee > HIGH_MAX_TX_FEE) {
            warnings.push_back(_("-maxtxfee is set very high! Fees this large could be paid on a single transaction."));
        }
        if (CFeeRate(nMaxFee, 1000) < chain.relayMinFee()) {
            error = strprintf(_("Invalid amount for -maxtxfee=<amount>: '%s' (must be at least the minrelay fee of %s to prevent stuck transactions)"),
                gArgs.GetArg("-maxtxfee", ""), chain.relayMinFee().ToString());
            return nullptr;
        }
        walletInstance->m_default_max_tx_fee = nMaxFee;
    }

    if (chain.relayMinFee().GetFeePerK() > HIGH_TX_FEE_PER_KB)
        warnings.push_back(AmountHighWarn("-minrelaytxfee") + Untranslated(" ") +
                    _("The wallet will avoid paying less than the minimum relay fee."));

    walletInstance->m_confirm_target = gArgs.GetArg("-txconfirmtarget", DEFAULT_TX_CONFIRM_TARGET);
    walletInstance->m_spend_zero_conf_change = gArgs.GetBoolArg("-spendzeroconfchange", DEFAULT_SPEND_ZEROCONF_CHANGE);

    walletInstance->WalletLogPrintf("Wallet completed loading in %15dms\n", GetTimeMillis() - nStart);

    // Try to top up keypool. No-op if the wallet is locked.
    walletInstance->TopUpKeyPool();

    auto locked_chain = chain.lock();
    LOCK(walletInstance->cs_wallet);

    int rescan_height = 0;
    if (!gArgs.GetBoolArg("-rescan", false))
    {
        WalletBatch batch(*walletInstance->database);
        CBlockLocator locator;
        if (batch.ReadBestBlock(locator)) {
            if (const Optional<int> fork_height = locked_chain->findLocatorFork(locator)) {
                rescan_height = *fork_height;
            }
        }
    }

    const Optional<int> tip_height = locked_chain->getHeight();
    if (tip_height) {
        walletInstance->m_last_block_processed = locked_chain->getBlockHash(*tip_height);
    } else {
        walletInstance->m_last_block_processed.SetNull();
    }

    if (tip_height && *tip_height != rescan_height)
    {
        // We can't rescan beyond non-pruned blocks, stop and throw an error.
        // This might happen if a user uses an old wallet within a pruned node
        // or if they ran -disablewallet for a longer time, then decided to re-enable
        if (chain.havePruned()) {
            // Exit early and print an error.
            // If a block is pruned after this check, we will load the wallet,
            // but fail the rescan with a generic error.
            int block_height = *tip_height;
            while (block_height > 0 && locked_chain->haveBlockOnDisk(block_height - 1) && rescan_height != block_height) {
                --block_height;
            }

            if (rescan_height != block_height) {
                return unload_wallet(_("Prune: last wallet synchronisation goes beyond pruned data. You need to -reindex (download the whole blockchain again in case of pruned node)"));
            }
        }

        chain.initMessage(_("Rescanning...").translated);
        walletInstance->WalletLogPrintf("Rescanning last %i blocks (from block %i)...\n", *tip_height - rescan_height, rescan_height);

        // No need to read and scan block if block was created before
        // our wallet birthday (as adjusted for block time variability)
        // unless a full rescan was requested
        if (gArgs.GetArg("-rescan", 0) != 2) {
            if (walletInstance->nTimeFirstKey) {
                if (Optional<int> first_block = locked_chain->findFirstBlockWithTimeAndHeight(walletInstance->nTimeFirstKey - TIMESTAMP_WINDOW, rescan_height, nullptr)) {
                    rescan_height = *first_block;
                }
            }
        }

        {
            WalletRescanReserver reserver(walletInstance.get());
            if (!reserver.reserve() || (ScanResult::SUCCESS != walletInstance->ScanForWalletTransactions(locked_chain->getBlockHash(rescan_height), {} /* stop block */, reserver, true /* update */).status)) {
                return unload_wallet(_("Failed to rescan the wallet during initialization"));
            }
        }
        walletInstance->ChainStateFlushed(locked_chain->getTipLocator());
        walletInstance->database->IncrementUpdateCounter();

        // Restore wallet transaction metadata after -zapwallettxes=1
        if (gArgs.GetBoolArg("-zapwallettxes", false) && gArgs.GetArg("-zapwallettxes", "1") != "2")
        {
            WalletBatch batch(*walletInstance->database);

            for (const CWalletTx& wtxOld : vWtx)
            {
                uint256 hash = wtxOld.GetHash();
                std::map<uint256, CWalletTx>::iterator mi = walletInstance->mapWallet.find(hash);
                if (mi != walletInstance->mapWallet.end())
                {
                    const CWalletTx* copyFrom = &wtxOld;
                    CWalletTx* copyTo = &mi->second;
                    copyTo->mapValue = copyFrom->mapValue;
                    copyTo->vOrderForm = copyFrom->vOrderForm;
                    copyTo->nTimeReceived = copyFrom->nTimeReceived;
                    copyTo->nTimeSmart = copyFrom->nTimeSmart;
                    copyTo->fFromMe = copyFrom->fFromMe;
                    copyTo->nOrderPos = copyFrom->nOrderPos;
                    batch.WriteTx(*copyTo);
                }
            }
        }
    }

    {
        LOCK(cs_wallets);
        for (auto& load_wallet : g_load_wallet_fns) {
            load_wallet(interfaces::MakeWallet(walletInstance));
        }
    }

    // Register with the validation interface. It's ok to do this after rescan since we're still holding locked_chain.
    walletInstance->handleNotifications();

    walletInstance->SetBroadcastTransactions(gArgs.GetBoolArg("-walletbroadcast", DEFAULT_WALLETBROADCAST));

    {
        walletInstance->WalletLogPrintf("setExternalKeyPool.size() = %u\n",   walletInstance->KeypoolCountExternalKeys());
        walletInstance->WalletLogPrintf("setInternalKeyPool.size() = %u\n",   walletInstance->KeypoolCountInternalKeys());
        walletInstance->WalletLogPrintf("mapWallet.size() = %u\n",            walletInstance->mapWallet.size());
        walletInstance->WalletLogPrintf("mapAddressBook.size() = %u\n",       walletInstance->mapAddressBook.size());
        walletInstance->WalletLogPrintf("nTimeFirstKey = %u\n",               walletInstance->nTimeFirstKey);
    }

    return walletInstance;
}

void CWallet::handleNotifications()
{
    m_chain_notifications_handler = m_chain.handleNotifications(*this);
}

void CWallet::postInitProcess()
{
    auto locked_chain = chain().lock();
    LOCK(cs_wallet);

    // Add wallet transactions that aren't already in a block to mempool
    // Do this here as mempool requires genesis block to be loaded
    ReacceptWalletTransactions(*locked_chain);

    // Update wallet transactions with current mempool transactions.
    chain().requestMempoolTransactions(*this);
}

bool CWallet::InitAutoBackup()
{
    if (gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET))
        return true;

    nWalletBackups = gArgs.GetArg("-createwalletbackups", 10);
    nWalletBackups = std::max(0, std::min(10, nWalletBackups));

    return true;
}

bool CWallet::BackupWallet(const std::string& strDest)
{
    return database->Backup(strDest);
}

// This should be called carefully:
// either supply the actual wallet_path to make a raw copy of wallet.dat or "" to backup current instance via BackupWallet()
bool CWallet::AutoBackupWallet(const fs::path& wallet_path, bilingual_str& error_string, std::vector<bilingual_str>& warnings)
{
    std::string strWalletName = GetName();
    if (strWalletName.empty()) {
        strWalletName = "wallet.dat";
    }

    if (nWalletBackups <= 0) {
        WalletLogPrintf("Automatic wallet backups are disabled!\n");
        return false;
    }

    fs::path backupsDir = GetBackupsDir();
    backupsDir.make_preferred();

    if (!fs::exists(backupsDir))
    {
        // Always create backup folder to not confuse the operating system's file browser
        WalletLogPrintf("Creating backup folder %s\n", backupsDir.string());
        if(!fs::create_directories(backupsDir)) {
            // something is wrong, we shouldn't continue until it's resolved
            error_string = strprintf(_("Wasn't able to create wallet backup folder %s!"), backupsDir.string());
            WalletLogPrintf("%s\n", error_string.translated);
            nWalletBackups = -1;
            return false;
        }
    } else if (!fs::is_directory(backupsDir)) {
        // something is wrong, we shouldn't continue until it's resolved
        error_string = strprintf(_("%s is not a valid backup folder!"), backupsDir.string());
        WalletLogPrintf("%s\n", error_string.translated);
        nWalletBackups = -1;
        return false;
    }

    // Create backup of the ...
    struct tm ts;
    time_t time_val = GetTime();
#ifdef HAVE_GMTIME_R
    gmtime_r(&time_val, &ts);
#else
    gmtime_s(&ts, &time_val);
#endif
    std::string dateTimeStr = strprintf(".%04i-%02i-%02i-%02i-%02i",
            ts.tm_year + 1900, ts.tm_mon + 1, ts.tm_mday, ts.tm_hour, ts.tm_min);

    if (wallet_path.empty()) {
        // ... opened wallet
        LOCK2(cs_main, cs_wallet);
        fs::path backupFile = backupsDir / (strWalletName + dateTimeStr);
        backupFile.make_preferred();
        if (!BackupWallet(backupFile.string())) {
            warnings.push_back(strprintf(_("Failed to create backup %s!"), backupFile.string()));
            WalletLogPrintf("%s\n", Join(warnings, Untranslated("\n")).original);
            nWalletBackups = -1;
            return false;
        }

        // Update nKeysLeftSinceAutoBackup using current external keypool size
        nKeysLeftSinceAutoBackup = KeypoolCountExternalKeys();
        WalletLogPrintf("nKeysLeftSinceAutoBackup: %d\n", nKeysLeftSinceAutoBackup);
        if (IsLocked(true)) {
            warnings.push_back(_("Wallet is locked, can't replenish keypool! Automatic backups and mixing are disabled, please unlock your wallet to replenish keypool."));
            WalletLogPrintf("%s\n", Join(warnings, Untranslated("\n")).original);
            nWalletBackups = -2;
            return false;
        }
    } else {
        // ... strWalletName file
        std::string strSourceFile;
        std::shared_ptr<BerkeleyEnvironment> env = GetWalletEnv(wallet_path, strSourceFile);
        fs::path sourceFile = env->Directory() / strSourceFile;
        fs::path backupFile = backupsDir / (strWalletName + dateTimeStr);
        sourceFile.make_preferred();
        backupFile.make_preferred();
        if (fs::exists(backupFile))
        {
            warnings.push_back(_("Failed to create backup, file already exists! This could happen if you restarted wallet in less than 60 seconds. You can continue if you are ok with this."));
            WalletLogPrintf("%s\n", Join(warnings, Untranslated("\n")).original);
            return false;
        }
        if(fs::exists(sourceFile)) {
            try {
                fs::copy_file(sourceFile, backupFile);
                WalletLogPrintf("Creating backup of %s -> %s\n", sourceFile.string(), backupFile.string());
            } catch(fs::filesystem_error &error) {
                warnings.push_back(strprintf(_("Failed to create backup, error: %s"), fsbridge::get_filesystem_error_message(error)));
                WalletLogPrintf("%s\n", Join(warnings, Untranslated("\n")).original);
                nWalletBackups = -1;
                return false;
            }
        }
    }

    // Keep only the last 10 backups, including the new one of course
    typedef std::multimap<std::time_t, fs::path> folder_set_t;
    folder_set_t folder_set;
    fs::directory_iterator end_iter;
    // Build map of backup files for current(!) wallet sorted by last write time
    fs::path currentFile;
    for (fs::directory_iterator dir_iter(backupsDir); dir_iter != end_iter; ++dir_iter)
    {
        // Only check regular files
        if ( fs::is_regular_file(dir_iter->status()))
        {
            currentFile = dir_iter->path().filename();
            // Only add the backups for the current wallet, e.g. wallet.dat.*
            if (dir_iter->path().stem().string() == strWalletName) {
                folder_set.insert(folder_set_t::value_type(fs::last_write_time(dir_iter->path()), *dir_iter));
            }
        }
    }

    // Loop backward through backup files and keep the N newest ones (1 <= N <= 10)
    int counter = 0;
    for(auto it = folder_set.rbegin(); it != folder_set.rend(); ++it) {
        std::pair<const std::time_t, fs::path> file = *it;
        counter++;
        if (counter > nWalletBackups)
        {
            // More than nWalletBackups backups: delete oldest one(s)
            try {
                fs::remove(file.second);
                WalletLogPrintf("Old backup deleted: %s\n", file.second);
            } catch(fs::filesystem_error &error) {
                warnings.push_back(strprintf(_("Failed to delete backup, error: %s"), fsbridge::get_filesystem_error_message(error)));
                WalletLogPrintf("%s\n", Join(warnings, Untranslated("\n")).original);
                return false;
            }
        }
    }

    return true;
}

void CWallet::NotifyTransactionLock(const CTransactionRef &tx, const std::shared_ptr<const llmq::CInstantSendLock>& islock)
{
    LOCK(cs_wallet);
    // Only notify UI if this transaction is in this wallet
    uint256 txHash = tx->GetHash();
    std::map<uint256, CWalletTx>::const_iterator mi = mapWallet.find(txHash);
    if (mi != mapWallet.end()){
        NotifyTransactionChanged(this, txHash, CT_UPDATED);
        NotifyISLockReceived();
        // notify an external script
        std::string strCmd = gArgs.GetArg("-instantsendnotify", "");
        if (!strCmd.empty()) {
            boost::replace_all(strCmd, "%s", txHash.GetHex());
            std::thread t(runCommand, strCmd);
            t.detach(); // thread runs free
        }
    }
}

void CWallet::NotifyChainLock(const CBlockIndex* pindexChainLock, const std::shared_ptr<const llmq::CChainLockSig>& clsig)
{
    NotifyChainLockReceived(pindexChainLock->nHeight);
}

bool CWallet::LoadGovernanceObject(const CGovernanceObject& obj)
{
    AssertLockHeld(cs_wallet);
    return m_gobjects.emplace(obj.GetHash(), obj).second;
}

bool CWallet::WriteGovernanceObject(const CGovernanceObject& obj)
{
    AssertLockHeld(cs_wallet);
    WalletBatch batch(*database);
    return batch.WriteGovernanceObject(obj) && LoadGovernanceObject(obj);
}

std::vector<const CGovernanceObject*> CWallet::GetGovernanceObjects()
{
    AssertLockHeld(cs_wallet);
    std::vector<const CGovernanceObject*> vecObjects;
    vecObjects.reserve(m_gobjects.size());
    for (auto& obj : m_gobjects) {
        vecObjects.push_back(&obj.second);
    }
    return vecObjects;
}

CKeyPool::CKeyPool()
{
    nTime = GetTime();
    fInternal = false;
}

CKeyPool::CKeyPool(const CPubKey& vchPubKeyIn, bool fInternalIn)
{
    nTime = GetTime();
    vchPubKey = vchPubKeyIn;
    fInternal = fInternalIn;
}

// TODO: Back compatibility for PirateCash wallet (need remove after fork 1f61b27399c815ea89ebc7b379283921815a800c )
CWalletKey::CWalletKey(int64_t nExpires)
{
    nTimeCreated = (nExpires ? GetTime() : 0);
    nTimeExpires = nExpires;
}

void CWalletTx::SetMerkleBranch(const uint256& block_hash, int posInBlock)
{
    // Update the tx's hashBlock
    hashBlock = block_hash;

    // set the position of the transaction in the block
    nIndex = posInBlock;
}

int CWalletTx::GetDepthInMainChain(interfaces::Chain::Lock& locked_chain) const
{
    if (hashUnset())
        return 0;

    return locked_chain.getBlockDepth(hashBlock) * (nIndex == -1 ? -1 : 1);
}

bool CWalletTx::IsLockedByInstantSend() const
{
    if (fIsChainlocked) {
        fIsInstantSendLocked = false;
    } else if (!fIsInstantSendLocked) {
        fIsInstantSendLocked = llmq::quorumInstantSendManager->IsLocked(GetHash());
    }
    return fIsInstantSendLocked;
}

bool CWalletTx::IsChainLocked() const
{
    if (!fIsChainlocked) {
        AssertLockHeld(cs_main);
        CBlockIndex* pIndex = LookupBlockIndex(hashBlock);
        if (pIndex != nullptr) {
            fIsChainlocked = llmq::chainLocksHandler->HasChainLock(pIndex->nHeight, hashBlock);
        }
    }
    return fIsChainlocked;
}

int CWalletTx::GetBlocksToMaturity(interfaces::Chain::Lock& locked_chain) const
{
    if (!(IsCoinBase() || IsCoinStake()))
        return 0;
    return std::max(0, (COINBASE_MATURITY+1) - GetDepthInMainChain(locked_chain));
}

bool CWalletTx::IsImmatureCoinBase(interfaces::Chain::Lock& locked_chain) const
{
    // note GetBlocksToMaturity is 0 for non-coinbase tx
    return GetBlocksToMaturity(locked_chain) > 0;
}

std::vector<OutputGroup> CWallet::GroupOutputs(const std::vector<COutput>& outputs, bool single_coin) const {
    std::vector<OutputGroup> groups;
    std::map<CTxDestination, OutputGroup> gmap;
    CTxDestination dst;
    for (const auto& output : outputs) {
        if (output.fSpendable) {
            CInputCoin input_coin = output.GetInputCoin();

            size_t ancestors, descendants;
            chain().getTransactionAncestry(output.tx->GetHash(), ancestors, descendants);
            if (!single_coin && ExtractDestination(output.tx->tx->vout[output.i].scriptPubKey, dst)) {
                // Limit output groups to no more than 10 entries, to protect
                // against inadvertently creating a too-large transaction
                // when using -avoidpartialspends
                if (gmap[dst].m_outputs.size() >= OUTPUT_GROUP_MAX_ENTRIES) {
                    groups.push_back(gmap[dst]);
                    gmap.erase(dst);
                }
                gmap[dst].Insert(input_coin, output.nDepth, output.tx->IsFromMe(ISMINE_ALL), ancestors, descendants);
            } else {
                groups.emplace_back(input_coin, output.nDepth, output.tx->IsFromMe(ISMINE_ALL), ancestors, descendants);
            }
        }
    }
    if (!single_coin) {
        for (const auto& it : gmap) groups.push_back(it.second);
    }
    return groups;
}

bool CWallet::GetKeyOrigin(const CKeyID& keyID, KeyOriginInfo& info) const {
    CKeyMetadata meta;
    {
        LOCK(cs_wallet);
        auto it = mapKeyMetadata.find(keyID);
        if (it != mapKeyMetadata.end()) {
            meta = it->second;
        }
    }
    if (meta.has_key_origin) {
        std::copy(meta.key_origin.fingerprint, meta.key_origin.fingerprint + 4, info.fingerprint);
        info.path = meta.key_origin.path;
    } else { // Single pubkeys get the master fingerprint of themselves
        std::copy(keyID.begin(), keyID.begin() + 4, info.fingerprint);
    }
    return true;
}

bool CWallet::AddKeyOrigin(const CPubKey& pubkey, const KeyOriginInfo& info)
{
    LOCK(cs_wallet);
    std::copy(info.fingerprint, info.fingerprint + 4, mapKeyMetadata[pubkey.GetID()].key_origin.fingerprint);
    mapKeyMetadata[pubkey.GetID()].key_origin.path = info.path;
    mapKeyMetadata[pubkey.GetID()].has_key_origin = true;
    return WriteKeyMetadata(mapKeyMetadata[pubkey.GetID()], pubkey, true);
}
