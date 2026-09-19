#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <unordered_map>
#include <vector>

#include "action_types.h"  // ActionQueue (the suspended mid-card queue)
#include "card.h"
#include "enemy.h"
#include "potion.h"
#include "relic.h"
#include "status_effect.h"

namespace minispire {

// Maximum number of enemy slots in a fight. Fixed so the observation and action
// space are fixed-size (ROB-59 / ROB-60). N = 5 covers the largest Act 1
// encounter: "Lots of Slimes" = 3 Spike-S + 2 Acid-S (ROB-66). The `enemies`
// vector is sized to this; dead enemies keep their slot (stable indices) and a
// slot is reusable only once its occupant is dead. Invariant: the count of
// *living* enemies never exceeds kMaxEnemies.
inline constexpr int kMaxEnemies = 5;

// Panache fires on every 5th card played in a turn.
inline constexpr int kPanacheCardsPerTrigger = 5;

// The Bomb counts down 3 turns, including the turn it is played. Every Bomb
// starts at the same number, which is what lets any number of them be tracked
// as three slots (docs/design/colorless-effects.md D3).
inline constexpr int kBombFuseTurns = 3;

enum class Outcome {
  InProgress,
  Won,
  Lost,
};

struct Character {
  int hp;
  int max_hp;
  int energy;
  int energy_per_turn;
  int current_block;
  std::unordered_map<Debuff, int> debuffs;
  std::unordered_map<Power, int> powers;
  // Combust's second counter (Stage 4a): HP lost at end of turn = casts, while
  // Power::Combust stacks = accumulated damage (5/7 per cast — one stack count
  // can't hold both once upgrades mix). Bumped by the ApplyPower executor.
  int combust_casts = 0;
  // --- Query-layer counters (Stage 4b). Read through query.cc rather than
  // applied as powers.
  //
  // These were originally excluded from the observation "because they are not
  // shown as power icons in StS". ROB-40's parity audit overturned that: the
  // premise confused REPRESENTATION with VISIBILITY. The player does see both —
  // Blood for Blood's card displays its reduced cost, and Combust's tooltip
  // displays the HP it will cost — just not as an icon. Both are now written to
  // the observation (ROB-40 B1). ---
  //
  // Blood for Blood: cost drops 1 per HP-loss EVENT this combat, from ANY
  // source including enemy attacks (unlike Rupture, which is self-inflicted
  // only). Combat-scoped — never reset per turn.
  int hp_loss_events = 0;
  // Panache's countdown: cards still to be played before it fires. Held here
  // rather than in the power's stacks because those are the DAMAGE (a second
  // Panache adds damage, never a second countdown). Reset to 5 at turn start,
  // so four cards this turn and one next turn never trigger it.
  int panache_counter = kPanacheCardsPerTrigger;
  // The Bomb, by turns remaining (index 0 = fires at the end of THIS turn).
  // Two arrays because each Bomb fires as its OWN all-enemy hit in StS, and a
  // Bomb and a Bomb+ in the same slot deal different damage; summing them into
  // one number would merge two hits into one. Bounded because every Bomb starts
  // at the same 3 turns, so any number of them collapses into three slots.
  std::array<int, kBombFuseTurns> bombs{};
  std::array<int, kBombFuseTurns> bombs_upgraded{};
  // Battle Trance's "no further draws this turn" is Debuff::NoDraw, not a field
  // here (ROB-40 B2) — StS renders it as a debuff icon.
  //
  // "This card costs 0" is NOT tracked here. It was, as a per-card-type counter
  // (`free_this_turn`), which could say "one copy of this type is free" but
  // never WHICH copy. Since colorless-effects.md D2 the discount lives on the
  // Card INSTANCE (`cost_override` + `cost_duration`), because Madness
  // discounts one copy and Enlightenment only the cards in hand right now —
  // neither is expressible per type.
};

struct CombatState {
  Character character;
  std::vector<Enemy> enemies;
  std::vector<Card> current_hand;
  std::vector<Card> discard_pile;
  std::vector<Card> draw_pile;
  std::vector<Card> exhaust_pile;
  int turn_number;
  bool character_turn;
  Outcome outcome;
  std::mt19937 rng;
  uint32_t seed;

  // In-combat card GENERATION draws from its own generator, never from `rng`
  // (v2-spec.md §3.5, RngStream::CardRandom). StS keeps `cardRandomRng`
  // separate for a reason a player can observe: because almost nothing advances
  // it, an Attack Potion offers the same cards whether drunk on turn 1 or turn
  // 5 — but playing an Infernal Blade first DOES change them. Sharing the
  // combat stream would break both halves of that.
  std::mt19937 card_rng;

  // Suspended mid-card choice (Stage 4c). `pending_choice.active()` means the
  // drain stopped to await the agent; the not-yet-executed actions live in
  // `suspended_queue` and resume when resolve_choice() answers.
  //
  // This is the ONLY queue state that ever persists across step(). Both members
  // are fixed-size PODs (no heap), so clone() remains a plain deep copy and
  // MCTS can branch on a paused state — verified by a pause -> clone -> resume
  // round-trip test.
  //
  // SIZE TRADEOFF: suspended_queue is ~5 KB (128 x 40 B) and is empty except
  // during a paused choice, yet clone() copies it every time. Measured cost:
  // clone() is 0.89 us / 1.1M per second, and engine throughput is unchanged,
  // so this is affordable. If MCTS ever makes it hurt, the fix is cheap and
  // local — shrink the capacity (a suspended card's remainder is a handful of
  // actions, nowhere near 128) or store the remainder in a smaller dedicated
  // buffer rather than reusing ActionQueue.
  PendingChoice pending_choice;
  ActionQueue suspended_queue;

  // Relics and potions are FIRST-CLASS combat state, not run-layer hooks
  // reaching in (v2-spec.md §3.0.1). A player sees their relic bar and potion
  // belt during a fight and can drink mid-combat, so §1's parity rule puts both
  // here — and it keeps a standalone CombatEnv complete rather than a fragment
  // that only makes sense inside a run.
  //
  // Both default empty, so a CombatEnv built the v1.0.0 way is unchanged.
  //
  // RunState owns these ACROSS fights and projects them in; within a fight this
  // is where they live. Relic counters are one run-scoped int each, written
  // back on exit with nothing resetting at the boundary (§3.3).
  std::vector<HeldRelic> relics;

  // Is this an elite (or boss) fight? Several relics are conditional on it —
  // Sling of Courage's Strength, Preserved Insect's HP cut, Slaver's Collar's
  // energy — and the hooks fire from inside the fight, where CombatSetup's pool
  // is long out of scope.
  //
  // Not derived from the encounter's contents: an elite roster is a fact about
  // which POOL was sampled, and Act 1's pools can share enemy kinds. A human
  // knows which kind of room they walked into, so this is state the fight
  // carries rather than something to infer.
  bool is_elite = false;
  std::vector<PotionId> potions;

  // Gold earned during THIS fight (Hand of Greed's "if Fatal, gain 20 Gold").
  // Gold itself belongs to RunState; combat only records what it earned, and
  // RunState::end_combat writes it back through the run's gold-gain path so
  // Ectoplasm can refuse it (colorless-effects.md D5). A standalone CombatEnv
  // simply never reads this, which is why the channel is a counter rather than
  // a reach into the run layer.
  int gold_gained = 0;

  bool has_relic(RelicId id) const {
    for (const HeldRelic& r : relics) {
      if (r.id == id) return true;
    }
    return false;
  }

  CombatState clone() const;
};

}  // namespace minispire
