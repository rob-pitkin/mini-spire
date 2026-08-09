# Roadmap

## Where things stand

**v1.0.0 shipped** (2026-08-03, on PyPI). A single combat encounter, fully
faithful: the complete Ironclad pool at 189 card types, the Act 1 roster with
real AI tables, multi-enemy fights, weighted encounter selection, a Gymnasium
env and a Textual TUI.

| phase | | |
|---|---|---|
| 1 — Engine | ✅ | C++ combat, action queue, seeded RNG, `clone()` |
| 2 — Python bridge | ✅ | pybind11 zero-copy, Gymnasium wrapper, action masking |
| 3 — RL baseline | ✅ | MaskablePPO trains; **M1** reached on the v0.x environment |
| 4 — Benchmarks | ✅ | 438k engine / 259k end-to-end steps/sec; **M2** |

## The reordering, and why

The original plan ran the algorithm comparison (**M3**) next, then expanded the
game afterwards. **That order is wrong, and the reason is worth recording.**

With a fixed deck against a single fight, there is no deck construction — and
deck construction is most of what makes Slay the Spire a hard decision problem.
Training three algorithms on the current environment would produce a rigorous
comparison on a narrow game, and the comparison would have to be redone once the
environment posed the real problem.

M1's result illustrates the cost of not noticing this: a PPO agent won 100% of
fights when the observation was 45 floats and the pool was 6 cards. The
environment is now 1772 floats and 189 cards. That result does not transfer, and
a comparison run today would age the same way.

**So expansion comes first.** M3 moves after v2.0.0.

## v2.0.0 — the run

An episode becomes **one run** rather than one fight. See
`docs/design/run-reward.md` for the reward design and the prior art behind it.

### The architectural claim

Every non-combat interaction — card reward, path choice, shop, event, rest — is
a **decision point**: the game stops and asks the player to pick one of N
options. That is exactly what the option-slot channel already does for Armaments
and Exhume (`docs/design/decision-points.md`), in a fixed-shape action space.

If that holds, v2 does not need a second action space, a second network, or a
macro/micro split. Miles Oram's project reported that split as its limitation —
his macro model "could not consider run context." Mini-spire can plausibly learn
combat and deck-building with **one policy**, which is the differentiator worth
building toward.

Where a decision does not fit the channel, the channel gets extended. That is
the design constraint, not a fallback.

### Phase 4.5 — design, and try to break it

**All of v2 is designed before any of it is implemented.** Not phase by phase:
the whole thing, then an adversarial pass whose explicit goal is to find the
case the spec cannot express.

The reason is that these five features share one action channel and one
observation. Designing them one at a time is how the v1 observation got out of
hand — each addition reasonable alone, the aggregate needing the ROB-40
redesign. A break found in design costs an afternoon; the same break found in
Phase 6 costs the layout, and v1.0.0 froze that layout, so a second break is a
second major version.

- [ ] Run state + episode boundary spec
- [ ] Decision-point taxonomy — one design doc covering all five
- [ ] Observation additions (deck, potions, gold, map, choices)
- [x] Reward — `docs/design/run-reward.md`
- [ ] **Adversarial pass**: for each decision type, find an instance that does
      not fit. Shops and events are the expected failure points.

**Exit criterion:** every one of the five decision types has a worked example
showing how it encodes, *including* the ones that needed the channel extended.
No implementation begins until a case that breaks the spec has been actively
hunted and either handled or written down as a known limit.

### Phase 5 — the run layer

The foundation everything else hangs off.

- [ ] Run state above `CombatState`: floor, persistent HP, persistent deck, potions
- [ ] Episode boundary — `reset()` starts a run, `step()` spans fights
- [ ] Non-combat decisions routed through the option-slot channel
- [ ] Observation additions: full deck, potions, map, current choices
- [ ] Potential-based reward with configurable coefficients

**Exit criterion:** a **walking skeleton** — three fights in sequence on a linear
path, HP and deck carrying across them, a card reward between each, terminating
after N floors. No map, no shop, no events, no boss.

That slice is deliberately the first target rather than a later one: it exercises
the entire loop end to end, so the architecture is proven before four features
are built on top of it. Every subsequent phase then widens a working thing
instead of completing an unproven one.

### Phase 6 — the decision points

Each is a decision point over the same channel; ordered by how much new
machinery each needs.

- [ ] **Card rewards** — pick 1 of 3, or skip. Simplest; already in the skeleton.
- [ ] **Rest sites** — rest vs. smith. Two options, but the first real
      resource-vs-investment tradeoff, and the case that motivated the reward design.
- [ ] **Path choice** — needs a map to exist at all. New state, new observation.
- [ ] **Events** — most varied; many are bespoke one-offs.
- [ ] **Shops** — most complex: multiple purchases, prices, gold as a resource.
      Miles reported shops as his most computationally expensive decision.

### Phase 7 — a complete act

- [ ] **Bosses.** A hard dependency of the reward design, not a finishing touch:
      without a terminal success condition the largest intended signal has
      nothing to attach to, and the scheme reduces to floor-counting.
- [ ] Single-path Act 1, start to boss
- [ ] **M5: an agent that completes a run**

### Phase 8 — the comparison (was Phase 5)

- [ ] PPO baseline on the run environment
- [ ] DQN baseline
- [ ] MCTS using `clone()` for rollouts
- [ ] Head-to-head on fixed seeds
- [ ] **M3: comparison results** ← workshop paper draft

**Exit criterion:** a table of win rates across algorithms, on an environment
that poses the actual problem.

### Phase 9 — beyond

- [ ] Memory architectures (LSTM / transformer policies) across floors
- [ ] Randomized enemy patterns
- [ ] Acts 2–3
- [ ] **M4: long-horizon memory across floors**

## Research milestones

| | goal | output | status |
|---|---|---|---|
| M1 | PPO > 80% win rate on a single fight | Bear Blog post | ✅ (on v0.x) |
| M2 | C++ throughput benchmark | README + benchmarks/ | ✅ |
| M5 | An agent that completes a full run | Blog post | v2.0.0 |
| M3 | PPO vs DQN vs MCTS on the run env | CoG workshop paper / arXiv | after v2.0.0 |
| M4 | Memory agents across floors | Extended paper | later |

M5 is new and sits before M3: "can anything complete a run" has to be answered
before "which algorithm completes it best" is a meaningful question.

## Compute plan

| phase | hardware | notes |
|---|---|---|
| 5–7 | M1 MacBook | Engine work, no training |
| 8 | Colab / RTX 3060 | Full sweeps; run-length episodes are far more expensive than fights |
| 9 | RTX 3060 | Memory architectures |

MCTS is CPU-bound and runs well on the M1 — no GPU needed until learned value
heads.

## Anti-goals

- All four acts, all characters
- A GUI or graphical renderer
- Containerized deployment
- Real-time play speed (training runs headless)
- Multiplayer or the daily-climb rulesets
