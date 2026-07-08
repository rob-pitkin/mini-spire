#include "enemy.h"

#include <cassert>
#include <cmath>
#include <random>

namespace minispire {

namespace {

// Largest consecutive count present in the table for a given last-move.
// Used to clamp consecutive_count during lookup so states beyond the table's
// resolution still resolve (defensive — should not be reached in practice
// when the table's banned-move structure is consistent).
int max_consecutive_for(
    const std::unordered_map<TransitionKey, std::vector<MoveTransition>>& transitions,
    MoveName last) {
  int max_c = 1;
  for (const auto& [key, _] : transitions) {
    if (key.last == last && key.consecutive > max_c) {
      max_c = key.consecutive;
    }
  }
  return max_c;
}

MoveName sample_from_distribution(const std::vector<MoveTransition>& dist,
                                  std::mt19937& rng) {
  std::uniform_real_distribution<float> uniform(0.0f, 1.0f);
  float roll = uniform(rng);
  float cumulative = 0.0f;
  for (const auto& transition : dist) {
    cumulative += transition.probability;
    if (roll < cumulative) {
      return transition.next_move;
    }
  }
  // Float rounding may leave us past the cumulative — return the last entry.
  return dist.back().next_move;
}

}  // namespace

MoveName select_next_move(Enemy& enemy, std::mt19937& rng) {
  // Turn 1: fire first_turn_move if no prior move exists.
  if (!enemy.last_move.has_value()) {
    assert(enemy.first_turn_move.has_value() &&
           "Enemy must have either a prior move or a first_turn_move");
    MoveName chosen = *enemy.first_turn_move;
    enemy.last_move = chosen;
    enemy.consecutive_count = 1;
    return chosen;
  }

  // Subsequent turns: clamp consecutive_count to the table's resolution.
  int max_c = max_consecutive_for(enemy.transitions, *enemy.last_move);
  int clamped = enemy.consecutive_count < max_c ? enemy.consecutive_count : max_c;

  auto it = enemy.transitions.find({*enemy.last_move, clamped});
  assert(it != enemy.transitions.end() &&
         "Transition table missing entry for (last_move, clamped_consecutive)");

  MoveName chosen = sample_from_distribution(it->second, rng);

  if (chosen == *enemy.last_move) {
    enemy.consecutive_count += 1;
  } else {
    enemy.last_move = chosen;
    enemy.consecutive_count = 1;
  }
  return chosen;
}

Enemy make_jaw_worm(std::mt19937& rng) {
  Enemy e;
  e.kind = EnemyKind::JawWorm;

  std::uniform_int_distribution<int> hp_roll(40, 44);
  e.max_hp = hp_roll(rng);
  e.hp = e.max_hp;
  e.current_block = 0;

  e.moves = {
      {MoveName::Chomp, {MoveName::Chomp, 11, 0, {}}},
      {MoveName::Thrash, {MoveName::Thrash, 7, 5, {}}},
      {MoveName::Bellow,
       {MoveName::Bellow, 0, 6, /*debuffs=*/{},
        {{Power::Strength, 3, Target::Enemy}}}},
  };

  e.first_turn_move = MoveName::Chomp;

  e.transitions = {
      {{MoveName::Chomp, 1},
       {{MoveName::Thrash, 0.41f}, {MoveName::Bellow, 0.59f}}},
      {{MoveName::Bellow, 1},
       {{MoveName::Chomp, 0.44f}, {MoveName::Thrash, 0.56f}}},
      {{MoveName::Thrash, 1},
       {{MoveName::Chomp, 0.25f},
        {MoveName::Thrash, 0.30f},
        {MoveName::Bellow, 0.45f}}},
      {{MoveName::Thrash, 2},
       {{MoveName::Chomp, 0.36f}, {MoveName::Bellow, 0.64f}}},
  };

  e.last_move = std::nullopt;
  e.consecutive_count = 0;

  // Validate: every entry's probabilities sum to 1.0 within epsilon.
  for (const auto& [key, dist] : e.transitions) {
    float sum = 0.0f;
    for (const auto& t : dist) sum += t.probability;
    (void)sum;
    assert(std::abs(sum - 1.0f) < 1e-4f &&
           "Transition probabilities must sum to 1.0");
  }

  // Prime the intent. The factory is responsible for leaving the enemy in a
  // "ready to fight" state where last_move stores the upcoming intent and
  // the agent's observation can read it. After this call, last_move is set
  // to first_turn_move and consecutive_count is 1.
  select_next_move(e, rng);

  return e;
}

Enemy make_cultist(std::mt19937& rng) {
  Enemy e;
  e.kind = EnemyKind::Cultist;

  std::uniform_int_distribution<int> hp_roll(48, 54);
  e.max_hp = hp_roll(rng);
  e.hp = e.max_hp;
  e.current_block = 0;

  e.moves = {
      // Incantation: gain Ritual 3 (self-buff via Target::Enemy). Ritual grants
      // Strength at the start of each subsequent turn (ROB-73), so the first
      // Dark Strike (turn 2) already hits for 6 + 3 = 9.
      {MoveName::Incantation,
       {MoveName::Incantation, 0, 0, /*debuffs=*/{},
        {{Power::Ritual, 3, Target::Enemy}}}},
      {MoveName::DarkStrike, {MoveName::DarkStrike, 6, 0, {}}},
  };

  e.first_turn_move = MoveName::Incantation;

  // Deterministic AI: Incantation once (turn 1), then Dark Strike forever.
  // The (DarkStrike, 1) entry covers all repeat counts — consecutive_count is
  // clamped to the table's max (1) in select_next_move.
  e.transitions = {
      {{MoveName::Incantation, 1}, {{MoveName::DarkStrike, 1.0f}}},
      {{MoveName::DarkStrike, 1}, {{MoveName::DarkStrike, 1.0f}}},
  };

  e.last_move = std::nullopt;
  e.consecutive_count = 0;

  for (const auto& [key, dist] : e.transitions) {
    float sum = 0.0f;
    for (const auto& t : dist) sum += t.probability;
    (void)sum;
    assert(std::abs(sum - 1.0f) < 1e-4f &&
           "Transition probabilities must sum to 1.0");
  }

  select_next_move(e, rng);

  return e;
}

namespace {

// Shared Louse factory (ROB-63). Red and Green Louse are identical except for
// HP range and their non-Bite move (Grow vs SpitWeb). Bite damage is rolled
// once per fight (5-7); Curl Up block is rolled once (3-7). AI: 75% Bite /
// 25% other, no move three times in a row (forced switch at consecutive 2).
Enemy make_louse(std::mt19937& rng, EnemyKind kind, int hp_lo, int hp_hi,
                 MoveName other_move, const Move& other_move_data) {
  Enemy e;
  e.kind = kind;

  std::uniform_int_distribution<int> hp_roll(hp_lo, hp_hi);
  e.max_hp = hp_roll(rng);
  e.hp = e.max_hp;
  e.current_block = 0;

  // Bite damage fixed for the whole fight.
  std::uniform_int_distribution<int> bite_roll(5, 7);
  int bite_damage = bite_roll(rng);

  e.moves = {
      {MoveName::Bite, {MoveName::Bite, bite_damage, 0, {}}},
      {other_move, other_move_data},
  };

  // Curl Up: gain X block (rolled 3-7) on the FIRST damage taken (ROB-62 ->
  // ROB-65 triggered effect: once=true latches it off after the first hit).
  std::uniform_int_distribution<int> curl_roll(3, 7);
  e.triggered_effects.push_back({.trigger = Trigger::OnDamaged,
                                 .action = TriggeredAction::GainBlock,
                                 .amount = curl_roll(rng),
                                 .once = true});

  // Transition table: 75% Bite / 25% other when both legal; forced switch when
  // a move has been used twice in a row (no three-in-a-row).
  const std::vector<MoveTransition> base = {{MoveName::Bite, 0.75f},
                                            {other_move, 0.25f}};
  e.transitions = {
      {{MoveName::Bite, 1}, base},
      {{MoveName::Bite, 2}, {{other_move, 1.0f}}},
      {{other_move, 1}, base},
      {{other_move, 2}, {{MoveName::Bite, 1.0f}}},
  };

  // Turn 1 is the same base 75/25 roll (no fixed opener). We roll it here and
  // prime the queued intent directly: `last_move` holds the *upcoming* move
  // (see its doc on Enemy), so this sets the turn-1 intent the obs displays.
  // first_turn_move stays unset — select_next_move is never reached with an
  // unprimed Louse.
  e.first_turn_move = std::nullopt;
  e.last_move = sample_from_distribution(base, rng);
  e.consecutive_count = 1;

  for (const auto& [key, dist] : e.transitions) {
    float sum = 0.0f;
    for (const auto& t : dist) sum += t.probability;
    (void)sum;
    assert(std::abs(sum - 1.0f) < 1e-4f &&
           "Transition probabilities must sum to 1.0");
  }

  return e;
}

}  // namespace

Enemy make_red_louse(std::mt19937& rng) {
  // Grow: gain 3 Strength (self-buff via Target::Enemy).
  Move grow{MoveName::Grow, 0, 0, /*debuffs=*/{},
            {{Power::Strength, 3, Target::Enemy}}};
  return make_louse(rng, EnemyKind::RedLouse, 10, 15, MoveName::Grow, grow);
}

Enemy make_green_louse(std::mt19937& rng) {
  // SpitWeb: apply 2 Weak to the player.
  Move spit{MoveName::SpitWeb, 0, 0,
            {{Debuff::Weak, 2, Target::Character}}};
  return make_louse(rng, EnemyKind::GreenLouse, 11, 17, MoveName::SpitWeb, spit);
}

namespace {

// Assert every transition row sums to 1.0 (within epsilon). Called by the slime
// factories below.
void validate_transitions(const Enemy& e) {
  for (const auto& [key, dist] : e.transitions) {
    float sum = 0.0f;
    for (const auto& t : dist) sum += t.probability;
    (void)sum;
    assert(std::abs(sum - 1.0f) < 1e-4f &&
           "Transition probabilities must sum to 1.0");
  }
}

}  // namespace

Enemy make_acid_slime_s(std::mt19937& rng) {
  // Acid Slime (S): Tackle (3 dmg) / Lick (1 Weak). AI: turn 1 is a 50/50 roll,
  // then strict alternation (never repeat a move).
  Enemy e;
  e.kind = EnemyKind::AcidSlimeS;
  std::uniform_int_distribution<int> hp_roll(8, 12);
  e.max_hp = hp_roll(rng);
  e.hp = e.max_hp;
  e.current_block = 0;

  e.moves = {
      {MoveName::Tackle, {MoveName::Tackle, 3, 0, {}}},
      {MoveName::Lick,
       {MoveName::Lick, 0, 0,
        {{Debuff::Weak, 1, Target::Character}}}},
  };

  // Strict alternation: each move forces the other next.
  e.transitions = {
      {{MoveName::Tackle, 1}, {{MoveName::Lick, 1.0f}}},
      {{MoveName::Lick, 1}, {{MoveName::Tackle, 1.0f}}},
  };

  // Turn 1: base 50/50 roll (no fixed opener).
  const std::vector<MoveTransition> first{{MoveName::Tackle, 0.5f},
                                          {MoveName::Lick, 0.5f}};
  e.first_turn_move = std::nullopt;
  e.last_move = sample_from_distribution(first, rng);
  e.consecutive_count = 1;

  validate_transitions(e);
  return e;
}

Enemy make_acid_slime_m(std::mt19937& rng) {
  // Acid Slime (M): Tackle (10) / Lick (1 Weak) / Corrosive Spit (7 + 1 Slimed).
  // Base 40/30/30. Tackle and Lick can't repeat (banned after 1); Corrosive
  // Spit can't be used 3x (banned after 2). Banned-move probability is
  // redistributed over the legal moves (renormalized).
  Enemy e;
  e.kind = EnemyKind::AcidSlimeM;
  std::uniform_int_distribution<int> hp_roll(28, 32);
  e.max_hp = hp_roll(rng);
  e.hp = e.max_hp;
  e.current_block = 0;

  e.moves = {
      {MoveName::Tackle, {MoveName::Tackle, 10, 0, {}}},
      {MoveName::Lick,
       {MoveName::Lick, 0, 0,
        {{Debuff::Weak, 1, Target::Character}}}},
      {MoveName::CorrosiveSpit,
       {MoveName::CorrosiveSpit, 7, 0, {}, {}, {CardId::Slimed}}},
  };

  // Renormalized distributions per (last, consecutive). Fractions written as
  // n/d so each row sums to exactly 1.0.
  e.transitions = {
      // After Tackle (banned): redistribute T's 0.40 over Lick/Spit (each 0.30
      // of the remaining 0.60) -> 0.5 / 0.5.
      {{MoveName::Tackle, 1}, {{MoveName::Lick, 0.5f}, {MoveName::CorrosiveSpit, 0.5f}}},
      // After Lick (banned): Tackle/Spit over remaining 0.70 -> 4/7, 3/7.
      {{MoveName::Lick, 1},
       {{MoveName::Tackle, 4.0f / 7.0f}, {MoveName::CorrosiveSpit, 3.0f / 7.0f}}},
      // After Spit once (still legal): full base 40/30/30.
      {{MoveName::CorrosiveSpit, 1},
       {{MoveName::Tackle, 0.4f}, {MoveName::Lick, 0.3f},
        {MoveName::CorrosiveSpit, 0.3f}}},
      // After Spit twice (banned): Tackle/Lick over remaining 0.70 -> 4/7, 3/7.
      {{MoveName::CorrosiveSpit, 2},
       {{MoveName::Tackle, 4.0f / 7.0f}, {MoveName::Lick, 3.0f / 7.0f}}},
  };

  // Turn 1: full base 40/30/30 roll.
  const std::vector<MoveTransition> first{{MoveName::Tackle, 0.4f},
                                          {MoveName::Lick, 0.3f},
                                          {MoveName::CorrosiveSpit, 0.3f}};
  e.first_turn_move = std::nullopt;
  e.last_move = sample_from_distribution(first, rng);
  e.consecutive_count = 1;

  validate_transitions(e);
  return e;
}

Enemy make_spike_slime_s(std::mt19937& rng) {
  // Spike Slime (S): Tackle (5 dmg), 100% of the time. Deterministic.
  Enemy e;
  e.kind = EnemyKind::SpikeSlimeS;
  std::uniform_int_distribution<int> hp_roll(10, 14);
  e.max_hp = hp_roll(rng);
  e.hp = e.max_hp;
  e.current_block = 0;

  e.moves = {{MoveName::Tackle, {MoveName::Tackle, 5, 0, {}}}};

  // One move, always repeats. (Tackle, 1) covers all consecutive counts via the
  // clamp in select_next_move.
  e.transitions = {{{MoveName::Tackle, 1}, {{MoveName::Tackle, 1.0f}}}};

  e.first_turn_move = MoveName::Tackle;  // deterministic opener
  e.last_move = std::nullopt;
  e.consecutive_count = 0;

  validate_transitions(e);
  select_next_move(e, rng);  // prime via the fixed first_turn_move
  return e;
}

Enemy make_spike_slime_m(std::mt19937& rng) {
  // Spike Slime (M): Flame Tackle (8 + 1 Slimed) / Lick (1 Frail). Base 30/70,
  // no same move 3x in a row (forced switch at consecutive 2).
  Enemy e;
  e.kind = EnemyKind::SpikeSlimeM;
  std::uniform_int_distribution<int> hp_roll(28, 32);
  e.max_hp = hp_roll(rng);
  e.hp = e.max_hp;
  e.current_block = 0;

  e.moves = {
      {MoveName::FlameTackle,
       {MoveName::FlameTackle, 8, 0, {}, {}, {CardId::Slimed}}},
      {MoveName::Lick,
       {MoveName::Lick, 0, 0,
        {{Debuff::Frail, 1, Target::Character}}}},
  };

  const std::vector<MoveTransition> base{{MoveName::FlameTackle, 0.3f},
                                         {MoveName::Lick, 0.7f}};
  e.transitions = {
      {{MoveName::FlameTackle, 1}, base},
      {{MoveName::FlameTackle, 2}, {{MoveName::Lick, 1.0f}}},
      {{MoveName::Lick, 1}, base},
      {{MoveName::Lick, 2}, {{MoveName::FlameTackle, 1.0f}}},
  };

  // Turn 1: base 30/70 roll.
  e.first_turn_move = std::nullopt;
  e.last_move = sample_from_distribution(base, rng);
  e.consecutive_count = 1;

  validate_transitions(e);
  return e;
}

Enemy make_fungi_beast(std::mt19937& rng) {
  // Fungi Beast: Bite (6 dmg) / Grow (+3 Strength self), reusing the shared
  // MoveName tags (move data is per-enemy). Base 60/40. Asymmetric no-repeat:
  // Bite can't go 3x in a row (banned after 2); Grow can't go 2x (banned after
  // 1). Spore Cloud 2: on death, apply 2 Vulnerable to the player (ROB-62 hook).
  Enemy e;
  e.kind = EnemyKind::FungiBeast;
  std::uniform_int_distribution<int> hp_roll(22, 28);
  e.max_hp = hp_roll(rng);
  e.hp = e.max_hp;
  e.current_block = 0;

  e.moves = {
      {MoveName::Bite, {MoveName::Bite, 6, 0, {}}},
      {MoveName::Grow,
       {MoveName::Grow, 0, 0, /*debuffs=*/{},
        {{Power::Strength, 3, Target::Enemy}}}},
  };

  const std::vector<MoveTransition> base{{MoveName::Bite, 0.6f},
                                         {MoveName::Grow, 0.4f}};
  e.transitions = {
      {{MoveName::Bite, 1}, base},                        // Bite legal again
      {{MoveName::Bite, 2}, {{MoveName::Grow, 1.0f}}},     // no 3rd Bite
      {{MoveName::Grow, 1}, {{MoveName::Bite, 1.0f}}},     // no 2nd Grow
      // No (Grow, 2): Grow can never reach two in a row.
  };

  // On-death: Spore Cloud applies 2 Vulnerable to the player (ROB-63 -> ROB-65).
  e.triggered_effects.push_back({.trigger = Trigger::OnDeath,
                                 .action = TriggeredAction::ApplyPlayerDebuff,
                                 .amount = 2,
                                 .debuff = Debuff::Vulnerable});

  // Turn 1: base 60/40 roll.
  e.first_turn_move = std::nullopt;
  e.last_move = sample_from_distribution(base, rng);
  e.consecutive_count = 1;

  validate_transitions(e);
  return e;
}

Enemy make_blue_slaver(std::mt19937& rng) {
  // Blue Slaver: Stab (12 dmg) / Rake (7 dmg + 1 Weak). Base 60/40, no move 3x
  // in a row. Turn 1 is the base roll (no fixed opener).
  Enemy e;
  e.kind = EnemyKind::BlueSlaver;
  std::uniform_int_distribution<int> hp_roll(46, 50);
  e.max_hp = hp_roll(rng);
  e.hp = e.max_hp;
  e.current_block = 0;

  e.moves = {
      {MoveName::Stab, {MoveName::Stab, 12, 0, {}}},
      {MoveName::Rake,
       {MoveName::Rake, 7, 0,
        {{Debuff::Weak, 1, Target::Character}}}},
  };

  const std::vector<MoveTransition> base{{MoveName::Stab, 0.6f},
                                         {MoveName::Rake, 0.4f}};
  e.transitions = {
      {{MoveName::Stab, 1}, base},
      {{MoveName::Stab, 2}, {{MoveName::Rake, 1.0f}}},
      {{MoveName::Rake, 1}, base},
      {{MoveName::Rake, 2}, {{MoveName::Stab, 1.0f}}},
  };

  // Turn 1: base 60/40 roll.
  e.first_turn_move = std::nullopt;
  e.last_move = sample_from_distribution(base, rng);
  e.consecutive_count = 1;

  validate_transitions(e);
  return e;
}

Enemy make_red_slaver(std::mt19937& rng) {
  // Red Slaver (HP 46-50). Enriched-state AI (ROB-76):
  //   Turn 1: OpenerStab (= Stab 13).
  //   Pre-Entangle cycle Scrape, Scrape, Stab, ... with a per-turn 25% Entangle
  //   interrupt. The first Entangle permanently enters the post-phase Markov
  //   table (55% Scrape / 45% Stab, no Stab-or-Scrape 3x in a row).
  Enemy e;
  e.kind = EnemyKind::RedSlaver;
  std::uniform_int_distribution<int> hp_roll(46, 50);
  e.max_hp = hp_roll(rng);
  e.hp = e.max_hp;
  e.current_block = 0;

  // Shared move data. Pseudo-states (OpenerStab, CycleScrape1/2, CycleStab) map
  // to the same Stab/Scrape data; only their transition role differs.
  const Move stab{MoveName::Stab, 13, 0, {}};
  const Move scrape{MoveName::Scrape, 8, 0,
                    {{Debuff::Weak, 1, Target::Character}}};
  const Move entangle{MoveName::Entangle, 0, 0,
                      {{Debuff::Entangle, 1,
                        Target::Character}}};
  e.moves = {
      {MoveName::Stab, stab},          {MoveName::Scrape, scrape},
      {MoveName::Entangle, entangle},  {MoveName::OpenerStab, stab},
      {MoveName::CycleScrape1, scrape}, {MoveName::CycleScrape2, scrape},
      {MoveName::CycleStab, stab},
  };

  const std::vector<MoveTransition> post{{MoveName::Scrape, 0.55f},
                                         {MoveName::Stab, 0.45f}};
  e.transitions = {
      // Pre-Entangle cycle with a 25% Entangle interrupt each turn.
      {{MoveName::OpenerStab, 1},   {{MoveName::CycleScrape1, 0.75f}, {MoveName::Entangle, 0.25f}}},
      {{MoveName::CycleScrape1, 1}, {{MoveName::CycleScrape2, 0.75f}, {MoveName::Entangle, 0.25f}}},
      {{MoveName::CycleScrape2, 1}, {{MoveName::CycleStab, 0.75f},    {MoveName::Entangle, 0.25f}}},
      {{MoveName::CycleStab, 1},    {{MoveName::CycleScrape1, 0.75f}, {MoveName::Entangle, 0.25f}}},
      // First Entangle -> permanent post-phase Markov (no route back to cycle).
      {{MoveName::Entangle, 1}, post},
      {{MoveName::Scrape, 1}, post},
      {{MoveName::Scrape, 2}, {{MoveName::Stab, 1.0f}}},
      {{MoveName::Stab, 1}, post},
      {{MoveName::Stab, 2}, {{MoveName::Scrape, 1.0f}}},
  };

  e.first_turn_move = MoveName::OpenerStab;  // fixed turn-1 Stab
  e.last_move = std::nullopt;
  e.consecutive_count = 0;

  validate_transitions(e);
  select_next_move(e, rng);  // prime via first_turn_move
  return e;
}

namespace {

// Shared Looter/Mugger factory (ROB-76). Identical scripted AI; they differ
// only in HP, Lunge damage, and Smoke Bomb block. Thievery/gold DEFERRED to
// M4/M5 — the escape is modeled, the gold-steal is not.
//   Turn 1-2: Mug (Mug1 -> Mug2). Turn 3: 50% Lunge / 50% Smoke Bomb.
//   Lunge -> Smoke Bomb -> Escape; Smoke Bomb -> Escape.
Enemy make_thief(std::mt19937& rng, EnemyKind kind, int hp_lo, int hp_hi,
                 int lunge_dmg, int smoke_block) {
  Enemy e;
  e.kind = kind;
  std::uniform_int_distribution<int> hp_roll(hp_lo, hp_hi);
  e.max_hp = hp_roll(rng);
  e.hp = e.max_hp;
  e.current_block = 0;

  const Move mug{MoveName::Mug, 10, 0, {}};
  e.moves = {
      {MoveName::Mug, mug},   {MoveName::Mug1, mug}, {MoveName::Mug2, mug},
      {MoveName::Lunge, {MoveName::Lunge, lunge_dmg, 0, {}}},
      {MoveName::SmokeBomb, {MoveName::SmokeBomb, 0, smoke_block, {}}},
      // Escape leaves the fight (ROB-74). No damage/block.
      {MoveName::Escape, [] { Move m{MoveName::Escape, 0, 0, {}}; m.escapes = true; return m; }()},
  };

  e.transitions = {
      {{MoveName::Mug1, 1}, {{MoveName::Mug2, 1.0f}}},
      {{MoveName::Mug2, 1}, {{MoveName::Lunge, 0.5f}, {MoveName::SmokeBomb, 0.5f}}},
      {{MoveName::Lunge, 1}, {{MoveName::SmokeBomb, 1.0f}}},
      {{MoveName::SmokeBomb, 1}, {{MoveName::Escape, 1.0f}}},
      // Escape terminates (enemy leaves); handle_end_turn skips its advance.
  };

  e.first_turn_move = MoveName::Mug1;  // fixed
  e.last_move = std::nullopt;
  e.consecutive_count = 0;

  validate_transitions(e);
  select_next_move(e, rng);  // prime via first_turn_move
  return e;
}

}  // namespace

Enemy make_looter(std::mt19937& rng) {
  return make_thief(rng, EnemyKind::Looter, 44, 48, /*lunge=*/12, /*smoke=*/6);
}

Enemy make_mugger(std::mt19937& rng) {
  return make_thief(rng, EnemyKind::Mugger, 48, 52, /*lunge=*/16, /*smoke=*/11);
}

Enemy make_acid_slime_l(std::mt19937& rng) {
  // Acid Slime (L) HP 65-69. Same renormalized 3-move AI as Acid Slime M
  // (Tackle/Lick no-repeat, Corrosive Spit no-3x), bigger numbers. At <=50% HP
  // the intent interrupts to Split, spawning 2 Acid Slime M children at the
  // parent's current HP (ROB-64).
  Enemy e;
  e.kind = EnemyKind::AcidSlimeL;
  std::uniform_int_distribution<int> hp_roll(65, 69);
  e.max_hp = hp_roll(rng);
  e.hp = e.max_hp;
  e.current_block = 0;

  e.moves = {
      {MoveName::Tackle, {MoveName::Tackle, 16, 0, {}}},
      {MoveName::Lick,
       {MoveName::Lick, 0, 0,
        {{Debuff::Weak, 2, Target::Character}}}},
      {MoveName::CorrosiveSpit,
       {MoveName::CorrosiveSpit, 11, 0, {}, {}, {CardId::Slimed, CardId::Slimed}}},
  };
  // Split move: kills self, spawns the children (HP inherited at split time).
  Move split{MoveName::Split, 0, 0, {}};
  split.splits = true;
  e.moves[MoveName::Split] = split;

  e.transitions = {
      {{MoveName::Tackle, 1}, {{MoveName::Lick, 0.5f}, {MoveName::CorrosiveSpit, 0.5f}}},
      {{MoveName::Lick, 1},
       {{MoveName::Tackle, 4.0f / 7.0f}, {MoveName::CorrosiveSpit, 3.0f / 7.0f}}},
      {{MoveName::CorrosiveSpit, 1},
       {{MoveName::Tackle, 0.4f}, {MoveName::Lick, 0.3f},
        {MoveName::CorrosiveSpit, 0.3f}}},
      {{MoveName::CorrosiveSpit, 2},
       {{MoveName::Tackle, 4.0f / 7.0f}, {MoveName::Lick, 3.0f / 7.0f}}},
  };

  // Split at <= 50% HP (integer floor): interrupt the intent to Split (ROB-64 ->
  // ROB-65 HpAtOrBelow trigger). Children are Medium Acid Slimes; their HP is
  // overridden to the parent's split-time HP by the split move resolution.
  e.triggered_effects.push_back({.trigger = Trigger::HpAtOrBelow,
                                 .action = TriggeredAction::RewriteIntent,
                                 .param = e.max_hp / 2,
                                 .move = MoveName::Split});
  e.split_children = {make_acid_slime_m(rng), make_acid_slime_m(rng)};

  const std::vector<MoveTransition> first{{MoveName::Tackle, 0.4f},
                                          {MoveName::Lick, 0.3f},
                                          {MoveName::CorrosiveSpit, 0.3f}};
  e.first_turn_move = std::nullopt;
  e.last_move = sample_from_distribution(first, rng);
  e.consecutive_count = 1;

  validate_transitions(e);
  return e;
}

Enemy make_spike_slime_l(std::mt19937& rng) {
  // Spike Slime (L) HP 64-70. Same AI as Spike Slime M (base 30/70, no move 3x),
  // bigger numbers. At <=50% HP interrupts to Split -> 2 Spike Slime M children.
  Enemy e;
  e.kind = EnemyKind::SpikeSlimeL;
  std::uniform_int_distribution<int> hp_roll(64, 70);
  e.max_hp = hp_roll(rng);
  e.hp = e.max_hp;
  e.current_block = 0;

  e.moves = {
      {MoveName::FlameTackle,
       {MoveName::FlameTackle, 16, 0, {}, {}, {CardId::Slimed, CardId::Slimed}}},
      {MoveName::Lick,
       {MoveName::Lick, 0, 0,
        {{Debuff::Frail, 2, Target::Character}}}},
  };
  Move split{MoveName::Split, 0, 0, {}};
  split.splits = true;
  e.moves[MoveName::Split] = split;

  const std::vector<MoveTransition> base{{MoveName::FlameTackle, 0.3f},
                                         {MoveName::Lick, 0.7f}};
  e.transitions = {
      {{MoveName::FlameTackle, 1}, base},
      {{MoveName::FlameTackle, 2}, {{MoveName::Lick, 1.0f}}},
      {{MoveName::Lick, 1}, base},
      {{MoveName::Lick, 2}, {{MoveName::FlameTackle, 1.0f}}},
  };

  // Split at <= 50% HP -> 2 Spike Slime M children (ROB-64 -> ROB-65).
  e.triggered_effects.push_back({.trigger = Trigger::HpAtOrBelow,
                                 .action = TriggeredAction::RewriteIntent,
                                 .param = e.max_hp / 2,
                                 .move = MoveName::Split});
  e.split_children = {make_spike_slime_m(rng), make_spike_slime_m(rng)};

  e.first_turn_move = std::nullopt;
  e.last_move = sample_from_distribution(base, rng);
  e.consecutive_count = 1;

  validate_transitions(e);
  return e;
}

namespace {

// Single-move gremlin (Fat/Sneaky): one deterministic move, 100% of the time.
Enemy make_simple_gremlin(std::mt19937& rng, EnemyKind kind, int hp_lo, int hp_hi,
                          MoveName move, const Move& move_data) {
  Enemy e;
  e.kind = kind;
  std::uniform_int_distribution<int> hp_roll(hp_lo, hp_hi);
  e.max_hp = hp_roll(rng);
  e.hp = e.max_hp;
  e.current_block = 0;

  e.moves = {{move, move_data}};
  // One move -> the (move, 1) row covers all consecutive counts via the clamp.
  e.transitions = {{{move, 1}, {{move, 1.0f}}}};
  e.first_turn_move = move;
  e.last_move = std::nullopt;
  e.consecutive_count = 0;

  validate_transitions(e);
  select_next_move(e, rng);  // prime via first_turn_move
  return e;
}

}  // namespace

Enemy make_fat_gremlin(std::mt19937& rng) {
  // Fat Gremlin (HP 13-17): Smash (4 dmg + 1 Weak), always.
  Move smash{MoveName::Smash, 4, 0,
             {{Debuff::Weak, 1, Target::Character}}};
  return make_simple_gremlin(rng, EnemyKind::FatGremlin, 13, 17, MoveName::Smash,
                             smash);
}

Enemy make_sneaky_gremlin(std::mt19937& rng) {
  // Sneaky Gremlin (HP 10-14): Puncture (9 dmg), always.
  return make_simple_gremlin(rng, EnemyKind::SneakyGremlin, 10, 14,
                             MoveName::Puncture,
                             {MoveName::Puncture, 9, 0, {}});
}

Enemy make_mad_gremlin(std::mt19937& rng) {
  // Mad Gremlin (HP 20-24): Scratch (4 dmg), always. Angry 1: +1 Strength on
  // every attack-damage instance it takes (ROB-64 -> ROB-65 triggered effect;
  // no `once`, so it fires every hit).
  Enemy e = make_simple_gremlin(rng, EnemyKind::MadGremlin, 20, 24,
                                MoveName::Scratch,
                                {MoveName::Scratch, 4, 0, {}});
  e.triggered_effects.push_back({.trigger = Trigger::OnDamaged,
                                 .action = TriggeredAction::GainStrength,
                                 .amount = 1});
  return e;
}

Enemy make_gremlin_wizard(std::mt19937& rng) {
  // Gremlin Wizard (HP 23-25): Ultimate Blast (25 dmg) after charging. First
  // cycle charges 2 turns, then 3 turns every cycle after (ROB-64 enriched
  // states). Charge has no effect.
  Enemy e;
  e.kind = EnemyKind::GremlinWizard;
  std::uniform_int_distribution<int> hp_roll(23, 25);
  e.max_hp = hp_roll(rng);
  e.hp = e.max_hp;
  e.current_block = 0;

  const Move charge{MoveName::Charge, 0, 0, {}};
  const Move blast{MoveName::UltimateBlast, 25, 0, {}};
  e.moves = {
      {MoveName::Charge, charge}, {MoveName::UltimateBlast, blast},
      {MoveName::Charge1, charge}, {MoveName::Charge2, charge},
      {MoveName::Charge3a, charge}, {MoveName::Charge3b, charge},
      {MoveName::Charge3c, charge},
  };

  e.transitions = {
      // First cycle: 2 charges then blast.
      {{MoveName::Charge1, 1}, {{MoveName::Charge2, 1.0f}}},
      {{MoveName::Charge2, 1}, {{MoveName::UltimateBlast, 1.0f}}},
      // Every cycle after: 3 charges then blast, looping.
      {{MoveName::UltimateBlast, 1}, {{MoveName::Charge3a, 1.0f}}},
      {{MoveName::Charge3a, 1}, {{MoveName::Charge3b, 1.0f}}},
      {{MoveName::Charge3b, 1}, {{MoveName::Charge3c, 1.0f}}},
      {{MoveName::Charge3c, 1}, {{MoveName::UltimateBlast, 1.0f}}},
  };

  e.first_turn_move = MoveName::Charge1;  // fixed
  e.last_move = std::nullopt;
  e.consecutive_count = 0;

  validate_transitions(e);
  select_next_move(e, rng);  // prime via first_turn_move
  return e;
}

Enemy make_shield_gremlin(std::mt19937& rng) {
  // Shield Gremlin (HP 12-15): Protect (7 block to a random living ally) while
  // allies live; Shield Bash (6 dmg) once alone. Enriched states (ROB-77):
  //   first_turn = Protect; (Protect,1) -> Protect (support loop).
  //   On becoming the last enemy, the queued Protect is rewritten to
  //   ProtectAlone (protect self this turn), which -> ShieldBash next turn ->
  //   ShieldBash forever.
  Enemy e;
  e.kind = EnemyKind::ShieldGremlin;
  std::uniform_int_distribution<int> hp_roll(12, 15);
  e.max_hp = hp_roll(rng);
  e.hp = e.max_hp;
  e.current_block = 0;

  Move protect{MoveName::Protect, 0, 7, {}};
  protect.blocks_ally = true;
  e.moves = {
      {MoveName::Protect, protect},
      {MoveName::ProtectAlone, protect},  // same data; different transition role
      {MoveName::ShieldBash, {MoveName::ShieldBash, 6, 0, {}}},
  };

  e.transitions = {
      {{MoveName::Protect, 1}, {{MoveName::Protect, 1.0f}}},
      {{MoveName::ProtectAlone, 1}, {{MoveName::ShieldBash, 1.0f}}},
      {{MoveName::ShieldBash, 1}, {{MoveName::ShieldBash, 1.0f}}},
  };

  // On becoming the last living enemy, rewrite the queued Protect intent to
  // ProtectAlone (protect self this turn, then Shield Bash) — ROB-77 -> ROB-65.
  e.triggered_effects.push_back({.trigger = Trigger::BecameLastEnemy,
                                 .action = TriggeredAction::RewriteIntent,
                                 .move = MoveName::ProtectAlone});

  e.first_turn_move = MoveName::Protect;  // never spawns alone; primes Protect
  e.last_move = std::nullopt;
  e.consecutive_count = 0;

  validate_transitions(e);
  select_next_move(e, rng);  // prime via first_turn_move
  return e;
}

Enemy make_lagavulin(std::mt19937& rng) {
  // Lagavulin (HP 109-111). Starts Asleep with Metallicize 8 (gains 8 block at
  // the start of every turn while asleep). Two wake paths (ROB-65):
  //   Self-wake: does nothing for 3 turns, then wakes UNSTUNNED and attacks turn
  //     4. Sleep3 fires OnWake on resolve (end of the 3rd turn) -> that turn
  //     keeps its 8 block; from turn 4 on it gets none.
  //   Damage-wake: taking HP damage while asleep fires OnWake immediately (no
  //     block on the stun turn) and interrupts the intent to Stunned (do nothing
  //     one turn), then joins the cycle.
  // OnWake = { Wake (clear is_asleep), RemoveSelfStatus(Metallicize) }.
  // Post-wake cycle: Attack (18), Attack (18), Siphon Soul (-1 Str/-1 Dex),
  // repeating endlessly.
  Enemy e;
  e.kind = EnemyKind::Lagavulin;
  std::uniform_int_distribution<int> hp_roll(109, 111);
  e.max_hp = hp_roll(rng);
  e.hp = e.max_hp;
  e.current_block = 0;
  e.is_asleep = true;
  e.powers[Power::Metallicize] = 8;

  const Move sleep{MoveName::Sleep, 0, 0, {}};
  // Sleep3 is the last asleep turn: it fires OnWake on resolve (self-wake path).
  Move sleep3{MoveName::Sleep3, 0, 0, {}};
  sleep3.wakes_on_resolve = true;
  const Move stunned{MoveName::Stunned, 0, 0, {}};
  const Move attack{MoveName::LagavulinAttack, 18, 0, {}};
  const Move siphon{MoveName::SiphonSoul,
                    0,
                    0,
                    /*debuffs=*/{},
                    {{Power::Strength, -1, Target::Character},
                     {Power::Dexterity, -1, Target::Character}}};
  e.moves = {
      {MoveName::Sleep, sleep},   {MoveName::Sleep1, sleep},
      {MoveName::Sleep2, sleep},  {MoveName::Sleep3, sleep3},
      {MoveName::Stunned, stunned},
      {MoveName::LagavulinAttack, attack},
      {MoveName::LagavulinAttack1, attack},
      {MoveName::LagavulinAttack2, attack},
      {MoveName::SiphonSoul, siphon},
  };

  e.transitions = {
      // Self-wake countdown: 3 asleep turns, then attack (unstunned) turn 4.
      {{MoveName::Sleep1, 1}, {{MoveName::Sleep2, 1.0f}}},
      {{MoveName::Sleep2, 1}, {{MoveName::Sleep3, 1.0f}}},
      {{MoveName::Sleep3, 1}, {{MoveName::LagavulinAttack1, 1.0f}}},
      // Post-stun (damage-wake) joins the cycle.
      {{MoveName::Stunned, 1}, {{MoveName::LagavulinAttack1, 1.0f}}},
      // Endless Attack, Attack, Siphon Soul cycle.
      {{MoveName::LagavulinAttack1, 1}, {{MoveName::LagavulinAttack2, 1.0f}}},
      {{MoveName::LagavulinAttack2, 1}, {{MoveName::SiphonSoul, 1.0f}}},
      {{MoveName::SiphonSoul, 1}, {{MoveName::LagavulinAttack1, 1.0f}}},
  };

  // OnWake: clear the asleep flag and drop Metallicize. Fires from both wake
  // paths (Sleep3 resolve via wakes_on_resolve; damage-wake in fire_on_damaged).
  e.triggered_effects.push_back(
      {.trigger = Trigger::OnWake, .action = TriggeredAction::Wake});
  e.triggered_effects.push_back({.trigger = Trigger::OnWake,
                                 .action = TriggeredAction::RemoveSelfPower,
                                 .power = Power::Metallicize});
  // Damage-wake: a hit while asleep (requires_asleep guard) interrupts the intent
  // to Stunned. once=true + the guard mean it fires only for the first wake,
  // never mid-cycle after is_asleep is cleared.
  e.triggered_effects.push_back({.trigger = Trigger::OnDamaged,
                                 .action = TriggeredAction::RewriteIntent,
                                 .move = MoveName::Stunned,
                                 .once = true,
                                 .requires_asleep = true});

  e.first_turn_move = MoveName::Sleep1;  // starts asleep
  e.last_move = std::nullopt;
  e.consecutive_count = 0;

  validate_transitions(e);
  select_next_move(e, rng);  // prime via first_turn_move
  return e;
}

}  // namespace minispire
