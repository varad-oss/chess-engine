// Fundamental chess types shared by every module: colours, pieces, squares,
// moves and search values. Everything here is a plain value type.
#pragma once

#include <cstdint>
#include <string>

namespace chess {

using Bitboard = std::uint64_t;
using Key = std::uint64_t;

enum Color : int { WHITE, BLACK };
constexpr int COLOR_NB = 2;
constexpr Color operator~(Color c) { return Color(c ^ 1); }

enum PieceType : int { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING, NO_PIECE_TYPE };
constexpr int PIECE_TYPE_NB = 6;

enum Piece : int {
    W_PAWN, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
    B_PAWN, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING,
    NO_PIECE
};
constexpr int PIECE_NB = 12;

constexpr Piece make_piece(Color c, PieceType pt) { return Piece(c * 6 + pt); }
constexpr PieceType type_of(Piece p) { return p == NO_PIECE ? NO_PIECE_TYPE : PieceType(p % 6); }
constexpr Color color_of(Piece p) { return Color(p / 6); }  // undefined for NO_PIECE

// Little-endian rank-file mapping: A1 = 0, H1 = 7, A8 = 56, H8 = 63.
enum Square : int {
    A1, B1, C1, D1, E1, F1, G1, H1,
    A2, B2, C2, D2, E2, F2, G2, H2,
    A3, B3, C3, D3, E3, F3, G3, H3,
    A4, B4, C4, D4, E4, F4, G4, H4,
    A5, B5, C5, D5, E5, F5, G5, H5,
    A6, B6, C6, D6, E6, F6, G6, H6,
    A7, B7, C7, D7, E7, F7, G7, H7,
    A8, B8, C8, D8, E8, F8, G8, H8,
    NO_SQUARE
};
constexpr int SQUARE_NB = 64;

constexpr Square make_square(int file, int rank) { return Square(rank * 8 + file); }
constexpr int file_of(Square s) { return s & 7; }
constexpr int rank_of(Square s) { return s >> 3; }
constexpr Square flip_rank(Square s) { return Square(s ^ 56); }
constexpr Square relative_square(Color c, Square s) { return c == WHITE ? s : flip_rank(s); }
constexpr int relative_rank(Color c, int rank) { return c == WHITE ? rank : 7 - rank; }
constexpr int relative_rank(Color c, Square s) { return relative_rank(c, rank_of(s)); }
constexpr int pawn_push(Color c) { return c == WHITE ? 8 : -8; }
constexpr Square operator+(Square s, int d) { return Square(int(s) + d); }
constexpr Square operator-(Square s, int d) { return Square(int(s) - d); }

inline std::string square_to_string(Square s) {
    return {char('a' + file_of(s)), char('1' + rank_of(s))};
}

enum CastlingRight : int {
    NO_CASTLING = 0,
    WHITE_OO = 1,
    WHITE_OOO = 2,
    BLACK_OO = 4,
    BLACK_OOO = 8,
    ALL_CASTLING = 15
};

// 16-bit move: bits 0-5 from, 6-11 to, 12-15 flag. The flag layout follows the
// common "from-to-flags" scheme: bit 2 = capture, bit 3 = promotion and the
// low two bits select the promotion piece (N, B, R, Q).
enum MoveFlag : int {
    QUIET = 0,
    DOUBLE_PUSH = 1,
    KING_CASTLE = 2,
    QUEEN_CASTLE = 3,
    CAPTURE = 4,
    EP_CAPTURE = 5,
    PROMO_N = 8, PROMO_B = 9, PROMO_R = 10, PROMO_Q = 11,
    PROMO_CAPTURE_N = 12, PROMO_CAPTURE_B = 13, PROMO_CAPTURE_R = 14, PROMO_CAPTURE_Q = 15
};

class Move {
public:
    constexpr Move() = default;
    constexpr Move(Square from, Square to, int flag = QUIET)
        : data_(std::uint16_t(int(from) | (int(to) << 6) | (flag << 12))) {}

    static constexpr Move none() { return Move(); }
    // A null move ("pass") is encoded as the otherwise impossible b1b1.
    static constexpr Move null() { return from_raw(65); }
    static constexpr Move from_raw(std::uint16_t raw) {
        Move m;
        m.data_ = raw;
        return m;
    }

    constexpr Square from() const { return Square(data_ & 63); }
    constexpr Square to() const { return Square((data_ >> 6) & 63); }
    constexpr int flag() const { return data_ >> 12; }
    constexpr std::uint16_t raw() const { return data_; }

    constexpr bool is_ok() const { return from() != to(); }
    constexpr bool is_capture() const { return flag() & 4; }
    constexpr bool is_promotion() const { return flag() & 8; }
    constexpr bool is_ep() const { return flag() == EP_CAPTURE; }
    constexpr bool is_castle() const { return flag() == KING_CASTLE || flag() == QUEEN_CASTLE; }
    constexpr PieceType promotion_type() const { return PieceType(KNIGHT + (flag() & 3)); }
    // "Noisy" moves change material: captures and queen promotions. This is
    // the partition used by move generation (Noisy vs Quiet) and the search.
    constexpr bool is_noisy() const {
        return is_capture() || (is_promotion() && promotion_type() == QUEEN);
    }

    constexpr bool operator==(const Move&) const = default;

    // Long algebraic (UCI) notation, e.g. "e2e4", "e7e8q", "0000" for none.
    std::string uci() const {
        if (!is_ok()) return "0000";
        std::string s = square_to_string(from()) + square_to_string(to());
        if (is_promotion()) s += "nbrq"[flag() & 3];
        return s;
    }

private:
    std::uint16_t data_ = 0;
};

// Search values (centipawns, side-to-move relative).
using Value = int;
constexpr int MAX_PLY = 128;
constexpr int MAX_MOVES = 256;  // the maximum number of legal moves in any position is 218
constexpr Value VALUE_DRAW = 0;
constexpr Value VALUE_MATE = 32000;
constexpr Value VALUE_INFINITE = 32001;
constexpr Value VALUE_NONE = 32002;
constexpr Value VALUE_MATE_IN_MAX_PLY = VALUE_MATE - MAX_PLY;

constexpr Value mate_in(int ply) { return VALUE_MATE - ply; }
constexpr Value mated_in(int ply) { return -VALUE_MATE + ply; }
constexpr bool is_mate_score(Value v) {
    return v != VALUE_NONE && (v >= VALUE_MATE_IN_MAX_PLY || v <= -VALUE_MATE_IN_MAX_PLY);
}

}  // namespace chess
