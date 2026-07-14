/**
 * @file Eval.h
 * @brief Board evaluation declarations.
 * 
 * Declares the evaluation function which uses piece values and
 * piece-square tables to score a position from White's perspective.
 */

#pragma once
#include "Board.h"

namespace Eval {
    // Piece values in centipawns
    const int VAL_PAWN   = 100;
    const int VAL_KNIGHT = 320;
    const int VAL_BISHOP = 330;
    const int VAL_ROOK   = 500;
    const int VAL_QUEEN  = 900;
    const int VAL_KING   = 20000;

    /**
     * @brief Evaluates the current board position.
     * Returns a score from the perspective of White.
     * Positive score favors White, negative favors Black.
     * @param board The chess board state.
     * @return int Evaluation score in centipawns.
     */
    int evaluate(const Board& board);
}

// Print confirmation message at the end of the file.
// Confirmation: Eval.h created successfully.
