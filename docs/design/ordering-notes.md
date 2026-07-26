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

## RNG stream

Unchanged by the queue: within a card resolution, only draws consume RNG, and
their count and relative order are identical to pre-queue. Same seed, same
fight, before and after.
