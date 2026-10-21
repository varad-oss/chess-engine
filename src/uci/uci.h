// Universal Chess Interface front end.
//
// The protocol loop runs on the calling thread and never blocks on the
// search: "go" starts a background search, so "stop", "isready" and "quit"
// are handled immediately while it runs. All output goes through one mutex
// because the search thread emits "info"/"bestmove" lines concurrently.
#pragma once

#include <iosfwd>
#include <mutex>
#include <string>
#include <vector>

#include "core/position.h"
#include "engine/search.h"

namespace chess {

constexpr const char* ENGINE_NAME = "Chess Engine 2.0";
constexpr const char* ENGINE_AUTHOR = "Varad Rajiv Pandare";

// Parses the argument of a UCI "position" command, starting at tokens[first]:
//   startpos [moves m1 m2 ...]   |   fen <fen fields> [moves m1 m2 ...]
// On an illegal move it returns false with `out` holding the position reached
// before that move; on a malformed command it returns false and leaves `out`
// unchanged. `error` describes the problem.
bool parse_position(const std::vector<std::string>& tokens, size_t first, Position& out, std::string& error);

class Uci {
public:
    Uci(std::istream& in, std::ostream& out);
    // Processes commands until "quit" or end of input. On end of input a
    // running fixed-limit search is allowed to finish first.
    void loop();

    // Formats a score the way UCI expects ("cp 25", "mate 3", "mate -2").
    static std::string format_score(Value v);

private:
    void handle(const std::string& line);
    void cmd_setoption(const std::vector<std::string>& tokens);
    void cmd_position(const std::vector<std::string>& tokens);
    void cmd_go(const std::vector<std::string>& tokens);
    void send(const std::string& line);

    std::istream& in_;
    std::ostream& out_;
    std::mutex out_mutex_;
    Engine engine_;
    Position pos_;
    bool quit_ = false;
    bool infinite_search_ = false;  // the last "go" was infinite (or ponder)
};

}  // namespace chess
