/**
 * @file Search.cpp
 * @brief AI Search implementation.
 */

#include "Search.h"
#include "MoveGen.h"
#include "Eval.h"
#include <iostream>
#include <cstring>

namespace Search {
    // Global variable definitions
    std::vector<TTEntry> tt_table;
    int tt_size_entries = 0;
    uint64_t nodes = 0;
    bool stop_search = false;
    long long start_time = 0;
    long long time_limit = -1;

    // Local structures and tables for search heuristics
    static Move killer_moves[2][MAX_PLY];
    static int history_moves[12][64];

    // Piece values for MVV-LVA scoring
    static const int victim_val[] = { 100, 320, 330, 500, 900, 20000 };
    static const int attacker_val[] = { 1, 2, 3, 4, 5, 6 };

    /**
     * Helper to get system time in milliseconds.
     */
    static long long get_time_ms() {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(now.time_since_epoch()).count();
    }

    /**
     * Checks if search time is exceeded and signals to stop.
     */
    static void check_time() {
        if (time_limit != -1 && (nodes & 2047) == 0) {
            if (get_time_ms() - start_time >= time_limit) {
                stop_search = true;
            }
        }
    }

    /**
     * Computes the MVV-LVA score for captures.
     */
    static int get_mvv_lva_score(int attacker, int victim) {
        return 100000 + victim_val[victim % 6] * 10 - attacker_val[attacker % 6];
    }

    void resize_tt(int mb) {
        int bytes = mb * 1024 * 1024;
        int num_entries = bytes / sizeof(TTEntry);
        
        // Force power of 2 size for fast index bitmasking
        int p2 = 1;
        while (p2 * 2 <= num_entries) {
            p2 *= 2;
        }
        tt_size_entries = p2;
        tt_table.resize(tt_size_entries);
        clear_tt();
    }

    void clear_tt() {
        for (int i = 0; i < tt_size_entries; i++) {
            tt_table[i].hash = 0ULL;
            tt_table[i].depth = 0;
            tt_table[i].score = 0;
            tt_table[i].flags = 0;
            tt_table[i].best_move = Move();
        }
    }

    void write_tt(uint64_t hash, int depth, int score, int flags, Move best_move, int ply) {
        if (tt_size_entries == 0) return;
        int index = hash & (tt_size_entries - 1);

        // Adjust mate scores from absolute to ply-relative
        if (score > MATE_THRESHOLD) score += ply;
        else if (score < -MATE_THRESHOLD) score -= ply;

        // Depth-preferred replacement scheme
        if (tt_table[index].hash == 0 || tt_table[index].depth <= depth) {
            tt_table[index].hash = hash;
            tt_table[index].depth = depth;
            tt_table[index].score = score;
            tt_table[index].flags = flags;
            tt_table[index].best_move = best_move;
        }
    }

    bool read_tt(uint64_t hash, int depth, int alpha, int beta, int& score, Move& best_move, int ply) {
        if (tt_size_entries == 0) return false;
        int index = hash & (tt_size_entries - 1);

        if (tt_table[index].hash == hash) {
            best_move = tt_table[index].best_move;
            
            if (tt_table[index].depth >= depth) {
                int tt_score = tt_table[index].score;
                
                // Adjust mate scores from ply-relative back to absolute
                if (tt_score > MATE_THRESHOLD) tt_score -= ply;
                else if (tt_score < -MATE_THRESHOLD) tt_score += ply;

                if (tt_table[index].flags == HASH_EXACT) {
                    score = tt_score;
                    return true;
                }
                if (tt_table[index].flags == HASH_ALPHA && tt_score <= alpha) {
                    score = alpha;
                    return true;
                }
                if (tt_table[index].flags == HASH_BETA && tt_score >= beta) {
                    score = beta;
                    return true;
                }
            }
        }
        return false;
    }

    /**
     * Score a move for ordering heuristics.
     */
    static int score_move(const Board& board, const Move& m, const Move& tt_move, int ply) {
        if (m == tt_move) return 1000000;

        int attacker = board.get_piece_on_square(m.from);

        if (m.is_capture()) {
            int victim;
            if (m.is_enpassant()) {
                victim = (board.side_to_move == WHITE) ? B_PAWN : W_PAWN;
            } else {
                victim = board.get_piece_on_square(m.to);
            }
            return get_mvv_lva_score(attacker, victim);
        }

        if (m.is_promotion()) {
            return 90000 + victim_val[m.promo]; // bias based on promo piece
        }

        // Killer moves
        if (m == killer_moves[0][ply]) return 80000;
        if (m == killer_moves[1][ply]) return 70000;

        // History heuristic
        return history_moves[attacker][m.to];
    }

    /**
     * Quiescence search for evaluating captures only.
     */
    static int quiescence(Board& board, int alpha, int beta, int ply) {
        check_time();
        if (stop_search) return 0;
        nodes++;

        // Draw detection
        if (board.halfmove_clock >= 100) return 0;

        int stand_pat = Eval::evaluate(board);
        if (board.side_to_move == BLACK) stand_pat = -stand_pat;

        if (stand_pat >= beta) return beta;
        if (stand_pat > alpha) alpha = stand_pat;

        std::vector<Move> moves;
        MoveGen::generate_legal_moves(board, moves);

        // Score and filter for captures only
        std::vector<int> scores;
        std::vector<Move> cap_moves;
        for (const auto& m : moves) {
            if (m.is_capture()) {
                cap_moves.push_back(m);
                scores.push_back(score_move(board, m, Move(), ply));
            }
        }

        // Search captures in order
        for (size_t next = 0; next < cap_moves.size(); next++) {
            // Selection sort on the fly
            int best_score = -INF;
            size_t best_idx = next;
            for (size_t i = next; i < cap_moves.size(); i++) {
                if (scores[i] > best_score) {
                    best_score = scores[i];
                    best_idx = i;
                }
            }
            std::swap(cap_moves[next], cap_moves[best_idx]);
            std::swap(scores[next], scores[best_idx]);

            board.make_move(cap_moves[next]);
            int score = -quiescence(board, -beta, -alpha, ply + 1);
            board.unmake_move(cap_moves[next]);

            if (stop_search) return 0;

            if (score >= beta) return beta;
            if (score > alpha) alpha = score;
        }

        return alpha;
    }

    /**
     * Principal Negamax alpha-beta search.
     */
    static int negamax(Board& board, int alpha, int beta, int depth, int ply) {
        check_time();
        if (stop_search) return 0;
        nodes++;

        // Draw detection (50-move rule and repetition)
        if (board.halfmove_clock >= 100) return 0;
        
        bool is_repetition = false;
        int start_idx = (int)board.history.size() - board.halfmove_clock;
        if (start_idx < 0) start_idx = 0;
        for (int i = start_idx; i < (int)board.history.size() - 1; i++) {
            if (board.history[i].hash_key == board.hash_key) {
                is_repetition = true;
                break;
            }
        }
        if (is_repetition) return 0;

        if (depth <= 0) {
            return quiescence(board, alpha, beta, ply);
        }

        Move tt_move;
        int tt_score;
        if (read_tt(board.hash_key, depth, alpha, beta, tt_score, tt_move, ply)) {
            return tt_score;
        }

        int king_p = board.side_to_move * 6 + KING;
        int king_sq = get_lsb(board.bitboards[king_p]);
        bool in_check = board.is_square_attacked(king_sq, board.side_to_move ^ 1);

        // Check Extensions
        if (in_check) {
            depth++;
        }

        // Null Move Pruning
        // Do not use null move if in check, or if we are at root (ply == 0).
        if (depth >= 3 && !in_check && ply > 0) {
            board.make_null_move();
            int R = 2; // standard depth reduction
            int null_score = -negamax(board, -beta, -beta + 1, depth - 1 - R, ply + 1);
            board.unmake_null_move();
            if (stop_search) return 0;
            if (null_score >= beta) return beta;
        }

        std::vector<Move> moves;
        MoveGen::generate_legal_moves(board, moves);

        if (moves.empty()) {
            if (in_check) {
                return -MATE_VALUE + ply; // Checkmate
            }
            return 0; // Stalemate
        }

        // Score moves
        std::vector<int> scores(moves.size());
        for (size_t i = 0; i < moves.size(); i++) {
            scores[i] = score_move(board, moves[i], tt_move, ply);
        }

        int best_score = -INF;
        Move best_move;
        int hash_flag = HASH_ALPHA;

        for (size_t next = 0; next < moves.size(); next++) {
            // Selection sort on the fly
            int best_sc = -INF;
            size_t best_idx = next;
            for (size_t i = next; i < moves.size(); i++) {
                if (scores[i] > best_sc) {
                    best_sc = scores[i];
                    best_idx = i;
                }
            }
            std::swap(moves[next], moves[best_idx]);
            std::swap(scores[next], scores[best_idx]);

            Move current_move = moves[next];

            board.make_move(current_move);
            
            int score;
            // Late Move Reductions (LMR)
            // Reduce depth for late moves that are quiet (non-capture, non-promotion)
            // and when we are not currently in check.
            if (depth >= 3 && next >= 4 && !in_check && !current_move.is_capture() && !current_move.is_promotion()) {
                score = -negamax(board, -alpha - 1, -alpha, depth - 2, ply + 1);
                if (score > alpha && score < beta) {
                    // Re-search at full depth if it improved alpha
                    score = -negamax(board, -beta, -alpha, depth - 1, ply + 1);
                }
            } else {
                score = -negamax(board, -beta, -alpha, depth - 1, ply + 1);
            }
            
            board.unmake_move(current_move);

            if (stop_search) return 0;

            if (score > best_score) {
                best_score = score;
                best_move = current_move;
            }

            if (score >= beta) {
                write_tt(board.hash_key, depth, beta, HASH_BETA, current_move, ply);
                
                // Update killer moves and history heuristic for non-captures
                if (!current_move.is_capture()) {
                    killer_moves[1][ply] = killer_moves[0][ply];
                    killer_moves[0][ply] = current_move;

                    int attacker = board.get_piece_on_square(current_move.from);
                    history_moves[attacker][current_move.to] += depth * depth;
                }
                return beta;
            }

            if (score > alpha) {
                alpha = score;
                hash_flag = HASH_EXACT;
            }
        }

        write_tt(board.hash_key, depth, best_score, hash_flag, best_move, ply);
        return best_score;
    }

    Move search_position(Board& board, int max_depth) {
        nodes = 0;
        stop_search = false;
        start_time = get_time_ms();

        std::memset(killer_moves, 0, sizeof(killer_moves));
        std::memset(history_moves, 0, sizeof(history_moves));

        Move best_move;

        // Iterative deepening search loop
        for (int depth = 1; depth <= max_depth; depth++) {
            int score = negamax(board, -INF, INF, depth, 0);

            if (stop_search) break;

            long long elapsed = get_time_ms() - start_time;
            uint64_t nps = elapsed > 0 ? (nodes * 1000 / elapsed) : 0;

            // Retrieve PV from the transposition table
            std::string pv_str = "";
            Board temp_board = board;
            for (int d = 0; d < depth; d++) {
                if (tt_size_entries == 0) break;
                int idx = temp_board.hash_key & (tt_size_entries - 1);
                if (tt_table[idx].hash == temp_board.hash_key && tt_table[idx].best_move.is_valid()) {
                    Move pv_move = tt_table[idx].best_move;
                    pv_str += pv_move.to_uci() + " ";
                    temp_board.make_move(pv_move);
                } else {
                    break;
                }
            }

            // Print search progress using standard UCI format
            std::cout << "info depth " << depth << " score ";
            if (score > MATE_THRESHOLD) {
                int mate_in_moves = (MATE_VALUE - score + 1) / 2;
                std::cout << "mate " << mate_in_moves;
            } else if (score < -MATE_THRESHOLD) {
                int mate_in_moves = (-MATE_VALUE - score) / 2;
                std::cout << "mate -" << mate_in_moves;
            } else {
                std::cout << "cp " << score;
            }
            std::cout << " nodes " << nodes << " time " << elapsed << " nps " << nps << " pv " << pv_str << "\n";
            std::cout << std::flush;
        }

        // Print final chosen move
        if (tt_size_entries > 0) {
            int index = board.hash_key & (tt_size_entries - 1);
            if (tt_table[index].hash == board.hash_key && tt_table[index].best_move.is_valid()) {
                best_move = tt_table[index].best_move;
            }
        }
        
        // Fallback to first legal move if TT was empty
        if (!best_move.is_valid()) {
            std::vector<Move> moves;
            MoveGen::generate_legal_moves(board, moves);
            if (!moves.empty()) {
                best_move = moves[0];
            }
        }

        std::cout << "bestmove " << best_move.to_uci() << "\n" << std::flush;
        return best_move;
    }
}

// Print confirmation message at the end of the file.
// Confirmation: Search.cpp created successfully.
