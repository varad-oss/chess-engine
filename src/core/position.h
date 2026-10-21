// Board state: piece bitboards plus a mailbox, incremental Zobrist hashing,
// and a stack of irreversible state so that moves can be unmade exactly.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "bitboard.h"
#include "types.h"

namespace chess {

// One-time initialisation of attack tables and Zobrist keys. Thread-safe.
void init();

namespace zobrist {
extern Key psq[PIECE_NB][SQUARE_NB];
extern Key side;
extern Key castling[16];
extern Key ep_file[8];
}  // namespace zobrist

// Everything that cannot be recomputed when a move is unmade.
struct StateInfo {
    Key key = 0;
    Bitboard checkers = 0;      // enemy pieces giving check to the side to move
    int castling = NO_CASTLING;
    Square ep = NO_SQUARE;      // only set when an en-passant capture is pseudo-legal
    int rule50 = 0;             // plies since the last capture or pawn move
    int plies_from_null = 0;    // plies since the last null move (bounds repetition scans)
    Piece captured = NO_PIECE;
};

class Position {
public:
    static constexpr std::string_view START_FEN =
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    Position();  // the standard starting position

    // Parses a FEN. The halfmove and fullmove fields are optional. On failure
    // the position is left unchanged and, if provided, *error says why.
    bool set_fen(std::string_view fen, std::string* error = nullptr);
    std::string fen() const;
    std::string pretty() const;

    // Board queries
    Piece piece_on(Square s) const { return board_[s]; }
    Bitboard pieces() const { return by_color_[WHITE] | by_color_[BLACK]; }
    Bitboard pieces(Color c) const { return by_color_[c]; }
    Bitboard pieces(PieceType pt) const { return by_type_[pt]; }
    Bitboard pieces(PieceType a, PieceType b) const { return by_type_[a] | by_type_[b]; }
    Bitboard pieces(Color c, PieceType pt) const { return by_color_[c] & by_type_[pt]; }
    Bitboard pieces(Color c, PieceType a, PieceType b) const { return by_color_[c] & pieces(a, b); }
    Square king_square(Color c) const { return lsb(pieces(c, KING)); }
    int count(Color c, PieceType pt) const { return popcount(pieces(c, pt)); }
    bool has_non_pawn_material(Color c) const {
        return (pieces(c) & ~pieces(PAWN, KING)) != 0;
    }

    // State queries
    Color side_to_move() const { return stm_; }
    Square ep_square() const { return st().ep; }
    int castling_rights() const { return st().castling; }
    int rule50() const { return st().rule50; }
    int game_ply() const { return game_ply_; }
    Key key() const { return st().key; }
    Bitboard checkers() const { return st().checkers; }
    bool in_check() const { return st().checkers != 0; }
    Piece captured_piece() const { return st().captured; }

    // All pieces (both colours) attacking s, given an occupancy.
    Bitboard attackers_to(Square s, Bitboard occupied) const;
    Bitboard attackers_to(Square s) const { return attackers_to(s, pieces()); }
    // Pieces of either colour that are the only blocker between c's king and
    // an enemy slider; those of colour c are pinned.
    Bitboard king_blockers(Color c) const;

    // Whether the (legal) move m checks the opponent, including discovered
    // checks, checks by the castling rook and en-passant line openings.
    bool gives_check(Move m) const;

    // Make/unmake. The move must be legal (as produced by generate()).
    void make_move(Move m);
    void unmake_move(Move m);
    void make_null_move();
    void unmake_null_move();

    // Draw detection. `ply` is the distance from the search root: a position
    // repeated once inside the search tree is scored as a draw, while a
    // repetition of game history before the root needs to be a true threefold.
    bool is_repetition(int ply) const;
    bool is_fifty_move_draw() const;  // the caller must rule out checkmate first
    bool is_insufficient_material() const;
    bool is_draw(int ply) const {
        return is_fifty_move_draw() || is_insufficient_material() || is_repetition(ply);
    }

    // Static exchange evaluation: material balance (centipawns) of the capture
    // sequence on m.to(), assuming both sides recapture with their least
    // valuable attacker and may stop at any point. Pins are ignored.
    int see(Move m) const;
    bool see_ge(Move m, int threshold) const { return see(m) >= threshold; }

    // Parses a UCI move string, returning Move::none() unless it is legal here.
    Move parse_uci_move(std::string_view text) const;

    // Debug support: hash recomputed from scratch, and a full consistency
    // check of the redundant board representations.
    Key compute_key() const;
    bool is_consistent(std::string* why = nullptr) const;

private:
    struct EmptyTag {};
    explicit Position(EmptyTag) { clear(); }

    const StateInfo& st() const { return states_.back(); }

    void clear();
    void put_piece(Piece p, Square s);
    void remove_piece(Square s);
    void move_piece(Square from, Square to);
    bool ep_capture_possible(Square ep, Color capturer) const;
    Bitboard compute_checkers() const;

    Bitboard by_type_[PIECE_TYPE_NB] = {};
    Bitboard by_color_[COLOR_NB] = {};
    Piece board_[SQUARE_NB];
    Color stm_ = WHITE;
    int game_ply_ = 0;
    std::vector<StateInfo> states_;  // states_.back() describes the current position
};

}  // namespace chess
