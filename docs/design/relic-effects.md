# Relic and potion effects — trigger taxonomy

**Status: AGREED (Rob, 2026-09-12).** The taxonomy and the expression model are
settled; implementation follows the batching in §5.

This is task A of the §11 step 7 remainder: classify all 140 relics by *where
they fire*, so the hook architecture is designed against the real distribution
rather than a guess.

**Decisions taken:**

- **§4.1 — expression model: Option B**, a `switch (id)` at each trigger site.
  Not the declarative `CardData`-style table proposed first: costing it against
  the real `initRelics` showed only ~8–10 of 46 combat-start relics fit a
  uniform "gain N of X" row, so a declarative table would have been a dispatch
  table with 80% of its entries deferring to an escape hatch. The deciding
  argument is parity review — *"at combat start, in what order, does what
  happen?"* is the question we ask repeatedly, and B is the only shape that
  answers it by reading one function top to bottom.
- **§4.2 — relic ORDER: half of it is now settled by implementation.** The
  *engine* ordering question turned concrete in batch 4 and is answered in §6.7:
  relics fire **sequentially**, not batched, wherever one can read what another
  just changed. What remains deferred is only whether the OBSERVATION exposes
  acquisition order, below.
- **§4.2 — relic order in the observation: deferred, not blocking.** The engine
  fires in acquisition order for parity regardless of what the observation
  shows, so `relics` vector order is load-bearing either way and no batch below
  depends on the answer. The v2 observation is not built, so adding a channel
  later costs nothing that §5.4's published constants do not already cover.
  Revisit if an implemented relic turns out genuinely order-sensitive.

---

## 1. What this document settles, and what it does not

**Settles:** which trigger point each of the 140 relics hangs off, and therefore
what set of trigger points the engine needs.

**Does not settle:** the exact effect of any individual relic — the numbers.
Those are cross-checked against the wiki *per implementation batch*, immediately
before they enter code, for the reason §15 correction #11 exists: a display
string read as a spec (`"Take 30% Hp damage."` against a real `floor(hp/10)*3`)
is the failure mode that matters, and it is caught by checking a number, not by
checking a category.

Classifying first and verifying numbers per batch also keeps each wiki check
adjacent to the code it validates, rather than 173 checks in one pass whose
results have gone stale by the time they are used.

---

## 2. Sourcing: what `sts_lightspeed` could and could not tell us

The taxonomy below was derived by mapping every `R::` / `RelicId::` reference in
`sts_lightspeed` to its enclosing function — the enclosing function *is* the
trigger point, so this reads its taxonomy directly rather than inferring one.

**This worked well and is the bulk of the classification below.** Two cautions
came out of doing it, both worth recording.

### 2.1 A narrow search reports zero, not "narrow"

Searching only `BattleContext.cpp` and `GameContext.cpp` — the two obvious files
— left **31** relics with no trigger site. That list included The Boot and
Centennial Puzzle, both plainly live combat relics, which is what made the result
obviously wrong rather than quietly wrong.

Widening to every `.cpp` brought it to 16; most of the missing hooks were in
`Player.cpp`. Nothing about the first result announced that it was partial.

### 2.2 `sts_lightspeed` does not implement every relic — and the gaps are structural

Of the remaining 16, **seven appear nowhere in the reference at all**:

| relic | real effect | consequence for the taxonomy |
|---|---|---|
| **The Boot** | unblocked attack damage ≤ 4 → raise to 5 | needs an **outgoing damage modifier** hook |
| **Gremlin Horn** | on enemy death: +1 energy, draw 1 | needs an **on-enemy-death** hook |
| **Hand Drill** | on breaking enemy Block: 2 Vulnerable | needs an **on-block-broken** hook |
| Toy Ornithopter | on drinking a potion: heal 5 | existing potion hook |
| Orrery | shop: choose 5 cards to add | run layer |
| Dolly's Mirror | shop: duplicate a card | run layer |
| Frozen Eye | see draw pile order | observation only |

The first three matter beyond their own row: **three trigger categories exist in
the real game that `sts_lightspeed`'s taxonomy has no site for.** A taxonomy
derived purely from its trigger sites would not contain them, and those seven
relics would have been silently classified "no effect" — a dead-index bug of
exactly the kind §5.1 rule 5 exists to prevent, arrived at by trusting a
reimplementation as an oracle.

The remaining nine of the 16 are present but only behind spawn gating
(`relicCanSpawn`), which is not an effect: Singing Bowl, Darkstone Periapt, Old
Coin, Shovel, Wing Boots, Peace Pipe, plus Ginger / Turnip / Champion Belt, which
live in headers as query modifiers rather than event hooks (§3.3).

**Standing rule this confirms:** `sts_lightspeed` is evidence of how a mechanic
*can* be structured, never evidence that a mechanic does not exist. Absence there
is a fact about that codebase.

---

## 3. The taxonomy

Three kinds of hook, which is the shape the classification actually has rather
than one imposed on it.

### 3.1 Event triggers — combat

Fire at a moment. Push `Action`s onto the queue like everything else; no relic
mutates state directly (`effects-architecture.md`).

| # | trigger | n | relics |
|---|---|---|---|
| 0 | **CombatStartPreDraw** | 1 | Toolbox — resolves BEFORE the opening hand is dealt, and can PAUSE the fight there (§6.9) |
| 1 | **CombatStart** | 45 | Akabeko, Anchor, AncientTeaSet, BagOfMarbles, BagOfPreparation, BloodVial, Brimstone, BronzeScales, BustedCrown, ClockworkSouvenir, CoffeeDripper, CursedKey, DuVuDoll, Ectoplasm, FossilizedHelix, FusionHammer, GamblingChip, Girya, GremlinVisage, HappyFlower, IncenseBurner, InkBottle, Lantern, LizardTail, MarkOfPain, MercuryHourglass, NeowsLament, Nunchaku, OddlySmoothStone, Omamori, Pantograph, PenNib, PhilosophersStone, PreservedInsect, RedSkull, RunicDome, SlaversCollar, SlingOfCourage, SneckoEye, Sozu, Sundial, ThreadAndNeedle, Vajra, VelvetChoker, WarpedTongs |
| 2 | **TurnStart** | 8 | ArtOfWar, Brimstone, CaptainsWheel, HappyFlower, HornCleat, IncenseBurner, MercuryHourglass, OrangePellets |
| 3 | **EnergyRecharge** (within turn start) | 5 | Akabeko, IceCream, InkBottle, LetterOpener, Nunchaku |
| 4 | **TurnEnd** | 2 | Orichalcum, StoneCalendar |
| 5 | **CombatEnd** | 10 | BurningBlood, HappyFlower, IncenseBurner, InkBottle, LizardTail, MeatOnTheBone, NeowsLament, Nunchaku, PenNib, Sundial |
| 6 | **PlayAttack** | 7 | InkBottle, Kunai, Nunchaku, OrangePellets, OrnamentalFan, PenNib, Shuriken |
| 7 | **PlaySkill** | 4 | InkBottle, LetterOpener, MummifiedHand, OrangePellets |
| 8 | **PlayPower** | 4 | BirdFacedUrn, InkBottle, MummifiedHand, OrangePellets |
| 9 | **PlayStatusOrCurse** | 3 | BlueCandle, InkBottle, MedicalKit |
| 10 | **AfterAnyCard** | 1 | StrangeSpoon |
| 11 | **Exhaust** | 2 | CharonsAshes, DeadBranch |
| 12 | **Shuffle** | 2 | Sundial, TheAbacus |
| 13 | **HpLost** | 4 | CentennialPuzzle, RedSkull, RunicCube, SelfFormingClay |
| 14 | **Damaged** | 1 | TungstenRod |
| 15 | **PlayerDeath** | 2 | LizardTail, SacredBark |
| 16 | **DrinkPotion** | 2 | SacredBark, **ToyOrnithopter** |
| 17 | **ObtainPotion** | 1 | Sozu |
| 18 | **AfterMonsterTurns** | 1 | Calipers |
| 19 | **DiscardAtEndOfTurn** | 1 | RunicPyramid |
| 20 | **EnemyDeath** ⚠️ | 1 | **GremlinHorn** — no site in the reference |
| 21 | **BlockBroken** ⚠️ | 1 | **HandDrill** — no site in the reference |

Note **CombatStart is 46 of 140** — a third of the vocabulary on one trigger.
That is the single largest implementation batch and also the one whose internal
ordering is most likely to matter (§4.2).

### 3.2 Event triggers — run layer

No combat hook at all; these fire between fights.

| trigger | n | relics |
|---|---|---|
| **Pickup** (`obtainRelic`) | 21 | Astrolabe, BottledFlame, BottledLightning, BottledTornado, BurningBlood, CallingBell, Cauldron, EmptyCage, LeesWaffle, Mango, MawBank, NeowsLament, Omamori, PandorasBox, Pear, PotionBelt, PrismaticShard, Strawberry, TinyHouse, WarPaint, Whetstone |
| **ObtainCard** | 5 | CeramicFish, FrozenEgg, MoltenEgg, Omamori, ToxicEgg |
| **EnterRoom** | 3 | EternalFeather, MawBank, SsserpentHead |
| **CardReward generation** | 3 | BustedCrown, PrismaticShard, QuestionCard |
| **CombatReward generation** | 3 | BlackStar, GoldenIdol, PrayerWheel |
| **PotionReward generation** | 1 | WhiteBeastStatue *(already implemented)* |
| **Chest** | 3 | CursedKey, Matryoshka, NlothsHungryFace |
| **Campfire options** | 3 | DreamCatcher, Girya, RegalPillow |
| **Shop pricing** | 3 | MembershipCard, SmilingMask, TheCourier |
| **Gold change** | 2 | Ectoplasm, MawBank |
| **Event options** | 4 | BloodVial, GoldenIdol, OddMushroom, WarpedTongs |
| **Face Trader** | 5 | CultistHeadpiece, FaceOfCleric, GremlinVisage, NlothsHungryFace, SsserpentHead |
| **Map / room resolution** | 2 | JuzuBracelet, TinyChest — ⚠️ NOT implemented. The `?`-room drift machinery exists in `resolve_unknown_room`; the relics that modify it do not. An earlier revision of this table claimed otherwise. |
| **AfterBattle** | 1 | FaceOfCleric |
| **Neow** | 1 | NeowsLament |

### 3.3 Query modifiers — not triggers at all

These have no event. They change a value that is *computed*, and the natural home
is `query.cc` alongside `DamageRule` — this is exactly the pattern `CardData`
already uses, where "the card only says which rule applies."

| relic | modifies |
|---|---|
| Ginger | cannot gain Weak |
| Turnip | cannot gain Frail |
| The Boot ⚠️ | outgoing unblocked attack damage floor |
| PaperPhrog, StrikeDummy | damage calculation |
| ChampionBelt | Vulnerable also applies Weak |
| BlueCandle, MedicalKit | card playability / cost |
| Calipers | block retention |
| RunicPyramid | end-of-turn discard |
| VelvetChoker | cards playable per turn |
| FrozenEye ⚠️ | draw pile visibility — **observation only** |
| SlaversCollar, BustedCrown, CoffeeDripper, FusionHammer, Ectoplasm, PhilosophersStone, MarkOfPain, RunicDome, Sozu, CursedKey | energy / passive combat parameters |

Several appear in §3.1 too: a relic can both fire at combat start (to set up) and
carry a standing modifier. Ink Bottle appears in six trigger rows because it
counts every card played.

---

## 4. Two decisions needed

### 4.1 How is per-relic behaviour expressed?

**Option A — declarative table, like `CardData`.** A `RelicData` row per relic
with trigger + parameters. Fits the ~half that are "at trigger T, apply N of X"
(Vajra: +1 Strength at CombatStart; Anchor: +10 Block). Needs an escape hatch
for the rest, exactly as `DamageRule` is one for cards.

**Option B — a `switch (id)` at each trigger site.** Ordering within a trigger is
explicit and reviewable in one place, which is the parity-critical property. 140
relics spread across ~21 switches, though most relics have one trigger.

**Recommendation: A with B as the escape hatch** — the house idiom. Declarative
rows for the regular shapes, a named-rule enum for the irregular ones, dispatched
at trigger sites. It keeps `CombatState::clone()` a plain copy, since the table
is static and only `HeldRelic{id, counter}` is per-state.

### 4.2 Does relic *order* need to be observable?

In StS, when several relics fire on one trigger, they fire in **acquisition
order**. With 46 relics on CombatStart, that ordering is load-bearing rather than
theoretical.

But §5.2 block 8 specifies relics-held as a **multi-hot**, which discards order.
Under §1 — *the observation should match what a human player can see* — a human
sees their relic bar in acquisition order, so this may be a parity gap in the
observation, in the same category as any engine parity bug.

Options: keep the multi-hot and accept it; or carry an order channel (140 floats
of normalised position, or a fixed-length ordered id list).

**This is cheaper to settle now than after 140 effects are wired to an ordering
the agent cannot see.** It is also independent of 4.1.

---

## 5. Proposed batching

Ordered so that infrastructure lands before volume, and the largest homogeneous
group is not first.

| batch | contents | n |
|---|---|---|
| 0 | Trigger infrastructure: sites fire, nothing wired. Includes the three trigger points the reference lacks (EnemyDeath, BlockBroken, outgoing damage modifier). | — |
| 1 | Query modifiers (§3.3) — no event plumbing, validates the declarative path | ~20 |
| 2 | Run-layer triggers (§3.2) — no combat coupling | ~40 |
| 3 | CombatStart (§3.1 row 1) — the big one, after the pattern is proven | 46 |
| 4 | Turn boundaries + CombatEnd | ~20 |
| 5 | Card-play and damage triggers | ~25 |
| 6 | Potions | 33 |

Each batch cross-checks its own effect numbers against the wiki before coding,
per §1.

---

## 7. Status: 70 of 140 wired

Counted from the source, not estimated — a relic counts as wired when its
`RelicId` is referenced from code (not from the pool tables or a comment).
Running the count is what caught the Juzu Bracelet / Tiny Chest error in §3.2.

The 77 remaining are not one backlog. **Most are blocked on engine pieces that
have nothing to do with relics**, and those blockers are shared:

| blocker | relics waiting | note |
|---|---|---|
| ~~**Missing `Power`s**~~ | ~~Bronze Scales, Akabeko, Incense Burner, Fossilized Helix, Thread and Needle, Pen Nib~~ | ✅ **done.** All six Powers exist: Vigor, PenNibCharge, Intangible, Buffer, Thorns, PlatedArmor. Intangible is the one power that ticks — a named exception to the status model (Rob, 2026-09-12). |
| **No curse cards** | Omamori, Darkstone Periapt, Blue Candle, and the unreachable halves of Du-Vu Doll and Cursed Key | §6.4 |
| ~~**No colorless cards**~~ | ~~Toolbox, half of Prismatic Shard~~ | ✅ **cleared.** All 35 colorless cards are implemented (`colorless-effects.md`) and the shop's 2 colorless slots now stock. **Toolbox** is wired: a 1-of-3 colorless choice that pauses combat setup BEFORE the opening hand (§6.9 — Rob ruled, 2026-09-20). **Prismatic Shard**'s other half still needs §6.8. ⚠️ CORRECTED earlier: Orrery, Dolly's Mirror and Cauldron never needed colorless — Cauldron is potions only, and the other two need a shop CHOICE SCREEN over normal card rewards. |
| **Needs a shop choice screen** | Orrery, Dolly's Mirror | moved here from the colorless row |
| **Needs potion effects** | Cauldron | moved here from the colorless row |
| **No events** | Neow's Lament, Odd Mushroom, Warped Tongs, Spirit Poop, and the 5 Face Trader masks | 10 of the special-tier relics |
| **Hooks not yet wired** | Gremlin Horn (EnemyDeath), Hand Drill (BlockBroken), Sundial + The Abacus (Shuffle), Toy Ornithopter + Sacred Bark (PotionDrunk) | the hooks exist in the enum; nothing fires them |
| **No boss encounter** | Pantograph | heals only at the start of a boss fight |
| **Needs a choice screen** | Gambling Chip, Empty Cage, Astrolabe, Pandora's Box, Calling Bell | all pause for player input |
| **Nothing blocking — just unwritten** | ~25, including Maw Bank, Meal Ticket, Ceramic Fish, Old Coin, Horn Cleat, Captain's Wheel, Mercury Hourglass, Tungsten Rod, Torii, Magic Flower, Ice Cream, Unceasing Top, Champion Belt, Charon's Ashes, Self-Forming Clay, Centennial Puzzle, Eternal Feather, Singing Bowl, Matryoshka, Wing Boots, Juzu Bracelet, Tiny Chest | the honest remainder |

**What this says about sequencing.** Continuing to batch relics hits diminishing
returns: the next batch would be ~25 relics of genuine work followed by a wall of
blockers. The six missing Powers and the curse/colorless card gaps are each
worth more than the relics behind them — Intangible, Thorns and Plated Armor are
core mechanics that cards want too, and colorless cards also unblock two shop
slots that have been empty since §4.3.

---

## 6. Known gaps

Recorded as they are found, so a later batch does not rediscover them as bugs.

### 6.1 A combat-start death fires no on-death hooks

`start_combat` drains twice around the opening draw and shares one
`ResolutionContext`, but never calls `process_deaths`. An enemy killed during a
combat-start drain would therefore record a death that nothing processes —
`Hook::EnemyDeath` and `BecameLastEnemy` would not fire.

**Unreachable today**: no wired combat-start relic deals damage. It becomes
reachable in batch 3 with **Mercury Hourglass** (3 damage to all enemies) and
**Preserved Insect** (elite enemies start at 75% HP).

Needs a ruling then, not now: does a combat-start kill fire on-death hooks?
`process_deaths`'s own comment states the invariant — *every drain that can
damage an enemy must reach one of the two death paths* — so the answer is
probably yes, and the fix is to call it after the second drain. What needs
deciding is whether an enemy that dies before the player has acted should
trigger Spore Cloud and friends at all.

### 6.2 The boss energy relics have their upside but not their drawback

Batch 1c wired `+1 energy` for eleven relics. Each also has a drawback, and
those live in later batches — so **each of these is currently strictly stronger
than the real relic.**

| relic | drawback | lands in |
|---|---|---|
| Sozu | no potions | ✅ done |
| Coffee Dripper | cannot Rest | ✅ done (batch 2b) |
| Fusion Hammer | cannot Smith | ✅ done (batch 2b) |
| Ectoplasm | cannot gain gold | ✅ done (batch 2d) |
| Busted Crown | fewer card reward options | ✅ done (batch 2d) |
| Cursed Key | chests give a Curse | **blocked** — needs curses in the deck |
| Philosopher's Stone | all enemies +1 Strength | batch 3 (combat start) |
| Mark of Pain | 2 Wounds into the draw pile | batch 3 — needs card generation |
| Velvet Choker | max 6 cards per turn | batch 5 — needs a per-turn play counter and a mask rule |
| Runic Dome | cannot see enemy intent | **needs the v2 observation** |
| Slaver's Collar | none (conditional upside) | ✅ complete |

**Why this is safe right now, and exactly when it stops being safe.** Every one
of these is Boss tier, and no run can obtain a Boss-tier relic today: the three
`random_relic` call sites pass chest tiers, `relic_tier_standard`
(common/uncommon/rare) and `RelicTier::Shop` — never `Boss`. Neow is the only
boss-relic source in an Act 1 run (§4.4) and is not implemented. Verified by
reading all three sites, not by grep.

**So the moment Neow lands, this becomes a live parity bug** — an agent would
train against eleven relics that are pure upside. Neow must not ship before the
drawbacks, or must exclude these from its boss-relic pool until they do.

Runic Dome is the sharp one: its drawback is an *observation* change (hiding
intent), so it cannot be fixed in the engine at all and is blocked on the v2
observation work. It is the one relic here whose parity depends on §5.

### 6.3 Taxonomy corrections found while implementing

Two relics were classified from the reference's `initRelics` and are in the
wrong row of §3.1. The reference is not wrong — `initRelics` runs at combat
start, which is also turn 1's start, so a per-turn effect looks identical there.
Reading the wiki's effect text is what separated them.

| relic | §3.1 said | actually | why it matters |
|---|---|---|---|
| **Brimstone** | CombatStart | **TurnStart** — "at the start of your turn, gain 2 Strength and ALL enemies gain 1" | fires every turn, not once. A combat-start reading caps it at +2/+1 for the whole fight. ✅ fixed in batch 3b |
| **Red Skull** | CombatStart | **dynamic on HP change** — "while your HP is at or below 50%, you have 3 additional Strength", gained *and removed* as HP crosses the threshold, even on the enemy's turn | not a grant at all; it needs an HP-change hook and a removal path. |

Both are deferred to batch 4/5 rather than implemented wrongly now.

### 6.4 Du-Vu Doll is correct but unreachable: no curse card exists

`CardType::Curse` is in the enum, but **no curse card is implemented**, so a
deck's curse count is always zero and Du-Vu Doll always grants 0 Strength. The
counting logic is right and cannot be tested.

Same state as Cursed Key (§6.2), and the same blocker: curses need to exist as
cards before either relic does anything. Ascender's Bane is out of scope
(Ascension is pinned at 0, §15), so the first curses will arrive with events.

### 6.7 Relics fire SEQUENTIALLY, not batched — found by a failing test

The action queue batches: hooks push, and the queue drains once at the end. For
relics that is **wrong wherever one relic reads what another just changed**,
because StS applies relics one at a time — a relic's effect has landed before
the next one looks at anything.

**Burning Blood + Meat on the Bone is the case that proves it.** Burning Blood
heals 6; Meat on the Bone then asks whether HP is at or below half. At 35 of 80:

| | Meat on the Bone sees | fires? | total heal |
|---|---|---|---|
| batched (both read pre-heal HP) | 35 — below half | yes | **18** |
| sequential (reads 41, post-heal) | 41 — above half | no | **6** |

`fire_relic_hooks_sequentially` drains after each relic and is what `end_combat`
uses. The batched `fire_relic_hooks` remains for hooks whose relics commute.

**The general rule**: use the sequential form wherever one relic on a hook can
read state another changed. Combat start is currently safe — its relics grant
Strength, Block, energy and debuffs without reading each other — but that is a
property of which relics are wired, not a guarantee. Anything added there that
*reads* HP, block or a power must move that hook to the sequential form.

Found because a test asserted the interaction rather than each relic alone. The
naive version passes every single-relic test.

### 6.8 Prismatic Shard reaches OUTSIDE the card vocabulary — needs a ruling

*"Combat reward screens now contain Colorless cards and cards from other
colors."*

The colorless half is implementable now that the 35 colorless cards exist. The
**other-colors half is not**: it means Silent, Defect and Watcher cards, which
are deliberately absent from the 270-card vocabulary (189 Ironclad + 70
colorless + 11 curses) and outside Act 1 scope.

This is a category the blocker table did not previously have. Prismatic Shard is
**not a dead index** — it is a Shop-tier relic, genuinely obtainable, so §5.1
rule 5 is satisfied. The problem is that half its *effect* reaches content the
vocabulary excludes by design.

Two options, and neither is obviously right:

| | cost |
|---|---|
| implement the colorless half only | a strictly weaker relic — the same failure mode as shipping a boss relic's upside without its drawback (§6.2), and as shipping an obtainable-but-unplayable card |
| widen the vocabulary to other classes | contradicts the Act 1 scope rule, and adds ~600 card ids for one relic |

**Unresolved.** Recorded rather than decided, because it is a scope question
rather than an implementation one.

### 6.5 The remaining counter relics need powers that do not exist

Happy Flower is done (batch 3b) and Girya (2b) uses its counter too, so §3.3's
run-scoped counters are now exercised end to end. Four counter relics remain,
and each is blocked on a missing `Power`, not on the counter machinery:

| relic | needs |
|---|---|
| **Incense Burner** | `Power::Intangible` — every 6 turns |
| **Pen Nib** | a "next Attack deals double" power; the reference carries `PS::PEN_NIB` |
| **Nunchaku** | nothing missing — needs the card-play hook (batch 5) |
| **Ink Bottle** | nothing missing — needs the card-play hook (batch 5) |

Intangible in particular is a real engine addition (damage reduced to 1 from all
sources), not a relic detail.

### 6.6 A turn-start relic must fire from TWO call sites

Turn 1's start *is* combat start, so anything firing every turn has to be
handled at both `Hook::CombatStart` and `Hook::TurnStartPlayer` — and exactly
once between them. `fire_turn_start_relic` exists so the two cannot drift:
both call sites route through it rather than duplicating the arms.

This is the same confusion that misclassified Brimstone and Red Skull (§6.3).
The reference has a single `initRelics` call site where a per-turn effect and a
once-per-fight effect are indistinguishable, and reproducing that shape would
have reproduced the ambiguity.

### 6.9 A fight can now BEGIN in a choice — Toolbox pauses combat setup

Toolbox ("at the start of each combat, choose 1 of 3 random Colorless cards and
add it to your hand") is the first relic whose effect *interrupts* combat setup
rather than adding to it, and the interruption is the mechanic.

**The ruling (Rob, 2026-09-20).** The choice resolves PRE-DRAW — before the
opening hand exists. I initially proposed resolving it after the draw and
claimed the two were observationally identical. **That was wrong.** In StS the
prompt appears before the hand is dealt, so a human chooses blind; an agent
choosing after the draw could pick the card that fits the hand it was given.
That is strictly more information than a human has, which `observation-space.md`
§1 makes a parity defect rather than a convenience.

**What it cost.** `start_combat` used to be imperative — fire pre-draw hooks,
drain, deal the hand, fire the rest, drain — and an imperative call has nowhere
to be interrupted. The opening draw and the two post-draw hooks are now
`ActionKind::DrawOpeningHand` and `ActionKind::CombatStartPostDraw`, queued
behind the pre-draw hooks and drained once. A relic that pauses at
`RequestChoice` therefore parks the draw in `suspended_queue` with everything
after it, and answering the choice resumes the sequence in order.

**The new state shape.** A `CombatState` can be returned from `start_combat`
already paused, with an empty hand and a live `pending_choice`. Nothing else
produces this, and anything that assumes "a fresh fight has a hand" is now
wrong — the env's very first observation may be a choice screen. Four tests in
`test_relic_triggers.cc` pin it, including that the answer is reachable through
the action space on step 0 and that the parked `CombatStart` hooks (Vajra's
Strength) still land afterwards.

**No discount.** Unlike Discovery, whose card text grants the free copy, Toolbox
hands the card over at full price. `ChoiceKind::DiscoverColorlessCard` shares
Discovery's rolled-options path — three distinct cards drawn without
replacement — and differs only in pool and in not setting a cost override.

**`CardId::None`, because a relic's menu has no source card.**
`PendingChoice::source_card` means "the card that caused the pause" and
defaulted to `CardId::Strike`, so Toolbox's prompt reported Strike as its
source — in the observation and in the TUI header both. Rob ruled for a
sentinel (2026-09-20) over a parallel `source_relic` field or leaving it.

The sentinel is free because `kNumCardTypes` is a literal `270` rather than a
count of the enum: `None` is **appended** and has **no `CARD_DATABASE` row**, so
it sits one past the last real id, the `CARD_DATABASE.size() == kNumCardTypes`
assert still holds, and `encode_action` cannot reach it — the action space stays
at 2,135. In the obs it encodes as its ordinal (270), uniform with the other
enum ordinals in the choice header rather than a special-cased `-1`.

Two consequences worth knowing: `card_name` had to become total (it is
`CARD_DATABASE.at()`, which throws, and the TUI calls it on `source_card`), and
Python cannot write `CardId.None` — readers use `getattr(CardId, "None")`, as
they already do for `ChoiceKind.None`. Shop and event screens inherit all of
this when they arrive.

**The same default was wrong in four places**, and fixing one would have left
the others to reintroduce it: `Action::card`, `CardData::generated_card`,
`fire_player_power_hooks`'s `card` parameter and `ChoiceView::source_card` all
defaulted to Strike. All four now default to `None`, which changes a silent
wrong answer into a throw — so every read site was read before the flip, not
inferred from a green suite:

| read | why the sentinel is safe |
|---|---|
| `Hook::CardPlayed` → `.at(card).type` | fired only from `CardPlayedHook`, which always sets the id |
| `Hook::CardDrawn` → `.at(card).type` | fired with the drawn card |
| `Hook::PlayerAttacked` | never reads `card` — keys on `attacker_slot`. This is the cardless path: an ENEMY attack |
| `PlayCard` (Havoc, Mayhem) | uses the drawn card's own id; `a.card` is unread on that branch |
| `AddCardToPile` | guarded by `generated_count > 0` |
| `ExhaustCard`, `DiscardCard`, `PlaceOnBottomOfDraw`, `ArmBomb`, Double Tap's replay | every push site sets the card, via `carry()` or directly |

The remaining hazard is a NEW read added to a cardless path, which now throws
instead of silently using Strike's row. `TurnLoop.AnEnemyAttackCarriesNoCard`
pins it.
