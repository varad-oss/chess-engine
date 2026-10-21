// Tests for the rules core: attack tables, FEN handling, make/unmake
// invariants, move generation, SEE and draw detection.
#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/epd.h"
#include "core/movegen.h"
#include "core/position.h"
#include "core/rng.h"
#include "framework.h"

using namespace chess;

namespace {

Position from_fen(const std::string& fen) {
    Position pos;
    std::string error;
    if (!pos.set_fen(fen, &error)) throw std::runtime_error("bad test FEN '" + fen + "': " + error);
    return pos;
}

void play(Position& pos, const std::vector<std::string>& moves) {
    for (const std::string& text : moves) {
        const Move m = pos.parse_uci_move(text);
        if (!m.is_ok()) throw std::runtime_error("illegal test move " + text);
        pos.make_move(m);
    }
}

std::set<std::uint16_t> as_set(const MoveList& list) {
    std::set<std::uint16_t> s;
    for (const Move m : list) s.insert(m.raw());
    return s;
}

std::vector<PerftCase> suite() {
    std::vector<PerftCase> cases;
    std::string error;
    if (!load_perft_suite(TEST_DATA_DIR "/perft.epd", cases, &error)) throw std::runtime_error(error);
    return cases;
}

}  // namespace

TEST_CASE(magic_attacks_match_ray_walking) {
    Rng rng(42);
    for (int sq = 0; sq < SQUARE_NB; ++sq) {
        for (int i = 0; i < 2000; ++i) {
            // Mix sparse and dense occupancies.
            const Bitboard occ = (i & 1) ? rng.sparse() : rng.next() & rng.next();
            const Square s = Square(sq);
            CHECK(bishop_attacks(s, occ) == sliding_attacks_slow(BISHOP, s, occ));
            CHECK(rook_attacks(s, occ) == sliding_attacks_slow(ROOK, s, occ));
        }
    }
}

TEST_CASE(embedded_magics_verify_without_search) {
    // Every embedded multiplier passed the startup collision check, so no
    // square fell back to searching (which would cost startup time).
    CHECK_EQ(magic_search_fallbacks(), 0);
}

TEST_CASE(between_and_line_tables) {
    CHECK_EQ(popcount(between_bb(A1, H8)), 6);
    CHECK_EQ(popcount(line_bb(A1, H8)), 8);
    CHECK_EQ(between_bb(E1, E8), file_bb(4) & ~(square_bb(E1) | square_bb(E8)));
    CHECK_EQ(between_bb(A1, B3), Bitboard(0));  // not aligned
    CHECK_EQ(line_bb(A1, B3), Bitboard(0));
    CHECK_EQ(between_bb(D4, E5), Bitboard(0));  // adjacent
    CHECK(line_bb(D4, E5) & square_bb(H8));
}

TEST_CASE(fen_round_trip) {
    for (const PerftCase& c : suite()) {
        const Position pos = from_fen(c.fen);
        CHECK_EQ(pos.fen(), c.fen);
        CHECK(pos.is_consistent());
    }
}

TEST_CASE(fen_optional_counters_and_whitespace) {
    const Position pos = from_fen("  rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR   b KQkq -  ");
    CHECK_EQ(pos.rule50(), 0);
    CHECK_EQ(pos.fen(), std::string("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"));
}

TEST_CASE(fen_rejects_malformed_input_without_side_effects) {
    const std::vector<std::string> bad = {
        "",
        "garbage",
        "8/8/8/8/8/8/8/8 w - - 0 1",                                    // no kings
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR x KQkq - 0 1",     // bad side
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBN w KQkq - 0 1",      // short rank
        "rnbqkbnr/pppppppp/9/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",     // rank overflow
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP w KQkq - 0 1",              // 7 ranks
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR/8 w KQkq - 0 1",   // 9 ranks
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKXNR w KQkq - 0 1",     // bad piece
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQxq - 0 1",     // bad castling
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq e9 0 1",    // bad ep square
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq e3 0 1",    // ep on wrong rank
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq e6 0 1",    // no pawn pushed
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - x 1",     // bad clock
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - -3 1",    // negative clock
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1 99",  // too many fields
        "Pnbqkbnr/pppppppp/8/8/8/8/1PPPPPPP/RNBQKBNR w KQkq - 0 1",     // pawn on rank 8
        "4k3/8/8/8/8/8/8/4K2R b - - 0 1",                               // ok shape...
        "4k2R/8/8/8/8/8/8/4K3 w - - 0 1",                               // side not to move in check
    };
    for (size_t i = 0; i < bad.size(); ++i) {
        if (i == bad.size() - 2) continue;  // the "ok shape" control entry is legal
        Position pos;
        play(pos, {"e2e4"});
        const std::string before = pos.fen();
        std::string error;
        CHECK(!pos.set_fen(bad[i], &error));
        CHECK(!error.empty());
        CHECK_EQ(pos.fen(), before);
    }
    Position control;
    CHECK(control.set_fen(bad[bad.size() - 2]));
}

TEST_CASE(fen_sanitises_impossible_castling_rights) {
    const Position pos = from_fen("4k3/8/8/8/8/8/8/4K3 w KQkq - 0 1");
    CHECK_EQ(pos.castling_rights(), int(NO_CASTLING));
}

TEST_CASE(en_passant_square_is_canonical) {
    // After 1.e4 no black pawn can capture en passant, so no ep square is
    // recorded and the hash equals that of the same position set from FEN.
    Position played;
    play(played, {"e2e4"});
    CHECK_EQ(played.ep_square(), NO_SQUARE);
    const Position loaded = from_fen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
    CHECK_EQ(loaded.key(), played.key());

    Position capturable;
    play(capturable, {"e2e4", "a7a6", "e4e5", "d7d5"});
    CHECK_EQ(capturable.ep_square(), D6);
    CHECK(capturable.parse_uci_move("e5d6").is_ep());
}

TEST_CASE(make_unmake_random_walks_preserve_invariants) {
    Rng rng(2024);
    int plies = 0;
    for (const PerftCase& c : suite()) {
        for (int walk = 0; walk < 20; ++walk) {
            Position pos = from_fen(c.fen);
            std::vector<Move> line;
            std::vector<std::string> fens{pos.fen()};
            for (int ply = 0; ply < 120; ++ply) {
                MoveList moves;
                generate<GenType::All>(pos, moves);
                if (moves.empty() || pos.is_draw(0)) break;
                const Move m = moves[rng.below(moves.size())];
                pos.make_move(m);
                ++plies;
                line.push_back(m);
                fens.push_back(pos.fen());
                std::string why;
                if (!pos.is_consistent(&why)) {
                    CHECK(why.empty());
                    return;
                }
                // The side that just moved must not have left its king in check.
                CHECK(!(pos.attackers_to(pos.king_square(~pos.side_to_move())) &
                        pos.pieces(pos.side_to_move())));
            }
            while (!line.empty()) {
                pos.unmake_move(line.back());
                line.pop_back();
                fens.pop_back();
                CHECK_EQ(pos.fen(), fens.back());
                CHECK(pos.is_consistent());
            }
        }
    }
    CHECK(plies > 10000);
}

TEST_CASE(null_move_round_trip) {
    Position pos = from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    play(pos, {"a2a4"});  // black may capture b4xa3 en passant
    REQUIRE(pos.ep_square() == A3);
    const Key key = pos.key();
    const std::string fen = pos.fen();
    pos.make_null_move();
    CHECK(pos.is_consistent());
    CHECK_EQ(pos.ep_square(), NO_SQUARE);
    CHECK(pos.key() != key);
    pos.unmake_null_move();
    CHECK_EQ(pos.key(), key);
    CHECK_EQ(pos.fen(), fen);
}

TEST_CASE(noisy_and_quiet_partition_all_moves) {
    Rng rng(7);
    for (const PerftCase& c : suite()) {
        Position pos = from_fen(c.fen);
        for (int ply = 0; ply < 60; ++ply) {
            MoveList all, noisy, quiet;
            generate<GenType::All>(pos, all);
            generate<GenType::Noisy>(pos, noisy);
            generate<GenType::Quiet>(pos, quiet);
            CHECK_EQ(all.size(), noisy.size() + quiet.size());
            std::set<std::uint16_t> joined = as_set(noisy);
            for (const Move m : quiet) joined.insert(m.raw());
            CHECK(joined == as_set(all));
            for (const Move m : noisy) CHECK(m.is_noisy());
            for (const Move m : quiet) CHECK(!m.is_noisy());
            if (all.empty()) break;
            pos.make_move(all[rng.below(all.size())]);
        }
    }
}

TEST_CASE(perft_suite_shallow) {
    // Depths are capped so this runs in about a second; `make perft` runs the
    // full suite to the depths recorded in the data file.
    constexpr std::uint64_t budget = 5'000'000;
    for (const PerftCase& c : suite()) {
        Position pos = from_fen(c.fen);
        for (const auto& [depth, expected] : c.depths) {
            if (expected > budget) continue;
            CHECK_EQ(perft(pos, depth), expected);
        }
        CHECK_EQ(pos.fen(), c.fen);  // perft must leave the position untouched
    }
}

TEST_CASE(static_exchange_evaluation) {
    struct Case {
        const char* fen;
        const char* move;
        int expected;
    };
    const Case cases[] = {
        {"1k1r4/1pp4p/p7/4p3/8/P5P1/1PP4P/2K1R3 w - - 0 1", "e1e5", 100},
        {"1k1r3q/1ppn3p/p4b2/4p3/8/P2N2P1/1PP1R1BP/2K1Q3 w - - 0 1", "d3e5", -220},
        {"4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1", "e4d5", 100},
        {"4k3/8/2p5/3p4/4P3/8/8/4K3 w - - 0 1", "e4d5", 0},
        {"4k3/8/2p5/3p4/8/8/8/3QK3 w - - 0 1", "d1d5", -800},
        {"4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", "e5d6", 100},
        {"4k3/P7/8/8/8/8/8/4K3 w - - 0 1", "a7a8q", 800},
        {"3rk3/3r4/8/3p4/8/8/3R4/3RK3 w - - 0 1", "d2d5", -400},
        {"4k3/8/8/8/8/8/8/4K2R w K - 0 1", "e1g1", 0},
    };
    for (const Case& c : cases) {
        const Position pos = from_fen(c.fen);
        const Move m = pos.parse_uci_move(c.move);
        REQUIRE(m.is_ok());
        CHECK_EQ(pos.see(m), c.expected);
        CHECK(pos.see_ge(m, c.expected));
        CHECK(!pos.see_ge(m, c.expected + 1));
    }
}

TEST_CASE(repetition_detection) {
    Position pos;
    const std::vector<std::string> shuffle = {"g1f3", "g8f6", "f3g1", "f6g8"};
    play(pos, shuffle);
    CHECK(!pos.is_repetition(0));  // second occurrence: not yet a threefold
    CHECK(pos.is_repetition(4));   // but a repetition inside a 4-ply search
    CHECK(!pos.is_repetition(3));
    play(pos, shuffle);
    CHECK(pos.is_repetition(0));  // third occurrence

    // An irreversible move resets the window.
    Position pawn;
    play(pawn, {"g1f3", "g8f6", "f3g1", "f6g8", "e2e4"});
    CHECK(!pawn.is_repetition(10));
}

TEST_CASE(fifty_move_rule_and_mate_precedence) {
    Position quiet = from_fen("8/8/8/4k3/8/8/8/R3K3 w - - 99 80");
    CHECK(!quiet.is_fifty_move_draw());
    play(quiet, {"a1a2"});
    CHECK(quiet.is_fifty_move_draw());

    Position mate = from_fen("7k/8/6K1/8/8/8/8/R7 w - - 99 80");
    play(mate, {"a1a8"});
    MoveList replies;
    generate<GenType::All>(mate, replies);
    CHECK(mate.in_check());
    CHECK(replies.empty());
    CHECK(!mate.is_fifty_move_draw());
}

TEST_CASE(insufficient_material) {
    CHECK(from_fen("8/8/8/4k3/8/8/8/4K3 w - - 0 1").is_insufficient_material());
    CHECK(from_fen("8/8/8/4k3/8/8/8/4KB2 w - - 0 1").is_insufficient_material());
    CHECK(from_fen("8/8/8/4k3/8/8/8/4KN2 w - - 0 1").is_insufficient_material());
    CHECK(from_fen("2b5/8/8/4k3/8/8/8/4KB2 w - - 0 1").is_insufficient_material());
    CHECK(!from_fen("3b4/8/8/4k3/8/8/8/4KB2 w - - 0 1").is_insufficient_material());
    CHECK(!from_fen("8/8/8/4k3/8/8/8/3NKN2 w - - 0 1").is_insufficient_material());
    CHECK(!from_fen("8/8/8/4k3/8/8/4P3/4K3 w - - 0 1").is_insufficient_material());
}

TEST_CASE(uci_move_parsing) {
    Position pos = from_fen("4k3/1P6/8/8/8/8/8/4K2R w K - 0 1");
    CHECK(pos.parse_uci_move("b7b8q").promotion_type() == QUEEN);
    CHECK(pos.parse_uci_move("b7b8N").promotion_type() == KNIGHT);
    CHECK(pos.parse_uci_move("e1g1").is_castle());
    CHECK(!pos.parse_uci_move("b7b8").is_ok());  // promotion piece required
    CHECK(!pos.parse_uci_move("e1e3").is_ok());
    CHECK(!pos.parse_uci_move("").is_ok());
    CHECK(!pos.parse_uci_move("zz").is_ok());
    CHECK_EQ(Move::none().uci(), std::string("0000"));
}
