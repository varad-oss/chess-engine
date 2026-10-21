// tune: fits the evaluation weights to game results (Texel's tuning method).
//
// The evaluation is linear in its weights (see src/engine/eval.h), so for each
// training position we store the sparse feature vector produced by
// eval::trace() once, and then minimise the mean squared error
//     E = mean( (target - sigmoid(K * eval(w) / 400))^2 )
// with Adam over all weights simultaneously, using exact analytic gradients.
// K is first fitted so that the *initial* weights explain the data as well as
// possible, which keeps the weights on the centipawn scale. A held-out
// validation split guards against overfitting; the weights of the best
// validation epoch are written out as C++ (src/engine/eval_params.cpp).
//
// Input lines (as written by `chess-engine datagen`):  <fen> | <score> | <result>
// where result is 1, 0.5 or 0 from White's point of view.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "core/position.h"
#include "core/rng.h"
#include "engine/eval.h"

using namespace chess;

namespace {

constexpr int N = eval::idx::Count;

struct Feature {
    std::uint16_t index;
    std::int16_t count;  // white count - black count
};

struct Sample {
    std::uint32_t first;  // into the shared feature array
    std::uint16_t size;
    std::uint8_t phase;
    std::uint8_t scale;
    float target;
};

struct Dataset {
    std::vector<Feature> features;
    std::vector<Sample> samples;
};

struct Weights {
    double mg[N];
    double eg[N];
};

struct Options {
    std::vector<std::string> data;
    std::string out = "src/engine/eval_params.cpp";
    int epochs = 3000;
    double lr = 1.0;
    double lambda = 1.0;  // target = lambda * result + (1 - lambda) * sigmoid(search score)
    double validation = 0.1;
    int threads = int(std::max(1u, std::thread::hardware_concurrency()));
    long long max_positions = 0;
    double min_samples = 3000;  // weights with fewer effective samples keep their prior value
    std::string comment;
    bool canonicalize_only = false;
};

double sigmoid(double k, double eval) { return 1.0 / (1.0 + std::pow(10.0, -k * eval / 400.0)); }

double evaluate(const Weights& w, const Dataset& d, const Sample& s) {
    double mg = 0, eg = 0;
    for (std::uint32_t i = s.first; i < s.first + s.size; ++i) {
        mg += d.features[i].count * w.mg[d.features[i].index];
        eg += d.features[i].count * w.eg[d.features[i].index];
    }
    return (mg * s.phase + eg * (eval::PHASE_MAX - s.phase) * s.scale / eval::SCALE_NORMAL) / eval::PHASE_MAX;
}

template <typename F>
void parallel_for(size_t n, int threads, F&& body) {
    std::vector<std::thread> pool;
    const size_t chunk = (n + size_t(threads) - 1) / size_t(threads);
    for (int t = 0; t < threads; ++t) {
        const size_t begin = size_t(t) * chunk, end = std::min(n, begin + chunk);
        if (begin >= end) break;
        pool.emplace_back([&body, t, begin, end] { body(t, begin, end); });
    }
    for (auto& th : pool) th.join();
}

double loss(const Weights& w, const Dataset& d, double k, int threads) {
    std::vector<double> partial(size_t(threads), 0.0);
    parallel_for(d.samples.size(), threads, [&](int t, size_t b, size_t e) {
        double sum = 0;
        for (size_t i = b; i < e; ++i) {
            const double err = double(d.samples[i].target) - sigmoid(k, evaluate(w, d, d.samples[i]));
            sum += err * err;
        }
        partial[size_t(t)] = sum;
    });
    double total = 0;
    for (double p : partial) total += p;
    return total / double(d.samples.size());
}

void gradient(const Weights& w, const Dataset& d, double k, int threads, Weights& grad) {
    std::vector<Weights> partial(static_cast<size_t>(threads));  // value-initialised: all zero
    parallel_for(d.samples.size(), threads, [&](int t, size_t b, size_t e) {
        Weights& g = partial[size_t(t)];
        for (size_t i = b; i < e; ++i) {
            const Sample& s = d.samples[i];
            const double p = sigmoid(k, evaluate(w, d, s));
            // d/d eval of (target - p)^2, with dp/deval = p(1-p) * ln(10) * k / 400.
            const double base = -2.0 * (double(s.target) - p) * p * (1 - p) * std::log(10.0) * k / 400.0;
            const double mg_factor = base * s.phase / eval::PHASE_MAX;
            const double eg_factor =
                base * (eval::PHASE_MAX - s.phase) * s.scale / eval::SCALE_NORMAL / eval::PHASE_MAX;
            for (std::uint32_t j = s.first; j < s.first + s.size; ++j) {
                g.mg[d.features[j].index] += mg_factor * d.features[j].count;
                g.eg[d.features[j].index] += eg_factor * d.features[j].count;
            }
        }
    });
    for (int i = 0; i < N; ++i) {
        grad.mg[i] = grad.eg[i] = 0;
        for (const auto& p : partial) grad.mg[i] += p.mg[i], grad.eg[i] += p.eg[i];
        grad.mg[i] /= double(d.samples.size());
        grad.eg[i] /= double(d.samples.size());
    }
}

// Golden-section search for the K that minimises the loss of the initial weights.
double fit_k(const Weights& w, const Dataset& d, int threads) {
    double lo = 0.1, hi = 4.0;
    const double phi = (std::sqrt(5.0) - 1) / 2;
    double a = hi - phi * (hi - lo), b = lo + phi * (hi - lo);
    double fa = loss(w, d, a, threads), fb = loss(w, d, b, threads);
    for (int i = 0; i < 40; ++i) {
        if (fa < fb) {
            hi = b, b = a, fb = fa, a = hi - phi * (hi - lo), fa = loss(w, d, a, threads);
        } else {
            lo = a, a = b, fa = fb, b = lo + phi * (hi - lo), fb = loss(w, d, b, threads);
        }
    }
    return (lo + hi) / 2;
}

bool load(const Options& opt, Dataset& train, Dataset& valid) {
    Rng rng(12345);
    long long lines = 0, skipped = 0;
    std::vector<std::pair<Feature, int>> scratch;
    for (const std::string& path : opt.data) {
        std::ifstream in(path);
        if (!in) {
            std::cerr << "cannot open " << path << '\n';
            return false;
        }
        std::string line;
        while (std::getline(in, line)) {
            if (opt.max_positions && lines >= opt.max_positions) break;
            const size_t bar1 = line.find('|'), bar2 = line.rfind('|');
            if (bar1 == std::string::npos || bar2 == bar1) {
                ++skipped;
                continue;
            }
            Position pos;
            if (!pos.set_fen(line.substr(0, bar1))) {
                ++skipped;
                continue;
            }
            const double result = std::stod(line.substr(bar2 + 1));
            const double score = std::stod(line.substr(bar1 + 1, bar2 - bar1 - 1));
            eval::Trace t;
            eval::trace(pos, t);

            Dataset& d = rng.below(1000) < std::uint64_t(opt.validation * 1000) ? valid : train;
            Sample s;
            s.first = std::uint32_t(d.features.size());
            for (int i = 0; i < N; ++i) {
                const int c = t.coef[i][WHITE] - t.coef[i][BLACK];
                if (c) d.features.push_back({std::uint16_t(i), std::int16_t(c)});
            }
            s.size = std::uint16_t(d.features.size() - s.first);
            s.phase = std::uint8_t(t.phase);
            s.scale = std::uint8_t(t.eg_scale);
            // Search scores are blended in with a fixed K of 1 (centipawn scale).
            s.target = float(opt.lambda * result + (1 - opt.lambda) * sigmoid(1.0, score));
            d.samples.push_back(s);
            if (++lines % 1000000 == 0) std::cerr << "loaded " << lines << " positions\n";
        }
    }
    std::cerr << "loaded " << train.samples.size() << " training and " << valid.samples.size()
              << " validation positions (" << skipped << " lines skipped)\n";
    return !train.samples.empty() && !valid.samples.empty();
}

struct Section {
    const char* name;
    int start;
    int size;
};

const Section SECTIONS[] = {
    {"Material: pawn, knight, bishop, rook, queen, king (unused)", eval::idx::Material, 6},
    {"Piece-square table: pawn (a1..h8, own point of view)", eval::idx::Pst + 0 * 64, 64},
    {"Piece-square table: knight (a1..h8, own point of view)", eval::idx::Pst + 1 * 64, 64},
    {"Piece-square table: bishop (a1..h8, own point of view)", eval::idx::Pst + 2 * 64, 64},
    {"Piece-square table: rook (a1..h8, own point of view)", eval::idx::Pst + 3 * 64, 64},
    {"Piece-square table: queen (a1..h8, own point of view)", eval::idx::Pst + 4 * 64, 64},
    {"Piece-square table: king (a1..h8, own point of view)", eval::idx::Pst + 5 * 64, 64},
    {"Knight mobility [0..8]", eval::idx::KnightMobility, 9},
    {"Bishop mobility [0..13]", eval::idx::BishopMobility, 14},
    {"Rook mobility [0..14]", eval::idx::RookMobility, 15},
    {"Queen mobility [0..27]", eval::idx::QueenMobility, 28},
    {"Passed pawn [relative rank]", eval::idx::PassedPawn, 8},
    {"Isolated pawn", eval::idx::IsolatedPawn, 1},
    {"Doubled pawn", eval::idx::DoubledPawn, 1},
    {"Connected pawn [relative rank]", eval::idx::ConnectedPawn, 8},
    {"Bishop pair", eval::idx::BishopPair, 1},
    {"Rook on open file", eval::idx::RookOpenFile, 1},
    {"Rook on semi-open file", eval::idx::RookSemiOpenFile, 1},
    {"King pawn shield [one rank ahead, two ranks ahead]", eval::idx::KingShield, 2},
    {"King zone attacks [knight, bishop, rook, queen]", eval::idx::KingZoneAttack, 4},
    {"Pawn threat on a piece", eval::idx::PawnThreat, 1},
    {"Tempo", eval::idx::Tempo, 1},
};

// Rounds the weights and rewrites them in a canonical form that evaluates
// every position *identically* (integer shifts only):
//  * every piece of a type contributes exactly one material weight, one
//    piece-square weight and (N..Q) one mobility weight, so only their sum is
//    determined by the data; the mean of each PST and mobility table is
//    moved into the material weight;
//  * the king PST is centred: a constant added to it cancels between sides.
// Without this the fit drifts along these directions and the raw numbers,
// e.g. "queen = 2406", mean nothing on their own.
void canonicalize(Weights& w) {
    for (int i = 0; i < N; ++i) w.mg[i] = double(std::lround(w.mg[i])), w.eg[i] = double(std::lround(w.eg[i]));
    auto shift_into = [&](int first, int count, int material_index) {
        for (double* half : {w.mg, w.eg}) {
            double sum = 0;
            for (int i = first; i < first + count; ++i) sum += half[i];
            const double c = double(std::lround(sum / count));
            for (int i = first; i < first + count; ++i) half[i] -= c;
            if (material_index >= 0) half[material_index] += c;
        }
    };
    using namespace eval::idx;
    // Pawn PST: only ranks 2..7 are reachable (indices 8..55).
    shift_into(Pst + 8, 48, Material + PAWN);
    for (PieceType pt : {KNIGHT, BISHOP, ROOK, QUEEN}) shift_into(Pst + 64 * pt, 64, Material + pt);
    shift_into(Pst + 64 * KING, 64, -1);
    shift_into(KnightMobility, 9, Material + KNIGHT);
    shift_into(BishopMobility, 14, Material + BISHOP);
    shift_into(RookMobility, 15, Material + ROOK);
    shift_into(QueenMobility, 28, Material + QUEEN);
}

bool write_params(const Options& opt, Weights w, const std::string& provenance) {
    canonicalize(w);
    int covered = 0;
    for (const Section& s : SECTIONS) covered += s.size;
    if (covered != N) {
        std::cerr << "internal error: section table does not match eval::idx layout\n";
        return false;
    }
    std::ofstream out(opt.out);
    if (!out) return false;
    out << "// Evaluation weights, indexed by the layout in eval.h (namespace idx).\n"
        << "// GENERATED by tools/tune -- regenerate rather than editing by hand.\n";
    std::istringstream lines(provenance);
    for (std::string l; std::getline(lines, l);) out << "// " << l << '\n';
    out << "#include \"eval.h\"\n\nnamespace chess::eval {\n\nconst Score Params[idx::Count] = {\n";
    for (const Section& s : SECTIONS) {
        out << "    // " << s.name << '\n';
        for (int i = 0; i < s.size; i += 8) {
            out << "   ";
            for (int j = i; j < std::min(s.size, i + 8); ++j)
                out << " {" << std::lround(w.mg[s.start + j]) << ", " << std::lround(w.eg[s.start + j]) << "},";
            out << '\n';
        }
    }
    out << "};\n\n}  // namespace chess::eval\n";
    return bool(out);
}

}  // namespace

int main(int argc, char** argv) {
    chess::init();
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("missing value for " + a);
            return argv[++i];
        };
        try {
            if (a == "--data") {
                std::istringstream list(next());
                for (std::string p; std::getline(list, p, ',');) opt.data.push_back(p);
            } else if (a == "--out") opt.out = next();
            else if (a == "--epochs") opt.epochs = std::stoi(next());
            else if (a == "--lr") opt.lr = std::stod(next());
            else if (a == "--lambda") opt.lambda = std::stod(next());
            else if (a == "--validation") opt.validation = std::stod(next());
            else if (a == "--threads") opt.threads = std::max(1, std::stoi(next()));
            else if (a == "--max-positions") opt.max_positions = std::stoll(next());
            else if (a == "--min-samples") opt.min_samples = std::stod(next());
            else if (a == "--comment") opt.comment = next();
            else if (a == "--canonicalize") opt.canonicalize_only = true;
            else throw std::runtime_error("unknown option " + a);
        } catch (const std::exception& e) {
            std::cerr << "error: " << e.what() << "\nusage: tune --data FILE[,FILE] [--out PATH] [--epochs N] "
                      << "[--lr X] [--lambda L] [--validation F] [--threads N] [--max-positions N] [--min-samples N] [--comment TEXT] [--canonicalize]\n";
            return 2;
        }
    }
    if (opt.canonicalize_only) {
        // Rewrite the compiled-in weights in canonical form, keeping the
        // provenance comment lines of the current file.
        Weights w;
        for (int i = 0; i < N; ++i) w.mg[i] = eval::Params[i].mg, w.eg[i] = eval::Params[i].eg;
        std::ifstream current(opt.out);
        std::string provenance, line;
        while (std::getline(current, line) && line.rfind("//", 0) == 0)
            if (line.find("GENERATED") == std::string::npos && line.find("indexed by the layout") == std::string::npos)
                provenance += (provenance.empty() ? "" : "\n") + line.substr(std::min<size_t>(3, line.size()));
        return write_params(opt, w, provenance) ? 0 : 1;
    }
    if (opt.data.empty()) {
        std::cerr << "usage: tune --data FILE[,FILE] [options]\n";
        return 2;
    }

    Dataset train, valid;
    if (!load(opt, train, valid)) return 1;

    Weights w;
    for (int i = 0; i < N; ++i) w.mg[i] = eval::Params[i].mg, w.eg[i] = eval::Params[i].eg;

    // Effective sample count per weight: a middlegame weight only influences a
    // position in proportion to its phase (and an endgame weight to the rest),
    // so a feature seen almost only in endgames leaves its middlegame weight
    // unconstrained. Such weights would be fitted to noise; they keep their
    // prior value instead.
    Weights support{};
    for (const Sample& s : train.samples)
        for (std::uint32_t j = s.first; j < s.first + s.size; ++j) {
            const Feature& f = train.features[j];
            support.mg[f.index] += std::abs(f.count) * double(s.phase) / eval::PHASE_MAX;
            support.eg[f.index] += std::abs(f.count) * double(eval::PHASE_MAX - s.phase) / eval::PHASE_MAX;
        }
    bool frozen_mg[N], frozen_eg[N];
    int frozen = 0;
    for (int i = 0; i < N; ++i) {
        frozen_mg[i] = support.mg[i] < opt.min_samples;
        frozen_eg[i] = support.eg[i] < opt.min_samples;
        frozen += frozen_mg[i] + frozen_eg[i];
    }
    std::cerr << frozen << " of " << 2 * N << " weights have under " << opt.min_samples
              << " effective samples and keep their prior value\n";
    const double k = fit_k(w, train, opt.threads);
    const double initial_train = loss(w, train, k, opt.threads), initial_valid = loss(w, valid, k, opt.threads);
    std::cerr << std::setprecision(6) << "K = " << k << "  initial loss: train " << initial_train << "  validation "
              << initial_valid << '\n';

    // Adam (Kingma & Ba), full batch.
    Weights m{}, v{}, grad{}, best = w;
    double best_valid = initial_valid;
    int best_epoch = 0;
    const double b1 = 0.9, b2 = 0.999, eps = 1e-8;
    const auto start = std::chrono::steady_clock::now();
    for (int epoch = 1; epoch <= opt.epochs; ++epoch) {
        gradient(w, train, k, opt.threads, grad);
        for (int i = 0; i < N; ++i) {
            if (frozen_mg[i]) grad.mg[i] = 0;
            if (frozen_eg[i]) grad.eg[i] = 0;
            for (auto [p, g, mm, vv] : {std::tuple{&w.mg[i], grad.mg[i], &m.mg[i], &v.mg[i]},
                                        std::tuple{&w.eg[i], grad.eg[i], &m.eg[i], &v.eg[i]}}) {
                *mm = b1 * *mm + (1 - b1) * g;
                *vv = b2 * *vv + (1 - b2) * g * g;
                const double mhat = *mm / (1 - std::pow(b1, epoch)), vhat = *vv / (1 - std::pow(b2, epoch));
                *p -= opt.lr * mhat / (std::sqrt(vhat) + eps);
            }
        }
        if (epoch % 50 == 0 || epoch == opt.epochs) {
            const double tl = loss(w, train, k, opt.threads), vl = loss(w, valid, k, opt.threads);
            if (vl < best_valid) best_valid = vl, best = w, best_epoch = epoch;
            const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            std::cerr << "epoch " << epoch << "  train " << tl << "  validation " << vl << "  (" << int(s) << "s)\n";
            if (epoch - best_epoch >= 500) {
                std::cerr << "validation loss stopped improving; stopping early\n";
                break;
            }
        }
    }

    std::ostringstream provenance;
    provenance << std::setprecision(6) << "Fitted on " << train.samples.size() << " positions ("
               << valid.samples.size() << " held out), K = " << k << ", lambda = " << opt.lambda
               << ", min-samples = " << opt.min_samples << " (" << frozen << " weights kept at prior).\n"
               << "Validation MSE " << initial_valid << " -> " << best_valid << " (epoch " << best_epoch << ").";
    if (!opt.comment.empty()) provenance << "\n" << opt.comment;
    if (!write_params(opt, best, provenance.str())) {
        std::cerr << "failed to write " << opt.out << '\n';
        return 1;
    }
    std::cerr << "wrote " << opt.out << " (validation " << initial_valid << " -> " << best_valid << ")\n";
    return 0;
}
