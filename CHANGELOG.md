# Changelog

All notable changes to Mini-Spire are recorded here.

This project follows [Semantic Versioning](https://semver.org/). For an RL
environment **the observation and action spaces are the public API**, so a
change to either is a breaking change — see [What 1.0 promises](#what-100-promises).

## [1.0.0] — 2026-08-03

First release. A complete, faithful Slay the Spire combat encounter as a
Gymnasium environment, with a C++ engine and zero-copy Python bindings.

### The environment

- **Full Ironclad card pool** — 189 card types, including the awkward ones:
  cards that pause mid-resolution to ask a question (Armaments, Exhume, Warcry,
  Dual Wield), cards whose cost changes as the fight goes on (Blood for Blood,
  Infernal Blade), and cards that grow permanently (Rampage, Searing Blow).
- **The Act 1 enemy roster** — 23 enemies with their real AI tables, modelled as
  Markov transitions on `(last move, consecutive count)` rather than fixed
  cycles, with "cannot use X twice in a row" encoded as enriched move-states.
- **Multi-enemy encounters** with explicit targeting, splitting slimes, fleeing
  Looters, and cross-enemy effects (Shield Gremlin protects an ally).
- **Deterministic** — every shuffle and enemy move draws from one seeded RNG, so
  a seed reproduces a fight exactly.
- **Action masking** in the convention `sb3-contrib`'s `MaskablePPO` expects.
- **`clone()`** for MCTS: `CombatState` is value-typed throughout, which is why
  the action queue and pending-choice records are POD with fixed arrays.

### Architecture

- **Effects resolve through an action queue**, not direct mutation. Effects are
  pushed as `Action` values and drained; the invariant is that the queue is
  empty at every agent decision point, so no resolution stays open while state
  changes. See `docs/design/effects-architecture.md`.
- **Per-instance card state is part of card identity.** A Rampage that has grown
  to 23 damage is a different `CardId` from a fresh one, so the observation
  cannot alias two copies that will do different things. Ladders are enumerated
  over the exactly-reachable set, with a saturating top rung. See
  `docs/design/observation-space.md` §5.
- **Heterogeneous decisions use an option-slot channel** with a fixed shape, so
  a mid-card choice does not change the observation or action size. Sized so
  truncation is structurally impossible. See `docs/design/decision-points.md`.

### Correctness

- Every card and enemy was **verified against the wiki one by one**, which
  turned up and fixed ten defects — Flex's temporary Strength, Ritual's timing,
  Spore Cloud's trigger, and the Gremlin roster's numbers among them.
- An **architecture review** audited the action-queue migration and found, among
  others, that on-death hooks never fired for kills dealt by Combust or Flame
  Barrier — so Fungi Beast's Spore Cloud silently did nothing.
- Deliberate divergences from the real game's ordering are documented and tested
  in `docs/design/ordering-notes.md` rather than left as folklore.

### Performance

- **438k engine steps/sec** on an M1 MacBook Pro (Release build, measured
  through `CombatEnv` rather than a raw state loop, median of 5 trials).
- **259k steps/sec end to end** through the Python bindings — mask, action,
  step, reset, timed from outside the loop with nothing subtracted. That is
  what an RL user actually gets; the 1.7× gap to the engine is the cost of
  crossing into Python once per step.
- `benchmarks/README.md` documents how to reproduce both. The numbers in this
  repo are measured, not remembered — `benchmarks/results.json` is regenerated
  rather than edited.

### Human play

- `minispire-play` — a Textual terminal UI with 12×12 pixel-art avatars, full
  card text, telegraphed enemy intents, keyboard navigation and browsable piles.
  Every fight writes a JSONL trajectory log.

### Install

- `pip install minispire` is the engine and the Gymnasium env only. The terminal
  UI (`[tui]`), the training stack (`[train]`) and plotting (`[analysis]`) are
  opt-in extras, so training does not pull a rendering library.

### What 1.0.0 promises

- **The card pool is frozen at 189 types.** Adding a card changes
  `kNumCardTypes`, which changes both the observation and the action space —
  that is a 2.0.0 change.
- **The observation and action layouts are frozen for 1.x.** Their landmarks
  (`OBS_SIZE`, `NUM_ACTIONS`, `END_TURN_ACTION`, `FIRST_OPTION_SLOT`,
  `DECLINE_ACTION`, `NUM_CARD_TYPES`, `TURN_OBS_INDEX`) are exposed as constants
  and **must be read, not hardcoded** — deriving them by hand has caused real
  bugs in this repo more than once.
- **Python is the supported surface.** The C++ API is not a stability promise
  for 1.x.

### Not in 1.0.0

Map traversal, shops, events, rest sites, card rewards, relics, potions and
sequential fights are 2.0.0. The option-slot channel is designed to absorb those
new decision *kinds* without an observation shape change, so the break should be
smaller than it sounds.
