/**
 * @file Search.h
 * @brief AI Search algorithm declarations.
 * 
 * Declares the iterative deepening, Negamax alpha-beta, quiescence search,
 * move ordering, and Transposition Table (TT) functions.
 */

#pragma once
#include <vector>
#include <chrono>
#include "Board.h"
#include "Move.h"

// Score constants
namespace Search {
    const int INF = 1000000;
    const int MATE_VALUE = 100000;
    const int MATE_THRESHOLD = 90000;
    const int MAX_PLY = 64;

    // Transposition Table flags
    enum HashFlags {
        HASH_EXACT = 0,
        HASH_ALPHA = 1,
        HASH_BETA = 2
    };

    /**
     * @struct TTEntry
     * @brief Entry in the Transposition Table.
     */
    struct TTEntry {
        uint64_t hash;     ///< 64-bit Zobrist key of the position.
        int depth;         ///< Search depth remaining.
        int score;         ///< Position score.
        int flags;         ///< Exact, alpha, or beta flag.
        Move best_move;    ///< Best move found from this position.
    };

    // Global variables for search control
    extern std::vector<TTEntry> tt_table;
    extern int tt_size_entries;
    extern uint64_t nodes;
    extern bool stop_search;
    extern long long start_time;
    extern long long time_limit;

    /**
     * @brief Allocates/resizes the transposition table.
     * @param mb Size in megabytes.
     */
    void resize_tt(int mb);

    /**
     * @brief Clears the transposition table.
     */
    void clear_tt();

    /**
     * @brief Stores an entry in the transposition table.
     */
    void write_tt(uint64_t hash, int depth, int score, int flags, Move best_move, int ply);

    /**
     * @brief Retrieves an entry from the transposition table.
     */
    bool read_tt(uint64_t hash, int depth, int alpha, int beta, int& score, Move& best_move, int ply);

    Move search_position(Board& board, int max_depth);
}

// Print confirmation message at the end of the file.
// Confirmation: Search.h created successfully.
