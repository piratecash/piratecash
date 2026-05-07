// Copyright (c) 2016-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <clientversion.h>
#include <fs.h>
#include <key_io.h>
#include <streams.h>
#include <util/translation.h>
#include <util/system.h>
#include <wallet/db.h>
#include <wallet/salvage.h>
#include <wallet/wallet.h>
#include <wallet/walletdb.h>
#include <wallet/walletutil.h>

#include <ctime>

namespace WalletTool {

// The standard wallet deleter function blocks on the validation interface
// queue, which doesn't exist for the piratecash-wallet. Define our own
// deleter here.
static void WalletToolReleaseWallet(CWallet* wallet)
{
    wallet->WalletLogPrintf("Releasing wallet\n");
    wallet->Close();
    delete wallet;
}

static void WalletCreate(CWallet* wallet_instance)
{
    wallet_instance->SetMinVersion(FEATURE_COMPRPUBKEY);

    // generate a new HD seed
    // NOTE: we do not yet create HD wallets by default
    // auto spk_man = wallet_instance->GetLegacyScriptPubKeyMan();
    // spk_man->GenerateNewHDChain("", "");

    tfm::format(std::cout, "Topping up keypool...\n");
    wallet_instance->TopUpKeyPool();
}

static std::shared_ptr<CWallet> MakeWallet(const std::string& name, const fs::path& path, bool create)
{
    DatabaseOptions options;
    DatabaseStatus status;
    if (create) {
        options.require_create = true;
    } else {
        options.require_existing = true;
    }
    bilingual_str error;
    std::unique_ptr<WalletDatabase> database = MakeDatabase(path, options, status, error);
    if (!database) {
        tfm::format(std::cerr, "%s\n", error.original);
        return nullptr;
    }

    // dummy chain interface
    std::shared_ptr<CWallet> wallet_instance{new CWallet(nullptr /* chain */, name, std::move(database)), WalletToolReleaseWallet};
    DBErrors load_wallet_ret;
    try {
        bool first_run;
        load_wallet_ret = wallet_instance->LoadWallet(first_run);
    } catch (const std::runtime_error&) {
        tfm::format(std::cerr, "Error loading %s. Is wallet being used by another process?\n", name);
        return nullptr;
    }

    if (load_wallet_ret != DBErrors::LOAD_OK) {
        wallet_instance = nullptr;
        if (load_wallet_ret == DBErrors::CORRUPT) {
            tfm::format(std::cerr, "Error loading %s: Wallet corrupted", name);
            return nullptr;
        } else if (load_wallet_ret == DBErrors::NONCRITICAL_ERROR) {
            tfm::format(std::cerr, "Error reading %s! All keys read correctly, but transaction data"
                            " or address book entries might be missing or incorrect.",
                name);
        } else if (load_wallet_ret == DBErrors::TOO_NEW) {
            tfm::format(std::cerr, "Error loading %s: Wallet requires newer version of %s",
                name, PACKAGE_NAME);
            return nullptr;
        } else if (load_wallet_ret == DBErrors::NEED_REWRITE) {
            tfm::format(std::cerr, "Wallet needed to be rewritten: restart %s to complete", PACKAGE_NAME);
            return nullptr;
        } else {
            tfm::format(std::cerr, "Error loading %s", name);
            return nullptr;
        }
    }

    if (create) WalletCreate(wallet_instance.get());

    return wallet_instance;
}

static void WalletShowInfo(CWallet* wallet_instance)
{
    // lock required because of some AssertLockHeld()
    LOCK(wallet_instance->cs_wallet);

    CHDChain hdChainTmp;
    tfm::format(std::cout, "Wallet info\n===========\n");
    tfm::format(std::cout, "Encrypted: %s\n", wallet_instance->IsCrypted() ? "yes" : "no");
    tfm::format(std::cout, "HD (hd seed available): %s\n", wallet_instance->IsHDEnabled() ? "yes" : "no");
    tfm::format(std::cout, "Keypool Size: %u\n", wallet_instance->GetKeyPoolSize());
    tfm::format(std::cout, "Transactions: %zu\n", wallet_instance->mapWallet.size());
    tfm::format(std::cout, "Address Book: %zu\n", wallet_instance->mapAddressBook.size());
}

// ---------------------------------------------------------------------------
// `migrate-v19` -- PirateCash-specific: bring a wallet up to the v19 on-disk
// format. Currently the only sub-step is RewriteReceiveRequestsToV19Format(),
// but additional v19-related migrations can be added here later and chained
// from MigrateToV19().
//
// TODO(remove-in-v20): once all known wallets have been migrated this command
// and the related backward-compat reader in src/qt/walletmodel.h can be
// removed.
// ---------------------------------------------------------------------------

// Migrate "rr" (Qt receive request) destdata records from the old 5-field
// SendCoinsRecipient format (master branch, after BIP70 was removed via
// upstream commit 38e7e164f0 / Bitcoin PR #17165) to the v19+ 7-field format
// that the v19 backport from Dash brought back.
//
// The on-disk record for a receive request is:
//   destdata key:   "rr<id>" (per-address)
//   destdata value: serialized RecentRequestEntry, which embeds SendCoinsRecipient
//
// 5-field layout (master, the broken case):
//   int32 nVersion (RecentRequestEntry)
//   int64 id
//   uint32 date_timet
//   int32 nVersion (SendCoinsRecipient)
//   varstr address_str
//   varstr label_str
//   int64  amount
//   varstr message_str
//
// 7-field layout (v19+):
//   ... same as above ...
//   varstr sPaymentRequest      (always empty in PirateCash -- no BIP70)
//   varstr authenticatedMerchant (always empty in PirateCash -- no BIP70)
//
// Reads each rr value, attempts to parse the 5-field layout. If the stream is
// exhausted right after message_str, appends two empty varstrs and rewrites
// the record. Otherwise the entry is already in the new format and is left
// untouched. Operates on an already-opened wallet under cs_wallet.
static bool RewriteReceiveRequestsToV19Format(CWallet& wallet,
                                              size_t& total, size_t& migrated,
                                              size_t& already_new,
                                              size_t& parse_errors,
                                              size_t& write_errors)
{
    AssertLockHeld(wallet.cs_wallet);

    // Snapshot the rr entries first to avoid mutating the map while iterating.
    struct Entry {
        CTxDestination dest;
        std::string addr_str;
        std::string key;
        std::string value;
    };
    std::vector<Entry> rr_entries;
    for (const auto& [dest, addrbook] : wallet.mapAddressBook) {
        for (const auto& [k, v] : addrbook.destdata) {
            if (k.rfind("rr", 0) != 0) continue; // not a receive request
            rr_entries.push_back({dest, EncodeDestination(dest), k, v});
        }
    }
    total = rr_entries.size();
    tfm::format(std::cout, "migrate-v19: found %u receive-request entries\n",
                (unsigned)total);

    WalletBatch batch(wallet.GetDBHandle());

    for (const auto& e : rr_entries) {
        std::vector<char> data(e.value.begin(), e.value.end());
        CDataStream ss(data, SER_DISK, CLIENT_VERSION);
        try {
            int32_t  outer_version;
            int64_t  id;
            uint32_t date_timet;
            int32_t  inner_version;
            std::string addr_str, label_str, message_str;
            int64_t  amount;
            ss >> outer_version >> id >> date_timet
               >> inner_version >> addr_str >> label_str >> amount >> message_str;

            if (!ss.empty()) {
                // Stream still has bytes after message_str -- record is already
                // in the v19+ 7-field format (or newer). Nothing to do.
                ++already_new;
                continue;
            }

            // Re-serialize with two empty trailing varstrs.
            CDataStream ss_out(SER_DISK, CLIENT_VERSION);
            std::string empty_payment_request, empty_auth_merchant;
            ss_out << outer_version << id << date_timet
                   << inner_version << addr_str << label_str << amount << message_str
                   << empty_payment_request << empty_auth_merchant;

            std::string new_value(ss_out.begin(), ss_out.end());
            if (!batch.WriteDestData(e.addr_str, e.key, new_value)) {
                tfm::format(std::cerr,
                            "migrate-v19: write failed for address=%s key=%s\n",
                            e.addr_str, e.key);
                ++write_errors;
                continue;
            }
            // Keep the in-memory cache in sync with what we just wrote.
            // We can't use CWallet::LoadDestData / AddDestData here because
            // they go through std::map::insert which is a no-op when the key
            // is already present -- and it always is, since the entry was
            // populated when the wallet was loaded. Assign directly so that
            // any subsequent v19 sub-migration step sees the rewritten value.
            wallet.mapAddressBook[e.dest].destdata[e.key] = new_value;
            ++migrated;
            tfm::format(std::cout,
                        "migrate-v19: migrated address=%s key=%s (label=\"%s\")\n",
                        e.addr_str, e.key, label_str);
        } catch (const std::exception& ex) {
            tfm::format(std::cerr,
                        "migrate-v19: parse error at address=%s key=%s: %s\n",
                        e.addr_str, e.key, ex.what());
            ++parse_errors;
        }
    }
    return parse_errors == 0 && write_errors == 0;
}

// Top-level migrator invoked by the `migrate-v19` command. Backs the wallet
// file up to <wallet>.bak.<UTC timestamp> (e.g. wallet.dat.bak.20260502T103022Z)
// BEFORE opening, then chains all v19 sub-migrations.
// Safe to re-run: each run gets its own timestamped backup, and each
// sub-migration is idempotent (entries already in v19 format are skipped).
static bool MigrateToV19(const std::string& name, const fs::path& path)
{
    // 1) Find the actual BDB file backing this wallet name (handles both
    //    "directory containing wallet.dat" and legacy "path is the file").
    fs::path wallet_file;
    if (fs::is_regular_file(path)) {
        wallet_file = path;
    } else if (fs::is_directory(path)) {
        wallet_file = BDBDataFile(path);
    } else {
        tfm::format(std::cerr, "migrate-v19: wallet path does not exist: %s\n", path.string());
        return false;
    }
    if (!fs::exists(wallet_file)) {
        tfm::format(std::cerr, "migrate-v19: wallet file not found: %s\n", wallet_file.string());
        return false;
    }

    // 2) Make a backup BEFORE opening the wallet (BDB will hold it open
    //    afterwards and we don't want to copy a file that's being mutated).
    //    Each run gets its own timestamped backup so the command stays safe
    //    to re-run after a partial or failed previous attempt.
    char ts_buf[32] = {0};
    {
        std::time_t now = std::time(nullptr);
        std::tm tm_utc{};
#if defined(_WIN32)
        gmtime_s(&tm_utc, &now);
#else
        gmtime_r(&now, &tm_utc);
#endif
        std::strftime(ts_buf, sizeof(ts_buf), "%Y%m%dT%H%M%SZ", &tm_utc);
    }
    fs::path backup = wallet_file;
    backup += ".bak.";
    backup += ts_buf;
    // Extremely unlikely (same-second re-run), but disambiguate just in case.
    for (unsigned suffix = 1; fs::exists(backup) && suffix < 1000; ++suffix) {
        backup = wallet_file;
        backup += ".bak.";
        backup += ts_buf;
        backup += "-";
        backup += std::to_string(suffix);
    }
    if (fs::exists(backup)) {
        tfm::format(std::cerr,
                    "migrate-v19: could not pick a free backup filename next to %s\n",
                    wallet_file.string());
        return false;
    }
    try {
        fs::copy_file(wallet_file, backup);
    } catch (const fs::filesystem_error& e) {
        tfm::format(std::cerr, "migrate-v19: failed to create backup %s: %s\n",
                    backup.string(), e.what());
        return false;
    }
    tfm::format(std::cout, "migrate-v19: backup created at %s\n", backup.string());

    // 3) Open the wallet.
    std::shared_ptr<CWallet> wallet_instance = MakeWallet(name, path, /* create= */ false);
    if (!wallet_instance) {
        tfm::format(std::cerr,
                    "migrate-v19: could not open wallet -- backup at %s is intact, "
                    "you can restore from it.\n", backup.string());
        return false;
    }

    size_t total = 0, migrated = 0, already_new = 0, parse_errors = 0, write_errors = 0;
    {
        LOCK(wallet_instance->cs_wallet);
        // Sub-step 1: receive-request format upgrade.
        // Add further v19 sub-migrations here as needed.
        RewriteReceiveRequestsToV19Format(*wallet_instance,
                                          total, migrated, already_new,
                                          parse_errors, write_errors);
    }

    wallet_instance->Close();

    tfm::format(std::cout,
                "migrate-v19: done. rr_entries: total=%u migrated=%u already_new=%u "
                "parse_errors=%u write_errors=%u\n",
                (unsigned)total, (unsigned)migrated, (unsigned)already_new,
                (unsigned)parse_errors, (unsigned)write_errors);
    if (parse_errors == 0 && write_errors == 0) {
        tfm::format(std::cout,
                    "migrate-v19: backup at %s can be removed if everything looks good.\n",
                    backup.string());
        return true;
    }
    tfm::format(std::cerr,
                "migrate-v19: there were errors -- keep the backup at %s.\n",
                backup.string());
    return false;
}

bool ExecuteWalletToolFunc(const std::string& command, const std::string& name)
{
    fs::path path = fs::absolute(name, GetWalletDir());

    if (command == "create") {
        std::shared_ptr<CWallet> wallet_instance = MakeWallet(name, path, /* create= */ true);
        if (wallet_instance) {
            WalletShowInfo(wallet_instance.get());
            wallet_instance->Close();
        }
    } else if (command == "info" || command == "salvage" || command == "wipetxes") {
        if (command == "info") {
            std::shared_ptr<CWallet> wallet_instance = MakeWallet(name, path, /* create= */ false);
            if (!wallet_instance) return false;
            WalletShowInfo(wallet_instance.get());
            wallet_instance->Close();
        } else if (command == "salvage") {
#ifdef USE_BDB
            bilingual_str error;
            std::vector<bilingual_str> warnings;
            bool ret = RecoverDatabaseFile(path, error, warnings);
            if (!ret) {
                for (const auto& warning : warnings) {
                    tfm::format(std::cerr, "%s\n", warning.original);
                }
                if (!error.empty()) {
                    tfm::format(std::cerr, "%s\n", error.original);
                }
            }
            return ret;
#else
            tfm::format(std::cerr, "Salvage command is not available as BDB support is not compiled");
            return false;
#endif
        } else if (command == "wipetxes") {
#ifdef USE_BDB
            std::shared_ptr<CWallet> wallet_instance = MakeWallet(name, path, /* create= */ false);
            if (wallet_instance == nullptr) return false;

            std::vector<uint256> vHash;
            std::vector<uint256> vHashOut;

            LOCK(wallet_instance->cs_wallet);

            for (auto& [txid, _] : wallet_instance->mapWallet) {
                vHash.push_back(txid);
            }

            if (wallet_instance->ZapSelectTx(vHash, vHashOut) != DBErrors::LOAD_OK) {
                tfm::format(std::cerr, "Could not properly delete transactions");
                wallet_instance->Close();
                return false;
            }

            wallet_instance->Close();
            return vHashOut.size() == vHash.size();
#else
            tfm::format(std::cerr, "Wipetxes command is not available as BDB support is not compiled");
            return false;
#endif
        }
    } else if (command == "migrate-v19") {
        // PirateCash-specific: see MigrateToV19 above.
        // TODO(remove-in-v20): drop together with the Qt-side backward-compat reader.
        return MigrateToV19(name, path);
    } else {
        tfm::format(std::cerr, "Invalid command: %s\n", command);
        return false;
    }

    return true;
}
} // namespace WalletTool
