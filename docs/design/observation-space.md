# Observation space — design review (ROB-40)

**Status: ACCEPTED (§5 A2 + §6 B1 + B2 + B3).** Ruled by Rob, all parameters
settled (§8). Not yet implemented — §9 is the spec, §10 the blast radius.

Second draft. The first draft's options (hand slot blocks, annotation lists,
choice-based disambiguation) survive below but are demoted: further research
turned up a direct negative result against slot-indexed actions on StS itself,
and a cheaper option family the first draft missed entirely.

### Accepted design, in one paragraph

Per-instance card state stops being a hidden counter and becomes part of card
**identity**: Rampage and Searing Blow gain a ladder of `CardId` rungs, grown by
ID swap exactly like every other upgrade, with a **saturating top rung** so the
engine keeps counting internally and mechanical parity stays exact (§5 A2).
Because two copies sharing a full ID are then behaviorally interchangeable, the
"which copy do I play" problem dissolves rather than being solved — no hand
slots, no pointer networks, no disambiguation prompts, and the existing
`card_id × target` action space is already complete. Alongside it, four scalars
that a human can see but the observation hid are exposed: `hp_loss_events` and
`combust_casts` as player-block floats (B1), Battle Trance's "No Draw" promoted
to a real `Debuff::NoDraw` so it lives in the debuff block the way the game
renders it (B2), and Infernal Blade's discount as a fifth per-card-type count
plane (B3).

---

## 1. The governing principle

> The observation should match what a human player can see — or get as close as
> possible. The environment's job is an adequate observation for a black-box
> agent; policy architecture is not this project's problem to solve.

Consequences:

- "The obs is too big" is not an argument by itself. If a human sees it, size is
  the price of parity.
- "The agent cannot see X, which a human can" is a **defect** — same category as
  the ROB-85 Flex/Ritual bugs.
- Anything a human *cannot* see (enemy AI internals, draw-pile order) stays out.

### 1.1 The formal version

"Adequate for a black-box agent" has a standard name: the observation must not
**alias** states that behave differently (state abstraction / bisimulation —
Li, Walsh & Littman 2006). If two situations produce identical observations but
different outcomes under the same action, no amount of experience can teach a
memoryless policy the difference.

Rob's framing — *"the agent just sees Searing Blow+ and knows from experience
what it does"* — is exactly this criterion, and it is the right one. It works
**iff the ID fully determines the card's behavior.** Where it holds (every
ID-swapped upgrade: Strike+ always deals 9), no per-instance machinery is
needed. Where it fails, experience cannot compensate:

- Two combats, identical observations, Rampage in hand. In one it has +0, in
  the other +15 (hidden `bonus_damage`). Same action, different outcome — the
  states are aliased and the policy cannot learn either correctly.

So the audit question for every piece of state is not "do we represent
instances?" but "**does anything behaviorally relevant escape the ID?**"

---

## 2. Current state

`OBS_SIZE = 1200`, `NUM_ACTIONS = 926`, `kNumCardTypes = 154`.

| Block | Size | Share |
|---|---:|---:|
| player (5 + 4 debuffs + 22 powers) | 31 | 2.6% |
| enemies (5 × 17) | 85 | 7.1% |
| piles (4 × 154 counts) | 616 | 51.3% |
| turn | 1 | 0.1% |
| choice (5 + 154 × 3) | 467 | 38.9% |

90% of the obs scales with `kNumCardTypes`. The piles are parity-correct (a
player can inspect pile contents but not order — a count vector is exactly
that), so their 616 floats are justified, not bloat.

---

## 3. Coverage audit — everything that escapes the observation

Full sweep of `CombatState` against the obs writer (`combat_env.cc`). The first
draft found two gaps; there are six.

| # | Hidden state | Human-visible how | Behavioral effect | Escapes via |
|---|---|---|---|---|
| 1 | `Card::bonus_damage` (Rampage) | printed damage on the card | damage | aliased ID |
| 2 | `Card::upgrades` (Searing Blow) | card name/number | damage | aliased ID |
| 3 | `free_this_turn` (Infernal Blade) | card shows cost 0 | effective cost | unobserved map |
| 4 | `hp_loss_events` (Blood for Blood) | card shows reduced cost | effective cost | unobserved counter |
| 5 | `combust_casts` | Combust power tooltip ("lose N HP") | end-turn HP loss | unobserved counter |
| 6 | `no_draw_this_turn` (Battle Trance) | "No Draw" debuff icon | draws blocked | unobserved flag |

All six are information StS displays to the player. #1–2 are per-instance; #3
is per-type, turn-scoped; #4–6 are single scalars. The fix families differ, so
options are organized by family below.

Everything else checks out: pile counts, all 26 powers/debuffs with stacks,
live enemy intent (`intent_attack_dmg` recomputed from current Strength — the
standard ROB-85 note 25 established), the choice block's per-option instance
damage. Enemy AI state (`consecutive_count`, enriched move names) is correctly
absent — a human cannot see it either.

---

## 4. Prior art

### 4.1 The decisive result: slot-indexed actions failed on StS

decapitate-the-spire (Brewer-Krebs) used `hand_index × target` actions and
reports the failure directly:

> "it's hard for an agent to learn that playing 'card in hand at index 4' only
> does 6 damage to a monster when the environment indicates that the card at
> index 4 is Strike. … It's much simpler if 'pressing a button' always has the
> same effect."

They abandoned slot indexing for card-identity actions (plus autoregressive
sub-choices). https://www.toypiper.com/creating-an-ai-for-slay-the-spire/

This is the same indirection cost the first draft listed theoretically, observed
empirically, on this game. Our current `card_id × target` space is the shape
they arrived at *after* trying the alternative.

### 4.2 Quantized state is the norm where state is discrete and bounded

- **Pokémon** RL encodes stat boosts as the game's own quantized stages
  (−6…+6), statuses as categories, moves as identities — nobody encodes "a
  slightly stronger Tackle" as a continuous annotation; the stage ladder *is*
  the representation (VGC-Bench, https://arxiv.org/pdf/2504.04395).
- **DouZero** (Doudizhu, SOTA): hand = count matrix per rank. Counts lose
  nothing because Doudizhu cards carry no instance state — the boundary case
  proving counts are right exactly until state escapes the ID.
  https://arxiv.org/pdf/2106.06135
- **Hearthstone / AlphaStar** sit in the regime where per-entity state is rich
  and unbounded (arbitrary buffs; 512 units) — entity encoders and pointer
  networks are the tool *there*. StS combat instance state is neither rich nor
  unbounded: two cards, fixed increments.

### 4.3 The engine's own precedent

`CARD_UPGRADES` already reifies the most common instance state — upgrades — as
**ID swaps** (Strike → StrikePlus via `upgraded_card`). Every ID-swapped
upgrade satisfies §1.1 by construction. Searing Blow and Rampage are the two
cards that escaped the pattern *because their state looked unbounded*. The
option below is the observation that in a single combat it is not.

---

## 5. Option family A — extend the ID space (the missing option)

**A card's rung on its ladder becomes part of its identity.** Searing Blow's
ladder is `@0 … @N`; Rampage's is its bonus in increments of its growth. The
count/plane architecture, the choice channel, and the action space are all
untouched in *shape* — `kNumCardTypes` grows by ~15 and everything scales as it
did when the pool grew 102 → 154 (zero re-encoding, by design).

What this buys, all at once:

- **Sight**: a `Rampage@+15` in hand, discard, *or* a choice list is a distinct
  category the agent knows "from experience" — Rob's criterion, satisfied
  literally. Fixes audit #1–2 in the hand **and the piles** (§4.2 of the first
  draft's defect list) with the same mechanism.
- **Selection dissolves**: two copies of the same full ID are behaviorally
  interchangeable, so "which copy" stops being a question. The action space is
  already complete. No slots, no pointer networks, no disambiguation choices.
- **The ROB-85 Searing Blow+ landmine** becomes principled: `SearingBlowPlus`
  *is* rung 1.

Reachability bounds the ladders: Searing Blow only upgrades via Armaments plays
(realistically ≤ ~5/combat); Rampage grows +5/+8 per play (≤ ~10 plays in a
degenerate stall). Caps of that order make the tail unreachable in practice.

### A1 — obs-level rungs (engine untouched)

The obs writer maps `(card_id, instance)` → rung index; the engine keeps its
counters. **Fixes sight only.** When two copies at different rungs share the
hand, `play Rampage` is still ambiguous and the engine must pick canonically —
and no canonical rule is strategy-free (concentrate one Rampage vs. spread
plays is a real decision). Named so it can be rejected knowingly.

### A2 — engine-level rung IDs, saturating top rung (both fixed)

Rungs are real `CardId`s; growth/upgrade is an ID swap like every other
upgrade; the instance counters disappear below the cap. At the top rung the
engine **keeps counting internally** (`bonus_damage` retained), so engine parity
stays exact everywhere — only the obs/action distinction saturates. States
beyond the cap are aliased *with each other* (all read "top rung"), which is the
unreachable tail by construction.

- Fixes sight and selection. Distinct rungs are distinct actions.
- Parity cost: zero below cap; aliasing above it. Recorded like other accepted
  divergences if adopted.

### A3 — engine-level rungs, hard cap (Rob's original proposal)

"Only allow a single upgrade on Searing Blow" — the engine itself stops at the
cap; upgrades beyond it no-op.

**Pushback, per invitation:** this is the first option in the project's history
that changes *engine mechanics* for representational convenience, which is
exactly what non-negotiable rule 2 prohibits ("a logic simplification to make
implementation easier … is never acceptable"). Searing Blow's printed text is
"can be upgraded **any number of times**" — a hard cap is a visible break on the
card's defining line. A2 delivers the same observation and action space with no
mechanical deviation; the only thing A3 buys over A2 is deleting one internal
counter. If A3 is chosen anyway, that is Rob's ruling to make — but it should
be recorded as the deliberate parity exception it is, alongside Curl Up.

### The mixed-increment wrinkle (affects A1/A2/A3 equally)

Armaments can upgrade a grown Rampage: +10 (two base plays) then upgraded, then
+8/play → bonuses like 18, 26 that sit between either variant's pure multiples.
Choices: (a) enumerate the reachable set exactly (a few dozen IDs), or
(b) fixed-step buckets with round-to-nearest (aliasing ≤ half a step, only for
mixed histories). Knob is Rob's; (b) at step 5 is the modest default.

Size math for A2 with caps SB@5, Rampage +30/+40:
`kNumCardTypes` 154 → ~169 · obs 1200 → ~1305 · actions 926 → ~1016.

---

## 6. Option family B — expose the unobserved scalars (audit #3–6)

Independent of family A; some subset is needed under *any* hand decision.

- **B1 — scalars into the player block**: `hp_loss_events`, `combust_casts` as
  two floats. Trivial, no downside found. (#4, #5)
- **B2 — No Draw as a real Debuff**: StS shows it as a debuff icon; modelling it
  as `Debuff::NoDraw` puts it in the existing debuff block and retires the
  `no_draw_this_turn` bool. Slightly more engine work than a raw float, but the
  representation then matches the game's own. (#6)
- **B3 — free-this-turn plane**: a fifth 154-wide count plane ("free copies of
  type T this turn"), consistent with the pile-plane architecture. +154 floats,
  almost always all-zero. (#3)
- **B4 — defer #3**: the discount is turn-scoped and self-inflicted (the agent
  just played Infernal Blade); accept as a recorded gap. Cheaper, but it is the
  exact "predicted cost 1, paid 0" mismatch the parity principle calls a defect.

---

## 7. Options carried from the first draft, demoted

Kept for exhaustiveness; each now has a concrete strike against it.

- **C1 — hand slot block** (per-slot features, replacing or alongside counts):
  solves sight for the hand only — piles and choice channel need separate
  mechanisms; adds a second representation to keep consistent; and its natural
  action-space partner is the one with the §4.1 negative result. A
  canonical-sort variant (order slots by `(id, rung)`) restores permutation
  invariance at the env level but keeps all the rest.
- **C2 — slot-indexed actions**: tried on StS, reported harmful, abandoned by
  its own author (§4.1). Also: state-dependent semantics, slot-shift ambiguity
  mid-turn, arbitrary draw order encoded into the interface.
- **C3 — sparse annotation block**: fixed-capacity list of "this copy differs"
  entries; can truncate (the failure mode `decision-points.md` engineered out of
  the choice channel); doesn't cover displayed-value parity for ordinary cards.
- **C4 — choice-channel disambiguation** ("which copy?" prompt): solves
  selection without sight — the agent commits to playing Rampage before it can
  see the copies differ. Dissolves entirely under A2.
- **C5 — recurrent policy / frame stacking**: let the model track history
  instead of fixing the obs. Contradicts §1 (pushes the burden onto the policy,
  and this project deliberately uses stock MaskablePPO); listed because the
  POMDP literature would name it.
- **C6 — accept and document**: always available; the audit table is the list
  of what would be knowingly wrong.

### Fit against the settled constraints

| | invariance | pile info kept | categorical | instance sight | instance selection | stock policy OK |
|---|---|---|---|---|---|---|
| A2 + B1/B2 (+B3) | ✓ | ✓ | ✓ | ✓ (everywhere) | ✓ (dissolved) | ✓ |
| A1 + B* | ✓ | ✓ | ✓ | ✓ | ✗ (canonical rule) | ✓ |
| C1 + C2 | needs sort/arch | ✓ | ✓ | hand only | ✓ | ✗ (or sort) |
| C3 | ✓ | ✓ | ✓ | partial, truncates | ✗ | ✓ |
| C6 | ✓ | ✓ | ✓ | ✗ | ✗ | ✓ |

---

## 8. Decisions

1. **Family A variant** — **RULED: A2** (engine-level rung IDs, saturating top
   rung). A3's hard cap was declined: capping Searing Blow in the engine would
   break "can be upgraded any number of times", and A2 delivers the identical
   observation and action space with no mechanical deviation.
2. **Caps and ladder construction** — **RULED: exact reachable set**, no
   bucketing, no aliasing below the caps. Enumerated in §9.
3. **Family B subset** — **RULED: B1 + B2 + B3.** All four hidden scalars are
   exposed; B4 (defer the Infernal Blade discount) declined in favour of the
   fifth plane.
4. **Timing** — **RULED: before v1.0.0** (ROB-86). The release must not publish
   an `OBS_SIZE`/`NUM_ACTIONS` pair we already know we are going to break.
5. Reconcile `CLAUDE.md`'s observation table ("one-hot per card slot"), which
   has never matched the implementation (per-type counts since ROB-40). Folded
   into the implementation work.

---

## 9. The ladders, enumerated

Exact reachable sets. A rung exists iff a real play sequence can produce it;
nothing is bucketed, so no two distinct states alias below the caps.

### 9.1 Searing Blow — 6 rungs

Damage `n(n+7)/2 + 12`. Climbs only via Armaments / Armaments+ plays.

| rung | 0 | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| damage | 12 | 16 | 21 | 27 | 34 | 42 |

Rung 1 **is** today's `SearingBlowPlus` — the ROB-85 landmine (a
default-constructed `Card{SearingBlowPlus}` dealing 12) stops being a special
case and becomes structurally impossible. Net +4 IDs.

### 9.2 Rampage — 7 rungs

`+5` per play. Cap `+30` (6 plays). Net +6 IDs.

`+0, +5, +10, +15, +20, +25, +30`

### 9.3 Rampage+ — 26 rungs

This is the ladder that made bucketing tempting. A Rampage may be grown `a`
times at `+5`, then upgraded by Armaments, then grown `b` times at `+8` — so the
reachable set is `{5a + 8b}`, bounded by `a ≤ 6` (§9.2's cap) and `≤ +40`.
Rampage+ cannot be upgraded again (absent from `CARD_UPGRADES`), so there is
exactly one transition and no deeper nesting.

```
a=0:  0   8  16  24  32  40
a=1:  5  13  21  29  37
a=2: 10  18  26  34
a=3: 15  23  31  39
a=4: 20  28  36
a=5: 25  33
a=6: 30  38
```

Union, sorted (26 values):

`0, 5, 8, 10, 13, 15, 16, 18, 20, 21, 23, 24, 25, 26, 28, 29, 30, 31, 32, 33,
34, 36, 37, 38, 39, 40`

Note the genuine gaps — `22`, `27`, `35` are **not** reachable under `a ≤ 6` and
must not be given rungs. A generator should compute this set rather than
transcribe it; transcribing positional tables by hand is exactly the failure
mode `dump_cards.cc` exists to guard against. Net +25 IDs.

### 9.4 Resulting constants

| | before | after |
|---|---:|---:|
| `kNumCardTypes` | 154 | **189** |
| `kNumDebuffs` (B2 adds NoDraw) | 4 | **5** |
| pile planes (B3 adds free-this-turn) | 4 | **5** |
| `OBS_SIZE` | 1200 | **1642** |
| `NUM_ACTIONS` | 926 | **1136** |

Derivation of 1642: player `5 + (5+22) = 32`, plus B1's 2 scalars = 34;
enemies `5 × (3 + (5+6) + 4) = 90`; piles `5 × 189 = 945`; turn `1`; choice
`5 + 189 × 3 = 572`.

**Saturation (A2):** at a cap the engine keeps incrementing `bonus_damage` /
`upgrades` internally, so damage stays exactly right forever. Only the
observation and action encoding saturate — states past the cap alias with each
other, in a region no realistic combat reaches.

## 10. Blast radius

`card.h` (CardId enum + ladder rows + `CARD_UPGRADES` entries), `query.cc`
(rung-aware damage; retire the SearingBlowPlus baseline special-case),
`turn_loop.cc` (growth becomes ID swap; instance counters retained only above
cap), `combat_env.{h,cc}` (constants recompute; B scalars/planes),
`status_effect.h` if B2, `bindings/_core.cc`, the mask oracle, Python shape
tests, `decision-points.md` cross-reference, blog figures, `CLAUDE.md`.

Notably *not* touched: obs architecture (still counts + planes + choice),
action encoding scheme, `decision-points.md`'s option-slot design, any pile
logic.
