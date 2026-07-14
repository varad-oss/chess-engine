/**
 * @file Board.cpp
 * @brief Board representation and game state manipulation logic.
 */

#include "Board.h"
#include "MoveGen.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>

// Define static members of Board
uint64_t Board::zobrist_pieces[12][64];
uint64_t Board::zobrist_side;
uint64_t Board::zobrist_castling[16];
uint64_t Board::zobrist_ep[65];

// Castling rights update masks for each square
const int castling_rights_mask[64] = {
    13, 15, 15, 15, 12, 15, 15, 14, // A1-H1
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
     7, 15, 15, 15,  3, 15, 15, 11  // A8-H8
};

// Deterministic PRNG using Xorshift64 for Zobrist keys
static uint64_t random_state = 1804289383ULL;
static uint64_t get_random_u64() {
    uint64_t x = random_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    random_state = x;
    return x;
}

Board::Board() {
    clear();
    // Initialize Zobrist keys once
    static bool keys_initialized = false;
    if (!keys_initialized) {
        init_zobrist();
        keys_initialized = true;
    }
    hash_key = generate_hash();
}

void Board::init_zobrist() {
    // Generate piece keys
    for (int p = 0; p < 12; p++) {
        for (int sq = 0; sq < 64; sq++) {
            zobrist_pieces[p][sq] = get_random_u64();
        }
    }
    
    // Generate side key
    zobrist_side = get_random_u64();
    
    // Generate castling keys
    for (int i = 0; i < 16; i++) {
        zobrist_castling[i] = get_random_u64();
    }
    
    // Generate en passant keys
    for (int sq = 0; sq < 65; sq++) {
        zobrist_ep[sq] = get_random_u64();
    }
}

void Board::clear() {
    for (int p = 0; p < 12; p++) {
        bitboards[p] = 0ULL;
    }
    for (int c = 0; c < 3; c++) {
        occupancies[c] = 0ULL;
    }
    side_to_move = WHITE;
    ep_sq = NO_SQ;
    castling_rights = 0;
    halfmove_clock = 0;
    fullmove_number = 1;
    hash_key = 0ULL;
    history.clear();
}

int Board::get_piece_on_square(int sq) const {
    for (int p = 0; p < 12; p++) {
        if (get_bit(bitboards[p], sq)) {
            return p;
        }
    }
    return EMPTY_PIECE;
}

void Board::update_occupancies() {
    occupancies[WHITE] = 0ULL;
    occupancies[BLACK] = 0ULL;
    
    // Aggregate white pieces
    for (int p = W_PAWN; p <= W_KING; p++) {
        occupancies[WHITE] |= bitboards[p];
    }
    // Aggregate black pieces
    for (int p = B_PAWN; p <= B_KING; p++) {
        occupancies[BLACK] |= bitboards[p];
    }
    
    // Combine occupancies
    occupancies[BOTH] = occupancies[WHITE] | occupancies[BLACK];
}

uint64_t Board::generate_hash() const {
    uint64_t hash = 0ULL;
    
    // XOR pieces on squares
    for (int p = 0; p < 12; p++) {
        uint64_t bb = bitboards[p];
        while (bb) {
            int sq = get_lsb(bb);
            hash ^= zobrist_pieces[p][sq];
            clear_bit(bb, sq);
        }
    }
    
    // XOR side to move
    if (side_to_move == BLACK) {
        hash ^= zobrist_side;
    }
    
    // XOR castling rights
    hash ^= zobrist_castling[castling_rights];
    
    // XOR en passant square (if valid)
    hash ^= zobrist_ep[ep_sq];
    
    return hash;
}

bool Board::parse_fen(const std::string& fen) {
    clear();
    std::stringstream ss(fen);
    std::string piece_placement, active_color, castling, en_passant, halfmove, fullmove;
    
    ss >> piece_placement >> active_color >> castling >> en_passant >> halfmove >> fullmove;
    
    if (piece_placement.empty()) return false;
    
    // 1. Parse pieces rank by rank from 8 down to 1
    int rank = 7;
    int file = 0;
    for (char c : piece_placement) {
        if (c == '/') {
            rank--;
            file = 0;
        } else if (c >= '1' && c <= '8') {
            file += (c - '0');
        } else {
            int sq = rank * 8 + file;
            int piece = EMPTY_PIECE;
            switch (c) {
                case 'P': piece = W_PAWN; break;
                case 'N': piece = W_KNIGHT; break;
                case 'B': piece = W_BISHOP; break;
                case 'R': piece = W_ROOK; break;
                case 'Q': piece = W_QUEEN; break;
                case 'K': piece = W_KING; break;
                case 'p': piece = B_PAWN; break;
                case 'n': piece = B_KNIGHT; break;
                case 'b': piece = B_BISHOP; break;
                case 'r': piece = B_ROOK; break;
                case 'q': piece = B_QUEEN; break;
                case 'k': piece = B_KING; break;
                default: return false; // invalid piece character
            }
            if (piece != EMPTY_PIECE) {
                set_bit(bitboards[piece], sq);
            }
            file++;
        }
    }
    
    // 2. Parse active color
    if (active_color == "w") {
        side_to_move = WHITE;
    } else if (active_color == "b") {
        side_to_move = BLACK;
    } else {
        return false;
    }
    
    // 3. Parse castling rights
    castling_rights = 0;
    if (castling != "-") {
        for (char c : castling) {
            if (c == 'K') castling_rights |= WK_CASTLE;
            else if (c == 'Q') castling_rights |= WQ_CASTLE;
            else if (c == 'k') castling_rights |= BK_CASTLE;
            else if (c == 'q') castling_rights |= BQ_CASTLE;
        }
    }
    
    // 4. Parse en passant square
    ep_sq = NO_SQ;
    if (en_passant != "-") {
        int ep_file = en_passant[0] - 'a';
        int ep_rank = en_passant[1] - '1';
        ep_sq = ep_rank * 8 + ep_file;
    }
    
    // 5. Parse move clocks
    halfmove_clock = halfmove.empty() ? 0 : std::stoi(halfmove);
    fullmove_number = fullmove.empty() ? 1 : std::stoi(fullmove);
    
    update_occupancies();
    hash_key = generate_hash();
    
    return true;
}

std::string Board::generate_fen() const {
    std::stringstream fen;
    
    // 1. Piece placement
    for (int rank = 7; rank >= 0; rank--) {
        int empty_count = 0;
        for (int file = 0; file < 8; file++) {
            int sq = rank * 8 + file;
            int piece = get_piece_on_square(sq);
            if (piece == EMPTY_PIECE) {
                empty_count++;
            } else {
                if (empty_count > 0) {
                    fen << empty_count;
                    empty_count = 0;
                }
                char c = ' ';
                switch (piece) {
                    case W_PAWN:   c = 'P'; break;
                    case W_KNIGHT: c = 'N'; break;
                    case W_BISHOP: c = 'B'; break;
                    case W_ROOK:   c = 'R'; break;
                    case W_QUEEN:  c = 'Q'; break;
                    case W_KING:   c = 'K'; break;
                    case B_PAWN:   c = 'p'; break;
                    case B_KNIGHT: c = 'n'; break;
                    case B_BISHOP: c = 'b'; break;
                    case B_ROOK:   c = 'r'; break;
                    case B_QUEEN:  c = 'q'; break;
                    case B_KING:   c = 'k'; break;
                }
                fen << c;
            }
        }
        if (empty_count > 0) {
            fen << empty_count;
        }
        if (rank > 0) {
            fen << "/";
        }
    }
    
    // 2. Active color
    fen << " " << (side_to_move == WHITE ? "w" : "b");
    
    // 3. Castling rights
    fen << " ";
    if (castling_rights == 0) {
        fen << "-";
    } else {
        if (castling_rights & WK_CASTLE) fen << "K";
        if (castling_rights & WQ_CASTLE) fen << "Q";
        if (castling_rights & BK_CASTLE) fen << "k";
        if (castling_rights & BQ_CASTLE) fen << "q";
    }
    
    // 4. En passant square
    fen << " ";
    if (ep_sq == NO_SQ) {
        fen << "-";
    } else {
        char file = 'a' + (ep_sq % 8);
        char rank = '1' + (ep_sq / 8);
        fen << file << rank;
    }
    
    // 5. Halfmove & Fullmove
    fen << " " << halfmove_clock << " " << fullmove_number;
    
    return fen.str();
}

void Board::print() const {
    std::cout << "\n  +---+---+---+---+---+---+---+---+\n";
    for (int rank = 7; rank >= 0; rank--) {
        std::cout << (rank + 1) << " |";
        for (int file = 0; file < 8; file++) {
            int sq = rank * 8 + file;
            int piece = get_piece_on_square(sq);
            char sym = '.';
            switch (piece) {
                case W_PAWN:   sym = 'P'; break;
                case W_KNIGHT: sym = 'N'; break;
                case W_BISHOP: sym = 'B'; break;
                case W_ROOK:   sym = 'R'; break;
                case W_QUEEN:  sym = 'Q'; break;
                case W_KING:   sym = 'K'; break;
                case B_PAWN:   sym = 'p'; break;
                case B_KNIGHT: sym = 'n'; break;
                case B_BISHOP: sym = 'b'; break;
                case B_ROOK:   sym = 'r'; break;
                case B_QUEEN:  sym = 'q'; break;
                case B_KING:   sym = 'k'; break;
            }
            std::cout << " " << sym << " |";
        }
        std::cout << "\n  +---+---+---+---+---+---+---+---+\n";
    }
    std::cout << "    a   b   c   d   e   f   g   h\n\n";
    std::cout << "  Side to move:    " << (side_to_move == WHITE ? "White" : "Black") << "\n";
    std::cout << "  Castling rights: ";
    if (castling_rights & WK_CASTLE) std::cout << "K";
    if (castling_rights & WQ_CASTLE) std::cout << "Q";
    if (castling_rights & BK_CASTLE) std::cout << "k";
    if (castling_rights & BQ_CASTLE) std::cout << "q";
    if (castling_rights == 0) std::cout << "-";
    std::cout << "\n";
    
    std::cout << "  En passant square: ";
    if (ep_sq == NO_SQ) std::cout << "-";
    else std::cout << (char)('a' + (ep_sq % 8)) << (ep_sq / 8 + 1);
    std::cout << "\n";
    
    std::cout << "  Halfmove clock:  " << halfmove_clock << "\n";
    std::cout << "  Fullmove number: " << fullmove_number << "\n";
    std::cout << "  Zobrist Hash:    0x" << std::hex << std::setw(16) << std::setfill('0') << hash_key << std::dec << "\n\n";
}

void Board::make_move(const Move& m) {
    // 1. Push current state into history
    UndoInfo undo;
    undo.ep_sq = ep_sq;
    undo.castling_rights = castling_rights;
    undo.halfmove_clock = halfmove_clock;
    undo.hash_key = hash_key;
    
    // 2. Identify the piece moving
    int us = side_to_move;
    int opponent = us ^ 1;
    int p_type = get_piece_on_square(m.from);
    
    // Default captured piece is EMPTY_PIECE
    undo.captured_piece = EMPTY_PIECE;
    
    // Remove old hash details
    hash_key ^= zobrist_pieces[p_type][m.from];
    if (ep_sq != NO_SQ) {
        hash_key ^= zobrist_ep[ep_sq];
    }
    hash_key ^= zobrist_castling[castling_rights];

    // Clear source bit, set target bit for the moving piece
    clear_bit(bitboards[p_type], m.from);
    set_bit(bitboards[p_type], m.to);
    hash_key ^= zobrist_pieces[p_type][m.to];

    // 3. Handle captures
    if (m.is_capture()) {
        if (m.is_enpassant()) {
            int ep_pawn_sq = (us == WHITE) ? (m.to - 8) : (m.to + 8);
            int ep_pawn_type = (us == WHITE) ? B_PAWN : W_PAWN;
            clear_bit(bitboards[ep_pawn_type], ep_pawn_sq);
            hash_key ^= zobrist_pieces[ep_pawn_type][ep_pawn_sq];
            undo.captured_piece = ep_pawn_type;
        } else {
            // Find captured piece on target square
            for (int p = opponent * 6; p <= opponent * 6 + 5; p++) {
                if (get_bit(bitboards[p], m.to)) {
                    clear_bit(bitboards[p], m.to);
                    hash_key ^= zobrist_pieces[p][m.to]; // remove from hash (we already added moving piece, so this clears the old occupant)
                    undo.captured_piece = p;
                    break;
                }
            }
        }
        halfmove_clock = 0; // reset halfmove clock on capture
    } else if (p_type == W_PAWN || p_type == B_PAWN) {
        halfmove_clock = 0; // reset on pawn move
    } else {
        halfmove_clock++;
    }

    // 4. Handle promotions
    if (m.is_promotion()) {
        // Clear pawn on target
        clear_bit(bitboards[p_type], m.to);
        hash_key ^= zobrist_pieces[p_type][m.to];
        
        // Add promotion piece on target
        int promo_p = us * 6 + m.promo; // promo corresponds to PieceTypes
        set_bit(bitboards[promo_p], m.to);
        hash_key ^= zobrist_pieces[promo_p][m.to];
    }

    // 5. Handle castling
    if (m.is_castle()) {
        switch (m.to) {
            case G1: // White King-side
                clear_bit(bitboards[W_ROOK], H1);
                set_bit(bitboards[W_ROOK], F1);
                hash_key ^= zobrist_pieces[W_ROOK][H1] ^ zobrist_pieces[W_ROOK][F1];
                break;
            case C1: // White Queen-side
                clear_bit(bitboards[W_ROOK], A1);
                set_bit(bitboards[W_ROOK], D1);
                hash_key ^= zobrist_pieces[W_ROOK][A1] ^ zobrist_pieces[W_ROOK][D1];
                break;
            case G8: // Black King-side
                clear_bit(bitboards[B_ROOK], H8);
                set_bit(bitboards[B_ROOK], F8);
                hash_key ^= zobrist_pieces[B_ROOK][H8] ^ zobrist_pieces[B_ROOK][F8];
                break;
            case C8: // Black Queen-side
                clear_bit(bitboards[B_ROOK], A8);
                set_bit(bitboards[B_ROOK], D8);
                hash_key ^= zobrist_pieces[B_ROOK][A8] ^ zobrist_pieces[B_ROOK][D8];
                break;
        }
    }

    // 6. Update castling rights
    castling_rights &= castling_rights_mask[m.from];
    castling_rights &= castling_rights_mask[m.to];
    hash_key ^= zobrist_castling[castling_rights];

    // 7. Update en passant square
    if (m.is_double_push()) {
        ep_sq = (m.from + m.to) / 2;
        hash_key ^= zobrist_ep[ep_sq];
    } else {
        ep_sq = NO_SQ;
    }

    // 8. Swap side to move
    side_to_move = opponent;
    hash_key ^= zobrist_side;

    // 9. Increment fullmove number if Black finished moving
    if (us == BLACK) {
        fullmove_number++;
    }

    history.push_back(undo);
    update_occupancies();
}

void Board::unmake_move(const Move& m) {
    if (history.empty()) return;
    
    // Retrieve previous state
    UndoInfo undo = history.back();
    history.pop_back();

    int us = side_to_move ^ 1; // Side that made the move we are unmaking
    int p_type = get_piece_on_square(m.to); // Get piece that ended up on target

    // If it was promotion, we need to revert promotion piece back to pawn
    if (m.is_promotion()) {
        clear_bit(bitboards[p_type], m.to);
        p_type = us * 6 + PAWN;
        set_bit(bitboards[p_type], m.to);
    }

    // Move the piece back from target to source
    clear_bit(bitboards[p_type], m.to);
    set_bit(bitboards[p_type], m.from);

    // Revert captures
    if (m.is_capture()) {
        if (m.is_enpassant()) {
            int ep_pawn_sq = (us == WHITE) ? (m.to - 8) : (m.to + 8);
            set_bit(bitboards[undo.captured_piece], ep_pawn_sq);
        } else {
            set_bit(bitboards[undo.captured_piece], m.to);
        }
    }

    // Revert castling
    if (m.is_castle()) {
        switch (m.to) {
            case G1: // White King-side
                clear_bit(bitboards[W_ROOK], F1);
                set_bit(bitboards[W_ROOK], H1);
                break;
            case C1: // White Queen-side
                clear_bit(bitboards[W_ROOK], D1);
                set_bit(bitboards[W_ROOK], A1);
                break;
            case G8: // Black King-side
                clear_bit(bitboards[B_ROOK], F8);
                set_bit(bitboards[B_ROOK], H8);
                break;
            case C8: // Black Queen-side
                clear_bit(bitboards[B_ROOK], D8);
                set_bit(bitboards[B_ROOK], A8);
                break;
        }
    }

    // Restore board state variables
    side_to_move = us;
    ep_sq = undo.ep_sq;
    castling_rights = undo.castling_rights;
    halfmove_clock = undo.halfmove_clock;
    hash_key = undo.hash_key;

    if (side_to_move == BLACK) {
        fullmove_number--;
    }

    update_occupancies();
}

void Board::make_null_move() {
    UndoInfo undo;
    undo.ep_sq = ep_sq;
    undo.castling_rights = castling_rights;
    undo.halfmove_clock = halfmove_clock;
    undo.captured_piece = EMPTY_PIECE;
    undo.hash_key = hash_key;
    history.push_back(undo);

    // Toggle side to move
    hash_key ^= zobrist_side;
    side_to_move ^= 1;

    // Clear en passant
    if (ep_sq != NO_SQ) {
        hash_key ^= zobrist_ep[ep_sq];
        ep_sq = NO_SQ;
    }

    halfmove_clock++;
}

void Board::unmake_null_move() {
    UndoInfo undo = history.back();
    history.pop_back();

    side_to_move ^= 1;
    ep_sq = undo.ep_sq;
    castling_rights = undo.castling_rights;
    halfmove_clock = undo.halfmove_clock;
    hash_key = undo.hash_key;
}

bool Board::is_square_attacked(int sq, int attacker_color) const {
    // 1. Pawn attacks
    if (attacker_color == WHITE) {
        if (sq > 7) {
            if (sq % 8 != 0 && get_bit(bitboards[W_PAWN], sq - 9)) return true;
            if (sq % 8 != 7 && get_bit(bitboards[W_PAWN], sq - 7)) return true;
        }
    } else {
        if (sq < 56) {
            if (sq % 8 != 7 && get_bit(bitboards[B_PAWN], sq + 9)) return true;
            if (sq % 8 != 0 && get_bit(bitboards[B_PAWN], sq + 7)) return true;
        }
    }

    // 2. Knight attacks
    uint64_t knights = bitboards[attacker_color == WHITE ? W_KNIGHT : B_KNIGHT];
    if (MoveGen::knight_attacks[sq] & knights) return true;

    // 3. King attacks
    uint64_t king = bitboards[attacker_color == WHITE ? W_KING : B_KING];
    if (MoveGen::king_attacks[sq] & king) return true;

    // 4. Bishop/Queen attacks
    uint64_t bishops_queens = bitboards[attacker_color == WHITE ? W_BISHOP : B_BISHOP] | 
                              bitboards[attacker_color == WHITE ? W_QUEEN : B_QUEEN];
    if (MoveGen::get_bishop_attacks(sq, occupancies[BOTH]) & bishops_queens) return true;

    // 5. Rook/Queen attacks
    uint64_t rooks_queens = bitboards[attacker_color == WHITE ? W_ROOK : B_ROOK] | 
                            bitboards[attacker_color == WHITE ? W_QUEEN : B_QUEEN];
    if (MoveGen::get_rook_attacks(sq, occupancies[BOTH]) & rooks_queens) return true;

    return false;
}

// Print confirmation message at the end of the file.
// Confirmation: Board.cpp created successfully.
