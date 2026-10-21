# Audit of the original engine (commit 0731f51)

Before the rewrite, the original sources were built unchanged (`make`, Apple
clang, `-O2`) and probed. Everything below was reproduced on that build; the
commands are given so it can be re-checked with `git worktree add old 0731f51`.

## What was solid

* **Move generation was correct.** It matched the published perft counts on
  all eight positions tried (start position d5, Kiwipete d4, positions 3–6,
  two en-passant pin cases), at roughly 30M leaf nodes/s. The README's perft
  claim held up.
* The overall structure (bitboards, magic lookups, iterative deepening,
  alpha-beta with a TT, quiescence, null move, LMR, killers/history) was the
  right skeleton.

## Defects found

### Protocol and robustness

| defect | evidence |
| --- | --- |
| `stop` ignored; `go infinite` never ends | search ran on the stdin thread. `go infinite`, then `stop`, then `quit`: the process was still searching 5 s later |
| crash on a valid FEN | `position fen <fen without counters> moves e2e4` → `libc++abi: terminating due to uncaught exception of type std::invalid_argument: stoi` |
| malformed mate scores | position `k7/8/1K6/8/8/8/8/7R b - - 0 1`: `info ... score mate --1` |
| `position fen` assumed exactly 6 FEN tokens | with 4 fields the word `moves` was consumed as part of the FEN |
| time management could exceed the clock | `wtime/20 + winc/2` can exceed the remaining time when the increment is large and the clock low |
| `go nodes` unsupported | silently ignored, then searched to depth 64 |
| UCI author string | `id author Google DeepMind pair-programming agent` |

### Search

* LMR: a reduced null-window search that failed **high** was accepted without
  a full-depth re-search (`if (score > alpha && score < beta)` re-search only).
* Null-move pruning applied in pawn-only endings (zugzwang) and allowed two
  null moves in a row.
* `killer_moves[2][MAX_PLY=64]` indexed by ply while check extensions are
  unbounded and the default depth is 64: out-of-bounds writes are reachable.
* History scores grew without bound and could exceed the 80000/100000 bands
  reserved for killers and captures.
* Quiescence search stood pat while in check (horizon mates missed) and
  generated all legal moves via make/unmake just to filter captures.
* The final move came from a TT probe of the root, which index collisions can
  overwrite.
* A `std::vector` was heap-allocated for the move list at every node.

### Evaluation

* **The mobility term was always zero**: it was computed from
  `w_knights | w_bishops | ...` *after* the PST loops had cleared those
  bitboards to 0. The README described it as "up to a 50x speedup in
  evaluation throughput".
* The trapped-bishop rule checked A2/H2 *and* A7/H7 for Black, so a normal
  black bishop on a7/h7 with a pawn on b6/g6 cost 150 cp. It also counted
  pawns of either colour as the trapping pawn.
* No tapering: the king switched from the middlegame to the endgame table when
  the seventh-last non-pawn piece was captured. From the tables, that single
  capture changes a king's score by up to 90 cp (a king on e4 goes from −50 to
  +40), a discontinuity the search can be steered by.

### Hashing

* `generate_hash()` XORed `zobrist_ep[NO_SQ]` while incremental updates did
  not maintain it, so a position reached by moves and the same position loaded
  from FEN had different keys (affecting repetition detection and the TT).
* The ep square was hashed even when no capture was possible.

### HTTP server

* Bound to `INADDR_ANY` (reachable from the local network), handled a request
  with a single `read()` of at most 4 KB, no timeouts, `Access-Control-Allow-
  Origin: *`, and received only a FEN, so the engine could not see
  repetitions in the browser game.
* The browser's alternative opponent ("Glip (JS 1000 Elo)") played uniformly
  random legal moves.

### Claims in the README that could not be supported

* "~2160 Elo", "44.5% vs Stockfish @2200", "+117 Elo [+77, +161] LOS 100%":
  the repository contained no match runner, opening book, game records or
  Stockfish integration, and the commit history has no builds corresponding to
  the "baseline / v1 / v2" rows. These numbers could not be reproduced and
  are not carried over.
* "Magic numbers verified at startup": they were not verified.
* "Mobility counting … 50x speedup": the term was always zero (above).
* The depth-8 node count (213,521) *was* reproduced exactly; it reflects the
  aggressive, partly unsound pruning above rather than search quality.

### Repository hygiene

* The compiled binary `chess` and `.DS_Store` were committed despite being
  listed in `.gitignore`.
* No tests of any kind.
* An unrelated project (`cv-reviewer-extension/`, a Chrome extension) lives in
  the repository. It was left untouched; it should move to its own repository.
