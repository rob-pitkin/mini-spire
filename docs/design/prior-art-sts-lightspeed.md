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

**DECIDED (Rob, 2026-08-14): adopt, narrowed to publishing the constants.**

An earlier draft of this section implied we might emit **raw** values and let the
consumer normalise. That has a cost this section glossed over: unnormalised input
trains badly, and every user would have to redo the same work correctly before
the environment behaves well. That is not algorithm-agnostic, it is
unhelpful-by-default.

| | |
|---|---|
| **Publish the normalisation constants** | ✅ **adopted** — cheap, auditable, says exactly what the divisor was, no behaviour change |
| Add a `normalize=False` raw mode | **deferred** — genuinely principle-3 pure, but a second code path nobody has asked for. Additive later. |

### 5.2 Cap count-vector entries — REJECTED

`cardCountMax = 7` caps their deck counts. **We are not adopting this**, and the
reason is our own governing rule.

`observation-space.md` §1: *the observation should match what a human player can
see*. Under a cap of 7, an agent holding nine Strikes sees the same vector as one
holding seven. **A human sees the difference.** That is a parity defect in the
same category as an engine bug, by the standard we already hold ourselves to.

The benefit was bounding the input range — which we already get by dividing by a
fixed constant. So the cap gives nothing we do not already have, and loses a
parity guarantee.

If an overflow guard is ever wanted, set it somewhere provably unreachable in Act
1 (32, say). That is a different thing from a compression, and it should be
documented as a guard rather than as an encoding choice.

**Noted as a process point:** this was recommended in the first draft *because
`sts_lightspeed` does it*, without checking it against our §1. Prior art is
evidence about what works for the other project's goals, not a default.

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

- add a **no-observation step path** to the benchmark, which is both the
  comparable measurement and what an MCTS rollout actually needs;
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

## 7.5 Standing rule: every mechanic taken from here is wiki-checked

**Policy (Rob, 2026-08-14): nothing sourced from `sts_lightspeed` enters the spec
or the engine until it has been cross-checked against the wiki — retroactively
for what is already taken, and as a gate for everything new.**

The rule exists because the first cross-check found two defects in one pass:

- The Neow damage drawback is `floor(currentHp / 10) * 3`, while
  `sts_lightspeed`'s display string says *"Take 30% Hp damage."* At 75 HP that is
  21 versus 22.5. **A UI string had been read as a spec.**
- Neow's boss-relic pool excludes the upgraded form of the base relic
  (no Black Blood for the Ironclad) — a constraint absent from the code we read.

`sts_lightspeed` is a **reimplementation**, exactly like `sts_map_oracle`. It is
the best executable source available and it is not ground truth. Where the two
disagree, that is a flag, not an answer — and the resolution can go either way
(§7.6's Neow row resolved *in the simulator's favour*, the damage drawback
against it).

### 7.6 Retroactive audit

| mechanic | source | wiki-checked | result |
|---|---|---|---|
| `?` room distribution (10/3/2, event as fallback) | GameContext.cpp | ✅ | **confirmed** — matches wiki.gg mechanism + Fandom numbers |
| Shop stock composition | Shop.cpp | ✅ | **confirmed exactly** |
| Shop removal 75 + 25/use | Shop.cpp | ✅ | **confirmed** |
| Shop 50%-off slot | Shop.cpp | ✅ | **confirmed** |
| Neow always 4 options | Neow.cpp | ✅ | **divergence understood** — wiki says 2-or-4 on cross-run history; both projects pin the 4 branch for the same structural reason |
| Neow slot 4 = boss relic | Neow.cpp | ✅ | **confirmed** |
| Neow damage drawback | Neow.cpp | ✅ | ❌ **CORRECTED** — `floor(hp/10)*3`, not 30% |
| Neow boss pool exclusion | — | ✅ | ➕ **ADDED** — wiki-only constraint |
| Potion drop 40% ±10 | GameContext.cpp | ✅ | **confirmed** — including that it *decreases* on a drop |
| Card rarity drift | GameContext.cpp | ✅ | **confirmed, opposite sign** — see below |
| `lastRoomWasShop` shop suppression | GameContext.cpp | ✅ | **confirmed** — "the game does not allow generating two shops in a row… setting the shop chance to 0". ➕ **and it applies whether the previous shop was a Shop *room* or a `?` that became one** |
| Tiny Chest every-4th-`?` | GameContext.cpp | ✅ | **confirmed** — "Every 4th ? room is a Treasure room" |
| Juzu Bracelet monster→event | GameContext.cpp | ✅ | **confirmed** — "Regular enemy combats are no longer encountered in ? rooms" |
| Juzu Bracelet **reset ordering** | GameContext.cpp | ⚠️ **unverifiable** | the wiki states the *effect*, not whether `monsterChance` resets on a converted roll. Simulator-only, and no wiki source can settle it — see note below |
| Gold amounts (10–20 / 25–35 / 100±5) | GameContext.cpp | ✅ | **confirmed exactly** — wiki gives 10–20, 25–35, 95–105. Asc 13+ → 71–79 matches the ×0.75 factor |
| `SHRINE_CHANCE = 0.25` | GameContext.cpp | ⚠️ **not found** | no wiki source states the shrine-vs-event split. Remains simulator-only |
| Neow tier composition | Neow.cpp | ✅ | **confirmed** — 6 / 5 / 7 contents match the index ranges exactly |
| Neow Max HP amounts | Neow.cpp | ✅ | ❌ **CORRECTED** — flat per-character (+8/+16/−8 Ironclad), not the 10%/20% its enum names claim. Coincides only for the Ironclad |
| Vocabulary pool sizes | CardPools/RelicPools/Potions | ⚠️ **not fetch-verifiable** | see §7.8 — and an earlier wiki-derived count was **wrong** |

### 7.7 The pattern: read their arithmetic, not their names

Three of the four defects found by cross-checking were **not behavioural** — the
code computed the right thing under a wrong label:

| what was wrong | what the code actually did |
|---|---|
| `"Take 30% Hp damage."` | computed `floor(hp/10)*3` — not 30% |
| `TEN_PERCENT_HP_BONUS` / `TWENTY_PERCENT_HP_BONUS` | applied flat per-character values that only *look* like 10%/20% for the Ironclad |
| `cardRarityFactor` sign | correct, but inverted relative to how the wiki describes the same system |

**So the failure mode when mining this repo is not "their logic is wrong", it is
"we read the name and believed it".** Trust the arithmetic; treat every
identifier and display string as a comment.

The fourth defect was different in kind — Neow's boss-relic pool exclusion is
absent from the code entirely and only the wiki has it. That is the case for
cross-checking even when the code looks unambiguous.

### 7.8 Pool sizes: the one row the wiki cannot settle by fetch

Attempting this row surfaced a **contradiction that discredits an earlier
number**, so it is worth recording rather than quietly resolving.

| source | Ironclad common relic pool |
|---|---:|
| wiki.gg page summary (fetched 2026-08-13) | **26 total**, of which 3 are other-class → **23** |
| `sts_lightspeed` `RelicPools.h` | **33** (declared `std::array<RelicId, 33>`) |

These cannot both be right, and **the wiki figure is the one to distrust.** It
came from a *summarising fetch* — a small model reading a long list page and
reporting a total. That is precisely the failure mode already recorded in
`v2-spec.md` §5.0 for potions ("Total: 19+", self-described as not exhaustive):
an under-count is indistinguishable from a complete one.

`sts_lightspeed`'s figure is a **declared array literal**. It is machine-checked
by its own compiler and re-verifiable by anyone at any time.

**Consequence: the `≈104` relic estimate from 2026-08-13 was built on the bad
number and is withdrawn.** The current figure (§5.0, ≈131 + event relics) derives
from the declared pools and is the one to use.

**What real verification would take:** enumerating the wiki's category listings
item by item and counting them, not asking a model for a total. That is a
different and slower kind of work than every other row in this audit, and it is
**not yet done**. Until it is, pool sizes are marked *derived from declared
arrays, not independently confirmed* — which is accurate, and materially
stronger than the wiki summary they replaced.

#### ⚠️ The card-rarity sign trap

The two sources describe the **same system with opposite sign**, and mixing them
gives you the mechanic exactly backwards:

| | wiki.gg | `sts_lightspeed` |
|---|---|---|
| starts at | **−5** | **+5** |
| per common | **+1** | **−1** |
| bound | **+40** max | **−40** floor |
| on rare | reset to −5 | reset to +5 |
| applied to | the **rare chance** (higher = more rares) | the **roll** (lower = more rares) |

`offset_wiki = −factor_lightspeed`. Both agree that commons make rares more
likely and that a rare resets the pity. **Implement one convention and state
which**, or a reader checking against the other source will "fix" it into being
wrong. Both also agree boss rewards reset the counter despite bypassing the roll.

## 8. Verdict

**No change to the architecture.** The two projects optimise different
objectives, and their central design decision — a variable-length legal-action
list feeding tree search over a run-level value network — is the exact
macro/micro split our roadmap is built to avoid.

**Three things adopted** (Rob, 2026-08-14), all additive, none breaking:

1. ✅ Publish the **normalisation constants** alongside the offset constants
   (§5.1). Raw-output mode deferred.
2. ❌ **Count capping rejected** (§5.2) — it is a parity defect under
   `observation-space.md` §1, and the range bound it buys we already have.
3. ✅ Expose `legal_actions()` for MCTS consumers (§5.3).
4. ✅ Add a no-observation step path to the benchmark and stop comparing unlike
   numbers (§5.4).

**One thing to keep doing**, now with evidence: `static_assert` published offsets
against the code that writes them (§4.2).

**One risk to note:** their existence is also a reason our differentiator has to
be *stated precisely*. "A fast C++ StS engine with Python bindings" already
exists. Ours is "a **fast C++ StS RL environment** with a Gymnasium contract,
action masking, a learnable combat observation, and a reproducible benchmark
protocol" — and the paper should say so explicitly, citing them.
