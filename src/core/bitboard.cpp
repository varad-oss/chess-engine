#include "bitboard.h"

#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "rng.h"

namespace chess {

namespace attacks {
Bitboard Pawn[COLOR_NB][SQUARE_NB];
Bitboard Knight[SQUARE_NB];
Bitboard King[SQUARE_NB];
Bitboard Between[SQUARE_NB][SQUARE_NB];
Bitboard Line[SQUARE_NB][SQUARE_NB];
Magic RookMagics[SQUARE_NB];
Magic BishopMagics[SQUARE_NB];
}  // namespace attacks

namespace {

// Sizes of the shared attack tables: sum over squares of 2^(relevant mask bits).
constexpr int ROOK_TABLE_SIZE = 102400;
constexpr int BISHOP_TABLE_SIZE = 5248;
Bitboard RookTable[ROOK_TABLE_SIZE];
Bitboard BishopTable[BISHOP_TABLE_SIZE];

bool on_board(int file, int rank) { return file >= 0 && file < 8 && rank >= 0 && rank < 8; }

Bitboard step_attacks(Square s, const int (&deltas)[8][2], int count) {
    Bitboard b = 0;
    for (int i = 0; i < count; ++i) {
        const int f = file_of(s) + deltas[i][0], r = rank_of(s) + deltas[i][1];
        if (on_board(f, r)) b |= square_bb(make_square(f, r));
    }
    return b;
}

#include "magics.inc"  // ROOK_MAGICS, BISHOP_MAGICS: precomputed by `chess-engine magics`

int g_magic_searches = 0;  // squares whose embedded magic failed verification

// Fills the attack table of one square for the multiplier in m.magic.
// Returns false on a destructive collision (two blocker sets with different
// attack sets mapping to the same slot), i.e. if the magic is invalid.
bool try_magic(attacks::Magic& m, Bitboard* slot, const std::vector<Bitboard>& occupancy,
               const std::vector<Bitboard>& reference, int size, std::vector<int>& epoch, int& attempt) {
    ++attempt;
    for (int i = 0; i < size; ++i) {
        const unsigned idx = m.index(occupancy[size_t(i)]);
        if (epoch[idx] < attempt) {
            epoch[idx] = attempt;
            slot[idx] = reference[size_t(i)];
        } else if (slot[idx] != reference[size_t(i)]) {
            return false;
        }
    }
    return true;
}

// Builds the tables for one slider type. Each square first tries its embedded
// magic; only if that fails verification (e.g. the tables were edited) does it
// fall back to a trial-and-error search with a fixed-seed PRNG.
void init_magics(PieceType pt, Bitboard* table, attacks::Magic* magics, const Bitboard* embedded,
                 Bitboard* found = nullptr) {
    // Scratch buffers sized for the largest mask (12 bits for a rook in a corner).
    std::vector<Bitboard> occupancy(4096), reference(4096);
    std::vector<int> epoch(4096, 0);
    int attempt = 0;
    Rng rng(0x9E3779B97F4A7C15ULL ^ Key(pt));
    Bitboard* slot = table;

    for (int sq = 0; sq < SQUARE_NB; ++sq) {
        const Square s = Square(sq);
        attacks::Magic& m = magics[sq];

        // Edge squares never block anything beyond themselves, so they are
        // excluded from the mask (unless the piece itself sits on that edge).
        const Bitboard edges = ((RANK_1_BB | RANK_8_BB) & ~rank_bb(rank_of(s))) |
                               ((FILE_A_BB | FILE_H_BB) & ~file_bb(file_of(s)));
        m.mask = sliding_attacks_slow(pt, s, 0) & ~edges;
        m.shift = unsigned(64 - popcount(m.mask));
        m.table = slot;

        // Enumerate every subset of the mask (Carry-Rippler trick).
        int size = 0;
        Bitboard b = 0;
        do {
            occupancy[size_t(size)] = b;
            reference[size_t(size)] = sliding_attacks_slow(pt, s, b);
            ++size;
            b = (b - m.mask) & m.mask;
        } while (b);

        m.magic = embedded ? embedded[sq] : 0;
        if (!embedded || !try_magic(m, slot, occupancy, reference, size, epoch, attempt)) {
            if (embedded) ++g_magic_searches;
            do {
                do {
                    m.magic = rng.sparse();
                } while (popcount((m.magic * m.mask) >> 56) < 6);
            } while (!try_magic(m, slot, occupancy, reference, size, epoch, attempt));
        }
        if (found) found[sq] = m.magic;
        slot += size;
    }
}

void init_all() {
    constexpr int knight_deltas[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2},
                                         {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
    constexpr int king_deltas[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1},
                                       {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
    constexpr int white_pawn[8][2] = {{-1, 1}, {1, 1}};
    constexpr int black_pawn[8][2] = {{-1, -1}, {1, -1}};

    for (int sq = 0; sq < SQUARE_NB; ++sq) {
        const Square s = Square(sq);
        attacks::Knight[s] = step_attacks(s, knight_deltas, 8);
        attacks::King[s] = step_attacks(s, king_deltas, 8);
        attacks::Pawn[WHITE][s] = step_attacks(s, white_pawn, 2);
        attacks::Pawn[BLACK][s] = step_attacks(s, black_pawn, 2);
    }

    init_magics(ROOK, RookTable, attacks::RookMagics, ROOK_MAGICS);
    init_magics(BISHOP, BishopTable, attacks::BishopMagics, BISHOP_MAGICS);

    for (int a = 0; a < SQUARE_NB; ++a) {
        for (int b = 0; b < SQUARE_NB; ++b) {
            const Square s1 = Square(a), s2 = Square(b);
            attacks::Between[a][b] = attacks::Line[a][b] = 0;
            if (a == b) continue;
            for (PieceType pt : {BISHOP, ROOK}) {
                if (sliding_attacks_slow(pt, s1, 0) & square_bb(s2)) {
                    attacks::Line[a][b] = (sliding_attacks_slow(pt, s1, 0) &
                                           sliding_attacks_slow(pt, s2, 0)) |
                                          square_bb(s1) | square_bb(s2);
                    attacks::Between[a][b] = sliding_attacks_slow(pt, s1, square_bb(s2)) &
                                             sliding_attacks_slow(pt, s2, square_bb(s1));
                }
            }
        }
    }
}

}  // namespace

Bitboard sliding_attacks_slow(PieceType pt, Square s, Bitboard occupied) {
    static constexpr int rook_dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    static constexpr int bishop_dirs[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    const auto& dirs = pt == ROOK ? rook_dirs : bishop_dirs;

    Bitboard result = 0;
    for (const auto& d : dirs) {
        int f = file_of(s) + d[0], r = rank_of(s) + d[1];
        while (on_board(f, r)) {
            const Bitboard b = square_bb(make_square(f, r));
            result |= b;
            if (occupied & b) break;
            f += d[0];
            r += d[1];
        }
    }
    return result;
}

void init_bitboards() {
    static std::once_flag once;
    std::call_once(once, init_all);
}

int magic_search_fallbacks() {
    init_bitboards();
    return g_magic_searches;
}

std::string generate_magics_source() {
    init_bitboards();
    // Searches into scratch tables so the live tables are untouched.
    std::vector<Bitboard> rook_table(ROOK_TABLE_SIZE), bishop_table(BISHOP_TABLE_SIZE);
    attacks::Magic rook[SQUARE_NB], bishop[SQUARE_NB];
    Bitboard rook_found[SQUARE_NB], bishop_found[SQUARE_NB];
    init_magics(ROOK, rook_table.data(), rook, nullptr, rook_found);
    init_magics(BISHOP, bishop_table.data(), bishop, nullptr, bishop_found);

    std::ostringstream out;
    out << "// Magic multipliers for the sliding-attack tables. Generated by\n"
           "// `chess-engine magics` (a fixed-seed trial-and-error search); they are\n"
           "// re-verified against ray-walked attacks every time the engine starts.\n";
    for (auto [name, values] : {std::pair{"ROOK_MAGICS", rook_found}, std::pair{"BISHOP_MAGICS", bishop_found}}) {
        out << "constexpr Bitboard " << name << "[SQUARE_NB] = {\n";
        for (int i = 0; i < SQUARE_NB; ++i)
            out << (i % 4 == 0 ? "    " : " ") << "0x" << std::hex << std::setw(16) << std::setfill('0')
                << values[i] << std::dec << "ULL," << (i % 4 == 3 ? "\n" : "");
        out << "};\n";
    }
    return out.str();
}

}  // namespace chess
