#pragma once

#include <cstdint>
#include <random>
#include <unordered_map>
#include <vector>

#include "action_types.h"  // ActionQueue (the suspended mid-card queue)
#include "card.h"
#include "enemy.h"
#include "status_effect.h"

namespace minispire {

// Maximum number of enemy slots in a fight. Fixed so the observation and action
// space are fixed-size (ROB-59 / ROB-60). N = 5 covers the largest Act 1
// encounter: "Lots of Slimes" = 3 Spike-S + 2 Acid-S (ROB-66). The `enemies`
// vector is sized to this; dead enemies keep their slot (stable indices) and a
// slot is reusable only once its occupant is dead. Invariant: the count of
// *living* enemies never exceeds kMaxEnemies.
inline constexpr int kMaxEnemies = 5;

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
  // Battle Trance's "no further draws this turn" is Debuff::NoDraw, not a field
  // here (ROB-40 B2) — StS renders it as a debuff icon.
  //
  // Cards that cost 0 for the rest of THIS turn (Infernal Blade's generated
  // attack), keyed by card type and consumed one play at a time (ROB-85).
  //
  // This comment used to claim "a duplicate of the same type would be
  // indistinguishable to the player anyway". That is false and was the
  // assumption ROB-40 was opened to fix: the discounted copy displays cost 0
  // while its siblings display their printed cost, so a human tells them apart
  // at a glance. The counter is a faithful model of "exactly one copy is free"
  // but cannot say WHICH — see docs/design/observation-space.md §4.1.
  std::unordered_map<CardId, int> free_this_turn;
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

  CombatState clone() const;
};

}  // namespace minispire
