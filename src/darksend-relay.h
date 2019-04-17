
// Copyright (c) 2014-2015 The Dash developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DARKSEND_RELAY_H
#define DARKSEND_RELAY_H

#include "core.h"
#include "main.h"
#include "activemasternode.h"
#include "masternodeman.h"


class CDarkSendRelay
{
public:
    CTxIn vinMasternode;
    vector<unsigned char> vchSig;
    vector<unsigned char> vchSig2;
    int nBlockHeight;
    int nRelayType;
    CTxIn in;
    CTxOut out;

    CDarkSendRelay();
    CDarkSendRelay(CTxIn& vinMasternodeIn, vector<unsigned char>& vchSigIn, int nBlockHeightIn, int nRelayTypeIn, CTxIn& in2, CTxOut& out2);
    
    IMPLEMENT_SERIALIZE

    template <typename T, typename Stream, typename Operation>
    inline static size_t SerializationOp(T thisPtr, Stream& s, Operation ser_action, int nType, int nVersion) {
        size_t nSerSize = 0;
        READWRITE(thisPtr->vinMasternode);
        READWRITE(thisPtr->vchSig);
        READWRITE(thisPtr->vchSig2);
        READWRITE(thisPtr->nBlockHeight);
        READWRITE(thisPtr->nRelayType);
        READWRITE(thisPtr->in);
        READWRITE(thisPtr->out);
        return nSerSize;
    }

    std::string ToString();

    bool Sign(std::string strSharedKey);
    bool VerifyMessage(std::string strSharedKey);
    void Relay();
    void RelayThroughNode(int nRank);
};



#endif
