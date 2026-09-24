# Mini-Spire — Claude Code context

## Writing Style

Mannered prose substitutes metaphor and flourish for direct statement. Instead of "a parameter worth varying," the mannered writer produces "a dial worth turning." Instead of "this point still matters," they write "this point earns its keep." The phrases exist to display the writer, not to convey the idea, and readers can tell. That is why mannered prose irritates: it makes the reader work harder so the writer can perform. It is also imprecise. Metaphors drag in connotations the writer did not choose and cannot control. The fix is to say what you mean. When a literal phrase is available, use it.

**Please remove all mannered prose.**

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

### Non-negotiable rules

1. **Always prefer the dedicated tools over Bash/CLI for reading and searching
   code.** Use Read, Edit, Write, Glob ("Find"), and Grep ("Search") — NOT
   `cat`, `sed`, `grep`, `head`, `tail`, `echo`, or heredocs (`cat >> file`).
   The dedicated tools are more reliable (no shell-quoting/escaping bugs),
   reviewable (clean diffs), and harness-tracked. Bash is ONLY for things that
   are genuinely commands: builds (`cmake`/`ctest`), tests (`uv run pytest`),
   git, and running scripts. If a dedicated tool can do it, use the tool.

2. **Stay faithful to actual Slay the Spire — full parity, no simplifications
   for implementation convenience.** This is an RL *research* environment; the
   whole point is that agents train against the real game's mechanics. A
   logic simplification "to make implementation easier" defeats the purpose and
   is never acceptable. When the real game's behavior is more complex than the
   current model (e.g. a random turn-1 move, a constrained AI distribution, a
   start-of-turn power trigger), extend the engine to match it — do not
   approximate. If parity requires an engine change, that's the correct path.
   When unsure of the exact StS behavior, ask rather than guess.

### Rule 2 corollary: reimplementations are evidence, the wiki is the check

We source game mechanics from `sts_lightspeed` and `sts_map_oracle`. Both are
**reimplementations** — the best executable sources available, and not ground
truth.

**The wiki is [slaythespire.wiki.gg](https://slaythespire.wiki.gg)** — not the
Fandom one. Two wikis exist, they disagree, and wiki.gg is the maintained one
that `v2-spec.md` §0 and the design docs already cite. The Fandom wiki also
cannot be fetched from here: it returns **HTTP 402** to `WebFetch`. So a failed
wiki fetch means you reached for the wrong host, not that the wiki is down —
a whole relic batch was once sourced from a single source on that mistaken
conclusion.

**Every mechanic taken from one of them is cross-checked against the wiki before
it enters the spec or the engine.** This is not ceremony: the first check found
that a *display string* (`"Take 30% Hp damage."`) had been read as a spec when
the real formula is `floor(hp/10)*3`, and surfaced a pool constraint the code did
not encode at all.

**Decompiled StS1 Java is the strongest source** — it is the game, not a model of
it. Fetch with
`gh api repos/aeubanks/sts/contents/<path> -H "Accept: application/vnd.github.raw"`;
the repo root is the package root (`relics/`, `cards/`, `screens/`, `rooms/`, …).

It is not a free pass, and the failure mode is specific: **read the call chain,
not the one method.** Tiny House's `onEquip` adds no card reward, and the relic
grants a card anyway — `combatRewardScreen.open` does it, one call further down.
That shipped as a confident "the game does not grant a card", in bold, in a
design doc. An absent line is not an absent effect, which is the same rule as
"only reading proves absence" applied to a call graph instead of a grep.

Where two sources disagree, that is a flag rather than an answer — resolution has
gone both ways. Audit table and the standing policy:
`docs/design/prior-art-sts-lightspeed.md` §7.5–7.6.

### Bash discipline — rule 1, with teeth

Rule 1 has been violated repeatedly in practice, always with the same shape: a
shortcut that was faster to *write* and worse to *review*. A one-line
prohibition wasn't enough, so the failure modes are named here. **Recognize the
rationalization, not just the rule.**

**Bash is for commands. Dedicated tools are for content.**

| Task | Use | Never |
|------|-----|-------|
| Read a file, or a region of one | Read | `cat`, `head`, `tail`, `sed -n` |
| Change a file | Edit / Write | `sed -i`, `python3 - <<EOF`, `cat >> f <<EOF` |
| Locate a symbol / count occurrences | Grep, Glob (Bash `grep -n` only if the tool is unavailable) | — |
| Build, test, git, run a script | Bash | — |

**The three rationalizations that keep winning, and why each is wrong:**

1. *"This edit touches four files — one script is faster."* Four Edit calls is
   the correct cost, and they can go in a single message anyway. The script
   saves Claude time and costs Rob the reviewable diff. This one has won most
   often; treat "multi-file" as a reason to be *more* careful, not less.
2. *"It's only appending a block of tests."* Heredoc appends have already
   mangled comments in `card.h`. If content must be assembled first, Write it
   to the scratch directory, then Write/Edit the real file.
3. *"I only need to check whether X exists."* Legitimate — but see the next
   rule, which is the one that actually caused damage.

**Grep can prove presence. Only reading proves absence.**

A regex matches what you already expected to find, so zero hits proves nothing
except that your pattern didn't match. This has already produced a *false
all-clear* on this project: a sweep for "direct state mutations outside
executors" checked only `state.character` and `state.enemies`, only `+=` and
`-=`, and could not span newlines — so it never examined the card piles at all,
and reported **0**. A broader pattern found 53 hits. The number was published
as reassurance in a Linear issue before it was caught.

So: never report "there are no X" on the strength of a grep. Either read the
code, or state the claim as what it actually is — *"no matches for this
pattern"* — and say what the pattern couldn't have seen.

This matters most for **review and audit work**, where the whole deliverable is
a claim about what isn't there. It applies to subagents too: an agent that
greps will produce confident, wrong all-clears at scale.

**No test may depend on which card a random draw produced.**

A seeded run reproduces exactly on one platform and not across platforms:
`std::uniform_int_distribution` and `std::shuffle` are not specified to agree
between libstdc++ and libc++. A test fixtured on "seed 1 generates a cost-1
Attack" records a fact about macOS, not about the engine — and CI runs Linux.

This has broken three times. Two Chrysalis tests asserted `cost_override`
directly and failed on Linux, because 7 of the 28 pool Skills already cost 0 and
the engine skips the override for those. A TUI test then failed on its own
guard, `card_data(card).cost > 0`, which Linux could not satisfy once Infernal
Blade rolled Clash. The test beside it, fixtured identically, *passed* on Linux
while proving nothing: a card that is already free renders `{0}` whether the
panel reads the effective cost or the printed one.

So: **assert an invariant true of every draw, or stub the input.** The grant is
free whatever is rolled — assert that. A render test needs a fixed difference
between printed and effective cost — build it from a stub, never from a roll.
Searching seeds until one fits is neither; it only fails less often, and it
starts failing again when the pool changes.

The vacuous pass is the more dangerous of the two. A failing test reports
itself. A test that holds for the wrong reason reports nothing, and the only way
to tell them apart is to break the code on purpose and confirm the test notices.

**Stronger form, because the rule above was read too loosely.** *Unless the
randomness itself is the thing under test, a test must not depend on it —
period.* (Rob, 2026-09-24.)

"Assert an invariant true of every draw" is correct. "Sweep enough seeds that it
is overwhelmingly likely" is not, however good the odds, and the odds are how
this keeps getting rationalized. Three times in one session a sweep was defended
as safe:

- **Which card** a draw produced — 25 seeds, "at least one already-free Skill".
- **Which pool member** was offered — 40 seeds, "nearly half the candidates are
  colorless, so 0.12^40".
- **Which encounter** was sampled — 30 seeds, "the Weak pool contains groups".

Each defence was a probability argument about a fact the RNG had nothing to do
with. The third is the clearest: the test needed a two-enemy fight, and *which
encounter a seed draws is exactly the platform-dependent thing* ROB-100 is
about, so the sweep reintroduced the bug one layer up.

So, in order of preference:

1. **Test the pure function.** If the behaviour is "the pool is widened", call
   the pool function and check membership exhaustively. Expose a file-local
   helper if that is what it takes — `reward_pool` was lifted out of an
   anonymous namespace for exactly this.
2. **Construct the state you need.** When a test needs a particular *shape* —
   two enemies, a 0-cost card in hand — build it. `s.enemies.push_back(
   make_jaw_worm(rng))` is deterministic; sampling until a group appears makes
   the test depend on the sampler.
3. **Assert an invariant true of every draw.** Fine, and a loop over seeds is
   fine here, because the assertion holds for all of them.
4. **Assert a fact about the data.** "The pool contains a 0-cost Skill" proves a
   branch is reachable without generating anything.

The tell: if a test's assertion could fail on a different standard library while
the engine is correct, it is the wrong assertion.

### The interaction model

Claude is used in three modes:

1. **Brainstorming partner** — thinking through design options, tradeoffs,
   research questions. Claude offers perspectives but does NOT pick the answer.
2. **Explainer** — helping Rob understand a concept, pattern, or piece of C++/RL
   theory. Claude explains; Rob decides what to do with it.
3. **Implementer** — writing code against a design the two of us have agreed.

Claude does NOT:
- Suggest an overall solution approach when Rob hasn't formed one yet
- Generate code when the design is vague or unsettled
- Make design decisions on Rob's behalf

### The design gate

**Implementation requires an agreed written design.** The behaviour is settled in
a spec section or design doc, and Rob has agreed to it. That is the gate.

`docs/design/` is the source of truth. Linear issues point *at* it and never
restate it — a description that duplicates the spec is a description that will
drift from it.

Design happens as a discussion: Claude drafts or offers options, Rob pushes back,
we converge, and the result is written down before code starts.

**Claude's job:**

- If the design is vague or unsettled, **say so and design first**. Do not paper
  over it with reasonable-sounding assumptions.
- If implementing surfaces a question the design does not answer, **stop and
  raise it**. Design gaps found while coding are normal, not a failure.
- Never decide silently. Surface, recommend, and let Rob rule.

Useful private check when something feels underspecified: what does this
component do, what does it take, what does it return or mutate, and what is one
concrete way it could go wrong? If any of those is fuzzy, keep designing.

### The decomposition rule

No task should be larger than one coherent, testable change.

**Do not split a task the spec treats as one unit.** Splitting to make a task
look smaller invents dependencies that do not exist and defers work that belongs
together — `RunState`, its uid counter and its RNG streams are one piece of work
because each is unusable without the others.

**`v2-spec.md` §11 is the authority on implementation ordering.** Where a Linear
board disagrees with it, the spec wins.

"I'll figure out what this needs to do when I get there" is a red flag.

### Session task tracking

Track work in the session task list (TaskCreate / TaskUpdate / TaskList) all
the time, not only when Rob hands over a numbered list. **The list survives
conversation compaction.** A summary can drop a deferred follow-up; the task
list will not. That is why it works without mirroring anything to Linear.

- **Add a task the moment work appears**, not at the end of a turn: a stale
  reference found mid-sweep, a follow-up Rob defers, a ruling still owed, a
  benchmark to re-run. If a message you are writing says "later" or
  "follow-up", that item needs a task.
- **Put the evidence in the description.** Write deferred tasks so a context
  that remembers nothing can pick them up: measured numbers, files, the agreed
  fix, and what unblocks it ("after v2").
- **Keep one task `in_progress` at a time, and check it off only when it is
  done**: tests pass, and anything that needs Rob's approval (a commit) has it.
  Partial or blocked work stays open, with what blocks it written down.
- **Check TaskList when a task completes and after compaction.** Work in Rob's
  order (lowest id first) unless he re-sequences.
- **Keep it honest.** Delete superseded tasks, and update a description when
  the scope changes, so the subject never drifts away from the work.

The list is session working state, not a record. Decisions still go in
`docs/design/`. Work that must outlive the session, or be visible to anyone
else, still goes to Linear.

### What Claude should never do

- One-shot a large component with no agreed design behind it
- Offer a solution when Rob is still in problem-solving mode
- Let a vague design slide to be helpful — unhelpfulness here IS helpfulness

### The goal

**Rob owns the architecture and the design decisions.** Claude implements against
designs the two of us have agreed, and raises anything the design does not
settle.

Rob does not hold every implementation detail in his head — the project is past
that size, and the design docs exist so he does not have to. What he does hold is
every *decision*: what the environment does, why, and where that is written down.
`docs/design/` is what makes that durable, which is why keeping it accurate
outranks keeping it short.

---

## Scope: v1 (single combat encounter)

One fight only. No map, no shop, no relics, no meta-progression.

This is intentional. The combat engine alone poses a non-trivial RL problem:
energy management, card sequencing, blocking vs attacking, deck stochasticity.

**v1 is one fight, configurable:** the full Ironclad card pool, the Act 1 enemy
roster with their real AI tables, multi-enemy encounters, a configurable deck
and encounter. Win when every enemy is dead, lose at 0 HP. Reward is win/loss
plus optional HP shaping.

(This section used to describe a single enemy and a ten-card starter deck. Both
were true once. Counts and rosters live in the code — `CARD_DATABASE`,
`EnemyKind`, `src/encounter.cc` — not here.)

**Not v1 — these are v2.0.0:** map traversal, shops, events, rest sites, card
rewards, relics, potions, sequential fights.

## Architecture

Three layers, strict separation:

```
Python RL layer          (StableBaselines3 / custom loop)
      |
pybind11 boundary        (thin: reset, step, get_obs, action_mask)
      |
C++ game engine          (CombatState, Card, Enemy, TurnLoop, ActionQueue)
```

### C++ engine (src/ — headers and .cc together, no include/ split)

Read the headers for the type shapes; they change and this file will not track
them. `src/combat_state.h`, `src/card.h`, `src/enemy.h`, `src/turn_loop.h`,
`src/action.h`, `src/query.h`.

What is worth knowing that a header does not tell you:

- **The engine runs on an action queue**, not direct mutation. Effects are
  pushed as `Action` values and drained; the invariant is that the queue is
  empty at every agent decision point, and no resolution stays open while state
  changes. Design: `docs/design/effects-architecture.md`. Deliberate ordering
  divergences from StS are logged in `docs/design/ordering-notes.md` — read it
  before "fixing" an ordering, several are intentional and tested.
- **`Card` is an instance, `CardData` is the type.** A card in a pile carries
  only what distinguishes that copy; everything else is looked up by id.
- **Enemy AI is a Markov table**, not a cycle: transitions keyed on
  `(last move, consecutive count)`, with "cannot use X twice in a row"
  constraints encoded as enriched pseudo-move-states. `src/enemy.h`.
- **`CombatState::clone()`** is a plain copy and must stay that way — MCTS needs
  it, and it is why the queue and pending-choice records are POD with fixed
  arrays rather than heap types.
- **RNG is seeded and deterministic.** Every shuffle and random enemy move draws
  from the RNG in `CombatState`, so a seed reproduces a fight exactly. Changing
  *how many* draws happen changes every downstream fight — treat the RNG stream
  as an interface.

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

## Observation and action spaces

**Do not restate the layout in this file.** It used to hold a table, and that
table was wrong for the entire life of the project — it described a per-slot
one-hot hand the implementation never had, alongside a "~50–80 floats" total
that ended up off by more than an order of magnitude. Nobody noticed, because a
stale doc never fails a build. Point at the source instead.

**The spec** — what the spaces contain and *why* — is
`docs/design/observation-space.md`. Its §1 is the governing rule for anything
added to the observation:

> The observation should match what a human player can see.

That criterion does real work: it makes "the obs is too big" a non-argument (if
a human can see it, the size is the price of parity), and it makes "the agent
cannot see X, which a human can" a **defect** in the same category as an engine
parity bug. Related: `docs/design/decision-points.md` for the option-slot
channel.

**The authority** is the code, and it is exposed so nothing needs to hardcode it:

- Obs layout: `CombatEnv::kObsSize`, `kPlayerObsSize`, `kEnemyObsStride`,
  `kPileObsSize`, `kChoiceObsSize` in `src/combat_env.h`, surfaced to Python as
  `CombatEnv.OBS_SIZE`, `NUM_CARD_TYPES`, `NUM_DEBUFFS`, …
- Action layout: the v2 block table (`kActionBlocks`, `kTotalActions`) and
  `encode_action` / `decode_action` in `src/turn_loop.h`, surfaced to Python as
  `encode_action`, `decode_action` and `ActionBlock`. Layout spec:
  `docs/design/v2-spec.md` §6.

**Read those constants; never re-derive them — and never do offset arithmetic
outside the encoder.** Computing end-turn as `size - 1` was a real bug — the
option-slot channel sat after the combat block, so the last index was the
*decline* action. It broke the TUI and 13 Python tests at once. The v2 refactor
then found the same class of bug in `apply_action` (`action > kEndTurnAction`
meaning "illegal"), which is why `encode_action` / `decode_action` are now the
only code allowed to add or subtract a block offset.

**Action masking is non-negotiable.** Unmasked invalid actions produce
degenerate training where the agent learns to spam end-turn.

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
uv run minispire-play [seed]   # interactive human play (Textual TUI)
```

Human play is the Python `minispire-play` TUI, built on Textual. Two
predecessors were retired the same way — each once its replacement reached
parity, never left running alongside it: the C++ `minispire-cli`, then the
rich print-and-prompt loop it had replaced (ROB-83).

The TUI needs the optional `tui` extra; a bare install is engine + Gymnasium
only, so training does not pull a rendering stack. `minispire.render.require_tui()`
turns a missing extra into an instruction rather than an ImportError.

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
Standing decisions, and where the reasoning lives when it is longer than a row.

| Decision | Choice | Reason |
|----------|--------|--------|
| Scope | Single combat for v1 | Avoid Miles's complexity spiral; ship something clean |
| Human play | Textual TUI (`minispire-play`) | Chosen for the v2 roadmap's screens, not for combat; predecessors retired at parity (ROB-83) |
| RL framework | sb3-contrib MaskablePPO | Action masking built-in, fast iteration |
| Action masking | Yes, from day one | Highest-leverage training stability trick in game RL |
| State clone | Yes, from day one | Required for MCTS; painful retrofit |
| Reward (v1, one fight) | Sparse win/loss + optional HP shaping | Avoid playstyle bias |
| Reward (v2, one run) | Potential-based shaping over floors + HP | `docs/design/run-reward.md` — the one form of shaping that provably cannot bias playstyle |
| Python bridge | pybind11 zero-copy | Consistent with cpp-pettingzoo experience |
| Source layout | Flat `src/` (headers + .cc together) | Simpler than src/include split; small project doesn't need it |
| Effect resolution | Action queue, not direct mutation | `docs/design/effects-architecture.md` |
| Heterogeneous decisions | Option-slot channel, fixed obs shape | `docs/design/decision-points.md` |
| Per-instance card state | Part of card IDENTITY (rung ladders) | `docs/design/observation-space.md` §5 |
| What belongs in the obs | Whatever a human player can see | `docs/design/observation-space.md` §1 |

**When a decision needs more than a row, write a design doc and link it here.**
Do not inline the reasoning — this file is read at the start of every session,
and everything in it competes for attention with the working rules above.
