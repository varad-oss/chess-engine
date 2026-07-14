/**
 * @file Board.h
 * @brief Board representation using 64-bit bitboards.
 * 
 * Defines square names, piece enums, colors, and the Board class
 * which manages game state, en passant, castling, and FEN parsing.
 */

#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "Move.h"

// Squares from A1 (0) to H8 (63). Little-endian rank-file mapping (LERF).
enum Squares : int {
    A1, B1, C1, D1, E1, F1, G1, H1,
    A2, B2, C2, D2, E2, F2, G2, H2,
    A3, B3, C3, D3, E3, F3, G3, H3,
    A4, B4, C4, D4, E4, F4, G4, H4,
    A5, B5, C5, D5, E5, F5, G5, H5,
    A6, B6, C6, D6, E6, F6, G6, H6,
    A7, B7, C7, D7, E7, F7, G7, H7,
    A8, B8, C8, D8, E8, F8, G8, H8,
    NO_SQ = 64
};

// Player colors.
enum Colors : int {
    WHITE = 0,
    BLACK = 1,
    BOTH = 2
};

// Piece types.
enum PieceTypes : int {
    PAWN = 0,
    KNIGHT = 1,
    BISHOP = 2,
    ROOK = 3,
    QUEEN = 4,
    KING = 5,
    NONE = 6
};

// Pieces index.
enum Pieces : int {
    W_PAWN = 0, W_KNIGHT = 1, W_BISHOP = 2, W_ROOK = 3, W_QUEEN = 4, W_KING = 5,
    B_PAWN = 6, B_KNIGHT = 7, B_BISHOP = 8, B_ROOK = 9, B_QUEEN = 10, B_KING = 11,
    EMPTY_PIECE = 12
};

// Castling rights represented by 4 bits.
enum Castling : int {
    WK_CASTLE = 1,  // White King-side (1)
    WQ_CASTLE = 2,  // White Queen-side (2)
    BK_CASTLE = 4,  // Black King-side (4)
    BQ_CASTLE = 8   // Black Queen-side (8)
};

/**
 * @struct UndoInfo
 * @brief Stores historical board state to allow unmaking moves.
 */
struct UndoInfo {
    int ep_sq;
    int castling_rights;
    int halfmove_clock;
    int captured_piece;
    uint64_t hash_key;
};

/**
 * @class Board
 * @brief Manages the 64-bit bitboard state of the chess game.
 */
class Board {
public:
    uint64_t bitboards[12];    ///< 12 bitboards, one for each piece type and color.
    uint64_t occupancies[3];   ///< White, Black, and Combined occupancies.
    
    int side_to_move;          ///< WHITE or BLACK.
    int ep_sq;                 ///< En passant square (0-63, or NO_SQ).
    int castling_rights;       ///< Castling rights bitmask.
    int halfmove_clock;        ///< 50-move rule counter.
    int fullmove_number;       ///< Current fullmove number.
    uint64_t hash_key;         ///< 64-bit Zobrist position hash.

    std::vector<UndoInfo> history; ///< Stack of undo information for past moves.

    // Zobrist hash keys (initialized on startup)
    static uint64_t zobrist_pieces[12][64];
    static uint64_t zobrist_side;
    static uint64_t zobrist_castling[16];
    static uint64_t zobrist_ep[65]; // 0-63, plus 64 (NO_SQ)

    Board();

    /**
     * @brief Initializes static Zobrist hashing keys.
     */
    static void init_zobrist();

    /**
     * @brief Resets the board to empty state.
     */
    void clear();

    /**
     * @brief Sets up the board based on a FEN string.
     * @param fen FEN representation of a chess position.
     * @return true if FEN parsed successfully, false otherwise.
     */
    bool parse_fen(const std::string& fen);

    /**
     * @brief Generates FEN string for the current board state.
     * @return std::string FEN representation.
     */
    std::string generate_fen() const;

    /**
     * @brief Displays the board in a user-friendly console format.
     */
    void print() const;

    /**
     * @brief Returns which piece is on a given square.
     * @param sq Square index (0-63).
     * @return Piece enum or EMPTY_PIECE.
     */
    int get_piece_on_square(int sq) const;

    /**
     * @brief Recalculates color and combined occupancy bitboards.
     */
    void update_occupancies();

    /**
     * @brief Generates the 64-bit Zobrist hash key from scratch.
     * @return uint64_t Zobrist hash key.
     */
    uint64_t generate_hash() const;

    /**
     * @brief Executes a move, updating bitboards and state.
     * @param m Move to execute.
     */
    void make_move(const Move& m);

    /**
     * @brief Reverts a move, restoring previous board state.
     * @param m Move to revert.
     */
    void unmake_move(const Move& m);

    /**
     * @brief Passes the turn to the opponent (null move).
     */
    void make_null_move();

    /**
     * @brief Reverts a null move.
     */
    void unmake_null_move();

    /**
     * @brief Checks if a square is attacked by any piece of the specified color.
     * @param sq Square to check.
     * @param attacker_color Color of the attacking player.
     * @return true if square is attacked, false otherwise.
     */
    bool is_square_attacked(int sq, int attacker_color) const;
};

// Bitboard helpers
inline void set_bit(uint64_t& bb, int sq) { bb |= (1ULL << sq); }
inline bool get_bit(uint64_t bb, int sq) { return (bb & (1ULL << sq)) != 0; }
inline void clear_bit(uint64_t& bb, int sq) { bb &= ~(1ULL << sq); }
inline int count_bits(uint64_t bb) {
#ifdef _MSC_VER
    return (int)__popcnt64(bb);
#else
    return __builtin_popcountll(bb);
#endif
}
inline int get_lsb(uint64_t bb) {
#ifdef _MSC_VER
    unsigned long index;
    _BitScanForward64(&index, bb);
    return (int)index;
#else
    return __builtin_ctzll(bb);
#endif
}

// Print confirmation message at the end of the file.
// Confirmation: Board.h created successfully.
