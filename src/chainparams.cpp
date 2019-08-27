// Copyright (c) 2010 Satoshi Nakamoto
/// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"

#include "assert.h"
#include "random.h"
#include "util.h"
#include "main.h"

#include <boost/assign/list_of.hpp>

using namespace boost::assign;

struct SeedSpec6 {
    uint8_t addr[16];
    uint16_t port;
};

#include "chainparamsseeds.h"

//
// Main network
//

// Convert the pnSeeds array into usable address objects.
static void convertSeeds(std::vector<CAddress> &vSeedsOut, const unsigned int *data, unsigned int count, int port)
{
     // It'll only connect to one or two seed nodes because once it connects,
     // it'll get a pile of addresses with newer timestamps.
     // Seed nodes are given a random 'last seen time' of between one and two
     // weeks ago.
     const int64_t nOneWeek = 7*24*60*60;
    for (unsigned int k = 0; k < count; ++k)
    {
        struct in_addr ip;
        unsigned int i = data[k], t;
        
        // -- convert to big endian
        t =   (i & 0x000000ff) << 24u
            | (i & 0x0000ff00) << 8u
            | (i & 0x00ff0000) >> 8u
            | (i & 0xff000000) >> 24u;
        
        memcpy(&ip, &t, sizeof(ip));
        
        CAddress addr(CService(ip, port));
        addr.nTime = GetTime()-GetRand(nOneWeek)-nOneWeek;
        vSeedsOut.push_back(addr);
    }
}

class CMainParams : public CChainParams {
public:
    CMainParams() {
        // The message start string is designed to be unlikely to occur in normal data.
        // The characters are rarely used upper ASCII, not valid as UTF-8, and produce
        // a large 4-byte int at any alignment.
        pchMessageStart[0] = 0x70;
        pchMessageStart[1] = 0x75;
        pchMessageStart[2] = 0x6d;
        pchMessageStart[3] = 0x70;
        vAlertPubKey = ParseHex("040ae919ea82ee6157d742875169262309f2c664c8f5a8055a91ba92e95081435dbbe1c9c75299501dabdacdddfe0a6c587b86d85f3730a1558b88184f2e60140f");
        nDefaultPort = 18888;
        nRPCPort = 11888;
        bnProofOfWorkLimit = CBigNum(~uint256(0) >> 16);
        SPEC_TARGET_FIX = 310000;

        const char* pszTimestamp = "2018/11/03 The Pirate Code of Conduct consisted of a number of agreements between the Captain and pirate crew which were called Articles. The Pirate Code of Conduct was necessary as pirates were not governed by any other rules such as Naval regulations. Pirate captains were elected and could lose their position for abuse of their authority.";
        std::vector<CTxIn> vin;
        vin.resize(1);
        vin[0].scriptSig = CScript() << 0 << CBigNum(42) << vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
        std::vector<CTxOut> vout;
        vout.resize(1);
        vout[0].SetEmpty();
        CTransaction txNew(1, 1541202300, vin, vout, 0);
        genesis.vtx.push_back(txNew);
        genesis.hashPrevBlock = 0;
        genesis.hashMerkleRoot = genesis.BuildMerkleTree();
        genesis.nVersion = 1;
        genesis.nTime    = 1541202300;
        genesis.nBits    = 0x1f00ffff; 
        genesis.nNonce   = 666;
        
        hashGenesisBlock = genesis.GetHash();
        assert(hashGenesisBlock == uint256("0x33422d3f8e94bae7cd2544e737d64ff8ec3ee140cc3fdc4db3d14656f9a60912"));
        assert(genesis.hashMerkleRoot == uint256("0x212a796316c63f41e26b31b9a947f48b56d5e0df7767774b152e29b08da8b0b7"));

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,55);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,13);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,203);
        base58Prefixes[STEALTH_ADDRESS] = std::vector<unsigned char>(1,204);
        base58Prefixes[EXT_PUBLIC_KEY] = list_of(0x04)(0x88)(0xB2)(0x1E).convert_to_container<std::vector<unsigned char> >();
        base58Prefixes[EXT_SECRET_KEY] = list_of(0x04)(0x88)(0xAD)(0xE4).convert_to_container<std::vector<unsigned char> >();
        vSeeds.push_back(CDNSSeedData("0", "m1.piratecash.net"));
        vSeeds.push_back(CDNSSeedData("1", "m2.piratecash.net"));
        vSeeds.push_back(CDNSSeedData("2", "m3.piratecash.net"));
        vSeeds.push_back(CDNSSeedData("3", "m4.piratecash.net"));
        vSeeds.push_back(CDNSSeedData("4", "m5.piratecash.net"));
        vSeeds.push_back(CDNSSeedData("5", "m6.piratecash.net"));
        vSeeds.push_back(CDNSSeedData("6", "m7.piratecash.net"));
        vSeeds.push_back(CDNSSeedData("7", "m8.piratecash.net"));
        vSeeds.push_back(CDNSSeedData("8", "m9.piratecash.net"));
        vSeeds.push_back(CDNSSeedData("9", "piratealjlfw7h2d.onion"));
        
        convertSeeds(vFixedSeeds, pnSeed, ARRAYLEN(pnSeed), nDefaultPort);

        nPoolMaxTransactions = 3;
        strDarksendPoolDummyAddress = "PUJfJNMugfTTiqFWZ21mwMQ1nfYCgn7naK";
        nLastPOWBlock = 100000;
        nPOSStartBlock = 1;
    }

    virtual const CBlock& GenesisBlock() const { return genesis; }
    virtual Network NetworkID() const { return CChainParams::MAIN; }

    virtual const vector<CAddress>& FixedSeeds() const {
        return vFixedSeeds;
    }
protected:
    CBlock genesis;
    vector<CAddress> vFixedSeeds;
};
static CMainParams mainParams;


//
// Testnet
//

class CTestNetParams : public CMainParams {
public:
    CTestNetParams() {
        // The message start string is designed to be unlikely to occur in normal data.
        // The characters are rarely used upper ASCII, not valid as UTF-8, and produce
        // a large 4-byte int at any alignment.
        pchMessageStart[0] = 0x41;
        pchMessageStart[1] = 0x52;
        pchMessageStart[2] = 0x52;
        pchMessageStart[3] = 0x52;
        bnProofOfWorkLimit = CBigNum(~uint256(0) >> 16);
        vAlertPubKey = ParseHex("0469fc77d961509210782f794edfc71d3dd9419d69976bbf01fdea6db81bff8f5c05144ba0420840ab0a8ba8a23ba96811239db4a333c093b0692982c6c564078b");
        nDefaultPort = 28888;
        nRPCPort = 21888;
        strDataDir = "testnet";
        SPEC_TARGET_FIX = 104000;

        // Modify the testnet genesis block so the timestamp is valid for a later start.
        genesis.nBits  = 0x1f04ade3;
        genesis.nNonce = 9999;
        hashGenesisBlock = genesis.GetHash();

        assert(hashGenesisBlock == uint256("0xc00cae22f3bf4a3fe0b3fa0e66628d251f8dab02148cc48ff06d81ae47a1f30d"));

        vFixedSeeds.clear();
        vSeeds.clear();

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,75);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,18);
        base58Prefixes[SECRET_KEY]     = std::vector<unsigned char>(1,203);
        base58Prefixes[STEALTH_ADDRESS] = std::vector<unsigned char>(1,40);
        base58Prefixes[EXT_PUBLIC_KEY] = list_of(0x04)(0x35)(0x87)(0xCF).convert_to_container<std::vector<unsigned char> >();
        base58Prefixes[EXT_SECRET_KEY] = list_of(0x04)(0x35)(0x83)(0x94).convert_to_container<std::vector<unsigned char> >();
        vSeeds.push_back(CDNSSeedData("0", "m1.piratecash.net"));

        convertSeeds(vFixedSeeds, pnTestnetSeed, ARRAYLEN(pnTestnetSeed), nDefaultPort);

        nLastPOWBlock = 3000;
    }
    virtual Network NetworkID() const { return CChainParams::TESTNET; }
};
static CTestNetParams testNetParams;


static CChainParams *pCurrentParams = &mainParams;

const CChainParams &Params() {
    return *pCurrentParams;
}

void SelectParams(CChainParams::Network network) {
    switch (network) {
        case CChainParams::MAIN:
            pCurrentParams = &mainParams;
            break;
        case CChainParams::TESTNET:
            pCurrentParams = &testNetParams;
            break;
        default:
            assert(false && "Unimplemented network");
            return;
    }
}

bool SelectParamsFromCommandLine() {
    
    bool fTestNet = GetBoolArg("-testnet", false);
    
    if (fTestNet) {
        SelectParams(CChainParams::TESTNET);
    } else {
        SelectParams(CChainParams::MAIN);
    }
    return true;
}
