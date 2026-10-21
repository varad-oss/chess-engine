// Match statistics: Elo estimates and the SPRT log-likelihood ratio from
// pentanomial (game-pair) counts. Header-only so the unit tests can check it.
#pragma once

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

namespace chess::match {

inline double elo_from_score(double s) {
    s = std::clamp(s, 1e-6, 1 - 1e-6);
    return -400.0 * std::log10(1.0 / s - 1.0);
}
inline double score_from_elo(double elo) { return 1.0 / (1.0 + std::pow(10.0, -elo / 400.0)); }

struct Stats {
    int wins = 0, losses = 0, draws = 0;
    int penta[5] = {};  // pair outcomes in half points for engine 1: 0, 0.5, 1, 1.5, 2

    int pairs() const { return penta[0] + penta[1] + penta[2] + penta[3] + penta[4]; }

    // Mean and variance of the per-pair score (normalised to [0,1]).
    void moments(double& mean, double& var) const {
        const double n = pairs();
        mean = var = 0;
        if (n == 0) return;
        for (int k = 0; k < 5; ++k) mean += penta[k] * (k / 4.0) / n;
        for (int k = 0; k < 5; ++k) var += penta[k] * std::pow(k / 4.0 - mean, 2) / n;
    }

    // Generalised SPRT log-likelihood ratio (normal approximation) for the
    // hypotheses elo = elo0 vs elo = elo1 (logistic Elo):
    //   LLR = N (s1 - s0) (2 mean - s0 - s1) / (2 var)
    double llr(double elo0, double elo1) const {
        double mean, var;
        moments(mean, var);
        if (pairs() < 2 || var <= 0) return 0;
        const double s0 = score_from_elo(elo0), s1 = score_from_elo(elo1);
        return pairs() * (s1 - s0) * (2 * mean - s0 - s1) / (2 * var);
    }

    // Elo estimate and the half-width of its 95% confidence interval.
    void elo(double& estimate, double& margin) const {
        double mean, var;
        moments(mean, var);
        const double se = pairs() > 0 ? std::sqrt(var / pairs()) : 0;
        estimate = elo_from_score(mean);
        margin = (elo_from_score(std::min(mean + 1.96 * se, 1.0)) - elo_from_score(std::max(mean - 1.96 * se, 0.0))) / 2;
    }

    std::string summary() const {
        std::ostringstream out;
        const int games = wins + losses + draws;
        out << "Games " << games << ": +" << wins << " -" << losses << " =" << draws;
        if (pairs() > 0) {
            double estimate, margin;
            elo(estimate, margin);
            out << std::fixed << std::setprecision(1) << "  Elo " << estimate << " +/- " << margin
                << " (95%)  Penta [" << penta[0] << ' ' << penta[1] << ' ' << penta[2] << ' ' << penta[3] << ' '
                << penta[4] << ']';
        }
        return out.str();
    }
};

// SPRT decision bounds for error rates alpha (false positive) and beta.
inline double sprt_lower_bound(double alpha, double beta) { return std::log(beta / (1 - alpha)); }
inline double sprt_upper_bound(double alpha, double beta) { return std::log((1 - beta) / alpha); }

}  // namespace chess::match
