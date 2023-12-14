// Copyright (c) 2021 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bls/bls.h>
#include <consensus/validation.h>
#include <evo/mnhftx.h>
#include <evo/specialtx.h>
#include <primitives/transaction.h>
#include <uint256.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>
#include <test/util/setup_common.h>

#include <cstdint>
#include <vector>


bool VerifyMNHFTx(const CTransaction& tx, CValidationState& state)
{
    MNHFTxPayload mnhfTx;
    if (!GetTxPayload(tx, mnhfTx)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-mnhf-payload");
    }

    if (mnhfTx.nVersion == 0 || mnhfTx.nVersion > MNHFTxPayload::CURRENT_VERSION) {
        return state.DoS(100, false, REJECT_INVALID, "bad-mnhf-version");
    }

    return true;
}

static CMutableTransaction CreateMNHFTx(const uint256& mnhfTxHash, const CBLSSignature& cblSig, const uint16_t& versionBit)
{
    MNHFTxPayload extraPayload;
    extraPayload.nVersion = 1;
    extraPayload.signal.nVersion = versionBit;
    extraPayload.signal.quorumHash = mnhfTxHash;
    extraPayload.signal.sig = cblSig;

    CMutableTransaction tx;
    tx.nVersion = 3;
    tx.nType = TRANSACTION_MNHF_SIGNAL;
    SetTxPayload(tx, extraPayload);

    return tx;
}

BOOST_FIXTURE_TEST_SUITE(specialtx_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(verify_mnhf_specialtx_tests)
{
    int count = 10;
    uint16_t ver = 2;

    std::vector<CBLSSignature> vec_sigs;
    std::vector<CBLSPublicKey> vec_pks;
    std::vector<CBLSSecretKey> vec_sks;

    CBLSSecretKey sk;
    uint256 hash = GetRandHash();
    for (int i = 0; i < count; i++) {
        sk.MakeNewKey();
        vec_pks.push_back(sk.GetPublicKey());
        vec_sks.push_back(sk);
    }

    CBLSSecretKey ag_sk = CBLSSecretKey::AggregateInsecure(vec_sks);
    CBLSPublicKey ag_pk = CBLSPublicKey::AggregateInsecure(vec_pks);

    BOOST_CHECK(ag_sk.IsValid());
    BOOST_CHECK(ag_pk.IsValid());

    uint256 verHash = uint256S(itostr(ver));
    auto sig = ag_sk.Sign(verHash);
    BOOST_CHECK(sig.VerifyInsecure(ag_pk, verHash));

    const CMutableTransaction tx = CreateMNHFTx(hash, sig, ver);
    CValidationState state;
    BOOST_CHECK(VerifyMNHFTx(CTransaction(tx), state));
}

BOOST_AUTO_TEST_SUITE_END()
