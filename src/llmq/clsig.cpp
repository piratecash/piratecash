// Copyright (c) 2021 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <llmq/clsig.h>
#include <tinyformat.h>

namespace llmq {
    const std::string CLSIG_REQUESTID_PREFIX = "clsig";

    bool CChainLockSig::IsNull() const {
        return nHeight == -1 && blockHash == uint256();
    }

    std::string CChainLockSig::ToString() const {
        return strprintf("CChainLockSig(nHeight=%d, blockHash=%s)", nHeight, blockHash.ToString());
    }
}
