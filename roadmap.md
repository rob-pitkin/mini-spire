# Roadmap

## Build order

The order matters — each step is a working, testable artifact.

### Phase 1 — Engine (no Python yet)

- [ ] `CombatState` struct with all fields
- [ ] `Card` types for Ironclad starter deck (Strike, Defend, Bash)
- [ ] `Enemy` with Jaw Worm pattern (Attack 6, Block 5, Attack 14, repeat)
- [ ] `TurnLoop::apply_action()` — play card or end turn
- [ ] `TurnLoop::valid_actions()` — action mask
- [ ] Terminal condition detection (win/loss)
- [ ] `CombatState::clone()` — deep copy with RNG
- [ ] CLI harness: play a fight manually, print state each turn
- [ ] Terminal renderer: ASCII HP bars, hand display, intent display
- [ ] Unit tests: verify card effects, enemy patterns, deck cycling

**Exit criterion:** A human can play a complete fight from the terminal
and the outcome is correct.

### Phase 2 — Python bridge

- [ ] pybind11 module (`minispire_cpp`)
- [ ] `CombatEnv` class wrapping `CombatState` + `TurnLoop`
- [ ] Zero-copy observation array (buffer protocol)
- [ ] `reset(seed)` / `step(action)` / `action_mask()` / `clone()`
- [ ] `MinispireEnv` Gymnasium wrapper (`python/env.py`)
- [ ] Random agent sanity check — confirm episodes terminate, obs shapes correct
- [ ] Observation space normalization verified

**Exit criterion:** `python -c "from python.env import MinispireEnv; env = MinispireEnv(); obs, _ = env.reset(); print(obs.shape)"` works.

### Phase 3 — RL baseline

- [ ] PPO training script with MaskablePPO (sb3-contrib)
- [ ] Evaluation script: win rate over 1000 episodes, seeds 0–999
- [ ] Training curves logged (TensorBoard or W&B)
- [ ] Hyperparameter sweep (lr, n_steps, batch_size)
- [ ] **M1: PPO beats Jaw Worm > 80% win rate** ← first blog post

**Exit criterion:** Reproducible training run with published win rate.

### Phase 4 — Benchmarks

- [ ] Python reference implementation of same combat logic
- [ ] Throughput benchmark: steps/sec at batch 1, 8, 32, 256
- [ ] C++ vs Python comparison table
- [ ] `benchmarks/results.md` with methodology
- [ ] **M2: Throughput numbers published** ← env-efficiency research artifact

**Exit criterion:** Reproducible benchmark, runnable by anyone with the repo.

### Phase 5 — Algorithm comparison

- [ ] DQN baseline (stable-baselines3)
- [ ] MCTS implementation using `clone()` for rollouts
- [ ] Optional: AlphaZero-style MCTS + learned value head
- [ ] Head-to-head evaluation: PPO vs DQN vs MCTS, same seeds
- [ ] **M3: Comparison results** ← workshop paper draft

**Exit criterion:** Table of win rates across algorithms on fixed benchmark.

### Phase 6 — Expansion (post-paper)

- [ ] Randomized enemy patterns
- [ ] Larger card pool (top 20 Ironclad cards)
- [ ] Multi-enemy rooms
- [ ] Map traversal (path choices, no shop)
- [ ] Memory architecture experiments (LSTM policy, transformer)
- [ ] **M4: Long-horizon memory across floors** ← extended research

## Research milestones

| Milestone | Goal | Output |
|-----------|------|--------|
| M1 | PPO > 80% win rate on Jaw Worm | Bear Blog post |
| M2 | C++ throughput benchmark vs Python | Bear Blog post + README |
| M3 | PPO vs DQN vs MCTS comparison | CoG workshop paper or arXiv preprint |
| M4 | Memory agents across map floors | Extended paper or follow-up post |

## Compute plan

| Phase | Hardware | Notes |
|-------|----------|-------|
| 1–2 | M1 MacBook | Engine dev, no training |
| 3 | M1 MacBook | Small PPO runs for iteration |
| 3 (full) | Colab / RTX 3060 | Full hyperparameter sweeps |
| 4–5 | M1 MacBook | Benchmarks + MCTS (no GPU needed) |
| 6 | RTX 3060 | Memory architecture experiments |

MCTS is CPU-bound and runs well on M1 — no GPU needed until learned value heads.

## Anti-goals

These are explicitly out of scope for v1:

- Full game recreation (all 4 acts, all characters)
- GUI or graphical renderer
- Docker / containerized deployment
- Real-time play speed (training runs headless)
- Relics, potions, shops (Phase 6+ only)
