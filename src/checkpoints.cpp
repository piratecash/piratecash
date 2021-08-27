// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/assign/list_of.hpp> // for 'map_list_of()'
#include <boost/foreach.hpp>

#include "checkpoints.h"

#include "txdb.h"
#include "main.h"
#include "uint256.h"


static const int nCheckpointSpan = 5000;

namespace Checkpoints
{
    typedef std::map<int, uint256> MapCheckpoints;
    typedef std::list<uint256> ListBannedBlocks;

    //
    // What makes a good checkpoint block?
    // + Is surrounded by blocks with reasonable timestamps
    //   (no blocks before with a timestamp after, none after with
    //    timestamp before)
    // + Contains no strange transactions
    //
    static MapCheckpoints mapCheckpoints =
        boost::assign::map_list_of
        (0, uint256("0x212a796316c63f41e26b31b9a947f48b56d5e0df7767774b152e29b08da8b0b7"))
        (102114, uint256("0x21617fcf6e6463dadb5852399a40a2318c8ba3e5e8ebba72802e59defc1fddd6"))
        (121719, uint256("0x5cace4a5ad16de8e13c1cfa8dffeb4fcfa6e7b668a3561f70480ce97cfa187f5"))
        (122666, uint256("0xfcdf3c2e28b624550a65912c545a4eb7e50847688f923eb442b99be2510249c6"))
        (122945, uint256("0x6d179e154035a49ca21a71a9cb1f20cac80cc4b77da4b80d8e61d4a8fdb4919e"))
        (207444, uint256("0xc811b01b3cddda7caca602d10dd626c2fbd3a095c792aa6770e4243b931dfc68"))
        (310001, uint256("0x2adbd1c527fe1a0b79de47a97092891795ede32af923aea48bbc9b29aa6ba68b"))
        (413200, uint256("0x13af1cc9445be9ee711bd9db551570a1f25543167da161ba03dee274e72b41ca"))
        (465065, uint256("0x00431b03387800031c6db298c377428ea28874cbb774768bd78e9e04d78ba5dc"))
        (529800, uint256("0xe3c536fb8e03a7f943f9b95897c9f44992f242723816c9c294d024dc1b9f0600"))
        (687687, uint256("0x981a2ee7a4c74f34689ec6de92bb3cbaf00aaf231a9b78ea97bb21a2e8bb3a42"))
        (741417, uint256("0x7440bc3d9dcf3b9f3208267e54eb0def7976b2ea0ffa656c0a54ac14614d04a3"))
    ;

    static ListBannedBlocks listBanned =
        boost::assign::list_of
        (uint256("0x0"))
    ;

    // TestNet has no checkpoints
    static MapCheckpoints mapCheckpointsTestnet;

    bool CheckHardened(int nHeight, const uint256& hash)
    {
        MapCheckpoints& checkpoints = (TestNet() ? mapCheckpointsTestnet : mapCheckpoints);

        MapCheckpoints::const_iterator i = checkpoints.find(nHeight);
        if (i == checkpoints.end()) return true;
        return hash == i->second;
    }

    bool CheckBanned(const uint256 &nHash)
    {
//        if (TestNet()) // Testnet has no banned blocks
//            return true;
        ListBannedBlocks::const_iterator it = std::find(listBanned.begin(), listBanned.end(), nHash);
        return it == listBanned.end();
    }

    int GetTotalBlocksEstimate()
    {
        MapCheckpoints& checkpoints = (TestNet() ? mapCheckpointsTestnet : mapCheckpoints);

        if (checkpoints.empty())
            return 0;
        return checkpoints.rbegin()->first;
    }

    CBlockIndex* GetLastCheckpoint(const BlockMap& mapBlockIndex)
    {
        MapCheckpoints& checkpoints = (TestNet() ? mapCheckpointsTestnet : mapCheckpoints);

        BOOST_REVERSE_FOREACH(const MapCheckpoints::value_type& i, checkpoints)
        {
            const uint256& hash = i.second;
            BlockMap::const_iterator t = mapBlockIndex.find(hash);
            if (t != mapBlockIndex.end())
                return t->second;
        }
        return NULL;
    }

    // Automatically select a suitable sync-checkpoint 
    const CBlockIndex* AutoSelectSyncCheckpoint()
    {
        const CBlockIndex *pindex = pindexBest;
        // Search backward for a block within max span and maturity window
        while (pindex->pprev && pindex->nHeight + nCheckpointSpan > pindexBest->nHeight)
            pindex = pindex->pprev;
        return pindex;
    }

    // Check against synchronized checkpoint
    bool CheckSync(int nHeight)
    {
        const CBlockIndex* pindexSync = AutoSelectSyncCheckpoint();
        if (nHeight <= pindexSync->nHeight){
            return false;
        }
        return true;
    }
}
