#pragma once

namespace minispire {

// StS splits persistent effects into two mechanically-distinct categories
// (ROB-78). Debuffs decrement at end of turn; Powers persist and never
// self-decrement. Keeping them as separate types makes "does this tick" a
// property of the type, not a maintained denylist.

// Debuffs — decrement by 1 at end of the bearer's turn, expire at 0. `None` is a
// sentinel default (not tracked in the obs); keep the real values first so their
// enumerator order matches kObsDebuffOrder.
enum class Debuff {
  Vulnerable,  // takes 50% more attack damage; ticks down
  Weak,        // deals 25% less attack damage; ticks down
  Frail,       // block gained from cards reduced 25% (floored); ticks down
  Entangle,    // player: cannot play attack cards this turn; non-stacking, 1 turn
  NoDraw,      // player: no further draws this turn (Battle Trance). StS renders
               // it as a debuff icon, so it lives here rather than as a hidden
               // bool (ROB-40 B2). Set to 1 when played; the end-of-turn tick
               // clears it, which is exactly "this turn" — the tick runs AFTER
               // the end-of-turn drain, so it still blocks a Dark Embrace draw
               // from an ethereal exhaust.
  NoBlock,     // player: block gained FROM CARDS is zeroed (Panic Button). Only
               // card block: Entrench, Metallicize, Plated Armor and relics are
               // unaffected, which falls out of Action::card_block already
               // marking exactly the card sources. A Debuff rather than a Power
               // because StS types it as one — so it ticks down (2 turns,
               // counting the turn it is played) and the player's Artifact
               // negates it.
  None,        // sentinel: "no debuff" (default for unused fields)
};

// Powers — persistent; never self-decrement (removed only by specific effects).
// `None` is a sentinel default (not tracked in the obs).
//
// Layout contract: the first kNumEnemyPowers values are the enemy-relevant
// powers (the enemy obs block encodes exactly these, in this order); the
// player obs block encodes ALL real values in enum order (ROB Stage 4a
// decision: per-entity order lists). Append new player powers before None.
enum class Power {
  Strength,     // +X attack damage per hit (may be negative)
  Dexterity,    // +X block from cards (may be negative)
  Ritual,       // enemy: gain Strength = stacks at the start of each of its turns
  Metallicize,  // gain block = stacks (enemy: start of its turn; player: end of turn)
  Enrage,       // enemy: gain Strength = stacks whenever the player plays a Skill
  Artifact,     // enemy: negates the next `stacks` debuff applications, then decrements
  Shackled,     // enemy: at the END of its turn, gain `stacks` Strength and
                // remove self. StS's "Shackled" (GainStrengthPower), the other
                // half of Dark Shackles: the Strength loss is permanent on its
                // own, and this is what gives it back. The player's mirror
                // image is StrengthDown (Flex), which LOSES at end of turn —
                // opposite sign, so it cannot be reused here.
  // --- Player powers (Tier C, effects-architecture Stage 4a). Behavior lives
  // in the static registry fire_player_power_hooks (action.cc). ---
  DemonForm,     // turn start: gain `stacks` Strength
  Combust,       // turn end: lose 1 HP per cast (Character::combust_casts) and
                 // deal `stacks` fixed damage to ALL enemies
  FeelNoPain,    // whenever a card is exhausted: gain `stacks` block
  DarkEmbrace,   // whenever a card is exhausted: draw `stacks` cards
  Evolve,        // whenever a Status card is drawn: draw `stacks` cards
  FireBreathing, // whenever a Status/Curse is drawn: `stacks` fixed dmg to all
  Rupture,       // whenever HP is lost from a card/power: gain `stacks` Strength
  Juggernaut,    // whenever block is gained: `stacks` fixed dmg to a random enemy
  Rage,          // this turn, whenever an Attack is played: gain `stacks` block;
                 // removed at end of turn
  FlameBarrier,  // this turn, whenever attacked: `stacks` fixed dmg back;
                 // removed at the start of the next turn
  Brutality,     // turn start: lose `stacks` HP, draw `stacks` cards
  Berserk,       // turn start: gain `stacks` energy
  // --- Query-layer powers (Stage 4b): consulted, never fired. Behavior lives
  // in query.cc, not in a hook registry. ---
  Corruption,    // Skills cost 0 and exhaust when played
  Barricade,     // block is not removed at the start of your turn
  DoubleTap,     // this turn, the next `stacks` Attacks are played twice
  // --- Next-attack modifiers. Both are read inside compute_attack_damage and
  // CONSUMED when the attack card finishes resolving, not per hit: a multi-hit
  // card gets the benefit on every hit and spends the power once. ---
  Vigor,         // next Attack deals `stacks` additional damage PER HIT.
                 // Additive with Strength, so Weak and Vulnerable scale it too.
                 // Akabeko grants 8 at combat start.
  Thorns,        // when receiving ATTACK damage, deal `stacks` back to the
                 // attacker. Fires even when the attack is fully blocked, like
                 // Flame Barrier — it keys on being attacked, not on being
                 // hurt. Permanent: unlike Flame Barrier it does not expire.
                 // The retaliation is FIXED damage, unscaled by Strength or
                 // Vulnerable.
  PlatedArmor,   // end of turn: gain `stacks` Block, NOT modified by Dexterity
                 // or Frail. Loses one stack on receiving unblocked damage.
  Intangible,    // reduce ALL incoming damage and HP loss to 1.
                 //
                 // THE ONE POWER THAT TICKS (Rob, 2026-09-12). The rule below
                 // — powers never decrement, decrement-ness is the TYPE — holds
                 // for everything else; Intangible is a duration buff in StS
                 // and loses a stack at the end of the player's turn. It is not
                 // modelled as a Debuff because it is beneficial: putting it in
                 // the debuff map would show it in the wrong observation block
                 // and route it through Artifact, which negates debuffs.
                 //
                 // The exception is narrow and named, not a general denylist.
                 // Flame Barrier and Rage are NOT counterexamples: they are
                 // one-turn flags removed wholesale at a named hook.
  Buffer,        // prevent the next `stacks` times you would LOSE HP. A
                 // COUNTER, not a duration: it does not tick, and a stack is
                 // spent only when HP would actually be lost — a fully blocked
                 // hit or a 0-damage attack spends nothing.
  PenNibCharge,  // next Attack deals DOUBLE damage, on every hit of that card.
                 // Named for the relic that grants it (Pen Nib, every 10th
                 // Attack) rather than "Double Damage", because the relic's
                 // counter and this charge are one mechanism.
  Magnetism,     // turn start: add `stacks` random COLORLESS cards to hand, at
                 // full price. Stacks intensify (two Magnetisms make two cards
                 // a turn), which is why the count is the stack value.
  Panache,       // every 5th card played IN A TURN: `stacks` damage to all
                 // enemies. Stacks are the DAMAGE (a second Panache adds to it
                 // rather than starting a second countdown); the countdown
                 // itself is Character::panache_counter, reset each turn.
  SadisticNature,// whenever the player applies a debuff to an enemy AND it
                 // lands, that enemy takes `stacks` fixed damage. Artifact
                 // negating the debuff means no damage.
  Mayhem,        // turn start, BEFORE the draw: play the top card of the draw
                 // pile, `stacks` times. Unlike Havoc it does NOT exhaust the
                 // card it plays.
  // --- Turn-scoped bookkeeping power. ---
  StrengthDown,  // turn end: lose `stacks` Strength, then remove self. StS
                 // models temporary Strength (Flex) as a Strength gain paired
                 // with an equal Strength Down, rather than as a Debuff — the
                 // loss is a fixed amount at end of turn, NOT a 1/turn tick,
                 // so it cannot be expressed with the Debuff vocabulary.
  NextTurnBlock, // at the START of the next turn: gain `stacks` Block, then
                 // remove self. Self-Forming Clay banks 3 here per HP loss and
                 // they STACK within a turn (wiki), which is why the amount is
                 // the stack value. Not card block: Dexterity and Frail do not
                 // apply. StS models it the same way, as NextTurnBlockPower.
  None,          // sentinel: "no power" (default for unused fields)
};

// Per-entity obs block widths (combat_env). Each block is [debuffs then
// powers]; the player block lists every power, the enemy block only the
// enemy-relevant prefix (Stage 4a). Keep kObsDebuffOrder /
// kObsPlayerPowerOrder / kObsEnemyPowerOrder in lockstep — static_asserts
// enforce the counts match. (The None sentinels are excluded.)
// Entangle and NoDraw are player-only; they occupy always-zero floats in the
// enemy blocks. Kept in one shared order (rather than split per entity like the
// powers) because two of five is not worth a second table.
inline constexpr int kNumDebuffs = 6;
inline constexpr int kNumEnemyPowers = 7;
inline constexpr int kNumPlayerPowers = 34;

// FUTURE (multi-enemy): Target { Character, Enemy } collapses any "the enemy"
// to a single entity, which is unambiguous in v1 with one enemy. Multi-enemy
// fights need richer targeting (Target::AllEnemies for Cleave/Whirlwind,
// Target::EnemyIndex(n) for specific targeting). The current enum will need
// to grow or be replaced by a small variant. See ROB-34 design doc.
enum class Target { Character, Enemy };

// A debuff/power applied by a card or move, to the player or an enemy.
struct DebuffApplication {
  Debuff effect;
  int amount;
  Target target;
};

struct PowerApplication {
  Power effect;
  int amount;  // may be negative (e.g. Siphon Soul -1 Strength)
  Target target;
};

}  // namespace minispire
