# Potion effects — design

**Status:** rulings agreed with Rob on 2026-10-04 (§1). Steps in §8 are
implemented in order; each lands as its own commit. **Step 1 done**
(2026-10-05): 20 of 33 potions drinkable, every potion discardable, the
observation block, Toy Ornithopter, Sacred Bark, debuff-typed powers (#30).

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
| Attack / Skill / Power Potion | Choose 1 of 3 distinct class cards of that type (HEALING-tagged excluded), from `card_rng`; **skippable**; the card costs 0 this turn; Bark adds 2 copies | 1 (2) | — | Exists: DiscoverCard. **New**: typed pools, optional, copies (§5.2) |
| Colorless Potion | As above from the colorless pool; **not skippable** | 1 (2) | — | As above |
| Flex Potion | Strength + Strength Down | 5 (10) | — | Exists; debuff-typed per Q4 |
| Speed Potion | Dexterity + Dexterity Down (end of turn: lose it, remove) | 5 (10) | — | **New**: Dexterity Down, debuff-typed (Q4) |
| Blessing of the Forge | Upgrade every card in hand | — | — | Exists: UpgradeHand |
| Elixir | Exhaust any number of cards in hand (zero allowed) | — | — | **New** ChoiceKind on the multi-select path |
| Regen Potion | End of the player's turn: heal p if alive, then p −= 1; removed at 0 | 5 (10) | — | **New**: Power::Regen (Q2) |
| Ancient Potion | Artifact | 1 (2) | — | Exists |
| Liquid Bronze | Thorns | 3 (6) | — | Exists |
| Gambler's Brew | If the hand is non-empty: discard any number (zero allowed), then draw that many (the draw goes to the queue's front) | — | — | **New** ChoiceKind on the multi-select path |
| Essence of Steel | Plated Armor | 4 (8) | — | Exists |
| Duplication Potion | The next p cards of any type are played twice; expiry per Q3 | 1 (2) | — | **New**: Power::Duplication |
| Distilled Chaos | Play the top p cards of the draw pile, not exhausting; each target is a random living enemy from `card_rng`, **all rolled at use** | 3 (6) | — | Exists: top-of-draw play (Mayhem). **New**: pre-rolled targets |
| Liquid Memories | Discard pile → hand, costing 0 this turn. Empty discard: nothing. Discard size ≤ p: all of it, hand limit permitting. Else choose p, not optional | 1 (2) | — | **New** ChoiceKind: DiscardToHand |
| Heart of Iron | Metallicize | 6 (12) | — | Exists |
| Cultist Potion | Ritual on the player: gain p Strength at the end of each of its turns, starting this one | 1 (2) | — | Q5 |
| Fruit Juice | +p Max HP, healing the same amount | 5 (10) | — | Exists: GainMaxHp. Also out of combat (§6) |
| Snecko Oil | Draw p, then every hand card with cost ≥ 0 rolls 0–3 from `card_rng`; if different, its combat cost becomes the roll. Unlike Confusion it does not clear `free_once` | 5 (10) draw | — | **New**: randomize-hand-costs action |
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

The existing DiscoverCard machinery (three distinct cards from three fixed draws on
`card_rng`, `colorless-effects.md` batch notes) gains a pool per card type, an
optional flag, and a copy count. With 2 copies (Sacred Bark): both go to hand if
they fit; at 9 cards one goes to hand and one to the discard; at 10 both go to the
discard (`DiscoveryAction`). StS regenerates the choices on every frame of the
action's update, so its draw count depends on frame rate; the fixed three draws are
the reproducible model, as already ruled for Discovery.

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
   Elixir; Gambler's Brew.
3. **Card-play potions:** Duplication, Distilled Chaos, Snecko Oil.
4. **Fairy in a Bottle and Lizard Tail** (§5.4, task #5).
5. **Smoke Bomb** (§5.5).
6. **Entropic Brew, Cauldron, out-of-combat use** (§5.6, §6).
7. **TUI potion belt** (Q10).
