// Small deterministic PRNG (xorshift64*, Vigna 2014). Used wherever the engine
// needs reproducible pseudo-randomness: Zobrist keys, magic search, datagen.
#pragma once

#include <cstdint>

namespace chess {

class Rng {
public:
    explicit Rng(std::uint64_t seed) : state_(seed ? seed : 0x2545F4914F6CDD1DULL) {}

    std::uint64_t next() {
        state_ ^= state_ >> 12;
        state_ ^= state_ << 25;
        state_ ^= state_ >> 27;
        return state_ * 0x2545F4914F6CDD1DULL;
    }

    // Numbers with roughly 1/8 of the bits set; good magic candidates.
    std::uint64_t sparse() { return next() & next() & next(); }

    // Uniform-ish integer in [0, n). Modulo bias is irrelevant for our uses.
    std::uint64_t below(std::uint64_t n) { return next() % n; }

private:
    std::uint64_t state_;
};

}  // namespace chess
