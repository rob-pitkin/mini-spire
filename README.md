# Mini-Spire

A fast, open-source Slay the Spire combat engine in C++ with Python bindings,
designed as a research platform for benchmarking reinforcement learning approaches
on roguelike environments.

## Motivation

Slay the Spire is an interesting RL problem: stochastic card draws, energy
management, sequential decision-making under partial information (shuffled deck),
and a clean win/loss reward signal. Despite this, there is no published, reproducible
benchmark environment for it.

This project fills that gap. The goal is not to beat the game — it is to provide a
fast, well-documented environment that lets researchers compare RL algorithms on a
fixed, reproducible benchmark.

## Scope (v1)

Single combat encounter: one fight, fixed deck (Ironclad starter), one enemy.

This is intentional. The full game adds map traversal, shops, relics, and
meta-progression — all interesting, but orthogonal to the core RL question of
*how well can an agent learn to play a hand of cards*.

## Architecture

```
Python RL (StableBaselines3 / custom)
        ↑↓  pybind11
C++ combat engine
```

The C++ engine handles all game logic. Python sees a standard Gymnasium interface
with action masking. Zero-copy observation arrays via pybind11 buffer protocol.

## Benchmark

Primary metric: PPO vs DQN vs MCTS win rate on a fixed evaluation set
(Jaw Worm, starter deck, seed range 0–999).

Secondary metric: environment throughput (steps/sec) vs equivalent Python
implementation, across batch sizes 1 / 8 / 32 / 256.

## Status

- [ ] CombatState + card/enemy data structures
- [ ] TurnLoop logic
- [ ] Terminal renderer
- [ ] pybind11 bindings + Gymnasium wrapper
- [ ] PPO baseline
- [ ] Throughput benchmark
- [ ] DQN baseline
- [ ] MCTS implementation
- [ ] Comparison paper / preprint

## Building

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

Requirements: C++17, pybind11, Python 3.10+

```bash
pip install gymnasium stable-baselines3 sb3-contrib
```

## Usage

```python
from python.env import MinispireEnv
from sb3_contrib import MaskablePPO

env = MinispireEnv()
model = MaskablePPO("MlpPolicy", env, verbose=1)
model.learn(total_timesteps=1_000_000)
```

## Related work

- Miles Oram (2024): Full STS recreation in C++ with DQN.
  Portfolio piece; no published benchmark or reproducible numbers.
- OpenAI Dota 2 (2019): Inspiration for the micro/macro split approach.

## License

MIT
