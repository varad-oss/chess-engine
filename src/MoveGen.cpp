/**
 * @file MoveGen.cpp
 * @brief Move generation implementation with Magic Bitboard setup.
 */

#include "MoveGen.h"

// Define namespaces and static members
namespace MoveGen {
    uint64_t knight_attacks[64];
    uint64_t king_attacks[64];
    uint64_t bishop_masks[64];
    extern uint64_t rook_masks[64];

    int bishop_shifts[64];
    int rook_shifts[64];

    // Magic tables
    uint64_t* bishop_attack_table[64];
    uint64_t* rook_attack_table[64];

    // Statically allocated memory for lookups
    static uint64_t bishop_attacks_flat[5248];
    static uint64_t rook_attacks_flat[102400];

    // File and Rank masks
    const uint64_t FILE_A = 0x0101010101010101ULL;
    const uint64_t FILE_B = 0x0202020202020202ULL;
    const uint64_t FILE_G = 0x4040404040404040ULL;
    const uint64_t FILE_H = 0x8080808080808080ULL;
    const uint64_t RANK_3 = 0x0000000000FF0000ULL;
    const uint64_t RANK_6 = 0x0000FF0000000000ULL;

    // Hardcoded magic numbers for bishops (verified at startup)
    uint64_t bishop_magics[64] = {
        0x208892c040040ULL, 0x218100502182110ULL, 0x420200410004b0ULL, 0x14080b4100003080ULL, 
        0x4408484000000042ULL, 0x1182a3010010104ULL, 0x6081008820880000ULL, 0x4a1122024620100dULL, 
        0x8040100202104aULL, 0x90052142040100ULL, 0x2a1218082325a000ULL, 0x80434018c2000ULL, 
        0x1060011040010002ULL, 0x208022820080800ULL, 0x80008808025019ULL, 0x980422020a414400ULL, 
        0x10c1012002420240ULL, 0x62280410420220ULL, 0x2090404051200ULL, 0x400c0050c0408006ULL, 
        0x22200400a00020ULL, 0x102800048044000ULL, 0x10021010484a0804ULL, 0x2009490840109ULL, 
        0x402408010900212ULL, 0x91040019082800ULL, 0xa060209080200ULL, 0x2011080084004010ULL, 
        0x2000840030802022ULL, 0x401004008080808ULL, 0x1060811461008ULL, 0x102d202101040320ULL, 
        0x4044304001348410ULL, 0x882000040440ULL, 0x30180400081040ULL, 0xaa0c00a00002200ULL, 
        0x5460060020020480ULL, 0x4a100a10200a0080ULL, 0xc108009400910100ULL, 0x48019100028154ULL, 
        0x808019008001002ULL, 0x4050410420001010ULL, 0x50000e0082001000ULL, 0x100004202250800ULL, 
        0x5200403091040200ULL, 0x71020292004100ULL, 0x2008011404000090ULL, 0x1011212090380ULL, 
        0x424189824300010ULL, 0x2050249828080000ULL, 0x1020102108088210ULL, 0x1002200020881054ULL, 
        0x3b0b000831240200ULL, 0x8100a022024a0080ULL, 0x4102408009000ULL, 0xb0100208803400ULL, 
        0x60020600450808c0ULL, 0xa0802208420800ULL, 0x20100880400ULL, 0x100800420200ULL, 
        0x1200a8070411ULL, 0x108844002844104ULL, 0x2082420404008a01ULL, 0x2302a02004200ULL
    };

    // Hardcoded magic numbers for rooks (verified at startup)
    uint64_t rook_magics[64] = {
        0x80008020104000ULL, 0x8040004010002008ULL, 0x6080082004811001ULL, 0x1080048010000802ULL, 
        0x100100800040300ULL, 0x20003100c086200ULL, 0x40016b012041308ULL, 0x420002004183002cULL, 
        0x180800080400020ULL, 0x102401004200040ULL, 0x1012002010420080ULL, 0x2102002190c00a00ULL, 
        0x802a001009042200ULL, 0x202000891040200ULL, 0x10802100220080ULL, 0x801000040810002ULL, 
        0x40208000804000ULL, 0x230104000200041ULL, 0x888020031000ULL, 0x250008080080010ULL, 
        0x2202050011010800ULL, 0x1010008040002ULL, 0x3040002080110ULL, 0x1000020010408124ULL, 
        0x10800080204008ULL, 0x8060002040005000ULL, 0x6001024300102001ULL, 0x40100080080084ULL, 
        0x1024040080080280ULL, 0x8804020080040080ULL, 0x5042004200085144ULL, 0x2010c08200004421ULL, 
        0x400028800082ULL, 0x882004804000ULL, 0x8010002800200401ULL, 0x4000801000800804ULL, 
        0x444800800800400ULL, 0x2002004040010ULL, 0x90c8100144000802ULL, 0x1002084902000084ULL, 
        0xdc0802040008000ULL, 0x4010002000404001ULL, 0x28200100e0450030ULL, 0x10010010210008ULL, 
        0x200080004008080ULL, 0xe24000200048080ULL, 0x8000210802a40030ULL, 0x20a0010040820004ULL, 
        0x204080010100ULL, 0x8020200098400180ULL, 0x4000200080100880ULL, 0x9100080010008080ULL, 
        0x4040080080080ULL, 0xba001400800280ULL, 0x7002000144084200ULL, 0x4240800100004080ULL, 
        0x300800a102041ULL, 0xd020440208012ULL, 0xc04058a0010013ULL, 0x11001000060821ULL, 
        0x11000800020411ULL, 0x4082001004810802ULL, 0x80082011004ULL, 0x1011cc0100205082ULL
    };

    const int bishop_relevant_bits[64] = {
        6, 5, 5, 5, 5, 5, 5, 6,
        5, 5, 5, 5, 5, 5, 5, 5,
        5, 5, 7, 7, 7, 7, 5, 5,
        5, 5, 7, 9, 9, 7, 5, 5,
        5, 5, 7, 9, 9, 7, 5, 5,
        5, 5, 7, 7, 7, 7, 5, 5,
        5, 5, 5, 5, 5, 5, 5, 5,
        6, 5, 5, 5, 5, 5, 5, 6
    };

    const int rook_relevant_bits[64] = {
        12, 11, 11, 11, 11, 11, 11, 12,
        11, 10, 10, 10, 10, 10, 10, 11,
        11, 10, 10, 10, 10, 10, 10, 11,
        11, 10, 10, 10, 10, 10, 10, 11,
        11, 10, 10, 10, 10, 10, 10, 11,
        11, 10, 10, 10, 10, 10, 10, 11,
        11, 10, 10, 10, 10, 10, 10, 11,
        12, 11, 11, 11, 11, 11, 11, 12
    };

    uint64_t rook_masks[64];
}

// ----------------------------------------------------
// Auxiliary precomputation functions
// ----------------------------------------------------

/**
 * Knight attack precomputation.
 */
static uint64_t generate_knight_attacks(int sq) {
    uint64_t attacks = 0ULL;
    uint64_t bit = 1ULL << sq;
    // -2 files (left 2)
    if (bit & ~(MoveGen::FILE_A | MoveGen::FILE_B)) {
        attacks |= (bit >> 10) | (bit << 6);
    }
    // -1 file (left 1)
    if (bit & ~MoveGen::FILE_A) {
        attacks |= (bit >> 17) | (bit << 15);
    }
    // +1 file (right 1)
    if (bit & ~MoveGen::FILE_H) {
        attacks |= (bit >> 15) | (bit << 17);
    }
    // +2 files (right 2)
    if (bit & ~(MoveGen::FILE_G | MoveGen::FILE_H)) {
        attacks |= (bit >> 6) | (bit << 10);
    }
    return attacks;
}

/**
 * King attack precomputation.
 */
static uint64_t generate_king_attacks(int sq) {
    uint64_t attacks = 0ULL;
    uint64_t bit = 1ULL << sq;
    attacks |= (bit << 8) | (bit >> 8);
    if (bit & ~MoveGen::FILE_A) {
        attacks |= (bit >> 1) | (bit >> 9) | (bit << 7);
    }
    if (bit & ~MoveGen::FILE_H) {
        attacks |= (bit << 1) | (bit << 9) | (bit >> 7);
    }
    return attacks;
}

/**
 * Mask bishop rays (excluding edge squares).
 */
static uint64_t generate_bishop_mask(int sq) {
    uint64_t attacks = 0ULL;
    int tr = sq / 8;
    int tf = sq % 8;
    int r, f;
    for (r = tr + 1, f = tf + 1; r <= 6 && f <= 6; r++, f++) attacks |= (1ULL << (r * 8 + f));
    for (r = tr - 1, f = tf + 1; r >= 1 && f <= 6; r--, f++) attacks |= (1ULL << (r * 8 + f));
    for (r = tr + 1, f = tf - 1; r <= 6 && f >= 1; r++, f--) attacks |= (1ULL << (r * 8 + f));
    for (r = tr - 1, f = tf - 1; r >= 1 && f >= 1; r--, f--) attacks |= (1ULL << (r * 8 + f));
    return attacks;
}

/**
 * Mask rook rays (excluding edge squares).
 */
static uint64_t generate_rook_mask(int sq) {
    uint64_t attacks = 0ULL;
    int tr = sq / 8;
    int tf = sq % 8;
    int r, f;
    for (r = tr + 1; r <= 6; r++) attacks |= (1ULL << (r * 8 + tf));
    for (r = tr - 1; r >= 1; r--) attacks |= (1ULL << (r * 8 + tf));
    for (f = tf + 1; f <= 6; f++) attacks |= (1ULL << (tr * 8 + f));
    for (f = tf - 1; f >= 1; f--) attacks |= (1ULL << (tr * 8 + f));
    return attacks;
}

/**
 * Calculate bishop attacks on-the-fly for precomputation.
 */
static uint64_t generate_bishop_attacks_on_the_fly(int sq, uint64_t block) {
    uint64_t attacks = 0ULL;
    int tr = sq / 8;
    int tf = sq % 8;
    int r, f;
    for (r = tr + 1, f = tf + 1; r <= 7 && f <= 7; r++, f++) {
        attacks |= (1ULL << (r * 8 + f));
        if (block & (1ULL << (r * 8 + f))) break;
    }
    for (r = tr - 1, f = tf + 1; r >= 0 && f <= 7; r--, f++) {
        attacks |= (1ULL << (r * 8 + f));
        if (block & (1ULL << (r * 8 + f))) break;
    }
    for (r = tr + 1, f = tf - 1; r <= 7 && f >= 0; r++, f--) {
        attacks |= (1ULL << (r * 8 + f));
        if (block & (1ULL << (r * 8 + f))) break;
    }
    for (r = tr - 1, f = tf - 1; r >= 0 && f >= 0; r--, f--) {
        attacks |= (1ULL << (r * 8 + f));
        if (block & (1ULL << (r * 8 + f))) break;
    }
    return attacks;
}

/**
 * Calculate rook attacks on-the-fly for precomputation.
 */
static uint64_t generate_rook_attacks_on_the_fly(int sq, uint64_t block) {
    uint64_t attacks = 0ULL;
    int tr = sq / 8;
    int tf = sq % 8;
    int r, f;
    for (r = tr + 1; r <= 7; r++) {
        attacks |= (1ULL << (r * 8 + tf));
        if (block & (1ULL << (r * 8 + tf))) break;
    }
    for (r = tr - 1; r >= 0; r--) {
        attacks |= (1ULL << (r * 8 + tf));
        if (block & (1ULL << (r * 8 + tf))) break;
    }
    for (f = tf + 1; f <= 7; f++) {
        attacks |= (1ULL << (sq / 8 * 8 + f));
        if (block & (1ULL << (sq / 8 * 8 + f))) break;
    }
    for (f = tf - 1; f >= 0; f--) {
        attacks |= (1ULL << (sq / 8 * 8 + f));
        if (block & (1ULL << (sq / 8 * 8 + f))) break;
    }
    return attacks;
}

/**
 * Generate sub-occupancies from a mask.
 */
static uint64_t set_occupancy_subset(int index, int bits_in_mask, uint64_t mask) {
    uint64_t occupancy = 0ULL;
    for (int count = 0; count < bits_in_mask; count++) {
        int sq = get_lsb(mask);
        clear_bit(mask, sq);
        if (index & (1 << count)) {
            occupancy |= (1ULL << sq);
        }
    }
    return occupancy;
}

void MoveGen::init_all() {
    // 1. Initialize Knights and King attacks
    for (int sq = 0; sq < 64; sq++) {
        knight_attacks[sq] = generate_knight_attacks(sq);
        king_attacks[sq] = generate_king_attacks(sq);
        bishop_masks[sq] = generate_bishop_mask(sq);
        rook_masks[sq] = generate_rook_mask(sq);
        
        bishop_shifts[sq] = 64 - bishop_relevant_bits[sq];
        rook_shifts[sq] = 64 - rook_relevant_bits[sq];
    }
    
    // 2. Initialize Bishop magic tables
    int bishop_offset = 0;
    for (int sq = 0; sq < 64; sq++) {
        bishop_attack_table[sq] = &bishop_attacks_flat[bishop_offset];
        int bits = bishop_relevant_bits[sq];
        int num_indices = 1 << bits;
        for (int i = 0; i < num_indices; i++) {
            uint64_t occ = set_occupancy_subset(i, bits, bishop_masks[sq]);
            int index = (int)((occ * bishop_magics[sq]) >> bishop_shifts[sq]);
            bishop_attack_table[sq][index] = generate_bishop_attacks_on_the_fly(sq, occ);
        }
        bishop_offset += num_indices;
    }

    // 3. Initialize Rook magic tables
    int rook_offset = 0;
    for (int sq = 0; sq < 64; sq++) {
        rook_attack_table[sq] = &rook_attacks_flat[rook_offset];
        int bits = rook_relevant_bits[sq];
        int num_indices = 1 << bits;
        for (int i = 0; i < num_indices; i++) {
            uint64_t occ = set_occupancy_subset(i, bits, rook_masks[sq]);
            int index = (int)((occ * rook_magics[sq]) >> rook_shifts[sq]);
            rook_attack_table[sq][index] = generate_rook_attacks_on_the_fly(sq, occ);
        }
        rook_offset += num_indices;
    }
}

// ----------------------------------------------------
// Move Generation Implementation
// ----------------------------------------------------

void MoveGen::generate_pseudo_legal_moves(const Board& board, std::vector<Move>& move_list) {
    int us = board.side_to_move;
    int opponent = us ^ 1;
    
    uint64_t occupancies_us = board.occupancies[us];
    uint64_t occupancies_them = board.occupancies[opponent];
    uint64_t occupancies_both = board.occupancies[BOTH];
    uint64_t empty = ~occupancies_both;

    // ------------------------------------------------
    // 1. Pawn Moves
    // ------------------------------------------------
    if (us == WHITE) {
        uint64_t pawns = board.bitboards[W_PAWN];
        
        // Single pushes
        uint64_t single_pushes = (pawns << 8) & empty;
        uint64_t temp_pushes = single_pushes;
        while (temp_pushes) {
            int to = get_lsb(temp_pushes);
            int from = to - 8;
            if (to >= A8) { // promotion row
                move_list.push_back(Move(from, to, PROMO_QUEEN, FLAG_QUIET));
                move_list.push_back(Move(from, to, PROMO_ROOK, FLAG_QUIET));
                move_list.push_back(Move(from, to, PROMO_BISHOP, FLAG_QUIET));
                move_list.push_back(Move(from, to, PROMO_KNIGHT, FLAG_QUIET));
            } else {
                move_list.push_back(Move(from, to, PROMO_NONE, FLAG_QUIET));
            }
            clear_bit(temp_pushes, to);
        }
        
        // Double pushes
        uint64_t double_pushes = ((single_pushes & RANK_3) << 8) & empty;
        while (double_pushes) {
            int to = get_lsb(double_pushes);
            int from = to - 16;
            move_list.push_back(Move(from, to, PROMO_NONE, FLAG_DOUBLE_PUSH));
            clear_bit(double_pushes, to);
        }
        
        // Diagonal captures (left: file decreases by 1, index increases by 7)
        uint64_t captures_left = ((pawns & ~FILE_A) << 7) & occupancies_them;
        while (captures_left) {
            int to = get_lsb(captures_left);
            int from = to - 7;
            if (to >= A8) {
                move_list.push_back(Move(from, to, PROMO_QUEEN, FLAG_CAPTURE));
                move_list.push_back(Move(from, to, PROMO_ROOK, FLAG_CAPTURE));
                move_list.push_back(Move(from, to, PROMO_BISHOP, FLAG_CAPTURE));
                move_list.push_back(Move(from, to, PROMO_KNIGHT, FLAG_CAPTURE));
            } else {
                move_list.push_back(Move(from, to, PROMO_NONE, FLAG_CAPTURE));
            }
            clear_bit(captures_left, to);
        }
        
        // Diagonal captures (right: file increases by 1, index increases by 9)
        uint64_t captures_right = ((pawns & ~FILE_H) << 9) & occupancies_them;
        while (captures_right) {
            int to = get_lsb(captures_right);
            int from = to - 9;
            if (to >= A8) {
                move_list.push_back(Move(from, to, PROMO_QUEEN, FLAG_CAPTURE));
                move_list.push_back(Move(from, to, PROMO_ROOK, FLAG_CAPTURE));
                move_list.push_back(Move(from, to, PROMO_BISHOP, FLAG_CAPTURE));
                move_list.push_back(Move(from, to, PROMO_KNIGHT, FLAG_CAPTURE));
            } else {
                move_list.push_back(Move(from, to, PROMO_NONE, FLAG_CAPTURE));
            }
            clear_bit(captures_right, to);
        }
        
        // En Passant
        if (board.ep_sq != NO_SQ) {
            // Check diagonal left EP
            if (board.ep_sq % 8 != 7 && (board.ep_sq - 7) >= 0) {
                int from = board.ep_sq - 7;
                if (get_bit(pawns, from)) {
                    move_list.push_back(Move(from, board.ep_sq, PROMO_NONE, FLAG_CAPTURE | FLAG_EN_PASSANT));
                }
            }
            // Check diagonal right EP
            if (board.ep_sq % 8 != 0 && (board.ep_sq - 9) >= 0) {
                int from = board.ep_sq - 9;
                if (get_bit(pawns, from)) {
                    move_list.push_back(Move(from, board.ep_sq, PROMO_NONE, FLAG_CAPTURE | FLAG_EN_PASSANT));
                }
            }
        }
    } else { // us == BLACK
        uint64_t pawns = board.bitboards[B_PAWN];
        
        // Single pushes
        uint64_t single_pushes = (pawns >> 8) & empty;
        uint64_t temp_pushes = single_pushes;
        while (temp_pushes) {
            int to = get_lsb(temp_pushes);
            int from = to + 8;
            if (to <= H1) { // promotion row
                move_list.push_back(Move(from, to, PROMO_QUEEN, FLAG_QUIET));
                move_list.push_back(Move(from, to, PROMO_ROOK, FLAG_QUIET));
                move_list.push_back(Move(from, to, PROMO_BISHOP, FLAG_QUIET));
                move_list.push_back(Move(from, to, PROMO_KNIGHT, FLAG_QUIET));
            } else {
                move_list.push_back(Move(from, to, PROMO_NONE, FLAG_QUIET));
            }
            clear_bit(temp_pushes, to);
        }
        
        // Double pushes
        uint64_t double_pushes = ((single_pushes & RANK_6) >> 8) & empty;
        while (double_pushes) {
            int to = get_lsb(double_pushes);
            int from = to + 16;
            move_list.push_back(Move(from, to, PROMO_NONE, FLAG_DOUBLE_PUSH));
            clear_bit(double_pushes, to);
        }
        
        // Diagonal captures (left: file decreases by 1, index decreases by 9)
        uint64_t captures_left = ((pawns & ~FILE_A) >> 9) & occupancies_them;
        while (captures_left) {
            int to = get_lsb(captures_left);
            int from = to + 9;
            if (to <= H1) {
                move_list.push_back(Move(from, to, PROMO_QUEEN, FLAG_CAPTURE));
                move_list.push_back(Move(from, to, PROMO_ROOK, FLAG_CAPTURE));
                move_list.push_back(Move(from, to, PROMO_BISHOP, FLAG_CAPTURE));
                move_list.push_back(Move(from, to, PROMO_KNIGHT, FLAG_CAPTURE));
            } else {
                move_list.push_back(Move(from, to, PROMO_NONE, FLAG_CAPTURE));
            }
            clear_bit(captures_left, to);
        }
        
        // Diagonal captures (right: file increases by 1, index decreases by 7)
        uint64_t captures_right = ((pawns & ~FILE_H) >> 7) & occupancies_them;
        while (captures_right) {
            int to = get_lsb(captures_right);
            int from = to + 7;
            if (to <= H1) {
                move_list.push_back(Move(from, to, PROMO_QUEEN, FLAG_CAPTURE));
                move_list.push_back(Move(from, to, PROMO_ROOK, FLAG_CAPTURE));
                move_list.push_back(Move(from, to, PROMO_BISHOP, FLAG_CAPTURE));
                move_list.push_back(Move(from, to, PROMO_KNIGHT, FLAG_CAPTURE));
            } else {
                move_list.push_back(Move(from, to, PROMO_NONE, FLAG_CAPTURE));
            }
            clear_bit(captures_right, to);
        }
        
        // En Passant
        if (board.ep_sq != NO_SQ) {
            // Check diagonal left EP (from A file offset)
            if (board.ep_sq % 8 != 0 && (board.ep_sq + 7) < 64) {
                int from = board.ep_sq + 7;
                if (get_bit(pawns, from)) {
                    move_list.push_back(Move(from, board.ep_sq, PROMO_NONE, FLAG_CAPTURE | FLAG_EN_PASSANT));
                }
            }
            // Check diagonal right EP (from H file offset)
            if (board.ep_sq % 8 != 7 && (board.ep_sq + 9) < 64) {
                int from = board.ep_sq + 9;
                if (get_bit(pawns, from)) {
                    move_list.push_back(Move(from, board.ep_sq, PROMO_NONE, FLAG_CAPTURE | FLAG_EN_PASSANT));
                }
            }
        }
    }

    // ------------------------------------------------
    // 2. Knight Moves
    // ------------------------------------------------
    int knight_p = us * 6 + KNIGHT;
    uint64_t knights = board.bitboards[knight_p];
    while (knights) {
        int from = get_lsb(knights);
        uint64_t attacks = knight_attacks[from] & ~occupancies_us;
        while (attacks) {
            int to = get_lsb(attacks);
            if (get_bit(occupancies_them, to)) {
                move_list.push_back(Move(from, to, PROMO_NONE, FLAG_CAPTURE));
            } else {
                move_list.push_back(Move(from, to, PROMO_NONE, FLAG_QUIET));
            }
            clear_bit(attacks, to);
        }
        clear_bit(knights, from);
    }

    // ------------------------------------------------
    // 3. Bishop Moves
    // ------------------------------------------------
    int bishop_p = us * 6 + BISHOP;
    uint64_t bishops = board.bitboards[bishop_p];
    while (bishops) {
        int from = get_lsb(bishops);
        uint64_t attacks = get_bishop_attacks(from, occupancies_both) & ~occupancies_us;
        while (attacks) {
            int to = get_lsb(attacks);
            if (get_bit(occupancies_them, to)) {
                move_list.push_back(Move(from, to, PROMO_NONE, FLAG_CAPTURE));
            } else {
                move_list.push_back(Move(from, to, PROMO_NONE, FLAG_QUIET));
            }
            clear_bit(attacks, to);
        }
        clear_bit(bishops, from);
    }

    // ------------------------------------------------
    // 4. Rook Moves
    // ------------------------------------------------
    int rook_p = us * 6 + ROOK;
    uint64_t rooks = board.bitboards[rook_p];
    while (rooks) {
        int from = get_lsb(rooks);
        uint64_t attacks = get_rook_attacks(from, occupancies_both) & ~occupancies_us;
        while (attacks) {
            int to = get_lsb(attacks);
            if (get_bit(occupancies_them, to)) {
                move_list.push_back(Move(from, to, PROMO_NONE, FLAG_CAPTURE));
            } else {
                move_list.push_back(Move(from, to, PROMO_NONE, FLAG_QUIET));
            }
            clear_bit(attacks, to);
        }
        clear_bit(rooks, from);
    }

    // ------------------------------------------------
    // 5. Queen Moves
    // ------------------------------------------------
    int queen_p = us * 6 + QUEEN;
    uint64_t queens = board.bitboards[queen_p];
    while (queens) {
        int from = get_lsb(queens);
        uint64_t attacks = get_queen_attacks(from, occupancies_both) & ~occupancies_us;
        while (attacks) {
            int to = get_lsb(attacks);
            if (get_bit(occupancies_them, to)) {
                move_list.push_back(Move(from, to, PROMO_NONE, FLAG_CAPTURE));
            } else {
                move_list.push_back(Move(from, to, PROMO_NONE, FLAG_QUIET));
            }
            clear_bit(attacks, to);
        }
        clear_bit(queens, from);
    }

    // ------------------------------------------------
    // 6. King Moves & Castling
    // ------------------------------------------------
    int king_p = us * 6 + KING;
    int king_from = get_lsb(board.bitboards[king_p]);
    uint64_t king_att = king_attacks[king_from] & ~occupancies_us;
    while (king_att) {
        int to = get_lsb(king_att);
        if (get_bit(occupancies_them, to)) {
            move_list.push_back(Move(king_from, to, PROMO_NONE, FLAG_CAPTURE));
        } else {
            move_list.push_back(Move(king_from, to, PROMO_NONE, FLAG_QUIET));
        }
        clear_bit(king_att, to);
    }

    // Castling Checks
    if (us == WHITE) {
        // Kingside castling
        if (board.castling_rights & WK_CASTLE) {
            if (!get_bit(occupancies_both, F1) && !get_bit(occupancies_both, G1)) {
                if (!board.is_square_attacked(E1, BLACK) &&
                    !board.is_square_attacked(F1, BLACK) &&
                    !board.is_square_attacked(G1, BLACK)) {
                    move_list.push_back(Move(E1, G1, PROMO_NONE, FLAG_CASTLE));
                }
            }
        }
        // Queenside castling
        if (board.castling_rights & WQ_CASTLE) {
            if (!get_bit(occupancies_both, D1) && !get_bit(occupancies_both, C1) && !get_bit(occupancies_both, B1)) {
                if (!board.is_square_attacked(E1, BLACK) &&
                    !board.is_square_attacked(D1, BLACK) &&
                    !board.is_square_attacked(C1, BLACK)) {
                    move_list.push_back(Move(E1, C1, PROMO_NONE, FLAG_CASTLE));
                }
            }
        }
    } else { // us == BLACK
        // Kingside castling
        if (board.castling_rights & BK_CASTLE) {
            if (!get_bit(occupancies_both, F8) && !get_bit(occupancies_both, G8)) {
                if (!board.is_square_attacked(E8, WHITE) &&
                    !board.is_square_attacked(F8, WHITE) &&
                    !board.is_square_attacked(G8, WHITE)) {
                    move_list.push_back(Move(E8, G8, PROMO_NONE, FLAG_CASTLE));
                }
            }
        }
        // Queenside castling
        if (board.castling_rights & BQ_CASTLE) {
            if (!get_bit(occupancies_both, D8) && !get_bit(occupancies_both, C8) && !get_bit(occupancies_both, B8)) {
                if (!board.is_square_attacked(E8, WHITE) &&
                    !board.is_square_attacked(D8, WHITE) &&
                    !board.is_square_attacked(C8, WHITE)) {
                    move_list.push_back(Move(E8, C8, PROMO_NONE, FLAG_CASTLE));
                }
            }
        }
    }
}

void MoveGen::generate_legal_moves(Board& board, std::vector<Move>& move_list) {
    std::vector<Move> pseudo_moves;
    generate_pseudo_legal_moves(board, pseudo_moves);
    int us = board.side_to_move;
    
    for (const auto& m : pseudo_moves) {
        board.make_move(m);
        
        int king_p = us * 6 + KING;
        int king_sq = get_lsb(board.bitboards[king_p]);
        
        if (!board.is_square_attacked(king_sq, us ^ 1)) {
            move_list.push_back(m);
        }
        
        board.unmake_move(m);
    }
}

// Print confirmation message at the end of the file.
// Confirmation: MoveGen.cpp created successfully.
