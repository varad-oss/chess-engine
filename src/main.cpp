#include <iostream>

#include "app/commands.h"
#include "core/bitboard.h"
#include "core/position.h"
#include "uci/uci.h"

namespace {

void usage() {
    std::cout << "usage: chess-engine [command]\n\n"
                 "  (no command)                  speak UCI on stdin/stdout\n"
                 "  bench [depth] [threads] [MB]  fixed-depth search benchmark\n"
                 "  perft <depth> [fen]           divided perft of a position\n"
                 "  perft-suite <file> [max]      verify perft counts from an EPD file\n"
                 "  datagen [options]             self-play training data for the tuner\n"
                 "  openings [options]            balanced random-ply openings for matches\n"
                 "  serve [options]               local HTTP API for the web frontend\n";
}

}  // namespace

int main(int argc, char** argv) {
    chess::init();
    const chess::Args args(argv + 1, argv + argc);
    if (args.empty()) {
        chess::Uci uci(std::cin, std::cout);
        uci.loop();
        return 0;
    }

    const std::string& cmd = args[0];
    if (cmd == "bench") return chess::bench_command(args);
    if (cmd == "perft") return chess::perft_command(args);
    if (cmd == "perft-suite") return chess::perft_suite_command(args);
    if (cmd == "datagen") return chess::datagen_command(args);
    if (cmd == "openings") return chess::openings_command(args);
    if (cmd == "serve") return chess::serve_command(args);
    if (cmd == "magics") {  // developer command: regenerate src/core/magics.inc
        std::cout << chess::generate_magics_source();
        return 0;
    }
    if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        usage();
        return 0;
    }
    std::cerr << "unknown command '" << cmd << "'\n";
    usage();
    return 2;
}
