#include "search.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "core/movegen.h"
#include "engine/eval.h"

namespace chess {

namespace {

constexpr int HISTORY_MAX = 16384;
constexpr int SEE_VALUE[PIECE_TYPE_NB + 1] = {100, 320, 330, 500, 900, 20000, 0};

// Move-ordering score bands.
constexpr int SCORE_TT_MOVE = 1 << 30;
constexpr int SCORE_GOOD_NOISY = 1 << 28;
constexpr int SCORE_KILLER = 1 << 27;
constexpr int SCORE_BAD_NOISY = -(1 << 28);

struct ReductionTable {
    int r[64][64] = {};
    ReductionTable() {
        for (int d = 1; d < 64; ++d)
            for (int m = 1; m < 64; ++m)
                r[d][m] = int(0.75 + std::log(d) * std::log(m) / 2.25);
    }
};
const ReductionTable Reductions;

int history_bonus(int depth) { return std::min(1536, 24 * depth * depth); }

void apply_history(int& entry, int bonus) {
    entry += bonus - entry * std::abs(bonus) / HISTORY_MAX;
}

}  // namespace

// One search thread. Workers own their position copy and move-ordering
// statistics; only the transposition table and the stop flag are shared.
class Worker {
public:
    Worker(Engine& engine, int id) : engine_(engine), id_(id) { clear_history(); }

    void clear_history() {
        std::memset(history_, 0, sizeof(history_));
        std::memset(killers_, 0, sizeof(killers_));
    }

    void prepare(const Position& root) {
        pos_ = root;
        nodes_.store(0, std::memory_order_relaxed);
        best_move_ = Move::none();
        best_score_ = -VALUE_INFINITE;
        completed_depth_ = 0;
        best_pv_.clear();
    }

    void iterative_deepening();

    std::uint64_t nodes() const { return nodes_.load(std::memory_order_relaxed); }
    Move best_move() const { return best_move_; }
    Value best_score() const { return best_score_; }
    int completed_depth() const { return completed_depth_; }
    const std::vector<Move>& best_pv() const { return best_pv_; }

private:
    struct StackEntry {
        Move move;
        Value static_eval = VALUE_NONE;
    };

    Value search(Value alpha, Value beta, int depth, int ply);
    Value qsearch(Value alpha, Value beta, int ply);
    void score_moves(const MoveList& moves, int* scores, Move tt_move, int ply) const;
    void update_quiet_stats(Move best, const Move* quiets, int quiet_count, int depth, int ply);
    void update_pv(int ply, Move m);
    bool should_stop();
    // Only this thread writes nodes_, so a plain relaxed load+store suffices
    // (no atomic read-modify-write); other threads only read it.
    void count_node() { nodes_.store(nodes_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed); }
    bool is_main() const { return id_ == 0; }
    StackEntry& ss(int ply) { return stack_[ply + 2]; }  // ss(-1), ss(-2) are valid sentinels

    Engine& engine_;
    const int id_;
    Position pos_;
    std::atomic<std::uint64_t> nodes_{0};
    int seldepth_ = 0;

    int history_[COLOR_NB][SQUARE_NB][SQUARE_NB];
    Move killers_[MAX_PLY + 2][2];
    StackEntry stack_[MAX_PLY + 4];
    Move pv_[MAX_PLY + 1][MAX_PLY + 1];
    int pv_len_[MAX_PLY + 1] = {};
    std::vector<Move> root_moves_;

    // Root results: the best move of the last completed iteration, possibly
    // improved by a root move that completed in an interrupted iteration.
    Move best_move_;
    Value best_score_ = -VALUE_INFINITE;
    int completed_depth_ = 0;
    std::vector<Move> best_pv_;
    bool root_improved_ = false;
};

// ---------------------------------------------------------------------------
// Time allocation

TimeBudget TimeBudget::allocate(const SearchLimits& limits, Color us, int move_overhead_ms) {
    TimeBudget b;
    if (limits.movetime > 0) {
        b.soft = b.hard = std::max<std::int64_t>(1, limits.movetime - move_overhead_ms);
        return b;
    }
    if (!limits.use_clock) return b;

    const std::int64_t remaining = std::max<std::int64_t>(limits.time[us], 1);
    const std::int64_t usable = std::max<std::int64_t>(1, remaining - move_overhead_ms);
    const int moves_to_go = limits.movestogo > 0 ? std::min(limits.movestogo, 40) : 30;
    const std::int64_t optimum = remaining / moves_to_go + limits.inc[us] * 3 / 4;
    // An iteration usually costs more than all previous ones together, so stop
    // starting new ones well before the optimum is used up.
    b.soft = std::clamp<std::int64_t>(optimum * 6 / 10, 1, usable);
    b.hard = std::clamp<std::int64_t>(optimum * 3, 1, usable);
    return b;
}

// ---------------------------------------------------------------------------
// Engine

Engine::Engine() { set_threads(1); }

Engine::~Engine() {
    stop();
    wait();
}

void Engine::set_hash_mb(std::size_t mb) {
    wait();
    tt_.resize(mb);
}

void Engine::set_threads(int n) {
    wait();
    n = std::max(1, n);
    workers_.clear();
    for (int i = 0; i < n; ++i) workers_.push_back(std::make_unique<Worker>(*this, i));
}

void Engine::new_game() {
    wait();
    tt_.clear();
    for (auto& w : workers_) w->clear_history();
}

void Engine::wait() {
    if (main_thread_.joinable()) main_thread_.join();
}

void Engine::start(const Position& root, const SearchLimits& limits, InfoCallback on_info,
                   DoneCallback on_done) {
    stop();
    wait();
    stop_.store(false, std::memory_order_relaxed);
    limits_ = limits;
    start_time_ = std::chrono::steady_clock::now();
    budget_ = TimeBudget::allocate(limits, root.side_to_move(), move_overhead_);
    main_thread_ = std::thread(&Engine::run, this, root, limits, std::move(on_info), std::move(on_done));
}

SearchResult Engine::search(const Position& root, const SearchLimits& limits, InfoCallback on_info) {
    SearchResult result;
    start(root, limits, std::move(on_info), [&result](const SearchResult& r) { result = r; });
    wait();
    return result;
}

std::uint64_t Engine::total_nodes() const {
    std::uint64_t n = 0;
    for (const auto& w : workers_) n += w->nodes();
    return n;
}

void Engine::run(Position root, SearchLimits limits, InfoCallback on_info, DoneCallback on_done) {
    tt_.new_search();
    for (auto& w : workers_) w->prepare(root);

    std::vector<std::thread> helpers;
    for (size_t i = 1; i < workers_.size(); ++i)
        helpers.emplace_back([w = workers_[i].get()] { w->iterative_deepening(); });

    Worker& main = *workers_[0];
    main_info_ = std::move(on_info);
    main.iterative_deepening();

    // UCI: in infinite mode the best move may only be sent after "stop".
    while (limits.infinite && !stop_.load(std::memory_order_relaxed))
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    stop_.store(true, std::memory_order_relaxed);
    for (std::thread& t : helpers) t.join();

    SearchResult result;
    result.best_move = main.best_move();
    result.score = main.best_score();
    result.depth = main.completed_depth();
    if (main.best_pv().size() > 1) result.ponder_move = main.best_pv()[1];
    result.nodes = total_nodes();
    result.time_ms = elapsed_ms();
    main_info_ = nullptr;
    if (on_done) on_done(result);
}

// ---------------------------------------------------------------------------
// Worker

bool Worker::should_stop() {
    if (engine_.stop_.load(std::memory_order_relaxed)) return true;
    if (!is_main()) return false;

    const SearchLimits& limits = engine_.limits_;
    const std::uint64_t n = nodes();
    if (limits.nodes) {
        const std::uint64_t total = engine_.workers_.size() == 1 ? n : engine_.total_nodes();
        if (total >= limits.nodes) {
            engine_.stop();
            return true;
        }
    }
    if ((n & 1023) == 0 && engine_.budget_.hard && engine_.elapsed_ms() >= engine_.budget_.hard) {
        engine_.stop();
        return true;
    }
    return false;
}

void Worker::update_pv(int ply, Move m) {
    pv_[ply][ply] = m;
    for (int i = ply + 1; i < pv_len_[ply + 1]; ++i) pv_[ply][i] = pv_[ply + 1][i];
    pv_len_[ply] = std::max(pv_len_[ply + 1], ply + 1);
}

void Worker::iterative_deepening() {
    const SearchLimits& limits = engine_.limits_;
    MoveList legal;
    generate<GenType::All>(pos_, legal);
    root_moves_.clear();
    for (const Move m : legal)
        if (limits.searchmoves.empty() ||
            std::find(limits.searchmoves.begin(), limits.searchmoves.end(), m) != limits.searchmoves.end())
            root_moves_.push_back(m);
    if (root_moves_.empty()) return;  // checkmate, stalemate or an empty searchmoves filter
    best_move_ = root_moves_.front();  // always have a legal move to play

    const int max_depth = limits.depth > 0 ? std::min(limits.depth, MAX_PLY - 1) : MAX_PLY - 1;
    Value score = 0;

    for (int depth = 1; depth <= max_depth; ++depth) {
        // Lazy SMP diversification: odd helpers skip every other depth.
        if (!is_main() && (id_ & 1) && depth > 1 && (depth & 1)) continue;

        seldepth_ = 0;
        root_improved_ = false;
        Value delta = 25;
        Value alpha = -VALUE_INFINITE, beta = VALUE_INFINITE;
        if (depth >= 5) {
            alpha = std::max(score - delta, -VALUE_INFINITE);
            beta = std::min(score + delta, VALUE_INFINITE);
        }

        // Aspiration loop: widen the window until the score falls inside it.
        while (true) {
            const Value v = search(alpha, beta, depth, 0);
            if (engine_.stop_.load(std::memory_order_relaxed)) break;

            if (v <= alpha) {
                beta = (alpha + beta) / 2;
                alpha = std::max(v - delta, -VALUE_INFINITE);
            } else if (v >= beta) {
                beta = std::min(v + delta, VALUE_INFINITE);
            } else {
                score = v;
                break;
            }
            delta += delta / 2;
        }

        const bool interrupted = engine_.stop_.load(std::memory_order_relaxed);
        if (interrupted) {
            // A root move that finished searching and raised alpha in the
            // aborted iteration is still a better choice than last iteration's.
            if (root_improved_ && pv_len_[0] > 0) {
                best_move_ = pv_[0][0];
                best_pv_.assign(pv_[0], pv_[0] + pv_len_[0]);
            }
            break;
        }

        completed_depth_ = depth;
        best_score_ = score;
        best_move_ = pv_[0][0];
        best_pv_.assign(pv_[0], pv_[0] + pv_len_[0]);

        if (is_main()) {
            if (engine_.main_info_) {
                SearchInfo info;
                info.depth = depth;
                info.seldepth = seldepth_;
                info.score = score;
                info.nodes = engine_.total_nodes();
                info.time_ms = engine_.elapsed_ms();
                info.hashfull = engine_.tt_.hashfull();
                info.pv = best_pv_;
                engine_.main_info_(info);
            }
            if (engine_.budget_.soft && engine_.elapsed_ms() >= engine_.budget_.soft) break;
        }
    }

    // The main thread finishing ends the search for everybody.
    if (is_main() && !limits.infinite) engine_.stop();
}

void Worker::score_moves(const MoveList& moves, int* scores, Move tt_move, int ply) const {
    const Color us = pos_.side_to_move();
    for (size_t i = 0; i < moves.size(); ++i) {
        const Move m = moves[i];
        if (m == tt_move) {
            scores[i] = SCORE_TT_MOVE;
        } else if (m.is_noisy()) {
            const PieceType victim = m.is_ep() ? PAWN : type_of(pos_.piece_on(m.to()));
            const PieceType attacker = type_of(pos_.piece_on(m.from()));
            int mvv_lva = SEE_VALUE[victim] * 8 - SEE_VALUE[attacker] / 100;
            if (m.is_promotion()) mvv_lva += SEE_VALUE[m.promotion_type()];
            scores[i] = (pos_.see_ge(m, 0) ? SCORE_GOOD_NOISY : SCORE_BAD_NOISY) + mvv_lva;
        } else if (m == killers_[ply][0]) {
            scores[i] = SCORE_KILLER;
        } else if (m == killers_[ply][1]) {
            scores[i] = SCORE_KILLER - 1;
        } else {
            scores[i] = history_[us][m.from()][m.to()];
        }
    }
}

// Selection sort step: moves the best remaining move to index i.
static Move pick_next(MoveList& moves, int* scores, size_t i) {
    size_t best = i;
    for (size_t j = i + 1; j < moves.size(); ++j)
        if (scores[j] > scores[best]) best = j;
    if (best != i) {
        std::swap(scores[i], scores[best]);
        moves.swap(i, best);
    }
    return moves[i];
}

void Worker::update_quiet_stats(Move best, const Move* quiets, int quiet_count, int depth, int ply) {
    const Color us = pos_.side_to_move();
    const int bonus = history_bonus(depth);
    apply_history(history_[us][best.from()][best.to()], bonus);
    for (int i = 0; i < quiet_count; ++i)
        apply_history(history_[us][quiets[i].from()][quiets[i].to()], -bonus);
    if (killers_[ply][0] != best) {
        killers_[ply][1] = killers_[ply][0];
        killers_[ply][0] = best;
    }
}

Value Worker::search(Value alpha, Value beta, int depth, int ply) {
    const bool pv_node = beta - alpha > 1;
    const bool root = ply == 0;
    pv_len_[ply] = ply;

    const bool in_check = pos_.in_check();
    if (in_check) ++depth;  // check extension
    if (depth <= 0) return qsearch(alpha, beta, ply);

    count_node();
    if (should_stop()) return 0;
    seldepth_ = std::max(seldepth_, ply);

    if (!root) {
        if (pos_.is_draw(ply)) return VALUE_DRAW;
        if (ply >= MAX_PLY - 1) return in_check ? VALUE_DRAW : eval::evaluate(pos_);
        // Mate distance pruning: no line from here can beat a shorter mate.
        alpha = std::max(alpha, mated_in(ply));
        beta = std::min(beta, mate_in(ply + 1));
        if (alpha >= beta) return alpha;
    }

    TTData tte;
    const bool tt_hit = engine_.tt_.probe(pos_.key(), tte);
    const Value tt_value = tt_hit ? value_from_tt(tte.score, ply) : VALUE_NONE;
    const Move tt_move = tt_hit ? tte.move : Move::none();
    if (!pv_node && tt_hit && tte.depth >= depth && tt_value != VALUE_NONE && pos_.rule50() < 90 &&
        (tte.bound == BOUND_EXACT || (tte.bound == BOUND_LOWER && tt_value >= beta) ||
         (tte.bound == BOUND_UPPER && tt_value <= alpha)))
        return tt_value;

    // Static evaluation, refined by the TT score when its bound allows.
    Value static_eval = VALUE_NONE, eval = VALUE_NONE;
    if (!in_check) {
        static_eval = tt_hit && tte.eval != VALUE_NONE ? tte.eval : eval::evaluate(pos_);
        eval = static_eval;
        if (tt_value != VALUE_NONE && (tte.bound & (tt_value > eval ? BOUND_LOWER : BOUND_UPPER)))
            eval = tt_value;
    }
    ss(ply).static_eval = static_eval;
    const bool improving = !in_check && ss(ply - 2).static_eval != VALUE_NONE &&
                           static_eval > ss(ply - 2).static_eval;
    killers_[ply + 1][0] = killers_[ply + 1][1] = Move::none();

    if (!pv_node && !in_check && !is_mate_score(beta)) {
        // Reverse futility pruning: far enough above beta that a shallow
        // search is very unlikely to fall below it.
        if (depth <= 7 && eval - 80 * (depth - improving) >= beta) return eval;

        // Null-move pruning: if passing still fails high, a real move will
        // too. Disabled without non-pawn material to avoid zugzwang errors
        // and after a null move to avoid two passes in a row.
        if (depth >= 3 && eval >= beta && static_eval >= beta && ss(ply - 1).move != Move::null() &&
            pos_.has_non_pawn_material(pos_.side_to_move())) {
            const int r = 3 + depth / 4 + std::min(3, (eval - beta) / 200);
            ss(ply).move = Move::null();
            pos_.make_null_move();
            const Value v = -search(-beta, -beta + 1, depth - 1 - r, ply + 1);
            pos_.unmake_null_move();
            if (engine_.stop_.load(std::memory_order_relaxed)) return 0;
            if (v >= beta) return is_mate_score(v) ? beta : v;
        }
    }

    // Internal iterative reduction: without a hash move, ordering is poor and
    // a slightly shallower search is the better investment.
    if (depth >= 4 && !tt_move.is_ok()) --depth;

    MoveList moves;
    generate<GenType::All>(pos_, moves);
    if (moves.empty()) return in_check ? mated_in(ply) : VALUE_DRAW;

    int scores[MAX_MOVES];
    score_moves(moves, scores, tt_move, ply);

    const Color us = pos_.side_to_move();
    const Value original_alpha = alpha;
    Value best = -VALUE_INFINITE;
    Move best_move = Move::none();
    Move quiets[64];
    int quiet_count = 0, searched = 0;
    bool skip_quiets = false;

    for (size_t i = 0; i < moves.size(); ++i) {
        const Move m = pick_next(moves, scores, i);
        if (root && std::find(root_moves_.begin(), root_moves_.end(), m) == root_moves_.end()) continue;

        const bool quiet = !m.is_noisy();
        const bool gives_check = pos_.gives_check(m);
        if (quiet && skip_quiets && !gives_check) continue;

        // Shallow-depth pruning, never while every move so far loses to mate.
        if (!root && best > -VALUE_MATE_IN_MAX_PLY && pos_.has_non_pawn_material(us)) {
            if (quiet && !gives_check) {
                // Late move pruning: beyond a depth-dependent move count, the
                // remaining quiet moves are almost never best.
                if (depth <= 8 && searched >= (3 + depth * depth) / (2 - improving)) {
                    skip_quiets = true;
                    continue;
                }
                // Futility pruning: the static eval is too far below alpha.
                if (!in_check && depth <= 6 && static_eval + 100 + 90 * depth <= alpha) continue;
                if (depth <= 7 && !pos_.see_ge(m, -50 * depth)) continue;
            } else if (depth <= 6 && !pos_.see_ge(m, -100 * depth)) {
                continue;
            }
        }

        ss(ply).move = m;
        pos_.make_move(m);
        engine_.tt_.prefetch(pos_.key());
        ++searched;
        const int new_depth = depth - 1;
        Value v;

        if (searched == 1) {
            v = -search(-beta, -alpha, new_depth, ply + 1);
        } else {
            // Late move reductions for quiet moves ordered late, verified by
            // a full-depth null-window search when they fail high, and by a
            // full-window search at PV nodes.
            int r = 0;
            if (depth >= 3 && quiet && searched > 1 + int(pv_node)) {
                r = Reductions.r[std::min(depth, 63)][std::min(searched, 63)];
                r += !improving;
                r -= pv_node;
                r -= gives_check;
                r -= scores[i] >= SCORE_KILLER - 1;
                r -= history_[us][m.from()][m.to()] / 8192;
                r = std::clamp(r, 0, new_depth - 1);
            }
            v = -search(-alpha - 1, -alpha, new_depth - r, ply + 1);
            if (v > alpha && r > 0) v = -search(-alpha - 1, -alpha, new_depth, ply + 1);
            if (v > alpha && v < beta) v = -search(-beta, -alpha, new_depth, ply + 1);
        }
        pos_.unmake_move(m);
        if (engine_.stop_.load(std::memory_order_relaxed)) return 0;

        if (v > best) {
            best = v;
            if (v > alpha) {
                best_move = m;
                if (pv_node) update_pv(ply, m);
                if (root) root_improved_ = true;
                if (v >= beta) {
                    if (quiet) update_quiet_stats(m, quiets, quiet_count, depth, ply);
                    break;
                }
                alpha = v;
            }
        }
        if (quiet && m != best_move && quiet_count < 64) quiets[quiet_count++] = m;
    }

    // Every move was filtered out (only possible at the root with searchmoves).
    if (searched == 0) return root ? alpha : best;

    const Bound bound = best >= beta ? BOUND_LOWER : best > original_alpha ? BOUND_EXACT : BOUND_UPPER;
    engine_.tt_.store(pos_.key(), best_move.is_ok() ? best_move : tt_move, value_to_tt(best, ply),
                      static_eval, depth, bound);
    return best;
}

Value Worker::qsearch(Value alpha, Value beta, int ply) {
    pv_len_[ply] = ply;
    count_node();
    if (should_stop()) return 0;
    seldepth_ = std::max(seldepth_, ply);

    const bool in_check = pos_.in_check();
    if (pos_.is_draw(ply)) return VALUE_DRAW;
    if (ply >= MAX_PLY - 1) return in_check ? VALUE_DRAW : eval::evaluate(pos_);

    TTData tte;
    const bool tt_hit = engine_.tt_.probe(pos_.key(), tte);
    const Value tt_value = tt_hit ? value_from_tt(tte.score, ply) : VALUE_NONE;
    const bool pv_node = beta - alpha > 1;
    if (!pv_node && tt_value != VALUE_NONE &&
        (tte.bound == BOUND_EXACT || (tte.bound == BOUND_LOWER && tt_value >= beta) ||
         (tte.bound == BOUND_UPPER && tt_value <= alpha)))
        return tt_value;

    // Stand pat: without check the side to move may decline all captures.
    const Value original_alpha = alpha;
    Value best = -VALUE_INFINITE, static_eval = VALUE_NONE;
    MoveList moves;
    if (in_check) {
        generate<GenType::All>(pos_, moves);  // all evasions
        if (moves.empty()) return mated_in(ply);
    } else {
        static_eval = tt_hit && tte.eval != VALUE_NONE ? tte.eval : eval::evaluate(pos_);
        best = static_eval;
        if (best >= beta) return best;
        alpha = std::max(alpha, best);
        generate<GenType::Noisy>(pos_, moves);
    }

    int scores[MAX_MOVES];
    score_moves(moves, scores, tt_hit ? tte.move : Move::none(), ply);
    Move best_move = Move::none();

    for (size_t i = 0; i < moves.size(); ++i) {
        const Move m = pick_next(moves, scores, i);
        // Skip captures that lose material by static exchange evaluation
        // (score_moves already placed them in the "bad" band).
        if (!in_check && scores[i] < SCORE_GOOD_NOISY) continue;

        pos_.make_move(m);
        const Value v = -qsearch(-beta, -alpha, ply + 1);
        pos_.unmake_move(m);
        if (engine_.stop_.load(std::memory_order_relaxed)) return 0;

        if (v > best) {
            best = v;
            if (v > alpha) {
                best_move = m;
                if (v >= beta) break;
                alpha = v;
            }
        }
    }

    const Bound bound = best >= beta ? BOUND_LOWER : best > original_alpha ? BOUND_EXACT : BOUND_UPPER;
    engine_.tt_.store(pos_.key(), best_move, value_to_tt(best, ply), static_eval, 0, bound);
    return best;
}

}  // namespace chess
