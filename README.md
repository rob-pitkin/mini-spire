# Mini-Spire

[![CI](https://github.com/rob-pitkin/mini-spire/actions/workflows/ci.yml/badge.svg)](https://github.com/rob-pitkin/mini-spire/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](#license)

A fast, open-source **Slay the Spire combat engine in C++ with a Gymnasium
Python API**, built as a research platform for benchmarking reinforcement
learning approaches on a roguelike deck-builder.

The engine is written in C++ for throughput (~145k env steps/sec on a laptop)
and exposed to Python via zero-copy pybind11 bindings, so you can train an agent
with [Stable-Baselines3](https://github.com/DLR-RM/stable-baselines3) and friends
using a standard Gymnasium interface — action masking included.

![Playing a fight in the terminal](assets/demo.gif)

> **Read the intro write-up:** [Mini-spire: a fast Slay the Spire RL environment
> in C++](https://rhp.bearblog.dev) — the story, the design choices, and the M1
> result (a ~14.7k-parameter PPO agent that wins 100% of the time, trained in
> ~4 minutes on an M1 MacBook).

## Highlights

- ⚡ **Fast** — pure C++ combat engine, ~145k steps/sec single-env; ~4 min to
  train 1M timesteps on a CPU laptop, no GPU required.
- 🧩 **Zero-copy bindings** — observations are numpy views backed by C++ memory
  (pybind11 buffer protocol), no per-step allocation.
- 🎭 **Action masking from day one** — `action_masks()` in the convention
  [sb3-contrib `MaskablePPO`](https://sb3-contrib.readthedocs.io) expects.
- 🎲 **Deterministic & reproducible** — every shuffle and enemy move draws from a
  single seeded RNG, so any fight replays exactly from its seed.
- 🕹️ **Human-playable** — a `rich` terminal UI (`minispire-play`) to play the
  same fight the agent trains on.

## Scope (v1)

A single combat encounter: **Ironclad starter deck vs. the Jaw Worm**, one fight,
fixed deck, one enemy. This is intentional — the combat alone is a non-trivial RL
problem (energy management, card sequencing, blocking vs. attacking, deck
stochasticity) without the orthogonal complexity of map traversal, shops, or
relics. Those are on the [roadmap](roadmap.md).

## Install

`uv` is the package manager. ([install uv](https://docs.astral.sh/uv/))

```bash
uv venv --python 3.12
uv pip install -e ".[dev]"        # engine + training + dev extras
uv run python -c "import minispire"
```

The C++ extension builds automatically via scikit-build-core on install. After
changing C++ sources, re-run `uv pip install -e .` to rebuild.

Requires Python 3.12, a C++17 compiler, and CMake ≥ 3.16.

## Quickstart

**Play a fight yourself** (terminal UI):

```bash
uv run minispire-play 0           # optional seed
```

**Use the environment in Python** — it's a standard Gymnasium env:

```python
import numpy as np
from minispire.env import MinispireEnv

env = MinispireEnv()                     # or hp_reward_coeff=0.5 for HP shaping
obs, info = env.reset(seed=0)

done = False
while not done:
    mask = env.action_masks()            # bool[7] of legal actions
    action = int(np.random.choice(np.flatnonzero(mask)))
    obs, reward, terminated, truncated, info = env.step(action)
    done = terminated or truncated
```

**Train a MaskablePPO agent** and evaluate it over a fixed seed set:

```bash
uv run minispire-train --config configs/baseline_sparse.yaml
uv run minispire-eval --checkpoint checkpoints/<run_id>/final.zip
```

Training logs to [Weights & Biases](https://wandb.ai) if configured (offline-safe
otherwise).

## Environment specification

**Observation** — a flat `float32[45]` vector, colored here by semantic group:

![Observation space layout](assets/obs_layout.png)

Pile slices are *counts per card type* (not ordered lists), so the vector is
fixed-width and order-invariant — and draw order stays hidden, just as a human
player sees only which cards are in the pile, not their order.

**Action** — `Discrete(7)`: play one of six card types, or end the turn.

![Action space](assets/action_space.png)

Invalid actions (card not in hand, or insufficient energy) are masked each step;
end-turn is always legal.

**Reward** — `+1` win / `-1` loss, `0` otherwise. An optional terminal HP-shaping
bonus (`hp_reward_coeff * current_hp / max_hp`, added on a win) rewards winning
with more HP remaining; the default coefficient is `0` (pure sparse reward).

## Architecture

```
Python RL layer        (Stable-Baselines3 / sb3-contrib / custom)
      │
pybind11 boundary      (thin: reset, step, action_masks, clone)
      │
C++ combat engine      (CombatState, Card, Enemy, TurnLoop)
```

The C++ engine owns all game logic and knows nothing about Python or RL. The
pybind11 module exposes only the environment surface, with zero-copy observation
arrays. See [architecture.md](architecture.md) for detail.

## Project layout

```
src/         C++ engine — headers (.h) + implementations (.cc)
bindings/    pybind11 module (_core)
python/
  minispire/ public Python API: env.py (Gymnasium), play/train/eval entry points
  tests/     pytest suite
tests/       GoogleTest C++ unit tests
configs/     training configs (YAML)
analysis/    blog/figure-generation tooling
```

## Development

```bash
uv run pytest python/tests        # Python tests (82)

cmake -S . -B build               # C++ build + GoogleTest
cmake --build build
ctest --test-dir build
```

## Roadmap

- **M2** — throughput benchmark: steps/sec vs. batch size, episode stats.
- **M3** — PPO vs. DQN vs. MCTS on the fixed benchmark (`clone()` exists for MCTS).
- **Beyond** — harder enemies, randomized intents, larger card pool; then map
  generation and persisting state between fights.

Full detail in [roadmap.md](roadmap.md).

## Related work

- [Miles Oram (2024)](https://milesoram.github.io/slay-the-spire-ml-project.html)
  — full STS recreation in C++ with a DQN. The inspiration; mini-spire adds a
  clean Gymnasium API and an open, documented codebase.
- [PokeRL](https://drubinstein.github.io/pokerl/) — beating Pokémon Red with pure
  deep RL; the broader inspiration for RL-on-games on modest hardware.
- [gym-sts](https://github.com/kronion/gym-sts) — Gymnasium wrapper around the
  real game (requires a copy of STS as a `.jar`).

## License

MIT
