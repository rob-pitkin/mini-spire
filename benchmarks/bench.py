"""Throughput benchmark for mini-spire (ROB-81).

Reports two numbers that are routinely conflated:

  engine path   the C++ combat engine with no Python in the loop
  env path      the same work through the pybind11 Gymnasium env

The distinction is the point of this script. The figure published before ROB-81
measured its own harness more than the environment: a per-step Python list
comprehension over the action mask cost roughly eight times the engine work it
was supposed to be timing. It also moved the wrong way — the engine got 2.3x
faster while the reported number fell 25%, because the action space grew and the
scan scaled with it.

So the env path here samples from the mask with numpy and never builds a Python
list, and the engine path is a separate Release binary (`minispire_bench`) whose
number bounds what the binding layer could ever reach.

A trained agent does not scan the mask at all — MaskablePPO passes it straight
into the policy — so action selection is reported separately from stepping
rather than folded into the headline.

    cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
    cmake --build build-release --target minispire_bench
    uv run python benchmarks/bench.py
"""

from __future__ import annotations

import argparse
import json
import pathlib
import platform
import statistics
import subprocess
import sys
import time

import numpy as np

from minispire._core import CombatEnv, EncounterPool, single_enemy_fixture_env
from minispire.env import MinispireEnv

ENGINE_BIN = pathlib.Path("build-release/minispire_bench")


def _obs_of(result):
    """reset() returns either obs or (obs, info) depending on the surface."""
    return result[0] if isinstance(result, tuple) else result


def run_engine(steps: int) -> dict | None:
    """Shell out to the Release C++ binary and parse its key/value output."""
    if not ENGINE_BIN.exists():
        return None
    out = subprocess.run([str(ENGINE_BIN), str(steps)],
                         capture_output=True, text=True, check=True).stdout
    parsed = {}
    for line in out.strip().splitlines():
        key, _, value = line.partition(" ")
        parsed[key] = float(value)
    return parsed


def bench_env(n_envs: int, steps: int, trials: int = 3) -> dict:
    """Step `n_envs` environments round-robin through the Python API.

    Sequential, not parallel: ROB-57 (thread-safe CombatEnv) is what makes real
    vectorisation possible. What this measures is how well per-step Python
    overhead amortises as the batch grows, which is the honest claim until then.
    """
    rates, sel_share = [], []
    for t in range(trials):
        envs = [MinispireEnv(pool=EncounterPool.Weak) for _ in range(n_envs)]
        for i, e in enumerate(envs):
            e.reset(seed=1000 * t + i)
        rng = np.random.default_rng(7)

        done_count, select_s, n = 0, 0.0, 0
        start = time.perf_counter()
        while n < steps:
            for i, env in enumerate(envs):
                mask = env.action_masks()
                t0 = time.perf_counter()
                legal = np.flatnonzero(mask)
                # integers() rather than random.choice(): choice() copies and
                # validates its input, which costs more than the engine step.
                action = int(legal[rng.integers(legal.size)])
                select_s += time.perf_counter() - t0
                _, _, terminated, truncated, _ = env.step(action)
                n += 1
                if terminated or truncated:
                    # int(): Gymnasium rejects numpy integer seeds.
                    env.reset(seed=int(rng.integers(1 << 30)))
                    done_count += 1
                if n >= steps:
                    break
        wall = time.perf_counter() - start
        rates.append(n / wall)
        sel_share.append(select_s / wall)

    return {
        "steps_per_sec": statistics.median(rates),
        "select_share": statistics.median(sel_share),
        "episodes": done_count,
    }


def bench_reset(trials: int = 2000) -> float:
    """Median reset latency in microseconds, through the Python surface."""
    env = MinispireEnv(pool=EncounterPool.Weak)
    samples = []
    for i in range(trials):
        t0 = time.perf_counter()
        env.reset(seed=i)
        samples.append((time.perf_counter() - t0) * 1e6)
    return statistics.median(samples)


def bench_episode_length(episodes: int = 500) -> float:
    env = MinispireEnv(pool=EncounterPool.Weak)
    rng = np.random.default_rng(11)
    lengths = []
    for i in range(episodes):
        env.reset(seed=i)
        steps, done = 0, False
        while not done:
            legal = np.flatnonzero(env.action_masks())
            _, _, terminated, truncated, _ = env.step(
                int(legal[rng.integers(legal.size)]))
            steps += 1
            done = terminated or truncated
        lengths.append(steps)
    return statistics.mean(lengths)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--steps", type=int, default=200_000)
    ap.add_argument("--json", help="also write raw results here")
    args = ap.parse_args()

    engine = run_engine(args.steps)
    batches = [1, 8, 32, 256]
    env_results = {n: bench_env(n, args.steps) for n in batches}
    reset_us = bench_reset()
    ep_len = bench_episode_length()

    single = env_results[1]["steps_per_sec"]

    print()
    print(f"mini-spire throughput   OBS_SIZE={CombatEnv.OBS_SIZE}  "
          f"NUM_ACTIONS={CombatEnv.NUM_ACTIONS}  "
          f"NUM_CARD_TYPES={CombatEnv.NUM_CARD_TYPES}")
    print(f"{platform.machine()} · {platform.system()} · "
          f"python {platform.python_version()}")
    print()

    if engine:
        print("engine path (C++, no Python in the loop)")
        print(f"  {engine['engine_steps_per_sec']:>12,.0f} steps/sec   "
              f"(spread {engine['spread_pct']:.1f}% over 5 trials)")
        print(f"  {engine['valid_actions_us']:>12.3f} us  valid_actions")
        print(f"  {engine['apply_action_us']:>12.3f} us  apply_action")
        print(f"  {engine['action_select_us']:>12.3f} us  action selection "
              f"(incidental — an agent does not scan the mask)")
        print(f"  {engine['reset_us']:>12.2f} us  reset")
        print()
    else:
        print(f"engine path: SKIPPED — {ENGINE_BIN} not built.")
        print("  cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release")
        print("  cmake --build build-release --target minispire_bench")
        print()

    print("env path (through pybind11 + Gymnasium, sequential)")
    print("  'net' excludes this harness's action sampling — a policy consumes "
          "the mask")
    print("  directly and never scans it, so that cost is not the env's.")
    for n in batches:
        r = env_results[n]
        net = r["steps_per_sec"] / (1.0 - r["select_share"])
        rel = f"{r['steps_per_sec'] / single:.2f}x vs 1 env"
        print(f"  {n:>4} env  {r['steps_per_sec']:>10,.0f} steps/sec  "
              f"net {net:>10,.0f}   {rel:>14}")
    print()
    print(f"  reset latency   {reset_us:.1f} us (median of 2000)")
    print(f"  episode length  {ep_len:.1f} steps (mean of 500 random episodes)")

    if engine:
        overhead = engine["engine_steps_per_sec"] / single
        print()
        print(f"  binding overhead: the env path runs {overhead:.1f}x slower "
              "than the engine,")
        print("  which is the cost of crossing into Python once per step.")

    if args.json:
        pathlib.Path(args.json).write_text(json.dumps(
            {"engine": engine, "env": {str(k): v for k, v in env_results.items()},
             "reset_us": reset_us, "episode_length": ep_len,
             "obs_size": CombatEnv.OBS_SIZE,
             "num_actions": CombatEnv.NUM_ACTIONS,
             "machine": platform.machine(), "system": platform.system()},
            indent=2))


if __name__ == "__main__":
    main()
