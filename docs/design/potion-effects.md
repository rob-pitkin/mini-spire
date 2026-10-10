# Potion effects — design

**Status:** rulings agreed with Rob on 2026-10-04 (§1) and 2026-10-05/09
(§1.1, step 2). Steps in §8 are implemented in order; each lands as its own
commit. **Step 1 done** (2026-10-05): 20 of 33 potions drinkable, every potion
discardable, the observation block, Toy Ornithopter, Sacred Bark, debuff-typed
powers (#30). **Step 2 done** (2026-10-10): 27 of 33 drinkable — the four
Discovery potions, Elixir, Gambler's Brew, Liquid Memories — plus Gambling Chip,
minimum picks, and the choice header and staged plane (#33). **Step 3 done**
(2026-10-10): 30 of 33 — Duplication, Distilled Chaos, Snecko Oil — plus
Double Tap's charge timing and Havoc's and Mayhem's targets (§1.2, §5.8).

Potions are first-class combat state (`v2-spec.md` §3.0.1): held in
`CombatState::potions`, observable, and usable through the combat action space.
The 33 potions an Ironclad can hold are `PotionId` (`src/potion.h`), with rarity,
name and targeting already in `src/potion.cc`.

## 0. Sources

- **Decompiled StS1** (`aeubanks/sts`): all 33 Ironclad-pool potions in
  `potions/`; `AbstractPotion` (`canUse`, `canDiscard`, `getPotency`);
  `ui/panels/PotionPopUp` (the use and discard flow); `TopPanel.destroyPotion`;
  `ObtainPotionAction`; `AbstractDungeon.returnRandomPotion`;
  `AbstractPlayer.damage` (Fairy in a Bottle) and `updateEscapeAnimation`;
  `AbstractRoom.update`/`endBattle` and `CombatRewardScreen.openCombat` (Smoke
  Bomb); `ApplyPowerAction` (Corruption, Artifact); the powers `RegenPower`,
  `DuplicationPower`, `RitualPower`, `LoseDexterityPower`, `CurlUpPower`,
  `AngryPower`; the actions `RegenAction`, `DiscoveryAction`,
  `BetterDiscardPileToHandAction`, `GamblingChipAction`,
  `RandomizeHandCostAction`, `PlayTopCardAction`; the relics `ToyOrnithopter`,
  `SacredBark`, `Sozu`.
- **wiki.gg** `Potions` and `Potions_List`, cross-checked 2026-10-04. The pool
  (16 "Any" commons + Blood Potion, 8 "Any" uncommons + Elixir, 6 "Any" rares +
  Heart of Iron = 33), every rarity, every number and every Sacred Bark value
  match the decompiled `getPotency`. One disagreement, resolved in §1 Q3.

## 1. Rulings (Rob, 2026-10-04)

| # | question | ruling |
|---|---|---|
| Q1 | Smoke Bomb in the single-combat env | `Outcome::Escaped`, **scored as a win**: escaping is a way past a fight, and a run rewards finishing, not how. The HP-shaped bonus applies as on a win. |
| Q2 | Regen loses a stack each turn | A second named exception to "powers never tick", beside Intangible. StS types it BUFF, so it must not be a Debuff (Artifact would block it). |
| Q3 | Duplication's expiry | Follow the decompiled `atEndOfRound`: reduce by 1, remove at 0. With Sacred Bark an unused charge carries into the next turn. The wiki's "this turn" is wrong for that case. |
| Q4 | Debuff-typed powers (#30) | Fixed in step 1: Strength Down (Flex, Flex Potion) and the new Dexterity Down (Speed Potion) are debuff-typed — Artifact negates them, Orange Pellets removes them. |
| Q5 | Cultist Potion | Reuse `Power::Ritual`, with a player-side end-of-turn hook. The enemy behaviour is unchanged. |
| Q6 | Entropic Brew | Combat carries the floor's potion RNG and the slot count (§5.6). The slot count is observed. |
| Q7 | Out-of-combat use | Engine functions and tests in step 6; actions when the run env lands (task #9). |
| Q8 | Potion use during a pending choice | **Forbidden**, use and discard both. StS may allow it; Rob judges it changes no outcome or strategy, so it is not listed as a divergence. |
| Q9 | Observation | Potions are observed in combat (§3.2). Outside combat, discard is always an available action and the belt is observed — a requirement on the run env (task #9). |
| Q10 | TUI | A potion belt display with use (with targeting) and discard. Sequenced last; manual play-testing waits for a larger milestone. |

### 1.1 Step 2 rulings (Rob, 2026-10-05; S7 2026-10-09)

| # | question | ruling |
|---|---|---|
| S1 | Where a potion's choice gets its settings | On the `RequestChoice` action: copies, maximum picks, minimum picks, optional, the generation pool, and whether the chosen card costs 0 this turn. Cards fill them from `CARD_DATABASE` as before; potions fill them from potency. A potion's menu has no source card, like Toolbox's. |
| S2 | Menu kinds for the Discovery potions | **No new kinds.** `DiscoverCard` serves Discovery and the Attack, Skill and Power Potions; `DiscoverColorlessCard` serves Toolbox and the Colorless Potion. The pool, skippability, cost-0 and copies are settings (S1). New: a class-Power pool, and a TUI prompt that names the card type. |
| S3 | Liquid Memories with Sacred Bark ("choose exactly 2") | A **minimum-picks** setting: Decline is legal only once that many cards are picked. 0 for every other multi-select. |
| S4 | Elixir and Gambler's Brew | Elixir reuses `ExhaustCardInHand`. Gambler's Brew gets a new kind, `DiscardForDraw`: discard every pick, then draw that many. Both allow 0 to a full hand. |
| S5 | Gambling Chip | Becomes a real choice (it is Gambler's Brew at the start of combat) and leaves the `v2-spec.md` §9 divergence table. |
| S6 | Staged picks are not observed (#33) | Fixed in step 2: a per-card-type count of staged picks in the choice block (§3.3). |
| S7 | What the menu tells a human that the header does not (copies, cost 0 on arrival, picks left) | Three header floats (§3.3). A potion's menu has no source card, and Sacred Bark is not in the combat observation, so nothing else carries them. |

### 1.2 Step 3 rulings (Rob, 2026-10-10)

| # | question | ruling |
|---|---|---|
| T1 | When Duplication spends a charge | When the original card is played: `DuplicationPower.onUseCard` decrements on the spot and queues the replay. Not when the replay resolves. Double Tap moves to the same rule, where the change is unobservable, so the two share one. |
| T2 | Duplication as a power | `Power::Duplication`, a BUFF, observed with the player's powers. It replays any card type except a replay. With Double Tap also up, an Attack is played three times. Expiry per Q3. |
| T3 | Distilled Chaos targets | Rolled at use from `card_rng`, one per card (§5.8). A card whose target has died by the time it plays is not played: it goes to its pile with no effect. A card whose target escaped is lost. |
| T4 | Havoc and Mayhem | Moved onto the same pre-rolled target. StS plays all three through `PlayTopCardAction`; the engine rolled Havoc's and Mayhem's targets from the combat stream at play time. |
| T5 | A top-of-draw card that cannot be used | The whole `canUse` rule, not only T3's dead target (§5.8). A card that fails `is_playable` (unplayable without Medical Kit, an Attack while Entangled, Clash, Secret Technique/Weapon) goes to its pile with no effect and no on-play triggers. It is exhausted only for Havoc or a card with its own Exhaust, else discarded. This fixes Mayhem exhausting an unplayable card, and autoplayed Attacks ignoring Entangle. |

## 2. Using and discarding a potion

Mirrors `PotionPopUp`:

1. **Remove one copy** of the potion from `CombatState::potions`. StS frees the
   slot (`destroyPotion`) synchronously, before any queued effect runs — which is
   why Entropic Brew refills its own slot.
2. **Queue the potion's effects** (§4), with Sacred Bark's doubled potency.
3. **Fire `Hook::PotionDrunk`** — Toy Ornithopter queues Heal 5 — so its effect
   resolves after the potion's own.
4. Drain.

A potion is not a card: no energy, no card-played hooks.

**When a potion can be used** (`canUse`): in combat, at a decision point with no
pending choice (Q8), the fight in progress. Exceptions:
- Fairy in a Bottle: never (it triggers on death, §5.4).
- Smoke Bomb: not in a boss fight (`CombatState::is_boss`, default false until
  bosses land, task #8); StS's other exclusion, BackAttack, is Act 4.
- Blood Potion, Fruit Juice, Entropic Brew: also outside combat (§6).

**Discard** is always available in combat (except during a pending choice, Q8)
and outside it (Q9). It removes one copy and does nothing else.

**Sacred Bark** doubles `getPotency()`. Potions that have no potency, or do not
read it, are unaffected: Blessing of the Forge, Elixir, Gambler's Brew, Smoke
Bomb, and Entropic Brew (its loop reads the slot count). The wiki's exclusion
list agrees.

## 3. Action and observation

### 3.1 Actions

Both blocks are already reserved (`v2-spec.md` §6): **PotionUse** is
potion × target (33 × 5), **PotionDiscard** is 33. Potions carry no per-copy
state, so a `PotionId` names any copy.

Mask, in combat with no pending choice:
- PotionUse: each held, usable potion lights target slots of living enemies if it
  targets one (Fire, Weak, Fear — `potion_targets_enemy`), else slot 0 only.
- PotionDiscard: each held potion.

### 3.2 Observation

A potion block **appended after the choice block**, so every existing index keeps
its position:

| floats | content |
|---|---|
| 33 | count held of each `PotionId` (`v2-spec.md` §5.6: a count vector, not slots) |
| 1 | slot capacity (3, or 5 with Potion Belt); empty slots = capacity − sum of counts |

### 3.3 The choice block (step 2)

**Header (S7).** Three floats join the five-float header (pending, kind,
source_pile, source_card, is_optional), each one something the screen tells a
human:

| float | content | why the agent cannot get it elsewhere |
|---|---|---|
| copies | how many copies the chosen card adds | "add 2 copies" is Sacred Bark's doubling, and relics are not observed in combat |
| arrives free | 1 if the chosen card costs 0 this turn | Colorless Potion (free) and Toolbox (full price) share a kind and have no source card |
| picks left | `max_picks − picks_made` | Liquid Memories' "choose 2" under Bark. Whether that many are required is `is_optional`, which now means "may stop now" (§5.7) |

**Staged picks (S6, #33).** A plane of `kNumCardTypes` floats is appended to the
choice block, after the option slots: how many cards of each type are staged in
the open multi-select. A human sees the selected cards; until now the agent saw
them vanish from the hand without knowing which it had picked. The potion block
moves back by 270 floats; it is addressed through `kPotionObsIndex`, so nothing
that reads the constant changes.

What a staged pick looks like in the piles follows the StS screen:
- **Hand choices** (Elixir, Gambler's Brew, Gambling Chip, Purity, Forethought+)
  use `HandCardSelectScreen`, which removes a selected card from the hand
  (`hand.removeCard`). The card leaves the hand plane and appears in the staged
  plane.
- **Discard choices** (Liquid Memories) use `GridCardSelectScreen`, which only
  highlights a selected card. It stays in the discard pile and plane until the
  choice finishes, and appears in the staged plane as well.

## 4. The potions

"Bark" is the Sacred Bark value. "Exists" means the engine already has the
mechanism.

| Potion | Effect | Base (Bark) | Target | Mechanism |
|---|---|---|---|---|
| Blood Potion | Heal `(int)(maxHP × p / 100)` | 20% (40%) | — | Exists: Heal. Also out of combat (§6) |
| Block Potion | Gain block, not card block (no Dexterity, no Frail) | 12 (24) | — | Exists: GainBlock |
| Dexterity Potion | Dexterity | 2 (4) | — | Exists |
| Energy Potion | Energy | 2 (4) | — | Exists |
| Explosive Potion | Pure damage to every enemy, no owner: no Vulnerable, no Curl Up, no Angry | 10 (20) | — | Exists: DamageAllEnemies |
| Fire Potion | THORNS-type damage to the target, owned by the player: no Strength, no Weak, **no Vulnerable** (§5.1); no Curl Up, no Angry | 20 (40) | enemy | Exists: DealFixedDamage |
| Strength Potion | Strength | 2 (4) | — | Exists |
| Swift Potion | Draw | 3 (6) | — | Exists |
| Weak Potion | Weak on the target | 3 (6) | enemy | Exists |
| Fear Potion | Vulnerable on the target | 3 (6) | enemy | Exists |
| Attack / Skill / Power Potion | Choose 1 of 3 distinct class cards of that type (HEALING-tagged excluded), from `card_rng`; **skippable**; the card costs 0 this turn; Bark adds 2 copies | 1 (2) | — | `DiscoverCard` with settings (S1, S2, §5.2). **New**: the class-Power pool |
| Colorless Potion | As above from the colorless pool; **not skippable** (§5.2) | 1 (2) | — | `DiscoverColorlessCard` with settings (S2) |
| Flex Potion | Strength + Strength Down | 5 (10) | — | Exists; debuff-typed per Q4 |
| Speed Potion | Dexterity + Dexterity Down (end of turn: lose it, remove) | 5 (10) | — | **New**: Dexterity Down, debuff-typed (Q4) |
| Blessing of the Forge | Upgrade every card in hand | — | — | Exists: UpgradeHand |
| Elixir | Exhaust any number of cards in hand (zero allowed) | — | — | `ExhaustCardInHand`, multi-select, 0 to a full hand (S4, §5.7) |
| Regen Potion | End of the player's turn: heal p if alive, then p −= 1; removed at 0 | 5 (10) | — | **New**: Power::Regen (Q2) |
| Ancient Potion | Artifact | 1 (2) | — | Exists |
| Liquid Bronze | Thorns | 3 (6) | — | Exists |
| Gambler's Brew | If the hand is non-empty: discard any number (zero allowed), then draw that many (the draw goes to the queue's front) | — | — | **New** `DiscardForDraw`, multi-select (S4, §5.7) |
| Essence of Steel | Plated Armor | 4 (8) | — | Exists |
| Duplication Potion | The next p cards of any type are played twice; a charge is spent as the card is played (T1); expiry per Q3 | 1 (2) | — | `Power::Duplication`, sharing Double Tap's replay (T2) |
| Distilled Chaos | Play the top p cards of the draw pile, not exhausting; each target is a random living enemy from `card_rng`, **all rolled at use** | 3 (6) | — | The top-of-draw play shared with Havoc and Mayhem, with pre-rolled targets (§5.8) |
| Liquid Memories | Discard pile → hand, costing 0 this turn. Empty discard: nothing. Discard size ≤ p: all of it, hand limit permitting. Else choose exactly p | 1 (2) | — | **New** `DiscardToHand`; minimum picks (S3, §5.7) |
| Heart of Iron | Metallicize | 6 (12) | — | Exists |
| Cultist Potion | Ritual on the player: gain p Strength at the end of each of its turns, starting this one | 1 (2) | — | Q5 |
| Fruit Juice | +p Max HP, healing the same amount | 5 (10) | — | Exists: GainMaxHp. Also out of combat (§6) |
| Snecko Oil | Draw p, then every hand card with cost ≥ 0 rolls 0–3 from `card_rng`; if different, its combat cost becomes the roll. Unlike Confusion it does not clear `free_once` | 5 (10) draw | — | `RandomizeHandCosts`: one roll per hand card that is neither X-cost nor unplayable (ours print unplayables at 0, so the flag excludes them) |
| Fairy in a Bottle | Never drinkable; see §5.4 | 30% (60%) | — | **New**: death intercept |
| Smoke Bomb | Escape the fight; see §5.5 | — | — | **New**: `Outcome::Escaped` |
| Entropic Brew | Fill every empty slot with a random potion; see §5.6 | — | — | **New**: potion RNG in combat |

## 5. New mechanisms

### 5.1 Fire Potion's damage

StS builds THORNS-type damage owned by the player and calls
`applyEnemyPowersOnly`, which runs only the target's `atDamageReceive` hooks. But
`VulnerablePower.atDamageReceive` multiplies only `NORMAL` damage
(`powers/VulnerablePower.java:58-69`), so **Vulnerable does not apply**; no Act 1
enemy has a power that modifies THORNS damage. sts_lightspeed agrees: a flat
`DamageEnemy(target, 20)` (`src/combat/BattleContext.cpp:2340-2342`). The wiki says
nothing either way. So Fire Potion is the existing fixed-damage path: no Strength,
Weak or Vulnerable, absorbed by block, firing no attack-only enemy triggers. Act 1
enemies with on-hit powers ignore it: Curl Up needs NORMAL damage with an owner,
Angry needs an owner and not THORNS. The Guardian's Mode Shift and Lagavulin's wake
key on HP loss, so they do see it.

(An earlier draft of this section said Vulnerable applies. That read
`applyEnemyPowersOnly` without following it into `VulnerablePower`.)

### 5.2 Discovery potions

All four potions are `DiscoveryAction`, the action behind the Discovery card. Its
constructors and `customCombatOpen(choices, text, skippable)` give the settings:

| opened by | kind | pool | skippable | costs 0 this turn | copies |
|---|---|---|---|---|---|
| Discovery (card) | `DiscoverCard` | class, any type | no | yes | 1 |
| Attack / Skill / Power Potion | `DiscoverCard` | class, that type | **yes** | yes | p (Bark: 2) |
| Colorless Potion | `DiscoverColorlessCard` | colorless | **no** | yes | p (Bark: 2) |
| Toolbox (relic) | `DiscoverColorlessCard` | colorless | no | **no** | 1 |

Skippability is `customCombatOpen`'s third argument, which `DiscoveryAction`
passes as `this.cardType != null`. The colorless constructor never sets a card
type, so the Colorless Potion's screen has no skip button, the same as
Discovery's. (Toolbox is not `DiscoveryAction`; its row is unchanged.) The pools
come from `returnTrulyRandomCardInCombat(type)` and
`returnTrulyRandomColorlessCardInCombat`: the obtainable rarity pools filtered to
the type, HEALING-tagged cards excluded. The engine has every pool but the
class Powers (14 cards); step 2 adds it.

The existing machinery is unchanged: three distinct cards from three fixed draws
on `card_rng` (`colorless-effects.md` batch notes). StS regenerates the choices
on every frame of the action's update, so its draw count depends on frame rate;
the fixed three draws are the reproducible model, as already ruled for Discovery.

With 2 copies (Sacred Bark), each copy costs 0 this turn and goes to hand while
it fits: at 8 cards or fewer both go to hand, at 9 one goes to hand and one to the
discard, at 10 both go to the discard (`DiscoveryAction`). Adding the copies one
at a time through `add_card_to_hand` gives exactly that table.

### 5.3 Debuff-typed powers (Q4, task #30)

A `power_is_debuff(Power)` predicate: Strength Down and Dexterity Down. `apply_power`
lets Artifact negate an application of one (as for a negative Strength
application), and `RemoveAllDebuffs` (Orange Pellets) removes them. Cross-check the
wiki's Artifact and Flex pages before building.

### 5.4 Death intercept (with Lizard Tail, task #5)

When the player's HP would drop below 1 (`AbstractPlayer.damage`), unless Mark of
the Bloom (a boss relic, absent): Fairy in a Bottle first — HP to 0, heal
`max(1, (int)(maxHP × p%))` through the normal heal path (Magic Flower applies),
remove the potion; otherwise Lizard Tail, once per run. Built once, for both.

### 5.5 Smoke Bomb (Q1)

- **Combat:** usable unless the fight is a boss fight. Using it ends the fight with
  `Outcome::Escaped` after the queue drains. The standalone env scores it as a win,
  HP-shaped bonus included; its info reports `escaped` as well as `won`.
- **Run layer** (`AbstractRoom.update`, `CombatRewardScreen.openCombat`): the fight
  ends through `endBattle`, so the victory relic hooks fire (Burning Blood heals,
  Meat on the Bone triggers). The reward phase still rolls gold (treasure stream),
  the elite relic (relic stream) and the potion drop with its ±10 drift (potion
  stream); then the smoked reward screen grants none of it, and no card reward is
  rolled.

### 5.6 The potion stream and slot count in combat (Q6)

The run layer derives each floor's potion generator from (run seed, Potion, floor)
and today creates it fresh for each roll. Entropic Brew must draw from it during
combat, and in StS its draws come before the post-combat drop roll on the same
`potionRng`. So:
- `CombatSetup` carries the floor's potion generator and the slot capacity;
  `CombatState` holds both. A standalone `CombatEnv` derives the generator from its
  own seed and uses capacity 3 (5 with Potion Belt).
- `RunState` keeps the current floor's potion generator, continues it after combat,
  and `roll_potion_drop` draws from that rather than re-deriving it.

Entropic Brew in combat rolls `returnRandomPotion(limited=true)` once per slot at use
(rarity roll, then draws until the rarity matches, re-drawing Fruit Juice), then
obtains each into the first empty slot: Sozu or a full belt drops it, but the rolls
happen regardless. Out of combat it rolls with `limited=false`, and with Sozu rolls
nothing at all.

### 5.7 Multi-select choices: Elixir, Gambler's Brew, Gambling Chip, Liquid Memories

These extend the sequential single-pick model (`colorless-effects.md` D1): each
pick stages a card, Decline means "done", and every staged card resolves together
when the choice finishes.

**Minimum picks (S3).** Decline is legal once `picks_made ≥ min_picks`, so
`is_optional` is recomputed on every rebuild instead of being fixed at `true` for
a multi-select. When declining is illegal and exactly one option remains, the
engine stages it without pausing. A single-pick choice already applies a lone
mandatory option without pausing, and the reason carries over: a step with one
legal action has no decision in it.

| choice | kind | source | min | max | finish |
|---|---|---|---|---|---|
| Elixir | `ExhaustCardInHand` | hand | 0 | full hand | exhaust each, through `ExhaustCard` (Feel No Pain, Dark Embrace, Sentinel see it) |
| Gambler's Brew | `DiscardForDraw` | hand | 0 | full hand | discard each, then draw that many at the front of the queue |
| Gambling Chip | `DiscardForDraw` | hand | 0 | full hand | as Gambler's Brew |
| Liquid Memories | `DiscardToHand` | discard | p | p | in pick order: to hand costing 0 this turn while it fits; otherwise left in the discard pile |

"Full hand" is `kMaxMultiSelectPicks` (10, the hand limit). StS passes 99
(`ExhaustAction(false, true, true)`, `GamblingChipAction`), which the hand bounds
the same way.

**Elixir** (`ExhaustAction(isRandom=false, anyNumber=true, canPickZero=true)`):
an empty hand ends it. `anyNumber` skips the "exhaust the whole hand when it holds
no more than `amount`" shortcut, so every non-empty hand opens the screen.

**Gambler's Brew and Gambling Chip** (`GamblingChipAction`): `handCardSelectScreen`
with 99 picks, any number, zero allowed. On confirm, if anything was picked it
queues `DrawCardAction(n)` with `addToTop`, then moves each pick to the discard
pile. Gambler's Brew opens it only if the hand is non-empty; Gambling Chip does not
check, but an empty hand offers nothing, which the engine already skips. Each
discard also calls `incrementDiscard` and `triggerOnManualDiscard`. The relics
that react (Tingsha, Tough Bandages, Hovering Kite) and the cards (Reflex,
Tactician, Eviscerate) are all Silent's, so neither has an Ironclad effect.

**Gambling Chip's timing**: `atTurnStartPostDraw`, once per combat (its `activated`
flag is reset in `atBattleStartPreDraw`). That is turn 1's `Hook::TurnStartPostDraw`,
which `CombatStartPostDraw` already fires. The wiki agrees: it waits until the
opening hand and the start-of-combat relics that add cards (Ring of the Snake,
the Bottled relics) have resolved, so those cards can be discarded.

**Liquid Memories** (`BetterDiscardPileToHandAction(p, newCost=0)`, not optional):
- Empty discard pile: nothing.
- Discard size ≤ p: no screen. Every card is taken in pile order, each to hand
  costing 0 this turn while the hand has room; a card that does not fit stays in
  the discard pile.
- Otherwise `gridSelectScreen` for exactly p cards. The 4-argument `open` it uses
  shows no cancel button outside upgrade, transform, purge or the shop, and the
  screen closes only when p cards are selected. On close, each selected card in
  turn goes to hand costing 0 if it fits, and otherwise stays where it is.

A grid-selected card never leaves the discard pile while the screen is open
(§3.3), so the engine does not take a staged discard card out of its pile. It
leaves it in place and excludes it from the rebuilt offer. A card that does not
fit in the hand at the finish therefore stays at its original position, as in
StS.

**Sacred Bark** doubles Liquid Memories' p (1 → 2). Elixir, Gambler's Brew and
Gambling Chip read no potency.

### 5.8 Autoplayed cards: the top-of-draw play and the replays (T1–T5)

StS plays a card that the player did not pick through its card queue
(`GameActionManager`), and two rules apply to every such card:

**The target is fixed when the play is queued.** Havoc, Mayhem and Distilled
Chaos all use `PlayTopCardAction(target, exhausts)`, and each caller passes
`getRandomMonster(null, true, cardRandomRng)` as it queues the action. Distilled
Chaos rolls all p targets in `use()`; Mayhem rolls one per stack at the start of
the turn; Havoc rolls as it is played. The engine carries the slot on the
`PlayCard` action (`roll_card_target`). A replay (Double Tap, Duplication) reuses
the original play's target.

**A card that cannot be used is not played.** The card queue checks
`canUse(player, target)`: `is_playable` (unplayable without Medical Kit, an
Attack while Entangled, Clash, Secret Technique/Weapon), and for a single-target
card a target that is not dying. A card that fails goes through
`UseCardAction` with `dontTriggerOnUseCard`: no effect, no on-play triggers, and
only its pile move. It is exhausted under Havoc (`exhaustOnUseOnce`) or by its
own Exhaust, Strange Spoon permitting, and discarded otherwise. A replay that
fails vanishes, being `purgeOnUse`.

`canUse` passes for an enemy that has escaped rather than died, and StS then
drops the card from limbo without putting it in any pile (line 254). That branch
cannot be reached here: every roll and every play in this section happens within
the player's turn, enemies only escape on their own, and the roll skips an
enemy that has already escaped.

**Replays spend their charge as the original is played (T1).**
`DoubleTapPower` and `DuplicationPower` decrement in `onUseCard` and queue the
copy. Double Tap replays an Attack, Duplication any card, and with both up an
Attack is played three times. Neither replays a replay.

**Two known gaps, open as tasks.**
- **#41, no card queue.** StS moves each autoplayed card into a card queue
  that runs only once the action queue is empty. So Distilled Chaos takes all
  p cards off the draw pile before playing any. The engine plays each before
  taking the next, which differs when a played card draws or shuffles. The
  same root cause makes Mayhem play before the turn's draw instead of after it.
- **#42, X under autoplay.** The engine plays an X-cost card at X = 0. The
  wiki's Mayhem page says it uses the full energy without spending it.

## 6. Run layer

- Out-of-combat use: Blood Potion (direct heal), Fruit Juice, Entropic Brew (§5.6),
  each with Toy Ornithopter's heal. Engine functions on `RunState` with tests in
  step 6; actions when the run env lands (#9).
- Discard is always available outside combat (Q9) — a requirement on the run env.
- Relics: Toy Ornithopter and Sacred Bark wire in step 1; Cauldron (5 random potions
  on pickup, through the potion reward screen) in step 6. Sozu, Potion Belt and White
  Beast Statue already work.

## 7. Divergences

- Discovery potions draw exactly three cards (§5.2): the same offers as StS, with a
  fixed draw count instead of StS's frame-dependent one. Not a behaviour difference.

## 8. Implementation order

1. **Plumbing and the direct potions:** PotionUse/PotionDiscard mask and apply (§2,
   §3.1); the observation block (§3.2); Sacred Bark potency; `Hook::PotionDrunk`
   with Toy Ornithopter; every potion marked "Exists" in §4 (Fire Potion among
   them, §5.1); debuff-typed powers and Speed Potion (§5.3); Regen (Q2); Cultist
   (Q5).
2. **Choice potions:** Attack, Skill, Power, Colorless (§5.2); Liquid Memories;
   Elixir; Gambler's Brew (§5.7). With them: choice settings on `RequestChoice`
   (S1), minimum picks (S3), Gambling Chip as a real choice (S5), and the
   staged-picks plane (S6, §3.3).
3. **Card-play potions:** Duplication, Distilled Chaos, Snecko Oil.
4. **Fairy in a Bottle and Lizard Tail** (§5.4, task #5).
5. **Smoke Bomb** (§5.5).
6. **Entropic Brew, Cauldron, out-of-combat use** (§5.6, §6).
7. **TUI potion belt** (Q10).
