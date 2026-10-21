// Shared transposition table.
//
// The table is read and written concurrently by all search threads without
// locks. Each entry is two 64-bit words, stored as (key ^ data, data) using
// relaxed atomics (Hyatt & Mann's "lockless hashing"): a torn read, where the
// two words come from different writers, fails the key check and is treated
// as a miss. Relaxed atomics make the races well-defined in the C++ memory
// model (and ThreadSanitizer-clean) at no cost on x86-64/ARM64, where they
// compile to ordinary loads and stores.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "core/types.h"

namespace chess {

enum Bound : std::uint8_t { BOUND_NONE = 0, BOUND_UPPER = 1, BOUND_LOWER = 2, BOUND_EXACT = 3 };

struct TTData {
    Move move;
    Value score = VALUE_NONE;  // node-relative; see value_to_tt/value_from_tt
    Value eval = VALUE_NONE;   // static evaluation
    int depth = 0;
    Bound bound = BOUND_NONE;
};

class TranspositionTable {
public:
    explicit TranspositionTable(std::size_t megabytes = 16) { resize(megabytes); }

    void resize(std::size_t megabytes);  // also clears
    void clear();
    void new_search() { generation_ = std::uint8_t((generation_ + 1) & 63); }

    bool probe(Key key, TTData& out) const;
    void store(Key key, Move move, Value score, Value eval, int depth, Bound bound);

    // Permille of sampled entries written during the current search (UCI hashfull).
    int hashfull() const;
    std::size_t size_mb() const { return (bucket_count_ * sizeof(Bucket)) >> 20; }

    void prefetch(Key key) const {
#if defined(__GNUC__) || defined(__clang__)
        __builtin_prefetch(&buckets_[key & (bucket_count_ - 1)]);
#endif
    }

private:
    struct Entry {
        std::atomic<std::uint64_t> check{0};  // key ^ data
        std::atomic<std::uint64_t> data{0};
    };
    static constexpr int BUCKET_SIZE = 4;
    struct alignas(64) Bucket {
        Entry entries[BUCKET_SIZE];
    };

    std::unique_ptr<Bucket[]> buckets_;
    std::size_t bucket_count_ = 0;  // power of two
    std::uint8_t generation_ = 0;
};

// Mate scores are stored relative to the node rather than the root, so that a
// "mate in n from here" entry stays correct when reached at a different ply.
constexpr Value value_to_tt(Value v, int ply) {
    return v >= VALUE_MATE_IN_MAX_PLY ? v + ply : v <= -VALUE_MATE_IN_MAX_PLY ? v - ply : v;
}
constexpr Value value_from_tt(Value v, int ply) {
    if (v == VALUE_NONE) return v;
    return v >= VALUE_MATE_IN_MAX_PLY ? v - ply : v <= -VALUE_MATE_IN_MAX_PLY ? v + ply : v;
}

}  // namespace chess
