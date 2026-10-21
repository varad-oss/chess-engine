// Search: iterative deepening over a principal-variation alpha-beta search,
// run on background threads. With N threads the engine uses "Lazy SMP": every
// thread searches the same root independently and they cooperate only through
// the shared transposition table.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "core/position.h"
#include "engine/tt.h"

namespace chess {

struct SearchLimits {
    int depth = 0;              // 0 = no depth limit
    std::uint64_t nodes = 0;    // 0 = no node limit
    std::int64_t movetime = 0;  // ms; 0 = none
    bool use_clock = false;     // time/inc below are meaningful
    std::int64_t time[COLOR_NB] = {0, 0};
    std::int64_t inc[COLOR_NB] = {0, 0};
    int movestogo = 0;
    bool infinite = false;  // never stop on our own; wait for stop()
    std::vector<Move> searchmoves;
};

struct SearchInfo {
    int depth = 0;
    int seldepth = 0;
    Value score = 0;
    Bound bound = BOUND_EXACT;
    std::uint64_t nodes = 0;
    std::int64_t time_ms = 0;
    int hashfull = 0;
    std::vector<Move> pv;
};

struct SearchResult {
    Move best_move;
    Move ponder_move;
    Value score = 0;
    int depth = 0;
    std::uint64_t nodes = 0;
    std::int64_t time_ms = 0;
};

// Time allocation: `soft` is the point after which no new iteration starts,
// `hard` the point at which a running iteration is aborted. 0 = unlimited.
struct TimeBudget {
    std::int64_t soft = 0;
    std::int64_t hard = 0;
    static TimeBudget allocate(const SearchLimits& limits, Color us, int move_overhead_ms);
};

class Worker;

class Engine {
public:
    using InfoCallback = std::function<void(const SearchInfo&)>;
    using DoneCallback = std::function<void(const SearchResult&)>;

    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Configuration; must not be called while searching.
    void set_hash_mb(std::size_t mb);
    void set_threads(int n);
    void set_move_overhead(int ms) { move_overhead_ = ms; }
    int threads() const { return int(workers_.size()); }
    void new_game();  // clears the hash table and all history tables

    // Starts a search in the background and returns immediately. on_info is
    // called from the search thread after each completed iteration; on_done is
    // called exactly once, with the final result, when the search ends.
    void start(const Position& root, const SearchLimits& limits, InfoCallback on_info,
               DoneCallback on_done);
    void stop() { stop_.store(true, std::memory_order_relaxed); }
    void wait();  // blocks until the current search (if any) has finished

    // Convenience: start + wait.
    SearchResult search(const Position& root, const SearchLimits& limits, InfoCallback on_info = {});

private:
    friend class Worker;

    void run(Position root, SearchLimits limits, InfoCallback on_info, DoneCallback on_done);
    std::uint64_t total_nodes() const;
    std::int64_t elapsed_ms() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - start_time_)
            .count();
    }

    TranspositionTable tt_;
    std::vector<std::unique_ptr<Worker>> workers_;
    InfoCallback main_info_;  // only touched by the main search thread
    std::thread main_thread_;
    std::atomic<bool> stop_{false};
    int move_overhead_ = 30;

    // Per-search state, written before the worker threads start.
    SearchLimits limits_;
    TimeBudget budget_;
    std::chrono::steady_clock::time_point start_time_;
};

}  // namespace chess
