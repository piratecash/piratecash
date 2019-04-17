// Copyright (c) 2012-2014 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BLOOM_H
#define BITCOIN_BLOOM_H

#include <vector>

#include "uint256.h"
#include "serialize.h"

class COutPoint;
class CTransaction;

// 20,000 items with fp rate < 0.1% or 10,000 items and <0.0001%
static const unsigned int MAX_BLOOM_FILTER_SIZE = 36000; // bytes
static const unsigned int MAX_HASH_FUNCS = 50;


/**
 * BloomFilter is a probabilistic filter which SPV clients provide
 * so that we can filter the transactions we sends them.
 * 
 * This allows for significantly more efficient transaction and block downloads.
 * 
 * Because bloom filters are probabilistic, an SPV node can increase the false-
 * positive rate, making us send them transactions which aren't actually theirs, 
 * allowing clients to trade more bandwidth for more privacy by obfuscating which
 * keys are owned by them.
 */
class CBloomFilter
{
private:
    std::vector<unsigned char> vData;
    unsigned int nHashFuncs;

    unsigned int Hash(unsigned int nHashNum, const std::vector<unsigned char>& vDataToHash) const;

public:
    // Creates a new bloom filter which will provide the given fp rate when filled with the given number of elements
    // Note that if the given parameters will result in a filter outside the bounds of the protocol limits,
    // the filter created will be as close to the given parameters as possible within the protocol limits.
    // This will apply if nFPRate is very low or nElements is unreasonably high.
    CBloomFilter(unsigned int nElements, double nFPRate);
    // Using a filter initialized with this results in undefined behavior
    // Should only be used for deserialization
    CBloomFilter() {}

    IMPLEMENT_SERIALIZE
    template <typename T, typename Stream, typename Operation>
    inline static size_t SerializationOp(T thisPtr, Stream& s, Operation ser_action, int nType, int nVersion) {
        size_t nSerSize = 0;
        READWRITE(thisPtr->vData);
        READWRITE(thisPtr->nHashFuncs);
        return nSerSize;
    }

    void insert(const std::vector<unsigned char>& vKey);
    void insert(const COutPoint& outpoint);
    void insert(const uint256& hash);

    bool contains(const std::vector<unsigned char>& vKey) const;
    bool contains(const COutPoint& outpoint) const;
    bool contains(const uint256& hash) const;

    // True if the size is <= MAX_BLOOM_FILTER_SIZE and the number of hash functions is <= MAX_HASH_FUNCS
    // (catch a filter which was just deserialized which was too big)
    bool IsWithinSizeConstraints() const;

    //! Also adds any outputs which match the filter to the filter (to match their spending txes)
    bool IsRelevantAndUpdate(const CTransaction& tx);
};

#endif // BITCOIN_BLOOM_H
