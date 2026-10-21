// match: plays two UCI engines against each other and measures the strength
// difference, optionally as a sequential probability ratio test (SPRT).
//
// Games are played in pairs from the same opening with colours reversed, and
// the statistics use the pentanomial model (the score of each game pair is one
// observation), which accounts for the correlation inside a pair and gives
// tighter, honest error bars than treating games as independent. Rules are
// adjudicated by the engine's own perft-verified move generator: checkmate,
// stalemate, threefold repetition, the fifty-move rule, insufficient
// material, illegal moves, crashes and time forfeits.
//
// POSIX only (fork/exec/pipes/poll).
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "core/movegen.h"
#include "core/position.h"
#include "core/rng.h"
#include "stats.h"

using namespace chess;
using chess::match::Stats;
using Clock = std::chrono::steady_clock;

namespace {

std::int64_t ms_since(Clock::time_point t) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
}

// ---------------------------------------------------------------------------
// Child process with line-oriented pipes.

std::mutex g_spawn_mutex;  // pipe creation + CLOEXEC + fork must not interleave across threads

class EngineProcess {
public:
    EngineProcess() = default;
    EngineProcess(const EngineProcess&) = delete;
    EngineProcess& operator=(const EngineProcess&) = delete;
    ~EngineProcess() { terminate(); }

    bool start(const std::string& command) {
        terminate();
        const std::string shell_cmd = "exec " + command;
        std::lock_guard<std::mutex> lock(g_spawn_mutex);
        int to_child[2], from_child[2];
        if (pipe(to_child) != 0) return false;
        if (pipe(from_child) != 0) {
            close(to_child[0]);
            close(to_child[1]);
            return false;
        }
        for (int fd : {to_child[0], to_child[1], from_child[0], from_child[1]}) fcntl(fd, F_SETFD, FD_CLOEXEC);

        pid_ = fork();
        if (pid_ == 0) {
            // Child: only async-signal-safe calls until exec.
            dup2(to_child[0], STDIN_FILENO);
            dup2(from_child[1], STDOUT_FILENO);
            const int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) dup2(devnull, STDERR_FILENO);
            execl("/bin/sh", "sh", "-c", shell_cmd.c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }
        close(to_child[0]);
        close(from_child[1]);
        if (pid_ < 0) {
            close(to_child[1]);
            close(from_child[0]);
            return false;
        }
        to_ = to_child[1];
        from_ = from_child[0];
        buffer_.clear();
        return true;
    }

    bool send(const std::string& line) {
        const std::string data = line + "\n";
        size_t written = 0;
        while (written < data.size()) {
            const ssize_t n = write(to_, data.data() + written, data.size() - written);
            if (n <= 0) return false;
            written += size_t(n);
        }
        return true;
    }

    // Reads one line, waiting at most timeout_ms. False on timeout or EOF.
    bool read_line(std::string& line, std::int64_t timeout_ms) {
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
        while (true) {
            const size_t nl = buffer_.find('\n');
            if (nl != std::string::npos) {
                line = buffer_.substr(0, nl);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                buffer_.erase(0, nl + 1);
                return true;
            }
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
            if (left <= 0) return false;
            pollfd p{from_, POLLIN, 0};
            const int r = poll(&p, 1, int(std::min<std::int64_t>(left, 1000)));
            if (r < 0 && errno != EINTR) return false;
            if (r <= 0) continue;
            char chunk[4096];
            const ssize_t n = read(from_, chunk, sizeof(chunk));
            if (n <= 0) return false;
            buffer_.append(chunk, size_t(n));
        }
    }

    bool wait_for(const std::string& token, std::int64_t timeout_ms) {
        const auto start = Clock::now();
        std::string line;
        while (read_line(line, timeout_ms - ms_since(start)))
            if (line.rfind(token, 0) == 0) return true;
        return false;
    }

    void terminate() {
        if (pid_ <= 0) return;
        send("quit");
        close(to_);
        // Give the engine a moment to exit cleanly, then kill it.
        for (int i = 0; i < 50; ++i) {
            if (waitpid(pid_, nullptr, WNOHANG) == pid_) {
                pid_ = -1;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (pid_ > 0) {
            kill(pid_, SIGKILL);
            waitpid(pid_, nullptr, 0);
        }
        close(from_);
        pid_ = -1;
        to_ = from_ = -1;
    }

private:
    pid_t pid_ = -1;
    int to_ = -1, from_ = -1;
    std::string buffer_;
};

// ---------------------------------------------------------------------------
// Configuration

struct EngineConfig {
    std::string command;
    std::string name;
    std::vector<std::string> options;  // "Name=Value"
};

struct Config {
    EngineConfig engine[2];
    int games = 100;
    int concurrency = 1;
    std::int64_t base_ms[2] = {0, 0}, inc_ms[2] = {0, 0};  // clock per engine (time odds allowed)
    std::uint64_t nodes = 0;               // or fixed nodes per move
    int depth = 0;                         // or fixed depth
    std::int64_t movetime = 0;             // or fixed time per move
    std::string openings_file;
    std::uint64_t seed = 1;
    bool sprt = false;
    double elo0 = 0, elo1 = 5, alpha = 0.05, beta = 0.05;
    int resign_score = 1000, resign_moves = 3;     // 0 disables
    int draw_score = 8, draw_moves = 10, draw_after = 40;  // draw_moves 0 disables
    int max_plies = 600;
    std::string log_file;
};

// ---------------------------------------------------------------------------
// Playing games

struct GameOutcome {
    int score = 1;  // for engine 0, in half points: 2 win, 1 draw, 0 loss
    std::string reason;
    std::string moves;
};

bool init_engine(EngineProcess& p, const EngineConfig& cfg) {
    if (!p.start(cfg.command)) return false;
    p.send("uci");
    if (!p.wait_for("uciok", 10000)) return false;
    for (const std::string& opt : cfg.options) {
        const size_t eq = opt.find('=');
        p.send("setoption name " + opt.substr(0, eq) + (eq == std::string::npos ? "" : " value " + opt.substr(eq + 1)));
    }
    p.send("isready");
    return p.wait_for("readyok", 30000);
}

// Parses the last reported score from an "info" line, side-to-move relative.
std::optional<int> parse_score(const std::string& info) {
    std::istringstream in(info);
    std::string tok;
    while (in >> tok) {
        if (tok != "score") continue;
        std::string kind;
        int v = 0;
        if (!(in >> kind >> v)) return std::nullopt;
        if (kind == "cp") return v;
        if (kind == "mate") return v > 0 ? 100000 - v : -100000 - v;
    }
    return std::nullopt;
}

GameOutcome play_game(EngineProcess* engines[2], const Config& cfg, const std::string& opening_fen,
                      int white_engine) {
    GameOutcome out;
    Position pos;
    pos.set_fen(opening_fen);
    // Clocks are indexed by colour; each side starts with its engine's time control.
    const int engine_of[2] = {white_engine, 1 - white_engine};
    std::int64_t clock_ms[2] = {cfg.base_ms[engine_of[WHITE]], cfg.base_ms[engine_of[BLACK]]};
    const std::int64_t inc_ms[2] = {cfg.inc_ms[engine_of[WHITE]], cfg.inc_ms[engine_of[BLACK]]};
    const bool use_clock = cfg.base_ms[0] > 0;
    int resign_streak = 0, draw_streak = 0, last_sign = 0;

    for (int i = 0; i < 2; ++i) {
        engines[i]->send("ucinewgame");
        engines[i]->send("isready");
        engines[i]->wait_for("readyok", 30000);
    }

    auto finish = [&](int engine0_score, const std::string& reason) {
        out.score = engine0_score;
        out.reason = reason;
        return out;
    };
    auto loss_for = [&](int engine_id) { return engine_id == 0 ? 0 : 2; };

    for (int ply = 0;; ++ply) {
        MoveList legal;
        generate<GenType::All>(pos, legal);
        const Color stm = pos.side_to_move();
        const int mover = (stm == WHITE) == (white_engine == 0) ? 0 : 1;
        if (legal.empty()) return finish(pos.in_check() ? loss_for(mover) : 1, pos.in_check() ? "checkmate" : "stalemate");
        if (pos.is_insufficient_material()) return finish(1, "insufficient material");
        if (pos.rule50() >= 100) return finish(1, "fifty-move rule");
        if (pos.is_repetition(0)) return finish(1, "threefold repetition");
        if (ply >= cfg.max_plies) return finish(1, "move limit");

        EngineProcess& e = *engines[mover];
        e.send("position fen " + opening_fen + (out.moves.empty() ? "" : " moves" + out.moves));
        std::ostringstream go;
        go << "go";
        std::int64_t timeout = 60000;
        if (use_clock) {
            go << " wtime " << clock_ms[WHITE] << " btime " << clock_ms[BLACK] << " winc " << inc_ms[WHITE]
               << " binc " << inc_ms[BLACK];
            timeout = clock_ms[stm] + 1000;
        } else if (cfg.nodes) {
            go << " nodes " << cfg.nodes;
        } else if (cfg.depth) {
            go << " depth " << cfg.depth;
        } else {
            go << " movetime " << cfg.movetime;
            timeout = cfg.movetime + 1000;
        }
        const auto t0 = Clock::now();
        e.send(go.str());

        std::string line, best;
        std::optional<int> score;
        while (true) {
            if (!e.read_line(line, timeout - ms_since(t0)))
                return finish(loss_for(mover), use_clock ? "time forfeit (no reply)" : "engine unresponsive");
            if (line.rfind("info", 0) == 0) {
                if (auto s = parse_score(line)) score = s;
            } else if (line.rfind("bestmove", 0) == 0) {
                std::istringstream in(line);
                in >> best >> best;
                break;
            }
        }
        if (use_clock) {
            clock_ms[stm] -= ms_since(t0);
            if (clock_ms[stm] < 0) return finish(loss_for(mover), "time forfeit");
            clock_ms[stm] += inc_ms[stm];
        }

        const Move m = pos.parse_uci_move(best);
        if (!m.is_ok()) return finish(loss_for(mover), "illegal move " + best);
        pos.make_move(m);
        out.moves += " " + best;

        // Adjudication. Scores are converted to White's point of view; both
        // engines must agree over consecutive plies.
        if (score) {
            const int white_score = stm == WHITE ? *score : -*score;
            const int sign = white_score > 0 ? 1 : -1;
            if (cfg.resign_moves && std::abs(white_score) >= cfg.resign_score) {
                resign_streak = sign == last_sign ? resign_streak + 1 : 1;
                last_sign = sign;
                if (resign_streak >= 2 * cfg.resign_moves) {
                    const bool white_wins = sign > 0;
                    return finish((white_wins == (white_engine == 0)) ? 2 : 0, "adjudicated win");
                }
            } else {
                resign_streak = 0;
            }
            if (cfg.draw_moves && ply >= 2 * cfg.draw_after && std::abs(white_score) <= cfg.draw_score) {
                if (++draw_streak >= 2 * cfg.draw_moves) return finish(1, "adjudicated draw");
            } else {
                draw_streak = 0;
            }
        }
    }
}

std::vector<std::string> load_openings(const Config& cfg) {
    std::vector<std::string> openings;
    if (cfg.openings_file.empty()) {
        openings.emplace_back(Position::START_FEN);
        return openings;
    }
    std::ifstream in(cfg.openings_file);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        // Accept EPD lines: keep the first four fields, drop any operations.
        std::istringstream fields(line);
        std::string fen, f;
        for (int i = 0; i < 6 && fields >> f && f.find(';') == std::string::npos; ++i) fen += (i ? " " : "") + f;
        Position pos;
        if (pos.set_fen(fen)) openings.push_back(pos.fen());
    }
    Rng rng(cfg.seed);
    for (size_t i = openings.size(); i > 1; --i) std::swap(openings[i - 1], openings[rng.below(i)]);
    return openings;
}

void usage() {
    std::cerr <<
        "usage: match --engine1 CMD --engine2 CMD [options]\n"
        "  --name1/--name2 NAME        display names\n"
        "  --option1/--option2 N=V     UCI option for that engine (repeatable)\n"
        "  --games N                   number of games (default 100, rounded up to pairs)\n"
        "  --concurrency N             parallel games (default 1)\n"
        "  --tc BASE+INC               clock in seconds, e.g. 10+0.1\n"
        "  --tc1/--tc2 BASE+INC        per-engine clock (time-odds matches)\n"
        "  --nodes N | --depth N | --movetime MS   fixed search limits instead of a clock\n"
        "  --openings FILE             EPD/FEN file (default: start position only)\n"
        "  --seed N                    opening shuffle seed\n"
        "  --sprt ELO0 ELO1 [ALPHA BETA]  stop when the SPRT accepts a hypothesis\n"
        "  --resign MOVES SCORE        adjudicate a win (0 MOVES disables; default 3 1000)\n"
        "  --draw AFTER MOVES SCORE    adjudicate a draw (0 MOVES disables; default 40 10 8)\n"
        "  --log FILE                  one line per game: opening, colours, result, moves\n";
}

bool parse_args(int argc, char** argv, Config& cfg) {
    auto need = [&](int& i, int n) {
        if (i + n >= argc) throw std::runtime_error(std::string("missing value for ") + argv[i]);
    };
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--engine1") need(i, 1), cfg.engine[0].command = argv[++i];
            else if (a == "--engine2") need(i, 1), cfg.engine[1].command = argv[++i];
            else if (a == "--name1") need(i, 1), cfg.engine[0].name = argv[++i];
            else if (a == "--name2") need(i, 1), cfg.engine[1].name = argv[++i];
            else if (a == "--option1") need(i, 1), cfg.engine[0].options.push_back(argv[++i]);
            else if (a == "--option2") need(i, 1), cfg.engine[1].options.push_back(argv[++i]);
            else if (a == "--games") need(i, 1), cfg.games = std::stoi(argv[++i]);
            else if (a == "--concurrency") need(i, 1), cfg.concurrency = std::stoi(argv[++i]);
            else if (a == "--nodes") need(i, 1), cfg.nodes = std::stoull(argv[++i]);
            else if (a == "--depth") need(i, 1), cfg.depth = std::stoi(argv[++i]);
            else if (a == "--movetime") need(i, 1), cfg.movetime = std::stoll(argv[++i]);
            else if (a == "--openings") need(i, 1), cfg.openings_file = argv[++i];
            else if (a == "--seed") need(i, 1), cfg.seed = std::stoull(argv[++i]);
            else if (a == "--log") need(i, 1), cfg.log_file = argv[++i];
            else if (a == "--tc" || a == "--tc1" || a == "--tc2") {
                need(i, 1);
                const std::string tc = argv[++i];
                const size_t plus = tc.find('+');
                const std::int64_t base = std::llround(std::stod(tc.substr(0, plus)) * 1000);
                const std::int64_t inc = plus == std::string::npos ? 0 : std::llround(std::stod(tc.substr(plus + 1)) * 1000);
                for (int e = 0; e < 2; ++e)
                    if (a == "--tc" || a == (e == 0 ? "--tc1" : "--tc2")) cfg.base_ms[e] = base, cfg.inc_ms[e] = inc;
            } else if (a == "--sprt") {
                need(i, 2);
                cfg.sprt = true;
                cfg.elo0 = std::stod(argv[++i]);
                cfg.elo1 = std::stod(argv[++i]);
                if (i + 2 < argc && argv[i + 1][0] != '-') {
                    cfg.alpha = std::stod(argv[++i]);
                    cfg.beta = std::stod(argv[++i]);
                }
            } else if (a == "--resign") {
                need(i, 2);
                cfg.resign_moves = std::stoi(argv[++i]);
                cfg.resign_score = std::stoi(argv[++i]);
            } else if (a == "--draw") {
                need(i, 3);
                cfg.draw_after = std::stoi(argv[++i]);
                cfg.draw_moves = std::stoi(argv[++i]);
                cfg.draw_score = std::stoi(argv[++i]);
            } else {
                throw std::runtime_error("unknown option " + a);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return false;
    }
    if (cfg.engine[0].command.empty() || cfg.engine[1].command.empty()) {
        std::cerr << "error: --engine1 and --engine2 are required\n";
        return false;
    }
    if ((cfg.base_ms[0] > 0) != (cfg.base_ms[1] > 0)) {
        std::cerr << "error: either both engines play on a clock or neither does\n";
        return false;
    }
    if (!cfg.base_ms[0] && !cfg.nodes && !cfg.depth && !cfg.movetime) cfg.movetime = 100;
    for (int i = 0; i < 2; ++i)
        if (cfg.engine[i].name.empty()) cfg.engine[i].name = "engine" + std::to_string(i + 1);
    cfg.games += cfg.games % 2;
    cfg.concurrency = std::max(1, cfg.concurrency);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    chess::init();
    signal(SIGPIPE, SIG_IGN);
    Config cfg;
    if (!parse_args(argc, argv, cfg)) {
        usage();
        return 2;
    }
    const std::vector<std::string> openings = load_openings(cfg);
    if (openings.empty()) {
        std::cerr << "error: no valid openings\n";
        return 2;
    }

    const double lower = match::sprt_lower_bound(cfg.alpha, cfg.beta), upper = match::sprt_upper_bound(cfg.alpha, cfg.beta);
    std::cout << cfg.engine[0].name << " vs " << cfg.engine[1].name << ": " << cfg.games << " games, "
              << openings.size() << " openings, concurrency " << cfg.concurrency << '\n';
    if (cfg.sprt)
        std::cout << "SPRT: elo0 " << cfg.elo0 << " elo1 " << cfg.elo1 << " alpha " << cfg.alpha << " beta "
                  << cfg.beta << "  bounds [" << std::fixed << std::setprecision(2) << lower << ", " << upper
                  << "]\n";

    std::mutex mutex;
    Stats stats;
    std::vector<int> first_of_pair(size_t(cfg.games / 2), -1);
    std::atomic<int> next_game{0};
    std::atomic<bool> done{false};
    std::ofstream log;
    if (!cfg.log_file.empty()) log.open(cfg.log_file);
    const auto start = Clock::now();

    auto worker = [&] {
        EngineProcess procs[2];
        bool ready[2] = {false, false};
        while (!done) {
            const int g = next_game++;
            if (g >= cfg.games) break;
            for (int i = 0; i < 2; ++i) {
                if (!ready[i] && !(ready[i] = init_engine(procs[i], cfg.engine[i]))) {
                    std::lock_guard<std::mutex> lock(mutex);
                    std::cerr << "error: could not start " << cfg.engine[i].command << '\n';
                    done = true;
                    return;
                }
            }
            const int pair = g / 2;
            const int white_engine = g % 2;  // engine 0 is White in even games
            const std::string& opening = openings[size_t(pair) % openings.size()];
            EngineProcess* ptrs[2] = {&procs[0], &procs[1]};
            const GameOutcome r = play_game(ptrs, cfg, opening, white_engine);
            // An engine that timed out or crashed is restarted for the next game.
            if (r.reason.find("forfeit") != std::string::npos || r.reason.find("unresponsive") != std::string::npos ||
                r.reason.find("illegal") != std::string::npos)
                ready[0] = ready[1] = false;

            std::lock_guard<std::mutex> lock(mutex);
            if (r.score == 2) ++stats.wins;
            else if (r.score == 0) ++stats.losses;
            else ++stats.draws;
            if (first_of_pair[size_t(pair)] < 0) {
                first_of_pair[size_t(pair)] = r.score;
            } else {
                ++stats.penta[first_of_pair[size_t(pair)] + r.score];
            }
            if (log) {
                log << opening << " | " << cfg.engine[white_engine].name << " (white) vs "
                    << cfg.engine[1 - white_engine].name << " | " << (r.score == 2 ? "1-0*" : r.score == 0 ? "0-1*" : "1/2")
                    << " for " << cfg.engine[0].name << " | " << r.reason << " |" << r.moves << '\n';
            }
            std::cout << std::fixed << std::setprecision(1) << "[" << ms_since(start) / 1000.0 << "s] "
                      << stats.summary();
            if (cfg.sprt) {
                const double llr = stats.llr(cfg.elo0, cfg.elo1);
                std::cout << std::setprecision(2) << "  LLR " << llr << " [" << lower << ", " << upper << "]";
                if (llr >= upper || llr <= lower) done = true;
            }
            std::cout << "  (" << r.reason << ")" << std::endl;
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < cfg.concurrency; ++i) threads.emplace_back(worker);
    for (auto& t : threads) t.join();

    std::cout << "\nFinal: " << stats.summary() << '\n';
    if (cfg.sprt) {
        const double llr = stats.llr(cfg.elo0, cfg.elo1);
        std::cout << "SPRT: LLR " << std::setprecision(2) << llr << " -> "
                  << (llr >= upper ? "H1 accepted (" + cfg.engine[0].name + " is stronger by >= elo1 with confidence)"
                      : llr <= lower ? "H0 accepted (no gain of elo1)" : "inconclusive")
                  << '\n';
    }
    return 0;
}
