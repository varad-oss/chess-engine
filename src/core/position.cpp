#include "position.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <mutex>
#include <sstream>

#include "movegen.h"
#include "rng.h"

namespace chess {

namespace zobrist {
Key psq[PIECE_NB][SQUARE_NB];
Key side;
Key castling[16];
Key ep_file[8];
}  // namespace zobrist

namespace {

constexpr std::string_view PIECE_CHARS = "PNBRQKpnbrqk";

// castling_mask[s]: rights that are lost when a piece moves from or to s.
constexpr int castling_mask(Square s) {
    switch (s) {
        case E1: return WHITE_OO | WHITE_OOO;
        case H1: return WHITE_OO;
        case A1: return WHITE_OOO;
        case E8: return BLACK_OO | BLACK_OOO;
        case H8: return BLACK_OO;
        case A8: return BLACK_OOO;
        default: return 0;
    }
}

void init_zobrist() {
    Rng rng(0x5EED5EED1234ABCDULL);
    for (auto& piece : zobrist::psq)
        for (Key& k : piece) k = rng.next();
    zobrist::side = rng.next();
    for (Key& k : zobrist::castling) k = rng.next();
    for (Key& k : zobrist::ep_file) k = rng.next();
}

bool parse_int(std::string_view s, int& out) {
    const char* end = s.data() + s.size();
    const auto [ptr, ec] = std::from_chars(s.data(), end, out);
    return ec == std::errc() && ptr == end;
}

}  // namespace

void init() {
    static std::once_flag once;
    std::call_once(once, [] {
        init_bitboards();
        init_zobrist();
    });
}

Position::Position() {
    init();
    set_fen(START_FEN);
}

void Position::clear() {
    std::fill(std::begin(by_type_), std::end(by_type_), 0);
    std::fill(std::begin(by_color_), std::end(by_color_), 0);
    std::fill(std::begin(board_), std::end(board_), NO_PIECE);
    stm_ = WHITE;
    game_ply_ = 0;
    states_.clear();
    states_.reserve(256);
}

void Position::put_piece(Piece p, Square s) {
    board_[s] = p;
    by_type_[type_of(p)] |= square_bb(s);
    by_color_[color_of(p)] |= square_bb(s);
}

void Position::remove_piece(Square s) {
    const Piece p = board_[s];
    by_type_[type_of(p)] ^= square_bb(s);
    by_color_[color_of(p)] ^= square_bb(s);
    board_[s] = NO_PIECE;
}

void Position::move_piece(Square from, Square to) {
    const Piece p = board_[from];
    const Bitboard from_to = square_bb(from) | square_bb(to);
    by_type_[type_of(p)] ^= from_to;
    by_color_[color_of(p)] ^= from_to;
    board_[from] = NO_PIECE;
    board_[to] = p;
}

bool Position::set_fen(std::string_view fen, std::string* error) {
    auto fail = [error](const char* message) {
        if (error) *error = message;
        return false;
    };

    std::vector<std::string_view> fields;
    for (size_t i = 0; i < fen.size();) {
        while (i < fen.size() && std::isspace(static_cast<unsigned char>(fen[i]))) ++i;
        const size_t start = i;
        while (i < fen.size() && !std::isspace(static_cast<unsigned char>(fen[i]))) ++i;
        if (i > start) fields.push_back(fen.substr(start, i - start));
    }
    if (fields.size() < 4 || fields.size() > 6)
        return fail("FEN must have 4 to 6 space-separated fields");

    Position p{EmptyTag{}};

    // 1. Piece placement, from rank 8 down to rank 1.
    int rank = 7, file = 0;
    for (const char c : fields[0]) {
        if (c == '/') {
            if (file != 8 || rank == 0) return fail("each rank must describe exactly 8 squares");
            --rank;
            file = 0;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
            if (file > 8) return fail("rank describes more than 8 squares");
        } else {
            const size_t idx = PIECE_CHARS.find(c);
            if (idx == std::string_view::npos) return fail("invalid piece character");
            if (file >= 8) return fail("rank describes more than 8 squares");
            p.put_piece(Piece(idx), make_square(file, rank));
            ++file;
        }
    }
    if (rank != 0 || file != 8) return fail("board must have 8 ranks of 8 squares");
    if (p.count(WHITE, KING) != 1 || p.count(BLACK, KING) != 1)
        return fail("each side must have exactly one king");
    if (p.pieces(PAWN) & (RANK_1_BB | RANK_8_BB)) return fail("pawns on the first or last rank");

    // 2. Side to move.
    if (fields[1] == "w") p.stm_ = WHITE;
    else if (fields[1] == "b") p.stm_ = BLACK;
    else return fail("side to move must be 'w' or 'b'");

    // 3. Castling rights. Rights whose king or rook is not on its original
    // square are dropped rather than rejected, as many GUIs emit them loosely.
    StateInfo st;
    if (fields[2] != "-") {
        for (const char c : fields[2]) {
            switch (c) {
                case 'K': st.castling |= WHITE_OO; break;
                case 'Q': st.castling |= WHITE_OOO; break;
                case 'k': st.castling |= BLACK_OO; break;
                case 'q': st.castling |= BLACK_OOO; break;
                default: return fail("invalid castling field");
            }
        }
    }
    if (p.piece_on(E1) != W_KING) st.castling &= ~(WHITE_OO | WHITE_OOO);
    if (p.piece_on(H1) != W_ROOK) st.castling &= ~WHITE_OO;
    if (p.piece_on(A1) != W_ROOK) st.castling &= ~WHITE_OOO;
    if (p.piece_on(E8) != B_KING) st.castling &= ~(BLACK_OO | BLACK_OOO);
    if (p.piece_on(H8) != B_ROOK) st.castling &= ~BLACK_OO;
    if (p.piece_on(A8) != B_ROOK) st.castling &= ~BLACK_OOO;

    // 4. En passant target. Stored only if a capture is pseudo-legal, so that
    // positions differing only in an unusable ep square hash identically.
    if (fields[3] != "-") {
        const std::string_view ep = fields[3];
        if (ep.size() != 2 || ep[0] < 'a' || ep[0] > 'h' || ep[1] < '1' || ep[1] > '8')
            return fail("invalid en passant square");
        const Square s = make_square(ep[0] - 'a', ep[1] - '1');
        if (relative_rank(p.stm_, s) != 5) return fail("en passant square on the wrong rank");
        const Square pushed = s - pawn_push(p.stm_);
        if (p.piece_on(pushed) != make_piece(~p.stm_, PAWN) || p.piece_on(s) != NO_PIECE ||
            p.piece_on(s + pawn_push(p.stm_)) != NO_PIECE)
            return fail("en passant square is inconsistent with the board");
        if (p.ep_capture_possible(s, p.stm_)) st.ep = s;
    }

    // 5./6. Optional move counters.
    int fullmove = 1;
    if (fields.size() >= 5 && (!parse_int(fields[4], st.rule50) || st.rule50 < 0))
        return fail("invalid halfmove clock");
    if (fields.size() == 6 && (!parse_int(fields[5], fullmove) || fullmove < 0))
        return fail("invalid fullmove number");
    p.game_ply_ = 2 * (std::max(fullmove, 1) - 1) + (p.stm_ == BLACK);

    if (p.attackers_to(p.king_square(~p.stm_)) & p.pieces(p.stm_))
        return fail("the side not to move is in check");

    p.states_.push_back(st);
    p.states_.back().key = p.compute_key();
    p.states_.back().checkers = p.compute_checkers();

    *this = std::move(p);
    return true;
}

std::string Position::fen() const {
    std::string s;
    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            const Piece p = board_[make_square(file, rank)];
            if (p == NO_PIECE) {
                ++empty;
                continue;
            }
            if (empty) s += char('0' + empty);
            empty = 0;
            s += PIECE_CHARS[p];
        }
        if (empty) s += char('0' + empty);
        if (rank) s += '/';
    }
    s += stm_ == WHITE ? " w " : " b ";
    const int cr = castling_rights();
    if (cr & WHITE_OO) s += 'K';
    if (cr & WHITE_OOO) s += 'Q';
    if (cr & BLACK_OO) s += 'k';
    if (cr & BLACK_OOO) s += 'q';
    if (!cr) s += '-';
    s += ' ';
    s += ep_square() == NO_SQUARE ? "-" : square_to_string(ep_square());
    s += ' ' + std::to_string(rule50()) + ' ' + std::to_string(1 + game_ply_ / 2);
    return s;
}

std::string Position::pretty() const {
    std::ostringstream out;
    out << "\n +---+---+---+---+---+---+---+---+\n";
    for (int rank = 7; rank >= 0; --rank) {
        for (int file = 0; file < 8; ++file) {
            const Piece p = board_[make_square(file, rank)];
            out << " | " << (p == NO_PIECE ? ' ' : PIECE_CHARS[p]);
        }
        out << " | " << rank + 1 << "\n +---+---+---+---+---+---+---+---+\n";
    }
    out << "   a   b   c   d   e   f   g   h\n\nFen: " << fen() << "\nKey: " << std::hex
        << key() << std::dec << "\nCheckers:";
    for (Bitboard b = checkers(); b;) out << ' ' << square_to_string(pop_lsb(b));
    out << '\n';
    return out.str();
}

Bitboard Position::attackers_to(Square s, Bitboard occupied) const {
    return (attacks::Pawn[BLACK][s] & pieces(WHITE, PAWN)) |
           (attacks::Pawn[WHITE][s] & pieces(BLACK, PAWN)) |
           (attacks::Knight[s] & pieces(KNIGHT)) |
           (bishop_attacks(s, occupied) & pieces(BISHOP, QUEEN)) |
           (rook_attacks(s, occupied) & pieces(ROOK, QUEEN)) |
           (attacks::King[s] & pieces(KING));
}

Bitboard Position::king_blockers(Color c) const {
    const Square ksq = king_square(c);
    Bitboard snipers = ((rook_attacks(ksq, 0) & pieces(ROOK, QUEEN)) |
                        (bishop_attacks(ksq, 0) & pieces(BISHOP, QUEEN))) &
                       pieces(~c);
    const Bitboard occupied = pieces() ^ snipers;
    Bitboard blockers = 0;
    while (snipers) {
        const Bitboard b = between_bb(ksq, pop_lsb(snipers)) & occupied;
        if (b && !more_than_one(b)) blockers |= b;
    }
    return blockers;
}

Bitboard Position::compute_checkers() const {
    return attackers_to(king_square(stm_)) & pieces(~stm_);
}

bool Position::ep_capture_possible(Square ep, Color capturer) const {
    return (attacks::Pawn[~capturer][ep] & pieces(capturer, PAWN)) != 0;
}

bool Position::gives_check(Move m) const {
    const Color us = stm_;
    const Square from = m.from(), to = m.to(), ksq = king_square(~us);
    const Bitboard from_bb = square_bb(from), to_bb = square_bb(to);

    // Our pieces and the occupancy as they will be after the move.
    Bitboard occupied = (pieces() ^ from_bb) | to_bb;
    Bitboard pawns = pieces(us, PAWN) & ~from_bb;
    Bitboard knights = pieces(us, KNIGHT) & ~from_bb;
    Bitboard diagonal = pieces(us, BISHOP, QUEEN) & ~from_bb;
    Bitboard orthogonal = pieces(us, ROOK, QUEEN) & ~from_bb;

    if (m.is_ep()) occupied ^= square_bb(to - pawn_push(us));
    if (m.is_castle()) {
        const bool king_side = m.flag() == KING_CASTLE;
        const Bitboard rook_move = square_bb(relative_square(us, king_side ? H1 : A1)) |
                                   square_bb(relative_square(us, king_side ? F1 : D1));
        occupied ^= rook_move;
        orthogonal ^= rook_move;
    }

    switch (m.is_promotion() ? m.promotion_type() : type_of(board_[from])) {
        case PAWN: pawns |= to_bb; break;
        case KNIGHT: knights |= to_bb; break;
        case BISHOP: diagonal |= to_bb; break;
        case ROOK: orthogonal |= to_bb; break;
        case QUEEN: diagonal |= to_bb; orthogonal |= to_bb; break;
        default: break;
    }

    return (attacks::Pawn[~us][ksq] & pawns) || (attacks::Knight[ksq] & knights) ||
           (bishop_attacks(ksq, occupied) & diagonal) || (rook_attacks(ksq, occupied) & orthogonal);
}

void Position::make_move(Move m) {
    // Copy what we need before push_back may reallocate the state stack.
    StateInfo st;
    {
        const StateInfo& prev = states_.back();
        st.key = prev.key ^ zobrist::side;
        st.castling = prev.castling;
        st.rule50 = prev.rule50 + 1;
        st.plies_from_null = prev.plies_from_null + 1;
        if (prev.ep != NO_SQUARE) st.key ^= zobrist::ep_file[file_of(prev.ep)];
    }

    const Color us = stm_, them = ~us;
    const Square from = m.from(), to = m.to();
    const Piece pc = board_[from];

    if (m.is_castle()) {
        const bool king_side = m.flag() == KING_CASTLE;
        const Square rook_from = relative_square(us, king_side ? H1 : A1);
        const Square rook_to = relative_square(us, king_side ? F1 : D1);
        const Piece rook = make_piece(us, ROOK);
        move_piece(from, to);
        move_piece(rook_from, rook_to);
        st.key ^= zobrist::psq[pc][from] ^ zobrist::psq[pc][to] ^ zobrist::psq[rook][rook_from] ^
                  zobrist::psq[rook][rook_to];
    } else {
        if (m.is_capture()) {
            const Square cap_sq = m.is_ep() ? to - pawn_push(us) : to;
            st.captured = board_[cap_sq];
            st.key ^= zobrist::psq[st.captured][cap_sq];
            remove_piece(cap_sq);
            st.rule50 = 0;
        }
        move_piece(from, to);
        st.key ^= zobrist::psq[pc][from] ^ zobrist::psq[pc][to];

        if (type_of(pc) == PAWN) {
            st.rule50 = 0;
            if (m.is_promotion()) {
                const Piece promoted = make_piece(us, m.promotion_type());
                remove_piece(to);
                put_piece(promoted, to);
                st.key ^= zobrist::psq[pc][to] ^ zobrist::psq[promoted][to];
            } else if (m.flag() == DOUBLE_PUSH) {
                const Square ep = from + pawn_push(us);
                if (ep_capture_possible(ep, them)) {
                    st.ep = ep;
                    st.key ^= zobrist::ep_file[file_of(ep)];
                }
            }
        }
    }

    const int lost = castling_mask(from) | castling_mask(to);
    if (st.castling & lost) {
        st.key ^= zobrist::castling[st.castling];
        st.castling &= ~lost;
        st.key ^= zobrist::castling[st.castling];
    }

    stm_ = them;
    ++game_ply_;
    st.checkers = attackers_to(king_square(them)) & pieces(us);
    states_.push_back(st);
}

void Position::unmake_move(Move m) {
    const Piece captured = st().captured;
    stm_ = ~stm_;
    --game_ply_;
    const Color us = stm_;
    const Square from = m.from(), to = m.to();

    if (m.is_castle()) {
        const bool king_side = m.flag() == KING_CASTLE;
        move_piece(to, from);
        move_piece(relative_square(us, king_side ? F1 : D1), relative_square(us, king_side ? H1 : A1));
    } else {
        if (m.is_promotion()) {
            remove_piece(to);
            put_piece(make_piece(us, PAWN), to);
        }
        move_piece(to, from);
        if (captured != NO_PIECE) put_piece(captured, m.is_ep() ? to - pawn_push(us) : to);
    }
    states_.pop_back();
}

void Position::make_null_move() {
    StateInfo st;
    {
        const StateInfo& prev = states_.back();
        st.key = prev.key ^ zobrist::side;
        if (prev.ep != NO_SQUARE) st.key ^= zobrist::ep_file[file_of(prev.ep)];
        st.castling = prev.castling;
        st.rule50 = prev.rule50 + 1;
        st.plies_from_null = 0;
    }
    stm_ = ~stm_;
    ++game_ply_;
    st.checkers = compute_checkers();
    states_.push_back(st);
}

void Position::unmake_null_move() {
    stm_ = ~stm_;
    --game_ply_;
    states_.pop_back();
}

bool Position::is_repetition(int ply) const {
    const int current = int(states_.size()) - 1;
    const int end = std::min({st().rule50, st().plies_from_null, current});
    int count = 0;
    for (int i = 4; i <= end; i += 2) {
        if (states_[current - i].key == st().key) {
            if (i <= ply || ++count == 2) return true;
        }
    }
    return false;
}

bool Position::is_fifty_move_draw() const {
    if (st().rule50 < 100) return false;
    if (!in_check()) return true;
    MoveList moves;
    generate<GenType::All>(*this, moves);
    return moves.size() > 0;  // checkmate on the 100th ply takes precedence
}

bool Position::is_insufficient_material() const {
    if (pieces(PAWN) | pieces(ROOK, QUEEN)) return false;
    const Bitboard minors = pieces(KNIGHT, BISHOP);
    if (!more_than_one(minors)) return true;
    if (pieces(KNIGHT)) return false;
    // Only bishops left: drawn when they all stand on squares of one colour.
    return !(minors & LIGHT_SQUARES_BB) || !(minors & DARK_SQUARES_BB);
}

int Position::see(Move m) const {
    static constexpr int value[PIECE_TYPE_NB + 1] = {100, 320, 330, 500, 900, 20000, 0};
    if (m.is_castle()) return 0;

    const Square from = m.from(), to = m.to();
    Bitboard occupied = pieces() ^ square_bb(from);
    PieceType on_square = type_of(board_[from]);  // piece standing on `to` after each capture
    int gain[32];
    int d = 0;

    if (m.is_ep()) {
        gain[0] = value[PAWN];
        occupied ^= square_bb(to - pawn_push(stm_));
    } else {
        gain[0] = value[type_of(board_[to])];
    }
    if (m.is_promotion()) {
        gain[0] += value[m.promotion_type()] - value[PAWN];
        on_square = m.promotion_type();
    }

    const Bitboard diagonal = pieces(BISHOP, QUEEN), orthogonal = pieces(ROOK, QUEEN);
    Bitboard attackers = attackers_to(to, occupied) & occupied;
    Color side = ~stm_;

    while (d < 31) {
        const Bitboard mine = attackers & pieces(side);
        if (!mine) break;
        PieceType pt = PAWN;
        while (!(mine & pieces(pt))) pt = PieceType(pt + 1);

        ++d;
        gain[d] = value[on_square] - gain[d - 1];
        on_square = pt;
        occupied ^= square_bb(lsb(mine & pieces(pt)));
        // Removing the attacker may uncover a slider behind it (x-ray).
        if (pt == PAWN || pt == BISHOP || pt == QUEEN) attackers |= bishop_attacks(to, occupied) & diagonal;
        if (pt == ROOK || pt == QUEEN) attackers |= rook_attacks(to, occupied) & orthogonal;
        attackers &= occupied;
        side = ~side;
    }

    // Negamax the gain list: each side may decline to continue the exchange.
    for (; d > 0; --d) gain[d - 1] = -std::max(-gain[d - 1], gain[d]);
    return gain[0];
}

Move Position::parse_uci_move(std::string_view text) const {
    std::string lowered(text);
    for (char& c : lowered) c = char(std::tolower(static_cast<unsigned char>(c)));
    MoveList moves;
    generate<GenType::All>(*this, moves);
    for (const Move m : moves)
        if (m.uci() == lowered) return m;
    return Move::none();
}

Key Position::compute_key() const {
    Key k = 0;
    for (int s = 0; s < SQUARE_NB; ++s)
        if (board_[s] != NO_PIECE) k ^= zobrist::psq[board_[s]][s];
    if (stm_ == BLACK) k ^= zobrist::side;
    k ^= zobrist::castling[st().castling];
    if (st().ep != NO_SQUARE) k ^= zobrist::ep_file[file_of(st().ep)];
    return k;
}

bool Position::is_consistent(std::string* why) const {
    auto fail = [why](const char* message) {
        if (why) *why = message;
        return false;
    };
    if (by_color_[WHITE] & by_color_[BLACK]) return fail("colour bitboards overlap");
    Bitboard all_types = 0;
    for (int pt = 0; pt < PIECE_TYPE_NB; ++pt) {
        if (all_types & by_type_[pt]) return fail("piece-type bitboards overlap");
        all_types |= by_type_[pt];
    }
    if (all_types != pieces()) return fail("type and colour bitboards disagree");
    for (int s = 0; s < SQUARE_NB; ++s) {
        const Piece p = board_[s];
        const bool occupied = pieces() & square_bb(Square(s));
        if ((p == NO_PIECE) == occupied) return fail("mailbox disagrees with occupancy");
        if (p != NO_PIECE && !(pieces(color_of(p), type_of(p)) & square_bb(Square(s))))
            return fail("mailbox piece missing from its bitboard");
    }
    if (count(WHITE, KING) != 1 || count(BLACK, KING) != 1) return fail("king count");
    if (key() != compute_key()) return fail("incremental hash differs from recomputed hash");
    if (checkers() != compute_checkers()) return fail("stale checkers bitboard");
    if (ep_square() != NO_SQUARE && !ep_capture_possible(ep_square(), stm_))
        return fail("en passant square set without a capturing pawn");
    return true;
}

}  // namespace chess
