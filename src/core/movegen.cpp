#include "movegen.h"

namespace chess {

namespace {

template <GenType G>
void add_promotions(MoveList& list, Square from, Square to, bool capture) {
    if (capture) {
        if constexpr (G != GenType::Quiet) {
            list.add(Move(from, to, PROMO_CAPTURE_Q));
            list.add(Move(from, to, PROMO_CAPTURE_R));
            list.add(Move(from, to, PROMO_CAPTURE_B));
            list.add(Move(from, to, PROMO_CAPTURE_N));
        }
    } else {
        if constexpr (G != GenType::Quiet) list.add(Move(from, to, PROMO_Q));
        if constexpr (G != GenType::Noisy) {
            list.add(Move(from, to, PROMO_R));
            list.add(Move(from, to, PROMO_B));
            list.add(Move(from, to, PROMO_N));
        }
    }
}

template <Color Us, GenType G>
void generate_pawn_moves(const Position& pos, MoveList& list, Bitboard check_mask, Bitboard pinned,
                         Square ksq) {
    constexpr Color Them = ~Us;
    constexpr Direction Up = Us == WHITE ? NORTH : SOUTH;
    constexpr Direction UpWest = Us == WHITE ? NORTH_WEST : SOUTH_WEST;
    constexpr Direction UpEast = Us == WHITE ? NORTH_EAST : SOUTH_EAST;
    constexpr Bitboard PromotionRank = Us == WHITE ? rank_bb(6) : rank_bb(1);  // pawns about to promote
    constexpr Bitboard DoublePushRank = Us == WHITE ? rank_bb(2) : rank_bb(5);  // after the first push

    const Bitboard pawns = pos.pieces(Us, PAWN);
    const Bitboard empty = ~pos.pieces();
    const Bitboard enemies = pos.pieces(Them);
    const Bitboard promoting = pawns & PromotionRank;
    const Bitboard normal = pawns & ~PromotionRank;

    // A pinned pawn may only move along the line through its king.
    auto pin_ok = [&](Square from, Square to) {
        return !(pinned & square_bb(from)) || (line_bb(ksq, from) & square_bb(to));
    };
    auto emit = [&](Bitboard targets, int delta, int flag) {
        while (targets) {
            const Square to = pop_lsb(targets);
            const Square from = to - delta;
            if (pin_ok(from, to)) list.add(Move(from, to, flag));
        }
    };

    if constexpr (G != GenType::Noisy) {
        const Bitboard single = shift<Up>(normal) & empty;
        const Bitboard dbl = shift<Up>(single & DoublePushRank) & empty;
        emit(single & check_mask, Up, QUIET);
        emit(dbl & check_mask, 2 * Up, DOUBLE_PUSH);
    }

    if (promoting) {
        auto emit_promotions = [&](Bitboard targets, int delta, bool capture) {
            while (targets) {
                const Square to = pop_lsb(targets);
                const Square from = to - delta;
                if (pin_ok(from, to)) add_promotions<G>(list, from, to, capture);
            }
        };
        emit_promotions(shift<Up>(promoting) & empty & check_mask, Up, false);
        emit_promotions(shift<UpWest>(promoting) & enemies & check_mask, UpWest, true);
        emit_promotions(shift<UpEast>(promoting) & enemies & check_mask, UpEast, true);
    }

    if constexpr (G != GenType::Quiet) {
        emit(shift<UpWest>(normal) & enemies & check_mask, UpWest, CAPTURE);
        emit(shift<UpEast>(normal) & enemies & check_mask, UpEast, CAPTURE);

        const Square ep = pos.ep_square();
        if (ep != NO_SQUARE) {
            const Square captured = ep - Up;
            const Bitboard theirs_without_victim = enemies ^ square_bb(captured);
            Bitboard candidates = attacks::Pawn[Them][ep] & normal;
            while (candidates) {
                const Square from = pop_lsb(candidates);
                // Simulate the capture and look for any attack on our king; this
                // covers pins, discovered rank attacks and unresolved checks.
                const Bitboard occ =
                    (pos.pieces() ^ square_bb(from) ^ square_bb(captured)) | square_bb(ep);
                if (!(pos.attackers_to(ksq, occ) & theirs_without_victim))
                    list.add(Move(from, ep, EP_CAPTURE));
            }
        }
    }
}

template <Color Us, GenType G>
void generate_castling(const Position& pos, MoveList& list) {
    struct Castle {
        int right;
        Square king_to;
        Bitboard must_be_empty;
        Bitboard must_be_safe;
        int flag;
    };
    constexpr Square E = relative_square(Us, E1);
    constexpr int R = Us == WHITE ? 0 : 56;
    constexpr Castle castles[2] = {
        {Us == WHITE ? WHITE_OO : BLACK_OO, relative_square(Us, G1),
         (square_bb(F1) | square_bb(G1)) << R, (square_bb(F1) | square_bb(G1)) << R, KING_CASTLE},
        {Us == WHITE ? WHITE_OOO : BLACK_OOO, relative_square(Us, C1),
         (square_bb(B1) | square_bb(C1) | square_bb(D1)) << R,
         (square_bb(C1) | square_bb(D1)) << R, QUEEN_CASTLE},
    };

    for (const Castle& c : castles) {
        if (!(pos.castling_rights() & c.right) || (pos.pieces() & c.must_be_empty)) continue;
        bool safe = true;
        for (Bitboard b = c.must_be_safe; b && safe;)
            safe = !(pos.attackers_to(pop_lsb(b)) & pos.pieces(~Us));
        if (safe) list.add(Move(E, c.king_to, c.flag));
    }
}

template <Color Us, GenType G>
void generate_all(const Position& pos, MoveList& list) {
    constexpr Color Them = ~Us;
    const Square ksq = pos.king_square(Us);
    const Bitboard occupied = pos.pieces();
    const Bitboard ours = pos.pieces(Us), theirs = pos.pieces(Them);
    const Bitboard checkers = pos.checkers();

    const Bitboard type_mask = G == GenType::Noisy ? theirs
                               : G == GenType::Quiet ? ~occupied
                                                     : ~ours;

    // King moves. Sliders must "see through" the king, otherwise stepping
    // back along a checking ray would look safe.
    {
        const Bitboard occ_without_king = occupied ^ square_bb(ksq);
        Bitboard targets = attacks::King[ksq] & type_mask;
        while (targets) {
            const Square to = pop_lsb(targets);
            if (!(pos.attackers_to(to, occ_without_king) & theirs))
                list.add(Move(ksq, to, (theirs & square_bb(to)) ? CAPTURE : QUIET));
        }
    }

    // In double check only the king can move.
    if (more_than_one(checkers)) return;

    // In single check, other pieces must capture the checker or block the ray.
    const Bitboard check_mask = checkers ? between_bb(ksq, lsb(checkers)) | checkers : ~0ULL;
    const Bitboard pinned = pos.king_blockers(Us) & ours;

    generate_pawn_moves<Us, G>(pos, list, check_mask, pinned, ksq);

    for (PieceType pt : {KNIGHT, BISHOP, ROOK, QUEEN}) {
        Bitboard pieces = pos.pieces(Us, pt);
        if (pt == KNIGHT) pieces &= ~pinned;  // a pinned knight can never move
        while (pieces) {
            const Square from = pop_lsb(pieces);
            Bitboard targets = attacks_bb(pt, from, occupied) & type_mask & check_mask;
            if (pinned & square_bb(from)) targets &= line_bb(ksq, from);
            while (targets) {
                const Square to = pop_lsb(targets);
                list.add(Move(from, to, (theirs & square_bb(to)) ? CAPTURE : QUIET));
            }
        }
    }

    if constexpr (G != GenType::Noisy) {
        if (!checkers) generate_castling<Us, G>(pos, list);
    }
}

}  // namespace

template <GenType G>
void generate(const Position& pos, MoveList& list) {
    if (pos.side_to_move() == WHITE) generate_all<WHITE, G>(pos, list);
    else generate_all<BLACK, G>(pos, list);
}

template void generate<GenType::Noisy>(const Position&, MoveList&);
template void generate<GenType::Quiet>(const Position&, MoveList&);
template void generate<GenType::All>(const Position&, MoveList&);

std::uint64_t perft(Position& pos, int depth) {
    if (depth <= 0) return 1;
    MoveList moves;
    generate<GenType::All>(pos, moves);
    if (depth == 1) return moves.size();  // bulk counting at the frontier
    std::uint64_t nodes = 0;
    for (const Move m : moves) {
        pos.make_move(m);
        nodes += perft(pos, depth - 1);
        pos.unmake_move(m);
    }
    return nodes;
}

}  // namespace chess
