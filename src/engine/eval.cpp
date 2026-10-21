#include "eval.h"

#include <algorithm>

namespace chess::eval {

namespace {

constexpr int PhaseWeight[PIECE_TYPE_NB] = {0, 1, 1, 2, 4, 0};
constexpr int MobilityBase[PIECE_TYPE_NB] = {0, idx::KnightMobility, idx::BishopMobility,
                                             idx::RookMobility, idx::QueenMobility, 0};
// Fixed material values used only to classify drawish endgames.
constexpr int NonPawnValue[PIECE_TYPE_NB] = {0, 320, 330, 500, 900, 0};

// Ranks strictly in front of `rank` from c's point of view.
constexpr Bitboard ranks_ahead(Color c, int rank) {
    if (c == WHITE) return rank >= 7 ? 0 : ~0ULL << (8 * (rank + 1));
    return rank <= 0 ? 0 : ~0ULL >> (8 * (8 - rank));
}

constexpr Bitboard adjacent_files(int file) {
    return (file > 0 ? file_bb(file - 1) : 0) | (file < 7 ? file_bb(file + 1) : 0);
}

template <bool Tracing>
struct Accumulator {
    Score score[COLOR_NB];
    Trace* trace;

    void add(Color c, int index, int n = 1) {
        score[c] += Params[index] * n;
        if constexpr (Tracing) trace->coef[index][c] += n;
    }
};

int non_pawn_material(const Position& pos, Color c) {
    int total = 0;
    for (PieceType pt : {KNIGHT, BISHOP, ROOK, QUEEN}) total += pos.count(c, pt) * NonPawnValue[pt];
    return total;
}

// Endgame scale factor (out of 128) for material configurations that the
// linear terms systematically overrate.
int endgame_scale(const Position& pos, int eg) {
    const Color strong = eg >= 0 ? WHITE : BLACK, weak = ~strong;
    if (!pos.pieces(strong, PAWN) &&
        non_pawn_material(pos, strong) - non_pawn_material(pos, weak) <= NonPawnValue[BISHOP])
        return SCALE_NORMAL / 8;  // e.g. KR vs KB, KN vs K: very hard or impossible to win

    const Bitboard bishops = pos.pieces(BISHOP);
    if (pos.count(WHITE, BISHOP) == 1 && pos.count(BLACK, BISHOP) == 1 &&
        !pos.pieces(KNIGHT) && !pos.pieces(ROOK, QUEEN) &&
        !!(bishops & LIGHT_SQUARES_BB) && !!(bishops & DARK_SQUARES_BB))
        return SCALE_NORMAL / 2;  // pure opposite-coloured bishops
    return SCALE_NORMAL;
}

template <bool Tracing>
Value evaluate_white(const Position& pos, Trace* trace) {
    Accumulator<Tracing> acc{{}, trace};
    const Bitboard occupied = pos.pieces();
    const Bitboard pawn_attacks[COLOR_NB] = {pawn_attacks_bb(WHITE, pos.pieces(WHITE, PAWN)),
                                             pawn_attacks_bb(BLACK, pos.pieces(BLACK, PAWN))};
    Bitboard king_zone[COLOR_NB];
    for (Color c : {WHITE, BLACK})
        king_zone[c] = attacks::King[pos.king_square(c)] | square_bb(pos.king_square(c));

    int phase = 0;
    for (Color c : {WHITE, BLACK}) {
        const Color them = ~c;
        const Bitboard our_pawns = pos.pieces(c, PAWN), their_pawns = pos.pieces(them, PAWN);
        const Bitboard mobility_area = ~(pos.pieces(c) | pawn_attacks[them]);

        for (int p = PAWN; p <= KING; ++p) {
            const PieceType pt = PieceType(p);
            phase += PhaseWeight[pt] * pos.count(c, pt);
            for (Bitboard b = pos.pieces(c, pt); b;) {
                const Square s = pop_lsb(b);
                if (pt != KING) acc.add(c, idx::Material + pt);
                acc.add(c, idx::Pst + pt * 64 + relative_square(c, s));

                if (pt >= KNIGHT && pt <= QUEEN) {
                    const Bitboard att = attacks_bb(pt, s, occupied);
                    acc.add(c, MobilityBase[pt] + popcount(att & mobility_area));
                    if (const int hits = popcount(att & king_zone[them]))
                        acc.add(c, idx::KingZoneAttack + pt - KNIGHT, hits);
                }
                if (pt == ROOK) {
                    const Bitboard file = file_bb(file_of(s));
                    if (!(pos.pieces(PAWN) & file)) acc.add(c, idx::RookOpenFile);
                    else if (!(our_pawns & file)) acc.add(c, idx::RookSemiOpenFile);
                }
            }
        }

        if (pos.count(c, BISHOP) >= 2) acc.add(c, idx::BishopPair);

        for (Bitboard b = our_pawns; b;) {
            const Square s = pop_lsb(b);
            const int file = file_of(s), rrank = relative_rank(c, s);
            const Bitboard ahead = ranks_ahead(c, rank_of(s));
            if (!(our_pawns & adjacent_files(file))) acc.add(c, idx::IsolatedPawn);
            if (our_pawns & ahead & file_bb(file)) acc.add(c, idx::DoubledPawn);
            const Bitboard supporters = pawn_attacks_bb(them, square_bb(s)) & our_pawns;
            const Bitboard phalanx = (shift<EAST>(square_bb(s)) | shift<WEST>(square_bb(s))) & our_pawns;
            if (supporters | phalanx) acc.add(c, idx::ConnectedPawn + rrank);
            if (!(their_pawns & ahead & (file_bb(file) | adjacent_files(file))))
                acc.add(c, idx::PassedPawn + rrank);
        }

        // Pawn shield: own pawns on the king's file and its neighbours, one
        // and two ranks in front of the king.
        const Square ksq = pos.king_square(c);
        const Bitboard shield_files = file_bb(file_of(ksq)) | adjacent_files(file_of(ksq));
        const Bitboard ahead = ranks_ahead(c, rank_of(ksq));
        const Bitboard first = ahead & ~ranks_ahead(c, rank_of(ksq) + (c == WHITE ? 1 : -1));
        const Bitboard second = (ahead & ~first) & ~ranks_ahead(c, rank_of(ksq) + (c == WHITE ? 2 : -2));
        acc.add(c, idx::KingShield + 0, popcount(our_pawns & shield_files & first));
        acc.add(c, idx::KingShield + 1, popcount(our_pawns & shield_files & second));

        acc.add(c, idx::PawnThreat,
                popcount(pawn_attacks[c] & pos.pieces(them) & ~pos.pieces(PAWN, KING)));
    }
    acc.add(pos.side_to_move(), idx::Tempo);

    phase = std::min(phase, PHASE_MAX);
    const Score total = acc.score[WHITE] - acc.score[BLACK];
    const int scale = endgame_scale(pos, total.eg);
    if constexpr (Tracing) {
        trace->phase = phase;
        trace->eg_scale = scale;
    }
    return (total.mg * phase + total.eg * (PHASE_MAX - phase) * scale / SCALE_NORMAL) / PHASE_MAX;
}

}  // namespace

Value evaluate(const Position& pos) {
    const Value v = evaluate_white<false>(pos, nullptr);
    return pos.side_to_move() == WHITE ? v : -v;
}

Value trace(const Position& pos, Trace& out) {
    out = Trace{};
    return evaluate_white<true>(pos, &out);
}

}  // namespace chess::eval
