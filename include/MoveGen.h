/**
 * @file MoveGen.h
 * @brief Move generation declarations and precomputed attack lookup functions.
 * 
 * Declares sliding and non-sliding attack structures, magic bitboard helpers,
 * and functions for generating pseudo-legal and fully legal chess moves.
 */

#pragma once
#include <vector>
#include "Board.h"
#include "Move.h"

namespace MoveGen {
    // Attack tables for non-sliding pieces
    extern uint64_t knight_attacks[64];
    extern uint64_t king_attacks[64];

    // Sliding piece masks
    extern uint64_t bishop_masks[64];
    extern uint64_t rook_masks[64];

    // Magic numbers, shifts, and lookup tables for sliding pieces
    extern int bishop_shifts[64];
    extern int rook_shifts[64];
    extern uint64_t bishop_magics[64];
    extern uint64_t rook_magics[64];
    extern uint64_t* bishop_attack_table[64];
    extern uint64_t* rook_attack_table[64];

    /**
     * @brief Precomputes knight, king, and sliding piece attack tables.
     * Must be called at engine startup before any move generation.
     */
    void init_all();

    /**
     * @brief Gets bishop attacks for a square and occupancy.
     * @param sq Square index (0-63).
     * @param occupancy Current board occupancy.
     * @return uint64_t Bishop attack bitboard.
     */
    inline uint64_t get_bishop_attacks(int sq, uint64_t occupancy) {
        uint64_t occ = occupancy & bishop_masks[sq];
        int index = (int)((occ * bishop_magics[sq]) >> bishop_shifts[sq]);
        return bishop_attack_table[sq][index];
    }

    /**
     * @brief Gets rook attacks for a square and occupancy.
     * @param sq Square index (0-63).
     * @param occupancy Current board occupancy.
     * @return uint64_t Rook attack bitboard.
     */
    inline uint64_t get_rook_attacks(int sq, uint64_t occupancy) {
        uint64_t occ = occupancy & rook_masks[sq];
        int index = (int)((occ * rook_magics[sq]) >> rook_shifts[sq]);
        return rook_attack_table[sq][index];
    }

    /**
     * @brief Gets queen attacks for a square and occupancy.
     * @param sq Square index (0-63).
     * @param occupancy Current board occupancy.
     * @return uint64_t Queen attack bitboard.
     */
    inline uint64_t get_queen_attacks(int sq, uint64_t occupancy) {
        return get_bishop_attacks(sq, occupancy) | get_rook_attacks(sq, occupancy);
    }

    /**
     * @brief Generates all pseudo-legal moves (ignoring checks on own king).
     * @param board Chess board state.
     * @param move_list Vector where generated moves will be appended.
     */
    void generate_pseudo_legal_moves(const Board& board, std::vector<Move>& move_list);

    /**
     * @brief Generates all fully legal moves for the side to move.
     * Filters pseudo-legal moves by executing them and ensuring own king is not in check.
     * @param board Chess board state.
     * @param move_list Vector where legal moves will be appended.
     */
    void generate_legal_moves(Board& board, std::vector<Move>& move_list);
}

// Print confirmation message at the end of the file.
// Confirmation: MoveGen.h created successfully.
