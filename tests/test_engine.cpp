// Tests for evaluation, the transposition table and the search.
#include <algorithm>
#include <chrono>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "app/commands.h"
#include "core/epd.h"
#include "core/movegen.h"
#include "core/rng.h"
#include "engine/eval.h"
#include "engine/search.h"
#include "engine/tt.h"
#include "framework.h"
#include "uci/uci.h"

using namespace chess;

namespace {

Position from_fen(const std::string& fen) {
    Position pos;
    std::string error;
    if (!pos.set_fen(fen, &error)) throw std::runtime_error("bad test FEN '" + fen + "': " + error);
    return pos;
}

// The same position with colours swapped and the board flipped vertically.
std::string mirror_fen(const std::string& fen) {
    std::istringstream in(fen);
    std::string placement, stm, castling, ep, half = "0", full = "1";
    in >> placement >> stm >> castling >> ep >> half >> full;
    std::vector<std::string> ranks;
    std::string rank;
    for (char c : placement + "/") {
        if (c == '/') {
            ranks.push_back(rank);
            rank.clear();
        } else {
            rank += std::isalpha(static_cast<unsigned char>(c))
                        ? char(std::isupper(static_cast<unsigned char>(c)) ? std::tolower(c) : std::toupper(c))
                        : c;
        }
    }
    std::reverse(ranks.begin(), ranks.end());
    std::string out;
    for (size_t i = 0; i < ranks.size(); ++i) out += (i ? "/" : "") + ranks[i];
    std::string cr;
    for (char c : std::string("KQkq"))
        if (castling.find(char(std::isupper(static_cast<unsigned char>(c)) ? std::tolower(c) : std::toupper(c))) !=
            std::string::npos)
            cr += c;
    if (cr.empty()) cr = "-";
    if (ep != "-") ep[1] = ep[1] == '3' ? '6' : '3';
    return out + (stm == "w" ? " b " : " w ") + cr + " " + ep + " " + half + " " + full;
}

std::vector<Position> sample_positions(int count) {
    std::vector<Position> out;
    Rng rng(99);
    const auto& fens = bench_positions();
    while (int(out.size()) < count) {
        Position pos = from_fen(fens[out.size() % fens.size()]);
        const int plies = int(rng.below(30));
        for (int i = 0; i < plies; ++i) {
            MoveList moves;
            generate<GenType::All>(pos, moves);
            if (moves.empty()) break;
            pos.make_move(moves[rng.below(moves.size())]);
        }
        out.push_back(pos);
    }
    return out;
}

SearchResult search(const std::string& fen, int depth) {
    Engine engine;
    SearchLimits limits;
    limits.depth = depth;
    return engine.search(from_fen(fen), limits);
}

}  // namespace

TEST_CASE(gives_check_matches_make_move) {
    int checks = 0;
    for (Position pos : sample_positions(400)) {
        MoveList moves;
        generate<GenType::All>(pos, moves);
        for (const Move m : moves) {
            const bool predicted = pos.gives_check(m);
            pos.make_move(m);
            CHECK_EQ(predicted, pos.in_check());
            checks += pos.in_check();
            pos.unmake_move(m);
        }
    }
    CHECK(checks > 100);  // the sample really contains checking moves
}

TEST_CASE(evaluation_is_colour_symmetric) {
    for (const Position& pos : sample_positions(500)) {
        const Position mirrored = from_fen(mirror_fen(pos.fen()));
        CHECK_EQ(eval::evaluate(pos), eval::evaluate(mirrored));
    }
}

TEST_CASE(trace_reproduces_evaluation_linearly) {
    // tools/tune assumes eval = phase-weighted sum of coef * weight; verify
    // that model against the real evaluation for many positions.
    for (const Position& pos : sample_positions(300)) {
        eval::Trace t;
        const Value white = eval::trace(pos, t);
        long mg = 0, eg = 0;
        for (int i = 0; i < eval::idx::Count; ++i) {
            const int n = t.coef[i][WHITE] - t.coef[i][BLACK];
            mg += long(n) * eval::Params[i].mg;
            eg += long(n) * eval::Params[i].eg;
        }
        const long linear =
            (mg * t.phase + eg * (eval::PHASE_MAX - t.phase) * t.eg_scale / eval::SCALE_NORMAL) / eval::PHASE_MAX;
        CHECK_EQ(long(white), linear);
        CHECK_EQ(pos.side_to_move() == WHITE ? white : -white, eval::evaluate(pos));
    }
}

TEST_CASE(tt_round_trips_entries_and_mate_scores) {
    TranspositionTable tt(1);
    const Move m(E2, E4, DOUBLE_PUSH);
    tt.store(0x1234567890ABCDEFULL, m, -31000, -250, 17, BOUND_LOWER);
    TTData d;
    REQUIRE(tt.probe(0x1234567890ABCDEFULL, d));
    CHECK(d.move == m);
    CHECK_EQ(d.score, -31000);
    CHECK_EQ(d.eval, -250);
    CHECK_EQ(d.depth, 17);
    CHECK_EQ(int(d.bound), int(BOUND_LOWER));
    CHECK(!tt.probe(0x1234567890ABCDEEULL, d));

    // A mate found 5 plies below a node at ply 7 is "mate in 5" relative to
    // that node, whatever ply it is later probed from.
    const Value at_root = mate_in(12);
    CHECK_EQ(value_from_tt(value_to_tt(at_root, 7), 7), at_root);
    CHECK_EQ(value_from_tt(value_to_tt(at_root, 7), 3), mate_in(8));
    CHECK_EQ(value_from_tt(value_to_tt(-at_root, 7), 3), -mate_in(8));
    CHECK_EQ(value_from_tt(VALUE_NONE, 4), VALUE_NONE);

    tt.clear();
    CHECK(!tt.probe(0x1234567890ABCDEFULL, d));
    CHECK_EQ(tt.hashfull(), 0);
}

TEST_CASE(search_finds_mates_and_reports_them) {
    const SearchResult m1 = search("1k6/8/1K6/8/8/8/8/7R w - - 0 1", 4);
    CHECK_EQ(m1.best_move.uci(), std::string("h1h8"));
    CHECK_EQ(m1.score, mate_in(1));
    CHECK_EQ(Uci::format_score(m1.score), std::string("mate 1"));

    const SearchResult mated = search("k7/8/1K6/8/8/8/8/7R b - - 0 1", 6);
    CHECK_EQ(mated.score, mated_in(2));
    CHECK_EQ(Uci::format_score(mated.score), std::string("mate -1"));

    const SearchResult m2 = search("r2qkb1r/pp2nppp/3p4/2pNN1B1/2BnP3/3P4/PPP2PPP/R2bK2R w KQkq - 1 1", 6);
    CHECK_EQ(m2.best_move.uci(), std::string("d5f6"));
    CHECK_EQ(m2.score, mate_in(3));
    CHECK_EQ(Uci::format_score(m2.score), std::string("mate 2"));
}

TEST_CASE(search_handles_terminal_and_forced_positions) {
    const SearchResult stalemate = search("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1", 5);
    CHECK(!stalemate.best_move.is_ok());
    CHECK_EQ(stalemate.best_move.uci(), std::string("0000"));

    const SearchResult forced = search("k7/8/1K6/8/8/8/8/7R b - - 0 1", 1);
    CHECK_EQ(forced.best_move.uci(), std::string("a8b8"));

    // A queen up but able to stalemate: the engine must not pick a stalemating move.
    const SearchResult avoid = search("k7/2Q5/1K6/8/8/8/8/8 w - - 0 1", 6);
    Position after = from_fen("k7/2Q5/1K6/8/8/8/8/8 w - - 0 1");
    after.make_move(avoid.best_move);
    MoveList replies;
    generate<GenType::All>(after, replies);
    CHECK(!replies.empty() || after.in_check());
    CHECK(avoid.score >= VALUE_MATE_IN_MAX_PLY);
}

TEST_CASE(search_respects_limits_and_is_deterministic) {
    const Position pos = from_fen(bench_positions()[1]);
    SearchLimits limits;
    limits.nodes = 20000;
    Engine a, b;
    const SearchResult ra = a.search(pos, limits);
    const SearchResult rb = b.search(pos, limits);
    CHECK(ra.nodes <= limits.nodes + 1);
    CHECK(ra.nodes >= limits.nodes - 1);
    CHECK(ra.best_move == rb.best_move);
    CHECK_EQ(ra.nodes, rb.nodes);

    SearchLimits restricted;
    restricted.depth = 6;
    restricted.searchmoves = {Position().parse_uci_move("a2a3")};
    CHECK_EQ(a.search(Position(), restricted).best_move.uci(), std::string("a2a3"));
}

TEST_CASE(search_stops_promptly_and_multithreaded_search_works) {
    Engine engine;
    engine.set_threads(4);
    SearchLimits limits;
    limits.infinite = true;
    SearchResult result;
    bool done = false;
    engine.start(Position(), limits, {}, [&](const SearchResult& r) {
        result = r;
        done = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const auto t0 = std::chrono::steady_clock::now();
    engine.stop();
    engine.wait();
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(done);
    CHECK(ms < 100);
    CHECK(Position().parse_uci_move(result.best_move.uci()).is_ok());
    CHECK(result.depth >= 6);
}

TEST_CASE(time_budget_allocation) {
    SearchLimits l;
    l.use_clock = true;
    l.time[WHITE] = 60000;
    l.inc[WHITE] = 1000;
    const TimeBudget b = TimeBudget::allocate(l, WHITE, 30);
    CHECK(b.soft > 0 && b.soft < b.hard);
    CHECK(b.hard < 60000 / 4);

    // Low on time with a large increment: never plan beyond the clock.
    l.time[WHITE] = 200;
    l.inc[WHITE] = 2000;
    const TimeBudget low = TimeBudget::allocate(l, WHITE, 30);
    CHECK(low.hard <= 170);
    CHECK(low.soft >= 1);

    SearchLimits fixed;
    fixed.movetime = 500;
    CHECK_EQ(TimeBudget::allocate(fixed, BLACK, 30).hard, std::int64_t(470));
    CHECK_EQ(TimeBudget::allocate(SearchLimits{}, WHITE, 30).hard, std::int64_t(0));
}

TEST_CASE(uci_session_end_to_end) {
    std::istringstream in(
        "uci\nisready\nsetoption name Hash value 8\nsetoption name Threads value 2\n"
        "position fen 4k3/8/8/8/8/8/8/4K3 w - - 0 1 moves e1e2 e8d7 bogus e2e3\n"
        "position fen not a fen\n"
        "position startpos moves e2e4 e7e5\ngo depth 5\n");
    std::ostringstream out;
    Uci uci(in, out);
    uci.loop();
    const std::string s = out.str();
    CHECK(s.find("uciok") != std::string::npos);
    CHECK(s.find("readyok") != std::string::npos);
    CHECK(s.find("info string illegal move bogus") != std::string::npos);
    CHECK(s.find("info string invalid FEN") != std::string::npos);
    CHECK(s.find("info depth 5 ") != std::string::npos);
    CHECK(s.find("bestmove ") != std::string::npos);
}
