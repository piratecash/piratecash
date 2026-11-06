// Copyright (c) 2012-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bls/bls.h>
#include <clientversion.h>
#include <common/bloom.h>
#include <key.h>
#include <key_io.h>
#include <merkleblock.h>
#include <primitives/block.h>
#include <serialize.h>
#include <streams.h>
#include <test/util/random.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <util/strencodings.h>

#include <vector>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(bloom_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(bloom_create_insert_serialize)
{
    CBloomFilter filter(3, 0.01, 0, BLOOM_UPDATE_ALL);

    BOOST_CHECK_MESSAGE( !filter.contains(ParseHex("99108ad8ed9bb6274d3980bab5a85c048f0950c8")), "Bloom filter should be empty!");
    filter.insert(ParseHex("99108ad8ed9bb6274d3980bab5a85c048f0950c8"));
    BOOST_CHECK_MESSAGE( filter.contains(ParseHex("99108ad8ed9bb6274d3980bab5a85c048f0950c8")), "Bloom filter doesn't contain just-inserted object!");
    // One bit different in first byte
    BOOST_CHECK_MESSAGE(!filter.contains(ParseHex("19108ad8ed9bb6274d3980bab5a85c048f0950c8")), "Bloom filter contains something it shouldn't!");

    filter.insert(ParseHex("b5a2c786d9ef4658287ced5914b37a1b4aa32eee"));
    BOOST_CHECK_MESSAGE(filter.contains(ParseHex("b5a2c786d9ef4658287ced5914b37a1b4aa32eee")), "Bloom filter doesn't contain just-inserted object (2)!");

    filter.insert(ParseHex("b9300670b4c5366e95b2699e8b18bc75e5f729c5"));
    BOOST_CHECK_MESSAGE(filter.contains(ParseHex("b9300670b4c5366e95b2699e8b18bc75e5f729c5")), "Bloom filter doesn't contain just-inserted object (3)!");

    DataStream stream{};
    stream << filter;

    std::vector<uint8_t> expected = ParseHex("03614e9b050000000000000001");
    auto result{MakeUCharSpan(stream)};

    BOOST_CHECK_EQUAL_COLLECTIONS(result.begin(), result.end(), expected.begin(), expected.end());

    BOOST_CHECK_MESSAGE( filter.contains(ParseHex("99108ad8ed9bb6274d3980bab5a85c048f0950c8")), "Bloom filter doesn't contain just-inserted object!");
}

BOOST_AUTO_TEST_CASE(bloom_create_insert_serialize_with_tweak)
{
    // Same test as bloom_create_insert_serialize, but we add a nTweak of 100
    CBloomFilter filter(3, 0.01, 2147483649UL, BLOOM_UPDATE_ALL);

    filter.insert(ParseHex("99108ad8ed9bb6274d3980bab5a85c048f0950c8"));
    BOOST_CHECK_MESSAGE( filter.contains(ParseHex("99108ad8ed9bb6274d3980bab5a85c048f0950c8")), "Bloom filter doesn't contain just-inserted object!");
    // One bit different in first byte
    BOOST_CHECK_MESSAGE(!filter.contains(ParseHex("19108ad8ed9bb6274d3980bab5a85c048f0950c8")), "Bloom filter contains something it shouldn't!");

    filter.insert(ParseHex("b5a2c786d9ef4658287ced5914b37a1b4aa32eee"));
    BOOST_CHECK_MESSAGE(filter.contains(ParseHex("b5a2c786d9ef4658287ced5914b37a1b4aa32eee")), "Bloom filter doesn't contain just-inserted object (2)!");

    filter.insert(ParseHex("b9300670b4c5366e95b2699e8b18bc75e5f729c5"));
    BOOST_CHECK_MESSAGE(filter.contains(ParseHex("b9300670b4c5366e95b2699e8b18bc75e5f729c5")), "Bloom filter doesn't contain just-inserted object (3)!");

    DataStream stream{};
    stream << filter;

    std::vector<uint8_t> expected = ParseHex("03ce4299050000000100008001");
    auto result{MakeUCharSpan(stream)};

    BOOST_CHECK_EQUAL_COLLECTIONS(result.begin(), result.end(), expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(bloom_create_insert_key)
{
    std::string strSecret = std::string("7qTqvAq8VFPJqUHbPHpNaTdWMawxLkcbbfTCbS9R7YaXv6RKJev");
    CKey key = DecodeSecret(strSecret);
    BOOST_REQUIRE(key.IsValid());
    CPubKey pubkey = key.GetPubKey();
    std::vector<unsigned char> vchPubKey(pubkey.begin(), pubkey.end());

    CBloomFilter filter(2, 0.001, 0, BLOOM_UPDATE_ALL);
    filter.insert(vchPubKey);
    uint160 hash = pubkey.GetID();
    filter.insert(hash);

    DataStream stream{};
    stream << filter;

    std::vector<unsigned char> expected = ParseHex("038fc16b080000000000000001");
    auto result{MakeUCharSpan(stream)};

    BOOST_CHECK_EQUAL_COLLECTIONS(result.begin(), result.end(), expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(bloom_match)
{
    // PirateCash mainnet transaction at height 1940440 (a70b49bc41af3184d83d550f1159b8bba710edab6204b05a7595b42a3fbf6e2d)
    CDataStream stream(ParseHex("040000000194ca1e0f6e5114717d998a8441ea87a2e932ee5a55ac92e5d3ae13417208e505000000006a47304402207ba70b692bd464b90cb2425e04c352d83f7f83999103af5a0a62a6d7777df12b02201b53b65a07de5aad9eb33acdfa8e12c2a2c765c598b7434622c73b827ddd6eb901210328dd86eb48a30e0b3f47428dbc3e86f2c35035c78cb8e37fbac37ea1284cea02feffffff0200e1f505000000001976a914c5ef50a5eec8b3f04017a0e841fb68de2a5ad46b88ac93401c75100000001976a91426dbf4cd4b778ede5fe9ba686e38b5f41f6cfb6588acd79b1d00"), SER_DISK, CLIENT_VERSION);
    CTransaction tx(deserialize, stream);

    // Its spending transaction at height 1941093 (f49e7932255d402c6d391df8f879479d34e99c14a8815a7e83a790899d990b98)
    unsigned char ch[] = {0x04, 0x00, 0x00, 0x00, 0x01, 0x2d, 0x6e, 0xbf, 0x3f, 0x2a, 0xb4, 0x95, 0x75, 0x5a, 0xb0, 0x04, 0x62, 0xab, 0xed, 0x10, 0xa7, 0xbb, 0xb8, 0x59, 0x11, 0x0f, 0x55, 0x3d, 0xd8, 0x84, 0x31, 0xaf, 0x41, 0xbc, 0x49, 0x0b, 0xa7, 0x01, 0x00, 0x00, 0x00, 0x6a, 0x47, 0x30, 0x44, 0x02, 0x20, 0x59, 0x7b, 0x0b, 0x24, 0xa0, 0xf9, 0x12, 0x82, 0xb8, 0x47, 0xa3, 0x7c, 0x91, 0x1a, 0xae, 0x37, 0x02, 0x1a, 0x5a, 0xc0, 0x67, 0xb9, 0x2b, 0xd9, 0xab, 0xc1, 0xa5, 0x18, 0xf4, 0x0b, 0xdd, 0xd0, 0x02, 0x20, 0x32, 0xcd, 0xeb, 0x3d, 0x27, 0xe7, 0x62, 0x3e, 0xd8, 0xaa, 0x6f, 0x46, 0xc1, 0x4e, 0xea, 0xbb, 0xb0, 0x5f, 0x18, 0xfe, 0x13, 0xa0, 0xe1, 0x79, 0x3c, 0x14, 0x12, 0x8d, 0x8a, 0xb6, 0xc4, 0xcb, 0x01, 0x21, 0x03, 0xe1, 0xb0, 0x34, 0x07, 0xb0, 0xaa, 0x52, 0x74, 0xdb, 0xb1, 0xb9, 0x57, 0x08, 0x59, 0xab, 0x02, 0x3b, 0xee, 0x49, 0x2d, 0xf8, 0xf0, 0x49, 0x9c, 0x6c, 0xbc, 0x7a, 0x17, 0x4f, 0x23, 0xea, 0x52, 0xfe, 0xff, 0xff, 0xff, 0x01, 0xd4, 0x3f, 0x1c, 0x75, 0x10, 0x00, 0x00, 0x00, 0x19, 0x76, 0xa9, 0x14, 0xcf, 0x3a, 0xc4, 0x13, 0x07, 0xa9, 0x9d, 0xa5, 0x4b, 0xd5, 0xac, 0x75, 0x3e, 0xe0, 0xe5, 0xfc, 0x82, 0x48, 0x2c, 0x29, 0x88, 0xac, 0x64, 0x9e, 0x1d, 0x00, 0x00};
    std::vector<unsigned char> vch(ch, ch + sizeof(ch) -1);
    CDataStream spendStream(vch, SER_DISK, CLIENT_VERSION);
    CTransaction spendingTx(deserialize, spendStream);

    CBloomFilter filter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    filter.insert(uint256S("0xa70b49bc41af3184d83d550f1159b8bba710edab6204b05a7595b42a3fbf6e2d"));
    BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(tx), "Simple Bloom filter didn't match tx hash");

    filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    // byte-reversed tx hash
    filter.insert(ParseHex("2d6ebf3f2ab495755ab00462abed10a7bbb859110f553dd88431af41bc490ba7"));
    BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(tx), "Simple Bloom filter didn't match manually serialized tx hash");

    filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    filter.insert(ParseHex("304402207ba70b692bd464b90cb2425e04c352d83f7f83999103af5a0a62a6d7777df12b02201b53b65a07de5aad9eb33acdfa8e12c2a2c765c598b7434622c73b827ddd6eb901"));
    BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(tx), "Simple Bloom filter didn't match input signature");

    filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    filter.insert(ParseHex("0328dd86eb48a30e0b3f47428dbc3e86f2c35035c78cb8e37fbac37ea1284cea02"));
    BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(tx), "Simple Bloom filter didn't match input pub key");

    filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    filter.insert(ParseHex("26dbf4cd4b778ede5fe9ba686e38b5f41f6cfb65"));
    BOOST_CHECK_MESSAGE(!filter.IsRelevantAndUpdate(spendingTx), "Simple Bloom filter matched spending transaction before adding output");
    BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(tx), "Simple Bloom filter didn't match output address");
    BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(spendingTx), "Simple Bloom filter didn't add output");

    filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    filter.insert(ParseHex("c5ef50a5eec8b3f04017a0e841fb68de2a5ad46b"));
    BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(tx), "Simple Bloom filter didn't match output address");

    filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    filter.insert(COutPoint(uint256S("0x05e508724113aed3e592ac555aee32e9a287ea41848a997d7114516e0f1eca94"), 0));
    BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(tx), "Simple Bloom filter didn't match COutPoint");

    filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    COutPoint prevOutPoint(uint256S("0x05e508724113aed3e592ac555aee32e9a287ea41848a997d7114516e0f1eca94"), 0);
    {
        std::vector<unsigned char> data(32 + sizeof(unsigned int));
        memcpy(data.data(), prevOutPoint.hash.begin(), 32);
        memcpy(data.data()+32, &prevOutPoint.n, sizeof(unsigned int));
        filter.insert(data);
    }
    BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(tx), "Simple Bloom filter didn't match manually serialized COutPoint");

    filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    filter.insert(uint256S("00000009e784f32f62ef849763d4f45b98e07ba658647343b915ff832b110436"));
    BOOST_CHECK_MESSAGE(!filter.IsRelevantAndUpdate(tx), "Simple Bloom filter matched random tx hash");

    filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    filter.insert(ParseHex("0000006d2965547608b9e15d9032a7b9d64fa431"));
    BOOST_CHECK_MESSAGE(!filter.IsRelevantAndUpdate(tx), "Simple Bloom filter matched random address");

    filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    filter.insert(COutPoint(uint256S("0x05e508724113aed3e592ac555aee32e9a287ea41848a997d7114516e0f1eca94"), 1));
    BOOST_CHECK_MESSAGE(!filter.IsRelevantAndUpdate(tx), "Simple Bloom filter matched COutPoint for an output we didn't care about");

    filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    filter.insert(COutPoint(uint256S("0x000000724113aed3e592ac555aee32e9a287ea41848a997d7114516e0f1eca94"), 0));
    BOOST_CHECK_MESSAGE(!filter.IsRelevantAndUpdate(tx), "Simple Bloom filter matched COutPoint for an output we didn't care about");
}

BOOST_AUTO_TEST_CASE(dip2_bloom_match)
{
    //TODO: Provide raw data for basic scheme as well
    bls::bls_legacy_scheme.store(true);
    // ProRegTx from testnet (txid: 39a1339d9bf26de701345beecc5de75a690bc9533741a3dbe90f2fd88b8ed461)
    CDataStream stream(ParseHex("0300010001c02142f16969d106f4cb144e17975bf9261b8e331354879c6d006e71494b3460000000006b483045022100d6eb9e25fc3215e6a4b5180a1957319d6658103566f0274452bd50d0eddedff202200824a00f582a29c9e3694d34e5525275cabd08c488a0c55b4350c6bfa4fb2cf8012102a8d6433a8f799a13d3495f4aa5b99bd288adca5f59e0c9609f15a221220241c5feffffff0121c89a3b000000001976a914e54445646929fac8b7d6c71715913af44324978488ac00000000fd12010100000000009b9054ff7839b940277b8eb8211570b2f16850ef729ee635e24d722fbc4a46230100000000000000000000000000ffffc6c74af14e1f891cbc8a94fa7fea64ca9994870dca0f75bbd0750efda51589f86e30cc2305e7388c01ce0309c19a182cf37bced97c7da72236f660c0a395e765e6e06962ecff5a69d7de359c348a574176c210c37a25d4ffd917866fb0a300001976a914e54445646929fac8b7d6c71715913af44324978488ac26d5a99c01521d1fca6299de576bae82f901fb56e4b797a945e876ac69068f36411f9cdb72a01b273a53bd916d8c90dd584bd3a1c01dd84fec84eb046f66a4e3b30d39ea3215293a035166b5072349ebc08efd30c2c73ebd023d920f1db0aef91a4e"), SER_DISK, CLIENT_VERSION);
    CTransaction proregtx(deserialize, stream);
    // ProUpServTx from testnet (txid: 0c9627a054784f207db34c311b06e8bb797c4a120a236dbb02b51268dd186d39)
    stream = CDataStream(ParseHex("030002000161d48e8bd82f0fe9dba3413753c90b695ae75dccee5b3401e76df29b9d33a139000000006a473044022036dfd41268e3a387b909d9d1cf61e4d5e97b6b37d5951e82c4ca4fed419b4b6a02203777d818319897a11cf4cd8b4c6495c5c3a5f96fcad6d3a0596431b1146b53c6012102a8d6433a8f799a13d3495f4aa5b99bd288adca5f59e0c9609f15a221220241c5feffffff01a1c69a3b000000001976a914e54445646929fac8b7d6c71715913af44324978488ac00000000b5010061d48e8bd82f0fe9dba3413753c90b695ae75dccee5b3401e76df29b9d33a13900000000000000000000ffffc6c74af14e1f000ccf39b295938f4021fc16b23e4eedbd64a6c9a5aaae6bc9ca085db4bc29ff911612dc4544fc470415cdd4be5659fee6aa7d84c3572c712f5904b2e5d38cb217762717d88e4e1a956a9e82ce132e2b9b144e26cb3ff1e53675ede2769d99f46796ea0b8cbf33b78c15fe9a437dc4d1131ce2af8fd2ed5b306b326f9fcffcd416"), SER_DISK, CLIENT_VERSION);
    CTransaction proupservtx(deserialize, stream);
    // ProUpRegTx from testnet (txid: 3037bf4e45d1a9d57891ea8baa72b38ad6323bd28945b11325bdd33ebea049b3)
    stream = CDataStream(ParseHex("0300030001396d18dd6812b502bb6d230a124a7c79bbe8061b314cb37d204f7854a027960c000000006a47304402203aefa0e1a08469050edc1e5da6e24f783162bebc15cc006f717097e56e07fa0b02207c7f47d113355b095c7edd2408d0554aa6b87187bf95652c516fc7747720fb19012102a8d6433a8f799a13d3495f4aa5b99bd288adca5f59e0c9609f15a221220241c5feffffff01f2c49a3b000000001976a914e54445646929fac8b7d6c71715913af44324978488ac00000000e4010061d48e8bd82f0fe9dba3413753c90b695ae75dccee5b3401e76df29b9d33a13900000efda51589f86e30cc2305e7388c01ce0309c19a182cf37bced97c7da72236f660c0a395e765e6e06962ecff5a69d7de359c348a574176c210c37a25d4ffd917866fb0a31976a914e54445646929fac8b7d6c71715913af44324978488aca8141db635af714f32c7415b298937e171c6bb8a3c8627852ae2b69917ece86a411fa51861025bb5ff4ba4ad3be6090c4ba76b1671d70799ed6882c57bcfeaf27cbd558068e9b5fc1553abce8822fcee63e8f6fbb06ad5b753d47e794bfacadbde3f"), SER_DISK, CLIENT_VERSION);
    CTransaction proupregtx(deserialize, stream);
    // ProUpRevTx from testnet (txid: 70c41a5fea07a80c1297771c43eb58b8f52222b971f61f5c7c2030ec0baf7f9a)
    stream = CDataStream(ParseHex("0300040001b349a0be3ed3bd2513b14589d23b32d68ab372aa8bea9178d5a9d1454ebf3730000000006a4730440220428d87daf3b0ddca9656d59592000f9930a88162c0b017ca460c69c33b76bda202200276a521d94acc57e010262aa1d7d016cebbe7a9806d26dbebb69102ce9cf7b2012102a8d6433a8f799a13d3495f4aa5b99bd288adca5f59e0c9609f15a221220241c5feffffff0183c39a3b000000001976a914e54445646929fac8b7d6c71715913af44324978488ac00000000a4010061d48e8bd82f0fe9dba3413753c90b695ae75dccee5b3401e76df29b9d33a1390000c024ffd34d15686f7a376ecdf45800ddcebf4eb966fd3123cf9450515a38881a0de852c97297bbd9cedb79280f3f1e31b092904a8870274a5a0e52de2ae1dc0bad74851dc0a1e4fe4c274535a2422d3e1510a7be2fbecaae73128e8eb2f336382fb376d2f82273a72960980972b02e3ecf895c00187e0cbe735dc44cb3d97711"), SER_DISK, CLIENT_VERSION);
    CTransaction prouprevtx(deserialize, stream);

    // check collateral outpoint match in ProRegTx
    CBloomFilter filter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    filter.insert(COutPoint(uint256S("0x23464abc2f724de235e69e72ef5068f1b2701521b88e7b2740b93978ff54909b"), 1));
    BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(proregtx), "Bloom filter didn't match collateral outpoint in ProRegTx");
    // check owner keyid match in ProRegTx
    filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    filter.insert(ParseHex("891cbc8a94fa7fea64ca9994870dca0f75bbd075"));
    BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(proregtx), "Bloom filter didn't match Owner keyid in ProRegTx");
    // check voting keyid match in ProRegTx
    filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    filter.insert(ParseHex("359c348a574176c210c37a25d4ffd917866fb0a3"));
    BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(proregtx), "Bloom filter didn't match Voting keyid in ProRegTx");
    // check scriptPayout match in ProRegTx
    filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    filter.insert(ParseHex("e54445646929fac8b7d6c71715913af443249784"));
    BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(proregtx), "Bloom filter didn't match scriptPayout in ProRegTx");

    // check proTxHash match in ProUpServTx
    filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    filter.insert(uint256S("0x39a1339d9bf26de701345beecc5de75a690bc9533741a3dbe90f2fd88b8ed461"));
    BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(proupservtx), "Bloom filter didn't match proTxHash in ProUpServTx");
    // check scriptOperatorPayout match in ProUpServTx
    filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    filter.insert(ParseHex("e54445646929fac8b7d6c71715913af443249784"));
    BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(proupservtx), "Bloom filter didn't match scriptOperatorPayout in ProUpServTx");

    // check proTxHash match in ProUpRegTx
    filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    filter.insert(uint256S("0x39a1339d9bf26de701345beecc5de75a690bc9533741a3dbe90f2fd88b8ed461"));
    BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(proupregtx), "Bloom filter didn't match proTxHash in ProUpRegTx");
    // check voting keyid match in ProUpRegTx
    filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    filter.insert(ParseHex("359c348a574176c210c37a25d4ffd917866fb0a3"));
    BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(proupregtx), "Bloom filter didn't match Voting keyid in ProUpRegTx");
    // check scriptPayout match in ProUpRegTx
    filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    filter.insert(ParseHex("e54445646929fac8b7d6c71715913af443249784"));
    BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(proupregtx), "Bloom filter didn't match scriptPayout in ProUpRegTx");

    // check proTxHash match in ProUpRevTx
    filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    filter.insert(uint256S("0x39a1339d9bf26de701345beecc5de75a690bc9533741a3dbe90f2fd88b8ed461"));
    BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(prouprevtx), "Bloom filter didn't match proTxHash in ProUpRevTx");

    // check filter is not matching if it doesn't contain relative data
    filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    // extend real data with additional byte
    filter.insert(uint256S("0x39a1339d9bf26de701345beecc5de75a690bc9533741a3dbe90f2fd88b8ed46100"));
    filter.insert(ParseHex("e54445646929fac8b7d6c71715913af44324978400"));
    filter.insert(ParseHex("359c348a574176c210c37a25d4ffd917866fb0a300"));
    filter.insert(uint256S("0x39a1339d9bf26de701345beecc5de75a690bc9533741a3dbe90f2fd88b8ed46100"));
    BOOST_CHECK_MESSAGE(!filter.IsRelevantAndUpdate(prouprevtx), "Bloom filter match unrelated data");
    BOOST_CHECK_MESSAGE(!filter.IsRelevantAndUpdate(proupregtx), "Bloom filter match unrelated data");
}

BOOST_AUTO_TEST_CASE(dip2_bloom_update)
{
    //TODO: Provide raw data for basic scheme as well
    bls::bls_legacy_scheme.store(true);
    // ProRegTx from testnet (txid: 39a1339d9bf26de701345beecc5de75a690bc9533741a3dbe90f2fd88b8ed461)
    CDataStream stream(ParseHex("0300010001c02142f16969d106f4cb144e17975bf9261b8e331354879c6d006e71494b3460000000006b483045022100d6eb9e25fc3215e6a4b5180a1957319d6658103566f0274452bd50d0eddedff202200824a00f582a29c9e3694d34e5525275cabd08c488a0c55b4350c6bfa4fb2cf8012102a8d6433a8f799a13d3495f4aa5b99bd288adca5f59e0c9609f15a221220241c5feffffff0121c89a3b000000001976a914e54445646929fac8b7d6c71715913af44324978488ac00000000fd12010100000000009b9054ff7839b940277b8eb8211570b2f16850ef729ee635e24d722fbc4a46230100000000000000000000000000ffffc6c74af14e1f891cbc8a94fa7fea64ca9994870dca0f75bbd0750efda51589f86e30cc2305e7388c01ce0309c19a182cf37bced97c7da72236f660c0a395e765e6e06962ecff5a69d7de359c348a574176c210c37a25d4ffd917866fb0a300001976a914e54445646929fac8b7d6c71715913af44324978488ac26d5a99c01521d1fca6299de576bae82f901fb56e4b797a945e876ac69068f36411f9cdb72a01b273a53bd916d8c90dd584bd3a1c01dd84fec84eb046f66a4e3b30d39ea3215293a035166b5072349ebc08efd30c2c73ebd023d920f1db0aef91a4e"), SER_DISK, CLIENT_VERSION);
    CTransaction proregtx(deserialize, stream);
    // ProUpServTx from testnet (txid: 0c9627a054784f207db34c311b06e8bb797c4a120a236dbb02b51268dd186d39)
    stream = CDataStream(ParseHex("030002000161d48e8bd82f0fe9dba3413753c90b695ae75dccee5b3401e76df29b9d33a139000000006a473044022036dfd41268e3a387b909d9d1cf61e4d5e97b6b37d5951e82c4ca4fed419b4b6a02203777d818319897a11cf4cd8b4c6495c5c3a5f96fcad6d3a0596431b1146b53c6012102a8d6433a8f799a13d3495f4aa5b99bd288adca5f59e0c9609f15a221220241c5feffffff01a1c69a3b000000001976a914e54445646929fac8b7d6c71715913af44324978488ac00000000b5010061d48e8bd82f0fe9dba3413753c90b695ae75dccee5b3401e76df29b9d33a13900000000000000000000ffffc6c74af14e1f000ccf39b295938f4021fc16b23e4eedbd64a6c9a5aaae6bc9ca085db4bc29ff911612dc4544fc470415cdd4be5659fee6aa7d84c3572c712f5904b2e5d38cb217762717d88e4e1a956a9e82ce132e2b9b144e26cb3ff1e53675ede2769d99f46796ea0b8cbf33b78c15fe9a437dc4d1131ce2af8fd2ed5b306b326f9fcffcd416"), SER_DISK, CLIENT_VERSION);
    CTransaction proupservtx(deserialize, stream);
    // ProUpRegTx from testnet (txid: 3037bf4e45d1a9d57891ea8baa72b38ad6323bd28945b11325bdd33ebea049b3)
    stream = CDataStream(ParseHex("0300030001396d18dd6812b502bb6d230a124a7c79bbe8061b314cb37d204f7854a027960c000000006a47304402203aefa0e1a08469050edc1e5da6e24f783162bebc15cc006f717097e56e07fa0b02207c7f47d113355b095c7edd2408d0554aa6b87187bf95652c516fc7747720fb19012102a8d6433a8f799a13d3495f4aa5b99bd288adca5f59e0c9609f15a221220241c5feffffff01f2c49a3b000000001976a914e54445646929fac8b7d6c71715913af44324978488ac00000000e4010061d48e8bd82f0fe9dba3413753c90b695ae75dccee5b3401e76df29b9d33a13900000efda51589f86e30cc2305e7388c01ce0309c19a182cf37bced97c7da72236f660c0a395e765e6e06962ecff5a69d7de359c348a574176c210c37a25d4ffd917866fb0a31976a914e54445646929fac8b7d6c71715913af44324978488aca8141db635af714f32c7415b298937e171c6bb8a3c8627852ae2b69917ece86a411fa51861025bb5ff4ba4ad3be6090c4ba76b1671d70799ed6882c57bcfeaf27cbd558068e9b5fc1553abce8822fcee63e8f6fbb06ad5b753d47e794bfacadbde3f"), SER_DISK, CLIENT_VERSION);
    CTransaction proupregtx(deserialize, stream);
    // ProUpRevTx from testnet (txid: 70c41a5fea07a80c1297771c43eb58b8f52222b971f61f5c7c2030ec0baf7f9a)
    stream = CDataStream(ParseHex("0300040001b349a0be3ed3bd2513b14589d23b32d68ab372aa8bea9178d5a9d1454ebf3730000000006a4730440220428d87daf3b0ddca9656d59592000f9930a88162c0b017ca460c69c33b76bda202200276a521d94acc57e010262aa1d7d016cebbe7a9806d26dbebb69102ce9cf7b2012102a8d6433a8f799a13d3495f4aa5b99bd288adca5f59e0c9609f15a221220241c5feffffff0183c39a3b000000001976a914e54445646929fac8b7d6c71715913af44324978488ac00000000a4010061d48e8bd82f0fe9dba3413753c90b695ae75dccee5b3401e76df29b9d33a1390000c024ffd34d15686f7a376ecdf45800ddcebf4eb966fd3123cf9450515a38881a0de852c97297bbd9cedb79280f3f1e31b092904a8870274a5a0e52de2ae1dc0bad74851dc0a1e4fe4c274535a2422d3e1510a7be2fbecaae73128e8eb2f336382fb376d2f82273a72960980972b02e3ecf895c00187e0cbe735dc44cb3d97711"), SER_DISK, CLIENT_VERSION);
    CTransaction prouprevtx(deserialize, stream);

    // if ProRegTx matches, all related pro txes match too
    CBloomFilter filter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    filter.insert(COutPoint(uint256S("0x23464abc2f724de235e69e72ef5068f1b2701521b88e7b2740b93978ff54909b"), 1));
    BOOST_CHECK_MESSAGE(!filter.IsRelevantAndUpdate(proupservtx), "Bloom filter matches without update");
    BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(proregtx), "Bloom filter didn't match collateral outpoint in ProRegTx");
    BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(proupservtx), "Bloom filter wasn't updated with proregtx hash");

    // if ProUpRegTx matches, all related pro txes match too
    filter = CBloomFilter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    filter.insert(ParseHex("359c348a574176c210c37a25d4ffd917866fb0a3"));
    BOOST_CHECK_MESSAGE(!filter.IsRelevantAndUpdate(proupservtx), "Bloom filter matches without update");
    BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(proupregtx), "Bloom filter didn't match Voting keyid in ProUpRegTx");
    BOOST_CHECK_MESSAGE(filter.IsRelevantAndUpdate(proupservtx), "Bloom filter wasn't updated with proregtx hash");
}

BOOST_AUTO_TEST_CASE(merkle_block_1)
{
    CBlock block = getBlock13b8a();
    CBloomFilter filter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    // Match the last transaction
    filter.insert(uint256S("0x65f25295455fa5b41d79ca2b86c3bec20924c0a6d9d0b97c43345d52c2701e74"));

    CMerkleBlock merkleBlock(block, filter);
    BOOST_CHECK_EQUAL(merkleBlock.header.GetHash().GetHex(), block.GetHash().GetHex());

    BOOST_CHECK_EQUAL(merkleBlock.vMatchedTxn.size(), 1U);
    std::pair<unsigned int, uint256> pair = merkleBlock.vMatchedTxn[0];

    BOOST_CHECK(merkleBlock.vMatchedTxn[0].second == uint256S("0x65f25295455fa5b41d79ca2b86c3bec20924c0a6d9d0b97c43345d52c2701e74"));
    BOOST_CHECK(merkleBlock.vMatchedTxn[0].first == 8);

    std::vector<uint256> vMatched;
    std::vector<unsigned int> vIndex;
    BOOST_CHECK(merkleBlock.txn.ExtractMatches(vMatched, vIndex) == block.hashMerkleRoot);
    BOOST_CHECK(vMatched.size() == merkleBlock.vMatchedTxn.size());
    for (unsigned int i = 0; i < vMatched.size(); i++)
        BOOST_CHECK(vMatched[i] == merkleBlock.vMatchedTxn[i].second);

    // Also match the 8th transaction
    filter.insert(uint256S("0x4730af5654d394a161b7c7c320207e919dabea32f1982135030746b04afd2db3"));
    merkleBlock = CMerkleBlock(block, filter);
    BOOST_CHECK(merkleBlock.header.GetHash() == block.GetHash());

    BOOST_CHECK(merkleBlock.vMatchedTxn.size() == 2);

    BOOST_CHECK(merkleBlock.vMatchedTxn[1] == pair);

    BOOST_CHECK(merkleBlock.vMatchedTxn[0].second == uint256S("0x4730af5654d394a161b7c7c320207e919dabea32f1982135030746b04afd2db3"));
    BOOST_CHECK(merkleBlock.vMatchedTxn[0].first == 7);

    BOOST_CHECK(merkleBlock.txn.ExtractMatches(vMatched, vIndex) == block.hashMerkleRoot);
    BOOST_CHECK(vMatched.size() == merkleBlock.vMatchedTxn.size());
    for (unsigned int i = 0; i < vMatched.size(); i++)
        BOOST_CHECK(vMatched[i] == merkleBlock.vMatchedTxn[i].second);
}

BOOST_AUTO_TEST_CASE(merkle_block_2)
{
    // Synthetic fixture adapted from Bitcoin block (000000005a4ded781e667e06ceefafb71410b511fe0d5adc3e5a27ecbec34ae6)
    // With 4 txes; transaction timestamps come from its header.
    CBlock block;
    CDataStream stream(ParseHex("0100000075616236cc2126035fadb38deb65b9102cc2c41c09cdf29fc051906800000000dffc4abed0a0e8ffea6dad68b8f355b10d4af988682379b9a8845f964ef13da2d4d26e49ffff001d00f0a4410401000000d4d26e49010000000000000000000000000000000000000000000000000000000000000000ffffffff0804ffff001d029105ffffffff0100f2052a010000004341046d8709a041d34357697dfcb30a9d05900a6294078012bf3bb09c6f9b525f1d16d5503d7905db1ada9501446ea00728668fc5719aa80be2fdfc8a858a4dbdd4fbac0000000001000000d4d26e490255605dc6f5c3dc148b6da58442b0b2cd422be385eab2ebea4119ee9c268d28350000000049483045022100aa46504baa86df8a33b1192b1b9367b4d729dc41e389f2c04f3e5c7f0559aae702205e82253a54bf5c4f65b7428551554b2045167d6d206dfe6a2e198127d3f7df1501ffffffff55605dc6f5c3dc148b6da58442b0b2cd422be385eab2ebea4119ee9c268d2835010000004847304402202329484c35fa9d6bb32a55a70c0982f606ce0e3634b69006138683bcd12cbb6602200c28feb1e2555c3210f1dddb299738b4ff8bbe9667b68cb8764b5ac17b7adf0001ffffffff0200e1f505000000004341046a0765b5865641ce08dd39690aade26dfbf5511430ca428a3089261361cef170e3929a68aee3d8d4848b0c5111b0a37b82b86ad559fd2a745b44d8e8d9dfdc0cac00180d8f000000004341044a656f065871a353f216ca26cef8dde2f03e8c16202d2e8ad769f02032cb86a5eb5e56842e92e19141d60a01928f8dd2c875a390f67c1f6c94cfc617c0ea45afac0000000001000000d4d26e4902545561269be66da54114746d14a564ecb9d799d5e3e3c8c105c17ee46ac75a88000000004847304402205d6058484157235b06028c30736c15613a28bdb768ee628094ca8b0030d4d6eb0220328789c9a2ec27ddaec0ad5ef58efded42e6ea17c2e1ce838f3d6913f5e95db601ffffffff545561269be66da54114746d14a564ecb9d799d5e3e3c8c105c17ee46ac75a88010000004a493046022100c45af050d3cea806cedd0ab22520c53ebe63b987b8954146cdca42487b84bdd6022100b9b027716a6b59e640da50a864d6dd8a0ef24c76ce62391fa3eabaf4d2886d2d01ffffffff0200e1f505000000004341046a0765b5865641ce08dd39690aade26dfbf5511430ca428a3089261361cef170e3929a68aee3d8d4848b0c5111b0a37b82b86ad559fd2a745b44d8e8d9dfdc0cac00180d8f000000004341046a0765b5865641ce08dd39690aade26dfbf5511430ca428a3089261361cef170e3929a68aee3d8d4848b0c5111b0a37b82b86ad559fd2a745b44d8e8d9dfdc0cac0000000001000000d4d26e490207a7a303ee2f4f71b178fbd3e34f27a4a0c85f7ec098d8ab2a6339b3f85e07e40000000048473044022016e7a727a061ea2254a6c358376aaa617ac537eb836c77d646ebda4c748aac8b0220192ce28bf9f2c06a6467e6531e27648d2b3e2e2bae85159c9242939840295ba501ffffffff07a7a303ee2f4f71b178fbd3e34f27a4a0c85f7ec098d8ab2a6339b3f85e07e4010000004a493046022100b7a1a755588d4190118936e15cd217d133b0e4a53c3c15924010d5648d8925c9022100aaef031874db2114f2d869ac2de4ae53908fbfea5b2b1862e181626bb9005c9f01ffffffff0200e1f505000000004341044a656f065871a353f216ca26cef8dde2f03e8c16202d2e8ad769f02032cb86a5eb5e56842e92e19141d60a01928f8dd2c875a390f67c1f6c94cfc617c0ea45afac00180d8f000000004341046a0765b5865641ce08dd39690aade26dfbf5511430ca428a3089261361cef170e3929a68aee3d8d4848b0c5111b0a37b82b86ad559fd2a745b44d8e8d9dfdc0cac00000000"), SER_NETWORK, PROTOCOL_VERSION);
    stream >> block;

    CBloomFilter filter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    // Match the first transaction
    filter.insert(uint256S("0x7ef36fd64f796775505050f8e4768d032a269a597c121ae99983f63adecf02f3"));

    CMerkleBlock merkleBlock(block, filter);
    BOOST_CHECK(merkleBlock.header.GetHash() == block.GetHash());

    BOOST_CHECK(merkleBlock.vMatchedTxn.size() == 1);
    std::pair<unsigned int, uint256> pair = merkleBlock.vMatchedTxn[0];

    BOOST_CHECK(merkleBlock.vMatchedTxn[0].second == uint256S("0x7ef36fd64f796775505050f8e4768d032a269a597c121ae99983f63adecf02f3"));
    BOOST_CHECK(merkleBlock.vMatchedTxn[0].first == 0);

    std::vector<uint256> vMatched;
    std::vector<unsigned int> vIndex;
    BOOST_CHECK(merkleBlock.txn.ExtractMatches(vMatched, vIndex) == block.hashMerkleRoot);
    BOOST_CHECK(vMatched.size() == merkleBlock.vMatchedTxn.size());
    for (unsigned int i = 0; i < vMatched.size(); i++)
        BOOST_CHECK(vMatched[i] == merkleBlock.vMatchedTxn[i].second);

    // Match an output from the second transaction (the pubkey for address 1DZTzaBHUDM7T3QvUKBz4qXMRpkg8jsfB5)
    // This should match the third transaction because it spends the output matched
    // It also matches the fourth transaction, which spends to the pubkey again
    filter.insert(ParseHex("044a656f065871a353f216ca26cef8dde2f03e8c16202d2e8ad769f02032cb86a5eb5e56842e92e19141d60a01928f8dd2c875a390f67c1f6c94cfc617c0ea45af"));

    merkleBlock = CMerkleBlock(block, filter);
    BOOST_CHECK(merkleBlock.header.GetHash() == block.GetHash());

    BOOST_CHECK(merkleBlock.vMatchedTxn.size() == 4);

    BOOST_CHECK(pair == merkleBlock.vMatchedTxn[0]);

    BOOST_CHECK(merkleBlock.vMatchedTxn[1].second == uint256S("0x885ac76ae47ec105c1c8e3e3d599d7b9ec64a5146d741441a56de69b26615554"));
    BOOST_CHECK(merkleBlock.vMatchedTxn[1].first == 1);

    BOOST_CHECK(merkleBlock.vMatchedTxn[2].second == uint256S("0xe4075ef8b339632aabd898c07e5fc8a0a4274fe3d3fb78b1714f2fee03a3a707"));
    BOOST_CHECK(merkleBlock.vMatchedTxn[2].first == 2);

    BOOST_CHECK(merkleBlock.vMatchedTxn[3].second == uint256S("0x56fb31578806d2604375dc815a877dff3145c032e2568f63afb62d82c0371ecd"));
    BOOST_CHECK(merkleBlock.vMatchedTxn[3].first == 3);

    BOOST_CHECK(merkleBlock.txn.ExtractMatches(vMatched, vIndex) == block.hashMerkleRoot);
    BOOST_CHECK(vMatched.size() == merkleBlock.vMatchedTxn.size());
    for (unsigned int i = 0; i < vMatched.size(); i++)
        BOOST_CHECK(vMatched[i] == merkleBlock.vMatchedTxn[i].second);
}

BOOST_AUTO_TEST_CASE(merkle_block_2_with_update_none)
{
    // Synthetic fixture adapted from Bitcoin block (000000005a4ded781e667e06ceefafb71410b511fe0d5adc3e5a27ecbec34ae6)
    // With 4 txes; transaction timestamps come from its header.
    CBlock block;
    CDataStream stream(ParseHex("0100000075616236cc2126035fadb38deb65b9102cc2c41c09cdf29fc051906800000000dffc4abed0a0e8ffea6dad68b8f355b10d4af988682379b9a8845f964ef13da2d4d26e49ffff001d00f0a4410401000000d4d26e49010000000000000000000000000000000000000000000000000000000000000000ffffffff0804ffff001d029105ffffffff0100f2052a010000004341046d8709a041d34357697dfcb30a9d05900a6294078012bf3bb09c6f9b525f1d16d5503d7905db1ada9501446ea00728668fc5719aa80be2fdfc8a858a4dbdd4fbac0000000001000000d4d26e490255605dc6f5c3dc148b6da58442b0b2cd422be385eab2ebea4119ee9c268d28350000000049483045022100aa46504baa86df8a33b1192b1b9367b4d729dc41e389f2c04f3e5c7f0559aae702205e82253a54bf5c4f65b7428551554b2045167d6d206dfe6a2e198127d3f7df1501ffffffff55605dc6f5c3dc148b6da58442b0b2cd422be385eab2ebea4119ee9c268d2835010000004847304402202329484c35fa9d6bb32a55a70c0982f606ce0e3634b69006138683bcd12cbb6602200c28feb1e2555c3210f1dddb299738b4ff8bbe9667b68cb8764b5ac17b7adf0001ffffffff0200e1f505000000004341046a0765b5865641ce08dd39690aade26dfbf5511430ca428a3089261361cef170e3929a68aee3d8d4848b0c5111b0a37b82b86ad559fd2a745b44d8e8d9dfdc0cac00180d8f000000004341044a656f065871a353f216ca26cef8dde2f03e8c16202d2e8ad769f02032cb86a5eb5e56842e92e19141d60a01928f8dd2c875a390f67c1f6c94cfc617c0ea45afac0000000001000000d4d26e4902545561269be66da54114746d14a564ecb9d799d5e3e3c8c105c17ee46ac75a88000000004847304402205d6058484157235b06028c30736c15613a28bdb768ee628094ca8b0030d4d6eb0220328789c9a2ec27ddaec0ad5ef58efded42e6ea17c2e1ce838f3d6913f5e95db601ffffffff545561269be66da54114746d14a564ecb9d799d5e3e3c8c105c17ee46ac75a88010000004a493046022100c45af050d3cea806cedd0ab22520c53ebe63b987b8954146cdca42487b84bdd6022100b9b027716a6b59e640da50a864d6dd8a0ef24c76ce62391fa3eabaf4d2886d2d01ffffffff0200e1f505000000004341046a0765b5865641ce08dd39690aade26dfbf5511430ca428a3089261361cef170e3929a68aee3d8d4848b0c5111b0a37b82b86ad559fd2a745b44d8e8d9dfdc0cac00180d8f000000004341046a0765b5865641ce08dd39690aade26dfbf5511430ca428a3089261361cef170e3929a68aee3d8d4848b0c5111b0a37b82b86ad559fd2a745b44d8e8d9dfdc0cac0000000001000000d4d26e490207a7a303ee2f4f71b178fbd3e34f27a4a0c85f7ec098d8ab2a6339b3f85e07e40000000048473044022016e7a727a061ea2254a6c358376aaa617ac537eb836c77d646ebda4c748aac8b0220192ce28bf9f2c06a6467e6531e27648d2b3e2e2bae85159c9242939840295ba501ffffffff07a7a303ee2f4f71b178fbd3e34f27a4a0c85f7ec098d8ab2a6339b3f85e07e4010000004a493046022100b7a1a755588d4190118936e15cd217d133b0e4a53c3c15924010d5648d8925c9022100aaef031874db2114f2d869ac2de4ae53908fbfea5b2b1862e181626bb9005c9f01ffffffff0200e1f505000000004341044a656f065871a353f216ca26cef8dde2f03e8c16202d2e8ad769f02032cb86a5eb5e56842e92e19141d60a01928f8dd2c875a390f67c1f6c94cfc617c0ea45afac00180d8f000000004341046a0765b5865641ce08dd39690aade26dfbf5511430ca428a3089261361cef170e3929a68aee3d8d4848b0c5111b0a37b82b86ad559fd2a745b44d8e8d9dfdc0cac00000000"), SER_NETWORK, PROTOCOL_VERSION);
    stream >> block;

    CBloomFilter filter(10, 0.000001, 0, BLOOM_UPDATE_NONE);
    // Match the first transaction
    filter.insert(uint256S("0x7ef36fd64f796775505050f8e4768d032a269a597c121ae99983f63adecf02f3"));

    CMerkleBlock merkleBlock(block, filter);
    BOOST_CHECK(merkleBlock.header.GetHash() == block.GetHash());

    BOOST_CHECK(merkleBlock.vMatchedTxn.size() == 1);
    std::pair<unsigned int, uint256> pair = merkleBlock.vMatchedTxn[0];

    BOOST_CHECK(merkleBlock.vMatchedTxn[0].second == uint256S("0x7ef36fd64f796775505050f8e4768d032a269a597c121ae99983f63adecf02f3"));
    BOOST_CHECK(merkleBlock.vMatchedTxn[0].first == 0);

    std::vector<uint256> vMatched;
    std::vector<unsigned int> vIndex;
    BOOST_CHECK(merkleBlock.txn.ExtractMatches(vMatched, vIndex) == block.hashMerkleRoot);
    BOOST_CHECK(vMatched.size() == merkleBlock.vMatchedTxn.size());
    for (unsigned int i = 0; i < vMatched.size(); i++)
        BOOST_CHECK(vMatched[i] == merkleBlock.vMatchedTxn[i].second);

    // Match an output from the second transaction (the pubkey for address 1DZTzaBHUDM7T3QvUKBz4qXMRpkg8jsfB5)
    // This should not match the third transaction though it spends the output matched
    // It will match the fourth transaction, which has another pay-to-pubkey output to the same address
    filter.insert(ParseHex("044a656f065871a353f216ca26cef8dde2f03e8c16202d2e8ad769f02032cb86a5eb5e56842e92e19141d60a01928f8dd2c875a390f67c1f6c94cfc617c0ea45af"));

    merkleBlock = CMerkleBlock(block, filter);
    BOOST_CHECK(merkleBlock.header.GetHash() == block.GetHash());

    BOOST_CHECK(merkleBlock.vMatchedTxn.size() == 3);

    BOOST_CHECK(pair == merkleBlock.vMatchedTxn[0]);

    BOOST_CHECK(merkleBlock.vMatchedTxn[1].second == uint256S("0x885ac76ae47ec105c1c8e3e3d599d7b9ec64a5146d741441a56de69b26615554"));
    BOOST_CHECK(merkleBlock.vMatchedTxn[1].first == 1);

    BOOST_CHECK(merkleBlock.vMatchedTxn[2].second == uint256S("0x56fb31578806d2604375dc815a877dff3145c032e2568f63afb62d82c0371ecd"));
    BOOST_CHECK(merkleBlock.vMatchedTxn[2].first == 3);

    BOOST_CHECK(merkleBlock.txn.ExtractMatches(vMatched, vIndex) == block.hashMerkleRoot);
    BOOST_CHECK(vMatched.size() == merkleBlock.vMatchedTxn.size());
    for (unsigned int i = 0; i < vMatched.size(); i++)
        BOOST_CHECK(vMatched[i] == merkleBlock.vMatchedTxn[i].second);
}

BOOST_AUTO_TEST_CASE(merkle_block_3_and_serialize)
{
    // Synthetic fixture adapted from Bitcoin block (000000000000dab0130bbcc991d3d7ae6b81aa6f50a798888dfe62337458dc45)
    // With one tx; transaction timestamps come from its header.
    CBlock block;
    CDataStream stream(ParseHex("0100000079cda856b143d9db2c1caff01d1aecc8630d30625d10e8b4b8b0000000000000758e4277ea0d090ca77ac29118e151352a66a2aebc80a1cecdcb43f3626b732767291b4d4c86041b8fa45d63010100000067291b4d010000000000000000000000000000000000000000000000000000000000000000ffffffff08044c86041b020a02ffffffff0100f2052a01000000434104ecd3229b0571c3be876feaac0442a9f13c5a572742927af1dc623353ecf8c202225f64868137a18cdd85cbbb4c74fbccfd4f49639cf1bdc94a5672bb15ad5d4cac00000000"), SER_NETWORK, PROTOCOL_VERSION);
    stream >> block;

    CBloomFilter filter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    // Match the only transaction
    filter.insert(uint256S("0x27736b62f343cbcdcea180bcaea2662a3551e11891c27aa70c090dea77428e75"));

    CMerkleBlock merkleBlock(block, filter);
    BOOST_CHECK(merkleBlock.header.GetHash() == block.GetHash());

    BOOST_CHECK(merkleBlock.vMatchedTxn.size() == 1);

    BOOST_CHECK(merkleBlock.vMatchedTxn[0].second == uint256S("0x27736b62f343cbcdcea180bcaea2662a3551e11891c27aa70c090dea77428e75"));
    BOOST_CHECK(merkleBlock.vMatchedTxn[0].first == 0);

    std::vector<uint256> vMatched;
    std::vector<unsigned int> vIndex;
    BOOST_CHECK(merkleBlock.txn.ExtractMatches(vMatched, vIndex) == block.hashMerkleRoot);
    BOOST_CHECK(vMatched.size() == merkleBlock.vMatchedTxn.size());
    for (unsigned int i = 0; i < vMatched.size(); i++)
        BOOST_CHECK(vMatched[i] == merkleBlock.vMatchedTxn[i].second);

    DataStream merkleStream{};
    merkleStream << merkleBlock;

    std::vector<uint8_t> expected = ParseHex("0100000079cda856b143d9db2c1caff01d1aecc8630d30625d10e8b4b8b0000000000000758e4277ea0d090ca77ac29118e151352a66a2aebc80a1cecdcb43f3626b732767291b4d4c86041b8fa45d630100000001758e4277ea0d090ca77ac29118e151352a66a2aebc80a1cecdcb43f3626b73270101");
    auto result{MakeUCharSpan(merkleStream)};

    BOOST_CHECK_EQUAL_COLLECTIONS(expected.begin(), expected.end(), result.begin(), result.end());
}

BOOST_AUTO_TEST_CASE(merkle_block_4)
{
    // Synthetic fixture adapted from Bitcoin block (000000000000b731f2eef9e8c63173adfb07e41bd53eb0ef0a6b720d6cb6dea4)
    // With 7 txes; transaction timestamps come from its header.
    CBlock block;
    CDataStream stream(ParseHex("0100000082bb869cf3a793432a66e826e05a6fc37469f8efb7421dc88067010000000000a919a9d5291a119f47b4d05396e97c2fd59299e8977e38e3bec3539f6b01251e76381b4d4c86041b554b8529070100000076381b4d010000000000000000000000000000000000000000000000000000000000000000ffffffff07044c86041b0136ffffffff0100f2052a01000000434104eaafc2314def4ca98ac970241bcab022b9c1e1f4ea423a20f134c876f2c01ec0f0dd5b2e86e7168cefe0d81113c3807420ce13ad1357231a2252247d97a46a91ac000000000100000076381b4d01bcad20a6a29827d1424f08989255120bf7f3e9e3cdaaa6bb31b0737fe048724300000000494830450220356e834b046cadc0f8ebb5a8a017b02de59c86305403dad52cd77b55af062ea10221009253cd6c119d4729b77c978e1e2aa19f5ea6e0e52b3f16e32fa608cd5bab753901ffffffff02008d380c010000001976a9142b4b8072ecbba129b6453c63e129e643207249ca88ac0065cd1d000000001976a9141b8dd13b994bcfc787b32aeadf58ccb3615cbd5488ac000000000100000076381b4d03f5734b594ff117607aa842383b4e12dea8920ac3b7d5d8a618565343ec17673e000000008c493046022100ea1608e70911ca0de5af51ba57ad23b9a51db8d28f82c53563c56a05c20f5a87022100a8bdc8b4a8acc8634c6b420410150775eb7f2474f5615f7fccd65af30f310fbf01410465fdf49e29b06b9a1582287b6279014f834edc317695d125ef623c1cc3aaece245bd69fcad7508666e9c74a49dc9056d5fc14338ef38118dc4afae5fe2c585caffffffff309e1913634ecb50f3c4f83e96e70b2df071b497b8973a3e75429df397b5af83000000004948304502202bdb79c596a9ffc24e96f4386199aba386e9bc7b6071516e2b51dda942b3a1ed022100c53a857e76b724fc14d45311eac5019650d415c3abb5428f3aae16d8e69bec2301ffffffff2089e33491695080c9edc18a428f7d834db5b6d372df13ce2b1b0e0cbcb1e6c10000000049483045022100d4ce67c5896ee251c810ac1ff9ceccd328b497c8f553ab6e08431e7d40bad6b5022033119c0c2b7d792d31f1187779c7bd95aefd93d90a715586d73801d9b47471c601ffffffff0100714460030000001976a914c7b55141d097ea5df7a0ed330cf794376e53ec8d88ac000000000100000076381b4d045bf0e214aa4069a3e792ecee1e1bf0c1d397cde8dd08138f4b72a00681743447000000008b48304502200c45de8c4f3e2c1821f2fc878cba97b1e6f8807d94930713aa1c86a67b9bf1e40221008581abfef2e30f957815fc89978423746b2086375ca8ecf359c85c2a5b7c88ad01410462bb73f76ca0994fcb8b4271e6fb7561f5c0f9ca0cf6485261c4a0dc894f4ab844c6cdfb97cd0b60ffb5018ffd6238f4d87270efb1d3ae37079b794a92d7ec95ffffffffd669f7d7958d40fc59d2253d88e0f248e29b599c80bbcec344a83dda5f9aa72c000000008a473044022078124c8beeaa825f9e0b30bff96e564dd859432f2d0cb3b72d3d5d93d38d7e930220691d233b6c0f995be5acb03d70a7f7a65b6bc9bdd426260f38a1346669507a3601410462bb73f76ca0994fcb8b4271e6fb7561f5c0f9ca0cf6485261c4a0dc894f4ab844c6cdfb97cd0b60ffb5018ffd6238f4d87270efb1d3ae37079b794a92d7ec95fffffffff878af0d93f5229a68166cf051fd372bb7a537232946e0a46f53636b4dafdaa4000000008c493046022100c717d1714551663f69c3c5759bdbb3a0fcd3fab023abc0e522fe6440de35d8290221008d9cbe25bffc44af2b18e81c58eb37293fd7fe1c2e7b46fc37ee8c96c50ab1e201410462bb73f76ca0994fcb8b4271e6fb7561f5c0f9ca0cf6485261c4a0dc894f4ab844c6cdfb97cd0b60ffb5018ffd6238f4d87270efb1d3ae37079b794a92d7ec95ffffffff27f2b668859cd7f2f894aa0fd2d9e60963bcd07c88973f425f999b8cbfd7a1e2000000008c493046022100e00847147cbf517bcc2f502f3ddc6d284358d102ed20d47a8aa788a62f0db780022100d17b2d6fa84dcaf1c95d88d7e7c30385aecf415588d749afd3ec81f6022cecd701410462bb73f76ca0994fcb8b4271e6fb7561f5c0f9ca0cf6485261c4a0dc894f4ab844c6cdfb97cd0b60ffb5018ffd6238f4d87270efb1d3ae37079b794a92d7ec95ffffffff0100c817a8040000001976a914b6efd80d99179f4f4ff6f4dd0a007d018c385d2188ac000000000100000076381b4d01834537b2f1ce8ef9373a258e10545ce5a50b758df616cd4356e0032554ebd3c4000000008b483045022100e68f422dd7c34fdce11eeb4509ddae38201773dd62f284e8aa9d96f85099d0b002202243bd399ff96b649a0fad05fa759d6a882f0af8c90cf7632c2840c29070aec20141045e58067e815c2f464c6a2a15f987758374203895710c2d452442e28496ff38ba8f5fd901dc20e29e88477167fe4fc299bf818fd0d9e1632d467b2a3d9503b1aaffffffff0280d7e636030000001976a914f34c3e10eb387efe872acb614c89e78bfca7815d88ac404b4c00000000001976a914a84e272933aaf87e1715d7786c51dfaeb5b65a6f88ac000000000100000076381b4d0143ac81c8e6f6ef307dfe17f3d906d999e23e0189fda838c5510d850927e03ae7000000008c4930460221009c87c344760a64cb8ae6685a3eec2c1ac1bed5b88c87de51acd0e124f266c16602210082d07c037359c3a257b5c63ebd90f5a5edf97b2ac1c434b08ca998839f346dd40141040ba7e521fa7946d12edbb1d1e95a15c34bd4398195e86433c92b431cd315f455fe30032ede69cad9d1e1ed6c3c4ec0dbfced53438c625462afb792dcb098544bffffffff0240420f00000000001976a9144676d1b820d63ec272f1900d59d43bc6463d96f888ac40420f00000000001976a914648d04341d00d7968b3405c034adc38d4d8fb9bd88ac000000000100000076381b4d0248cc917501ea5c55f4a8d2009c0567c40cfe037c2e71af017d0a452ff705e3f1000000008b483045022100bf5fdc86dc5f08a5d5c8e43a8c9d5b1ed8c65562e280007b52b133021acd9acc02205e325d613e555f772802bf413d36ba807892ed1a690a77811d3033b3de226e0a01410429fa713b124484cb2bd7b5557b2c0b9df7b2b1fee61825eadc5ae6c37a9920d38bfccdc7dc3cb0c47d7b173dbc9db8d37db0a33ae487982c59c6f8606e9d1791ffffffff545a1bfcc21d5488b1308f7723260046a31d1792e52b33cfbebf190e7f4e1fee000000008b4830450221008513ad65187b903aed1102d1d0c47688127658c51106753fed0151ce9c16b80902201432b9ebcb87bd04ceb2de66035fbbaf4bf8b00d1cfe41f1a1f7338f9ad79d210141049d4cf80125bf50be1709f718c07ad15d0fc612b7da1f5570dddc35f2a352f0f27c978b06820edca9ef982c35fda2d255afba340068c5035552368bc7200c1488ffffffff0100093d00000000001976a9148edb68822f1ad580b043c7b3df2e400f8699eb4888ac00000000"), SER_NETWORK, PROTOCOL_VERSION);
    stream >> block;

    CBloomFilter filter(10, 0.000001, 0, BLOOM_UPDATE_ALL);
    // Match the last transaction
    filter.insert(uint256S("0xebdb09a2016a321829a8d4a67f95e97bb63124d7384c45da621ea53dc08f4ad4"));

    CMerkleBlock merkleBlock(block, filter);
    BOOST_CHECK(merkleBlock.header.GetHash() == block.GetHash());

    BOOST_CHECK(merkleBlock.vMatchedTxn.size() == 1);
    std::pair<unsigned int, uint256> pair = merkleBlock.vMatchedTxn[0];

    BOOST_CHECK(merkleBlock.vMatchedTxn[0].second == uint256S("0xebdb09a2016a321829a8d4a67f95e97bb63124d7384c45da621ea53dc08f4ad4"));
    BOOST_CHECK(merkleBlock.vMatchedTxn[0].first == 6);

    std::vector<uint256> vMatched;
    std::vector<unsigned int> vIndex;
    BOOST_CHECK(merkleBlock.txn.ExtractMatches(vMatched, vIndex) == block.hashMerkleRoot);
    BOOST_CHECK(vMatched.size() == merkleBlock.vMatchedTxn.size());
    for (unsigned int i = 0; i < vMatched.size(); i++)
        BOOST_CHECK(vMatched[i] == merkleBlock.vMatchedTxn[i].second);

    // Also match the 4th transaction
    filter.insert(uint256S("0x20a531bfd5e8714224ad5196280ab9a61a6861e8d9c833125a3c182fd23be29d"));
    merkleBlock = CMerkleBlock(block, filter);
    BOOST_CHECK(merkleBlock.header.GetHash() == block.GetHash());

    BOOST_CHECK(merkleBlock.vMatchedTxn.size() == 2);

    BOOST_CHECK(merkleBlock.vMatchedTxn[0].second == uint256S("0x20a531bfd5e8714224ad5196280ab9a61a6861e8d9c833125a3c182fd23be29d"));
    BOOST_CHECK(merkleBlock.vMatchedTxn[0].first == 3);

    BOOST_CHECK(merkleBlock.vMatchedTxn[1] == pair);

    BOOST_CHECK(merkleBlock.txn.ExtractMatches(vMatched, vIndex) == block.hashMerkleRoot);
    BOOST_CHECK(vMatched.size() == merkleBlock.vMatchedTxn.size());
    for (unsigned int i = 0; i < vMatched.size(); i++)
        BOOST_CHECK(vMatched[i] == merkleBlock.vMatchedTxn[i].second);
}

BOOST_AUTO_TEST_CASE(merkle_block_4_test_p2pubkey_only)
{
    // Synthetic fixture adapted from Bitcoin block (000000000000b731f2eef9e8c63173adfb07e41bd53eb0ef0a6b720d6cb6dea4)
    // With 7 txes; transaction timestamps come from its header.
    CBlock block;
    CDataStream stream(ParseHex("0100000082bb869cf3a793432a66e826e05a6fc37469f8efb7421dc88067010000000000a919a9d5291a119f47b4d05396e97c2fd59299e8977e38e3bec3539f6b01251e76381b4d4c86041b554b8529070100000076381b4d010000000000000000000000000000000000000000000000000000000000000000ffffffff07044c86041b0136ffffffff0100f2052a01000000434104eaafc2314def4ca98ac970241bcab022b9c1e1f4ea423a20f134c876f2c01ec0f0dd5b2e86e7168cefe0d81113c3807420ce13ad1357231a2252247d97a46a91ac000000000100000076381b4d01bcad20a6a29827d1424f08989255120bf7f3e9e3cdaaa6bb31b0737fe048724300000000494830450220356e834b046cadc0f8ebb5a8a017b02de59c86305403dad52cd77b55af062ea10221009253cd6c119d4729b77c978e1e2aa19f5ea6e0e52b3f16e32fa608cd5bab753901ffffffff02008d380c010000001976a9142b4b8072ecbba129b6453c63e129e643207249ca88ac0065cd1d000000001976a9141b8dd13b994bcfc787b32aeadf58ccb3615cbd5488ac000000000100000076381b4d03f5734b594ff117607aa842383b4e12dea8920ac3b7d5d8a618565343ec17673e000000008c493046022100ea1608e70911ca0de5af51ba57ad23b9a51db8d28f82c53563c56a05c20f5a87022100a8bdc8b4a8acc8634c6b420410150775eb7f2474f5615f7fccd65af30f310fbf01410465fdf49e29b06b9a1582287b6279014f834edc317695d125ef623c1cc3aaece245bd69fcad7508666e9c74a49dc9056d5fc14338ef38118dc4afae5fe2c585caffffffff309e1913634ecb50f3c4f83e96e70b2df071b497b8973a3e75429df397b5af83000000004948304502202bdb79c596a9ffc24e96f4386199aba386e9bc7b6071516e2b51dda942b3a1ed022100c53a857e76b724fc14d45311eac5019650d415c3abb5428f3aae16d8e69bec2301ffffffff2089e33491695080c9edc18a428f7d834db5b6d372df13ce2b1b0e0cbcb1e6c10000000049483045022100d4ce67c5896ee251c810ac1ff9ceccd328b497c8f553ab6e08431e7d40bad6b5022033119c0c2b7d792d31f1187779c7bd95aefd93d90a715586d73801d9b47471c601ffffffff0100714460030000001976a914c7b55141d097ea5df7a0ed330cf794376e53ec8d88ac000000000100000076381b4d045bf0e214aa4069a3e792ecee1e1bf0c1d397cde8dd08138f4b72a00681743447000000008b48304502200c45de8c4f3e2c1821f2fc878cba97b1e6f8807d94930713aa1c86a67b9bf1e40221008581abfef2e30f957815fc89978423746b2086375ca8ecf359c85c2a5b7c88ad01410462bb73f76ca0994fcb8b4271e6fb7561f5c0f9ca0cf6485261c4a0dc894f4ab844c6cdfb97cd0b60ffb5018ffd6238f4d87270efb1d3ae37079b794a92d7ec95ffffffffd669f7d7958d40fc59d2253d88e0f248e29b599c80bbcec344a83dda5f9aa72c000000008a473044022078124c8beeaa825f9e0b30bff96e564dd859432f2d0cb3b72d3d5d93d38d7e930220691d233b6c0f995be5acb03d70a7f7a65b6bc9bdd426260f38a1346669507a3601410462bb73f76ca0994fcb8b4271e6fb7561f5c0f9ca0cf6485261c4a0dc894f4ab844c6cdfb97cd0b60ffb5018ffd6238f4d87270efb1d3ae37079b794a92d7ec95fffffffff878af0d93f5229a68166cf051fd372bb7a537232946e0a46f53636b4dafdaa4000000008c493046022100c717d1714551663f69c3c5759bdbb3a0fcd3fab023abc0e522fe6440de35d8290221008d9cbe25bffc44af2b18e81c58eb37293fd7fe1c2e7b46fc37ee8c96c50ab1e201410462bb73f76ca0994fcb8b4271e6fb7561f5c0f9ca0cf6485261c4a0dc894f4ab844c6cdfb97cd0b60ffb5018ffd6238f4d87270efb1d3ae37079b794a92d7ec95ffffffff27f2b668859cd7f2f894aa0fd2d9e60963bcd07c88973f425f999b8cbfd7a1e2000000008c493046022100e00847147cbf517bcc2f502f3ddc6d284358d102ed20d47a8aa788a62f0db780022100d17b2d6fa84dcaf1c95d88d7e7c30385aecf415588d749afd3ec81f6022cecd701410462bb73f76ca0994fcb8b4271e6fb7561f5c0f9ca0cf6485261c4a0dc894f4ab844c6cdfb97cd0b60ffb5018ffd6238f4d87270efb1d3ae37079b794a92d7ec95ffffffff0100c817a8040000001976a914b6efd80d99179f4f4ff6f4dd0a007d018c385d2188ac000000000100000076381b4d01834537b2f1ce8ef9373a258e10545ce5a50b758df616cd4356e0032554ebd3c4000000008b483045022100e68f422dd7c34fdce11eeb4509ddae38201773dd62f284e8aa9d96f85099d0b002202243bd399ff96b649a0fad05fa759d6a882f0af8c90cf7632c2840c29070aec20141045e58067e815c2f464c6a2a15f987758374203895710c2d452442e28496ff38ba8f5fd901dc20e29e88477167fe4fc299bf818fd0d9e1632d467b2a3d9503b1aaffffffff0280d7e636030000001976a914f34c3e10eb387efe872acb614c89e78bfca7815d88ac404b4c00000000001976a914a84e272933aaf87e1715d7786c51dfaeb5b65a6f88ac000000000100000076381b4d0143ac81c8e6f6ef307dfe17f3d906d999e23e0189fda838c5510d850927e03ae7000000008c4930460221009c87c344760a64cb8ae6685a3eec2c1ac1bed5b88c87de51acd0e124f266c16602210082d07c037359c3a257b5c63ebd90f5a5edf97b2ac1c434b08ca998839f346dd40141040ba7e521fa7946d12edbb1d1e95a15c34bd4398195e86433c92b431cd315f455fe30032ede69cad9d1e1ed6c3c4ec0dbfced53438c625462afb792dcb098544bffffffff0240420f00000000001976a9144676d1b820d63ec272f1900d59d43bc6463d96f888ac40420f00000000001976a914648d04341d00d7968b3405c034adc38d4d8fb9bd88ac000000000100000076381b4d0248cc917501ea5c55f4a8d2009c0567c40cfe037c2e71af017d0a452ff705e3f1000000008b483045022100bf5fdc86dc5f08a5d5c8e43a8c9d5b1ed8c65562e280007b52b133021acd9acc02205e325d613e555f772802bf413d36ba807892ed1a690a77811d3033b3de226e0a01410429fa713b124484cb2bd7b5557b2c0b9df7b2b1fee61825eadc5ae6c37a9920d38bfccdc7dc3cb0c47d7b173dbc9db8d37db0a33ae487982c59c6f8606e9d1791ffffffff545a1bfcc21d5488b1308f7723260046a31d1792e52b33cfbebf190e7f4e1fee000000008b4830450221008513ad65187b903aed1102d1d0c47688127658c51106753fed0151ce9c16b80902201432b9ebcb87bd04ceb2de66035fbbaf4bf8b00d1cfe41f1a1f7338f9ad79d210141049d4cf80125bf50be1709f718c07ad15d0fc612b7da1f5570dddc35f2a352f0f27c978b06820edca9ef982c35fda2d255afba340068c5035552368bc7200c1488ffffffff0100093d00000000001976a9148edb68822f1ad580b043c7b3df2e400f8699eb4888ac00000000"), SER_NETWORK, PROTOCOL_VERSION);
    stream >> block;

    CBloomFilter filter(10, 0.000001, 0, BLOOM_UPDATE_P2PUBKEY_ONLY);
    // Match the generation pubkey
    filter.insert(ParseHex("04eaafc2314def4ca98ac970241bcab022b9c1e1f4ea423a20f134c876f2c01ec0f0dd5b2e86e7168cefe0d81113c3807420ce13ad1357231a2252247d97a46a91"));
    // ...and the output address of the 4th transaction
    filter.insert(ParseHex("b6efd80d99179f4f4ff6f4dd0a007d018c385d21"));

    CMerkleBlock merkleBlock(block, filter);
    BOOST_CHECK(merkleBlock.header.GetHash() == block.GetHash());

    // We should match the generation outpoint
    BOOST_CHECK(filter.contains(COutPoint(uint256S("0xb2cdd76d675ee48905929ea5201950dd22c9fc9ad23c02f8b491dd57d8b7dd97"), 0)));
    // ... but not the 4th transaction's output (its not pay-2-pubkey)
    BOOST_CHECK(!filter.contains(COutPoint(uint256S("0x20a531bfd5e8714224ad5196280ab9a61a6861e8d9c833125a3c182fd23be29d"), 0)));
}

BOOST_AUTO_TEST_CASE(merkle_block_4_test_update_none)
{
    // Synthetic fixture adapted from Bitcoin block (000000000000b731f2eef9e8c63173adfb07e41bd53eb0ef0a6b720d6cb6dea4)
    // With 7 txes; transaction timestamps come from its header.
    CBlock block;
    CDataStream stream(ParseHex("0100000082bb869cf3a793432a66e826e05a6fc37469f8efb7421dc88067010000000000a919a9d5291a119f47b4d05396e97c2fd59299e8977e38e3bec3539f6b01251e76381b4d4c86041b554b8529070100000076381b4d010000000000000000000000000000000000000000000000000000000000000000ffffffff07044c86041b0136ffffffff0100f2052a01000000434104eaafc2314def4ca98ac970241bcab022b9c1e1f4ea423a20f134c876f2c01ec0f0dd5b2e86e7168cefe0d81113c3807420ce13ad1357231a2252247d97a46a91ac000000000100000076381b4d01bcad20a6a29827d1424f08989255120bf7f3e9e3cdaaa6bb31b0737fe048724300000000494830450220356e834b046cadc0f8ebb5a8a017b02de59c86305403dad52cd77b55af062ea10221009253cd6c119d4729b77c978e1e2aa19f5ea6e0e52b3f16e32fa608cd5bab753901ffffffff02008d380c010000001976a9142b4b8072ecbba129b6453c63e129e643207249ca88ac0065cd1d000000001976a9141b8dd13b994bcfc787b32aeadf58ccb3615cbd5488ac000000000100000076381b4d03f5734b594ff117607aa842383b4e12dea8920ac3b7d5d8a618565343ec17673e000000008c493046022100ea1608e70911ca0de5af51ba57ad23b9a51db8d28f82c53563c56a05c20f5a87022100a8bdc8b4a8acc8634c6b420410150775eb7f2474f5615f7fccd65af30f310fbf01410465fdf49e29b06b9a1582287b6279014f834edc317695d125ef623c1cc3aaece245bd69fcad7508666e9c74a49dc9056d5fc14338ef38118dc4afae5fe2c585caffffffff309e1913634ecb50f3c4f83e96e70b2df071b497b8973a3e75429df397b5af83000000004948304502202bdb79c596a9ffc24e96f4386199aba386e9bc7b6071516e2b51dda942b3a1ed022100c53a857e76b724fc14d45311eac5019650d415c3abb5428f3aae16d8e69bec2301ffffffff2089e33491695080c9edc18a428f7d834db5b6d372df13ce2b1b0e0cbcb1e6c10000000049483045022100d4ce67c5896ee251c810ac1ff9ceccd328b497c8f553ab6e08431e7d40bad6b5022033119c0c2b7d792d31f1187779c7bd95aefd93d90a715586d73801d9b47471c601ffffffff0100714460030000001976a914c7b55141d097ea5df7a0ed330cf794376e53ec8d88ac000000000100000076381b4d045bf0e214aa4069a3e792ecee1e1bf0c1d397cde8dd08138f4b72a00681743447000000008b48304502200c45de8c4f3e2c1821f2fc878cba97b1e6f8807d94930713aa1c86a67b9bf1e40221008581abfef2e30f957815fc89978423746b2086375ca8ecf359c85c2a5b7c88ad01410462bb73f76ca0994fcb8b4271e6fb7561f5c0f9ca0cf6485261c4a0dc894f4ab844c6cdfb97cd0b60ffb5018ffd6238f4d87270efb1d3ae37079b794a92d7ec95ffffffffd669f7d7958d40fc59d2253d88e0f248e29b599c80bbcec344a83dda5f9aa72c000000008a473044022078124c8beeaa825f9e0b30bff96e564dd859432f2d0cb3b72d3d5d93d38d7e930220691d233b6c0f995be5acb03d70a7f7a65b6bc9bdd426260f38a1346669507a3601410462bb73f76ca0994fcb8b4271e6fb7561f5c0f9ca0cf6485261c4a0dc894f4ab844c6cdfb97cd0b60ffb5018ffd6238f4d87270efb1d3ae37079b794a92d7ec95fffffffff878af0d93f5229a68166cf051fd372bb7a537232946e0a46f53636b4dafdaa4000000008c493046022100c717d1714551663f69c3c5759bdbb3a0fcd3fab023abc0e522fe6440de35d8290221008d9cbe25bffc44af2b18e81c58eb37293fd7fe1c2e7b46fc37ee8c96c50ab1e201410462bb73f76ca0994fcb8b4271e6fb7561f5c0f9ca0cf6485261c4a0dc894f4ab844c6cdfb97cd0b60ffb5018ffd6238f4d87270efb1d3ae37079b794a92d7ec95ffffffff27f2b668859cd7f2f894aa0fd2d9e60963bcd07c88973f425f999b8cbfd7a1e2000000008c493046022100e00847147cbf517bcc2f502f3ddc6d284358d102ed20d47a8aa788a62f0db780022100d17b2d6fa84dcaf1c95d88d7e7c30385aecf415588d749afd3ec81f6022cecd701410462bb73f76ca0994fcb8b4271e6fb7561f5c0f9ca0cf6485261c4a0dc894f4ab844c6cdfb97cd0b60ffb5018ffd6238f4d87270efb1d3ae37079b794a92d7ec95ffffffff0100c817a8040000001976a914b6efd80d99179f4f4ff6f4dd0a007d018c385d2188ac000000000100000076381b4d01834537b2f1ce8ef9373a258e10545ce5a50b758df616cd4356e0032554ebd3c4000000008b483045022100e68f422dd7c34fdce11eeb4509ddae38201773dd62f284e8aa9d96f85099d0b002202243bd399ff96b649a0fad05fa759d6a882f0af8c90cf7632c2840c29070aec20141045e58067e815c2f464c6a2a15f987758374203895710c2d452442e28496ff38ba8f5fd901dc20e29e88477167fe4fc299bf818fd0d9e1632d467b2a3d9503b1aaffffffff0280d7e636030000001976a914f34c3e10eb387efe872acb614c89e78bfca7815d88ac404b4c00000000001976a914a84e272933aaf87e1715d7786c51dfaeb5b65a6f88ac000000000100000076381b4d0143ac81c8e6f6ef307dfe17f3d906d999e23e0189fda838c5510d850927e03ae7000000008c4930460221009c87c344760a64cb8ae6685a3eec2c1ac1bed5b88c87de51acd0e124f266c16602210082d07c037359c3a257b5c63ebd90f5a5edf97b2ac1c434b08ca998839f346dd40141040ba7e521fa7946d12edbb1d1e95a15c34bd4398195e86433c92b431cd315f455fe30032ede69cad9d1e1ed6c3c4ec0dbfced53438c625462afb792dcb098544bffffffff0240420f00000000001976a9144676d1b820d63ec272f1900d59d43bc6463d96f888ac40420f00000000001976a914648d04341d00d7968b3405c034adc38d4d8fb9bd88ac000000000100000076381b4d0248cc917501ea5c55f4a8d2009c0567c40cfe037c2e71af017d0a452ff705e3f1000000008b483045022100bf5fdc86dc5f08a5d5c8e43a8c9d5b1ed8c65562e280007b52b133021acd9acc02205e325d613e555f772802bf413d36ba807892ed1a690a77811d3033b3de226e0a01410429fa713b124484cb2bd7b5557b2c0b9df7b2b1fee61825eadc5ae6c37a9920d38bfccdc7dc3cb0c47d7b173dbc9db8d37db0a33ae487982c59c6f8606e9d1791ffffffff545a1bfcc21d5488b1308f7723260046a31d1792e52b33cfbebf190e7f4e1fee000000008b4830450221008513ad65187b903aed1102d1d0c47688127658c51106753fed0151ce9c16b80902201432b9ebcb87bd04ceb2de66035fbbaf4bf8b00d1cfe41f1a1f7338f9ad79d210141049d4cf80125bf50be1709f718c07ad15d0fc612b7da1f5570dddc35f2a352f0f27c978b06820edca9ef982c35fda2d255afba340068c5035552368bc7200c1488ffffffff0100093d00000000001976a9148edb68822f1ad580b043c7b3df2e400f8699eb4888ac00000000"), SER_NETWORK, PROTOCOL_VERSION);
    stream >> block;

    CBloomFilter filter(10, 0.000001, 0, BLOOM_UPDATE_NONE);
    // Match the generation pubkey
    filter.insert(ParseHex("04eaafc2314def4ca98ac970241bcab022b9c1e1f4ea423a20f134c876f2c01ec0f0dd5b2e86e7168cefe0d81113c3807420ce13ad1357231a2252247d97a46a91"));
    // ...and the output address of the 4th transaction
    filter.insert(ParseHex("b6efd80d99179f4f4ff6f4dd0a007d018c385d21"));

    CMerkleBlock merkleBlock(block, filter);
    BOOST_CHECK(merkleBlock.header.GetHash() == block.GetHash());

    // We shouldn't match any outpoints (UPDATE_NONE)
    BOOST_CHECK(!filter.contains(COutPoint(uint256S("0xb2cdd76d675ee48905929ea5201950dd22c9fc9ad23c02f8b491dd57d8b7dd97"), 0)));
    BOOST_CHECK(!filter.contains(COutPoint(uint256S("0x20a531bfd5e8714224ad5196280ab9a61a6861e8d9c833125a3c182fd23be29d"), 0)));
}

static std::vector<unsigned char> RandomData()
{
    uint256 r = InsecureRand256();
    return std::vector<unsigned char>(r.begin(), r.end());
}

BOOST_AUTO_TEST_CASE(rolling_bloom)
{
    SeedInsecureRand(SeedRand::ZEROS);
    g_mock_deterministic_tests = true;

    // last-100-entry, 1% false positive:
    CRollingBloomFilter rb1(100, 0.01);

    // Overfill:
    static const int DATASIZE=399;
    std::vector<unsigned char> data[DATASIZE];
    for (int i = 0; i < DATASIZE; i++) {
        data[i] = RandomData();
        rb1.insert(data[i]);
    }
    // Last 100 guaranteed to be remembered:
    for (int i = 299; i < DATASIZE; i++) {
        BOOST_CHECK(rb1.contains(data[i]));
    }

    // false positive rate is 1%, so we should get about 100 hits if
    // testing 10,000 random keys. We get worst-case false positive
    // behavior when the filter is as full as possible, which is
    // when we've inserted one minus an integer multiple of nElement*2.
    unsigned int nHits = 0;
    for (int i = 0; i < 10000; i++) {
        if (rb1.contains(RandomData()))
            ++nHits;
    }
    // Expect about 100 hits
    BOOST_CHECK_EQUAL(nHits, 75U);

    BOOST_CHECK(rb1.contains(data[DATASIZE-1]));
    rb1.reset();
    BOOST_CHECK(!rb1.contains(data[DATASIZE-1]));

    // Now roll through data, make sure last 100 entries
    // are always remembered:
    for (int i = 0; i < DATASIZE; i++) {
        if (i >= 100)
            BOOST_CHECK(rb1.contains(data[i-100]));
        rb1.insert(data[i]);
        BOOST_CHECK(rb1.contains(data[i]));
    }

    // Insert 999 more random entries:
    for (int i = 0; i < 999; i++) {
        std::vector<unsigned char> d = RandomData();
        rb1.insert(d);
        BOOST_CHECK(rb1.contains(d));
    }
    // Sanity check to make sure the filter isn't just filling up:
    nHits = 0;
    for (int i = 0; i < DATASIZE; i++) {
        if (rb1.contains(data[i]))
            ++nHits;
    }
    // Expect about 5 false positives
    BOOST_CHECK_EQUAL(nHits, 6U);

    // last-1000-entry, 0.01% false positive:
    CRollingBloomFilter rb2(1000, 0.001);
    for (int i = 0; i < DATASIZE; i++) {
        rb2.insert(data[i]);
    }
    // ... room for all of them:
    for (int i = 0; i < DATASIZE; i++) {
        BOOST_CHECK(rb2.contains(data[i]));
    }
    g_mock_deterministic_tests = false;
}

BOOST_AUTO_TEST_SUITE_END()
