// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2014-2023 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>

#include <chainparamsseeds.h>
#include <consensus/merkle.h>
#include <llmq/params.h>
#include <tinyformat.h>
#include <util/ranges.h>
#include <util/system.h>
#include <util/underlying.h>
#include <versionbitsinfo.h>

#include <arith_uint256.h>

#include <assert.h>

#include <chainparamsseeds.h>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

#include "versionbits.h"

//#define COSANTA_MINE_NEW_GENESIS_BLOCK
#ifdef COSANTA_MINE_NEW_GENESIS_BLOCK

#include <validation.h>

#include <chrono>
#include <iomanip>

struct GenesisMiner
{
    GenesisMiner(CBlock & genesisBlock, std::string networkID)
    {
        using namespace std;

        arith_uint256 bnTarget = arith_uint256().SetCompact(genesisBlock.nBits);

        auto start = std::chrono::system_clock::now();

        genesisBlock.nTime = chrono::seconds(time(NULL)).count();
        int i = 0;
        while (true)
        {
            uint256 powHash = genesisBlock.GetHash();

            if ((++i % 250000) == 0)
            {
                auto end = chrono::system_clock::now();
                auto elapsed = chrono::duration_cast<std::chrono::milliseconds>(end - start);
                cout << i << " hashes in " << elapsed.count() / 1000.0 << " seconds ("
                    << static_cast<double>(i) / static_cast<double>(elapsed.count() / 1000.0) << " hps)" << endl;
            }

            if (UintToArith256(powHash) < bnTarget)
            {
                auto end = chrono::system_clock::now();
                auto elapsed = chrono::duration_cast<std::chrono::milliseconds>(end - start);
                cout << "Mined genesis block for " << networkID << " network: 0x" << genesisBlock.GetHash().ToString() << endl
                    << "target was " << bnTarget.ToString() << " POWHash was 0x" << genesisBlock.GetHash().ToString() << endl
                    << "took " << i << " hashes in " << elapsed.count() / 1000.0 << " seconds ("
                    << static_cast<double>(i) / static_cast<double>(elapsed.count() / 1000.0) << " hps)" << endl << endl
                    << genesisBlock.ToString() << endl;
                exit(0);
            }
            genesisBlock.nNonce++;
        }
    }

};
#endif // COSANTA_MINE_NEW_GENESIS_BLOCK
static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

static CBlock CreateDevNetGenesisBlock(const uint256 &prevBlockHash, const std::string& devNetName, uint32_t nTime, uint32_t nNonce, uint32_t nBits, const CAmount& genesisReward)
{
    assert(!devNetName.empty());

    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    // put height (BIP34) and devnet name into coinbase
    txNew.vin[0].scriptSig = CScript() << 1 << std::vector<unsigned char>(devNetName.begin(), devNetName.end());
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = CScript() << OP_RETURN;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = 4;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock = prevBlockHash;
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 *
 * CBlock(hash=00000ffd590b14, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=e0028e, nTime=1390095618, nBits=1e0ffff0, nNonce=28917698, vtx=1)
 *   CTransaction(hash=e0028e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff001d01044c5957697265642030392f4a616e2f3230313420546865204772616e64204578706572696d656e7420476f6573204c6976653a204f76657273746f636b2e636f6d204973204e6f7720416363657074696e6720426974636f696e73)
 *     CTxOut(nValue=50.00000000, scriptPubKey=0xA9037BAC7050C479B121CF)
 *   vMerkleTree: e0028e
 */
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "Sic parvis magna";
    const CScript genesisOutputScript = CScript() << ParseHex("045b03cb0f02869cfe578880740c00363cc3c58958f737360d1ec5df054a1ad27c801e3a7353333738cf17bd314dd71f8fb8118d7c424d6f69e71017e0d2c3e9e9") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}

static CBlock FindDevNetGenesisBlock(const CBlock &prevBlock, const CAmount& reward)
{
    std::string devNetName = gArgs.GetDevNetName();
    assert(!devNetName.empty());

    CBlock block = CreateDevNetGenesisBlock(prevBlock.GetHash(), devNetName, prevBlock.nTime + 1, 0, prevBlock.nBits, reward);

    arith_uint256 bnTarget;
    bnTarget.SetCompact(block.nBits);

    for (uint32_t nNonce = 0; nNonce < UINT32_MAX; nNonce++) {
        block.nNonce = nNonce;

        uint256 hash = block.GetHash();
        if (UintToArith256(hash) <= bnTarget)
            return block;
    }

    // This is very unlikely to happen as we start the devnet with a very low difficulty. In many cases even the first
    // iteration of the above loop will give a result already
    error("FindDevNetGenesisBlock: could not find devnet genesis block for %s", devNetName);
    assert(false);
}

void CChainParams::AddLLMQ(Consensus::LLMQType llmqType)
{
    assert(!GetLLMQ(llmqType).has_value());
    for (const auto& llmq_param : Consensus::available_llmqs) {
        if (llmq_param.type == llmqType) {
            consensus.llmqs.push_back(llmq_param);
            return;
        }
    }
    error("CChainParams::%s: unknown LLMQ type %d", __func__, static_cast<uint8_t>(llmqType));
    assert(false);
}

std::optional<Consensus::LLMQParams> CChainParams::GetLLMQ(Consensus::LLMQType llmqType) const
{
    for (const auto& llmq_param : consensus.llmqs) {
        if (llmq_param.type == llmqType) {
            return std::make_optional(llmq_param);
        }
    }
    return std::nullopt;
}

/**
 * Main network
 */
class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = CBaseChainParams::MAIN;
        consensus.nSubsidyHalvingInterval = 210240; // Note: actual number of blocks per calendar year with DGW v3 is ~200700 (for example 449750 - 249050)
        consensus.nMasternodePaymentsStartBlock = 525252;
        consensus.nInstantSendConfirmationsRequired = 6;
        consensus.nInstantSendKeepLock = 24;
        consensus.nBudgetPaymentsStartBlock = 700000; // actual historical value
        consensus.nBudgetPaymentsCycleBlocks = 16616; // ~(60*24*30)/2.6, actual number of blocks per month is 200700 / 12 = 16725
        consensus.nBudgetPaymentsWindowBlocks = 100;
        consensus.nSuperblockStartBlock = 710000; // The block at which 12.1 goes live (end of final 12.0 budget cycle)
        consensus.nSuperblockStartHash = uint256(); // do not check this //uint256S("0000000000020cb27c7ef164d21003d5d20cdca2f54dd9a9ca6d45f4d47f8aa3");
        consensus.nSuperblockCycle = 16616; // ~(60*24*30)/2.6, actual number of blocks per month is 200700 / 12 = 16725
        consensus.nSuperblockMaturityWindow = 100;
        consensus.nGovernanceMinQuorum = 10;
        consensus.nGovernanceFilterElements = 20000;
        consensus.nMasternodeMinimumConfirmations = 15;
        consensus.BIP34Height = 76;
        consensus.BIP34Hash = uint256(); //uint256S("0x000001f35e70f7c5705f64c6c5cc3dea9449e74d5b5c7cf74dad1bcca14a8012");
        consensus.BIP65Height = 2431; // 00000000000076d8fcea02ec0963de4abfd01e771fec0863f960c2c64fe6f357
        consensus.BIP66Height = 2075; // 00000000000b1fa2dfa312863570e13fae9ca7b5566cb27e55422620b469aefa
        consensus.DIP0001Height = 5500;
        consensus.DIP0003Height = 7000;
        consensus.DIP0003EnforcementHeight = 7300;
        consensus.DIP0003EnforcementHash = uint256(); //uint256S("000000000000002d1734087b4c5afc3133e4e1c3e1a89218f62bcd9bb3d17f81");
        consensus.DIP0008Height = 300; // 00000000000000112e41e4b3afda8b233b8cc07c532d2eac5de097b68358c43e
        consensus.BRRHeight = 555555; // 000000000000000c5a124f3eccfbe6e17876dca79cec9e63dfa70d269113c926
        consensus.MinBIP9WarningHeight = 1899072 + 2016; // V19 activation height + miner confirmation window
        consensus.powLimit = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~uint256(0) >> 20
        consensus.posLimit = uint256S("000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~uint256(0) >> 24
        consensus.nPowTargetTimespan = 24 * 60 * 60; // Cosanta: 1 day
        consensus.nPowTargetSpacing = 2.5 * 60; // Cosanta: 2.5 minutes
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nPowKGWHeight = 30;
        consensus.nPowDGWHeight = 40;
        consensus.nRuleChangeActivationThreshold = 1916; // 95% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = VERSIONBITS_NUM_BITS - 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1619222400; // April 24, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1672444800; // December 31, 2022

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1619222400; // April 24, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1672444800; // December 31, 2022

        // Deployment of DIP0001
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nStartTime = 1619222400; // April 24, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nTimeout = 1672444800; // December 31, 2022
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nThresholdStart = 50; // 50% of 100

        // Deployment of BIP147
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nStartTime = 1619222400; // April 24, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nTimeout = 1672444800; // December 31, 2022
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nThresholdStart = 50; // 50% of 100

        // Deployment of DIP0003
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].bit = 3;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nStartTime = 1619222400; // April 24, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nTimeout = 1672444800; // December 31, 2022
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nWindowSize = 4032;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nThresholdStart = 3226; // 80% of 4032

        // Deployment of DIP0008
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].bit = 4;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nStartTime = 1619222400; // April 24, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nTimeout = 1672444800; // December 31, 2022
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nThresholdStart = 50; // 50% of 100

        // Deployment of Block Reward Reallocation
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].bit = 5;
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nStartTime = 1664582400; // Oct 1st, 2022
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nTimeout = 1696118400; // Oct 1st, 2023
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nWindowSize = 4032;
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nThresholdStart = 3226; // 80% of 4032
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nThresholdMin = 2420; // 60% of 4032
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nFalloffCoeff = 5; // this corresponds to 10 periods

        // Deployment of DIP0020, DIP0021 and LLMQ_100_67 quorums
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].bit = 6;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nStartTime = 1625097600; // July 1st, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nTimeout = 1656633600; // July 1st, 2022
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nWindowSize = 4032;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nThresholdStart = 3226; // 80% of 4032
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nThresholdMin = 2420; // 60% of 4032
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nFalloffCoeff = 5; // this corresponds to 10 periods

        // Deployment of Quorum Rotation DIP and decreased proposal fee (Values to be determined)
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0024].bit = 7;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0024].nStartTime = 1705276800; // January 15, 2024 12:00:00 AM
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0024].nTimeout = 1736899200;   // January 15, 2025 12:00:00 AM
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0024].nWindowSize = 4032;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0024].nThresholdStart = 3226; // 80% of 4032
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0024].nThresholdMin = 2420;   // 60% of 4032
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0024].nFalloffCoeff = 5;      // this corresponds to 10 periods

        // Deployment of Deployment of Basic BLS, AssetLocks, EHF
        consensus.vDeployments[Consensus::DEPLOYMENT_V19].bit = 8;
        consensus.vDeployments[Consensus::DEPLOYMENT_V19].nStartTime = 1778889600;     // Saturday, May 16, 2026 0:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_V19].nTimeout = 1811030400;       // Sunday, May 23, 2027 0:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_V19].nWindowSize = 4032;
        consensus.vDeployments[Consensus::DEPLOYMENT_V19].nThresholdStart = 3226;       // 80% of 4032
        consensus.vDeployments[Consensus::DEPLOYMENT_V19].nThresholdMin = 2420;         // 60% of 4032
        consensus.vDeployments[Consensus::DEPLOYMENT_V19].nFalloffCoeff = 5;            // this corresponds to 10 periods

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x000000000000000000000000000000000000000000000000df7736a8140e4dd3");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x000000000000000000000000000000000000000000000000000000000000000");

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0x43;
        pchMessageStart[1] = 0x6f;
        pchMessageStart[2] = 0x73;
        pchMessageStart[3] = 0x61;
        nDefaultPort = 60606;
        nDefaultPlatformP2PPort = 26656;
        nDefaultPlatformHTTPPort = 443;
        nPruneAfterHeight = 100000;
        m_assumed_blockchain_size = 1;
        m_assumed_chain_state_size = 1;

        genesis = CreateGenesisBlock(1626442320, 9542573, 0x1e0ffff0, 1, 0 * COIN); //  2021-07-16 13:32:00
        consensus.hashGenesisBlock = genesis.GetHash();

        uint256 expectedGenesisHash = uint256S("0x00000216af2a362c1833a0a608408bcdc69d23b276e47d7510a776e3b0bb1fce");
        uint256 expectedGenesisMerkleRoot = uint256S("0xe16337d6f2cd561e3b9b2c470ec2adc11cf94ba2cda40bddfd2f23deff2499fb");

        #ifdef COSANTA_MINE_NEW_GENESIS_BLOCK
        if (consensus.hashGenesisBlock != expectedGenesisHash)
        {
            GenesisMiner mine(genesis, strNetworkID);
        }
        #endif // COSANTA_MINE_NEW_GENESIS_BLOCK

        assert(consensus.hashGenesisBlock == expectedGenesisHash);
        assert(genesis.hashMerkleRoot == expectedGenesisMerkleRoot);
        // Note that of those which support the service bits prefix, most only support a subset of
        // possible options.
        // This is fine at runtime as we'll fall back to using them as a oneshot if they don't support the
        // service bits we want, but we should get them updated to support all service bits wanted by any
        // release ASAP to avoid it where possible.
        vSeeds.emplace_back("m1.cosa.is");
        vSeeds.emplace_back("m2.cosa.is");
        vSeeds.emplace_back("dns.cosanta.io");
        vSeeds.emplace_back("dns.cosa.is");

        // Cosanta addresses start with 'C'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,28);
        // Cosanta script addresses start with '7'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,13);
        // Cosanta private keys start with '7' or 'X'
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,204);
        // Cosanta BIP32 pubkeys start with 'xpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        // Cosanta BIP32 prvkeys start with 'xprv' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};

        // Type BIP44 coin type is '5'
        nExtCoinType = 770;

        vFixedSeeds = std::vector<SeedSpec6>(std::begin(pnSeed6_main), std::end(pnSeed6_main));

        // long living quorum params
        AddLLMQ(Consensus::LLMQType::LLMQ_50_60);
        AddLLMQ(Consensus::LLMQType::LLMQ_60_75);
        AddLLMQ(Consensus::LLMQType::LLMQ_400_60);
        AddLLMQ(Consensus::LLMQType::LLMQ_400_85);
        AddLLMQ(Consensus::LLMQType::LLMQ_100_67);
        consensus.llmqTypeChainLocks = Consensus::LLMQType::LLMQ_400_60;
        consensus.llmqTypeInstantSend = Consensus::LLMQType::LLMQ_50_60;
        consensus.llmqTypeDIP0024InstantSend = Consensus::LLMQType::LLMQ_60_75;
        consensus.llmqTypePlatform = Consensus::LLMQType::LLMQ_100_67;
        consensus.llmqTypeMnhf = Consensus::LLMQType::LLMQ_400_85;

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fRequireRoutableExternalIP = true;
        m_is_test_chain = false;
        fAllowMultipleAddressesFromGroup = false;
        fAllowMultiplePorts = false;
        nLLMQConnectionRetryTimeout = 60;
        m_is_mockable_chain = false;

        nPoolMinParticipants = 3;
        nPoolMaxParticipants = 20;
        nFulfilledRequestExpireTime = 60*60; // fulfilled requests expire in 1 hour

        vSporkAddresses = {"CPU5ip4oDKkmcSfbZkmeXnMq5aTQUSRtwz"};
        nMinSporkKeys = 1;
        fBIP9CheckMasternodesUpgraded = true;

        nStakeMinAge = 24 * 60 * 60; // 24 hours
        nFirstPoSv2Block = 100000ULL;

        checkpointData = {
            {
                {0, uint256S("0x00000216af2a362c1833a0a608408bcdc69d23b276e47d7510a776e3b0bb1fce")},
                {666, uint256S("0x00000018ea7460307260c065ac63435466b8eb9f433a4ce26b6e1ca56f6c556d")},
                {73400, uint256S("0x00000000826cf1c50b9316838a1449156a008820d48197f2ef0f95fc48906534")},
                {97079, uint256S("0x000000006a99d070477085796509db527a7572a94f06d40e128021acd9e3c4e1")},
                {103066, uint256S("0x000000003a15c5c066a8a1168fa314bcbf8a21295f8a0012052f62687a65f13c")},
                {114444, uint256S("0x0000000006fae477498ba63e8467859fef34ace0593706b1a67de639e83766f2")},
                {136336, uint256S("0x0000000044e0cd6b405929c0f581cccb7d8437c3d6b775e4b730728d5487d984")},
                {161053, uint256S("0x00000000066aba6811bdc9aeec4d56986e617dfa54a4fc0bee49235e1ecaea48")},
                {201010, uint256S("0x0000000003b9bd34fa0047a2b238e9be27bdadd603fe0d0dcf93c8c373c16159")},
                {227510, uint256S("0x0000a1c4fa380e46b9f52ef1793ed8992e4c735a1b0a6fdf8180284ef94676ba")},
                {329066, uint256S("0x0000d4f1c5d27c25201f01d9f335c2a5e4345258941cb785ff302ee935cd995c")},
                {341606, uint256S("0x000084bcb3ca5c12fee6183b068ed37775ca81ef4cc502bab41df2059de857ed")},
                {478500, uint256S("0x00001bca3db981f1dca7ff21371260b84ef3a2e2b5259e60c4bd5a538860f673")},
                {606060, uint256S("0x00007da0a68306dc32e1c096eccbbf818574ff6445f5966d11506ee87a7d82ce")},
                {876000, uint256S("0x000091c5d4a90042d58097a0e06239a6fa273b02fdd1b7ccd809005d3e3aeace")},
                {929600, uint256S("0xecf2d7392585b41c5acc81548fe3593410c500d676718f5161e9b8a246cbe3ba")},
                {958666, uint256S("0x80caa466ea0cfb71168d58c368e53f455dcabeadbc2ceebdad071f42152ee845")},
            }
        };

        // getchaintxstats 17280 00000000000000261bdbe99c01fcba992e577efa6cc41aae564b8ca9f112b2a3
        chainTxData = ChainTxData{
            1626636432, // * UNIX timestamp of last known number of transactions (Block 689)
            704,   // * total number of transactions between genesis and that timestamp
                        //   (the tx=... number in the ChainStateFlushed debug.log lines)
            0.2         // * estimated number of transactions per second after that timestamp
        };
    }
};

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = CBaseChainParams::TESTNET;
        consensus.nSubsidyHalvingInterval = 210240;
        consensus.nMasternodePaymentsStartBlock = 488888;
        consensus.nInstantSendConfirmationsRequired = 2;
        consensus.nInstantSendKeepLock = 6;
        consensus.nBudgetPaymentsStartBlock = 4100;
        consensus.nBudgetPaymentsCycleBlocks = 50;
        consensus.nBudgetPaymentsWindowBlocks = 10;
        consensus.nSuperblockStartBlock = 4200; // NOTE: Should satisfy nSuperblockStartBlock > nBudgetPeymentsStartBlock
        consensus.nSuperblockStartHash = uint256(); // do not check this on testnet
        consensus.nSuperblockCycle = 24; // Superblocks can be issued hourly on testnet
        consensus.nSuperblockMaturityWindow = 24; // This is equal to SB cycle on testnet
        consensus.nGovernanceMinQuorum = 1;
        consensus.nGovernanceFilterElements = 500;
        consensus.nMasternodeMinimumConfirmations = 1;
        consensus.BIP34Height = 76;
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 2431; // 00000073c09c20fe2ad2b2d17fb71a87b9f97d8dfee9bc75a51c8cf7c3b04127
        consensus.BIP66Height = 2075; // 00000071708936a4853ff8d2da260c9b3dc23ed99f72324ddf4a20c320bb4b68
        consensus.DIP0001Height = 5500;
        consensus.DIP0003Height = 7000;
        consensus.DIP0003EnforcementHeight = 7300;
        consensus.DIP0003EnforcementHash = uint256();
        consensus.DIP0008Height = 300; // 000000000e9329d964d80e7dab2e704b43b6bd2b91fea1e9315d38932e55fb55
        consensus.BRRHeight = 488999; // 0000001537dbfd09dea69f61c1f8b2afa27c8dc91c934e144797761c9f10367b
        consensus.MinBIP9WarningHeight = 850100 + 2016;  // v19 activation height + miner confirmation window
        consensus.powLimit = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~uint256(0) >> 20
        consensus.posLimit = uint256S("0fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~uint256(0) >> 4
        consensus.nPowTargetTimespan = 24 * 60 * 60; // Cosanta: 1 day
        consensus.nPowTargetSpacing = 2.5 * 60; // Cosanta: 2.5 minutes
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nPowKGWHeight = 30; // nPowKGWHeight >= nPowDGWHeight means "no KGW"
        consensus.nPowDGWHeight = 40; // TODO: make sure to drop all spork6 related code on next testnet reset
        consensus.nRuleChangeActivationThreshold = 1512; // 75% for testchains
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = VERSIONBITS_NUM_BITS - 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1619222400; // April 24, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1672444800; // December 31, 2022

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1618221600; // April 12, 2021 10am
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1672444800; // December 31, 2022

        // Deployment of DIP0001
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nStartTime = 1544655600; // Dec 13th, 2018
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nTimeout = 1635903900; // Nov 3rd, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nThresholdStart = 50; // 50% of 100

        // Deployment of BIP147
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nStartTime = 1619222400; // April 24, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nTimeout = 1672444800; // December 31, 2022
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nThresholdStart = 50; // 50% of 100

        // Deployment of DIP0003
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].bit = 3;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nStartTime = 1619222400; // April 24, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nTimeout = 1672444800; // December 31, 2022
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nThresholdStart = 50; // 50% of 100

        // Deployment of DIP0008
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].bit = 4;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nStartTime = 1619222400; // April 24, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nTimeout = 1672444800; // December 31, 2022
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nThresholdStart = 50; // 50% of 100

        // Deployment of Block Reward Reallocation
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].bit = 5;
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nStartTime = 1664582400; // Oct 1st, 2022
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nTimeout = 1696118400; // Oct 1st, 2023
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nThresholdStart = 80; // 80% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nThresholdMin = 60; // 60% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nFalloffCoeff = 5; // this corresponds to 10 periods

        // Deployment of DIP0020, DIP0021 and LLMQ_100_67 quorums
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].bit = 6;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nStartTime = 1625097600; // July 1st, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nTimeout = 1733011200; // December 1st, 2024
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nThresholdStart = 80; // 80% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nThresholdMin = 60; // 60% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nFalloffCoeff = 5; // this corresponds to 10 periods

        // Deployment of Quorum Rotation DIP and decreased proposal fee
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0024].bit = 7;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0024].nStartTime = 1649980800; // Friday, April 15, 2022 0:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0024].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0024].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0024].nThresholdStart = 80; // 80% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0024].nThresholdMin = 60;   // 60% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0024].nFalloffCoeff = 5;      // this corresponds to 10 periods

        // Deployment of Deployment of Basic BLS, AssetLocks, EHF
        consensus.vDeployments[Consensus::DEPLOYMENT_V19].bit = 8;
        consensus.vDeployments[Consensus::DEPLOYMENT_V19].nStartTime = 1776211200;     // Wednesday, April 15, 2026 0:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_V19].nTimeout = 1808352000;       // Thursday, April 22, 2027 0:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_V19].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_V19].nThresholdStart = 80;         // 80% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_V19].nThresholdMin = 60;           // 60% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_V19].nFalloffCoeff = 5;            // this corresponds to 10 periods

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x000000000000000000000000000000000000000000000000083c9c7361558f43");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x000000000000000000000000000000000000000000000000000000000000000");

        pchMessageStart[0] = 0x43;
        pchMessageStart[1] = 0x6f;
        pchMessageStart[2] = 0x73;
        pchMessageStart[3] = 0x54;
        nDefaultPort = 60696;
        nDefaultPlatformP2PPort = 22000;
        nDefaultPlatformHTTPPort = 22001;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 1;
        m_assumed_chain_state_size = 1;

        genesis = CreateGenesisBlock(1618221600, 3068881, 0x1e0ffff0, 1, 0 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();

        uint256 expectedGenesisHash = uint256S("0x000004ba6ec1309022987a66c17fe66b550396bd9710463335ad59de8bfe2c02");
        uint256 expectedGenesisMerkleRoot = uint256S("e16337d6f2cd561e3b9b2c470ec2adc11cf94ba2cda40bddfd2f23deff2499fb");

        #ifdef COSANTA_MINE_NEW_GENESIS_BLOCK
        if (consensus.hashGenesisBlock != expectedGenesisHash)
        {
            GenesisMiner mine(genesis, strNetworkID);
        }
        #endif // COSANTA_MINE_NEW_GENESIS_BLOCK

        assert(consensus.hashGenesisBlock == expectedGenesisHash);
        assert(genesis.hashMerkleRoot == expectedGenesisMerkleRoot);

        vFixedSeeds.clear();
        vFixedSeeds = std::vector<SeedSpec6>(std::begin(pnSeed6_test), std::end(pnSeed6_test));

        vSeeds.clear();
        // nodes with support for servicebits filtering should be at the top
        vSeeds.emplace_back("m1.cosa.is");
        vSeeds.emplace_back("m2.cosa.is");

        // Testnet Cosanta addresses start with 'c'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,88);
        // Testnet Cosanta script addresses start with '8' or '9'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,19);
        // Testnet Cosanta keys start with '9' or 'c' (Bitcoin defaults)
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        // Testnet Cosanta BIP32 pubkeys start with 'tpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        // Testnet Cosanta BIP32 prvkeys start with 'tprv' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        // Testnet Cosanta BIP44 coin type is '1' (All coin's testnet default)
        nExtCoinType = 1;

        // long living quorum params
        AddLLMQ(Consensus::LLMQType::LLMQ_50_60);
        AddLLMQ(Consensus::LLMQType::LLMQ_60_75);
        AddLLMQ(Consensus::LLMQType::LLMQ_400_60);
        AddLLMQ(Consensus::LLMQType::LLMQ_400_85);
        AddLLMQ(Consensus::LLMQType::LLMQ_100_67);
        AddLLMQ(Consensus::LLMQType::LLMQ_25_67);
        consensus.llmqTypeChainLocks = Consensus::LLMQType::LLMQ_50_60;
        consensus.llmqTypeInstantSend = Consensus::LLMQType::LLMQ_50_60;
        consensus.llmqTypeDIP0024InstantSend = Consensus::LLMQType::LLMQ_60_75;
        consensus.llmqTypePlatform = Consensus::LLMQType::LLMQ_25_67;
        consensus.llmqTypeMnhf = Consensus::LLMQType::LLMQ_50_60;

        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fRequireRoutableExternalIP = true;
        m_is_test_chain = true;
        fAllowMultipleAddressesFromGroup = false;
        fAllowMultiplePorts = true;
        nLLMQConnectionRetryTimeout = 60;
        m_is_mockable_chain = false;

        nPoolMinParticipants = 2;
        nPoolMaxParticipants = 20;
        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes

        vSporkAddresses = {"certvWMfGR8ahresKNppg8LZf84kmXUiZy"};
        nMinSporkKeys = 1;
        fBIP9CheckMasternodesUpgraded = true;

        nStakeMinAge = 24 * 60 * 60; // 24 hours
        nFirstPoSv2Block = 78200ULL;

        checkpointData = {
            {
                {0, uint256S("0x000004ba6ec1309022987a66c17fe66b550396bd9710463335ad59de8bfe2c02")},
                {256, uint256S("0x00000bf90483dee21e5ec43f44b5065656034c773377c582764de5cd935ee563")},
                {60000, uint256S("0x00000ea72da4e1acd8d957d34c199ebf0d189015efeb9bdd2ff9f22ecbdb64d4")},
                {78200, uint256S("0x000008c7d80b614ac6d3770a80c1482dc8047b56afb899a380f5635a45bf93ea")},
                {94000, uint256S("0x0068a733805059fba4ea3b1b60bbade9401305840a9df89987520e478e6e2465")},
                {147800,uint256S("0x012ca46fb71629895e065c1900eb5c485e8832dfae8b673373ac49b6a6f505d2")},
                {444444,uint256S("0x4ca2d67c258b8bfb767947970f8a5945ca93e982c31cac36c7615c9b4ed6755c")},
                {666666,uint256S("0x08e01c3010fd9a9f6ad7632bbf179775942998c5a6ef945a4d0a2933d8219429")},
            }
        };

        // getchaintxstats 17280 0000005c35514190ef3c38d322f69412553dc7e1107ed5f92adc2935b90acc51
        chainTxData = ChainTxData{
            1626622319, // * UNIX timestamp of last known number of transactions (Block 263)
            264,    	// * total number of transactions between genesis and that timestamp
                        //   (the tx=... number in the ChainStateFlushed debug.log lines)
            0.01        // * estimated number of transactions per second after that timestamp
        };
    }
};

/**
 * Devnet
 */
class CDevNetParams : public CChainParams {
public:
    explicit CDevNetParams(const ArgsManager& args) {
        strNetworkID = CBaseChainParams::DEVNET;
        consensus.nSubsidyHalvingInterval = 210240;
        consensus.nMasternodePaymentsStartBlock = 4010;
        consensus.nInstantSendConfirmationsRequired = 2;
        consensus.nInstantSendKeepLock = 6;
        consensus.nBudgetPaymentsStartBlock = 4100;
        consensus.nBudgetPaymentsCycleBlocks = 50;
        consensus.nBudgetPaymentsWindowBlocks = 10;
        consensus.nSuperblockStartBlock = 4200; // NOTE: Should satisfy nSuperblockStartBlock > nBudgetPeymentsStartBlock
        consensus.nSuperblockStartHash = uint256(); // do not check this on devnet
        consensus.nSuperblockCycle = 24; // Superblocks can be issued hourly on devnet
        consensus.nSuperblockMaturityWindow = 24; // This is equal to SB cycle on devnet
        consensus.nGovernanceMinQuorum = 1;
        consensus.nGovernanceFilterElements = 500;
        consensus.nMasternodeMinimumConfirmations = 1;
        consensus.BIP34Height = 1; // BIP34 activated immediately on devnet
        consensus.BIP65Height = 1; // BIP65 activated immediately on devnet
        consensus.BIP66Height = 1; // BIP66 activated immediately on devnet
        consensus.DIP0001Height = 2; // DIP0001 activated immediately on devnet
        consensus.DIP0003Height = 2; // DIP0003 activated immediately on devnet
        consensus.DIP0003EnforcementHeight = 2; // DIP0003 activated immediately on devnet
        consensus.DIP0003EnforcementHash = uint256();
        consensus.DIP0008Height = 2; // DIP0008 activated immediately on devnet
        consensus.BRRHeight = 300;
        consensus.MinBIP9WarningHeight = 300 + 2016; // v19 activation height + miner confirmation window
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~uint256(0) >> 1
        consensus.posLimit = uint256S("0fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~uint256(0) >> 4
        consensus.nPowTargetTimespan = 24 * 60 * 60; // Cosanta: 1 day
        consensus.nPowTargetSpacing = 1 * 60; // Cosanta: 1 minute
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nPowKGWHeight = 30; // nPowKGWHeight >= nPowDGWHeight means "no KGW"
        consensus.nPowDGWHeight = 40;
        consensus.nRuleChangeActivationThreshold = 1512; // 75% for testchains
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = VERSIONBITS_NUM_BITS - 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1619222400; // April 24, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1672444800; // December 31, 2022

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1619222400; // April 24, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1672444800; // December 31, 2022

        // Deployment of DIP0001
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nStartTime = 1619222400; // April 24, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nTimeout = 1672444800; // December 31, 2022
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nThresholdStart = 50; // 50% of 100

        // Deployment of BIP147
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nStartTime = 1619222400; // April 24, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nTimeout = 1672444800; // December 31, 2022
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nThresholdStart = 50; // 50% of 100

        // Deployment of DIP0003
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].bit = 3;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nStartTime = 1619222400; // April 24, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nTimeout = 1672444800; // December 31, 2022
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nThresholdStart = 50; // 50% of 100

        // Deployment of DIP0008
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].bit = 4;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nStartTime = 1619222400; // April 24, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nTimeout = 1672444800; // December 31, 2022
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nThresholdStart = 50; // 50% of 100

        // Deployment of Block Reward Reallocation
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].bit = 5;
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nStartTime = 1661990400; // Sep 1st, 2022
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nTimeout = 1679529600; // Mar 21st, 2023
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nThresholdStart = 80; // 80% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nThresholdMin = 60; // 60% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nFalloffCoeff = 5; // this corresponds to 10 periods

        // Deployment of DIP0020, DIP0021 and LLMQ_100_67 quorums
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].bit = 6;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nStartTime = 1604188800; // November 1st, 2020
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nTimeout = 1635724800; // November 1st, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nThresholdStart = 80; // 80% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nThresholdMin = 60; // 60% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nFalloffCoeff = 5; // this corresponds to 10 periods

        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0024].bit = 7;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0024].nStartTime = 1625097600; // July 1st, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0024].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0024].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0024].nThresholdStart = 80; // 80% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0024].nThresholdMin = 60;   // 60% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0024].nFalloffCoeff = 5;    // this corresponds to 10 periods

        // Deployment of Deployment of Basic BLS, AssetLocks, EHF
        consensus.vDeployments[Consensus::DEPLOYMENT_V19].bit = 8;
        consensus.vDeployments[Consensus::DEPLOYMENT_V19].nStartTime = 1661990400; // Sep 1st, 2022
        consensus.vDeployments[Consensus::DEPLOYMENT_V19].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_V19].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_V19].nThresholdStart = 80; // 80% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_V19].nThresholdMin = 60;   // 60% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_V19].nFalloffCoeff = 5;     // this corresponds to 10 periods

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x000000000000000000000000000000000000000000000000000000000000000");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x000000000000000000000000000000000000000000000000000000000000000");

        pchMessageStart[0] = 0xe2;
        pchMessageStart[1] = 0xca;
        pchMessageStart[2] = 0xff;
        pchMessageStart[3] = 0xce;
        nDefaultPort = 63636;
        nDefaultPlatformP2PPort = 22100;
        nDefaultPlatformHTTPPort = 22101;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        UpdateDevnetSubsidyAndDiffParametersFromArgs(args);
        genesis = CreateGenesisBlock(1618221600, 98745, 0x207fffff, 1, 0 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();

        uint256 expectedGenesisHash = uint256S("0x5cc74fcae11b83a1f00ca81106deaae119486ded33918571a639e7b6eac83150");
        uint256 expectedGenesisMerkleRoot = uint256S("0xe16337d6f2cd561e3b9b2c470ec2adc11cf94ba2cda40bddfd2f23deff2499fb");

        #ifdef COSANTA_MINE_NEW_GENESIS_BLOCK
        if (consensus.hashGenesisBlock != expectedGenesisHash)
        {
            GenesisMiner mine(genesis, strNetworkID);
        }
        #endif // COSANTA_MINE_NEW_GENESIS_BLOCK

        assert(consensus.hashGenesisBlock == expectedGenesisHash);
        assert(genesis.hashMerkleRoot == expectedGenesisMerkleRoot);

        devnetGenesis = FindDevNetGenesisBlock(genesis, 1 * COIN);
        consensus.hashDevnetGenesisBlock = devnetGenesis.GetHash();

        vFixedSeeds.clear();
        vSeeds.clear();
        //vSeeds.push_back(CDNSSeedData("cosanta.org",  "devnet-seed.cosanta.org"));

        // Testnet Cosanta addresses start with 'o'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,115);
        // Testnet Cosanta script addresses start with '8' or '9'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,19);
        // Testnet private keys start with '9' or 'c' (Bitcoin defaults)
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        // Testnet Cosanta BIP32 pubkeys start with 'tpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        // Testnet Cosanta BIP32 prvkeys start with 'tprv' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        // Testnet Cosanta BIP44 coin type is '1' (All coin's testnet default)
        nExtCoinType = 1;

        // long living quorum params
        AddLLMQ(Consensus::LLMQType::LLMQ_50_60);
        AddLLMQ(Consensus::LLMQType::LLMQ_60_75);
        AddLLMQ(Consensus::LLMQType::LLMQ_400_60);
        AddLLMQ(Consensus::LLMQType::LLMQ_400_85);
        AddLLMQ(Consensus::LLMQType::LLMQ_100_67);
        AddLLMQ(Consensus::LLMQType::LLMQ_DEVNET);
        AddLLMQ(Consensus::LLMQType::LLMQ_DEVNET_DIP0024);
        AddLLMQ(Consensus::LLMQType::LLMQ_DEVNET_PLATFORM);
        consensus.llmqTypeChainLocks = Consensus::LLMQType::LLMQ_50_60;
        consensus.llmqTypeInstantSend = Consensus::LLMQType::LLMQ_50_60;
        consensus.llmqTypeDIP0024InstantSend = Consensus::LLMQType::LLMQ_60_75;
        consensus.llmqTypePlatform = Consensus::LLMQType::LLMQ_100_67;
        consensus.llmqTypeMnhf = Consensus::LLMQType::LLMQ_50_60;

        UpdateDevnetLLMQChainLocksFromArgs(args);
        UpdateDevnetLLMQInstantSendFromArgs(args);
        UpdateDevnetLLMQInstantSendDIP0024FromArgs(args);
        UpdateDevnetLLMQPlatformFromArgs(args);
        UpdateLLMQDevnetParametersFromArgs(args);
        UpdateDevnetPowTargetSpacingFromArgs(args);

        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fRequireRoutableExternalIP = true;
        m_is_test_chain = true;
        fAllowMultipleAddressesFromGroup = true;
        fAllowMultiplePorts = true;
        nLLMQConnectionRetryTimeout = 60;
        m_is_mockable_chain = false;

        nPoolMinParticipants = 2;
        nPoolMaxParticipants = 20;
        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes

        vSporkAddresses = {"oRg7XeNysTv1djHe21x4DNe2kuGCpkwxvC"};
        nMinSporkKeys = 1;
        // devnets are started with no blocks and no MN, so we can't check for upgraded MN (as there are none)
        fBIP9CheckMasternodesUpgraded = false;

        nStakeMinAge = 24 * 60 * 60; // 24 hours
        nFirstPoSv2Block = 78000ULL;

        checkpointData = (CCheckpointData) {
            {
                { 1, devnetGenesis.GetHash() },
            }
        };

        chainTxData = ChainTxData{
            devnetGenesis.GetBlockTime(), // * UNIX timestamp of devnet genesis block
            2,                            // * we only have 2 coinbase transactions when a devnet is started up
            0.01                          // * estimated number of transactions per second
        };
    }

    /**
     * Allows modifying the subsidy and difficulty devnet parameters.
     */
    void UpdateDevnetSubsidyAndDiffParameters(int nMinimumDifficultyBlocks, int nHighSubsidyBlocks, int nHighSubsidyFactor)
    {
        consensus.nMinimumDifficultyBlocks = nMinimumDifficultyBlocks;
        consensus.nHighSubsidyBlocks = nHighSubsidyBlocks;
        consensus.nHighSubsidyFactor = nHighSubsidyFactor;
    }
    void UpdateDevnetSubsidyAndDiffParametersFromArgs(const ArgsManager& args);

    /**
     * Allows modifying the LLMQ type for ChainLocks.
     */
    void UpdateDevnetLLMQChainLocks(Consensus::LLMQType llmqType)
    {
        consensus.llmqTypeChainLocks = llmqType;
    }
    void UpdateDevnetLLMQChainLocksFromArgs(const ArgsManager& args);

    /**
     * Allows modifying the LLMQ type for InstantSend.
     */
    void UpdateDevnetLLMQInstantSend(Consensus::LLMQType llmqType)
    {
        consensus.llmqTypeInstantSend = llmqType;
    }

    /**
     * Allows modifying the LLMQ type for InstantSend (DIP0024).
     */
    void UpdateDevnetLLMQDIP0024InstantSend(Consensus::LLMQType llmqType)
    {
        consensus.llmqTypeDIP0024InstantSend = llmqType;
    }

    /**
     * Allows modifying the LLMQ type for Platform.
     */
    void UpdateDevnetLLMQPlatform(Consensus::LLMQType llmqType)
    {
        consensus.llmqTypePlatform = llmqType;
    }

    /**
     * Allows modifying PowTargetSpacing
     */
    void UpdateDevnetPowTargetSpacing(int64_t nPowTargetSpacing)
    {
        consensus.nPowTargetSpacing = nPowTargetSpacing;
    }

    /**
     * Allows modifying parameters of the devnet LLMQ
     */
    void UpdateLLMQDevnetParameters(int size, int threshold)
    {
        auto params = ranges::find_if(consensus.llmqs, [](const auto& llmq){ return llmq.type == Consensus::LLMQType::LLMQ_DEVNET;});
        assert(params != consensus.llmqs.end());
        params->size = size;
        params->minSize = threshold;
        params->threshold = threshold;
        params->dkgBadVotesThreshold = threshold;
    }
    void UpdateLLMQDevnetParametersFromArgs(const ArgsManager& args);
    void UpdateDevnetLLMQInstantSendFromArgs(const ArgsManager& args);
    void UpdateDevnetLLMQInstantSendDIP0024FromArgs(const ArgsManager& args);
    void UpdateDevnetLLMQPlatformFromArgs(const ArgsManager& args);
    void UpdateDevnetPowTargetSpacingFromArgs(const ArgsManager& args);
};

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    explicit CRegTestParams(const ArgsManager& args) {
        strNetworkID =  CBaseChainParams::REGTEST;
        consensus.nSubsidyHalvingInterval = 150;
        consensus.nMasternodePaymentsStartBlock = 240;
        consensus.nInstantSendConfirmationsRequired = 2;
        consensus.nInstantSendKeepLock = 6;
        consensus.nBudgetPaymentsStartBlock = 1000;
        consensus.nBudgetPaymentsCycleBlocks = 50;
        consensus.nBudgetPaymentsWindowBlocks = 10;
        consensus.nSuperblockStartBlock = 1500;
        consensus.nSuperblockStartHash = uint256(); // do not check this on regtest
        consensus.nSuperblockCycle = 10;
        consensus.nSuperblockMaturityWindow = 10; // This is equal to SB cycle on regtest
        consensus.nGovernanceMinQuorum = 1;
        consensus.nGovernanceFilterElements = 100;
        consensus.nMasternodeMinimumConfirmations = 1;
        consensus.BIP34Height = 100; // BIP34 has not activated on regtest (far in the future so block v1 are not rejected in tests)
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 101; // BIP65 activated on regtest (Used in rpc activation tests)
        consensus.BIP66Height = 102; // BIP66 activated on regtest (Used in rpc activation tests)
        consensus.DIP0001Height = 103;
        consensus.DIP0003Height = 104;
        consensus.DIP0003EnforcementHeight = 105;
        consensus.DIP0003EnforcementHash = uint256();
        consensus.DIP0008Height = 432;
        consensus.BRRHeight = 2500; // see block_reward_reallocation_tests
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~uint256(0) >> 1
        consensus.posLimit = uint256S("0fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~uint256(0) >> 4
        consensus.nPowTargetTimespan = 24 * 60 * 60; // Cosanta: 1 day
        consensus.nPowTargetSpacing = 1 * 60; // Cosanta: 1 minute
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nPowKGWHeight = 30; // same as mainnet
        consensus.nPowDGWHeight = 40; // same as mainnet
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = VERSIONBITS_NUM_BITS - 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].bit = 3;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].bit = 4;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].bit = 5;
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nWindowSize = 500;
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nThresholdStart = 400; // 80%
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nThresholdMin = 300; // 60%
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nFalloffCoeff = 5;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].bit = 6;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nThresholdStart = 80;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nThresholdMin = 60;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nFalloffCoeff = 5;

        // Deployment of Quorum Rotation DIP and decreased proposal fee
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0024].bit = 7;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0024].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0024].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0024].nWindowSize = 300;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0024].nThresholdStart = 240; // 80% of 300
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0024].nThresholdMin = 180;   // 60% of 300
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0024].nFalloffCoeff = 5;     // this corresponds to 10 periods

        // Deployment of Deployment of Basic BLS, AssetLocks, EHF
        consensus.vDeployments[Consensus::DEPLOYMENT_V19].bit = 8;
        consensus.vDeployments[Consensus::DEPLOYMENT_V19].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_V19].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_V19].nWindowSize = 300;
        consensus.vDeployments[Consensus::DEPLOYMENT_V19].nThresholdStart = 240; // 80% of 300
        consensus.vDeployments[Consensus::DEPLOYMENT_V19].nThresholdMin = 180;   // 60% of 300
        consensus.vDeployments[Consensus::DEPLOYMENT_V19].nFalloffCoeff = 5;     // this corresponds to 10 periods

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00");

        pchMessageStart[0] = 0xff;
        pchMessageStart[1] = 0x01;
        pchMessageStart[2] = 0x02;
        pchMessageStart[3] = 0x03;
        nDefaultPort = 63646;
        nDefaultPlatformP2PPort = 22200;
        nDefaultPlatformHTTPPort = 22201;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        UpdateVersionBitsParametersFromArgs(args);
        UpdateDIP3ParametersFromArgs(args);
        UpdateDIP8ParametersFromArgs(args);
        UpdateBudgetParametersFromArgs(args);

        genesis = CreateGenesisBlock(1618221600, 98745, 0x207fffff, 1, 0 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();

        uint256 expectedGenesisHash = uint256S("0x5cc74fcae11b83a1f00ca81106deaae119486ded33918571a639e7b6eac83150");
        uint256 expectedGenesisMerkleRoot = uint256S("e16337d6f2cd561e3b9b2c470ec2adc11cf94ba2cda40bddfd2f23deff2499fb");

        #ifdef COSANTA_MINE_NEW_GENESIS_BLOCK
        if (consensus.hashGenesisBlock != expectedGenesisHash)
        {
            GenesisMiner mine(genesis, strNetworkID);
        }
        #endif // COSANTA_MINE_NEW_GENESIS_BLOCK

        assert(consensus.hashGenesisBlock == expectedGenesisHash);
        assert(genesis.hashMerkleRoot == expectedGenesisMerkleRoot);

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        fDefaultConsistencyChecks = true;
        fRequireStandard = true;
        fRequireRoutableExternalIP = false;
        m_is_test_chain = true;
        fAllowMultipleAddressesFromGroup = true;
        fAllowMultiplePorts = true;
        nLLMQConnectionRetryTimeout = 1; // must be lower then the LLMQ signing session timeout so that tests have control over failing behavior
        m_is_mockable_chain = true;

        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes
        nPoolMinParticipants = 2;
        nPoolMaxParticipants = 20;

        vSporkAddresses = {"santaeu6dAf3tren1oTEhgRfuTc8f88nZk"};
        nMinSporkKeys = 1;
        // regtest usually has no masternodes in most tests, so don't check for upgraged MNs
        fBIP9CheckMasternodesUpgraded = false;

        nStakeMinAge = 24 * 60 * 60; // 24 hours
        nFirstPoSv2Block = 10000ULL;

        checkpointData = {
            {
                {0, uint256S("0x5cc74fcae11b83a1f00ca81106deaae119486ded33918571a639e7b6eac83150")},
            }
        };

        chainTxData = ChainTxData{
            0,
            0,
            0
        };

        // Regtest Cosanta addresses start with 's'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,125);
        // Regtest Cosanta script addresses start with '8' or '9'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,19);
        // Regtest Cosanta keys start with '9' or 'c' (Bitcoin defaults)
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        // Regtest Cosana BIP32 pubkeys start with 'tpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        // Regtest Cosana BIP32 prvkeys start with 'tprv' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        // Regtest Cosanta BIP44 coin type is '1' (All coin's testnet default)
        nExtCoinType = 1;

        // long living quorum params
        AddLLMQ(Consensus::LLMQType::LLMQ_TEST);
        AddLLMQ(Consensus::LLMQType::LLMQ_TEST_INSTANTSEND);
        AddLLMQ(Consensus::LLMQType::LLMQ_TEST_V17);
        AddLLMQ(Consensus::LLMQType::LLMQ_TEST_DIP0024);
        AddLLMQ(Consensus::LLMQType::LLMQ_TEST_PLATFORM);
        consensus.llmqTypeChainLocks = Consensus::LLMQType::LLMQ_TEST;
        consensus.llmqTypeInstantSend = Consensus::LLMQType::LLMQ_TEST_INSTANTSEND;
        consensus.llmqTypeDIP0024InstantSend = Consensus::LLMQType::LLMQ_TEST_DIP0024;
        consensus.llmqTypePlatform = Consensus::LLMQType::LLMQ_TEST_PLATFORM;
        consensus.llmqTypeMnhf = Consensus::LLMQType::LLMQ_TEST;

        UpdateLLMQTestParametersFromArgs(args, Consensus::LLMQType::LLMQ_TEST);
        UpdateLLMQTestParametersFromArgs(args, Consensus::LLMQType::LLMQ_TEST_INSTANTSEND);
        UpdateLLMQInstantSendFromArgs(args);
        UpdateLLMQInstantSendDIP0024FromArgs(args);
    }

    /**
     * Allows modifying the Version Bits regtest parameters.
     */
    void UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout, int64_t nWindowSize, int64_t nThresholdStart, int64_t nThresholdMin, int64_t nFalloffCoeff)
    {
        consensus.vDeployments[d].nStartTime = nStartTime;
        consensus.vDeployments[d].nTimeout = nTimeout;
        if (nWindowSize != -1) {
            consensus.vDeployments[d].nWindowSize = nWindowSize;
        }
        if (nThresholdStart != -1) {
            consensus.vDeployments[d].nThresholdStart = nThresholdStart;
        }
        if (nThresholdMin != -1) {
            consensus.vDeployments[d].nThresholdMin = nThresholdMin;
        }
        if (nFalloffCoeff != -1) {
            consensus.vDeployments[d].nFalloffCoeff = nFalloffCoeff;
        }
    }
    void UpdateVersionBitsParametersFromArgs(const ArgsManager& args);

    /**
     * Allows modifying the DIP3 activation and enforcement height
     */
    void UpdateDIP3Parameters(int nActivationHeight, int nEnforcementHeight)
    {
        consensus.DIP0003Height = nActivationHeight;
        consensus.DIP0003EnforcementHeight = nEnforcementHeight;
    }
    void UpdateDIP3ParametersFromArgs(const ArgsManager& args);

    /**
     * Allows modifying the DIP8 activation height
     */
    void UpdateDIP8Parameters(int nActivationHeight)
    {
        consensus.DIP0008Height = nActivationHeight;
    }
    void UpdateDIP8ParametersFromArgs(const ArgsManager& args);

    /**
     * Allows modifying the budget regtest parameters.
     */
    void UpdateBudgetParameters(int nMasternodePaymentsStartBlock, int nBudgetPaymentsStartBlock, int nSuperblockStartBlock)
    {
        consensus.nMasternodePaymentsStartBlock = nMasternodePaymentsStartBlock;
        consensus.nBudgetPaymentsStartBlock = nBudgetPaymentsStartBlock;
        consensus.nSuperblockStartBlock = nSuperblockStartBlock;
    }
    void UpdateBudgetParametersFromArgs(const ArgsManager& args);

    /**
     * Allows modifying parameters of the test LLMQ
     */
    void UpdateLLMQTestParameters(int size, int threshold, const Consensus::LLMQType llmqType)
    {
        auto params = ranges::find_if(consensus.llmqs, [llmqType](const auto& llmq){ return llmq.type == llmqType;});
        assert(params != consensus.llmqs.end());
        params->size = size;
        params->minSize = threshold;
        params->threshold = threshold;
        params->dkgBadVotesThreshold = threshold;
    }

    /**
     * Allows modifying the LLMQ type for InstantSend.
     */
    void UpdateLLMQInstantSend(Consensus::LLMQType llmqType)
    {
        consensus.llmqTypeInstantSend = llmqType;
    }

    /**
     * Allows modifying the LLMQ type for InstantSend (DIP0024).
     */
    void UpdateLLMQDIP0024InstantSend(Consensus::LLMQType llmqType)
    {
        consensus.llmqTypeDIP0024InstantSend = llmqType;
    }

    void UpdateLLMQTestParametersFromArgs(const ArgsManager& args, const Consensus::LLMQType llmqType);
    void UpdateLLMQInstantSendFromArgs(const ArgsManager& args);
    void UpdateLLMQInstantSendDIP0024FromArgs(const ArgsManager& args);
};

void CRegTestParams::UpdateVersionBitsParametersFromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-vbparams")) return;

    for (const std::string& strDeployment : args.GetArgs("-vbparams")) {
        std::vector<std::string> vDeploymentParams = SplitString(strDeployment, ':');
        if (vDeploymentParams.size() != 3 && vDeploymentParams.size() != 5 && vDeploymentParams.size() != 7) {
            throw std::runtime_error("Version bits parameters malformed, expecting "
                    "<deployment>:<start>:<end> or "
                    "<deployment>:<start>:<end>:<window>:<threshold> or "
                    "<deployment>:<start>:<end>:<window>:<thresholdstart>:<thresholdmin>:<falloffcoeff>");
        }
        int64_t nStartTime, nTimeout, nWindowSize = -1, nThresholdStart = -1, nThresholdMin = -1, nFalloffCoeff = -1;
        if (!ParseInt64(vDeploymentParams[1], &nStartTime)) {
            throw std::runtime_error(strprintf("Invalid nStartTime (%s)", vDeploymentParams[1]));
        }
        if (!ParseInt64(vDeploymentParams[2], &nTimeout)) {
            throw std::runtime_error(strprintf("Invalid nTimeout (%s)", vDeploymentParams[2]));
        }
        if (vDeploymentParams.size() >= 5) {
            if (!ParseInt64(vDeploymentParams[3], &nWindowSize)) {
                throw std::runtime_error(strprintf("Invalid nWindowSize (%s)", vDeploymentParams[3]));
            }
            if (!ParseInt64(vDeploymentParams[4], &nThresholdStart)) {
                throw std::runtime_error(strprintf("Invalid nThresholdStart (%s)", vDeploymentParams[4]));
            }
        }
        if (vDeploymentParams.size() == 7) {
            if (!ParseInt64(vDeploymentParams[5], &nThresholdMin)) {
                throw std::runtime_error(strprintf("Invalid nThresholdMin (%s)", vDeploymentParams[5]));
            }
            if (!ParseInt64(vDeploymentParams[6], &nFalloffCoeff)) {
                throw std::runtime_error(strprintf("Invalid nFalloffCoeff (%s)", vDeploymentParams[6]));
            }
        }
        bool found = false;
        for (int j=0; j < (int)Consensus::MAX_VERSION_BITS_DEPLOYMENTS; ++j) {
            if (vDeploymentParams[0] == VersionBitsDeploymentInfo[j].name) {
                UpdateVersionBitsParameters(Consensus::DeploymentPos(j), nStartTime, nTimeout, nWindowSize, nThresholdStart, nThresholdMin, nFalloffCoeff);
                found = true;
                LogPrintf("Setting version bits activation parameters for %s to start=%ld, timeout=%ld, window=%ld, thresholdstart=%ld, thresholdmin=%ld, falloffcoeff=%ld\n",
                          vDeploymentParams[0], nStartTime, nTimeout, nWindowSize, nThresholdStart, nThresholdMin, nFalloffCoeff);
                break;
            }
        }
        if (!found) {
            throw std::runtime_error(strprintf("Invalid deployment (%s)", vDeploymentParams[0]));
        }
    }
}

void CRegTestParams::UpdateDIP3ParametersFromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-dip3params")) return;

    std::string strParams = args.GetArg("-dip3params", "");
    std::vector<std::string> vParams = SplitString(strParams, ':');
    if (vParams.size() != 2) {
        throw std::runtime_error("DIP3 parameters malformed, expecting <activation>:<enforcement>");
    }
    int nDIP3ActivationHeight, nDIP3EnforcementHeight;
    if (!ParseInt32(vParams[0], &nDIP3ActivationHeight)) {
        throw std::runtime_error(strprintf("Invalid activation height (%s)", vParams[0]));
    }
    if (!ParseInt32(vParams[1], &nDIP3EnforcementHeight)) {
        throw std::runtime_error(strprintf("Invalid enforcement height (%s)", vParams[1]));
    }
    LogPrintf("Setting DIP3 parameters to activation=%ld, enforcement=%ld\n", nDIP3ActivationHeight, nDIP3EnforcementHeight);
    UpdateDIP3Parameters(nDIP3ActivationHeight, nDIP3EnforcementHeight);
}

void CRegTestParams::UpdateDIP8ParametersFromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-dip8params")) return;

    std::string strParams = args.GetArg("-dip8params", "");
    std::vector<std::string> vParams = SplitString(strParams, ':');
    if (vParams.size() != 1) {
        throw std::runtime_error("DIP8 parameters malformed, expecting <activation>");
    }
    int nDIP8ActivationHeight;
    if (!ParseInt32(vParams[0], &nDIP8ActivationHeight)) {
        throw std::runtime_error(strprintf("Invalid activation height (%s)", vParams[0]));
    }
    LogPrintf("Setting DIP8 parameters to activation=%ld\n", nDIP8ActivationHeight);
    UpdateDIP8Parameters(nDIP8ActivationHeight);
}

void CRegTestParams::UpdateBudgetParametersFromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-budgetparams")) return;

    std::string strParams = args.GetArg("-budgetparams", "");
    std::vector<std::string> vParams = SplitString(strParams, ':');
    if (vParams.size() != 3) {
        throw std::runtime_error("Budget parameters malformed, expecting <masternode>:<budget>:<superblock>");
    }
    int nMasternodePaymentsStartBlock, nBudgetPaymentsStartBlock, nSuperblockStartBlock;
    if (!ParseInt32(vParams[0], &nMasternodePaymentsStartBlock)) {
        throw std::runtime_error(strprintf("Invalid masternode start height (%s)", vParams[0]));
    }
    if (!ParseInt32(vParams[1], &nBudgetPaymentsStartBlock)) {
        throw std::runtime_error(strprintf("Invalid budget start block (%s)", vParams[1]));
    }
    if (!ParseInt32(vParams[2], &nSuperblockStartBlock)) {
        throw std::runtime_error(strprintf("Invalid superblock start height (%s)", vParams[2]));
    }
    LogPrintf("Setting budget parameters to masternode=%ld, budget=%ld, superblock=%ld\n", nMasternodePaymentsStartBlock, nBudgetPaymentsStartBlock, nSuperblockStartBlock);
    UpdateBudgetParameters(nMasternodePaymentsStartBlock, nBudgetPaymentsStartBlock, nSuperblockStartBlock);
}

void CRegTestParams::UpdateLLMQTestParametersFromArgs(const ArgsManager& args, const Consensus::LLMQType llmqType)
{
    assert(llmqType == Consensus::LLMQType::LLMQ_TEST || llmqType == Consensus::LLMQType::LLMQ_TEST_INSTANTSEND);

    std::string cmd_param{"-llmqtestparams"}, llmq_name{"LLMQ_TEST"};
    if (llmqType == Consensus::LLMQType::LLMQ_TEST_INSTANTSEND) {
        cmd_param = "-llmqtestinstantsendparams";
        llmq_name = "LLMQ_TEST_INSTANTSEND";
    }

    if (!args.IsArgSet(cmd_param)) return;

    std::string strParams = args.GetArg(cmd_param, "");
    std::vector<std::string> vParams = SplitString(strParams, ':');
    if (vParams.size() != 2) {
        throw std::runtime_error(strprintf("%s parameters malformed, expecting <size>:<threshold>", llmq_name));
    }
    int size, threshold;
    if (!ParseInt32(vParams[0], &size)) {
        throw std::runtime_error(strprintf("Invalid %s size (%s)", llmq_name, vParams[0]));
    }
    if (!ParseInt32(vParams[1], &threshold)) {
        throw std::runtime_error(strprintf("Invalid %s threshold (%s)", llmq_name, vParams[1]));
    }
    LogPrintf("Setting %s parameters to size=%ld, threshold=%ld\n", llmq_name, size, threshold);
    UpdateLLMQTestParameters(size, threshold, llmqType);
}

void CRegTestParams::UpdateLLMQInstantSendFromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-llmqinstantsend")) return;

    const auto& llmq_params_opt = GetLLMQ(consensus.llmqTypeInstantSend);
    assert(llmq_params_opt.has_value());

    std::string strLLMQType = gArgs.GetArg("-llmqinstantsend", std::string(llmq_params_opt->name));

    Consensus::LLMQType llmqType = Consensus::LLMQType::LLMQ_NONE;
    for (const auto& params : consensus.llmqs) {
        if (params.name == strLLMQType) {
            llmqType = params.type;
        }
    }
    if (llmqType == Consensus::LLMQType::LLMQ_NONE) {
        throw std::runtime_error("Invalid LLMQ type specified for -llmqinstantsend.");
    }
    LogPrintf("Setting llmqinstantsend to size=%ld\n", ToUnderlying(llmqType));
    UpdateLLMQInstantSend(llmqType);
}

void CRegTestParams::UpdateLLMQInstantSendDIP0024FromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-llmqinstantsenddip0024")) return;

    const auto& llmq_params_opt = GetLLMQ(consensus.llmqTypeDIP0024InstantSend);
    assert(llmq_params_opt.has_value());

    std::string strLLMQType = gArgs.GetArg("-llmqinstantsenddip0024", std::string(llmq_params_opt->name));

    Consensus::LLMQType llmqType = Consensus::LLMQType::LLMQ_NONE;
    for (const auto& params : consensus.llmqs) {
        if (params.name == strLLMQType) {
            llmqType = params.type;
        }
    }
    if (llmqType == Consensus::LLMQType::LLMQ_NONE) {
        throw std::runtime_error("Invalid LLMQ type specified for -llmqinstantsenddip0024.");
    }
    LogPrintf("Setting llmqinstantsenddip0024 to size=%ld\n", ToUnderlying(llmqType));
    UpdateLLMQDIP0024InstantSend(llmqType);
}

void CDevNetParams::UpdateDevnetSubsidyAndDiffParametersFromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-minimumdifficultyblocks") && !args.IsArgSet("-highsubsidyblocks") && !args.IsArgSet("-highsubsidyfactor")) return;

    int nMinimumDifficultyBlocks = gArgs.GetArg("-minimumdifficultyblocks", consensus.nMinimumDifficultyBlocks);
    int nHighSubsidyBlocks = gArgs.GetArg("-highsubsidyblocks", consensus.nHighSubsidyBlocks);
    int nHighSubsidyFactor = gArgs.GetArg("-highsubsidyfactor", consensus.nHighSubsidyFactor);
    LogPrintf("Setting minimumdifficultyblocks=%ld, highsubsidyblocks=%ld, highsubsidyfactor=%ld\n", nMinimumDifficultyBlocks, nHighSubsidyBlocks, nHighSubsidyFactor);
    UpdateDevnetSubsidyAndDiffParameters(nMinimumDifficultyBlocks, nHighSubsidyBlocks, nHighSubsidyFactor);
}

void CDevNetParams::UpdateDevnetLLMQChainLocksFromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-llmqchainlocks")) return;

    const auto& llmq_params_opt = GetLLMQ(consensus.llmqTypeChainLocks);
    assert(llmq_params_opt.has_value());

    std::string strLLMQType = gArgs.GetArg("-llmqchainlocks", std::string(llmq_params_opt->name));

    Consensus::LLMQType llmqType = Consensus::LLMQType::LLMQ_NONE;
    for (const auto& params : consensus.llmqs) {
        if (params.name == strLLMQType) {
            if (params.useRotation) {
                throw std::runtime_error("LLMQ type specified for -llmqchainlocks must NOT use rotation");
            }
            llmqType = params.type;
        }
    }
    if (llmqType == Consensus::LLMQType::LLMQ_NONE) {
        throw std::runtime_error("Invalid LLMQ type specified for -llmqchainlocks.");
    }
    LogPrintf("Setting llmqchainlocks to size=%ld\n", static_cast<uint8_t>(llmqType));
    UpdateDevnetLLMQChainLocks(llmqType);
}

void CDevNetParams::UpdateDevnetLLMQInstantSendFromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-llmqinstantsend")) return;

    const auto& llmq_params_opt = GetLLMQ(consensus.llmqTypeInstantSend);
    assert(llmq_params_opt.has_value());

    std::string strLLMQType = gArgs.GetArg("-llmqinstantsend", std::string(llmq_params_opt->name));

    Consensus::LLMQType llmqType = Consensus::LLMQType::LLMQ_NONE;
    for (const auto& params : consensus.llmqs) {
        if (params.name == strLLMQType) {
            if (params.useRotation) {
                throw std::runtime_error("LLMQ type specified for -llmqinstantsend must NOT use rotation");
            }
            llmqType = params.type;
        }
    }
    if (llmqType == Consensus::LLMQType::LLMQ_NONE) {
        throw std::runtime_error("Invalid LLMQ type specified for -llmqinstantsend.");
    }
    LogPrintf("Setting llmqinstantsend to size=%ld\n", static_cast<uint8_t>(llmqType));
    UpdateDevnetLLMQInstantSend(llmqType);
}

void CDevNetParams::UpdateDevnetLLMQInstantSendDIP0024FromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-llmqinstantsenddip0024")) return;

    const auto& llmq_params_opt = GetLLMQ(consensus.llmqTypeDIP0024InstantSend);
    assert(llmq_params_opt.has_value());

    std::string strLLMQType = gArgs.GetArg("-llmqinstantsenddip0024", std::string(llmq_params_opt->name));

    Consensus::LLMQType llmqType = Consensus::LLMQType::LLMQ_NONE;
    for (const auto& params : consensus.llmqs) {
        if (params.name == strLLMQType) {
            if (!params.useRotation) {
                throw std::runtime_error("LLMQ type specified for -llmqinstantsenddip0024 must use rotation");
            }
            llmqType = params.type;
        }
    }
    if (llmqType == Consensus::LLMQType::LLMQ_NONE) {
        throw std::runtime_error("Invalid LLMQ type specified for -llmqinstantsenddip0024.");
    }
    LogPrintf("Setting llmqinstantsenddip0024 to size=%ld\n", static_cast<uint8_t>(llmqType));
    UpdateDevnetLLMQDIP0024InstantSend(llmqType);
}

void CDevNetParams::UpdateDevnetLLMQPlatformFromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-llmqplatform")) return;

    const auto& llmq_params_opt = GetLLMQ(consensus.llmqTypePlatform);
    assert(llmq_params_opt.has_value());

    std::string strLLMQType = gArgs.GetArg("-llmqplatform", std::string(llmq_params_opt->name));

    Consensus::LLMQType llmqType = Consensus::LLMQType::LLMQ_NONE;
    for (const auto& params : consensus.llmqs) {
        if (params.name == strLLMQType) {
            llmqType = params.type;
        }
    }
    if (llmqType == Consensus::LLMQType::LLMQ_NONE) {
        throw std::runtime_error("Invalid LLMQ type specified for -llmqplatform.");
    }
    LogPrintf("Setting llmqplatform to size=%ld\n", static_cast<uint8_t>(llmqType));
    UpdateDevnetLLMQPlatform(llmqType);
}

void CDevNetParams::UpdateDevnetPowTargetSpacingFromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-powtargetspacing")) return;

    std::string strPowTargetSpacing = gArgs.GetArg("-powtargetspacing", "");

    int64_t powTargetSpacing;
    if (!ParseInt64(strPowTargetSpacing, &powTargetSpacing)) {
        throw std::runtime_error(strprintf("Invalid parsing of powTargetSpacing (%s)", strPowTargetSpacing));
    }

    if (powTargetSpacing < 1) {
        throw std::runtime_error(strprintf("Invalid value of powTargetSpacing (%s)", strPowTargetSpacing));
    }

    LogPrintf("Setting powTargetSpacing to %ld\n", powTargetSpacing);
    UpdateDevnetPowTargetSpacing(powTargetSpacing);
}

void CDevNetParams::UpdateLLMQDevnetParametersFromArgs(const ArgsManager& args)
{
    if (!args.IsArgSet("-llmqdevnetparams")) return;

    std::string strParams = args.GetArg("-llmqdevnetparams", "");
    std::vector<std::string> vParams = SplitString(strParams, ':');
    if (vParams.size() != 2) {
        throw std::runtime_error("LLMQ_DEVNET parameters malformed, expecting <size>:<threshold>");
    }
    int size, threshold;
    if (!ParseInt32(vParams[0], &size)) {
        throw std::runtime_error(strprintf("Invalid LLMQ_DEVNET size (%s)", vParams[0]));
    }
    if (!ParseInt32(vParams[1], &threshold)) {
        throw std::runtime_error(strprintf("Invalid LLMQ_DEVNET threshold (%s)", vParams[1]));
    }
    LogPrintf("Setting LLMQ_DEVNET parameters to size=%ld, threshold=%ld\n", size, threshold);
    UpdateLLMQDevnetParameters(size, threshold);
}

static std::unique_ptr<const CChainParams> globalChainParams;

const CChainParams &Params() {
    assert(globalChainParams);
    return *globalChainParams;
}

std::unique_ptr<const CChainParams> CreateChainParams(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
        return std::make_unique<CMainParams>();
    else if (chain == CBaseChainParams::TESTNET)
        return std::make_unique<CTestNetParams>();
    else if (chain == CBaseChainParams::DEVNET)
        return std::make_unique<CDevNetParams>(gArgs);
    else if (chain == CBaseChainParams::REGTEST)
        return std::make_unique<CRegTestParams>(gArgs);

    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    globalChainParams = CreateChainParams(network);
}
