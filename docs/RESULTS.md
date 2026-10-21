# Measured results

Everything here was measured on one machine: an Apple M5 laptop (4 performance
+ 6 efficiency cores, 16 GB), macOS 26, Apple clang 21, `make` defaults
(`-O3`, no `-march=native`). Raw match logs were not committed (they are
large); the commands below regenerate them.

## How strength is measured

* `tools/match` plays games in **pairs**: the same opening twice with colours
  reversed. Openings come from `tools/match/openings.epd`: 1,000 positions
  after 8 random plies, kept only if a depth-8 search scores them within
  ±80 cp.
* The Elo estimate and its 95% interval use the **pentanomial** model: each
  game pair is one observation with score 0, ½, 1, 1½ or 2. Pairing cancels
  most of the opening's bias, and treating the two games as one observation
  accounts for their correlation.
* **SPRT** (sequential probability ratio test) with logistic-Elo bounds
  `[elo0, elo1]` and α = β = 0.05. The log-likelihood ratio uses the
  normal (GSPRT) approximation `N·(s1−s0)·(2·mean−s0−s1)/(2·var)` over pairs.
  "H1 accepted" means the data favour a gain of at least elo1 over a gain of
  at most elo0, with 5% error rates.
* Adjudication: a game is called once both engines agree that |score| ≥ 1000 cp
  for 3 moves each, or that |score| ≤ 8 cp for 10 moves each after move 40.
  Otherwise games end by the rules (checkmate, stalemate, threefold, fifty
  moves, insufficient material) or by time forfeit/illegal move.
* Concurrency 8 on a 4P+6E machine means some games run on efficiency cores.
  That adds noise but should not bias the result: which game lands on which
  core is unrelated to the engines, and colours and openings are balanced.
  For the same reason, the absolute time controls below correspond to longer
  ones on a desktop CPU.

## 1. Move generation correctness

`make perft` runs `tests/data/perft.epd`: the six standard positions from the
Chess Programming Wiki plus 15 edge-case positions (en-passant pins, castling
through and into check, promotions out of and into check, self-stalemate),
51 (position, depth) entries in total, to full recorded depth.

```
51 passed, 0 failed; 640240179 leaf nodes in 1.19 s (536 Mnps)
```

The frontend's JavaScript move generator passes the same suite up to 100k
nodes per entry (25/25 entries; `make frontend-test`).

## 2. Perft speed vs. the original engine

Same machine, best of two runs. "Like-for-like" makes and unmakes every leaf
move, as the original does; "bulk" counts the last ply from the legal move
list, which only a legal generator can do.

| position | original | new, like-for-like | new, bulk counting |
| --- | --- | --- | --- |
| start, depth 6 (119,060,324) | 4.05 s (29 Mnps) | 1.33 s (90 Mnps), **3.0x** | 0.23 s (512 Mnps), **17.5x** |
| Kiwipete, depth 5 (193,690,690) | 5.31 s (36 Mnps) | 2.24 s (86 Mnps), **2.4x** | 0.34 s (565 Mnps), **15.5x** |

## 3. Protocol conformance

`tests/integration/test_uci.py` (9 tests) drives the real binary over pipes.
The new engine passes 9/9. The original engine passes 2/9: it fails the
handshake (no options), `stop` during `go infinite`, `quit` during a search,
malformed input (it crashes), low-clock time management, mate-score output,
and `go nodes`.

## 4. Evaluation tuning

Data: `chess-engine datagen --games 40000 --nodes 5000 --threads 10 --seed 1`
with the round-0 (hand-set) weights produced 3,599,616 quiet positions in
about 16 minutes. Of those positions, 38.5% are labelled White win, 34.0% draw
and 27.5% Black win.
Tuning:

```
tune --data round0.txt --epochs 3000 --min-samples 3000
3239352 training and 360264 validation positions
142 of 970 weights have under 3000 effective samples and keep their prior value
K = 1.19534  validation MSE 0.0821637 -> 0.0791113 (best epoch, then early stop)
```

Without the minimum-support rule, the fit was equally good on the validation
set but produced absurd values for rarely-exercised weights (a middlegame
king on its eighth rank worth +409 cp). Validation loss cannot see this,
because those weights hardly influence any position. That is why the rule
exists.

Strength, tuned vs. round-0 weights (identical search), 8+0.08, concurrency 8:

```
Final: Games 209: +121 -52 =36  Elo 117.9 +/- 41.0 (95%)  Penta [6 3 41 25 29]
SPRT [0, 10]: LLR 3.15 -> H1 accepted
```

## 5. Lazy SMP

`bench 18 <threads> 64` (30 positions to depth 18, 64 MB hash):

| threads | time | nodes/second | time-to-depth speedup |
| --- | --- | --- | --- |
| 1 | 3.63 s | 3.36 M | 1.00x |
| 4 | 2.13 s | 10.84 M | 1.70x |

Time-to-depth understates Lazy SMP: the helpers' extra nodes also widen the
search at each depth, and that does not show up in this number.

## 6. Sanitizers

`make sanitize`: unit tests and a bench under AddressSanitizer +
UndefinedBehaviorSanitizer, and under ThreadSanitizer with a 4-thread bench.
All runs are clean (no reports).

## 7. A rejected change: continuation history

Continuation history (scoring quiet moves by how well the same piece-to-square
move worked as a reply to the moves one and two plies earlier) is a standard
technique in strong engines. It was implemented and tested before merging,
with the rule fixed in advance: merge only if the SPRT accepts H1.

```
conthist vs tuned-r1, 4+0.04, concurrency 8, SPRT [0, 10], capped at 2000 games
Final: Games 2000: +571 -528 =901  Elo 7.5 +/- 10.5 (95%)  Penta [54 235 385 266 60]
SPRT: LLR 0.86 -> inconclusive
```

The point estimate is positive, but the test did not establish a gain of the
size targeted within the budget, so the change was **not merged**. Restarting
the test or extending the cap after seeing the result would bias the outcome
(optional stopping). The implementation is ~40 lines; a longer test, or one at
a slower time control where better move ordering matters more, is listed as
future work.

## 8. A rejected tuning round

A second tuning round used 7.2M positions (round-0 data plus 40,000 games by
the tuned engine). Validation loss improved (0.0846 → 0.0809), but the result
failed a sanity check on simple material situations (static evaluation, side
to move up the material):

| position | round 1 (shipped) | round 2 | round 2, λ = 0.5 |
| --- | --- | --- | --- |
| start position, Black's queen removed | +1303 | +2317 | +1540 |
| K+Q+4P vs K+4P | +848 | +277 | +597 |
| K+Q vs K | +884 | +464 | +644 |

The same material advantage is worth eight times more in the middlegame than
in the endgame, so the engine would refuse to simplify when winning. Two
causes reinforce each other. `datagen` adjudicates games once both sides see
±1500 cp, so decided endgames are rare in the data. And Texel's sigmoid
saturates, so the loss barely changes once a position is clearly won, which
leaves large middlegame weights unconstrained. Blending search scores into the
targets (λ = 0.5) helped but did not fix it. Round 2 was not adopted. Round 1
has the same tendency to a smaller degree.

Fix, not yet done: generate data without win adjudication, and add
regularisation toward the prior.

## 9. Version 2 vs. version 1

The final engine against the original built from commit `0731f51`, 200 games
each, same openings:

| condition | v2 result | score |
| --- | --- | --- |
| equal time, 8+0.08 | +200 −0 =0 | 100% |
| 10:1 time odds for v1 (v2 1+0.01, v1 10+0.1) | +193 −1 =6 | 98% |

At equal time, 120 games ended by adjudication (both engines agreed on a
±1000 cp score) and 80 by checkmate. Neither match had a time forfeit or an
illegal move. A 100% score has no finite Elo estimate, and pentanomial
estimates at 98% are unreliable (the runner reports 676 ± 172). The defensible
summary is "several hundred Elo stronger, even at 10x less thinking time".
No absolute rating is claimed (see the README's limitations).

## Bench signature

`chess-engine bench` (30 positions, depth 13, 1 thread, 16 MB) searches
**1,707,612 nodes** in this version, at about 3.6M nodes/s on this machine. Any
change to that count means the search behaves differently, so it is a cheap
check that a refactor was purely mechanical. It held across the node-counter
change, the revert of the rejected continuation history, and the canonical
rewrite of the weights.

## Not measured: absolute rating

No reference engine with a calibrated strength limiter was installed on the
development machine, so no Elo-on-a-rating-list number is claimed. With
Stockfish installed, a calibration run is:

```bash
build/release/match --engine1 build/release/chess-engine --name1 v2 \
  --engine2 stockfish --name2 sf2000 \
  --option2 UCI_LimitStrength=true --option2 UCI_Elo=2000 \
  --tc 10+0.1 --concurrency 4 --games 400 --openings tools/match/openings.epd
```

Repeat it with a few `UCI_Elo` values to bracket the 50% point. Stockfish's
limiter is calibrated against CCRL-like conditions, so even that number would
only be approximate.
