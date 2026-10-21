#include "tt.h"

#include <algorithm>
#include <bit>

namespace chess {

namespace {

// Packed entry layout (64 bits):
//   [0,16) move  [16,32) score  [32,48) static eval  [48,56) depth
//   [56,58) bound  [58,64) generation
constexpr std::uint64_t pack(Move move, Value score, Value eval, int depth, Bound bound,
                             std::uint8_t generation) {
    return std::uint64_t(move.raw()) | (std::uint64_t(std::uint16_t(std::int16_t(score))) << 16) |
           (std::uint64_t(std::uint16_t(std::int16_t(eval))) << 32) |
           (std::uint64_t(std::uint8_t(depth)) << 48) | (std::uint64_t(bound) << 56) |
           (std::uint64_t(generation) << 58);
}

constexpr Move move_of(std::uint64_t d) { return Move::from_raw(std::uint16_t(d)); }
constexpr Value score_of(std::uint64_t d) { return std::int16_t(std::uint16_t(d >> 16)); }
constexpr Value eval_of(std::uint64_t d) { return std::int16_t(std::uint16_t(d >> 32)); }
constexpr int depth_of(std::uint64_t d) { return int((d >> 48) & 0xFF); }
constexpr Bound bound_of(std::uint64_t d) { return Bound((d >> 56) & 3); }
constexpr std::uint8_t generation_of(std::uint64_t d) { return std::uint8_t(d >> 58); }

}  // namespace

void TranspositionTable::resize(std::size_t megabytes) {
    const std::size_t bytes = std::max<std::size_t>(megabytes, 1) << 20;
    bucket_count_ = std::bit_floor(bytes / sizeof(Bucket));
    buckets_ = std::make_unique<Bucket[]>(bucket_count_);
    generation_ = 0;
}

void TranspositionTable::clear() {
    for (std::size_t i = 0; i < bucket_count_; ++i)
        for (Entry& e : buckets_[i].entries) {
            e.check.store(0, std::memory_order_relaxed);
            e.data.store(0, std::memory_order_relaxed);
        }
    generation_ = 0;
}

bool TranspositionTable::probe(Key key, TTData& out) const {
    const Bucket& bucket = buckets_[key & (bucket_count_ - 1)];
    for (const Entry& e : bucket.entries) {
        const std::uint64_t data = e.data.load(std::memory_order_relaxed);
        const std::uint64_t check = e.check.load(std::memory_order_relaxed);
        if ((check ^ data) == key && bound_of(data) != BOUND_NONE) {
            out.move = move_of(data);
            out.score = score_of(data);
            out.eval = eval_of(data);
            out.depth = depth_of(data);
            out.bound = bound_of(data);
            return true;
        }
    }
    return false;
}

void TranspositionTable::store(Key key, Move move, Value score, Value eval, int depth, Bound bound) {
    Bucket& bucket = buckets_[key & (bucket_count_ - 1)];
    depth = std::clamp(depth, 0, 255);

    // Prefer the slot already holding this position; otherwise evict the
    // least valuable entry, weighing shallow depth and staleness.
    Entry* victim = nullptr;
    int victim_worth = 1 << 30;
    for (Entry& e : bucket.entries) {
        const std::uint64_t data = e.data.load(std::memory_order_relaxed);
        const std::uint64_t check = e.check.load(std::memory_order_relaxed);
        if ((check ^ data) == key && bound_of(data) != BOUND_NONE) {
            // Keep a deeper result for the same position from this search,
            // unless the new one is exact.
            if (bound != BOUND_EXACT && depth + 3 < depth_of(data) && generation_of(data) == generation_)
                return;
            if (!move.is_ok()) move = move_of(data);  // keep the old best move
            victim = &e;
            break;
        }
        const int age = (generation_ - generation_of(data)) & 63;
        const int worth = bound_of(data) == BOUND_NONE ? -1000 : depth_of(data) - 8 * age;
        if (worth < victim_worth) {
            victim_worth = worth;
            victim = &e;
        }
    }

    const std::uint64_t data = pack(move, score, eval, depth, bound, generation_);
    victim->data.store(data, std::memory_order_relaxed);
    victim->check.store(key ^ data, std::memory_order_relaxed);
}

int TranspositionTable::hashfull() const {
    constexpr std::size_t sample = 250;  // 1000 entries
    int used = 0;
    for (std::size_t i = 0; i < std::min(sample, bucket_count_); ++i)
        for (const Entry& e : buckets_[i].entries) {
            const std::uint64_t data = e.data.load(std::memory_order_relaxed);
            used += bound_of(data) != BOUND_NONE && generation_of(data) == generation_;
        }
    return int(used * 1000 / (std::min(sample, bucket_count_) * BUCKET_SIZE));
}

}  // namespace chess
