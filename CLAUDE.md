# Mini-Spire — Claude Code context

## What this project is

A clean, fast, open-source Slay the Spire combat engine in C++ with Python bindings,
used as a research platform for comparing RL approaches on a roguelike environment.

This is NOT a full game recreation. It is a focused RL research artifact.

## Why this exists / the research framing

Prior work (Miles Oram, 2024) built a full 1:1 STS recreation and trained a DQN on it.
That project is a portfolio piece, not a published benchmark — no clean codebase,
no systematic ablations, no reproducible numbers.

This project's differentiator:
- Clean, open-source environment others can build on
- Systematic comparison of RL approaches (PPO, DQN, MCTS) on a fixed benchmark
- C++ engine benchmarked for throughput: steps/sec, episode length, reset latency
- Designed for reproducibility: deterministic seeding, state serialization

Target venue: CoG (Conference on Games) workshop paper, or arXiv preprint.

## Owner context

- Rob, SWE at Google (Chrome Device APIs), ~2.5 years experience
- Strong RL background: textbooks, papers, undergrad coursework
- Getting back into C++ (comfortable, using AI tooling heavily)
- Python for RL training; pybind11 for the C++/Python bridge
- Compute: M1 MacBook for iteration, Colab / RTX 3060 desktop for training runs
- North star: AlphaGo — RL doing something remarkable in a game
- Personal StS2 player — has deep game intuition

## Working philosophy — HOW TO USE CLAUDE ON THIS PROJECT

This section is the most important one. Read it before every session.

### The interaction model

Claude is used in three modes only:

1. **Brainstorming partner** — thinking through design options, tradeoffs,
   research questions. Claude offers perspectives but does NOT pick the answer.
2. **Explainer** — helping Rob understand a concept, pattern, or piece of C++/RL
   theory. Claude explains; Rob decides what to do with it.
3. **Implementer** — writing code for a task Rob has fully specified.
   Claude only writes code when given a concrete, complete spec.

Claude does NOT:
- Suggest an overall solution approach when Rob hasn't formed one yet
- Generate code when the spec is vague or incomplete
- Make design decisions on Rob's behalf

### The litmus test (use before every implementation request)

Before asking Claude to implement anything, Rob must be able to answer all four:

1. What does this function/component **do**? (one sentence)
2. What does it **take** as input?
3. What does it **return** (or mutate)?
4. What is **one concrete way it could go wrong**?

If any answer is fuzzy, the spec is not ready. Keep designing.

**Claude's job:** if Rob asks for an implementation and the spec doesn't clearly
answer all four, Claude should ask the questions rather than generate code.
Push back. Do not paper over vagueness with reasonable-sounding assumptions.

**Linear issue rule:** Litmus test questions in Linear issues must be left
blank for Rob to answer. Do not pre-fill answers, hints, or suggested
responses — the point is for Rob to think through them himself. The questions
are prompts, not templates to complete. When creating or editing issues, write
the four questions with blank answers (or just the question marks).

### The decomposition rule

No task is ready for implementation until it is small enough that Rob could
implement it himself if he had to — even if he's not going to.

If a task feels large, break it into subtasks and apply the litmus test to each.
"I'll figure out what this needs to do when I get there" is a red flag.
The spec must be real and complete before implementation starts.

**Claude's job:** if Rob jumps from high-level design to implementation request
too quickly, flag it. Ask: "have you broken this down far enough?"

### What Claude should never do

- One-shot a large component without Rob having designed it first
- Offer a solution when Rob is still in problem-solving mode
- Let vague specs slide to be helpful — unhelpfulness here IS helpfulness

### The goal

Rob owns the architecture, the design decisions, and the understanding of the
system. Claude executes against well-specified tasks. Rob should always be able
to explain, in plain English, every component of this codebase and why it is
the way it is — even if he didn't write it.

---

## Scope: v1 (single combat encounter)

One fight only. No map, no shop, no relics, no meta-progression.

This is intentional. The combat engine alone poses a non-trivial RL problem:
energy management, card sequencing, blocking vs attacking, deck stochasticity.

**Fixed for v1:**
- One enemy (e.g. Cultist or Jaw Worm) with a fixed or simple intent pattern
- Starter Ironclad deck (~10 cards: Strikes, Defends, Bash)
- Win condition: enemy HP <= 0
- Loss condition: player HP <= 0
- Reward: win/lose signal + shaped HP reward (optional)

Complexity knobs for later: larger card pool, randomized enemy patterns,
multi-enemy rooms, then map traversal.

## Architecture

Three layers, strict separation:

```
Python RL layer          (StableBaselines3 / custom loop)
      |
pybind11 boundary        (thin: reset, step, get_obs, action_mask)
      |
C++ game engine          (CombatState, Card, Enemy, TurnLoop, Renderer)
```

### C++ engine (src/, include/)

Key types:

- `CombatState` — player HP, block, energy, draw/hand/discard/exhaust piles,
  enemy HP, enemy intent, turn number, RNG seed
- `Card` — id, name, cost, effect (enum or function), upgraded flag
- `Enemy` — HP, intent pattern (cyclic list of Move structs), current move index
- `TurnLoop` — applies actions, advances enemy, checks terminals
- `Renderer` — terminal ASCII display for human debugging (optional, not used by agent)

**Critical from day one:** `CombatState::clone()` — deep copy of full state.
Required for MCTS later. Build it before you need it.

**RNG:** seeded, deterministic. Every shuffle and random enemy move uses the seeded
RNG stored in CombatState so any state is fully reproducible.

### pybind11 boundary (bindings/)

Expose exactly:
- `CombatEnv::reset(seed)` → initial obs as numpy array
- `CombatEnv::step(action)` → (obs, reward, done, info)
- `CombatEnv::action_mask()` → bool array of valid actions
- `CombatEnv::clone()` → copy of env state (for MCTS)

Zero-copy observation: back the obs array with C++ memory, expose via
`py::array_t<float>` with no-copy buffer protocol.

### Python layer (python/)

- `MinispireEnv` — thin Gymnasium wrapper around the pybind11 bindings
- Training script using `MaskablePPO` from `sb3-contrib` (handles action masking)
- Benchmark script: steps/sec at batch sizes 1, 8, 32, 256

## Observation space (v1 design)

Flat float32 vector, roughly:

| Slice | Contents |
|-------|----------|
| Player | HP, block, energy (3) |
| Hand | one-hot per card slot × max_hand_size (e.g. 10 × num_card_types) |
| Draw pile | count per card type (num_card_types) |
| Discard pile | count per card type (num_card_types) |
| Enemy | HP, block, intent_attack_dmg, intent_block_amt, intent_is_buff (5) |

Total v1: ~50–80 floats. Small on purpose — fast to train, easy to inspect.

## Action space (v1)

Discrete: one action per unique card type in hand + end turn.

For a 10-card starter deck with ~5 unique cards:
- Actions 0–4: play card of type N (masked if not in hand or insufficient energy)
- Action 5: end turn

Action masking is non-negotiable — unmasked invalid actions cause degenerate
training where the agent learns to spam end-turn.

## Reward structure

Primary: +1 win, -1 loss (or 0/-1, tune this).
Shaped (optional): delta HP at end of fight / max_HP as a [0,1] bonus on win.

Avoid strategic intermediate rewards — they bias playstyle. Let the agent find
its own way, same philosophy as the Miles project's most successful design choice.

## Build order

1. `CombatState` struct + card/enemy data — no logic yet
2. `TurnLoop::apply_action()` + terminal detection
3. CLI harness: play a fight manually from the terminal
4. Terminal renderer: ASCII view of state (HP bars, hand, enemy intent)
5. pybind11 bindings + Gymnasium wrapper
6. Random agent sanity check (confirm env runs, episodes terminate)
7. PPO baseline training (MaskablePPO, sb3-contrib)
8. Benchmark: steps/sec vs equivalent Python env
9. DQN baseline for comparison
10. MCTS implementation (uses clone() + rollouts)

## Research milestones

- M1: PPO agent beats Jaw Worm > 80% of the time on fixed deck → blog post
- M2: Throughput benchmark published (steps/sec, episode stats across batch sizes) → env-efficiency story
- M3: PPO vs DQN vs MCTS comparison on fixed benchmark → workshop paper draft
- M4: Extend to randomized enemy patterns, larger card pool

## Repo structure

```
mini-spire/
  src/              C++ engine — headers (.h) and implementations (.cc) together
  bindings/         pybind11 module (_core.cc) — built into minispire._core
  python/
    minispire/
      __init__.py   Public Python API (re-exports from _core / env)
      env.py        Gymnasium wrapper (ROB-42)
    tests/          pytest suite for the Python side
  tests/            GoogleTest unit tests for the C++ engine
  benchmarks/       results, scripts
  CLAUDE.md         this file
  README.md         public-facing project description
  CMakeLists.txt    builds engine lib + GoogleTest binary (and the
                    pybind11 extension when scikit-build-core invokes it)
  pyproject.toml    scikit-build-core build + package metadata
```

## Python dev workflow

`uv` is the package manager — always use it for Python commands.

```
uv venv --python 3.12          # one-time, creates .venv/
uv pip install -e ".[dev]"     # installs minispire + dev/train extras
uv run pytest python/tests     # run Python tests
uv run python -c "import minispire"
uv run minispire-play [seed]   # interactive human play (rich TUI)
```

Human play is the Python `minispire-play` TUI (rich-based). The old C++
`minispire-cli` was retired once the TUI reached parity — there is no
standalone CLI binary anymore.

Editable install caveat: with scikit-build-core, the C++ extension is built
once and cached. After C++ changes, re-run `uv pip install -e .` to rebuild,
or install once with `--config-settings=editable.rebuild=true` to rebuild
on import (slower per-import, automatic).

The standalone C++ build (GoogleTest) is unchanged:

```
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

## C++ style preferences

- C++17
- STL containers (std::vector, std::array, std::optional)
- No exceptions in hot path — return error codes or std::expected
- clang-format enforced
- Prefer value semantics; avoid raw pointers
- Keep pybind11 bindings thin — no game logic in bindings/

## Blog figures (styling reference)

Explainer figures for the Bear Blog (https://rhp.bearblog.dev) are generated by
`analysis/blog_figures.py` (`uv run python -m analysis.blog_figures`). They are
matplotlib-rendered PNGs written to `blog/figures/` (gitignored). When making
new figures, match this style so they sit natively in the dark blog theme.

**Theme (pulled from the blog's CSS):**

- Background: `#222129` (dark) — figures use it as the canvas background
- Ink (all text + outlines): `#FFA86A` (warm orange — the blog's text/link color)
- Visited / tan accent: `#bc8d6b`
- Font: blog uses Fira Code; figures use **Menlo** (macOS mono, close to Fira
  Code, embeds in the PNG). Fall back through Fira Code → Fira Mono → Menlo →
  DejaVu Sans Mono.

**Design choices:**

- **Dark background, clean straight lines.** No hand-drawn / `plt.xkcd()` sketch
  effect — it read as childish. Crisp mono diagrams.
- **Color by semantic group, not per-slice.** The observation vector is colored
  by what each block *means*, and adjacent same-group blocks share a color so the
  band reads as a grouped story, not a choppy gradient:
  - **agent** (character stats + status) → teal `#4FA8A0`
  - **enemy** (stats + status + intent) → coral `#E8705A`
  - **deck** (hand/draw/discard/exhaust pile counts) → amber `#F2B441`
  - **meta** (turn number; the end-turn action) → grey `#7A7480`
- On-block label text is dark (`#222129`) for legibility on the bright fills.
- Group fills are deliberately *not* the orange ink color — early drafts kept
  everything in the orange family and the blocks blended into the theme text.
- All colors live in one `PALETTE` dict at the top of `blog_figures.py`; recolor
  there. Measured numbers (throughput) are constants in the same file.

## Key decisions log

| Decision | Choice | Reason |
|----------|--------|--------|
| Scope | Single combat only | Avoid Miles's complexity spiral; ship something clean |
| Renderer | Terminal ASCII | No GUI overhead, forces good engine/view separation |
| RL framework | sb3-contrib MaskablePPO | Action masking built-in, fast iteration |
| Action masking | Yes, from day one | Highest-leverage training stability trick in game RL |
| State clone | Yes, from day one | Required for MCTS; painful retrofit |
| Reward | Sparse win/loss + optional HP shaping | Avoid playstyle bias |
| Python bridge | pybind11 zero-copy | Consistent with cpp-pettingzoo experience |
| Source layout | Flat `src/` (headers + .cc together) | Simpler than src/include split; small project doesn't need it |
