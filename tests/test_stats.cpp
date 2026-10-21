// Tests for the match runner's statistics (tools/match/stats.h). Every
// strength claim in the project rests on these formulas.
#include <cmath>

#include "../tools/match/stats.h"
#include "framework.h"

using chess::match::Stats;
using namespace chess::match;

namespace {
bool near(double a, double b, double eps) { return std::fabs(a - b) <= eps; }
}  // namespace

TEST_CASE(elo_score_conversions) {
    CHECK(near(elo_from_score(0.5), 0.0, 1e-9));
    CHECK(near(elo_from_score(0.75), 190.848501, 1e-5));  // 400 * log10(3)
    CHECK(near(elo_from_score(0.25), -190.848501, 1e-5));
    for (double s : {0.1, 0.3, 0.5, 0.64, 0.9}) CHECK(near(score_from_elo(elo_from_score(s)), s, 1e-12));
    CHECK(near(sprt_upper_bound(0.05, 0.05), std::log(19.0), 1e-12));
    CHECK(near(sprt_lower_bound(0.05, 0.05), -std::log(19.0), 1e-12));
}

TEST_CASE(pentanomial_moments_and_llr_match_hand_calculation) {
    // Symmetric outcome: mean 0.5, variance
    //   (10*.25 + 20*.0625 + 0 + 20*.0625 + 10*.25) / 100 = 0.075.
    Stats s;
    const int counts[5] = {10, 20, 40, 20, 10};
    for (int k = 0; k < 5; ++k) s.penta[k] = counts[k];
    double mean, var;
    s.moments(mean, var);
    CHECK(near(mean, 0.5, 1e-12));
    CHECK(near(var, 0.075, 1e-12));
    // LLR = 100 * (s1 - s0) * (2*0.5 - s0 - s1) / (2 * 0.075), with s0 = 0.5
    // and s1 = 1 / (1 + 10^(-10/400)) = 0.5143868...
    const double s1 = 1.0 / (1.0 + std::pow(10.0, -10.0 / 400.0));
    const double expected = 100 * (s1 - 0.5) * (1.0 - 0.5 - s1) / 0.15;
    CHECK(near(s.llr(0, 10), expected, 1e-12));
    CHECK(near(expected, -0.13800, 1e-4));

    double elo, margin;
    s.elo(elo, margin);
    CHECK(near(elo, 0.0, 1e-9));
    CHECK(margin > 0);
}

TEST_CASE(llr_direction_and_degenerate_cases) {
    Stats strong, weak, empty, all_draws;
    const int good[5] = {2, 5, 30, 25, 18};
    for (int k = 0; k < 5; ++k) {
        strong.penta[k] = good[k];
        weak.penta[4 - k] = good[k];  // mirror image: the same games lost
    }
    CHECK(strong.llr(0, 10) > 0);
    CHECK(weak.llr(0, 10) < 0);
    double e1, m1, e2, m2;
    strong.elo(e1, m1);
    weak.elo(e2, m2);
    CHECK(near(e1, -e2, 1e-9));
    CHECK(near(m1, m2, 1e-9));

    CHECK_EQ(empty.llr(0, 10), 0.0);  // no data
    all_draws.penta[2] = 50;          // zero variance: no information about direction
    CHECK_EQ(all_draws.llr(0, 10), 0.0);
}
