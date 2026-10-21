// Legal move generation.
//
// Moves are generated strictly legal (no make/unmake filtering): the generator
// computes the checking pieces and the pinned pieces up front, restricts
// non-king moves to the check-evasion mask and pinned pieces to their pin
// line, and verifies king destinations against attacks computed with the king
// removed from the board. En passant, whose capture removes two pieces from a
// rank, is verified by recomputing attacks on the king directly.
#pragma once

#include <cstddef>
#include <utility>

#include "position.h"

namespace chess {

// Noisy = captures (including capture-promotions) and quiet queen promotions.
// Quiet = everything else, including castling and quiet under-promotions.
// All   = Noisy + Quiet. Noisy and Quiet partition All exactly.
enum class GenType { Noisy, Quiet, All };

class MoveList {
public:
    void add(Move m) { moves_[size_++] = m; }
    void swap(size_t a, size_t b) { std::swap(moves_[a], moves_[b]); }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    Move operator[](size_t i) const { return moves_[i]; }
    const Move* begin() const { return moves_; }
    const Move* end() const { return moves_ + size_; }
    bool contains(Move m) const {
        for (const Move x : *this)
            if (x == m) return true;
        return false;
    }

private:
    Move moves_[MAX_MOVES];
    size_t size_ = 0;
};

template <GenType G>
void generate(const Position& pos, MoveList& list);

// Leaf-node count of the legal move tree; the standard movegen correctness test.
std::uint64_t perft(Position& pos, int depth);

}  // namespace chess
