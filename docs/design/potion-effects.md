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
**Step 4 done** (2026-10-17): 31 of 33 — Fairy in a Bottle, never drinkable,
and Lizard Tail with it, through one death intercept (§1.3, §5.4). **Step 5
done** (2026-10-17): 32 of 33 — Smoke Bomb, `Outcome::Escaped` scored as a
win, and the smoked reward screen in the run layer (§1.4, §5.5). **Step 6
done** (2026-10-18): 33 of 33 — Entropic Brew in and out of combat, Blood
Potion and Fruit Juice out of combat, Cauldron, the floor's shared potion
stream, and a potion reward screen for potions that do not fit (§1.5, §6).
Only step 7, the TUI belt, remains.

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

### 1.3 Step 4 rulings (Rob, 2026-10-17)

| # | question | ruling |
|---|---|---|
| U1 | A `Hook::PlayerDeath` or a fixed intercept | A fixed function with StS's two checks in StS's order: Fairy in a Bottle, then Lizard Tail (§5.4). No hook. |
| U2 | Lizard Tail's used state is not observed | Left with #9, as part of relics not being in the combat observation at all. The aim is to make relics observable, as they are to a human. |
| U3 | Out-of-combat death, and Red Skull | Requirements on other work, not built now: events (#7) route their HP loss through the same intercept, without Magic Flower; Red Skull (#3) must see the revive heal lift HP above half. |

### 1.4 Step 5 rulings (Rob, 2026-10-17)

| # | question | ruling |
|---|---|---|
| V1 | The info dict for an escape | `won` is true, as Q1 scores an escape as a win, and a new `escaped` key is true. `eval.py` records `escaped` per seed. |
| V2 | What a smoked fight rolls | Match StS where the roll leaves state behind: the potion drop is rolled and not granted (its drift moves), and elite relics are drawn and not obtained (for #22). Gold and the card reward are not rolled. The run leaves the room with no reward decision. |
| — | The potion drop's count of rewards on screen | Logged as its own issue (#47): StS's count holds gold and relics only, the engine's adds the card reward. |

### 1.5 Step 6 rulings (Rob, 2026-10-18)

| # | question | ruling |
|---|---|---|
| W1 | Potion rewards that do not fit | A pending potion reward list (§6.3–§6.4): every offered potion is taken at once if they all fit; otherwise they all wait to be claimed, with discard to make room, and leaving the screen loses the rest. Combat drops on a full belt now wait instead of being lost. |
| W2 | Cauldron's hidden card reward | Match StS: rolled at shop odds and discarded, so the rarity counter moves. |

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

**Status: agreed with Rob 2026-10-17 (§1.3).**

**Sources.** Decompiled `AbstractPlayer.damage` (the intercept),
`potions/FairyPotion`, `relics/LizardTail`, `relics/MagicFlower`,
`relics/SacredBark`, `AbstractCreature.heal`, `actions/common/LoseHPAction`,
`ui/panels/PotionPopUp` and `TopPanel.destroyPotion`. wiki.gg Fairy in a
Bottle, Lizard Tail and Toy Ornithopter, checked 2026-10-17. A code search of
the decompiled repo finds "Lizard Tail" only in `LizardTail` and
`AbstractPlayer`, and "FairyPotion" only in `FairyPotion`, `PotionHelper` and
`AbstractPlayer` (a search shows where a name appears, not every effect).

**What StS does.** Every HP loss reaches `AbstractPlayer.damage`, including
`LoseHPAction`, which calls it with `HP_LOSS`. At the end of `damage`, after
the HP is subtracted and every HP-loss hook has been called, if HP is below 1
and the player lacks Mark of the Bloom:
1. **Fairy in a Bottle**, if held: HP is set to 0, the potion's `use` heals
   `max(1, (int)(maxHP × potency / 100))` through `heal`, and the potion is
   destroyed. Potency is 30, or 60 with Sacred Bark. One copy, the first in the
   belt. `damage` returns.
2. **Otherwise Lizard Tail**, if its counter is -1 (unused): HP is set to 0,
   it heals `max(1, maxHP / 2)` through `heal`, and the counter becomes -2
   (used up). `damage` returns.
3. Otherwise the player is dead.

Consequences:
- **The revive happens inside the hit**, synchronously. The rest of a
  multi-hit attack, and every action after it, lands on the revived HP. The
  wiki says the same for Lizard Tail: "Powerful enemy attacks can still kill you
  through Lizard Tail by continuing damage after the revival triggers."
- **Fairy before Lizard Tail.** The wiki: revival effects trigger "top to
  bottom, so the potion activates before Lizard Tail." Holding both, a second
  death in the same fight uses Lizard Tail.
- **The heal is a normal heal.** Magic Flower's ×1.5 (combat only) applies: 24
  becomes 36 at 80 max HP. Mark of the Bloom would make it 0 and block both
  revives. Mark of the Bloom comes from Mind Bloom, an Act 3 event, and is not
  in the engine, so nothing is built for it.
- **The revive is not drinking a potion.** Toy Ornithopter's `onUsePotion` is
  called only from `PotionPopUp`. The Fairy branch calls `use` and
  `destroyPotion` directly and never goes through it, so a Fairy revive does
  not heal 5. The wiki's Toy Ornithopter page does not mention Fairy.
- **Integer arithmetic is exact here.** `(int)(maxHP × 0.3f)` equals
  `maxHP × 30 / 100` for every reachable max HP: `0.3f` and `0.6f` sit slightly
  above 0.3 and 0.6, so a product that should be an integer is never rounded
  below it, and when it is not an integer its fractional part is a multiple of
  0.1, far larger than the float error.
- **Lizard Tail has no combat-start or combat-end behaviour.** The decompiled
  class has only `onTrigger` and `setCounter`. `relic-effects.md` §3.1 lists it
  under CombatStart and CombatEnd, rows taken from the reference; neither is a
  behaviour of the relic.

**Engine design.**
- The engine has two functions that lower the player's HP: `lose_player_hp`
  (HP loss) and `damage_player` (attack and fixed damage). Both call one new
  helper that subtracts the HP, clamps at 0 and, if HP is below 1, runs the
  intercept. A search for assignments to the player's HP finds only these two
  functions, `heal_player` and fight setup; every hit was read.
  The file's own convention (`action.cc`'s header) is that no other code
  assigns to HP.
- The intercept is a plain function with the two checks in StS's order (U1).
- The revive heal cannot go through `heal_player`, which refuses a player at 0
  HP (StS's `isDying` guard, which the player never sets). A sibling,
  `revive_player`, sets HP to 0, adds `boost_player_heal(amount)` (Magic
  Flower) and clamps to max HP.
- Fairy: one copy is removed from `CombatState::potions`, and the amount uses
  `potion_potency` (Sacred Bark). It stays out of the potion mask: it is never
  drinkable.
- Lizard Tail: `HeldRelic::counter` 0 (the engine default) means unused, and
  `kLizardTailUsedUp = -2` means used, the convention Maw Bank, Matryoshka and
  Wing Boots already use. `RunState::end_combat` already writes every relic
  counter back, so a used Lizard Tail stays used for the rest of the run.

**The questions as they were put** (rulings in §1.3):
- **U1. A hook or a fixed intercept.** Task #5 assumed a new
  `Hook::PlayerDeath`. StS has no hook: `damage` checks the potion and the
  relic by name, in a fixed order. Recommended the fixed function. A hook would
  dispatch over relics in bar order, which is the wrong order (the potion goes
  first and is not a relic), and only these two things can cancel a death.
- **U2. Lizard Tail's used state is not observed.** A human sees the relic
  greyed out once it is used. No relic is in the combat observation today, so
  this is part of that gap, not a new one. Recommended leaving it with #9. The
  Fairy count is already in the potion block.
- **U3. Out-of-combat death.** Events call `AbstractPlayer.damage` too, so both
  revives also save the player from event damage, without Magic Flower's bonus.
  The run layer has no HP-loss path yet. Recommended recording it as a
  requirement on the events work (#7) rather than building it now. Likewise,
  Red Skull (#3) must see the revive heal: StS's `heal` fires `onNotBloodied`
  when HP rises above half, which removes Red Skull's Strength.

**Tests** (`tests/test_potions.cc`, `DeathIntercept.*`, every fight built, no
randomness). Enemy hits go through the real `DealDamage` path, all in one
queue:
1. Fairy revives at 30%: 80 max HP, an enemy hit to 0 leaves 24, and the
   potion is gone.
2. Sacred Bark: 48.
3. Magic Flower raises either revive: Fairy 36, Lizard Tail 60.
4. A tiny max HP still revives to 1: the Fairy at max HP 3 (`3 × 30 / 100` is
   0) and Lizard Tail at max HP 1.
5. HP loss from a card is intercepted: Bloodletting at 3 HP leaves 24, and the
   terminal check after the card sees the fight still in progress.
6. The rest of a multi-hit lands on the revived HP: 5 HP, three hits of 10:
   24 → 14 → 4. A revive at the end of the drain would leave 24.
7. Fairy before Lizard Tail: holding both, the first death uses the Fairy and
   leaves the counter at 0; the second uses Lizard Tail (40, counter -2); the
   third leaves HP at 0.
8. Lizard Tail at odd max HP: 81 → 40.
9. A used Lizard Tail (counter -2) does not revive.
10. Two Fairies: one death uses one.
11. Toy Ornithopter does not heal on a Fairy revive: 24, not 29.

Mutation checks, all caught: the revive moved to the end of `drain` (test 6
alone), Lizard Tail checked first (7), Magic Flower skipped (3), Sacred Bark
skipped (2), the minimum of 1 dropped (4, both halves), the used-up check
dropped (7 and 9), every Fairy consumed (10), Toy Ornithopter fired (11), and
`lose_player_hp` bypassing the intercept (5).

### 5.5 Smoke Bomb (Q1)

**Status: agreed with Rob 2026-10-17 (§1.4).**

**Sources.** Decompiled `potions/SmokeBomb`, `AbstractPlayer.updateEscapeAnimation`,
`AbstractRoom.update` (the post-combat block) and `endBattle`,
`CombatRewardScreen.openCombat` and `setupItemReward`, `relics/ToyOrnithopter`.
wiki.gg Smoke Bomb and Potions, checked 2026-10-17.

**What StS does.**
- `canUse`: false if any monster in the room is a BOSS, or has Back Attack
  (Act 4's Surrounded fight). The loop covers dead monsters too, so a Slime
  Boss that has split still blocks it.
- `use` sets `room.smoked`, starts a 2.5-second escape animation and disables
  End Turn. It queues no game action. Actions already queued keep running
  during the animation: Toy Ornithopter's `onUsePotion` queues its
  `HealAction` (in combat), so it heals 5 as it does for any potion. When the
  timer runs out, `endBattle` runs. The enemies never act again.
- `endBattle` is the victory path: Meat on the Bone's `onTrigger`, then
  `player.onVictory()` (Burning Blood's heal and every other victory relic).
  It then clears the action queue.
- The reward block in `AbstractRoom.update` runs as for a win: gold is rolled
  (`treasureRng`), the elite's `dropReward` draws its relic(s) (`relicRng`),
  and `addPotionToRewards` rolls the drop and moves the ±10 drift
  (`potionRng`). Then `openCombat(..., smoked = true)` skips
  `setupItemReward`, which is where the card reward (and Prayer Wheel's
  second one) would be created. The screen shows only Proceed: no gold, relic,
  potion or card is granted. wiki.gg's Potions page: on Smoke Bomb "the game
  rolls for a potion, but does not display this potion to the player if it
  drops."

**Engine design, combat.**
- `Outcome::Escaped`, a fourth combat outcome. The run's own `outcome` (which
  reuses the enum) never takes it.
- `CombatState::is_boss`, default false. The mask refuses Smoke Bomb when it
  is true. Nothing sets it until bosses exist (#8), which will set it from the
  encounter pool as `is_elite` is set now. A room flag gives StS's answer for
  every Act 1 boss, the Slime Boss's split included. Back Attack is Act 4 and
  is not built.
- `use_potion` drains the potion's actions and Toy Ornithopter's heal, runs
  the terminal checks, and then, if the potion was Smoke Bomb and the fight
  is still on, sets `Outcome::Escaped`. The engine's drain stands in for the
  2.5-second animation. The card queue is empty at that point: a potion cannot
  be used while a choice is open (Q8), which is the only time cards wait.
- `CombatEnv` scores Escaped as Won: +1 plus the HP-shaped bonus (Q1). See V1
  for the info dict.

**Engine design, run layer** (`RunState::end_combat`, see V2):
- The CombatEnd relic hooks already fire whenever the player is alive, so an
  escape fires them as a win does.
- Hand of Greed's gold was gained during the fight, so it is kept.
- Not granted: gold, elite relics, the potion drop, and any card reward. The
  run then leaves the room without a reward decision, since StS's smoked
  screen offers nothing to choose.

**The questions as they were put** (rulings in §1.4):
- **V1. The info dict.** Q1 scores an escape as a win. Recommended
  `info["won"] = True` for Escaped, so the reward and every metric that reads
  `won` (training callbacks, `eval.py`'s win rate) agree with Q1. Add
  `info["escaped"]` so analysis can separate the two, and record `escaped`
  per seed in `eval.py` with an escape count. The alternative,
  `won = False` with `escaped = True`, would make the info dict disagree with
  the reward.
- **V2. What a smoked fight rolls.** Recommended matching StS's rolls where they
  carry state and skipping them where they cannot show:
  - **Potion drop: rolled, not granted.** The roll moves the drift, which the
    next fight's drop chance reads. The count of rewards already on screen is
    computed as for an unsmoked fight, because StS rolls before it knows the
    screen is smoked.
  - **Elite relics: drawn, not obtained.** Today the draw changes nothing,
    because relic pools are not consumed. Once they are (#22), StS's draw
    removes the relic from the pool even when smoked, and drawing it here keeps
    that right without revisiting this code.
  - **Gold: not rolled.** Every run-layer draw site builds its generator fresh
    from (seed, stream, floor), so a skipped draw cannot shift any other, and
    the gold roll leaves no counter behind. `award_combat_gold` is the only
    reader of the treasure stream.
  - **Card reward: not rolled.** StS never creates it, so the rarity counter
    must not move. Prayer Wheel's extra screen is not owed.

**Not in this step.** The TUI's end screen treats every outcome but Won as a
loss. No human can throw a Smoke Bomb until the TUI has a potion belt (step
7), so that screen is fixed there.

**A separate flag found here (#47).** StS's potion roll counts the rewards
already on the room's list, which at that point hold gold and relics but not
the card reward, which is created later. The engine counts the card reward.
A smoked fight uses the same count as an unsmoked one, so whatever is ruled
there applies to both.

**Where `won` and `escaped` live.** `CombatEnv::won()` and `escaped()`, which
the bindings read, so the rule stays out of `bindings/` and is testable in
C++. A standalone env holds no potions, so Python cannot reach an escape.

**Tests.**
- `tests/test_potions.cc`, `SmokeBomb.*`:
  1. The fight ends Escaped after Toy Ornithopter's heal (50 → 55), with the
     enemy alive.
  2. `is_boss` masks it at every target; discard stays available.
  3. `CombatEnv` scores the escape +1 plus the HP bonus, and `won()` and
     `escaped()` are both true.
  4. A real win is `won()` and not `escaped()`.
- `tests/test_relic_state.cc`, `SmokeBombEscape.*`:
  5. An escaped elite, with the potion drop forced to 100%, grants no gold,
     relic, potion or card reward, leaves the rarity counter alone, and goes
     to the map.
  6. The potion roll still moves the drift (60 → 50).
  7. The escape's drift equals the same fight won, with Black Star making the
     count of rewards matter.
  8. Burning Blood heals 6, and Hand of Greed's in-fight gold is kept.
  9. No Prayer Wheel screen is owed.
- `tests/test_mask_oracle.cc`: the independent mask derivation adds Smoke Bomb
  and its boss rule; the sweep flips `is_boss` and counts states holding a
  Smoke Bomb in a boss fight.
- `python/tests/test_smoke.py`: the info dict has `escaped`, false in a
  standalone fight.

Mutation checks. Caught: the escape never set (1, 3); the boss rule dropped
(2, oracle); the escape scored 0 (3); `won()` excluding escapes (3); gold,
the potion, the elite's relics or a card reward granted (5, 8 for gold);
the potion roll skipped (6, 7); the smoked count without the card (7); the
victory relics only on Won (8); Prayer Wheel's screen owed (9). One survives,
and is equivalent today: setting Escaped *before* the drain rather than after.
`drain` checks the outcome only after running an action, and the most a Smoke
Bomb can leave queued is Toy Ornithopter's one heal, so both orders give the
same result. It would show only if a second action could follow a potion.

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

**Step 6 detail (2026-10-17, from the decompiled chain).**
- Why one generator per floor: the run layer builds a fresh generator for each
  draw site, which is harmless where a floor has one potion draw. With Entropic
  Brew, Cauldron and the drop all on `potionRng`, two fresh generators on the
  same floor would replay the same numbers, so the Brew's first rarity roll and
  the drop's roll would be one number. The run layer keeps the floor's
  generator, combat receives it through `CombatSetup` and returns it in
  `CombatState`, and the drop continues it. Shop stock keeps its own stream, as
  now.
- `EntropicBrew.use` rolls every potion at use, one per slot the belt has (not
  per empty slot), and queues one `ObtainPotionAction` per roll. `PotionPopUp`
  then frees the Brew's own slot, before any of those run, so the Brew refills
  its own slot (wiki.gg: "including its own slot"). Each obtain takes the first
  empty slot; Sozu or a full belt loses it. Toy Ornithopter's heal is queued
  after them.
- `returnRandomPotion(rarity, limited)`: with `limited` the first draw is always
  discarded, then draws repeat until one has the rarity and is not Fruit Juice;
  without it, draws repeat until the rarity matches. The engine does not
  reproduce StS's stream, only its distribution: a rarity roll (65/25/10), then
  a uniform draw from that rarity's pool, without Fruit Juice when limited.
  wiki.gg: "Cannot generate Fruit Juice while in combat".
- Sacred Bark does not change it: its potency is the slot count.

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
Chaos rolls all p targets in `use()`; Mayhem rolls one per stack when its
start-of-turn wrapper runs; Havoc rolls as it is played. The engine carries the
slot on the `TakeTopCard` action and then on the card's `QueuedPlay`
(`roll_card_target`; `card-queue.md`). A replay (Double Tap, Duplication) reuses
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

**The card queue (#41, fixed 2026-10-10).** StS moves each autoplayed card
into a card queue that runs only once the action queue is empty, so Distilled
Chaos takes all p cards off the draw pile before playing any. The engine now
does the same (`card-queue.md`).

**X under autoplay (#42, fixed 2026-10-17).** An X-cost card Distilled Chaos
plays has X = the player's energy when the card is taken, and spends no energy
(`card-queue.md` §3.2). The engine used to play it at X = 0.

## 6. Run layer

- Out-of-combat use: Blood Potion (direct heal), Fruit Juice, Entropic Brew (§5.6),
  each with Toy Ornithopter's heal. Engine functions on `RunState` with tests in
  step 6; actions when the run env lands (#9).
- Discard is always available outside combat (Q9) — a requirement on the run env.
- Relics: Toy Ornithopter and Sacred Bark wire in step 1; Cauldron (5 random potions
  on pickup, through the potion reward screen) in step 6. Sozu, Potion Belt and White
  Beast Statue already work.

**Status of §6.1–§6.6: agreed with Rob 2026-10-18 (§1.5).**

Sources: decompiled `potions/EntropicBrew`, `BloodPotion`, `FruitJuice`,
`AbstractPotion` (`canUse`, `canDiscard`), `ui/panels/PotionPopUp`,
`actions/common/ObtainPotionAction`, `AbstractPlayer.obtainPotion`,
`rewards/RewardItem` (`claimReward`), `relics/Cauldron`,
`screens/CombatRewardScreen` (`open`, `setupItemReward`), `helpers/PotionHelper`
(`getRandomPotion`), `AbstractDungeon.returnRandomPotion`. wiki.gg Entropic
Brew and Cauldron, checked 2026-10-17.

### 6.1 Drinking outside combat

- **Which potions.** `AbstractPotion.canUse` is true only in combat. Three
  potions override it: Blood Potion, Fruit Juice and Entropic Brew are usable
  anywhere except while the room's event is We Meet Again, which also blocks
  discard. Built with events step 3 (`potions_locked`); it lasts until the next
  room is entered, the map after the event included (`events.md` §5, Rob B1).
- **When.** In StS, whenever the top panel can be clicked: map, shop, rest
  site, events, reward screens, chests. The engine allows it in every run phase
  but Combat, and not while a deck choice is open, which is where
  `discard_potion` is already refused.
- **Order** (`PotionPopUp`): the potion's `use`, then every relic's
  `onUsePotion`, then the slot is freed.
- **Effects.** Blood Potion heals `(int)(maxHP × potency / 100)`: 20%, 40% with
  Sacred Bark. Fruit Juice raises max HP by 5 (10) and heals as much. Entropic
  Brew (§5.6), unlimited, and nothing at all with Sozu. Toy Ornithopter heals 5.
  Magic Flower does not apply out of combat. The integer form of Blood
  Potion's percentage is exact, for the reason given for Fairy in a Bottle
  (§5.4).
- **API.** `RunState::drink_potion(index)`, a no-op for any other potion or a
  refused phase.

### 6.2 Cauldron

- `onEquip` adds five potions to the room's rewards with
  `PotionHelper.getRandomPotion()`: one uniform draw over the class's whole
  pool. There is no rarity roll, so a rare is as likely as a common, and Fruit
  Juice can appear (wiki.gg: "Uniquely able to generate Fruit Juice";
  duplicates allowed). The draws use `potionRng`, so the floor's generator.
- It then opens the reward screen with `open()`, whose `setupItemReward`
  creates a card reward, rolling its cards, before `onEquip` removes it. The
  roll moves the card rarity counter (W2).
- The screen holds the five potions (§6.3). Proceed returns to the shop,
  where Cauldron is sold. Unclaimed potions are lost.
- Sozu: every claim is consumed with nothing obtained (`claimReward`), so the
  potions are lost (wiki.gg: "completely useless").

### 6.3 Potion rewards that do not fit

`RewardItem.claimReward` for a potion returns false when the belt is full, and
the reward stays on the screen. The player can discard a held potion from the
top panel and claim it then. The engine today grants a combat drop at once and
loses it when the belt is full (`roll_potion_drop`), so that choice does not
exist. Cauldron's five potions make it unavoidable.

### 6.4 The questions as they were put (rulings in §1.5)

- **W1. A potion reward screen.** Recommended `pending_potion_rewards` on
  `RunState`:
  - If every offered potion fits in the free slots, they are all taken at
    once. Taking a potion is never worse than leaving it, since discard is free
    and always available, so nothing is decided by the automatic take.
  - Otherwise they all wait: `take_potion_reward(i)` claims one if a slot is
    free, and `discard_potion` makes room. Leaving the screen loses the rest.
  - After a fight, the screen ends when the card reward is taken or skipped, so
    the potions are claimed first. Cauldron's screen is modal in the shop: the
    shop is closed to purchases until `close_potion_rewards()`.
  - This changes combat drops with a full belt, which wait instead of being
    lost.
  - With Sozu a claim consumes the reward and obtains nothing, so there is no
    decision: the rewards are lost at once, as now.
  The alternative is to keep losing a reward that does not fit, record it as a
  divergence, and have Cauldron take the first potions that fit. That removes a
  real choice (which three of five), so it is not recommended.
- **W2. Cauldron's hidden card reward.** Recommended rolling it and discarding it,
  as StS does. `getRewardCards` rolls each card's rarity through the room, and
  a shop's `getCardRarity` uses rare 9 and uncommon 37 with no relic changes,
  the counter added to the roll as for the shop's own cards. The counter then
  moves as after a fight (a rare resets it, a common lowers it). Three cards,
  with Question Card and Busted Crown applied. Its only visible effect is on
  later card rewards, which is the reason to keep it. The engine adds a
  `RewardSource::Shop` with those odds.

### 6.5 Engine design

- `RunState`: the floor's potion generator, made on the first potion draw of a
  floor and kept for the rest of it; `roll_potion_drop`, Cauldron and
  out-of-combat Entropic Brew draw from it. `CombatSetup` passes it into
  combat and `end_combat` takes it back before the drop.
- `CombatState::potion_rng`. A standalone `CombatEnv` seeds it from a
  constant, as its card stream defaults to seed 0.
- Combat Entropic Brew: rolls every potion when drunk and queues an
  `ObtainPotion` action for each; the executor fills the first empty slot
  unless Sozu or a full belt.
- Cauldron in `obtain_relic`: five uniform draws to `pending_potion_rewards`,
  the hidden card reward rolled and discarded, the screen open (W1).

As built:
- `potion.h`: `potion_draw_pool(rarity, limited)`, `random_potion(rng,
  limited)` and `any_potion(rng)`. The post-combat drop now uses
  `random_potion` too, with the same draws as before.
- `RunState`: `floor_potion_rng` and the floor it belongs to,
  `potion_rng_for_floor()`, `potion_potency`, `heal`, `drink_potion`,
  `pending_potion_rewards` with `offer_potion_rewards`, `take_potion_reward`
  and `close_potion_rewards`, and `shop_is_covered()`, which the five shop
  actions check. `leave_room` clears the pending rewards. `end_combat` records
  the floor along with the generator it takes back, so the drop can never take
  the generator as stale and derive a fresh one over it.
- `card_rarity_for_roll(roll, source)`, a pure function with the thresholds,
  and `RewardSource::Shop` (9 / 37).
- `ActionKind::ObtainPotion` for combat Entropic Brew.

### 6.6 Tests

- `tests/test_potions.cc`:
  - `PotionDraws`: the limited pool is the pool without Fruit Juice, and the
    unlimited rare pool contains it.
  - `EntropicBrew`: fills every empty slot, its own included; rolls once per
    belt slot (checked against the stream); never makes Fruit Juice (an
    invariant over 200 seeds); obtains nothing with Sozu.
- `tests/test_relic_state.cc`:
  - `PotionsOutOfCombat`: Blood Potion 20% and 40%; Fruit Juice +5 and +10;
    Toy Ornithopter +5 with no Magic Flower bonus; other potions refused; not
    in Combat phase or during a deck choice; Entropic Brew fills the belt from
    the floor's stream; its potions are the stream's unlimited draws (32
    seeds); with Sozu it rolls nothing.
  - `FloorPotionStream`: combat borrows the floor's generator as it is; the
    drop continues where the fight left it.
  - `PotionRewards`: a drop on a full belt waits, cannot be claimed until a
    slot is free, and is lost when the room is left; a drop with room is taken
    at once.
  - `CauldronRelic`: five uniform draws from the floor's stream, predicted
    with a plain uniform draw (16 seeds); five wait when they do not fit, and
    the claim and discard order works; all five taken with Potion Belt; none
    with Sozu; the screen closes the shop until closed; the hidden card reward
    leaves the rarity counter where a shop-odds reward does and the offer on
    screen untouched (64 seeds).
  - `CardRarityOdds`: the shop, fight, elite and boss thresholds, exhaustively
    at each boundary.
- `tests/test_mask_oracle.cc`: Entropic Brew joins the drinkable list.

Mutation checks, all caught: combat Brew rolling per empty slot; combat Brew
unlimited; out-of-combat Brew limited; Cauldron with a rarity roll; the hidden
reward at a fight's odds; potions auto-taken when they do not all fit;
`leave_room` keeping pending rewards; the shop not closed by Cauldron's screen;
`end_combat` not taking the generator back; `begin_combat` lending a fresh one;
a full-belt drop lost as before; drinking in Combat phase; no Toy Ornithopter
heal out of combat; Sacred Bark ignored out of combat; Sozu ignored in combat.

Two of those were caught only after a test was added for them. A limited draw
consumes the stream exactly as an unlimited one does, so a stream check could
not tell them apart; the prediction test compares the potions themselves,
which differ whenever a rare is rolled. A one-roll-per-potion Cauldron was
likewise invisible until its draws were predicted.

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
4. **Fairy in a Bottle and Lizard Tail** (§5.4, task #5). ✅ 2026-10-17.
5. **Smoke Bomb** (§5.5). ✅ 2026-10-17.
6. **Entropic Brew, Cauldron, out-of-combat use** (§5.6, §6). ✅ 2026-10-18.
7. **TUI potion belt** (Q10).
