#include "uci.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>

#include "core/movegen.h"
#include "engine/eval.h"

namespace chess {

namespace {

constexpr int MAX_HASH_MB = 65536;
constexpr int MAX_THREADS = 512;

std::vector<std::string> split(const std::string& line) {
    std::istringstream in(line);
    std::vector<std::string> tokens;
    for (std::string t; in >> t;) tokens.push_back(t);
    return tokens;
}

template <typename T>
bool parse_number(const std::string& s, T& out) {
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc() && ptr == s.data() + s.size();
}

std::string join(const std::vector<std::string>& tokens, size_t from, size_t to) {
    std::string s;
    for (size_t i = from; i < to; ++i) s += (i > from ? " " : "") + tokens[i];
    return s;
}

}  // namespace

Uci::Uci(std::istream& in, std::ostream& out) : in_(in), out_(out) {}

std::string Uci::format_score(Value v) {
    if (v >= VALUE_MATE_IN_MAX_PLY) return "mate " + std::to_string((VALUE_MATE - v + 1) / 2);
    if (v <= -VALUE_MATE_IN_MAX_PLY) return "mate " + std::to_string(-(VALUE_MATE + v) / 2);
    return "cp " + std::to_string(v);
}

void Uci::send(const std::string& line) {
    std::lock_guard<std::mutex> lock(out_mutex_);
    out_ << line << std::endl;  // flush: the GUI is waiting on a pipe
}

void Uci::loop() {
    std::string line;
    while (!quit_ && std::getline(in_, line)) handle(line);
    // End of input: let a bounded search finish so that piped scripts such as
    // `echo "go depth 12" | chess-engine` still print a bestmove.
    if (!quit_ && !infinite_search_) engine_.wait();
    engine_.stop();
    engine_.wait();
}

void Uci::handle(const std::string& line) {
    const std::vector<std::string> tokens = split(line);
    if (tokens.empty()) return;
    const std::string& cmd = tokens[0];

    // Commands that must be answered while a search is running.
    if (cmd == "isready") return send("readyok");
    if (cmd == "stop") return engine_.stop();
    if (cmd == "quit") {
        quit_ = true;
        return engine_.stop();
    }
    if (cmd == "ponderhit") return;  // pondering is not advertised

    // Everything else may change engine state; a running search is
    // preempted (a well-behaved GUI sends "stop" first anyway).
    engine_.stop();
    engine_.wait();

    if (cmd == "uci") {
        send(std::string("id name ") + ENGINE_NAME);
        send(std::string("id author ") + ENGINE_AUTHOR);
        send("option name Hash type spin default 16 min 1 max " + std::to_string(MAX_HASH_MB));
        send("option name Threads type spin default 1 min 1 max " + std::to_string(MAX_THREADS));
        send("option name Move Overhead type spin default 30 min 0 max 5000");
        send("option name Clear Hash type button");
        send("uciok");
    } else if (cmd == "ucinewgame") {
        engine_.new_game();
    } else if (cmd == "setoption") {
        cmd_setoption(tokens);
    } else if (cmd == "position") {
        cmd_position(tokens);
    } else if (cmd == "go") {
        cmd_go(tokens);
    } else if (cmd == "d") {
        send(pos_.pretty());
    } else if (cmd == "eval") {
        send("info string static eval " + std::to_string(eval::evaluate(pos_)) +
             " cp (side to move)");
    } else if (cmd == "perft") {
        int depth = 0;
        if (tokens.size() < 2 || !parse_number(tokens[1], depth) || depth < 1)
            return send("info string usage: perft <depth>");
        const auto start = std::chrono::steady_clock::now();
        MoveList moves;
        generate<GenType::All>(pos_, moves);
        std::uint64_t total = 0;
        for (const Move m : moves) {
            pos_.make_move(m);
            const std::uint64_t n = perft(pos_, depth - 1);
            pos_.unmake_move(m);
            total += n;
            send(m.uci() + ": " + std::to_string(n));
        }
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        send("\nNodes searched: " + std::to_string(total) + "  (" +
             std::to_string(std::uint64_t(double(total) / std::max(s, 1e-9))) + " nps)");
    } else {
        send("info string unknown command: " + cmd);
    }
}

void Uci::cmd_setoption(const std::vector<std::string>& tokens) {
    // setoption name <name words...> [value <value>]
    const auto name_it = std::find(tokens.begin(), tokens.end(), "name");
    const auto value_it = std::find(tokens.begin(), tokens.end(), "value");
    if (name_it == tokens.end()) return send("info string setoption: missing name");
    const size_t name_from = size_t(name_it - tokens.begin()) + 1;
    const size_t name_to = size_t(value_it - tokens.begin());
    std::string name = join(tokens, name_from, std::min(name_to, tokens.size()));
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
    const std::string value = value_it == tokens.end() ? "" : join(tokens, name_to + 1, tokens.size());

    int n = 0;
    if (name == "hash") {
        if (!parse_number(value, n) || n < 1 || n > MAX_HASH_MB) return send("info string invalid Hash value");
        engine_.set_hash_mb(std::size_t(n));
    } else if (name == "threads") {
        if (!parse_number(value, n) || n < 1 || n > MAX_THREADS) return send("info string invalid Threads value");
        engine_.set_threads(n);
    } else if (name == "move overhead") {
        if (!parse_number(value, n) || n < 0 || n > 5000) return send("info string invalid Move Overhead value");
        engine_.set_move_overhead(n);
    } else if (name == "clear hash") {
        engine_.new_game();
    } else {
        send("info string unknown option: " + name);
    }
}

bool parse_position(const std::vector<std::string>& tokens, size_t first, Position& out, std::string& error) {
    const auto moves_it = std::find(tokens.begin() + std::ptrdiff_t(std::min(first, tokens.size())), tokens.end(), "moves");
    const size_t moves_at = size_t(moves_it - tokens.begin());
    Position next;
    if (first < tokens.size() && tokens[first] == "fen") {
        if (!next.set_fen(join(tokens, first + 1, moves_at), &error)) {
            error = "invalid FEN: " + error;
            return false;
        }
    } else if (first >= tokens.size() || tokens[first] != "startpos" ||
               (moves_at != first + 1 && moves_at != tokens.size())) {
        error = "expected 'startpos' or 'fen <fen>', optionally followed by 'moves ...'";
        return false;
    }
    bool ok = true;
    for (size_t i = moves_at + 1; i < tokens.size(); ++i) {
        const Move m = next.parse_uci_move(tokens[i]);
        if (!m.is_ok()) {
            error = "illegal move " + tokens[i];
            ok = false;
            break;
        }
        next.make_move(m);
    }
    out = std::move(next);
    return ok;
}

void Uci::cmd_position(const std::vector<std::string>& tokens) {
    std::string error;
    if (!parse_position(tokens, 1, pos_, error)) {
        if (error.rfind("illegal move", 0) == 0) error += "; ignoring the remaining moves";
        send("info string " + error);
    }
}

void Uci::cmd_go(const std::vector<std::string>& tokens) {
    SearchLimits limits;
    static const char* keywords[] = {"searchmoves", "ponder", "wtime", "btime", "winc", "binc",
                                     "movestogo", "depth", "nodes", "mate", "movetime", "infinite"};
    auto is_keyword = [](const std::string& t) {
        return std::find(std::begin(keywords), std::end(keywords), t) != std::end(keywords);
    };

    for (size_t i = 1; i < tokens.size(); ++i) {
        const std::string& t = tokens[i];
        const std::string next = i + 1 < tokens.size() ? tokens[i + 1] : "";
        std::int64_t n = 0;
        const bool has_number = parse_number(next, n);
        if (t == "infinite" || t == "ponder") {
            limits.infinite = true;
        } else if (t == "searchmoves") {
            while (i + 1 < tokens.size() && !is_keyword(tokens[i + 1])) {
                const Move m = pos_.parse_uci_move(tokens[++i]);
                if (m.is_ok()) limits.searchmoves.push_back(m);
            }
        } else if (has_number) {
            ++i;
            // Some GUIs send negative clock values once flagged; clamp them.
            if (t == "wtime") limits.time[WHITE] = std::max<std::int64_t>(n, 1), limits.use_clock = true;
            else if (t == "btime") limits.time[BLACK] = std::max<std::int64_t>(n, 1), limits.use_clock = true;
            else if (t == "winc") limits.inc[WHITE] = std::max<std::int64_t>(n, 0);
            else if (t == "binc") limits.inc[BLACK] = std::max<std::int64_t>(n, 0);
            else if (t == "movestogo") limits.movestogo = int(std::clamp<std::int64_t>(n, 0, 1000));
            else if (t == "depth") limits.depth = int(std::clamp<std::int64_t>(n, 1, MAX_PLY - 1));
            else if (t == "nodes") limits.nodes = std::uint64_t(std::max<std::int64_t>(n, 1));
            else if (t == "mate") limits.depth = int(2 * std::clamp<std::int64_t>(n, 1, (MAX_PLY - 1) / 2));
            else if (t == "movetime") limits.movetime = std::max<std::int64_t>(n, 1);
            else --i;
        }
    }

    infinite_search_ = limits.infinite;
    engine_.start(
        pos_, limits,
        [this](const SearchInfo& info) {
            std::ostringstream s;
            s << "info depth " << info.depth << " seldepth " << info.seldepth << " multipv 1 score "
              << format_score(info.score) << " nodes " << info.nodes << " nps "
              << info.nodes * 1000 / std::uint64_t(std::max<std::int64_t>(info.time_ms, 1))
              << " hashfull " << info.hashfull << " time " << info.time_ms << " pv";
            for (const Move m : info.pv) s << ' ' << m.uci();
            send(s.str());
        },
        [this](const SearchResult& result) {
            std::string line = "bestmove " + result.best_move.uci();
            if (result.ponder_move.is_ok()) line += " ponder " + result.ponder_move.uci();
            send(line);
        });
}

}  // namespace chess
