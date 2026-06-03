# Research framing

## The problem

Environment throughput and design quality are genuine bottlenecks in RL research.
Most game environments for RL are either:

1. Python-only (slow, limits sample efficiency)
2. Full commercial game hooks (brittle, hard to reproduce)
3. Toy environments (too simple to be interesting)

Slay the Spire sits in an underexplored middle ground: complex enough to require
genuine strategy, small enough to train on modest hardware, with a clean win/loss
reward signal that requires no hand-crafted heuristics.

## Prior work gap

Miles Oram (2024) built a full C++ STS recreation with a DQN agent.
Key limitations of that work from a research perspective:

- Not open source / not reproducible
- No published benchmark (fixed seed set, fixed deck)
- No algorithm comparison (DQN only)
- No throughput numbers
- Architecture complexity (micro/macro split, 1325-dim input, 18M params)
  makes it hard to isolate what's actually driving performance

This project addresses all five gaps.

## Research questions

**Primary (v1):**
1. How do PPO, DQN, and MCTS compare on a fixed single-combat benchmark?
2. What is the throughput advantage of a C++ combat engine over Python?

**Secondary (v2+):**
3. Does memory architecture (LSTM, transformer policy) improve performance
   on multi-floor runs where combat history matters?
4. How does curriculum learning (easy enemies → hard enemies) affect
   convergence rate vs random sampling?

## Why single combat is enough for a paper

Single combat in STS is not a toy:
- Deck is stochastic (shuffled draw order unknown)
- Enemy intents are partially predictable (cyclic but not always shown)
- Energy management creates hard combinatorial constraints per turn
- Optimal play requires sequencing (e.g. Bash → Strike exploits Vulnerable)
- Action space grows combinatorially with hand size

A careful algorithm comparison on single combat with reproducible numbers
is a legitimate contribution to the game AI / RL environments literature.

## Positioning

Target: CoG (Conference on Games) or MARL @ NeurIPS workshop.
Framing: "A fast, reproducible benchmark environment for roguelike combat RL"

The contribution is the environment + benchmark + comparison, not a
claim to superhuman play. This framing is achievable with M1 + Colab compute.

## Narrative arc for blog posts

**Post 1 (after M1):**
"I trained a PPO agent to beat Slay the Spire's first fight — here's what I learned"
- Show training curves
- Visualize what the agent learned (does it use Bash before Strikes?)
- Compare to random baseline

**Post 2 (after M2):**
"How fast is a C++ game environment vs Python? I benchmarked it."
- Steps/sec table across batch sizes
- Architecture walkthrough (pybind11 zero-copy)
- Open-source the repo

**Post 3 (after M3):**
"PPO vs DQN vs MCTS on Slay the Spire: a comparison"
- Win rate table
- Training efficiency comparison
- MCTS discussion: does tree search help when the deck is stochastic?

## Connection to broader interests

This project sits at the intersection of:
- RL environment engineering (C++ efficiency, benchmarking)
- Games as RL research platforms (AlphaGo lineage)
- Long-horizon planning (deck composition shapes future action space)
- Model-based RL (MCTS is the classic instance)

Future extensions naturally lead into memory architectures across floors,
which connects to the broader question of how RL agents handle long-horizon tasks —
the research direction that genuinely excites me beyond this project.
