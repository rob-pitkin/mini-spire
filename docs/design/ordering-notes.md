# Action-queue ordering notes

The divergence ledger required by the effects-architecture doc (§7): every
place where queue ordering differs from the pre-queue nested resolution, and
whether that difference is observable at an agent decision point. Policy
reminder: hook responses default to `push_back` (≈ StS `addToBottom`);
documented StS interactions override case-by-case.

## Stage 2 (queue under card resolution)

### 1. On-damaged responses resolve after the card's remaining actions

Pre-queue, `fire_on_damaged` applied trigger responses *immediately, between
hits*. Queued responses land at the back, after the card's pre-queued actions.

- **Curl Up vs multi-hit** — the one *observable* divergence, and a parity
  FIX: in StS, Curl Up's `GainBlockAction` is addToBottom'd during the first
  hit's damage action, so the remaining already-queued hits resolve before the
  block exists. Twin Strike on a fresh Louse now damages with both hits; the
  pre-queue engine wrongly absorbed hit 2. Pinned by
  `TurnLoop.CurlUpBlockDoesNotAbsorbRemainingMultiHits` (deliberate §8
  change).
- **Lagavulin damage-wake** — `RewriteIntent(Stunned)` / `Wake` /
  `RemoveSelfPower(Metallicize)` now execute at the end of the drain instead
  of mid-card. Unobservable: intent and Metallicize are only read at the next
  enemy phase, and both orderings finish before the decision point. Note the
  wake executes *after* later hits of a multi-hit fire their triggers, so a
  second hit re-fires the still-guarded wake effects; every response is
  idempotent (same rewrite, same erase, same flag), so the final state is
  identical. The existing Lagavulin tests all pass unchanged.
- **Angry (Mad Gremlin), split HP-threshold interrupt** — Strength stacks and
  intent rewrites are only read at the enemy phase. Unobservable.

### 2. On-death responses resolve after the card's draw

Pre-queue order was deaths (6) → draw (6c); `CheckDeath` still executes before
`DrawCards`, but the *responses* it pushes (Spore Cloud's Vulnerable) land
behind `DrawCards` in the queue. Unobservable: applying a debuff consumes no
RNG and draw doesn't read debuffs, so the decision-point state and the RNG
stream are identical. (StS itself queues the power application during the
damage action, which can likewise land behind an already-queued draw.)

### 3. Enrage's Strength gain resolves after deferred deaths and draw

Pre-queue, `OnPlayerSkill` applied at step 5b (before deaths/draw). The
`CardPlayedHook` action still *fires* at the 5b position, but its pushed
`ApplyPower` executes at the back of the queue. Unobservable: enemy Strength
is only read at the enemy phase.

## Stage 3 (enemy phase on the queue)

Granularity decision (open question 4, resolved by Rob): enemy moves
translate to **per-effect actions on the same queue as cards** — StS resolves
an enemy's damage/block/debuff applications sequentially through the one
manager (visible in-game when a move applies several debuffs or attacks and
blocks in one turn).

### 4. One enemy turn = one translate + drain

Per-enemy interleaving is preserved exactly: start-of-turn power hooks
(Ritual, then Metallicize) and the primed move's per-effect actions drain
fully before the terminal check, debuff tick, and `select_next_move` — so
enemy B still acts strictly after enemy A's move resolves and A's next intent
is sampled at the same point in the RNG stream as pre-queue. Damage computes
at execution, so the queued Ritual Strength is visible to the same turn's
attack (as before).

### 5. Protect's ally roll happens at translation time

The random living ally for a `blocks_ally` move is rolled when the move is
translated, not when the `GainBlock` executes. No RNG consumer sits between
the two points and the ally set cannot change during the actor's own move, so
this is unobservable and keeps the RNG stream byte-identical.

### 6. Wake-on-resolve responses land behind the move's actions

Pre-queue, the Sleep3 mini-drain ran after the move's applies but before
escape/split handling; queued, the OnWake responses execute after all of the
move's actions. Lagavulin neither escapes nor splits, and the affected state
(Metallicize, is_asleep) is next read at the following enemy turn.
Unobservable.

## Stage 4a (player powers via the static registry)

### 7. End of turn: hand first, then powers

StS handles the hand at end of turn (ethereal exhausts, the rest discards)
*before* end-of-turn powers resolve, so an ethereal exhaust's Feel No Pain
block is gained in time to absorb the enemy attack, and queues ahead of
Combust. Encoded as `DiscardHand` pushed before the `TurnEndPlayer` hook.
Pinned by `EtherealExhaustAtEndOfTurnTriggersFeelNoPain`. (Not directly
documented on the wiki — searched; this follows from ethereal being described
as a special end-of-turn hand step, and matches in-game behavior. Revisit if
a counterexample turns up.)

### 8. Fixed (thorns-type) damage fires only the any-damage hooks

Juggernaut / Combust / Fire Breathing / Flame Barrier damage ignores
Strength / Weak / Vulnerable but IS absorbed by block. It fires
`Hook::OnAnyDamage` (Lagavulin's wake — the wiki says "if it takes any
damage"), `EnemyHpThreshold` (the split interrupt is pure HP), and death
recording — but NOT `Hook::EnemyDamaged`, whose listeners (Curl Up, Angry)
are worded "on taking **attack** damage" in StS, mirroring the documented rule
that Thorns doesn't trigger on power damage. This split the old `OnDamaged`
trigger into `OnDamaged` (attack-only) and `OnAnyDamage`; Lagavulin's
damage-wake moved to the latter. Pinned by `FixedDamageDoesNotTriggerCurlUp`.

### 9. Rupture keys on self-inflicted HP loss only

`Hook::HpLostPlayer` fires from the `LoseHp` executor only. Enemy attack
damage goes through `DealDamage`, which deliberately does not fire it — so
Rupture never triggers on being attacked. Pinned by
`RuptureDoesNotTriggerOnEnemyDamage`.

### 10. Expiry timing: Rage vs Flame Barrier

Rage expires at the end of the player's turn (`TurnEndPlayer` →
`RemovePower`). Flame Barrier must survive the enemy phase in order to
retaliate, so it expires at the START of the next player turn
(`TurnStartPlayer`). Both pinned by tests.

### 11. Combust needs two counters

`Power::Combust` stacks hold the accumulated damage (5 or 7 per cast) while
`Character::combust_casts` counts casts for the 1-HP-per-cast loss. One count
can't express both once upgrades mix: Combust + Combust+ = lose 2 HP, deal 12.

## RNG stream

Unchanged by the queue: within a card resolution, only draws consume RNG;
within an enemy phase, only the Protect ally roll (translation-time, note 5)
and `select_next_move` (post-drain, per enemy) do — and their count and
relative order are identical to pre-queue. Same seed, same fight, before and
after.

Stage 4a adds one new RNG consumer: Juggernaut's random-enemy roll, taken at
execution time, once per trigger. It only draws when the power is in play, so
fights without Juggernaut have an unchanged stream.
