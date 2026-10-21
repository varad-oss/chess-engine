// Command-line subcommands of the engine binary (see main.cpp for usage).
#pragma once

#include <string>
#include <vector>

namespace chess {

using Args = std::vector<std::string>;

int bench_command(const Args& args);
int perft_command(const Args& args);
int perft_suite_command(const Args& args);
int datagen_command(const Args& args);
int openings_command(const Args& args);
int serve_command(const Args& args);

// Fixed benchmark positions, also used by the tests.
const std::vector<std::string>& bench_positions();

}  // namespace chess
