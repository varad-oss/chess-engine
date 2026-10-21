// Self-play data generation for evaluation tuning, and balanced opening
// generation for matches.
//
// datagen: plays games between two copies of the engine at a fixed node count
// per move, starting from a few random plies, and writes the quiet positions
// of each game labelled with the final result. Every game is a deterministic
// function of (seed, game index): the hash table is cleared before each game
// and each game owns a single-threaded search.
//
// openings: random-ply positions kept only if a shallow search scores them as
// roughly balanced, deduplicated by hash key.
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "app/commands.h"
#include "core/movegen.h"
#include "core/rng.h"
#include "engine/search.h"

namespace chess {

namespace {

struct Flags {
    std::string get(const std::string& name, const std::string& fallback) const {
        for (size_t i = 1; i + 1 < args.size(); ++i)
            if (args[i] == "--" + name) return args[i + 1];
        return fallback;
    }
    long long num(const std::string& name, long long fallback) const {
        return std::stoll(get(name, std::to_string(fallback)));
    }
    Args args;
};

// Plays `plies` uniformly random legal moves. Returns false if the game ended.
bool random_opening(Position& pos, Rng& rng, int plies) {
    for (int i = 0; i < plies; ++i) {
        MoveList moves;
        generate<GenType::All>(pos, moves);
        if (moves.empty()) return false;
        pos.make_move(moves[rng.below(moves.size())]);
    }
    MoveList moves;
    generate<GenType::All>(pos, moves);
    return !moves.empty();
}

struct Sample {
    std::string fen;
    Value score_white;
};

}  // namespace

// datagen --games N --nodes N --threads N --out FILE [--seed S] [--random-plies P]
int datagen_command(const Args& args) {
    const Flags f{args};
    long long games, nodes, threads, seed, random_plies;
    std::string out_path;
    try {
        games = f.num("games", 1000);
        nodes = f.num("nodes", 5000);
        threads = f.num("threads", 1);
        seed = f.num("seed", 1);
        random_plies = f.num("random-plies", 8);
        out_path = f.get("out", "");
    } catch (...) {
        std::cerr << "datagen: invalid numeric argument\n";
        return 2;
    }
    if (out_path.empty() || games < 1 || nodes < 1 || threads < 1) {
        std::cerr << "usage: chess-engine datagen --out FILE [--games N] [--nodes N] [--threads N] [--seed S] "
                     "[--random-plies P]\n";
        return 2;
    }
    std::ofstream out(out_path, std::ios::app);
    if (!out) {
        std::cerr << "datagen: cannot open " << out_path << '\n';
        return 2;
    }

    std::mutex out_mutex;
    std::atomic<long long> next{0}, finished{0}, positions{0};
    const auto start = std::chrono::steady_clock::now();

    auto worker = [&] {
        Engine engine;
        engine.set_hash_mb(8);
        SearchLimits limits;
        limits.nodes = std::uint64_t(nodes);

        for (long long g; (g = next++) < games;) {
            Rng rng(std::uint64_t(seed) * 0x9E3779B97F4A7C15ULL + std::uint64_t(g) + 1);
            Position pos;
            if (!random_opening(pos, rng, int(random_plies))) continue;
            engine.new_game();

            std::vector<Sample> samples;
            double result = 0.5;  // from White's point of view
            int win_streak = 0, draw_streak = 0;
            for (int ply = 0;; ++ply) {
                MoveList legal;
                generate<GenType::All>(pos, legal);
                if (legal.empty()) {
                    result = pos.in_check() ? (pos.side_to_move() == WHITE ? 0.0 : 1.0) : 0.5;
                    break;
                }
                if (pos.is_draw(0) || ply >= 400) break;

                const SearchResult r = engine.search(pos, limits);
                const Value white = pos.side_to_move() == WHITE ? r.score : -r.score;
                if (ply == 0 && std::abs(white) > 300) break;  // unbalanced opening: discard (no samples)

                if (!pos.in_check() && !r.best_move.is_noisy() && !is_mate_score(r.score))
                    samples.push_back({pos.fen(), white});

                // Adjudicate decided games to save time.
                win_streak = std::abs(white) >= 1500 ? win_streak + 1 : 0;
                if (win_streak >= 6) {
                    result = white > 0 ? 1.0 : 0.0;
                    break;
                }
                draw_streak = ply >= 80 && std::abs(white) <= 10 ? draw_streak + 1 : 0;
                if (draw_streak >= 12) break;

                pos.make_move(r.best_move);
            }

            std::ostringstream lines;
            for (const Sample& s : samples) lines << s.fen << " | " << s.score_white << " | " << result << '\n';
            positions += static_cast<long long>(samples.size());
            {
                std::lock_guard<std::mutex> lock(out_mutex);
                out << lines.str();
                const long long done = ++finished;
                if (done % 100 == 0 || done == games) {
                    const double s =
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                    std::cerr << "games " << done << '/' << games << "  positions " << positions << "  ("
                              << int(double(done) / s) << " games/s)\n";
                }
            }
        }
    };

    std::vector<std::thread> pool;
    for (long long i = 0; i < threads; ++i) pool.emplace_back(worker);
    for (auto& t : pool) t.join();
    // Games whose random opening already ended the game are skipped, so
    // `finished` can be below `games`.
    std::cerr << "done: " << finished << " games written (" << games - finished
              << " skipped in the random opening), " << positions << " positions\n";
    return 0;
}

// openings --count N [--plies P] [--depth D] [--max-score CP] [--seed S]   (FENs on stdout)
int openings_command(const Args& args) {
    const Flags f{args};
    long long count, plies, depth, max_score, seed;
    try {
        count = f.num("count", 1000);
        plies = f.num("plies", 8);
        depth = f.num("depth", 8);
        max_score = f.num("max-score", 80);
        seed = f.num("seed", 1);
    } catch (...) {
        std::cerr << "openings: invalid numeric argument\n";
        return 2;
    }
    Engine engine;
    SearchLimits limits;
    limits.depth = int(depth);
    std::set<Key> seen;
    Rng rng{std::uint64_t(seed)};
    long long produced = 0;
    for (long long attempt = 0; produced < count && attempt < count * 100; ++attempt) {
        Position pos;
        if (!random_opening(pos, rng, int(plies)) || seen.count(pos.key())) continue;
        engine.new_game();
        const SearchResult r = engine.search(pos, limits);
        if (std::abs(r.score) > max_score) continue;
        seen.insert(pos.key());
        std::cout << pos.fen() << '\n';
        ++produced;
    }
    return produced == count ? 0 : 1;
}

}  // namespace chess
