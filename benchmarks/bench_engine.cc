// Engine-path throughput: the combat engine with no Python in the loop.
//
// WHY THIS EXISTS (ROB-81). The previous "env throughput" number measured its
// own benchmark more than the environment. A per-step Python list comprehension
// over the mask cost ~8.6us against ~1.1us of actual engine work, so ~87% of
// the published figure was harness. Worse, it moved the wrong way: Stage 4a
// made the engine 2.3x faster while the end-to-end number FELL 25%, purely
// because the action space grew and the scan scales with it. A benchmark that
// moves opposite to the thing it measures cannot support a published claim.
//
// This binary is the control: same engine calls a trained agent makes, zero
// binding overhead, so it bounds what the Python path could ever reach.
//
// A real agent does NOT scan the mask for legal indices — MaskablePPO feeds the
// mask straight into the policy. Action selection here is therefore incidental
// cost, so it is reported separately rather than folded into the headline.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <numeric>
#include <random>
#include <vector>

#include "combat_env.h"
#include "combat_state.h"
#include "encounter.h"
#include "turn_loop.h"

using namespace minispire;
using Clock = std::chrono::steady_clock;

namespace {

double seconds_since(Clock::time_point t0) {
  return std::chrono::duration<double>(Clock::now() - t0).count();
}

struct Result {
  double steps_per_sec = 0;
  double mask_us = 0;
  double step_us = 0;
  double select_us = 0;
  double reset_us = 0;
  double mean_episode_len = 0;
  long long steps = 0;
  long long episodes = 0;
};

// One timed sweep.
//
// Drives CombatEnv, NOT raw CombatState — that distinction is the whole reason
// this file exists and the first draft got it wrong. CombatEnv::step also
// recomputes the observation and the mask, which is most of the per-step work
// an RL loop actually pays for. Timing valid_actions + apply_action alone
// measured strictly less work than the env path and made the binding look ~4.7x
// more expensive than it is. Same work, no Python: that is the useful control.
Result run(long long target_steps, uint32_t seed0) {
  std::mt19937 rng(12345);
  Result r;
  double mask_s = 0, step_s = 0, select_s = 0, reset_s = 0;
  std::vector<int> episode_lengths;

  uint32_t seed = seed0;
  CombatEnv env(0.0f, EncounterPool::Weak);
  auto t_reset = Clock::now();
  env.reset(seed++);
  reset_s += seconds_since(t_reset);
  r.episodes++;
  int ep_len = 0;

  const auto t_all = Clock::now();
  while (r.steps < target_steps) {
    // The mask is already computed by step()/reset(); reading it is what a
    // consumer does, so that read is the "mask" cost here.
    auto t0 = Clock::now();
    const std::vector<uint8_t>& mask = env.action_mask();
    mask_s += seconds_since(t0);

    // Reservoir-sample a legal index in one pass. Incidental to the engine, so
    // timed separately — an agent gets its action from the policy instead.
    t0 = Clock::now();
    int chosen = -1, seen = 0;
    for (std::size_t a = 0; a < mask.size(); ++a) {
      if (!mask[a]) continue;
      if (rng() % static_cast<uint32_t>(++seen) == 0) chosen = static_cast<int>(a);
    }
    select_s += seconds_since(t0);

    if (chosen < 0) {  // no legal action: treat as a terminal state
      episode_lengths.push_back(ep_len);
      ep_len = 0;
      t_reset = Clock::now();
      env.reset(seed++);
      reset_s += seconds_since(t_reset);
      r.episodes++;
      continue;
    }

    t0 = Clock::now();
    env.step(chosen);
    step_s += seconds_since(t0);
    r.steps++;
    ep_len++;

    if (env.outcome() != Outcome::InProgress) {
      episode_lengths.push_back(ep_len);
      ep_len = 0;
      t_reset = Clock::now();
      env.reset(seed++);
      reset_s += seconds_since(t_reset);
      r.episodes++;
    }
  }
  const double wall = seconds_since(t_all);

  r.steps_per_sec = static_cast<double>(r.steps) / wall;
  r.mask_us = mask_s * 1e6 / static_cast<double>(r.steps);
  r.step_us = step_s * 1e6 / static_cast<double>(r.steps);
  r.select_us = select_s * 1e6 / static_cast<double>(r.steps);
  r.reset_us = reset_s * 1e6 / static_cast<double>(r.episodes);
  if (!episode_lengths.empty()) {
    r.mean_episode_len =
        std::accumulate(episode_lengths.begin(), episode_lengths.end(), 0.0) /
        static_cast<double>(episode_lengths.size());
  }
  return r;
}

}  // namespace

int main(int argc, char** argv) {
  long long steps = 300000;
  if (argc > 1) steps = std::atoll(argv[1]);

  run(steps / 10, 9000);  // warm up caches and the allocator

  // Median of 5 so a scheduler hiccup cannot set the published number.
  std::vector<Result> trials;
  for (int i = 0; i < 5; ++i) trials.push_back(run(steps, 1000u * (i + 1)));
  std::sort(trials.begin(), trials.end(),
            [](const Result& a, const Result& b) {
              return a.steps_per_sec < b.steps_per_sec;
            });
  const Result& med = trials[trials.size() / 2];

  std::printf("engine_steps_per_sec %.0f\n", med.steps_per_sec);
  std::printf("valid_actions_us %.3f\n", med.mask_us);
  std::printf("apply_action_us %.3f\n", med.step_us);
  std::printf("action_select_us %.3f\n", med.select_us);
  std::printf("reset_us %.2f\n", med.reset_us);
  std::printf("mean_episode_len %.1f\n", med.mean_episode_len);
  std::printf("steps %lld\n", med.steps);
  std::printf("spread_pct %.1f\n",
              100.0 * (trials.back().steps_per_sec - trials.front().steps_per_sec) /
                  med.steps_per_sec);
  return 0;
}
