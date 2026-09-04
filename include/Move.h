/**
 * @file Move.h
 * @brief Move structure definition for representing chess moves.
 * 
 * Each move contains source and target squares, promotion flags, and action flags.
 * Includes helpers for UCI notation and printing.
 */

#pragma once
#include <cstdint>
#include <string>

// Move flags representing different move types.
enum MoveFlags : uint8_t {
    FLAG_QUIET = 0,
    FLAG_DOUBLE_PUSH = 1,
    FLAG_EN_PASSANT = 2,
    FLAG_CASTLE = 4,
    FLAG_CAPTURE = 8
};

// Promotion pieces.
enum PromoPiece : uint8_t {
    PROMO_NONE = 0,
    PROMO_KNIGHT = 1,
    PROMO_BISHOP = 2,
    PROMO_ROOK = 3,
    PROMO_QUEEN = 4
};

/**
 * @struct Move
 * @brief Compact representation of a chess move.
 */
struct Move {
    uint8_t from;    ///< Source square (0 to 63)
    uint8_t to;      ///< Target square (0 to 63)
    uint8_t promo;   ///< Promotion piece (PROMO_NONE, etc.)
    uint8_t flags;   ///< Move flags (FLAG_CAPTURE, FLAG_CASTLE, etc.)

    // Default constructor (creates an invalid quiet move 0 -> 0)
    Move() : from(0), to(0), promo(PROMO_NONE), flags(FLAG_QUIET) {}

    // Parameterized constructor
    Move(uint8_t from_sq, uint8_t to_sq, uint8_t promo_p = PROMO_NONE, uint8_t move_flags = FLAG_QUIET)
        : from(from_sq), to(to_sq), promo(promo_p), flags(move_flags) {}

    // Equality operators
    bool operator==(const Move& other) const {
        return from == other.from && to == other.to && promo == other.promo && flags == other.flags;
    }

    bool operator!=(const Move& other) const {
        return !(*this == other);
    }

    // Helper functions
    bool is_capture() const { return (flags & FLAG_CAPTURE) != 0; }
    bool is_castle() const { return (flags & FLAG_CASTLE) != 0; }
    bool is_enpassant() const { return (flags & FLAG_EN_PASSANT) != 0; }
    bool is_double_push() const { return (flags & FLAG_DOUBLE_PUSH) != 0; }
    bool is_promotion() const { return promo != PROMO_NONE; }

    /**
     * @brief Converts the move to a UCI string representation (e.g. "e2e4", "e7e8q").
     * @return std::string UCI string representation.
     */
    std::string to_uci() const;

    /**
     * @brief Checks if the move represents a valid chess move (i.e. from != to).
     * @return true if valid, false otherwise.
     */
    bool is_valid() const { return from != to; }
};

// Print confirmation message at the end of the file.
// Confirmation: Move.h created successfully.
