/**
 * @file Eval.cpp
 * @brief Board evaluation function using material values, PSTs, and positional rules.
 */

#include "Eval.h"
#include "MoveGen.h"

namespace Eval {
    // File masks for checking doubled/isolated pawns and rook open files
    const uint64_t file_masks[8] = {
        0x0101010101010101ULL, // File A
        0x0202020202020202ULL, // File B
        0x0404040404040404ULL, // File C
        0x0808080808080808ULL, // File D
        0x1010101010101010ULL, // File E
        0x2020202020202020ULL, // File F
        0x4040404040404040ULL, // File G
        0x8080808080808080ULL  // File H
    };

    // Piece-Square Tables (PST) from White's perspective.
    // Index 0 is A1 (White's bottom-left), index 63 is H8 (Black's top-right).
    // Black positions are mirrored rank-wise using (sq ^ 56).

    const int pst_pawn[64] = {
          0,   0,   0,   0,   0,   0,   0,   0,
          5,  10,  10, -20, -20,  10,  10,   5,
          5,  -5, -10,   0,   0, -10,  -5,   5,
          0,   0,   0,  20,  20,   0,   0,   0,
          5,   5,  10,  25,  25,  10,   5,   5,
         10,  10,  20,  30,  30,  20,  10,  10,
         50,  50,  50,  50,  50,  50,  50,  50,
          0,   0,   0,   0,   0,   0,   0,   0
    };

    const int pst_knight[64] = {
        -50, -40, -30, -30, -30, -30, -40, -50,
        -40, -20,   0,   5,   5,   0, -20, -40,
        -30,   5,  10,  15,  15,  10,   5, -30,
        -30,   0,  15,  20,  20,  15,   0, -30,
        -30,   5,  15,  20,  20,  15,   5, -30,
        -30,   0,  10,  15,  15,  10,   0, -30,
        -40, -20,   0,   0,   0,   0, -20, -40,
        -50, -40, -30, -30, -30, -30, -40, -50
    };

    const int pst_bishop[64] = {
        -20, -10, -10, -10, -10, -10, -10, -20,
        -10,   5,   0,   0,   0,   0,   5, -10,
        -10,  10,  10,  10,  10,  10,  10, -10,
        -10,   0,  10,  10,  10,  10,   0, -10,
        -10,   5,   5,  10,  10,   5,   5, -10,
        -10,   0,   5,  10,  10,   5,   0, -10,
        -10,   0,   0,   0,   0,   0,   0, -10,
        -20, -10, -10, -10, -10, -10, -10, -20
    };

    const int pst_rook[64] = {
          0,   0,   0,   5,   5,   0,   0,   0,
         -5,   0,   0,   0,   0,   0,   0,  -5,
         -5,   0,   0,   0,   0,   0,   0,  -5,
         -5,   0,   0,   0,   0,   0,   0,  -5,
         -5,   0,   0,   0,   0,   0,   0,  -5,
         -5,   0,   0,   0,   0,   0,   0,  -5,
          5,  10,  10,  10,  10,  10,  10,   5,
          0,   0,   0,   0,   0,   0,   0,   0
    };

    const int pst_queen[64] = {
        -20, -10, -10,  -5,  -5, -10, -10, -20,
        -10,   0,   0,   0,   0,   0,   0, -10,
        -10,   0,   5,   5,   5,   5,   0, -10,
         -5,   0,   5,   5,   5,   5,   0,  -5,
          0,   0,   5,   5,   5,   5,   0,  -5,
        -10,   5,   5,   5,   5,   5,   0, -10,
        -10,   0,   5,   0,   0,   0,   0, -10,
        -20, -10, -10,  -5,  -5, -10, -10, -20
    };

    // King PST for middlegame (prefers safety)
    const int pst_king_middle[64] = {
         20,  30,  10,   0,   0,  10,  30,  20,
         20,  20,   0,   0,   0,   0,  20,  20,
        -10, -20, -20, -20, -20, -20, -20, -10,
        -20, -30, -30, -40, -40, -30, -30, -20,
        -30, -40, -40, -50, -50, -40, -40, -30,
        -30, -40, -40, -50, -50, -40, -40, -30,
        -30, -40, -40, -50, -50, -40, -40, -30,
        -30, -40, -40, -50, -50, -40, -40, -30
    };

    // King PST for endgame (prefers central activity)
    const int pst_king_end[64] = {
        -50, -30, -30, -30, -30, -30, -30, -50,
        -30, -30,   0,   0,   0,   0, -30, -30,
        -30, -10,  20,  30,  30,  20, -10, -30,
        -30, -10,  30,  40,  40,  30, -10, -30,
        -30, -10,  30,  40,  40,  30, -10, -30,
        -30, -10,  20,  30,  30,  20, -10, -30,
        -30, -30,   0,   0,   0,   0, -30, -30,
        -50, -30, -30, -30, -30, -30, -30, -50
    };

    int evaluate(const Board& board) {
        int score = 0;

        // 1. Determine if we are in the endgame.
        // We count white and black knights, bishops, rooks, queens.
        int non_pawn_pieces = 0;
        non_pawn_pieces += count_bits(board.bitboards[W_KNIGHT]) + count_bits(board.bitboards[B_KNIGHT]);
        non_pawn_pieces += count_bits(board.bitboards[W_BISHOP]) + count_bits(board.bitboards[B_BISHOP]);
        non_pawn_pieces += count_bits(board.bitboards[W_ROOK])   + count_bits(board.bitboards[B_ROOK]);
        non_pawn_pieces += count_bits(board.bitboards[W_QUEEN])  + count_bits(board.bitboards[B_QUEEN]);

        bool is_endgame = (non_pawn_pieces <= 6);

        // All active pawns combined
        uint64_t all_pawns = board.bitboards[W_PAWN] | board.bitboards[B_PAWN];

        // 2. Evaluate material and PSTs
        // White Pawns
        uint64_t w_pawns = board.bitboards[W_PAWN];
        while (w_pawns) {
            int sq = get_lsb(w_pawns);
            int f = sq % 8;
            
            int pawn_score = VAL_PAWN + pst_pawn[sq];

            // Doubled pawn penalty (-15)
            if (count_bits(board.bitboards[W_PAWN] & file_masks[f]) > 1) {
                pawn_score -= 15;
            }

            // Isolated pawn penalty (-20)
            bool adjacent_pawns = false;
            if (f > 0 && (board.bitboards[W_PAWN] & file_masks[f - 1])) adjacent_pawns = true;
            if (f < 7 && (board.bitboards[W_PAWN] & file_masks[f + 1])) adjacent_pawns = true;
            if (!adjacent_pawns) {
                pawn_score -= 20;
            }

            score += pawn_score;
            clear_bit(w_pawns, sq);
        }

        // Black Pawns
        uint64_t b_pawns = board.bitboards[B_PAWN];
        while (b_pawns) {
            int sq = get_lsb(b_pawns);
            int f = sq % 8;
            
            int pawn_score = VAL_PAWN + pst_pawn[sq ^ 56];

            // Doubled pawn penalty
            if (count_bits(board.bitboards[B_PAWN] & file_masks[f]) > 1) {
                pawn_score -= 15;
            }

            // Isolated pawn penalty
            bool adjacent_pawns = false;
            if (f > 0 && (board.bitboards[B_PAWN] & file_masks[f - 1])) adjacent_pawns = true;
            if (f < 7 && (board.bitboards[B_PAWN] & file_masks[f + 1])) adjacent_pawns = true;
            if (!adjacent_pawns) {
                pawn_score -= 20;
            }

            score -= pawn_score;
            clear_bit(b_pawns, sq);
        }

        // White Knights
        uint64_t w_knights = board.bitboards[W_KNIGHT];
        while (w_knights) {
            int sq = get_lsb(w_knights);
            score += VAL_KNIGHT + pst_knight[sq];
            clear_bit(w_knights, sq);
        }
        // Black Knights
        uint64_t b_knights = board.bitboards[B_KNIGHT];
        while (b_knights) {
            int sq = get_lsb(b_knights);
            score -= VAL_KNIGHT + pst_knight[sq ^ 56];
            clear_bit(b_knights, sq);
        }

        // White Bishops
        uint64_t w_bishops = board.bitboards[W_BISHOP];
        while (w_bishops) {
            int sq = get_lsb(w_bishops);
            int bishop_score = VAL_BISHOP + pst_bishop[sq];

            // Long diagonal bonus (+10)
            if ((sq / 8 == sq % 8) || (sq / 8 + sq % 8 == 7)) {
                bishop_score += 10;
            }

            // Trapped bishop penalty (-150)
            if (sq == 48 && get_bit(all_pawns, 41)) bishop_score -= 150;      // A7 blocked by B6
            else if (sq == 55 && get_bit(all_pawns, 46)) bishop_score -= 150; // H7 blocked by G6
            else if (sq == 8  && get_bit(all_pawns, 17)) bishop_score -= 150; // A2 blocked by B3
            else if (sq == 15 && get_bit(all_pawns, 22)) bishop_score -= 150; // H2 blocked by G3

            score += bishop_score;
            clear_bit(w_bishops, sq);
        }

        // Black Bishops
        uint64_t b_bishops = board.bitboards[B_BISHOP];
        while (b_bishops) {
            int sq = get_lsb(b_bishops);
            int bishop_score = VAL_BISHOP + pst_bishop[sq ^ 56];

            // Long diagonal bonus
            if ((sq / 8 == sq % 8) || (sq / 8 + sq % 8 == 7)) {
                bishop_score += 10;
            }

            // Trapped bishop penalty
            if (sq == 8  && get_bit(all_pawns, 17)) bishop_score -= 150;      // A2 blocked by B3 (from Black's view, A2 blocked by B3)
            else if (sq == 15 && get_bit(all_pawns, 22)) bishop_score -= 150; // H2 blocked by G3
            else if (sq == 48 && get_bit(all_pawns, 41)) bishop_score -= 150; // A7 blocked by B6
            else if (sq == 55 && get_bit(all_pawns, 46)) bishop_score -= 150; // H7 blocked by G6

            score -= bishop_score;
            clear_bit(b_bishops, sq);
        }

        // White Rooks
        uint64_t w_rooks = board.bitboards[W_ROOK];
        while (w_rooks) {
            int sq = get_lsb(w_rooks);
            int f = sq % 8;
            int r = sq / 8;
            int rook_score = VAL_ROOK + pst_rook[sq];

            // Open/Semi-open files
            bool has_w_pawns = (board.bitboards[W_PAWN] & file_masks[f]) != 0;
            bool has_b_pawns = (board.bitboards[B_PAWN] & file_masks[f]) != 0;
            if (!has_w_pawns && !has_b_pawns) {
                rook_score += 25; // Open file
            } else if (!has_w_pawns) {
                rook_score += 15; // Semi-open file
            }

            // 7th rank bonus (+30)
            if (r == 6) {
                rook_score += 30;
            }

            score += rook_score;
            clear_bit(w_rooks, sq);
        }

        // Black Rooks
        uint64_t b_rooks = board.bitboards[B_ROOK];
        while (b_rooks) {
            int sq = get_lsb(b_rooks);
            int f = sq % 8;
            int r = sq / 8;
            int rook_score = VAL_ROOK + pst_rook[sq ^ 56];

            // Open/Semi-open files
            bool has_w_pawns = (board.bitboards[W_PAWN] & file_masks[f]) != 0;
            bool has_b_pawns = (board.bitboards[B_PAWN] & file_masks[f]) != 0;
            if (!has_w_pawns && !has_b_pawns) {
                rook_score += 25; // Open file
            } else if (!has_b_pawns) {
                rook_score += 15; // Semi-open file
            }

            // 7th rank bonus (rank 2 from White view = index rank 1)
            if (r == 1) {
                rook_score += 30;
            }

            score -= rook_score;
            clear_bit(b_rooks, sq);
        }

        // White Queens
        uint64_t w_queens = board.bitboards[W_QUEEN];
        while (w_queens) {
            int sq = get_lsb(w_queens);
            score += VAL_QUEEN + pst_queen[sq];
            clear_bit(w_queens, sq);
        }
        // Black Queens
        uint64_t b_queens = board.bitboards[B_QUEEN];
        while (b_queens) {
            int sq = get_lsb(b_queens);
            score -= VAL_QUEEN + pst_queen[sq ^ 56];
            clear_bit(b_queens, sq);
        }

        // Kings
        int w_king_sq = get_lsb(board.bitboards[W_KING]);
        int b_king_sq = get_lsb(board.bitboards[B_KING]);

        int w_king_score = VAL_KING;
        int b_king_score = VAL_KING;

        if (is_endgame) {
            w_king_score += pst_king_end[w_king_sq];
            b_king_score += pst_king_end[b_king_sq ^ 56];
        } else {
            w_king_score += pst_king_middle[w_king_sq];
            b_king_score += pst_king_middle[b_king_sq ^ 56];

            // King safety: check pawns in front of the castled King
            // White King safety
            if (w_king_sq == 6 || w_king_sq == 7) { // Kingside castled
                if (!get_bit(board.bitboards[W_PAWN], 13)) w_king_score -= 20; // F2
                if (!get_bit(board.bitboards[W_PAWN], 14)) w_king_score -= 20; // G2
                if (!get_bit(board.bitboards[W_PAWN], 15)) w_king_score -= 20; // H2
            } else if (w_king_sq == 1 || w_king_sq == 2) { // Queenside castled
                if (!get_bit(board.bitboards[W_PAWN], 8))  w_king_score -= 20; // A2
                if (!get_bit(board.bitboards[W_PAWN], 9))  w_king_score -= 20; // B2
                if (!get_bit(board.bitboards[W_PAWN], 10)) w_king_score -= 20; // C2
            }

            // Black King safety
            if (b_king_sq == 62 || b_king_sq == 63) { // Kingside castled
                if (!get_bit(board.bitboards[B_PAWN], 53)) b_king_score -= 20; // F7
                if (!get_bit(board.bitboards[B_PAWN], 54)) b_king_score -= 20; // G7
                if (!get_bit(board.bitboards[B_PAWN], 55)) b_king_score -= 20; // H7
            } else if (b_king_sq == 57 || b_king_sq == 58) { // Queenside castled
                if (!get_bit(board.bitboards[B_PAWN], 48)) b_king_score -= 20; // A7
                if (!get_bit(board.bitboards[B_PAWN], 49)) b_king_score -= 20; // B7
                if (!get_bit(board.bitboards[B_PAWN], 50)) b_king_score -= 20; // C7
            }
        }

        score += w_king_score;
        score -= b_king_score;

        // 3. Mobility approximation
        // Instead of generating full legal moves which destroys NPS, we just 
        // approximate mobility by counting the number of non-pawn pieces.
        // A more advanced engine would use pseudo-legal attack bitboards.
        int w_mobility_approx = count_bits(w_knights | w_bishops | w_rooks | w_queens);
        int b_mobility_approx = count_bits(b_knights | b_bishops | b_rooks | b_queens);
        score += 5 * (w_mobility_approx - b_mobility_approx);

        return score;
    }
}

// Print confirmation message at the end of the file.
// Confirmation: Eval.cpp created successfully.
