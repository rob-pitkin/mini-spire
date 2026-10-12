# When the player's debuffs and Intangible tick — design (task #39)

**Status:** agreed with Rob, 2026-10-10: Q1 adopt StS's timing, Q2 leave the
just-applied flag out of the observation, Q3 the behaviour changes are wanted.
Built in the commit that adds this doc.

The engine ticks every player debuff, and Intangible, at the end of the PLAYER's
turn (`handle_end_turn` step 1b). StS ticks most of them at the end of the ROUND,
after the enemies have acted, and skips one tick for a debuff an enemy has just
applied. The difference shows during the enemy phase, so it hits exactly the
statuses that matter there: Vulnerable on the player, and Intangible.

## 0. Sources

Decompiled StS1 (`aeubanks/sts`): `powers/VulnerablePower`, `WeakPower`,
`FrailPower`, `NoBlockPower`, `EntanglePower`, `NoDrawPower`,
`IntangiblePlayerPower`, `ConfusionPower`; `monsters/MonsterGroup.applyEndOfTurnPowers`;
`actions/common/ApplyPowerAction`. Verified with constructed probes (since
deleted): under Intangible 1, a Jaw Worm's Chomp took 11 HP, not 1; a player
Vulnerable 2 lasted one enemy phase.

## 1. What StS does

**Which hook each status uses:**

| status | hook | timing |
|---|---|---|
| Vulnerable, Weak, Frail, No Block | `atEndOfRound` | after every enemy has acted (`MonsterGroup.applyEndOfTurnPowers`, which runs the player's powers' `atEndOfRound`) |
| Intangible (player), Duplication | `atEndOfRound` | the same |
| Entangle, No Draw | `atEndOfTurn(isPlayer)` | removed at the end of the player's turn |
| Regen, Strength Down, Dexterity Down | `atEndOfTurn` | end of the player's turn |
| Confusion | none | never |

**Just applied.** Weak, Frail, Vulnerable and No Block each carry a
`justApplied` flag. While it is set, the next `atEndOfRound` clears it instead
of reducing the stack. The constructors set it differently:
- Weak and Frail: whenever a monster applies them (`isSourceMonster`).
- Vulnerable and No Block: only when a monster applies them after the player's
  turn has ended (`turnHasEnded && isSourceMonster`).

`ApplyPowerAction` stacks onto an existing power with `stackPower` and discards
the new object, so the flag is set only when the debuff was absent.

## 2. What changes in play

| case | engine today | StS |
|---|---|---|
| Incense Burner's Intangible 1 (turn start) | gone before the enemy phase | protects that enemy phase |
| Gremlin Nob's Skull Bash, Vulnerable 2 (enemy turn N) | +50% in enemy phase N+1 only | enemy phases N+1 and N+2 |
| Fungi Beast's Spore Cloud, Vulnerable 2 (dies on the player's turn N) | enemy phase N only | enemy phases N and N+1 |
| Weak / Frail from an enemy | — | unchanged: the skipped tick and the later tick cancel out over player turns |
| Panic Button's No Block 2 | — | unchanged: it only matters on player turns |
| Entangle, No Draw, Regen | — | unchanged |

## 3. Proposed change

1. **End of the player's turn (step 1b):** tick only Entangle and No Draw among
   the debuffs, and Regen among the powers. These are unchanged.
2. **A new end-of-round step,** after the enemy phase and before the next turn
   starts. It ticks Vulnerable, Weak, Frail and No Block, then Intangible, then
   Duplication. Duplication moves only to sit with its StS hook; no card is
   played in the enemy phase, so the move cannot be seen.
3. **Just applied:** a per-debuff flag on the player, set by `apply_debuff` when
   a monster applies a debuff the player does not already hold. Weak and Frail
   always set it; Vulnerable and No Block set it only outside the player's turn.
   The end-of-round tick clears a set flag instead of decrementing. Enemy-sourced
   applications must record their source, so enemy moves and triggered effects
   (Spore Cloud) set `Action::actor`, which today they leave unset.
4. **Enemy debuffs are unchanged.** The engine ticks an enemy's debuffs at the
   end of that enemy's own turn; StS ticks them at the end of the round. Nothing
   that happens in between reads them, and the player never applies a debuff
   from a monster's side, so no `justApplied` arises there.

## 4. Questions for Rob

**Q1.** Adopt StS's timing as §3 describes? Recommend yes. Vulnerable lasting one
enemy phase too few, and Intangible that never protects, both change the value
the agent learns for real cards and relics.

**Q2. The just-applied flag is not observed.** Recommend leaving it out of the
observation. In Act 1 it is never set at a decision point:
- Vulnerable and No Block set it only on the enemy turn, and that round's end
  clears it.
- Weak and Frail would set it on the player's turn only through an enemy effect
  that fires then, and no Act 1 enemy has one. Spore Cloud applies Vulnerable.

A test pins this: no flag is set at any decision point across a sweep of fights.
If a boss or Act 2 enemy breaks it, that test fails.

**Q3. Behaviour changes** for Skull Bash, Spore Cloud and Incense Burner, as in
§2. Confirm these are wanted.

## 5. Tests

`tests/test_debuff_timing.cc`. Each test measures the damage taken in the enemy
phases, which is where the timing shows; counts at decision points match the
old timing.
- Intangible 1 holds an enemy's Chomp to 1. Incense Burner's grant does the same
  in the turn it fires.
- Skull Bash's Vulnerable 2 (enemy turn, just applied) covers the next two enemy
  phases.
- Spore Cloud's Vulnerable 2 on the player's turn covers that phase and the
  next. A Fungi Beast that dies to Thorns on the enemy turn applies it just
  applied: the enemy acting after it in that phase and the next two phases.
- Skull Bash stacking onto a held Vulnerable 1 keeps the held one's tick.
- Weak from Lick still lasts its count.
- No just-applied flag is set at any decision point, across 150 seeds of random
  fights in every pool, with a floor on how often the player carries an
  enemy-applied debuff.

Mutation checks, all caught:
- the end-of-round ticks moved back to the end of the turn;
- the flag never set;
- Vulnerable flagged on the player's turn;
- the flag set on a stack;
- enemy moves without a source;
- triggered effects without a source.

The sweep also caught Vulnerable being flagged on the player's turn, so it is
not vacuous. Three existing enemy tests (Spit Web, Lick, Rake) caught the
missing-flag mutation as well: they pin Weak and Frail at their count after an
enemy's move.
