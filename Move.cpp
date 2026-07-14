/**
 * @file Move.cpp
 * @brief Implementation of helper functions for Move struct.
 */

#include "Move.h"

/**
 * Converts the move to a UCI string representation.
 * For example: 8 (a2) to 24 (a4) -> "a2a4".
 * Promotion is appended: "e7e8q".
 */
std::string Move::to_uci() const {
    if (!is_valid()) {
        return "0000";
    }

    std::string uci = "";
    
    // File of source square (0 to 7 -> 'a' to 'h')
    char from_file = 'a' + (from % 8);
    // Rank of source square (0 to 7 -> '1' to '8')
    char from_rank = '1' + (from / 8);
    
    // File of target square
    char to_file = 'a' + (to % 8);
    // Rank of target square
    char to_rank = '1' + (to / 8);

    uci += from_file;
    uci += from_rank;
    uci += to_file;
    uci += to_rank;

    // Append promotion piece character if applicable
    if (is_promotion()) {
        switch (promo) {
            case PROMO_KNIGHT: uci += 'n'; break;
            case PROMO_BISHOP: uci += 'b'; break;
            case PROMO_ROOK:   uci += 'r'; break;
            case PROMO_QUEEN:  uci += 'q'; break;
            default:           break;
        }
    }

    return uci;
}

// Print confirmation message at the end of the file.
// Confirmation: Move.cpp created successfully.
