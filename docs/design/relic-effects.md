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
  shows, so the order of the `relics` vector matters either way and no batch
  below depends on the answer. The v2 observation is not built, so adding a channel
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
*can* be structured, never evidence that a mechanic does not exist. Absence
there means only that that codebase does not implement it.

---

## 3. The taxonomy

Three kinds of hook, taken from how the relics group rather than from a
structure chosen in advance.

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

**Recommendation: A with B as the escape hatch** — the idiom already used for
cards. Declarative
rows for the regular shapes, a named-rule enum for the irregular ones, dispatched
at trigger sites. It keeps `CombatState::clone()` a plain copy, since the table
is static and only `HeldRelic{id, counter}` is per-state.

### 4.2 Does relic *order* need to be observable?

In StS, when several relics fire on one trigger, they fire in **acquisition
order**. With 46 relics on CombatStart, that ordering changes results rather
than being a formality.

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

Ordered so the trigger infrastructure is built before the bulk of the relics,
and the largest single group is not first.

| batch | contents | n |
|---|---|---|
| 0 | Trigger infrastructure: sites fire, nothing wired. Includes the three trigger points the reference lacks (EnemyDeath, BlockBroken, outgoing damage modifier). | — |
| 1 | Query modifiers (§3.3) — no event plumbing, validates the declarative path | ~20 |
| 2 | Run-layer triggers (§3.2) — no combat coupling | ~40 |
| 3 | CombatStart (§3.1 row 1) — the largest group, after the pattern is proven | 46 |
| 4 | Turn boundaries + CombatEnd | ~20 |
| 5 | Card-play and damage triggers | ~25 |
| 6 | Potions | 33 |

Each batch cross-checks its own effect numbers against the wiki before coding,
per §1.

---

## 7. Status: 88 of 140 wired

Counted from the source, not estimated — a relic counts as wired when its
`RelicId` is referenced from code (not from the pool tables or a comment).
Running the count is what caught the Juzu Bracelet / Tiny Chest error in §3.2.

The 52 remaining are not one backlog. **Most are blocked on engine pieces that
have nothing to do with relics**, and those blockers are shared:

| blocker | relics waiting | note |
|---|---|---|
| ~~**Missing `Power`s**~~ | ~~Bronze Scales, Akabeko, Incense Burner, Fossilized Helix, Thread and Needle, Pen Nib~~ | ✅ **done.** All six Powers exist: Vigor, PenNibCharge, Intangible, Buffer, Thorns, PlatedArmor. Intangible is the one power that ticks — a named exception to the status model (Rob, 2026-09-12). |
| **No curse cards** | Omamori, Darkstone Periapt, Blue Candle, and the unreachable halves of Du-Vu Doll and Cursed Key | §6.4 |
| ~~**No colorless cards**~~ | ~~Toolbox, half of Prismatic Shard~~ | ✅ **cleared.** All 35 colorless cards are implemented (`colorless-effects.md`) and the shop's 2 colorless slots now stock. **Toolbox** is wired: a 1-of-3 colorless choice that pauses combat setup BEFORE the opening hand (§6.9 — Rob ruled, 2026-09-20). **Prismatic Shard**'s other half still needs §6.8. ⚠️ CORRECTED earlier: Orrery, Dolly's Mirror and Cauldron never needed colorless — Cauldron is potions only, and the other two need a shop CHOICE SCREEN over normal card rewards. |
| **Needs a shop choice screen** | Orrery, Dolly's Mirror | moved here from the colorless row |
| **Needs potion effects** | Cauldron, Toy Ornithopter, Sacred Bark | Cauldron moved here from the colorless row; the other two need `Hook::PotionDrunk`, which cannot fire until a potion can be drunk |
| **No events** | Neow's Lament, Odd Mushroom, Warped Tongs, Spirit Poop, and the 5 Face Trader masks | 10 of the special-tier relics |
| ~~**Hooks not yet wired**~~ | ~~Gremlin Horn (EnemyDeath), Hand Drill (BlockBroken), Sundial + The Abacus (Shuffle)~~ | ✅ **done** (§6.10). All three hooks now fire and all four relics are wired. **Toy Ornithopter + Sacred Bark** still wait on PotionDrunk, which needs potions to be drinkable — moved to the potions row. |
| **No boss encounter** | Pantograph | heals only at the start of a boss fight |
| **Needs a choice screen** | Gambling Chip, Empty Cage, Astrolabe, Pandora's Box, Calling Bell | all pause for player input |
| **Nothing blocking — just unwritten** | **~27**: Maw Bank, Meal Ticket, Ceramic Fish, Old Coin, Magic Flower, Unceasing Top, Champion Belt, Charon's Ashes, Eternal Feather, Singing Bowl, Matryoshka, Wing Boots, Juzu Bracelet, Tiny Chest, Dead Branch, Snecko Eye, Ancient Tea Set, Pocketwatch, Chemical X, Tiny House, Frozen Egg, Molten Egg, Toxic Egg, and the three Bottled relics | ⚠️ **the "~25" here was an undercount** (corrected 2026-09-20): classifying all 66 then-remaining against the blocker rows left ~42 with nothing blocking them. 6 shipped in §6.11 and 5 in §6.12. The Bottled relics and the Eggs may each need a card-selection screen at pickup — check before batching them |
| **Needs an HP-threshold detector** | Red Skull | §6.12 — it keys on crossing 50% Max HP in BOTH directions, so it fires on healing too and cannot ride the HP-loss hook |

**What this says about sequencing.** Updated 2026-09-20. The earlier advice here
was to build the missing Powers and the curse and colorless card gaps before
more relics, because each unblocked more than the relics behind it. The Powers
are done, all 35 colorless cards are done, and the three dead hooks are wired,
which is what moved the count from 69 to 74.

What is left splits cleanly: about 25 relics with nothing blocking them, and the
rest waiting on potions, events, curse acquisition, a boss encounter, or a
choice screen. Those four are separate pieces of work with their own tasks, so
the next relic batch is the 25, not another dependency hunt.

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
trigger Spore Cloud and the other on-death effects at all.

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

**So when Neow is implemented, this becomes a live parity bug** — an agent would
train against eleven relics that are pure upside. Neow must not ship before the
drawbacks, or must exclude these from its boss-relic pool until they do.

Runic Dome is the hardest case: its drawback is an *observation* change (hiding
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

### 6.4 Du-Vu Doll is correct but unreachable: no curse can enter a deck

**Corrected 2026-09-20.** This section used to say "no curse card is
implemented". That is no longer true and was misleading even then: all 11 curse
rows exist in `CARD_DATABASE` (the vocabulary is 189 + 70 + **11**). The real
blocker is ACQUISITION — nothing puts a curse into a deck, so a deck's curse
count is always zero and Du-Vu Doll always grants 0 Strength. Cursed Key
(§6.2) is in the same state, and both stay blocked until events, Neow and
Cursed Key's own chest can add one.

Of the 11, **4 already work** by riding machinery that exists for other cards:
Clumsy (`ethereal`), Decay (`end_of_turn_damage_in_hand`, Burn's field), Writhe
(`innate`), and Injury, whose whole rule is being an unplayable clog.

**7 are data-only.** Their `CardData` fields are set and asserted by
`Curses.TheUnwiredCursesCarryTheirData`, and no engine code reads them —
Regret, Doubt, Shame, Pain and Normality (combat), Parasite and Curse of the
Bell (run layer). The test is honest about this; it says so in its name.

Two of those seven were worse than unwired — they were **wrong**, because the
deck already had removal paths that ignored them. `buy_card_removal` and
`rest_toke` each erased from `master_deck` directly, so Curse of the Bell could
be removed at a shop and Parasite cost nothing. Both now route through
`RunState::remove_card_from_deck`, which is the only place a card leaves the
master deck. Note `lose_max_hp` is deliberately NOT `gain_max_hp` with a
negative: gaining Max HP heals by the same amount, losing it only clamps.

⚠️ Still owed: when the run-layer action space lands, Curse of the Bell must be
excluded from the removal MASK as well. Refusing it in the method keeps the
state correct, but an agent can still spend a step asking.

### 6.7 Relics fire SEQUENTIALLY, not batched — found by a failing test

The action queue batches: hooks push, and the queue drains once at the end. For
relics that is **wrong wherever one relic reads what another just changed**,
because StS applies relics one at a time — a relic's effect is applied before
the next one reads any state.

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

**✅ All four are now wired** (corrected 2026-09-20 — the table below described
the state before `Power::Intangible`, `Power::PenNibCharge` and the card-play
hook existed, and was read as current while planning a later batch):

| relic | was blocked on | now |
|---|---|---|
| **Incense Burner** | `Power::Intangible` — every 6 turns | ✅ the Power exists; wired in `fire_turn_start_relic` |
| **Pen Nib** | a "next Attack deals double" power | ✅ `Power::PenNibCharge`, granted on the ninth Attack so the tenth is the doubled one |
| **Nunchaku** | the card-play hook | ✅ `fire_card_played_relic`, run-scoped counter |
| **Ink Bottle** | the card-play hook | ✅ same, counting every card rather than a type |

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

**What changed.** `start_combat` used to be imperative — fire pre-draw hooks,
drain, deal the hand, fire the rest, drain — and an imperative call has nowhere
to be interrupted. The opening draw and the two post-draw hooks are now
`ActionKind::DrawOpeningHand` and `ActionKind::CombatStartPostDraw`, queued
behind the pre-draw hooks and drained once. A relic that pauses at
`RequestChoice` therefore parks the draw in `suspended_queue` with everything
after it, and answering the choice resumes the sequence in order.

**A state nothing produced before.** A `CombatState` can be returned from `start_combat`
already paused, with an empty hand and a live `pending_choice`. Nothing else
produces this, and anything that assumes "a fresh fight has a hand" is now
wrong — the env's very first observation may be a choice screen. Four tests in
`test_relic_triggers.cc` pin it, including that the answer is reachable through
the action space on step 0 and that the parked `CombatStart` hooks (Vajra's
Strength) still run afterwards.

**No discount.** Unlike Discovery, whose card text grants the free copy, Toolbox
hands the card over at full price. `ChoiceKind::DiscoverColorlessCard` shares
Discovery's rolled-options path — three distinct cards drawn without
replacement — and differs only in pool and in not setting a cost override.

**`CardId::None`, because a relic's menu has no source card.**
`PendingChoice::source_card` means "the card that caused the pause" and
defaulted to `CardId::Strike`, so Toolbox's prompt reported Strike as its
source — in the observation and in the TUI header both. Rob ruled for a
sentinel (2026-09-20) over a parallel `source_relic` field or leaving it.

The sentinel costs nothing because `kNumCardTypes` is a literal `270` rather than a
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

### 6.10 Three hooks existed but nothing fired them

`EnemyDeath`, `ShuffleDrawPile` and `BlockBroken` sat in the `Hook` enum with no
call site, so Gremlin Horn, Sundial, The Abacus and Hand Drill were unreachable
however correctly they were written. Each needed a parity question answered
first, and in each case the answer was narrower than the card text.

**Only some shuffles fire the hook.** StS fires `onShuffle` from
`EmptyDeckShuffleAction`'s constructor and from `ShuffleAction` only when it is
constructed with `triggerRelics`. The COMBAT-START shuffle goes through neither:
`CardGroup.initializeDeck` calls `shuffle()` directly, so **Sundial and The
Abacus do not trigger on turn 1**. Our three qualifying sites — the draw-dry
reshuffle, Deep Breath, and Havoc/Mayhem's reshuffle — now share
`reshuffle_discard_into_draw`, which fires the hook; `start_combat`'s shuffle
deliberately does not call it. `RelicTriggers.TheOpeningShuffleDoesNotCountAsAShuffle`
tests this. Without it, every fight with The Abacus would start with 6 block.

This is why `draw_one` now takes an `ActionQueue&`: drawing can reshuffle, and a
reshuffle is an event relics answer, so the function performing it has to be
able to emit.

**Gremlin Horn does not pay for the last kill.** The decompiled relic guards on
`!areMonstersBasicallyDead()` — true when every monster is dying or escaping —
so the killing blow that ends the fight grants no energy and no card. The wiki
text omits this. `CheckDeath` applies the guard once for the whole resolution
rather than per relic, since the condition is about the fight, not the relic.

**Hand Drill is enemy-only, and equality breaks block.** `onBlockBroken` takes
an `AbstractCreature`, but `brokeBlock()` only loops the relics when
`this instanceof AbstractMonster` — so a player whose own block breaks triggers
nothing, and a direct port of the signature would have applied the Vulnerable to
the player. `decrementBlock` also shows two details worth having: damage EQUAL
to block breaks it (same branch as `>`), and `DamageType.HP_LOSS` never breaks
block at all. Thorns-type damage is not HP loss, so our fixed-damage path fires
the hook alongside the attack path — wiring only the attack path would have left
Combust and Fire Breathing unable to trigger the relic.

### 6.11 The turn-boundary batch, and the energy bug it exposed

Six relics on §5's batch 4: Art of War, Captain's Wheel, Horn Cleat, Mercury
Hourglass, Orange Pellets and Ice Cream. 74 → 80. Each needed a small piece of
shared machinery rather than a bespoke arm.

**End-of-turn energy was being discarded, and that destroyed Ice Cream.**
`handle_end_turn` set `character.energy = 0` before the enemy phase. Without Ice
Cream this is invisible — the turn-start refill replaces the total either way —
so no test could have caught it. With Ice Cream the leftover is exactly what
carries forward, and the relic silently did nothing. The discard is removed:
`query.cc`'s `energy_after_turn_start` is now the single authority on what
energy a turn begins with, and it replaces the total unless Ice Cream is held,
in which case it adds. Nothing reads energy between the two points.

Ice Cream lives in the query layer and not in a relic arm because that is where
StS puts it: `IceCream.java` is an empty class with no hooks, and
`EnergyManager::recharge()` branches on holding it.

**Orange Pellets clears more than the Debuff enum.** StS's `RemoveDebuffsAction`
removes every power whose `type` is DEBUFF, and `StrengthPower` reports DEBUFF
whenever its amount is negative. So a Strength reduction from Disarm or
Shockwave is cleared along with Weak and Vulnerable. Our Strength is a `Power`,
so clearing the debuff map alone would have left it — the executor now also
erases any power holding a negative value. Two tests pin both halves: the
reduction goes, and Inflame's +2 stays.

**Horn Cleat and Captain's Wheel key on `turn_number`, not a counter.** StS
resets their counters in `atBattleStart`, but our `fire_turn_start_relic` runs
BEFORE the CombatStart arm (§6.6's two call sites), so a reset there would erase
turn 1's increment. `turn_number` is per-combat by construction and cannot
double-fire; the relic counter still carries the countdown the icon displays.

**Art of War reads the flag the previous turn left behind.** Three per-turn
`played_*_this_turn` flags on `Character` are set by the `CardPlayedHook`
executor — so a card played by Havoc, Mayhem or Double Tap counts exactly as a
hand-played one does — and cleared at turn start immediately AFTER the hooks
have read them. Art of War never fires on turn 1, matching StS's `firstTurn`
guard: there is no previous turn to have played an Attack in.

**Lizard Tail was split out** (§3.1 row 15 pairs it with Sacred Bark under
PlayerDeath). `Hook::PlayerDeath` does not exist in the enum at all, and adding
it means intercepting `check_character_terminal` so a listener can cancel the
death — a change to how a fight ends, not a turn-boundary addition.

### 6.12 The incoming-damage batch, and why there are now two HP-loss hooks

Five relics: Tungsten Rod, Torii, Centennial Puzzle, Runic Cube and
Self-Forming Clay. 80 → 85.

**One choke point for reductions.** `query.cc`'s `reduce_player_hp_loss` is
called by every path that removes player HP — attack damage after block, fixed
damage after block, and the block-bypassing `LoseHp` — so a new damage source
cannot skip a reduction. Two relics live there and the ORDER between them is
specified, not incidental: the wiki's Tungsten Rod page states "Torii activates
before Tungsten Rod", and it changes the result, since a 5-damage hit becomes 1
and then 0 where the other order would leave 4.

Torii is narrower than it first looks. StS's `onAttacked` requires an attacker
and excludes `DamageType` HP_LOSS and THORNS, so Burn's tick and a thorns
retaliation are untouched — which is why `damage_player` now takes a
`from_attack` flag. It also reads the UNBLOCKED number (StS runs those relics
after `decrementBlock`), so 9 damage into 5 block is reduced, and it applies per
HIT: the wiki's example is a 4x3 attack landing as 1x3.

**Two HP-loss hooks, deliberately.** `Hook::HpLostPlayer` is Rupture's, and
Rupture fires only on loss from a card or power — never on enemy damage
(ordering-notes §9), and not on Burn's tick either. The three drawing relics key
on "whenever you lose HP", from any source. Rather than widen the existing hook
and add a hidden filter, `Hook::PlayerHpLostAny` is fired from all three sites
and consumed only by relics. Two hooks with one meaning each; no existing
behaviour moved.

**`Power::NextTurnBlock`** is new, for Self-Forming Clay: block granted at the
next turn start, then the power removes itself, exactly as StS's
`NextTurnBlockPower` does. It stacks within a turn. Adding a power widens the
player observation block by one float (`kNumPlayerPowers` 33 → 34), which also
means a `bindings/_core.cc` `.value()` line — omitted at first, and caught
mechanically by `test_every_player_power_is_bound` (33 ≠ 34), which is the bug
class that test exists for.

**A test-writing trap worth recording.** The first Self-Forming Clay test ended
the turn and expected 3 block, and read 6. The enemy's attack during the enemy
phase is itself an HP loss, so the relic banks again — correct behaviour, and
now pinned by its own test. Any test that ends a turn is also testing the enemy
phase.

### 6.13 The card-play batch: sharing an executor, and a near-miss

Three relics — Mummified Hand, Medical Kit, Strange Spoon. 85 → 88. All three
change what happens when a card is PLAYED, which is why they batch together.

**Medical Kit** is a query-layer exception, not an effect: `is_playable` lets a
Status through when the relic is held, and the pile move exhausts it. Status
only — Blue Candle is the curse equivalent and stays blocked on curses reaching
a deck. Slimed is untouched by either: it is already playable by design (cost 1,
exhausts), exactly as in StS.

**Strange Spoon** rolls 50% to discard a card that would exhaust. Three details
from the source rather than the card text: it applies only to the PLAYED card's
own exhaust (StS puts it in `UseCardAction`, so a card exhausted from hand by
Second Wind or Fiend Fire is untouched), it excludes Powers, and it rolls from
`cardRandomRng` — our `card_rng`, the same stream Discovery and Infernal Blade
use. The Power exclusion needs no code: our pile move is already gated on
non-Power.

**Mummified Hand shares Madness's executor** rather than duplicating it, and the
two differ in exactly two ways, both now carried on the action instead of being
hardcoded: Madness lasts `ThisCombat` and falls back to printed cost when every
card is already discounted, while Mummified Hand is `UntilPlayed` with no
fallback — StS filters it on `cost > 0 && costForTurn > 0 && !freeToPlayOnce`
and stops there. The `UntilPlayed` duration is the wiki correcting the card's
own text: "the description states that it lasts until the end of the turn, but
this is not in fact the case".

⚠️ **The near-miss worth recording.** Madness pushed a bare
`Action{ActionKind::DiscountRandomCardInHand}`, so `card_cost_duration`
defaulted to `None`. Moving the duration onto the action would have silently set
Madness's discount to `None` and disabled its fallback tier — a live bug in a
shipped card, introduced by a change to a different relic. Caught by reading the
push site before writing, not by a test, and now pinned by
`RelicTriggers.MadnessStillDiscountsForTheWholeCombat`. Sharing an executor
means auditing every existing caller for the fields it never had to set.

**Red Skull was split out** (task, and a new blocker row above). It keys on
crossing 50% Max HP in both directions — `onBloodied` and `onNotBloodied` — so
it fires on HEALING as well as damage and cannot ride this batch's hook. Note
also the wiki detail that its removal is applied as an invisible debuff, so
Artifact can block it and leave the player at +3 with another +3 available; that
is unreachable for us while `Power::Artifact` is enemy-only.
