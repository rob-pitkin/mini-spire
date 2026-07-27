#include "turn_loop.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <vector>

#include "action.h"
#include "card.h"
#include "encounter.h"
#include "enemy.h"
#include "query.h"
#include "status_effect.h"

namespace minispire {

namespace {

// Debuffs decrement by 1 at end of the bearer's turn; remove at 0. Powers never
// tick — decrement-ness is now the TYPE, so no per-effect denylist.
void tick_debuffs(std::unordered_map<Debuff, int>& debuffs) {
  for (auto it = debuffs.begin(); it != debuffs.end();) {
    it->second -= 1;
    if (it->second <= 0) {
      it = debuffs.erase(it);
    } else {
      ++it;
    }
  }
}

// Draw the opening hand. Innate cards (Brutality+) are pulled from the draw
// pile into the hand FIRST and count toward the opening draw, so the hand is
// still STARTING_HAND_SIZE. Combat-start powers don't exist yet, so this needs
// no queue — the CardDrawn hook can't have a listener on turn 1.
void draw_opening_hand(CombatState& state) {
  int drawn = 0;
  for (auto it = state.draw_pile.begin();
       it != state.draw_pile.end() && drawn < STARTING_HAND_SIZE;) {
    if (CARD_DATABASE.at(it->card_id).innate) {
      state.current_hand.push_back(*it);
      it = state.draw_pile.erase(it);
      ++drawn;
    } else {
      ++it;
    }
  }
  for (; drawn < STARTING_HAND_SIZE; ++drawn) {
    draw_one(state);
  }
}

void check_enemy_terminal(CombatState& state) {
  for (const auto& e : state.enemies) {
    if (e.hp > 0) return;
  }
  state.outcome = Outcome::Won;
}

void check_character_terminal(CombatState& state) {
  if (state.character.hp <= 0) state.outcome = Outcome::Lost;
}

// Returns hand index of first card with this id, or -1 if absent.
int find_first_in_hand(const std::vector<Card>& hand, CardId id) {
  for (std::size_t i = 0; i < hand.size(); ++i) {
    if (hand[i].card_id == id) return static_cast<int>(i);
  }
  return -1;
}

// Play one card: translate it into its action sequence and drain the queue
// (effects-architecture Stage 2). The translation preserves the pre-queue
// resolution order: damage (hits x targets), block, debuffs/powers, energy,
// lose-HP, pile move, CardPlayed hook, deferred deaths (ROB-62), then draw
// LAST (ROB-80 Tier B). Hook responses are pushed to the BACK of the queue as
// they fire (§7 default ordering) — divergences from the pre-queue nested
// timing are recorded in docs/design/ordering-notes.md.
void handle_play_card(CombatState& state, CardId card_id, int target) {
  const CardData& data = CARD_DATABASE.at(card_id);

  // 1. Pay energy at translation time — an X-cost card's X (= its hit count)
  // must be known before its hits can be queued.
  int x = 0;
  const int cost = effective_cost(state, card_id);  // Corruption, Blood for Blood
  if (cost == kXCost) {
    x = spend_all_energy(state);
  } else {
    spend_energy(state, cost);
  }
  // hits: -1 means "X hits" (Whirlwind); otherwise the literal count.
  const int hits = (data.hits < 0) ? x : data.hits;

  // 2. The played card leaves the hand now — it is "in flight" during
  // resolution (StS) and rejoins a pile via the queued ExhaustCard/DiscardCard.
  // The INSTANCE is captured, not just the id: Rampage's accumulated bonus and
  // Searing Blow's upgrade count ride on the copy and must survive back into
  // the pile it lands in.
  const int idx = find_first_in_hand(state.current_hand, card_id);
  assert(idx >= 0 && "mask should have rejected this action");
  Card played = state.current_hand[idx];
  state.current_hand.erase(state.current_hand.begin() + idx);

  // The set of enemy slots this card resolves against.
  std::vector<int> target_slots;
  if (data.target == CardTarget::AllEnemies) {
    for (std::size_t i = 0; i < state.enemies.size(); ++i) {
      if (state.enemies[i].hp > 0) target_slots.push_back(static_cast<int>(i));
    }
  } else if (data.target == CardTarget::Enemy && target >= 0 &&
             target < static_cast<int>(state.enemies.size())) {
    target_slots.push_back(target);
  }

  // 3. Translate the CardData field-bag into its default action sequence.
  ActionQueue q;
  ResolutionContext ctx;

  // 3a. Pre-damage hand/block effects (Stage 4b). These resolve BEFORE the
  // card's damage: Sever Soul's exhausts must feed Feel No Pain first, and
  // Entrench's doubling must land before a Body Slam reads block.
  if (data.exhausts_non_attacks_in_hand) {
    // Sever Soul: exhaust every non-Attack in hand (the card itself is already
    // in flight, so it can't exhaust itself).
    std::vector<Card> keep;
    for (const Card& c : state.current_hand) {
      if (CARD_DATABASE.at(c.card_id).type == CardType::Attack) {
        keep.push_back(c);
      } else {
        Action a;
        a.kind = ActionKind::ExhaustCard;
        a.card = c.card_id;
        q.push_back(a);
      }
    }
    state.current_hand = std::move(keep);
  }
  if (data.doubles_block) {
    // Entrench: double current block. Queued as a GainBlock of the current
    // amount so Juggernaut sees a block gain (raw, no Dex/Frail — this isn't
    // block "from a card" in the Dexterity sense).
    Action a;
    a.kind = ActionKind::GainBlock;
    a.target = kPlayerSlot;
    a.amount = state.character.current_block;
    q.push_back(a);
  }

  // Base damage is a QUERY: Body Slam reads current block, Perfected Strike
  // counts Strikes in the deck. Resolved here (at play time) so a mid-card
  // change can't retroactively alter the queued hits.
  // Damage is read from the INSTANCE (Rampage's accumulated bonus, Searing
  // Blow's upgrade count) BEFORE Rampage's growth is applied below — the card
  // reads "deal 8 damage, [then] increase this card's damage by 5".
  const int card_damage = instance_card_damage(state, played);
  // Rampage: this copy permanently gains damage for the rest of the combat.
  // Applied after the damage read, before the pile move, so the growth rides
  // back into the pile on this instance.
  played.bonus_damage += data.bonus_damage_per_play;
  if (card_damage > 0) {
    // One DealDamage per hit per target; a multi-hit AoE (Whirlwind) sweeps
    // all targets each swing. Damage math runs at execution (Strength per hit).
    for (int h = 0; h < hits; ++h) {
      for (int slot : target_slots) {
        Action a;
        a.kind = ActionKind::DealDamage;
        a.actor = kPlayerSlot;
        a.target = slot;
        a.amount = card_damage;
        a.card = card_id;
        a.strength_mult = strength_multiplier(card_id);  // Heavy Blade 3x/5x
        q.push_back(a);
      }
    }
  }
  if (data.block > 0) {
    Action a;
    a.kind = ActionKind::GainBlock;
    a.target = kPlayerSlot;
    a.amount = data.block;
    a.card_block = true;  // Dex/Frail math applies in the executor
    q.push_back(a);
  }
  // Self/None applications route to the player; enemy applications go to each
  // target slot (AoE loops).
  for (const auto& app : data.applies_debuffs) {
    Action a;
    a.kind = ActionKind::ApplyDebuff;
    a.debuff = app.effect;
    a.amount = app.amount;
    if (app.target == Target::Character) {
      a.target = kPlayerSlot;
      q.push_back(a);
    } else {
      for (int slot : target_slots) {
        a.target = slot;
        q.push_back(a);
      }
    }
  }
  for (const auto& app : data.applies_powers) {
    Action a;
    a.kind = ActionKind::ApplyPower;
    a.power = app.effect;
    a.amount = app.amount;
    if (app.target == Target::Character) {
      a.target = kPlayerSlot;
      q.push_back(a);
    } else {
      for (int slot : target_slots) {
        a.target = slot;
        q.push_back(a);
      }
    }
  }
  // Card-flow effects (ROB-80 Tier B). Energy gain, then lose-HP (an EFFECT,
  // not a cost: direct HP loss bypassing block, and it CAN kill the player).
  if (data.energy > 0) {
    Action a;
    a.kind = ActionKind::GainEnergy;
    a.amount = data.energy;
    q.push_back(a);
  }
  if (data.lose_hp > 0) {
    Action a;
    a.kind = ActionKind::LoseHp;
    a.amount = data.lose_hp;
    q.push_back(a);
  }
  // Generated cards (Wild Strike's Wound, Power Through's Wounds, Immolate's
  // Burn, Anger's self-copy). Queued after the card's own effects so e.g.
  // Power Through's Wounds cannot be caught by its own block calculation.
  if (data.generated_count > 0) {
    for (int i = 0; i < data.generated_count; ++i) {
      Action a;
      a.kind = ActionKind::AddCardToPile;
      a.amount = static_cast<int>(data.generated_pile);
      if (data.generates_self_copy) {
        // Anger: the copy inherits this instance's state.
        a.card = card_id;
        a.card_bonus_damage = played.bonus_damage;
        a.card_upgrades = played.upgrades;
      } else {
        a.card = data.generated_card;
      }
      q.push_back(a);
    }
  }
  // Dropkick: if the TARGET is Vulnerable, gain 1 energy and draw 1. Checked at
  // translation, i.e. against the Vulnerable state before this card's own
  // damage — which is what the player sees when choosing the card.
  if (data.bonus_if_target_vulnerable && !target_slots.empty()) {
    const int slot = target_slots.front();
    if (get_status(state.enemies[slot].debuffs, Debuff::Vulnerable) > 0) {
      Action e;
      e.kind = ActionKind::GainEnergy;
      e.amount = 1;
      q.push_back(e);
      Action d2;
      d2.kind = ActionKind::DrawCards;
      d2.amount = 1;
      q.push_back(d2);
    }
  }
  // The played card lands in its pile after the card's own effects resolve.
  // A Power card VANISHES (StS): it enters no pile at all, so it can never be
  // Exhumed or replayed — and, not being exhausted, it doesn't trigger Feel No
  // Pain / Dark Embrace.
  // A card that opens a choice stays IN FLIGHT until the choice resolves, so
  // its pile placement is deferred to after the RequestChoice below. Otherwise
  // the card would be an option for its own effect: Exhume could retrieve
  // itself, and Headbutt's own discarded copy would turn a should-auto-resolve
  // single-card discard pile into a two-option prompt.
  const bool defers_pile_move = data.requests_choice != ChoiceKind::None;
  Action pile_move;
  bool has_pile_move = false;
  if (data.type != CardType::Power) {
    // Corruption also EXHAUSTS every Skill played (not just making them free).
    const bool corrupted_skill =
        data.type == CardType::Skill &&
        get_status(state.character.powers, Power::Corruption) > 0;
    pile_move.kind = (data.exhaust || corrupted_skill)
                         ? ActionKind::ExhaustCard
                         : ActionKind::DiscardCard;
    pile_move.card = card_id;
    // Carry the instance so Rampage's growth (and Searing Blow's upgrades)
    // return to the pile with this copy.
    pile_move.card_bonus_damage = played.bonus_damage;
    pile_move.card_upgrades = played.upgrades;
    has_pile_move = true;
    if (!defers_pile_move) q.push_back(pile_move);
  }
  // CardPlayed hook (Gremlin Nob Enrage), then deferred deaths, then draw.
  {
    Action a;
    a.kind = ActionKind::CardPlayedHook;
    a.card = card_id;
    q.push_back(a);
  }
  q.push_back(Action{ActionKind::CheckDeath});
  if (data.draw > 0) {
    Action a;
    a.kind = ActionKind::DrawCards;
    a.amount = data.draw;
    q.push_back(a);
  }
  // Armaments+: upgrade the WHOLE hand — a shape change from the base card's
  // single choice, so it resolves inline with no pause.
  if (data.upgrades_whole_hand) {
    for (Card& c : state.current_hand) c.card_id = upgraded_card(c.card_id);
  }
  // The card's choice (Stage 4c) queues LAST, after draw: Warcry draws first
  // and you then pick from the resulting hand. The card itself has already
  // left the hand (it is in flight), so it can never be its own option —
  // which is what stops Exhume retrieving itself.
  if (data.requests_choice != ChoiceKind::None) {
    Action a;
    a.kind = ActionKind::RequestChoice;
    a.amount = static_cast<int>(data.requests_choice);
    a.card = card_id;
    q.push_back(a);
    // The deferred pile move lands after the choice — the card was in flight
    // for the whole of its own resolution.
    if (has_pile_move) q.push_back(pile_move);
  }

  // 4. Drain to completion — every mutation is a flat, sequential step; no
  // live reference or open loop spans a mutation. May pause here on a choice.
  drain(state, q, ctx);

  // Battle Trance: no FURTHER draws this turn. Set after the drain so the
  // card's own draw (queued above) still resolves. Cleared at turn start.
  if (data.no_draw_after) state.character.no_draw_this_turn = true;

  // 5. Terminal checks. DEATH takes precedence over victory: if the card's
  // self-damage (a lose-HP card — ROB-80) killed the player, it's a Loss even
  // if the same card also cleared the room (e.g. Hemokinesis at 2 HP killing
  // the last enemy while its 2 HP loss kills you).
  check_character_terminal(state);
  if (state.outcome != Outcome::InProgress) return;
  check_enemy_terminal(state);
}

// Choose a uniform-random living ally (a slot != actor with hp > 0), or -1 if
// there are none. Used by ally-targeting moves (ROB-77 Protect).
int random_living_ally(CombatState& state, int actor_slot) {
  std::vector<int> allies;
  for (int i = 0; i < static_cast<int>(state.enemies.size()); ++i) {
    if (i != actor_slot && state.enemies[i].hp > 0) allies.push_back(i);
  }
  if (allies.empty()) return -1;
  std::uniform_int_distribution<int> pick(0, static_cast<int>(allies.size()) - 1);
  return allies[pick(state.rng)];
}

// Translate one enemy's move into its per-effect action sequence (Stage 3,
// granularity decided: per-effect, same queue as cards — StS resolves an
// enemy's damage/block/debuffs sequentially through the one manager).
// `actor_slot` is the acting enemy's slot — the move's damage uses that
// enemy's status (computed at execution, so a queued Ritual Strength gain is
// visible), its block lands on that enemy (or a random ally for a blocks_ally
// move), and a Target::Enemy status is that enemy's self-buff (e.g. Cultist
// Incantation -> own Strength). Order preserved from the imperative version:
// damage, block, debuffs, powers, added status cards, wake, escape, split.
//
// STS limitation: multi-hit enemy attacks (Lagavulin) would deal Strength
// per-hit; our Move model is one hit per cast until Move grows a `hits` field.
void translate_enemy_move(CombatState& state, const Move& move, int actor_slot,
                          ActionQueue& q) {
  if (move.damage > 0) {
    Action a;
    a.kind = ActionKind::DealDamage;
    a.actor = actor_slot;
    a.target = kPlayerSlot;
    a.amount = move.damage;
    q.push_back(a);
  }
  if (move.block > 0) {
    // Protect (ROB-77): block a random living ally; fall back to self if none.
    // The ally is rolled at TRANSLATION time — no RNG consumer sits between
    // here and execution, so the stream order matches the pre-queue engine,
    // and the ally set can't change during the actor's own move.
    Action a;
    a.kind = ActionKind::GainBlock;
    a.target = actor_slot;
    if (move.blocks_ally) {
      int ally = random_living_ally(state, actor_slot);
      if (ally >= 0) a.target = ally;
    }
    a.amount = move.block;
    q.push_back(a);
  }
  for (const auto& app : move.applies_debuffs) {
    Action a;
    a.kind = ActionKind::ApplyDebuff;
    a.target = (app.target == Target::Character) ? kPlayerSlot : actor_slot;
    a.debuff = app.effect;
    a.amount = app.amount;
    q.push_back(a);
  }
  for (const auto& app : move.applies_powers) {
    Action a;
    a.kind = ActionKind::ApplyPower;
    a.target = (app.target == Target::Character) ? kPlayerSlot : actor_slot;
    a.power = app.effect;
    a.amount = app.amount;
    q.push_back(a);
  }
  // Status cards the move adds to the player's discard (ROB-72), e.g. a slime
  // spit adding Slimed. Resolves with the move (end of this enemy's action).
  for (CardId card : move.adds_to_discard) {
    Action a;
    a.kind = ActionKind::DiscardCard;
    a.card = card;
    q.push_back(a);
  }
  // Wake-on-resolve (ROB-65): Lagavulin's last sleep move (Sleep3) fires the
  // enemy's OnWake effects at the END of the asleep turn (self-wake path), so
  // that turn keeps its Metallicize block and the next turn onward gets none.
  // Fired at translation; the responses land behind the move's own actions.
  if (move.wakes_on_resolve) {
    fire_enemy_hooks(state, actor_slot, Hook::EnemyWake, q);
  }
  if (move.escapes) {
    Action a;
    a.kind = ActionKind::EnemyEscape;
    a.target = actor_slot;
    q.push_back(a);
  }
  if (move.splits) {
    Action a;
    a.kind = ActionKind::EnemySplit;
    a.target = actor_slot;
    q.push_back(a);
  }
}

void handle_end_turn(CombatState& state) {
  // 1. End of player turn — one translate + drain (Stage 4a). StS order:
  // the hand is handled FIRST (ethereal exhausts, rest discards), so an
  // ethereal exhaust's Feel No Pain block queues ahead of the end-of-turn
  // powers; then Combust / player Metallicize / Rage expiry.
  {
    ActionQueue q;
    ResolutionContext ctx;
    q.push_back(Action{ActionKind::DiscardHand});
    fire_player_power_hooks(state, Hook::TurnEndPlayer, q);
    drain(state, q, ctx);
    // Combust can kill the player, and can clear the room.
    check_character_terminal(state);
    if (state.outcome != Outcome::InProgress) return;
    check_enemy_terminal(state);
    if (state.outcome != Outcome::InProgress) return;
  }
  // 1b. Tick character debuffs (powers never tick)
  tick_debuffs(state.character.debuffs);
  // 1c. Discard leftover energy
  state.character.energy = 0;

  // 2. Enemy turn — each enemy that is ALIVE AT THE START OF THE PHASE acts, in
  // slot order. An enemy can leave via escape (ROB-74: hp->0) or spawn children
  // mid-phase via a Split move (ROB-64). Split children must NOT act the phase
  // they spawn (StS), so we snapshot the acting slots up front: a child placed
  // into a freed/appended slot mid-phase is not in the snapshot and is skipped
  // until next phase. Terminal cases checked: the player dying (per enemy) and
  // all enemies gone (after the loop).
  state.character_turn = false;

  // 2a. Reset ALL enemies' block once, at the start of the enemy phase — not
  // per-individual-turn. This matches StS: block persists through the whole
  // enemy phase, so a Protect (ROB-77) granted to an ally that hasn't acted yet
  // survives into the player's turn. (Per-turn reset would wipe it.)
  for (Enemy& e : state.enemies) {
    if (e.hp > 0) e.current_block = 0;
  }

  // Snapshot the slots alive at phase start — the only enemies that act.
  std::vector<std::size_t> acting_slots;
  for (std::size_t i = 0; i < state.enemies.size(); ++i) {
    if (state.enemies[i].hp > 0) acting_slots.push_back(i);
  }

  for (std::size_t slot : acting_slots) {
    if (state.enemies[slot].hp <= 0) continue;  // died earlier this phase

    // 2b. One enemy turn = one translate + drain (Stage 3): start-of-turn
    // power hooks (Ritual, then Metallicize — queued after the phase-start
    // block reset), then the primed intent's per-effect actions. last_move
    // always stores the upcoming intent so the obs shows it. Copy the move by
    // value — an EnemySplit action reallocates state.enemies during the drain,
    // and a Move& into enemy.moves would dangle.
    assert(state.enemies[slot].last_move.has_value() &&
           "enemy.last_move must be primed by start_v1_combat or prior turn");
    const Move move = state.enemies[slot].moves.at(*state.enemies[slot].last_move);
    ActionQueue q;
    ResolutionContext ctx;
    fire_enemy_power_hooks(state, static_cast<int>(slot), Hook::TurnStartEnemy,
                           q);
    translate_enemy_move(state, move, static_cast<int>(slot), q);
    drain(state, q, ctx);

    // 2c. Terminal check — an enemy attack may have killed the player.
    check_character_terminal(state);
    if (state.outcome != Outcome::InProgress) return;

    // If this enemy left the fight via its move — escape (ROB-74) or Split
    // (ROB-64) — it takes no further action this phase. Both make the actor's
    // hp 0, but a Split child may immediately REOCCUPY this slot, so we can't
    // re-test state.enemies[slot].hp here (that would read the child). Key off
    // the move instead. Also skip if the actor died some other way (hp <= 0).
    if (move.escapes || move.splits || state.enemies[slot].hp <= 0) continue;

    // 2d. Tick this enemy's debuffs (powers never tick).
    tick_debuffs(state.enemies[slot].debuffs);

    // 2e. Advance this enemy's Markov chain to set its next intent.
    select_next_move(state.enemies[slot], state.rng);
  }

  // 2f. All enemies gone? An escape (ROB-74) can clear the last living enemy,
  // which ends the fight as a Win even though nothing was killed this turn.
  check_enemy_terminal(state);
  if (state.outcome != Outcome::InProgress) return;

  // 3. Start new player turn. Block reset and the energy refill are upkeep;
  // the start-of-turn powers (Demon Form, Brutality, Berserk, Flame Barrier
  // expiry) and the draw are a translate + drain, so drawn Statuses can fire
  // Evolve / Fire Breathing.
  // Barricade keeps block across the turn boundary (query, Stage 4b).
  if (block_resets_at_turn_start(state)) state.character.current_block = 0;
  state.character.energy = state.character.energy_per_turn;
  state.character.no_draw_this_turn = false;  // Battle Trance is turn-scoped
  state.turn_number += 1;
  state.character_turn = true;
  {
    ActionQueue q;
    ResolutionContext ctx;
    fire_player_power_hooks(state, Hook::TurnStartPlayer, q);
    Action draw;
    draw.kind = ActionKind::DrawCards;
    draw.amount = STARTING_HAND_SIZE;
    q.push_back(draw);
    drain(state, q, ctx);
    // Brutality's HP loss can kill; Fire Breathing can clear the room.
    check_character_terminal(state);
    if (state.outcome != Outcome::InProgress) return;
    check_enemy_terminal(state);
  }
}

}  // namespace

int compute_attack_damage(
    int base, const std::unordered_map<Power, int>& attacker_powers,
    const std::unordered_map<Debuff, int>& attacker_debuffs,
    const std::unordered_map<Debuff, int>& defender_debuffs,
    int strength_mult) {
  // Float-internal, truncated once at the end (per the STS wiki rounding rule).
  // strength_mult is Heavy Blade's "Strength affects this 3x/5x" (Stage 4b);
  // 1 for everything else.
  float d = static_cast<float>(base) +
            static_cast<float>(
                get_status(attacker_powers, Power::Strength) * strength_mult);
  if (get_status(attacker_debuffs, Debuff::Weak) > 0) d *= 0.75f;
  if (get_status(defender_debuffs, Debuff::Vulnerable) > 0) d *= 1.5f;
  int result = static_cast<int>(std::floor(d));
  return result < 0 ? 0 : result;
}

std::vector<Card> starter_deck() {
  std::vector<Card> deck;
  for (int i = 0; i < 5; ++i) deck.push_back(Card{CardId::Strike});
  for (int i = 0; i < 4; ++i) deck.push_back(Card{CardId::Defend});
  deck.push_back(Card{CardId::Bash});
  return deck;
}

CombatState start_combat(uint32_t seed, EncounterPool pool,
                         std::vector<Card> deck) {
  CombatState state;
  state.seed = seed;
  state.rng = std::mt19937(seed);

  state.character.max_hp = IRONCLAD_MAX_HP;
  state.character.hp = IRONCLAD_MAX_HP;
  state.character.energy_per_turn = IRONCLAD_ENERGY_PER_TURN;
  state.character.energy = IRONCLAD_ENERGY_PER_TURN;
  state.character.current_block = 0;

  // Sample the encounter (each enemy primed with its turn-1 intent).
  state.enemies = sample_encounter(pool, state.rng);

  // Shuffle the given deck into the draw pile.
  state.draw_pile = std::move(deck);
  std::shuffle(state.draw_pile.begin(), state.draw_pile.end(), state.rng);

  state.turn_number = 1;
  state.character_turn = true;
  state.outcome = Outcome::InProgress;

  // Draw the opening hand (Innate cards come first and count toward it).
  draw_opening_hand(state);

  return state;
}

CombatState start_v1_combat(uint32_t seed) {
  // Backward-compatible v1 fixture: a fixed single Jaw Worm + the starter deck.
  // Distinct from start_combat (which samples an encounter) so M1 / existing
  // tests keep their deterministic Jaw Worm fight.
  CombatState state;
  state.seed = seed;
  state.rng = std::mt19937(seed);

  state.character.max_hp = IRONCLAD_MAX_HP;
  state.character.hp = IRONCLAD_MAX_HP;
  state.character.energy_per_turn = IRONCLAD_ENERGY_PER_TURN;
  state.character.energy = IRONCLAD_ENERGY_PER_TURN;
  state.character.current_block = 0;

  state.enemies.push_back(make_jaw_worm(state.rng));

  state.draw_pile = starter_deck();
  std::shuffle(state.draw_pile.begin(), state.draw_pile.end(), state.rng);

  state.turn_number = 1;
  state.character_turn = true;
  state.outcome = Outcome::InProgress;

  draw_opening_hand(state);

  return state;
}

DecodedAction decode_action(int action) {
  const int num_card_ids = static_cast<int>(CARD_DATABASE.size());
  const int end_turn_idx = num_card_ids * kMaxEnemies;
  if (action == end_turn_idx) {
    return DecodedAction{/*is_end_turn=*/true, CardId::Strike, 0};
  }
  // action = card_idx * kMaxEnemies + target_idx
  const int card_idx = action / kMaxEnemies;
  const int target = action % kMaxEnemies;
  return DecodedAction{/*is_end_turn=*/false, static_cast<CardId>(card_idx),
                       target};
}

namespace {

// Is one decoded card action legal right now? The single source of truth for
// legality: valid_actions loops it, apply_action calls it once. `entangled` is
// hoisted by the caller (it's per-state, not per-action).
bool card_action_is_legal(const CombatState& state, const DecodedAction& d) {
  const int card_idx = static_cast<int>(d.card);
  if (card_idx < 0 || card_idx >= static_cast<int>(CARD_DATABASE.size())) {
    return false;
  }
  const CardData& data = CARD_DATABASE.at(d.card);
  // Playability (unplayable / Entangle / Clash) and cost (Corruption, Blood for
  // Blood) come from the query layer, so the mask can't disagree with what
  // resolution actually does (Stage 4b, §4.5).
  if (!is_playable(state, d.card)) return false;
  if (find_first_in_hand(state.current_hand, d.card) < 0) return false;
  // X-cost cards (ROB-80) are always affordable (X = current energy, may be 0);
  // fixed-cost cards need enough energy.
  const int cost = effective_cost(state, d.card);
  if (cost != kXCost && state.character.energy < cost) return false;

  // Target legality fork.
  if (card_targets_enemy(data)) {
    // Targeted: the chosen enemy slot must hold a living enemy.
    return d.target < static_cast<int>(state.enemies.size()) &&
           state.enemies[d.target].hp > 0;
  }
  // Untargeted (Defend): only the canonical slot 0 is legal.
  return d.target == 0;
}

}  // namespace

std::vector<bool> valid_actions(const CombatState& state) {
  const int num_card_ids = static_cast<int>(CARD_DATABASE.size());
  std::vector<bool> mask(kTotalActions, false);

  if (state.outcome != Outcome::InProgress) {
    return mask;  // all false
  }

  // Choice mode (Stage 4c): the combat block is entirely illegal and only the
  // offered option slots are legal. Walks the option list (<= 102), never the
  // action space — walking the action space is what cost 39% at Stage 4a.
  if (state.pending_choice.active()) {
    const PendingChoice& pc = state.pending_choice;
    for (int i = 0; i < pc.num_options; ++i) {
      mask[kFirstOptionSlot + i] = true;
    }
    if (pc.is_optional) mask[kDeclineAction] = true;
    return mask;
  }

  // Walk the HAND, not the whole action space: a card not in hand is illegal in
  // all of its target slots, and the hand is <= 10 cards against 400+ actions.
  // (The all-actions loop hashed CARD_DATABASE once per action — the dominant
  // per-step cost once the pool reached 80 cards.)
  for (const Card& c : state.current_hand) {
    const int card_idx = static_cast<int>(c.card_id);
    if (card_idx < 0 || card_idx >= num_card_ids) continue;
    for (int target = 0; target < kMaxEnemies; ++target) {
      const int action = card_idx * kMaxEnemies + target;
      if (mask[action]) continue;  // duplicate card in hand, already decided
      mask[action] = card_action_is_legal(
          state, DecodedAction{/*is_end_turn=*/false, c.card_id, target});
    }
  }

  // End turn is always legal while in progress. (Named constant, not
  // `size - 1`: the last index is now the decline action, not end-turn.)
  mask[kEndTurnAction] = true;
  return mask;
}

bool apply_action(CombatState& state, int action) {
  if (state.outcome != Outcome::InProgress) return false;
  if (action < 0 || action >= kTotalActions) return false;

  // Choice mode (Stage 4c): only the option-slot channel is legal, and it is
  // legal ONLY here — the two blocks are mutually exclusive, which is what
  // keeps an index from ever meaning two things at once.
  if (state.pending_choice.active()) {
    if (action == kDeclineAction) return resolve_choice(state, kDeclineChoice);
    if (action < kFirstOptionSlot) return false;  // combat action while paused
    return resolve_choice(state, action - kFirstOptionSlot);
  }
  if (action >= kFirstOptionSlot) return false;  // slot action with no choice

  // Validate just THIS action rather than building the whole mask (the action
  // space is 600+ entries; building it here doubled the per-step mask cost).
  // Shares card_action_is_legal with valid_actions, so the two can't disagree.
  const DecodedAction d = decode_action(action);
  if (!d.is_end_turn && !card_action_is_legal(state, d)) return false;
  if (d.is_end_turn) {
    handle_end_turn(state);
  } else {
    handle_play_card(state, d.card, d.target);
  }
  return true;
}

}  // namespace minispire
