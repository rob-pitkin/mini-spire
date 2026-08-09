# v2 observation / decision-point — raw hunting notes

**Status: EXPLORATORY. Nothing here is decided.** These are attacks on the
existing spec, written down as found. Solving them individually is explicitly
not the goal — the goal is one mechanism general enough to cover all of them.

Working constraint (Rob, 2026-08-09): *"we do need a solution generic enough
that one size can fit all."*

---

## Attack 1 — `payload_id` becomes load-bearing exactly when it becomes wrong

**Status: real. Highest confidence of the lot.**

ROB-89 accepted `payload_id` as a known wart: a categorical encoded as a
magnitude, which is the same modelling error `observation-space.md` §5.2 rejects
sts2-rl-agent for. The acceptance rested on it being *bounded* —
`decision-points.md`: "one float per slot, in a channel that is fully masked off
during ordinary combat."

It is bounded **because it is currently unused.** For card choices the slot
index already carries identity, so `payload_id` is written as zero. In v2 it
becomes the *primary* identity encoding for map nodes, shop items and event
options, where no slot-index redundancy exists.

So the justification and the cost never overlap in time. It was cheap while
dormant and wakes up as the main encoding for three of the five new decision
types.

The doc names its own revisit trigger — "if v2's decision types turn out not to
need the shared channel, or if training shows the choice channel is where a
policy is losing" — and neither clause covers this case: the wart's *scope*
grows rather than its cost.

Also worth re-deriving rather than inheriting: the fix that would have worked
(index option slots by `CardId`) was rejected **for v2's sake** — it "buys a v1
improvement by spending generality that has not been used yet." That reasoning
was correct then. The generality is now being used, so the trade is no longer
the same trade.

**Direction to explore, not a decision:** only one `ChoiceKind` is ever live.
Payload could be a small one-hot over a **per-kind vocabulary** (a map node has
≤7 types; a shop ≤10 slots) rather than a magnitude over a global id space. Cheap
precisely because non-card decisions have small option sets — the 189 slots exist
only because a card pile may hold every card type.

## Attack 2 — shops are a chain, not a choice

**Status: probably real; a control-flow question, not an encoding one.**

The *shape* fits: a sequence of "pick one of N" plus a "leave" option. What may
not fit is the engine's model. Today a `PendingChoice` resolves once and the
drain continues. A shop needs a choice that **re-opens after resolving**, N
times, with affordability changing between iterations because gold was spent.

Open: does `PendingChoice` express "ask again"? Rest sites do not need this
(one choice), events mostly do not, card rewards do not. Shops and possibly
multi-card events do.

## Attack 3 — the map is terrain, not a decision

**Status: real, and it does not fit the framing at all.**

Path choice *as an action* is trivially "pick one of 2–4 exits". But a path's
value depends on rooms several floors ahead, so the observation needs the
**whole map** — roughly 15 floors × up to 7 nodes, with room types.

This is the largest single obs addition in v2, and the "the channel absorbs it"
story does not apply, because the map is not a choice. It is **terrain the
choice is made against**. Card piles are the closest existing analogue: standing
state the agent reads, not options it selects.

Consequence: v2's obs work splits in two. Decision *content* flows through the
channel; **persistent run state** (deck, potions, gold, map) is a separate
addition and is where the real growth is.

## Attack 4 — non-enumerable event payloads

**Status: real but probably small.**

Events offer options that are neither cards nor objects with ids: "gain 100
gold", "lose 8 HP", "fight the boss now". These need a vocabulary of their own.

Probably tractable — the set of event-option *kinds* is small and closed even
though the numbers vary. Related to Attack 1: whatever solves per-kind payload
vocabularies likely solves this too.

## Attack 5 — two-phase decisions

**Status: probably already handled.**

Smithing at a rest site is "pick smith, then pick a card". Precedented by combat
targeting and by Armaments, and the TUI already models two-phase selection
(`app.py` holds the pending card while the target is chosen). Noting it so it is
not rediscovered.

---

## Not yet hunted

- **Relics.** Deliberately out of the current five, but they modify *everything*
  and are the obvious next addition. A design that cannot absorb them is
  probably wrong.
- **Potions.** Usable at almost any point, including mid-combat — an action
  available in two different phases. Does the action space fork by phase?
- **Card removal / transform at shops** — picking from the *deck*, which can
  exceed the option-slot count if a deck grows past `kNumCardTypes` distinct
  cards. Probably safe (slots == 189 == every distinct card) but worth checking.
- **Whether the phase itself needs to be in the observation.** If the agent
  cannot tell "I am in a shop" from "I am in an event", the same option slot
  means different things. Currently `ChoiceKind` is in the header — check it
  generalises.

## Prior art

### NetHack Learning Environment — the closest structural analogue

[NLE (Küttler et al., NeurIPS 2020)](https://arxiv.org/pdf/2006.13760) is a
terminal roguelike with menus, inventory and thousands of object types. It is
the nearest thing to our problem that has been studied properly.

**It does not use one flat vector.** The observation is a **tuple of
heterogeneous components** — `glyphs`, `blstats`, `message`, `inventory` — each
with its own shape and semantics:

| component | shape / type | our analogue |
|---|---|---|
| `glyphs` | int array (21×79) of glyph ids, 0..5991 | the map |
| `blstats` | scalar vector: HP, gold, depth, hunger | player stats — we have this |
| `message` | byte array of game text | roughly our choice prompt |
| `inventory` | fixed-length (55) arrays of item ids + letters | our option slots / deck |

Two findings that bear directly on Attack 1:

**1. Categorical ids in the observation are standard and fine.** NLE ships raw
glyph ids up to 5991 as integers, and the baseline policies **embed** them. This
is not considered a modelling error — it is the normal way to represent a large
categorical space.

**2. So our actual problem is narrower than `decision-points.md` framed it.**
The issue is not "a categorical is in the observation". It is that ours is
**smuggled into a float32 vector where every other entry is a magnitude**, with
nothing marking which is which. NLE avoids that by giving categoricals their own
integer-typed component; the type *is* the signal.

That reframing matters, because `decision-points.md` rejected embeddings on the
grounds that "policy architecture is outside the environment's remit"
(`observation-space.md` §1). NLE suggests the environment's job is to *expose
the categorical as a categorical* — after which embedding is the policy's
business and the remit rule is satisfied rather than violated.

**Direction worth taking seriously (undecided):** a **structured observation**
(Gymnasium `Dict`/`Tuple`) rather than one flat `Box`. Categoricals — map node
ids, shop item ids, event option ids, and `payload_id` itself — live in an
integer component; magnitudes stay in the float component.

Why this is the "one size fits all" candidate rather than five fixes:

- Attack 1 dissolves — `payload_id` stops being a false ordinal because it stops
  pretending to be a magnitude.
- Attack 3 (the map) gets NLE's `glyphs` treatment: a 2D categorical grid, which
  is exactly what a Slay the Spire map is.
- Attack 4 (event payloads) becomes another small categorical vocabulary.
- Fixed-length padded slots — how NLE does inventory — is already our pattern
  for enemy slots and option slots, so it generalises rather than conflicts.

Costs to weigh, not yet weighed: `sb3-contrib` MaskablePPO does support `Dict`
observations, but it is a real break from the current flat contract, and every
consumer (TUI, benchmarks, `test_bindings.py`) reads offsets today. v2 breaks
the layout anyway — the question is whether it breaks *shape* or *type* as well.

### Parameterised action spaces — looked at, not applicable

The [parameterised action space](https://arxiv.org/html/1511.04143v5) literature
(P-DQN, MP-DQN) covers discrete actions carrying continuous parameters — robot
soccer's "kick(direction, power)". Ours is entirely discrete and heterogeneous in
*kind*, not in type. Noted so it is not re-researched.

### Still to look at

Hanabi (phase-structured, small), MTG/Hearthstone agents (card games with
heterogeneous decisions and huge card pools), and board-game RL with distinct
phases (Catan, Terraforming Mars).
