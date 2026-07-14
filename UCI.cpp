/**
 * @file UCI.cpp
 * @brief Universal Chess Interface command loop implementation.
 */

#include "UCI.h"
#include "MoveGen.h"
#include "Search.h"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>

/**
 * Recursive helper to count leaf nodes at a given depth.
 */
static uint64_t perft(Board& board, int depth) {
    if (depth <= 0) return 1ULL;

    std::vector<Move> moves;
    MoveGen::generate_legal_moves(board, moves);

    uint64_t nodes = 0;
    for (const auto& m : moves) {
        board.make_move(m);
        nodes += perft(board, depth - 1);
        board.unmake_move(m);
    }
    return nodes;
}

/**
 * Run a performance test (perft) and print split results for each root move.
 */
static void run_perft(Board& board, int depth) {
    auto start = std::chrono::steady_clock::now();

    std::vector<Move> moves;
    MoveGen::generate_legal_moves(board, moves);

    uint64_t total_nodes = 0;
    std::cout << "\nPerft results (depth " << depth << "):\n";
    for (const auto& m : moves) {
        board.make_move(m);
        uint64_t nodes = perft(board, depth - 1);
        total_nodes += nodes;
        board.unmake_move(m);
        std::cout << m.to_uci() << ": " << nodes << "\n";
    }

    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;
    std::cout << "\nTotal nodes: " << total_nodes << "\n";
    std::cout << "Time elapsed: " << (long long)elapsed.count() << " ms\n";
    if (elapsed.count() > 0) {
        std::cout << "NPS: " << (uint64_t)(total_nodes * 1000.0 / elapsed.count()) << "\n\n";
    }
    std::cout << std::flush;
}

/**
 * Parse the "position" command.
 */
static void parse_position(Board& board, const std::string& line) {
    std::stringstream ss(line);
    std::string token;
    ss >> token; // skip "position"

    ss >> token;
    if (token == "startpos") {
        board.parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        ss >> token; // check if "moves" is next
    } else if (token == "fen") {
        std::string fen = "";
        for (int i = 0; i < 6; i++) {
            ss >> token;
            fen += token + " ";
        }
        board.parse_fen(fen);
        ss >> token; // check if "moves" is next
    }

    if (token == "moves") {
        while (ss >> token) {
            std::vector<Move> moves;
            MoveGen::generate_legal_moves(board, moves);
            Move matched_move;
            for (const auto& m : moves) {
                if (m.to_uci() == token) {
                    matched_move = m;
                    break;
                }
            }
            if (matched_move.is_valid()) {
                board.make_move(matched_move);
            }
        }
    }
}

/**
 * Parse the "go" command.
 */
static void parse_go(Board& board, const std::string& line) {
    std::stringstream ss(line);
    std::string token;
    ss >> token; // skip "go"

    int depth = Search::MAX_PLY;
    long long wtime = -1, btime = -1, winc = -1, binc = -1, movetime = -1;

    while (ss >> token) {
        if (token == "depth") ss >> depth;
        else if (token == "wtime") ss >> wtime;
        else if (token == "btime") ss >> btime;
        else if (token == "winc") ss >> winc;
        else if (token == "binc") ss >> binc;
        else if (token == "movetime") ss >> movetime;
    }

    long long time_allocated = -1;
    if (movetime != -1) {
        time_allocated = movetime;
    } else if (board.side_to_move == WHITE && wtime != -1) {
        time_allocated = wtime / 20;
        if (winc != -1) time_allocated += winc / 2;
    } else if (board.side_to_move == BLACK && btime != -1) {
        time_allocated = btime / 20;
        if (binc != -1) time_allocated += binc / 2;
    }

    // Adjust allocation if it's too short
    if (time_allocated > 0) {
        // subtract a small safety buffer (e.g. 50ms) to ensure we don't flag
        time_allocated = std::max(10LL, time_allocated - 50);
    }

    Search::time_limit = time_allocated;
    Search::search_position(board, depth);
}

void UCI::loop() {
    Board board;
    Search::resize_tt(24); // default 24MB transposition table (1M entries)

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        std::stringstream ss(line);
        std::string command;
        ss >> command;

        if (command == "uci") {
            std::cout << "id name Antigravity Chess Engine\n";
            std::cout << "id author Google DeepMind pair-programming agent\n";
            std::cout << "uciok\n" << std::flush;
        } else if (command == "isready") {
            std::cout << "readyok\n" << std::flush;
        } else if (command == "ucinewgame") {
            board.parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
            Search::clear_tt();
        } else if (command == "position") {
            parse_position(board, line);
        } else if (command == "go") {
            parse_go(board, line);
        } else if (command == "stop") {
            Search::stop_search = true;
        } else if (command == "quit") {
            break;
        } else if (command == "d") {
            // debug print board
            board.print();
        } else if (command == "perft") {
            int depth = 1;
            ss >> depth;
            run_perft(board, depth);
        }
    }
}

// Print confirmation message at the end of the file.
// Confirmation: UCI.cpp created successfully.
