# Colorless card effects

**Status: AGREED (Rob, 2026-09-15).** D1–D5 are ruled, including the two
follow-up confirmations. One sourcing question is still open (§2); it does not
block any batch.

This is task 1 of the agreed order for the rest of `v2-spec.md` §11 step 7:
colorless effects → relic effects → potions → events.

---

## 1. Scope

The colorless vocabulary (35 cards, 70 ids) is in `CARD_DATABASE`. When this
document was written, **22 of the 35 were marked `unplayable` because their
effect was not implemented**, not because of a game rule. For curses,
`unplayable` *is* the game rule; they are out of scope here.

**✅ ALL 35 ARE NOW PLAYABLE** (batches 1–6). The `unplayable` flag no longer
appears on any colorless card, and `Colorless.AllThirtyFiveArePlayable` fails if
one comes back.

The 22 that had to be built, by pool:

| pool | was unplayable |
|---|---|
| uncommon (8) | Dark Shackles, Discovery, Enlightenment, Forethought, Jack of All Trades, Madness, Panic Button, Purity |
| rare (14) | Apotheosis, Chrysalis, Hand of Greed, Magnetism, Mayhem, Metamorphosis, Panache, Sadistic Nature, Secret Technique, Secret Weapon, The Bomb, Thinking Ahead, Transmutation, Violence |

**Effects come before reachability** (Rob, option A). A card that reaches a
deck before its effect exists is a dead draw. So the shop's two colorless slots
and Toolbox were stocked only after this document's batches landed — both are
now live (`relic-effects.md` §6.9 for Toolbox's pre-draw pause).

---

## 2. Sourcing

As for relics (`relic-effects.md` §2): `sts_lightspeed` is evidence and the wiki
is the check. **Each batch cross-checks its cards' numbers and edge cases
immediately before code**, not in one pass up front.

**A third source appeared while designing D3: decompiled StS1 Java** (for
example `aeubanks/sts`, `powers/TheBombPower.java`). It is the game's own logic
rather than a reimplementation, so where it is unambiguous it outranks both
other sources. It is used for reading mechanics only; no code is copied.
⚠️ **OPEN (Rob):** whether it becomes a recognised source, which would amend
`prior-art-sts-lightspeed.md` §7.5. Until then it is treated as supporting
evidence alongside `sts_lightspeed`, and the wiki is still checked.

---

## 3. Machinery

| needs | cards | engine work |
|---|---|---|
| **existing machinery (probably)** | Thinking Ahead | `sts_lightspeed` implements it as `DrawCards(2)` then `WarcryAction()`, which is the shape Warcry already uses (the choice queues after the draw). The `card.h` comment saying it needs something new looks stale; a test decides. |
| **small, self-contained** | Apotheosis, Violence, Dark Shackles, Panic Button | Upgrade all four piles; move N random Attacks from draw to hand; an enemy's temporary Strength loss (same Artifact gate as ROB-95); a 2-turn "no Block from cards" state |
| **generation** | Jack of All Trades, Transmutation, Magnetism, Discovery | one primitive: pool + `CardRandom` stream + destination + cost modifier. It also fixes Infernal Blade (§4 D4), and the Attack/Skill/Power potions reuse it. |
| **per-instance cost** | Madness, Enlightenment, Forethought, Chrysalis, Metamorphosis | D2 |
| **new choice sources** | Secret Technique, Secret Weapon, Forethought | pick from the draw pile filtered by type; pick a hand card to go to the BOTTOM of the draw pile |
| **power triggers** | Panache, Sadistic Nature, Mayhem | a counter on a power; an on-debuff-applied hook; playing the top card of the draw pile at turn start |
| **delayed effect** | The Bomb | D3 |
| **combat → run write-back** | Hand of Greed | D5 |
| **multi-select** | Purity, Forethought+ | D1 |

---

## 4. Decisions

### D1 — Multi-select is a sequence of single picks (Rob, 2026-09-15)

Purity ("Exhaust up to 3 cards in your hand") and Forethought+ ("any number")
are subset choices. `v2-spec.md` §9 deferred multi-select on the premise that
"no v2.0.0 decision is a subset choice". **These two cards are, so §9 needs an
amendment** pointing here.

**Ruling:** frame the choice as sequential single picks. Pick a card, it leaves
the offer, pick another, up to the card's maximum.

- **No new action.** Decline means "done". For these cards, confirming with
  nothing selected and skipping are the same outcome, so one action covers both.
- A picked card is removed from the mask, so it cannot be picked twice.
- Reaching the maximum (Purity: 3) finishes the choice automatically.
- `PendingChoice` gains a maximum pick count and a per-option picked flag. Both
  are fixed-size, so `clone()` stays a plain copy.

**When the picked cards resolve (Rob, 2026-09-15): all together, after the
last pick.** A picked card leaves the offer but is not exhausted yet. The
exhausts apply once the choice finishes, whether by Decline or by reaching the
maximum. Exhausting each card as it is picked would change the game: Dark
Embrace draws on each exhaust, so the player would see drawn cards between
picks and could even pick one. The real game resolves Purity from one selection
screen, so nothing is drawn until the whole selection is confirmed.

**Observation requirement, for when the obs is sequenced:** the set picked so far
must be visible, because a human sees the highlighted cards.

### D2 — Per-instance cost with a duration (Rob, 2026-09-15)

Madness makes **one copy** cost 0. Enlightenment changes **only the cards in hand
now**, not cards drawn later. A per-card-id map (`free_this_turn`) cannot express
either.

**Ruling:** `Card` gains a cost override **with a duration**:

| duration | cleared when | cards |
|---|---|---|
| this turn | end of turn | Discovery, Transmutation, Enlightenment, Infernal Blade |
| this combat | never within the fight | Madness, Chrysalis, Metamorphosis, Enlightenment+ |
| until played | the card is played | Forethought |

- It is **instance state**: `same_as` and `has_instance_state` include it, so a
  free Strike and a normal Strike stay separate options in a choice.
- The query layer's `effective_cost` reads it, so the mask and resolution
  cannot disagree about a card's cost (`effects-architecture.md` §4.5). Its
  order relative to Corruption and Blood for Blood is verified in the batch
  that builds it.
- It is combat-scoped. Write-back discards the piles, so an override can never
  leak into the master deck.
- **Consequence: Infernal Blade migrates.** `free_this_turn[id]` makes *some*
  copy of that id free, not the copy Infernal Blade created. It moves to the
  instance field, and the free-this-turn observation plane then derives from
  instances. (The obs is not current work; this is recorded so it is not lost.)

Per-card semantics are checked in their batch, not assumed. For example,
Enlightenment *caps* cost at 1, which is not the same as *setting* it to 1, and
X-cost cards may be unaffected.

### D3 — The Bomb: separate timers, stored as three buckets

**Evidence (StS1), all three sources agree:**

- **Wiki (Fandom):** "Creates separate buffs for each card use rather than
  stacking normally." N starts at 3 and "decreases by 1 at the end of your turn.
  Once it reaches 0, it deals damage and expires."
- **Decompiled `TheBombPower.java`:** every play mints a new power ID
  (`"TheBomb" + bombIdOffset`), so instances never merge. `atEndOfTurn` queues
  a reduce-by-1, and **if the amount is 1**, queues `DamageAllEnemiesAction` for
  the stored damage, as `DamageType.THORNS` from a pure damage matrix
  (`createDamageMatrix(damage, true)`).
- **`sts_lightspeed`:** three `int8_t` slots, `bomb1`, `bomb2`, `bomb3`. A play
  adds its damage to `bomb3`. At the end of each turn, `bomb1` fires, then the
  slots shift down (`bomb1 = bomb2; bomb2 = bomb3; bomb3 = 0`).

**Timing:** a Bomb ticks at the end of the turn it is played. It goes off at the
end of the **third** turn, counting that one. STS2 matches, going by Rob's
description.

**Rob's concern was an unbounded number of instances, and the evidence removes
it.** Every Bomb starts at the same 3, so Bombs played on the same turn fire on
the same turn. Any number of Bombs collapses, losslessly for play, into **pending
damage by turns remaining: three numbers.**

**Representation (Rob, 2026-09-15): three slots by turns remaining.** This
replaces the first idea of showing only the closest Bomb.

| | engine | observation | lossless? |
|---|---|---|---|
| **buckets by turns remaining (proposed)** | per bucket (1, 2, 3 turns left), a count of Bombs and a count of Bomb+: 6 small ints on `Character`, plain POD | 3 floats: damage going off in 1, 2 and 3 turns | ✅ for every decision: how much damage, and when |
| closest only (Rob's first idea) | still needs every timer internally | 1–2 floats | ❌ Bombs due in 1 and 2 turns read the same as a single Bomb due in 1 |

**Why counts rather than `sts_lightspeed`'s summed damage:** in the real game
each Bomb is its own power, so each fires as a **separate** all-enemy hit.
`sts_lightspeed` adds them into one hit, which is a reimplementation shortcut.
Two separate hits and one summed hit happen to deal the same total against
Block, but the engine should fire the hits the game fires, and counts make that
possible. The observation can still sum, because total damage and timing are
the whole effect.

Parity details to verify in the batch: whether pure THORNS damage ignores
Strength, Weak and Vulnerable (it should, being pure) while still being blocked
by enemy Block, and the `areMonstersBasicallyDead` guard (probably irrelevant,
since combat has already ended by then).

### D4 — Generated cards draw from `CardRandom` (Rob, 2026-09-15)

`v2-spec.md` §3.5 already rules that in-combat generation uses a dedicated
per-combat `cardRandomRng`. `RngStream::CardRandom` exists in `run_rng.h`, but
`CombatState` holds no such generator yet.

- `CombatState` gains that generator. In a run it is seeded from stream
  `CardRandom`, indexed by floor like the combat stream. A standalone
  `CombatEnv` derives it from the combat seed. The exact derivation is fixed in
  the batch.
- **Infernal Blade moves onto it**, which changes later draws for any seed that
  plays Infernal Blade. Accepted: v2 breaks v1 compatibility anyway.
- Fixes task #14 in the same batch: Infernal Blade currently picks from every
  Attack id in `CARD_DATABASE`, including colorless, upgraded and rung ids. The
  correct pool is checked against the wiki then.

### D5 — Hand of Greed's gold is written back like HP (Rob, 2026-09-15)

`CombatState` records gold gained during the fight. `RunState::end_combat`
writes it back through the run's gold-gain function, so Ectoplasm ("you can no
longer gain Gold") can block it. A standalone `CombatEnv` ignores it.

---

## 5. Batches

Ordered so the cheapest cards prove the pattern first, and each piece of
machinery lands with the cards that need it.

| batch | cards | machinery |
|---|---|---|
| 1 ✅ | Thinking Ahead, Apotheosis, Violence, Dark Shackles, Panic Button, Hand of Greed | small, self-contained; D5 |
| 2 ✅ | Jack of All Trades, Transmutation, Magnetism, Discovery + Infernal Blade fix | generation; `CardRandom` (D4); cost this turn (first use of D2) |
| 3 ✅ | Madness, Enlightenment, Chrysalis, Metamorphosis | the rest of D2 |
| 4 ✅ | Secret Technique, Secret Weapon, Forethought | new choice sources |
| 5 ✅ | Panache, Sadistic Nature, Mayhem, The Bomb | power triggers; D3 |
| 6 ✅ | Purity, Forethought+ | D1 |

**After batch 6:** stock the shop's two colorless slots (`v2-spec.md` §4.3), wire
Toolbox, and update `relic-effects.md` §7.

---

## 6. Findings while implementing

### Batch 1

- **Thinking Ahead needed no new machinery at all.** `card.h` claimed it did,
  because its choice must follow its draw — but that is Warcry's shape and the
  engine already orders it that way. `sts_lightspeed` implements the card as
  literally `DrawCards(2)` then Warcry's own action. The card row was the only
  change. **A "not implemented yet" note is a claim with a shelf life**; this one
  had expired.
- **`sts_lightspeed` has Dark Shackles' Artifact check inverted.** It applies the
  Shackled give-back when the target *has* Artifact
  (`BattleContext.cpp`: `if (monsters.arr[t].hasStatus<MS::ARTIFACT>())`). The
  decompiled game and the wiki both say the opposite: no Artifact means apply
  the give-back. Following the reference here would have made Dark Shackles a
  permanent Strength *gain* for any enemy holding Artifact. Another entry for
  `prior-art-sts-lightspeed.md` §7.5.
- **A one-option choice auto-resolves** (`action.cc`, `RequestChoice`): the
  engine applies it without pausing, as StS does. Thinking Ahead hits this
  constantly — draw 2 near the bottom of the pile and one card is all there is.
  Pinned by a test, because it is easy to mistake for a missing prompt.
- **`end_combat` already pays the fight's gold reward**, so a test asserting an
  absolute total after adding kill gold measures both. The kill gold is asserted
  as the difference between two otherwise identical runs.
- **Deferred to batch 2:** Infernal Blade generates from every Attack id in
  `CARD_DATABASE` (colorless and upgraded ids included) and draws from the combat
  RNG rather than `CardRandom`. It is the same primitive batch 2 builds, so it is
  fixed there rather than twice.

### Batch 2

- **The generation pool rule is one filter, not the wiki's long list.** Each
  card's wiki page lists a dozen "excluded cards", but the game applies exactly
  one: skip cards tagged `CardTags.HEALING`, over pools that already hold only
  shop/reward-obtainable cards of your class. Eight cards carry that tag and
  three are in our vocabulary — **Feed, Reaper** (Ironclad Attacks, so Infernal
  Blade loses both) and **Bandage Up** (colorless, leaving 34 of 35). The rest of
  the wiki's list is other characters' cards and event-only cards, which are not
  in the pools at all. Verified against the decompiled game, not the wiki text.
- **Discovery draws its three WITHOUT REPLACEMENT (Rob, 2026-09-19).** StS
  re-rolls until it has three distinct ids. That has the same distribution but a
  *variable* number of draws, and a variable draw count shifts every later roll
  in the stream. Three draws without replacement gives an identical offer with a
  fixed cost, which is what keeps replay stable. **A deliberate mechanism
  divergence with no behavioural difference.**
- **"Add a card" is not "add it for free."** Jack of All Trades and Magnetism add
  at full price; only Transmutation, Discovery and Infernal Blade discount what
  they make. Easy to get wrong from the card text alone.
- **The cheapest-copy rule (D2 option A) has a sharp edge, and a test found it.**
  `instance_effective_cost` initially fell back to the id-based `effective_cost`,
  which scans the hand for a discounted copy — so an *undiscounted* Strike
  reported 0 because a *different* Strike was free, every copy looked free, and
  the cheapest-copy search picked an arbitrary one. The fix is a split: one
  helper for the type-level modifiers (Corruption, Blood for Blood), which
  neither entry point may re-enter. The migrated "only one copy is free" test
  caught it.
- **`Character::free_this_turn` is gone.** The per-card-type counter could say
  "one copy of this type is free" but never which; the instance override says it
  exactly. The observation's free-cost plane is now read off the hand's
  instances, which is also what the player sees — a 0 printed on a specific card.
- **Task #14 is closed by this batch**, as planned: Infernal Blade now rolls from
  the class Attack pool and draws from `CardRandom`.

### Batch 3

- **`Action::as_card()` silently dropped the cost override, and two tests caught
  it.** The queue carries a card's instance state as loose fields
  (`card_bonus_damage`, `card_upgrades`) and rebuilds the `Card` on the far side.
  The override was not among them, so every pile move erased it — and the hand is
  discarded through a pile move at end of turn, which meant *every this-combat
  discount expired with the turn*. The fix adds the override to the carried
  state and introduces `Action::carry(const Card&)`, so the next field added to
  `Card` is picked up by all eight call sites instead of three of them.
  **Anger's self-copy and Double Tap's replay deliberately do NOT carry it**:
  both make a fresh copy in StS, which does not inherit a discount.
- **Madness' eligibility is two-tier and the tiers are not interchangeable.**
  Normally it targets a card whose CURRENT cost is above 0; only if every card
  is already discounted does it fall back to one whose PRINTED cost is above 0.
  That second tier is why Madness still works on a card another effect made free
  this turn — and it upgrades that discount from this-turn to this-combat.
  X-cost cards are never eligible. As with Discovery, the uniform pick is made
  over the eligible set rather than by StS's re-roll loop: same distribution,
  fixed draw count.
- **Enlightenment is a CAP, not a discount.** Cards already at 0 or 1 are
  untouched, so it can never raise a Madness-discounted card back to 1, and
  X-cost cards are unaffected. The upgrade changes only the duration.
- **Chrysalis and Metamorphosis need the combat duration, not the turn one**,
  because their cards go into the DRAW pile: a this-turn discount would usually
  expire before the card was ever drawn.

### Batch 4

- **Secret Technique and Secret Weapon are MASK rules, not fizzling effects.**
  StS's `canUse` returns false when the draw pile holds no card of the required
  type, and the card greys out. So the rule lives in `is_playable` beside
  Clash's, and the mask oracle re-derives it independently. Implementing it as
  an effect that finds nothing would have let the agent burn a card — and, for
  the base versions, Exhaust it — for no result.
- **A draw-pile choice reveals WHAT is in the pile but never its ORDER.** The
  options are deduplicated by identity before being offered, so a pile holding
  three Strikes is one option, not three in draw order. That is `v2-spec.md`
  §5.10's second parity rule, and it is also what StS shows: an unordered grid.
- **Forethought's discount only marks a card whose PRINTED cost is above 0**,
  and it is spent by the play rather than expiring with a turn or a combat —
  the `UntilPlayed` duration, now exercised for the first time. StS models it
  as `freeToPlayOnce` for the same reason.
- **A test-construction trap worth recording:** "the hand is full" for a
  retrieved card means full AFTER the played card has left it. The played card
  is removed before its effect resolves, so a hand of nine plus the card being
  played has room, and an overflow test built that way silently tests nothing.

### Batch 5

- **Sadistic Nature forced a real interface change: `apply_debuff` and
  `apply_power` now report whether the effect LANDED.** StS deals no damage when
  the target's Artifact eats the debuff, and only the mutator knows — it is
  where the charge is spent. Everything else ignores the return value.
- **A negative POWER is a debuff in StS terms**, so Disarm's Strength loss
  triggers Sadistic Nature — but `Shackled` is excluded by name. That single
  exclusion is what stops Dark Shackles triggering it twice (its Strength loss
  counts, the give-back does not), and StS patched exactly that.
- **Panache's stacks are the DAMAGE, not a countdown.** A second Panache adds
  damage rather than starting a second counter, which is Combust's shape.
  The countdown lives on `Character::panache_counter`, resets every turn, and is
  decremented in the **card-played executor** rather than the power registry —
  that registry pushes actions and never mutates state.
- **Playing Panache itself counts as one of the five (Rob, 2026-09-19, from
  play).** Our ordering gives that for free — the power is applied, then the
  card-played hook fires — and StS's action order matches: `use()` queues the
  power before `UseCardAction` triggers `onUseCard`. No written source states
  it, which is why it was carried as an open question until Rob ruled; it now
  has its own test, so a reordering cannot change it silently.
- **Mayhem is Havoc as a recurring power, minus the Exhaust**, and it fires
  BEFORE the turn's draw, so it plays the card already on top. The two share one
  executor branch with a single flag between them.
- **The Bomb's blast is unmodifiable, not unblockable.** It lands at the end of
  the player's turn, before the enemies act, so block an enemy gained on its
  previous turn is still standing and absorbs part of it. That is correct, and
  it made three of my tests wrong: they expected the full 40 and, in two cases,
  negative HP on an enemy the blast killed.

### Batch 6

- **STAGING is what makes sequential picks work.** A picked card leaves the hand
  immediately and its effect waits for the finish. That is what StS's selection
  screen shows (a chosen card lifts out of the hand), and it solves the problem
  deduplicated options create: three Strikes are ONE option, so "the picked card
  leaves the offer" would wrongly remove all three. With staging, the offer is
  rebuilt from what remains and the same card stays pickable while copies last.
- **A multi-select can finish without a Decline.** When the offer empties — a
  hand of two into Forethought+ — the choice closes on its own. Two of my tests
  assumed Decline was always available and got `false` from `resolve_choice`.
- **Purity exhausts itself too**, and its own pile move is deferred until the
  choice finishes, so the exhaust pile holds the picks *and* Purity. Two more
  of my tests counted that wrongly; they now count the picked cards by id.
- ⚠️ **A `CardData` field must be APPENDED, never inserted.** `choice_max_picks`
  first went in beside `choice_copies`, in the middle of the struct — which
  shifts every positional value in the 189 original `CARD_DATABASE` rows. The
  compiler caught it this time, by a type mismatch several fields later. It
  would not always.
