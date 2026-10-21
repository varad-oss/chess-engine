# Chess Engine

A UCI chess engine in C++20, built around one rule: **every claim about it is
backed by a check you can run.** The move generator is verified by perft, the
evaluation weights are fitted to self-play data by an in-repo Texel tuner, and
every strength claim comes from an in-repo match runner with SPRT statistics.

This is version 2, a ground-up rewrite. [docs/AUDIT.md](docs/AUDIT.md)
documents what was wrong with version 1, and [docs/DESIGN.md](docs/DESIGN.md)
explains the design.

## Results at a glance

All numbers were measured on an Apple M5 (4 performance + 6 efficiency cores)
with the commands in [docs/RESULTS.md](docs/RESULTS.md).

| what | result |
| --- | --- |
| Move generation | perft suite: 21 positions, 51 depth entries, 640M leaf nodes, all exact |
| Perft speed vs v1 | 3.0x faster like-for-like; 17.5x with bulk counting (start position, depth 6) |
| Strength vs v1 | **+200 −0 =0** at equal time; **+193 −1 =6** with v1 given 10x the time |
| Evaluation tuning | **+117.9 ± 41.0 Elo** over the hand-set weights (SPRT accepted, 209 games) |
| Rejected changes | continuation history (+7.5 ± 10.5, SPRT inconclusive at 2,000 games); a second tuning round (failed an eval sanity check) |
| Lazy SMP | 4 threads reach depth 18 1.7x faster; nodes/s scale 3.2x |
| UCI conformance | 9/9 protocol integration tests (v1: 2/9) |
| Tests | 30 unit-test cases (~400k checks), 17 integration tests, all passing |
| Sanitizers | clean under ASan + UBSan and ThreadSanitizer (4-thread search) |
| Bench signature | 1,707,612 nodes (`chess-engine bench`) |

## Features

* **Rules core**: bitboards + mailbox, magic bitboards (embedded multipliers
  re-verified at startup), strictly legal move generation (pin and check
  masks, no make/unmake filtering), incremental Zobrist hashing with canonical
  en-passant, threefold/fifty-move/insufficient-material detection, static
  exchange evaluation, `gives_check`.
* **Search**: iterative deepening, aspiration windows, principal variation
  search, check extension, mate-distance pruning, reverse futility pruning,
  null-move pruning (zugzwang-guarded), internal iterative reduction,
  late-move reductions with re-search, late-move/futility/SEE pruning,
  quiescence search with SEE filtering and full check evasions, killer moves,
  history heuristic with gravity.
* **Parallel search**: Lazy SMP over a lockless transposition table that uses
  relaxed atomics, so it is free of data races and clean under ThreadSanitizer.
* **Evaluation**: tapered (middlegame/endgame) evaluation with material,
  piece-square tables, mobility, pawn structure, king safety and more: 485
  (mg, eg) weight pairs fitted by `tools/tune` (weights with too little
  data keep their prior value).
* **UCI**: the search runs on its own thread, so `stop`, `isready` and `quit`
  work mid-search. Options: `Hash`, `Threads`, `Move Overhead`, `Clear Hash`.
* **Tooling**: `bench`, `perft`, `perft-suite`, `datagen` (self-play training
  data), `openings` (balanced opening book), `serve` (local HTTP API for the
  browser demo), plus `tools/match` (engine-vs-engine matches with SPRT) and
  `tools/tune` (Texel tuner).

## Building

Requires a C++20 compiler (Clang 14+ or GCC 11+) and `make`. No other dependencies.

```bash
make                       # build/release/chess-engine
make test                  # unit + UCI + HTTP integration tests (needs python3)
make perft                 # full perft suite (640M leaf nodes, ~1-2 s)
make bench                 # deterministic search benchmark
make sanitize              # unit tests + bench under ASan/UBSan and TSan
make tools                 # build/release/match and build/release/tune
make frontend-test         # perft of the web demo's JS move generator (needs node)
```

`make CONFIG=debug|asan|tsan` selects other build configurations, and
`make ARCH=native` tunes for the host CPU.

## Using it

The engine speaks UCI on stdin/stdout. Point any UCI GUI (Cute Chess, Arena,
BanksiaGUI, En Croissant, ...) at `build/release/chess-engine`, or talk to it directly:

```text
$ build/release/chess-engine
position startpos moves e2e4
go depth 10
info depth 1 seldepth 3 multipv 1 score cp -10 nodes 28 nps 28000 hashfull 0 time 0 pv d7d5
...
info depth 10 seldepth 19 multipv 1 score cp -28 nodes 16050 nps 1337500 hashfull 11 time 12 pv d7d5 e4d5 g8f6 ...
bestmove d7d5 ponder e4d5
```

Extra console commands: `d` (show the board), `eval` (static evaluation),
`perft <depth>` (divided perft).

### Browser demo

```bash
build/release/chess-engine serve           # engine API on http://127.0.0.1:8765, localhost only
python3 -m http.server -d frontend 8000    # then open http://localhost:8000
```

The page sends the whole game (`startpos moves ...`) to the engine, so it sees
repetitions. The server checks every move, caps think time and request size,
and returns CORS headers only to local origins.

## Measuring strength

```bash
make tools
build/release/match \
  --engine1 build/release/chess-engine --name1 new \
  --engine2 path/to/other-engine      --name2 base \
  --tc 8+0.08 --concurrency 8 --openings tools/match/openings.epd \
  --sprt 0 10 --games 4000
```

Games are played in colour-reversed pairs from 1,000 balanced openings
(generated by `chess-engine openings`). Results are reported as Elo with a
95% interval from pentanomial (game-pair) statistics. With `--sprt elo0 elo1`,
the run stops as soon as the sequential probability ratio test accepts either
hypothesis. Rules are adjudicated by the engine's own verified move generator,
so the runner can referee any two UCI engines.

## Tuning the evaluation

```bash
build/release/chess-engine datagen --games 40000 --nodes 5000 --threads 10 --out data/games.txt
build/release/tune --data data/games.txt --out src/engine/eval_params.cpp
make && build/release/match ... --sprt 0 10      # accept only if it gains Elo
```

`datagen` games are deterministic per seed. The tuner fits Texel's logistic
model with Adam, using exact gradients over the engine's real evaluation (a
unit test checks that the tuner's linear model and the evaluation agree on
every position). It keeps the weights from the best epoch on a held-out
validation split, and leaves weights with too little phase-weighted support at
their prior value instead of fitting them to noise.

## Repository layout

```
src/core/       rules: types, bitboards, position, legal move generation
src/engine/     evaluation (+ generated weights), transposition table, search
src/uci/        UCI protocol
src/app/        bench / perft / datagen / openings / serve subcommands
tools/match/    match runner with SPRT, opening suite
tools/tune/     Texel tuner
tests/          unit tests, perft data, UCI / HTTP / frontend integration tests
frontend/       single-page browser demo
docs/           design notes, audit of v1, measured results
```

## Limitations

* **No absolute rating.** No calibrated reference engine (e.g. Stockfish with
  `UCI_Elo`) was available on the development machine, so every Elo figure
  here is *relative*: between versions of this engine, or against version 1.
  The "~2000–2160 Elo" figures in the old README were unsupported and have
  been dropped. A calibration run is one command once a reference engine is
  installed (see [docs/RESULTS.md](docs/RESULTS.md)).
* **Hand-crafted evaluation.** The weights are fitted to data, but the feature
  set is classical. The tuning data is self-play at 5,000 nodes per move,
  with decided games adjudicated early. As a result, material advantages are
  worth noticeably more in the middlegame than in the endgame (a queen: +1303
  vs +848 cp), which discourages simplifying when ahead. A second tuning round
  made this worse and was rejected ([RESULTS.md §8](docs/RESULTS.md)).
  Regenerating data without adjudication is the planned fix. An NNUE network
  would be the next step in strength, but that is a different project.
* **Standard chess only**: no Chess960, pondering, MultiPV, or endgame
  tablebases.
* `tools/match` and `serve` use POSIX APIs (fork/exec, sockets). The engine
  itself is portable C++20, but only macOS was tested locally. CI for Linux
  GCC/Clang is configured but has not been run from this repository yet.
* Lazy SMP is simple, and its scaling was only measured up to 4 threads.
