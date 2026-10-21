// Minimal EPD-style line parsing shared by the perft suite runner and tests.
// A line looks like:  <fen fields> ;D1 20 ;D2 400
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace chess {

struct PerftCase {
    std::string fen;
    std::vector<std::pair<int, std::uint64_t>> depths;  // (depth, expected leaf count)
};

// Reads a perft suite; blank lines and lines starting with '#' are skipped.
// Returns false (and sets *error) if the file cannot be read or is malformed.
bool load_perft_suite(const std::string& path, std::vector<PerftCase>& out, std::string* error);

}  // namespace chess
