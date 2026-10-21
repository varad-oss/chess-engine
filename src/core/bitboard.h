// Bitboard primitives and precomputed attack tables.
//
// Sliding-piece attacks use "fancy" magic bitboards. The multipliers are
// embedded (src/core/magics.inc), but never trusted blindly: at startup every
// one is verified by filling its square's table from ray-walked reference
// attacks and checking that no two blocker sets with different attacks collide.
// A failing multiplier is replaced by a fresh fixed-seed search, so the tables
// are correct by construction; the unit tests also cross-check them.
#pragma once

#include <bit>
#include <string>

#include "types.h"

namespace chess {

constexpr Bitboard FILE_A_BB = 0x0101010101010101ULL;
constexpr Bitboard FILE_H_BB = FILE_A_BB << 7;
constexpr Bitboard RANK_1_BB = 0xFFULL;
constexpr Bitboard RANK_8_BB = RANK_1_BB << 56;
constexpr Bitboard LIGHT_SQUARES_BB = 0x55AA55AA55AA55AAULL;
constexpr Bitboard DARK_SQUARES_BB = ~LIGHT_SQUARES_BB;

constexpr Bitboard file_bb(int file) { return FILE_A_BB << file; }
constexpr Bitboard rank_bb(int rank) { return RANK_1_BB << (8 * rank); }
constexpr Bitboard square_bb(Square s) { return 1ULL << s; }

inline int popcount(Bitboard b) { return std::popcount(b); }
inline Square lsb(Bitboard b) { return Square(std::countr_zero(b)); }
inline Square pop_lsb(Bitboard& b) {
    const Square s = lsb(b);
    b &= b - 1;
    return s;
}
constexpr bool more_than_one(Bitboard b) { return (b & (b - 1)) != 0; }

enum Direction : int {
    NORTH = 8, SOUTH = -8, EAST = 1, WEST = -1,
    NORTH_EAST = 9, NORTH_WEST = 7, SOUTH_EAST = -7, SOUTH_WEST = -9
};

// Shifts a bitboard one step in direction D, discarding squares that would
// wrap around the board edge.
template <Direction D>
constexpr Bitboard shift(Bitboard b) {
    if constexpr (D == NORTH) return b << 8;
    else if constexpr (D == SOUTH) return b >> 8;
    else if constexpr (D == EAST) return (b & ~FILE_H_BB) << 1;
    else if constexpr (D == WEST) return (b & ~FILE_A_BB) >> 1;
    else if constexpr (D == NORTH_EAST) return (b & ~FILE_H_BB) << 9;
    else if constexpr (D == NORTH_WEST) return (b & ~FILE_A_BB) << 7;
    else if constexpr (D == SOUTH_EAST) return (b & ~FILE_H_BB) >> 7;
    else return (b & ~FILE_A_BB) >> 9;
}

constexpr Bitboard pawn_attacks_bb(Color c, Bitboard pawns) {
    return c == WHITE ? shift<NORTH_WEST>(pawns) | shift<NORTH_EAST>(pawns)
                      : shift<SOUTH_WEST>(pawns) | shift<SOUTH_EAST>(pawns);
}

namespace attacks {

struct Magic {
    Bitboard mask = 0;
    Bitboard magic = 0;
    const Bitboard* table = nullptr;
    unsigned shift = 0;

    unsigned index(Bitboard occupied) const {
        return unsigned(((occupied & mask) * magic) >> shift);
    }
};

extern Bitboard Pawn[COLOR_NB][SQUARE_NB];
extern Bitboard Knight[SQUARE_NB];
extern Bitboard King[SQUARE_NB];
extern Bitboard Between[SQUARE_NB][SQUARE_NB];  // squares strictly between two aligned squares
extern Bitboard Line[SQUARE_NB][SQUARE_NB];     // whole line through two aligned squares
extern Magic RookMagics[SQUARE_NB];
extern Magic BishopMagics[SQUARE_NB];

}  // namespace attacks

// Builds all tables. Idempotent and thread-safe; called by chess::init().
void init_bitboards();

// Number of squares whose embedded magic failed verification at startup and
// had to be searched for (expected: 0).
int magic_search_fallbacks();

// Searches all magics from scratch and returns them as C++ source for
// src/core/magics.inc (used by the `magics` command).
std::string generate_magics_source();

inline Bitboard bishop_attacks(Square s, Bitboard occupied) {
    const attacks::Magic& m = attacks::BishopMagics[s];
    return m.table[m.index(occupied)];
}
inline Bitboard rook_attacks(Square s, Bitboard occupied) {
    const attacks::Magic& m = attacks::RookMagics[s];
    return m.table[m.index(occupied)];
}
inline Bitboard queen_attacks(Square s, Bitboard occupied) {
    return bishop_attacks(s, occupied) | rook_attacks(s, occupied);
}

// Attacks of a non-pawn piece type from s.
inline Bitboard attacks_bb(PieceType pt, Square s, Bitboard occupied) {
    switch (pt) {
        case KNIGHT: return attacks::Knight[s];
        case BISHOP: return bishop_attacks(s, occupied);
        case ROOK: return rook_attacks(s, occupied);
        case QUEEN: return queen_attacks(s, occupied);
        case KING: return attacks::King[s];
        default: return 0;
    }
}

inline Bitboard between_bb(Square a, Square b) { return attacks::Between[a][b]; }
inline Bitboard line_bb(Square a, Square b) { return attacks::Line[a][b]; }

// Reference implementation used to build the magic tables and by the tests.
Bitboard sliding_attacks_slow(PieceType pt, Square s, Bitboard occupied);

}  // namespace chess
