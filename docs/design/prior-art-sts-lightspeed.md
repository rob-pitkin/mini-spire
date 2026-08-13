# Prior art: `sts_lightspeed`

**Status:** analysis, 2026-08-13. Source:
[`gamerpuppy/sts_lightspeed`](https://github.com/gamerpuppy/sts_lightspeed) —
106 files, C++17, pybind11 bindings, MIT-adjacent (see `LICENSE.md`).

We started using it as a *data source* for ROB-104/106. It is close enough to
mini-spire structurally — C++ engine, Python bindings, tree-search ambitions —
that it deserves a real comparison rather than being mined and forgotten.

**Headline conclusion: no fundamental change to our approach, and the reason is
that we are not building the same kind of artifact.** But there are four
concrete things worth taking, and one place where their code is direct evidence
for a discipline we already have.

---

## 1. What they built

| | `sts_lightspeed` | mini-spire |
|---|---|---|
| Scope | **All 4 acts**, all characters, all relics, all cards | Act 1, Ironclad (v2) |
| Language | C++17 | C++17 |
| Python | pybind11 | pybind11 |
| Purpose | **Tree search / simulation** | **RL environment** |
| Combat solved by | search (`BattleScumSearcher2`) | a learned policy |
| Run state | `GameContext` (3,835-line .cpp) | `RunState` (planned) |
| Combat state | `BattleContext` | `CombatState` |
| Phases | `ScreenState` enum | `phase` enum |
| Determinism | per-system `Random` objects | named streams (§3.5) |
| Throughput claim | 1M random playouts / 5s / 16 threads | 438k engine steps/sec, 259k end-to-end |

Their content coverage is far ahead of ours. Their *research framing* is
essentially absent — no benchmark protocol, no reproducibility story, no design
docs, README is 32 lines.

## 2. The fundamental divergence — action representation

This is the crux, and everything else follows from it.

**Theirs:**

```cpp
struct GameAction {
    std::uint32_t bits;                     // packed idx1/idx2/idx3 + type
    static std::vector<GameAction> getAllActionsInState(const GameContext &gc);
};
```

`getAllActionsInState` switches on `ScreenState` and returns a
**variable-length vector of legal actions**.

**Ours:** a fixed `Discrete(N)` plus a boolean mask.

| | variable-length legal list | fixed Discrete + mask |
|---|---|---|
| MCTS node expansion | **natural** — it *is* the child list | needs a mask scan |
| PPO / DQN policy head | **impossible** — output dim varies | **natural** |
| Gymnasium contract | violates it (`action_space` must be fixed) | satisfies it |
| SB3 compatibility | none | out of the box |

Their design is *correct for tree search* and *unusable for policy-gradient RL*.
Ours is the reverse. **This is not a defect on either side** — it is the
artifact each project is trying to be.

**Implication for us:** we should not adopt their action API. We should,
however, expose a cheap way to *enumerate* legal actions for MCTS consumers
(§5.3), because our mask already contains that information and materialising it
is trivial.

## 3. Their neural interface, and what it reveals

```cpp
struct NNInterface {
    static constexpr int observation_space_size = 412;
    static constexpr int playerHpMax  = 200;
    static constexpr int playerGoldMax = 1800;
    static constexpr int cardCountMax = 7;
    std::array<int,412> getObservation(const GameContext &gc) const;
    std::array<int,412> getObservationMaximums() const;
};
```

Layout, read from `bindings/bindings-util.cpp`:

| block | size | contents |
|---|---:|---|
| scalars | 4 | curHp, maxHp, gold, floorNum |
| boss | 10 | one-hot |
| deck | 220 | 110 card types × {unupgraded, upgraded}, **count capped at 7** |
| relics | 178 | multi-hot |
| **total** | **412** | |

### ⚠️ Their observation contains NO combat state

No hand, no enemies, no block, no energy, no piles, no intents. It is a
**run-level** encoding only: deck, relics, HP, gold, floor, boss.

That is not an oversight — it is the architecture. Combat is solved by tree
search, so the network never needs to see a combat state. Their NN is a
**value estimator over run states**, not a policy over game actions.

**This is the macro/micro split** — the one Miles Oram reported as his
limitation and that `roadmap.md` explicitly rejects. Their design does not merely
permit it; it *forecloses the alternative*, because the observation cannot
express a combat decision.

**So: their 412 vs our ~4,200 is not a comparison of efficiency.** It is a
comparison of what the two observations are *for*. Ours is bigger because it can
answer "which card should I play at this enemy", and theirs cannot.

## 4. What their code is evidence *for*

### 4.1 Named RNG streams — independent confirmation of §3.5

`GameContext` carries separate `Random` objects: `eventRng`, `cardRng`,
`potionRng`, `treasureRng`, `relicRng`, `miscRng`, `monsterRng`. A working,
RNG-accurate reimplementation partitions its randomness **exactly the way
`v2-spec.md` §3.5 specifies**. That decision was reasoned from first principles;
it is now also the observed practice of the most accurate simulator available.

### 4.2 Their offset bug is the case for our `static_assert` discipline

`getObservationMaximums()` walks the same layout as `getObservation()` — and
gets it wrong:

```cpp
ret[0] = playerHpMax;  ret[1] = playerHpMax;
ret[2] = playerGoldMax; ret[3] = 60;
spaceOffset += 3;                                   // ← 4 scalars were written
std::fill(ret.begin()+spaceOffset, ret.end(), 1);   // ← fills to END, not band
spaceOffset += 10;
std::fill(ret.begin()+spaceOffset, ret.end(), cardCountMax);
```

Two independent defects: `+= 3` after writing four values, and each `fill`
running to `end()` rather than the band boundary, so every later fill overwrites
the earlier one. The published maxima are misaligned against the observation
they describe — `ret[3]` ends up 1 instead of 60, and every band boundary is off
by one.

**This is precisely the bug class CLAUDE.md records for us** (`TURN_NUMBER =
OBS_SIZE - 1`, which broke the TUI and 13 tests). It is the strongest available
argument for `v2-spec.md` §5.1.3's requirement: publish per-block offset
constants and `static_assert` them against the code that writes them. A careful
project with a working simulator still got this wrong, in the one function whose
entire job is to agree with another function.

## 5. What we should actually take

### 5.1 Publish observation maxima — worth adopting

`getObservationMaximums()` is a good idea independent of their bug. Emitting
**raw values plus a published maxima vector** rather than pre-normalised floats:

- keeps the environment's output **lossless** — no baked-in scaling to undo;
- is **algorithm-agnostic** (principle 3) — the consumer picks the
  normalisation, rather than inheriting ours;
- makes the normalisation **auditable** instead of implicit.

⚠️ Tension with our current design: we normalise in-engine with fixed constants,
and `run-reward.md` records a real bug from normalising by a *varying*
denominator. Publishing maxima does not conflict with that — it makes the
constants explicit and inspectable. **Recommendation: publish
`OBS_MAXIMUMS` alongside the offset constants**, whether or not we also
normalise.

### 5.2 Cap count-vector entries

`cardCountMax = 7` bounds deck counts. Cheap, bounds the input range, and no
realistic Act 1 deck holds 8 copies of one card where the 8th changes a decision.
Worth doing, with the cap published like any other constant.

### 5.3 Expose a legal-action enumeration for MCTS

Our mask already knows the legal set. `std::vector<int> legal_actions()` (or a
fixed-capacity span) costs us nothing and makes the env pleasant for exactly the
tree-search consumers `sts_lightspeed` serves. This is additive and does not
touch the Gymnasium contract.

### 5.4 Benchmark like-for-like — and fix our own claim first

Their README: **1M random playouts in 5s with 16 threads.** A *playout* is a
full game, not a step, and **no observation is computed** during it.

Our 259k end-to-end steps/sec includes building a 1,772-float observation every
step. **These numbers are not comparable, and we should stop implying they
are.** Concretely:

- add a **no-observation step path** to the benchmark, which is both the honest
  comparison point and what an MCTS rollout actually needs;
- report **steps/sec single-threaded** and note that theirs is 16-threaded;
- do not publish a head-to-head until both are measured the same way.

⚠️ Do not take their figure at face value either — 200k full games/sec across 16
threads implies ~12k games/sec/thread, which at ~1,000 decisions per game would
be ~12M decisions/sec/thread. That may be right for a pure simulator with no
allocation, but it should be **measured, not quoted**. Treat it as a target to
verify, not a fact.

## 6. Where we are better, for our goals

| | |
|---|---|
| **Gymnasium contract** | Fixed obs shape and fixed `Discrete`, so SB3 works out of the box. Theirs cannot satisfy this by construction. |
| **Combat is learnable** | Our observation can express a combat decision; theirs cannot. This is the entire research question. |
| **Action masking** | First-class, from day one. They have enumeration, which does not help a policy network. |
| **Layout as a contract** | Published constants + `static_assert`s. §4.2 shows what the absence costs. |
| **Reproducibility framing** | Seeded streams *documented as an interface*, determinism tests in CI, benchmark protocol. Theirs is accurate but undocumented. |
| **Research artifact** | Design docs, decision log, evaluation protocol, published numbers. Their README is 32 lines. |

## 7. Where they are better

| | |
|---|---|
| **Content coverage** | 4 acts, 4 characters, all relics. We have Act 1 Ironclad. Not close. |
| **RNG accuracy** | "Designed to be 100% RNG accurate", including save-file loading to reproduce real runs. We have no save-file interop. |
| **Raw simulation speed** | Almost certainly faster per step with no obs — they have no observation to build in the hot path. |
| **Tree-search tooling** | A real MCTS-ish agent exists and runs. Ours is planned. |
| **Data tables** | Complete, machine-readable game constants — which is why ROB-104/106 are now nearly closed. |

## 8. Verdict

**No change to the architecture.** The two projects optimise different
objectives, and their central design decision — a variable-length legal-action
list feeding tree search over a run-level value network — is the exact
macro/micro split our roadmap is built to avoid.

**Four things to adopt**, all additive, none breaking:

1. Publish `OBS_MAXIMUMS` alongside the offset constants (§5.1).
2. Cap count-vector entries and publish the cap (§5.2).
3. Expose `legal_actions()` for MCTS consumers (§5.3).
4. Add a no-observation step path to the benchmark and stop comparing unlike
   numbers (§5.4).

**One thing to keep doing**, now with evidence: `static_assert` published offsets
against the code that writes them (§4.2).

**One risk to note:** their existence is also a reason our differentiator has to
be *stated precisely*. "A fast C++ StS engine with Python bindings" already
exists. Ours is "a **fast C++ StS RL environment** with a Gymnasium contract,
action masking, a learnable combat observation, and a reproducible benchmark
protocol" — and the paper should say so explicitly, citing them.
