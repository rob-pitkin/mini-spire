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
- **§4.2 — relic ORDER in the observation: deferred, not blocking.** The engine
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
| 1 | **CombatStart** | 46 | Akabeko, Anchor, AncientTeaSet, BagOfMarbles, BagOfPreparation, BloodVial, Brimstone, BronzeScales, BustedCrown, ClockworkSouvenir, CoffeeDripper, CursedKey, DuVuDoll, Ectoplasm, FossilizedHelix, FusionHammer, GamblingChip, Girya, GremlinVisage, HappyFlower, IncenseBurner, InkBottle, Lantern, LizardTail, MarkOfPain, MercuryHourglass, NeowsLament, Nunchaku, OddlySmoothStone, Omamori, Pantograph, PenNib, PhilosophersStone, PreservedInsect, RedSkull, RunicDome, SlaversCollar, SlingOfCourage, SneckoEye, Sozu, Sundial, ThreadAndNeedle, Toolbox, Vajra, VelvetChoker, WarpedTongs |
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
| **Map / room resolution** | 2 | JuzuBracelet, TinyChest *(already implemented)* |
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
| Ectoplasm | cannot gain gold | batch 2c |
| Busted Crown | fewer card reward options | batch 2c |
| Cursed Key | chests give a Curse | batch 2d — needs curses in the deck |
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

### 6.3 Relic counters are loaded but never read at combat start

Five relics branch on their counter *during* setup in the reference — Happy
Flower, Ink Bottle, Pen Nib, Nunchaku, Incense Burner — e.g. Pen Nib at 9
converts into a buff and resets to −1. `HeldRelic::counter` already carries
across fights (§3.3), but no wired relic reads it yet. Batch 4 (turn boundaries)
is where this starts to matter.
