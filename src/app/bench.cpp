// `bench`: searches a fixed set of positions to a fixed depth. With one thread
// the total node count is deterministic, which makes it a cheap functional
// fingerprint of the search (any change in it means the search changed), and
// the elapsed time gives a nodes-per-second figure.
//
// `perft` / `perft-suite`: move generator verification and speed measurement.
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>

#include "app/commands.h"
#include "core/epd.h"
#include "core/movegen.h"
#include "engine/search.h"

namespace chess {

namespace {

constexpr int DEFAULT_BENCH_DEPTH = 13;

double seconds_since(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
}

bool parse_int_arg(const Args& args, size_t i, int& out) {
    if (i >= args.size()) return true;
    try {
        size_t used = 0;
        out = std::stoi(args[i], &used);
        return used == args[i].size();
    } catch (...) {
        return false;
    }
}

}  // namespace

const std::vector<std::string>& bench_positions() {
    // Middlegames and endgames of varied character (several from the
    // Stockfish bench set), including positions with mate and stalemate themes.
    static const std::vector<std::string> fens = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 10",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 11",
        "4rrk1/pp1n3p/3q2pQ/2p1pb2/2PP4/2P3N1/P2B2PP/4RRK1 b - - 7 19",
        "rq3rk1/ppp2ppp/1bnpb3/3N2B1/3NP3/7P/PPPQ1PP1/2KR3R w - - 7 14",
        "r1bq1r1k/1pp1n1pp/1p1p4/4p2Q/4Pp2/1BNP4/PPP2PPP/3R1RK1 w - - 2 14",
        "r3r1k1/2p2ppp/p1p1bn2/8/1q2P3/2NPQN2/PPP3PP/R4RK1 b - - 2 15",
        "r1bbk1nr/pp3p1p/2n5/1N4p1/2Np1B2/8/PPP2PPP/2KR1B1R w kq - 0 13",
        "r1bq1rk1/ppp1nppp/4n3/3p3Q/3P4/1BP1B3/PP1N2PP/R4RK1 w - - 1 16",
        "4r1k1/r1q2ppp/ppp2n2/4P3/5Rb1/1N1BQ3/PPP3PP/R5K1 w - - 1 17",
        "2rqkb1r/ppp2p2/2npb1p1/1N1Nn2p/2P1PP2/8/PP2B1PP/R1BQK2R b KQ - 0 11",
        "r1bq1r1k/b1p1npp1/p2p3p/1p6/3PP3/1B2NN2/PP3PPP/R2Q1RK1 w - - 1 16",
        "3r1rk1/p5pp/bpp1pp2/8/q1PP1P2/b3P3/P2NQRPP/1R2B1K1 b - - 6 22",
        "r1q2rk1/2p1bppp/2Pp4/p6b/Q1PNp3/4B3/PP1R1PPP/2K4R w - - 2 18",
        "4k2r/1pb2ppp/1p2p3/1R1p4/3P4/2r1PN2/P4PPP/1R4K1 b - - 3 22",
        "3q2k1/pb3p1p/4pbp1/2r5/PpN2N2/1P2P2P/5PP1/Q2R2K1 b - - 4 26",
        "6k1/6p1/6Pp/ppp5/3pn2P/1P3K2/1PP2P2/3N4 b - - 0 1",
        "3b4/5kp1/1p1p1p1p/pP1PpP1P/P1P1P3/3KN3/8/8 w - - 0 1",
        "2K5/p7/7P/5pR1/8/5k2/r7/8 w - - 0 1",
        "8/6pk/1p6/8/PP3p1p/5P2/4KP1q/3Q4 w - - 0 1",
        "7k/3p2pp/4q3/8/4Q3/5Kp1/P6b/8 w - - 0 1",
        "8/2p5/8/2kPKp1p/2p4P/2P5/3P4/8 w - - 0 1",
        "8/1p3pp1/7p/5P1P/2k3P1/8/2K2P2/8 w - - 0 1",
        "8/pp2r1k1/2p1p3/3pP2p/1P1P1P1P/P5KR/8/8 w - - 0 1",
        "8/3p4/p1bk3p/Pp6/1Kp1PpPp/2P2P1P/2P5/5B2 b - - 0 1",
        "5k2/7R/4P2p/5K2/p1r2P1p/8/8/8 b - - 0 1",
        "6k1/6p1/P6p/r1N5/5p2/7P/1b3PP1/4R1K1 w - - 0 1",
        "1r3k2/4q3/2Pp3b/3Bp3/2Q2p2/1p1P2P1/1P2KP2/3N4 w - - 0 1",
        "6k1/4pp1p/3p2p1/P1pPb3/R7/1r2P1PP/3B1P2/6K1 w - - 0 1",
        "8/3p3B/5p2/5P2/p7/PP5b/k7/6K1 w - - 0 1",
    };
    return fens;
}

// bench [depth] [threads] [hash MB]
int bench_command(const Args& args) {
    int depth = DEFAULT_BENCH_DEPTH, threads = 1, hash = 16;
    if (!parse_int_arg(args, 1, depth) || !parse_int_arg(args, 2, threads) || !parse_int_arg(args, 3, hash) ||
        depth < 1 || threads < 1 || hash < 1) {
        std::cerr << "usage: chess-engine bench [depth=" << DEFAULT_BENCH_DEPTH << "] [threads=1] [hash=16]\n";
        return 2;
    }

    Engine engine;
    engine.set_threads(threads);
    engine.set_hash_mb(std::size_t(hash));
    SearchLimits limits;
    limits.depth = depth;

    std::uint64_t nodes = 0;
    const auto start = std::chrono::steady_clock::now();
    const auto& fens = bench_positions();
    for (size_t i = 0; i < fens.size(); ++i) {
        Position pos;
        std::string error;
        if (!pos.set_fen(fens[i], &error)) {
            std::cerr << "bench position " << i + 1 << " is invalid: " << error << '\n';
            return 1;
        }
        engine.new_game();
        const SearchResult r = engine.search(pos, limits);
        nodes += r.nodes;
        std::cerr << "Position " << std::setw(2) << i + 1 << '/' << fens.size() << "  bestmove "
                  << r.best_move.uci() << "  nodes " << r.nodes << '\n';
    }
    const double s = seconds_since(start);
    std::cout << "\n===========================\n"
              << "Total time (ms) : " << std::int64_t(s * 1000) << '\n'
              << "Nodes searched  : " << nodes << '\n'
              << "Nodes/second    : " << std::uint64_t(double(nodes) / s) << '\n';
    return 0;
}

// perft <depth> [fen]   -- divided perft of one position
int perft_command(const Args& args) {
    int depth = 0;
    if (args.size() < 2 || !parse_int_arg(args, 1, depth) || depth < 1) {
        std::cerr << "usage: chess-engine perft <depth> [fen]\n";
        return 2;
    }
    Position pos;
    if (args.size() > 2) {
        std::string fen, error;
        for (size_t i = 2; i < args.size(); ++i) fen += args[i] + ' ';
        if (!pos.set_fen(fen, &error)) {
            std::cerr << "invalid FEN: " << error << '\n';
            return 2;
        }
    }
    const auto start = std::chrono::steady_clock::now();
    MoveList moves;
    generate<GenType::All>(pos, moves);
    std::uint64_t total = 0;
    for (const Move m : moves) {
        pos.make_move(m);
        const std::uint64_t n = perft(pos, depth - 1);
        pos.unmake_move(m);
        std::cout << m.uci() << ": " << n << '\n';
        total += n;
    }
    const double s = seconds_since(start);
    std::cout << "\nNodes: " << total << "\nTime (ms): " << std::int64_t(s * 1000)
              << "\nNodes/second: " << std::uint64_t(double(total) / s) << '\n';
    return 0;
}

// perft-suite <file> [max leaf nodes per test]
int perft_suite_command(const Args& args) {
    if (args.size() < 2) {
        std::cerr << "usage: chess-engine perft-suite <file.epd> [max_nodes]\n";
        return 2;
    }
    std::uint64_t max_nodes = ~0ULL;
    if (args.size() > 2) max_nodes = std::stoull(args[2]);

    std::vector<PerftCase> cases;
    std::string error;
    if (!load_perft_suite(args[1], cases, &error)) {
        std::cerr << error << '\n';
        return 2;
    }

    int passed = 0, failed = 0;
    std::uint64_t total_nodes = 0;
    const auto start = std::chrono::steady_clock::now();
    for (const PerftCase& c : cases) {
        Position pos;
        if (!pos.set_fen(c.fen, &error)) {
            std::cout << "INVALID " << c.fen << ": " << error << '\n';
            ++failed;
            continue;
        }
        for (const auto& [depth, expected] : c.depths) {
            if (expected > max_nodes) continue;
            const auto t0 = std::chrono::steady_clock::now();
            const std::uint64_t got = perft(pos, depth);
            const double s = seconds_since(t0);
            total_nodes += got;
            const bool ok = got == expected;
            (ok ? passed : failed)++;
            std::cout << (ok ? "PASS " : "FAIL ") << "depth " << depth << " nodes " << got;
            if (!ok) std::cout << " (expected " << expected << ")";
            std::cout << "  " << std::uint64_t(double(got) / std::max(s, 1e-9) / 1e6) << " Mnps  " << c.fen
                      << '\n';
        }
    }
    const double s = seconds_since(start);
    std::cout << '\n'
              << passed << " passed, " << failed << " failed; " << total_nodes << " leaf nodes in " << std::fixed
              << std::setprecision(2) << s << " s (" << std::uint64_t(double(total_nodes) / s / 1e6)
              << " Mnps)\n";
    return failed == 0 ? 0 : 1;
}

}  // namespace chess
