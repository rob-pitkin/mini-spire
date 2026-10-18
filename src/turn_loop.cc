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
#include "run_rng.h"  // splitmix64 — the card-generation stream's seed mix
#include "status_effect.h"

namespace minispire {

namespace {

// Debuffs decrement by 1 and are removed at 0 — an enemy's at the end of its
// own turn, the player's split between the end of the turn and the end of the
// round as StS does (debuff-timing.md). Powers never tick — decrement-ness is
// now the TYPE, so no per-effect denylist — with three named exceptions:
// Intangible and Duplication at the end of the round, Regen at the end of the
// player's turn (potion-effects.md Q2, Q3). Everything else is either
// permanent for the fight or removed wholesale at a named hook (Flame Barrier,
// Rage).
void tick_one_power(std::unordered_map<Power, int>& powers, Power p) {
  auto it = powers.find(p);
  if (it == powers.end()) return;
  if (--it->second <= 0) powers.erase(it);
}

// Rampage's growth on one copy: up the rung ladder (ROB-87), or past its top
// on the instance counter.
void grow_copy(Card& c, const CardData& data) {
  if (!grow_card_in_place(c)) c.bonus_damage += data.bonus_damage_per_play;
}

// Spend one charge of a counted power (Double Tap, Duplication), removing it
// at 0. False, with nothing spent, if there is no charge.
bool spend_one_charge(std::unordered_map<Power, int>& powers, Power p) {
  auto it = powers.find(p);
  if (it == powers.end() || it->second <= 0) return false;
  if (--it->second <= 0) powers.erase(it);
  return true;
}

// The player's debuffs StS removes at the end of the PLAYER's turn
// (atEndOfTurn(isPlayer)): Entangle and No Draw.
void tick_end_of_turn_debuffs(std::unordered_map<Debuff, int>& debuffs) {
  for (Debuff d : {Debuff::Entangle, Debuff::NoDraw}) {
    auto it = debuffs.find(d);
    if (it != debuffs.end() && --it->second <= 0) debuffs.erase(it);
  }
}

// The player's debuffs StS ticks at the end of the ROUND (atEndOfRound):
// Vulnerable, Weak, Frail, No Block. One an enemy has just applied clears its
// flag instead of losing a stack (debuff-timing.md §1). Confusion never ticks.
void tick_end_of_round_debuffs(Character& c) {
  for (Debuff d : {Debuff::Vulnerable, Debuff::Weak, Debuff::Frail,
                   Debuff::NoBlock}) {
    bool& just = c.debuff_just_applied[static_cast<int>(d)];
    auto it = c.debuffs.find(d);
    if (it == c.debuffs.end() || just) {
      just = false;
      continue;
    }
    if (--it->second <= 0) c.debuffs.erase(it);
  }
}

// An ENEMY's debuffs, at the end of its own turn. StS ticks them at the end of
// the round, after every enemy has acted; nothing between the two reads them.
void tick_debuffs(std::unordered_map<Debuff, int>& debuffs) {
  for (auto it = debuffs.begin(); it != debuffs.end();) {
    // Confusion is a debuff that never wears off (StS's ConfusionPower has no
    // end-of-round decrement) — the one named exception to "debuffs tick", as
    // Intangible is among powers (Rob, 2026-10-03; §6.28).
    if (it->first == Debuff::Confusion) {
      ++it;
      continue;
    }
    it->second -= 1;
    if (it->second <= 0) {
      it = debuffs.erase(it);
    } else {
      ++it;
    }
  }
}

// draw_opening_hand now lives below the anonymous namespace: it is called from
// an executor (ActionKind::DrawOpeningHand) as well as from here.

// Fire on-death hooks for anything that died during a drain (ROB-90).
//
// `CheckDeath` used to be queued from exactly ONE place — inside card
// resolution — so an enemy killed by anything else never fired EnemyDeath or
// BecameLastEnemy. Fungi Beast's Spore Cloud silently did not go off when the
// kill came from Combust or Flame Barrier, which the wiki says it should
// ("will still activate ... ex. from Thorns damage"). No test caught it because
// win/loss detection scans HP directly and was unaffected.
//
// Runs AFTER the drain rather than as a queued action, and that ordering is the
// whole trick: Flame Barrier's retaliation is pushed DURING the drain, when the
// enemy's attack fires PlayerAttacked. A CheckDeath queued up front would
// execute before the kill it exists to notice.
//
// `handle_play_card` keeps its own in-queue CheckDeath instead — its position
// relative to the card's draw is deliberate and pinned (ordering-notes §2).
//
// INVARIANT: every drain that can damage an enemy must reach one of the two.
void process_deaths(CombatState& state, ResolutionContext& ctx) {
  if (ctx.died_count == 0) return;
  ActionQueue q;
  q.push_back(Action{ActionKind::CheckDeath});
  drain(state, q, ctx);
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

// Play one card: translate it into its action sequence and drain the queue
// (effects-architecture Stage 2). The translation preserves the pre-queue
// resolution order: damage (hits x targets), block, debuffs/powers, energy,
// lose-HP, pile move, CardPlayed hook, deferred deaths (ROB-62), then draw
// LAST (ROB-80 Tier B). Hook responses are pushed to the BACK of the queue as
// they fire (§7 default ordering) — divergences from the pre-queue nested
// timing are recorded in docs/design/ordering-notes.md.
}  // namespace

void handle_play_card(CombatState& state, CardId card_id, int target,
                      const PlayContext& ctx_play) {
  const CardData& data = CARD_DATABASE.at(card_id);

  // 1. Which copy is played. A play names (card, cost column), so the copy is
  // one in that column; when several are, the least durable discount goes
  // first (v2-spec.md §6.5, taken_before). It is chosen before energy is paid
  // because the copy's OWN cost is what is paid.
  int hand_idx = -1;
  if (ctx_play.take_from_hand) {
    assert(ctx_play.cost_column >= 0 && "a play from hand must name its copy");
    for (int i = 0; i < static_cast<int>(state.current_hand.size()); ++i) {
      const Card& c = state.current_hand[i];
      if (c.card_id != card_id) continue;
      if (cost_column(state, c) != ctx_play.cost_column) continue;
      if (hand_idx < 0 || taken_before(c, state.current_hand[hand_idx])) {
        hand_idx = i;
      }
    }
    assert(hand_idx >= 0 && "mask should have rejected this action");
  }

  // 2. Pay energy at translation time — an X-cost card's X (= its hit count)
  // must be known before its hits can be queued. A free replay pays nothing
  // and reuses the original X ("the second play of an X-cost Attack will use
  // the same value of X as the first").
  int x = 0;
  if (ctx_play.pay_energy) {
    // The played copy's own cost: Corruption, its layers, Blood for Blood.
    const int cost =
        hand_idx >= 0
            ? instance_effective_cost(state, state.current_hand[hand_idx])
            : effective_cost(state, card_id);
    if (cost == kXCost) {
      x = spend_all_energy(state);
    } else {
      spend_energy(state, cost);
    }
  } else if (ctx_play.forced_x >= 0) {
    x = ctx_play.forced_x;
  }
  // Chemical X raises the EFFECT of an X-cost card, not the energy it spent.
  // `x` stays raw because the Double Tap replay below carries it: the replay
  // adds its own +2, as each StS X action does, so it must not inherit this
  // play's (wiki.gg: free and duplicated X plays both get the bonus).
  const int x_effect = (data.cost == kXCost && state.has_relic(RelicId::ChemicalX))
                           ? x + kChemicalXBonus
                           : x;
  // hits: -1 means "X hits" (Whirlwind); otherwise the literal count.
  const int hits = (data.hits < 0) ? x_effect : data.hits;

  // 3. The played card leaves the hand now — it is "in flight" during
  // resolution (StS) and rejoins a pile via the queued ExhaustCard/DiscardCard.
  // The INSTANCE is captured, not just the id: Rampage's accumulated bonus and
  // Searing Blow's upgrade count ride on the copy and must survive back into
  // the pile it lands in.
  Card played = ctx_play.instance;
  if (ctx_play.take_from_hand) {
    played = state.current_hand[hand_idx];
    state.current_hand.erase(state.current_hand.begin() + hand_idx);
    // Forethought's freeToPlayOnce is spent here, on the copy that leaves the
    // hand. The turn layer is reset by the pile it lands in (move_to_discard /
    // move_to_exhaust), which is StS's Soul -> clearPowers.
    played.free_once = false;
  }

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

  // 4. Translate the CardData field-bag into its default action sequence.
  ActionQueue q;
  ResolutionContext ctx;

  // 3a. Pre-damage hand/block effects (Stage 4b). These resolve BEFORE the
  // card's damage: Sever Soul's exhausts must feed Feel No Pain first, and
  // Entrench's doubling must land before a Body Slam reads block.
  // Wholesale hand exhausts. Sever Soul (non-Attacks, no scaling), Second Wind
  // (non-Attacks, block per card) and Fiend Fire (whole hand, damage per card)
  // share this path. The played card is already in flight, so it never
  // exhausts itself.
  int exhausted_from_hand = 0;
  const bool exhausts_non_attacks =
      data.exhausts_non_attacks_in_hand ||
      data.exhausts_hand == ExhaustHandRule::NonAttacks;
  const bool exhausts_whole_hand = data.exhausts_hand == ExhaustHandRule::All;
  if (exhausts_non_attacks || exhausts_whole_hand) {
    std::vector<Card> keep;
    for (const Card& c : state.current_hand) {
      const bool goes = exhausts_whole_hand ||
                        CARD_DATABASE.at(c.card_id).type != CardType::Attack;
      if (!goes) {
        keep.push_back(c);
        continue;
      }
      Action a;
      a.kind = ActionKind::ExhaustCard;
      a.carry(c);
      q.push_back(a);
      ++exhausted_from_hand;
    }
    state.current_hand = std::move(keep);
  }
  // True Grit: exhaust N RANDOM cards from hand. Rolled here (translation) so
  // the count is known; the exhausts themselves are actions, so Feel No Pain /
  // Dark Embrace / Sentinel all see them.
  for (int i = 0; i < data.exhaust_random_from_hand; ++i) {
    if (state.current_hand.empty()) break;
    std::uniform_int_distribution<std::size_t> pick(
        0, state.current_hand.size() - 1);
    const std::size_t idx = pick(state.rng);
    const Card c = state.current_hand[idx];
    state.current_hand.erase(state.current_hand.begin() + idx);
    Action a;
    a.kind = ActionKind::ExhaustCard;
    a.carry(c);
    q.push_back(a);
    ++exhausted_from_hand;
  }
  // Second Wind: block per card exhausted. Queued after the exhausts so the
  // count is final.
  if (data.block_per_exhausted > 0 && exhausted_from_hand > 0) {
    Action a;
    a.kind = ActionKind::GainBlock;
    a.target = kPlayerSlot;
    a.amount = data.block_per_exhausted * exhausted_from_hand;
    a.card_block = true;  // Dexterity/Frail apply — it is block from a card
    q.push_back(a);
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
  // Fiend Fire deals damage_per_exhausted for EACH card its hand-wipe
  // exhausted, so its damage is only known after that count is final.
  const int card_damage =
      data.damage_per_exhausted > 0
          ? data.damage_per_exhausted * exhausted_from_hand
          : instance_card_damage(state, played);
  // Rampage: this copy permanently gains damage for the rest of the combat.
  // Applied after the damage read, before the pile move, so the growth rides
  // back into the pile on this instance.
  //
  // Growth is an ID SWAP up the rung ladder (ROB-87), not a hidden counter —
  // that is what makes a grown copy visible in the observation and selectable
  // as its own action. Only past the top rung does it fall back to the
  // instance counter, so damage stays exact where the encoding saturates.
  if (data.bonus_damage_per_play > 0) grow_copy(played, data);
  if (card_damage > 0) {
    // One DealDamage per hit per target; a multi-hit AoE (Whirlwind) sweeps
    // all targets each swing. Damage math runs at execution (Strength per hit).
    if (data.hits_random_enemies) {
      // Sword Boomerang: each hit rolls its own target, at EXECUTION time, so
      // a hit that kills an enemy changes the pool for the next one.
      for (int h = 0; h < hits; ++h) {
        Action a;
        a.kind = ActionKind::DamageRandomEnemyAttack;
        a.actor = kPlayerSlot;
        a.amount = card_damage;
        a.card = card_id;
        a.strength_mult = strength_multiplier(card_id);
        q.push_back(a);
      }
    } else {
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
  // Dark Shackles: the target loses Strength for the rest of ITS turn. Two
  // applications, and the second is conditional — StS applies the Shackled
  // give-back only when the target has no Artifact, because a charge negates
  // the loss outright and there is then nothing to hand back. Checked at
  // translation, like Spot Weakness, against the state the player is looking at.
  if (data.enemy_strength_loss_for_turn > 0) {
    for (int slot : target_slots) {
      Action loss;
      loss.kind = ActionKind::ApplyPower;
      loss.target = slot;
      loss.power = Power::Strength;
      loss.amount = -data.enemy_strength_loss_for_turn;
      q.push_back(loss);
      if (get_status(state.enemies[slot].powers, Power::Artifact) > 0) continue;
      Action give_back;
      give_back.kind = ActionKind::ApplyPower;
      give_back.target = slot;
      give_back.power = Power::Shackled;
      give_back.amount = data.enemy_strength_loss_for_turn;
      q.push_back(give_back);
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
  // Bandage Up: flat healing. Queued like every other effect, so it cannot heal
  // above max HP and is visible to anything watching the Heal executor.
  if (data.heal > 0) {
    Action a;
    a.kind = ActionKind::Heal;
    a.amount = data.heal;
    q.push_back(a);
  }
  // Limit Break: multiply the player's Strength.
  if (data.strength_multiply > 0) {
    Action a;
    a.kind = ActionKind::MultiplyStrength;
    a.amount = data.strength_multiply;
    q.push_back(a);
  }
  // Spot Weakness: Strength only if the TARGET's queued intent is an attack.
  // Read at translation, against the intent the player can see when choosing.
  if (data.strength_if_target_attacking > 0 && !target_slots.empty()) {
    const Enemy& e = state.enemies[target_slots.front()];
    bool attacking = false;
    if (e.hp > 0 && e.last_move.has_value()) {
      auto it = e.moves.find(*e.last_move);
      attacking = it != e.moves.end() && it->second.damage > 0;
    }
    if (attacking) {
      Action a;
      a.kind = ActionKind::ApplyPower;
      a.target = kPlayerSlot;
      a.power = Power::Strength;
      a.amount = data.strength_if_target_attacking;
      q.push_back(a);
    }
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
  // Violence: pull random cards of a type out of the draw pile. Queued before
  // the pile move for the same reason Deep Breath is — the card is in flight,
  // so it can never pull itself.
  if (data.draw_pile_to_hand_count > 0) {
    Action a;
    a.kind = ActionKind::DrawPileToHand;
    a.amount = data.draw_pile_to_hand_count;
    a.card_type = data.draw_pile_to_hand_type;
    q.push_back(a);
  }
  // Apotheosis: upgrade every pile. Also queued BEFORE the pile move, which is
  // what keeps it from upgrading itself — in StS the played card is in flight
  // for its whole resolution and is in no pile to be found.
  if (data.upgrades_all_piles) {
    q.push_back(Action{ActionKind::UpgradeAllPiles});
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
  // Double Tap's second copy enters NO pile ("not added to your draw or
  // discard pile, and not Exhausted unless the card would Exhaust normally" —
  // the first copy already handled that).
  // Deep Breath: the reshuffle happens while this card is still IN FLIGHT, so
  // it must be queued before the card's own pile move below. Queued after, the
  // card would shuffle ITSELF back into the draw pile — a test caught exactly
  // that. In StS a played card is in flight for its whole resolution and only
  // then lands in the discard, which is what this ordering reproduces.
  if (data.shuffles_discard_into_draw) {
    q.push_back(Action{ActionKind::ShuffleDiscardIntoDraw});
  }
  if (data.type != CardType::Power && ctx_play.enters_pile) {
    // Corruption also EXHAUSTS every Skill played (not just making them free).
    const bool corrupted_skill =
        data.type == CardType::Skill &&
        get_status(state.character.powers, Power::Corruption) > 0;
    // Medical Kit: playing a Status exhausts it. StS sets card.exhaust on use
    // rather than editing the card's data, which is the same thing here.
    const bool medical_kit_status =
        data.type == CardType::Status &&
        state.has_relic(RelicId::MedicalKit);
    // Havoc forces its card to exhaust regardless of what it would normally do.
    bool exhausts = data.exhaust || corrupted_skill || ctx_play.force_exhaust ||
                    medical_kit_status;
    // Strange Spoon: a card that would exhaust DISCARDS instead, half the time.
    // Rolled from the card RNG, as StS does (cardRandomRng.randomBoolean in
    // UseCardAction), and only for the PLAYED card's own exhaust — a card
    // exhausted from hand by Second Wind or Fiend Fire is untouched, because
    // those never reach this pile move. Powers do not reach here at all, which
    // is StS's explicit `type != POWER` guard falling out of the branch above.
    if (exhausts && strange_spoon_spares(state)) exhausts = false;
    pile_move.kind =
        exhausts ? ActionKind::ExhaustCard : ActionKind::DiscardCard;
    // played.card_id, NOT the card_id argument: since ROB-87 a card's id can
    // CHANGE during its own resolution (Rampage grows a rung), so the copy that
    // returns to the pile must be the one that leaves the queue, not the one
    // that entered it. Using the argument silently discarded the growth.
    // carry() takes played.card_id along with the rest of the instance —
    // above a ladder's cap the overflow counters keep the damage exact, and a
    // this-combat cost discount must ride back into the pile too.
    pile_move.carry(played);
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

  // Effects resolve in CARD-TEXT order, which decides whether the choice comes
  // before or after this card's draw (ROB-85).
  //
  // Warcry reads "Draw 1 card. Put a card from your hand on top of your draw
  // pile" — draw first, so the drawn card is a legal option. Burning Pact reads
  // the other way: "Exhaust 1 card. Draw 2 cards." Queueing its choice after
  // the draw let the agent exhaust a card that same play had just drawn it.
  //
  // Keyed on the choice KIND rather than a new CardData flag: an
  // ExhaustCardInHand choice always reads before the draw in StS card text, and
  // the field would have to be appended past ~20 positional initializers to be
  // set safely. The only other card using this kind is True Grit+, which draws
  // nothing, so the ordering is a no-op there.
  const bool choice_before_draw =
      data.requests_choice == ChoiceKind::ExhaustCardInHand;

  auto queue_choice = [&]() {
    if (data.requests_choice == ChoiceKind::None) return;
    Action a;
    a.kind = ActionKind::RequestChoice;
    a.amount = static_cast<int>(data.requests_choice);
    a.card = card_id;
    // The card's own settings (potion-effects.md S1). A card's multi-select is
    // "up to N" (Purity, Forethought+), and its single pick is mandatory: no
    // card offers a skippable choice, so the minimum follows from the maximum.
    a.choice.copies = data.choice_copies;
    a.choice.max_picks = data.choice_max_picks;
    a.choice.min_picks = data.choice_max_picks > 1 ? 0 : 1;
    a.choice.arrives_free = data.choice_arrives_free;
    a.choice.pool = data.choice_pool;
    q.push_back(a);
    // The deferred pile move lands after the choice — the card was in flight
    // for the whole of its own resolution.
    if (has_pile_move) q.push_back(pile_move);
  };

  if (choice_before_draw) queue_choice();

  // Impatience: the draw is conditional on the hand holding no Attacks. Checked
  // HERE, at translation, rather than inside the executor — the card has already
  // left the hand by this point, so it cannot count itself, and the condition
  // reads the hand as the player sees it when they play the card.
  bool draw_allowed = true;
  if (data.draw_needs_no_attacks_in_hand) {
    for (const Card& c : state.current_hand) {
      if (CARD_DATABASE.at(c.card_id).type == CardType::Attack) {
        draw_allowed = false;
        break;
      }
    }
  }
  if (data.draw > 0 && draw_allowed) {
    Action a;
    a.kind = ActionKind::DrawCards;
    a.amount = data.draw;
    q.push_back(a);
  }
  // Armaments+: upgrade the WHOLE hand — a shape change from the base card's
  // single choice, so it needs no pause. Queued rather than applied inline so
  // every gameplay mutation stays inside an executor.
  if (data.upgrades_whole_hand) {
    q.push_back(Action{ActionKind::UpgradeHand});
  }
  // Infernal Blade: a random class ATTACK, free for the rest of the turn.
  if (data.generates_random_attack) {
    Action a;
    a.kind = ActionKind::GenerateCards;
    a.amount = 1;
    a.gen_pool = GenerationPool::ClassAttack;
    a.gen_pile = GeneratedPile::Hand;
    a.gen_free_this_turn = true;
    q.push_back(a);
  }
  // Random generation proper (Jack of All Trades, Transmutation, Chrysalis,
  // Metamorphosis). Transmutation generates X cards, where X is the energy this
  // play spent.
  if (data.generates_pool != GenerationPool::None) {
    Action a;
    a.kind = ActionKind::GenerateCards;
    a.amount = data.generates_x_count ? x_effect : data.generates_count;
    a.gen_pool = data.generates_pool;
    a.gen_pile = data.generates_into;
    a.gen_upgraded = data.generates_upgraded;
    a.gen_free_this_turn = data.generates_free_this_turn;
    a.gen_free_this_combat = data.generates_free_this_combat;
    q.push_back(a);
  }
  // Madness: one random card in hand costs 0 for the rest of the combat. The
  // layer is set HERE rather than in the executor, because Mummified Hand
  // shares that executor on the turn layer — a bare Action would leave this at
  // CostLayer::None and silently discount nothing.
  if (data.discounts_random_card_in_hand) {
    Action a;
    a.kind = ActionKind::DiscountRandomCardInHand;
    a.discount_layer = CostLayer::Combat;
    q.push_back(a);
  }
  // The Bomb: light a fuse. `card` names which Bomb, since the two differ only
  // in the damage they eventually deal.
  if (data.bomb_damage > 0) {
    Action a;
    a.kind = ActionKind::ArmBomb;
    a.card = card_id;
    q.push_back(a);
  }
  // Enlightenment: cap every hand card's cost at 1.
  if (data.caps_hand_cost_at_one) {
    Action a;
    a.kind = ActionKind::CapHandCost;
    a.cost_cap_for_combat = data.caps_hand_cost_for_combat;
    q.push_back(a);
  }
  // Havoc: take the top card of the draw pile into the card queue, to be
  // played — and exhausted — once this card's actions have all run
  // (card-queue.md). Its target is rolled NOW, as Havoc is played: Havoc.use
  // passes getRandomMonster(null, true, cardRandomRng) to PlayTopCardAction
  // (potion-effects.md T4).
  if (data.plays_top_of_draw) {
    Action a;
    a.kind = ActionKind::TakeTopCard;
    a.amount = 1;  // "and Exhaust it"
    a.target = roll_card_target(state);
    q.push_back(a);
  }
  // Double Tap (an Attack) and Duplication (any card): each replays this card
  // once, free, at the same target with the same X — a copy put at the FRONT
  // of the card queue, played once this card's actions have all run. StS's
  // addCardQueueItem(copy, true) inserts right behind the card being played,
  // and the engine's card in flight is not in the queue, so that is the front.
  //
  // The charge is spent HERE, as the card is played, not when the replay
  // resolves (potion-effects.md T1): DoubleTapPower and DuplicationPower
  // decrement in onUseCard and queue the copy. So a card Havoc plays before
  // the replay resolves finds the charge already gone.
  //
  // A replay is never replayed itself (StS's purgeOnUse); enters_pile is false
  // only for a replay. With both powers up an Attack is played three times;
  // the two copies are identical, so their order cannot show.
  if (ctx_play.enters_pile) {
    int replays = 0;
    if (data.type == CardType::Attack &&
        spend_one_charge(state.character.powers, Power::DoubleTap)) {
      ++replays;
    }
    if (spend_one_charge(state.character.powers, Power::Duplication)) {
      ++replays;
    }
    // The copy is the card AFTER this play's growth (#44). In StS it sits in
    // limbo sharing the original's uuid while Rampage's ModifyDamageAction
    // runs, which reaches every instance with that uuid, so a replayed Rampage
    // plays at 13, not 8. `played` has already grown above; card_id has not.
    const int lineage = replays > 0 ? ++state.replay_lineage : 0;
    const OriginPile origin =
        !has_pile_move ? OriginPile::None
        : pile_move.kind == ActionKind::ExhaustCard ? OriginPile::Exhaust
                                                    : OriginPile::Discard;
    for (int i = 0; i < replays; ++i) {
      QueuedPlay copy;
      copy.instance = Card{played.card_id, played.bonus_damage, played.upgrades};
      copy.target = target;
      copy.replay = true;
      copy.x = x;  // the first play's RAW X — the replay adds Chemical X
      copy.lineage = lineage;
      copy.original = copy.instance;
      copy.original_pile = origin;
      state.card_queue.push_front(copy);
    }
  }
  // The card's choice (Stage 4c) normally queues LAST, after draw: Warcry draws
  // first and you then pick from the resulting hand. The card itself has
  // already left the hand (it is in flight), so it can never be its own option
  // — which is what stops Exhume retrieving itself.
  if (!choice_before_draw) queue_choice();

  // 4. Drain to completion — every mutation is a flat, sequential step; no
  // live reference or open loop spans a mutation. May pause here on a choice.
  drain(state, q, ctx);

  // Late effects — they run after the drain because each depends on what the
  // card's resolution actually did, which is not known until it finishes.
  //
  // They are QUEUED rather than applied directly (ROB-91). All three used to
  // write state straight from here, which bypassed the single write path:
  // NoDraw skipped `apply_debuff`'s Artifact gate, and Reaper/Feed had no
  // executor to route through at all. Same shape as ROB-90's death gap — an
  // effect that arrives late takes the short path because nothing is holding
  // the queue open for it. Draining a second time costs one pass and keeps the
  // rule intact.
  {
    ActionQueue late;
    // Battle Trance: no FURTHER draws this turn. Queued after this drain so the
    // card's own draw already resolved. Cleared by the end-of-turn tick.
    if (data.no_draw_after) {
      Action a;
      a.kind = ActionKind::ApplyDebuff;
      a.target = kPlayerSlot;
      a.debuff = Debuff::NoDraw;
      a.amount = 1;
      late.push_back(a);
    }
    // Reaper: heal the UNBLOCKED total across its AoE targets.
    if (data.heals_unblocked_damage && ctx.unblocked_damage_dealt > 0) {
      Action a;
      a.kind = ActionKind::Heal;
      a.amount = ctx.unblocked_damage_dealt;
      late.push_back(a);
    }
    // Feed: "If Fatal" — max HP only if this card's damage killed something.
    // None of the Act 1 roster are minions, the only case StS excludes.
    if (data.max_hp_on_kill > 0 && ctx.died_count > 0) {
      Action a;
      a.kind = ActionKind::GainMaxHp;
      a.amount = data.max_hp_on_kill;
      late.push_back(a);
    }
    // Hand of Greed: "If Fatal, gain 20 Gold" — the same shape as Feed, and
    // late for the same reason: whether the card killed anything is only known
    // once its damage has resolved.
    if (data.gold_on_kill > 0 && ctx.died_count > 0) {
      Action a;
      a.kind = ActionKind::GainGold;
      a.amount = data.gold_on_kill;
      late.push_back(a);
    }
    if (!late.empty()) drain(state, late, ctx);
  }

  // 5. Terminal checks. DEATH takes precedence over victory: if the card's
  // self-damage (a lose-HP card — ROB-80) killed the player, it's a Loss even
  // if the same card also cleared the room (e.g. Hemokinesis at 2 HP killing
  // the last enemy while its 2 HP loss kills you).
  check_character_terminal(state);
  if (state.outcome != Outcome::InProgress) return;
  check_enemy_terminal(state);
}

namespace {

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
    a.actor = actor_slot;  // the enemy is the source (debuff-timing.md)
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
    // The Bomb ticks with the other end-of-turn powers, which is what it is in
    // StS — a power whose fuse counts down at the end of each of your turns.
    q.push_back(Action{ActionKind::TickBombs});
    fire_relic_hooks(state, Hook::TurnEndPlayer, q);
    drain(state, q, ctx);
    // Combust's damage lands inside that drain, so its kills need their
    // on-death hooks fired before we decide the fight is over (ROB-90).
    process_deaths(state, ctx);
    // Combust can kill the player, and can clear the room.
    check_character_terminal(state);
    if (state.outcome != Outcome::InProgress) return;
    check_enemy_terminal(state);
    if (state.outcome != Outcome::InProgress) return;
  }
  // 1b. End of the player's turn: what StS ticks in atEndOfTurn(isPlayer) —
  // Entangle and No Draw come off, Regen loses a stack. Everything else that
  // ticks waits for the end of the ROUND, step 2h (debuff-timing.md).
  //
  // Regen ticks AFTER the drain above, which healed by its current stacks —
  // StS's RegenAction heals, then decrements.
  tick_end_of_turn_debuffs(state.character.debuffs);
  tick_one_power(state.character.powers, Power::Regen);
  // 1c. Leftover energy is NOT discarded here. The turn-start refill is the
  // single authority on what energy a turn begins with (query.cc's
  // energy_after_turn_start), and it replaces the total for everyone who does
  // not hold Ice Cream — so zeroing here changed nothing except destroying the
  // leftover Ice Cream exists to carry forward. Nothing reads the player's
  // energy between this point and the refill: the enemy phase does not, and
  // there is no agent decision point inside it.

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
    const MoveName executed = *state.enemies[slot].last_move;
    const Move move = state.enemies[slot].moves.at(executed);
    ActionQueue q;
    ResolutionContext ctx;
    fire_enemy_power_hooks(state, static_cast<int>(slot), Hook::TurnStartEnemy,
                           q);
    translate_enemy_move(state, move, static_cast<int>(slot), q);
    drain(state, q, ctx);
    // Flame Barrier can kill the attacker mid-drain, and a Juggernaut or
    // Combust tick can kill anything (ROB-90). Fire the on-death hooks before
    // the terminal check, so a Spore Cloud from the last enemy still lands.
    process_deaths(state, ctx);

    // 2c. Terminal check — an enemy attack may have killed the player.
    check_character_terminal(state);
    if (state.outcome != Outcome::InProgress) return;

    // If this enemy left the fight via its move — escape (ROB-74) or Split
    // (ROB-64) — it takes no further action this phase. Both make the actor's
    // hp 0, but a Split child may immediately REOCCUPY this slot, so we can't
    // re-test state.enemies[slot].hp here (that would read the child). Key off
    // the move instead. Also skip if the actor died some other way (hp <= 0).
    if (move.escapes || move.splits || state.enemies[slot].hp <= 0) continue;

    // 2d. End-of-turn power hooks (Ritual). Fires before the next intent is
    // sampled, so the Strength is in place when the player's observation reads
    // intent damage (ROB-85). An enemy that escaped, split, or died skipped
    // this via the `continue` above — it took no end of turn.
    {
      ActionQueue end_q;
      ResolutionContext end_ctx;
      fire_enemy_power_hooks(state, static_cast<int>(slot), Hook::TurnEndEnemy,
                             end_q);
      drain(state, end_q, end_ctx);
    }

    // 2e. Tick this enemy's debuffs (powers never tick).
    tick_debuffs(state.enemies[slot].debuffs);

    // 2f. Advance this enemy's Markov chain to set its next intent — unless an
    // interrupt rewrote it during this very turn, in which case the rewrite IS
    // the next intent. A Large slime that Thorns or Flame Barrier drops to half
    // HP while it attacks: AcidSlime_L.damage() queues SetMoveAction(SPLIT) with
    // addToBottom, behind the RollMoveAction its takeTurn already queued, so the
    // split is the move that stands. Rolling from Split instead found no
    // transition and asserted (#38).
    //
    // Only a RewriteIntent changes last_move mid-turn, and none of today's
    // interrupts rewrites a move to itself, so "changed" means "interrupted".
    if (state.enemies[slot].last_move == executed) {
      select_next_move(state.enemies[slot], state.rng);
    }
  }

  // 2g. All enemies gone? An escape (ROB-74) can clear the last living enemy,
  // which ends the fight as a Win even though nothing was killed this turn.
  check_enemy_terminal(state);
  if (state.outcome != Outcome::InProgress) return;

  // 2h. End of the round: the player's atEndOfRound, after every enemy has
  // acted (MonsterGroup.applyEndOfTurnPowers; debuff-timing.md). Vulnerable,
  // Weak, Frail and No Block lose a stack unless just applied; then
  // Intangible and Duplication. So a player's Vulnerable covers the enemy
  // phase it is still up for, and Incense Burner's Intangible protects the
  // enemy phase of the turn it was granted — at the end of the player's turn,
  // as this used to be, both were gone before the enemies acted.
  //
  // Intangible (Rob, 2026-09-12), Regen (Rob, 2026-10-04) and Duplication
  // (potion-effects.md Q3) are deliberate, named exceptions to "powers never
  // tick" (see status_effect.h). All are BUFFs in StS — beneficial, so none can
  // live in the debuff map, which would put it in the wrong observation block
  // and route it through Artifact. Named one by one rather than by a per-power
  // tick list, so each exception stays one line and one name instead of
  // becoming a general mechanism.
  tick_end_of_round_debuffs(state.character);
  tick_one_power(state.character.powers, Power::Intangible);
  tick_one_power(state.character.powers, Power::Duplication);

  // 3. Start new player turn. Block reset and the energy refill are upkeep;
  // the start-of-turn powers (Demon Form, Brutality, Berserk, Flame Barrier
  // expiry) and the draw are a translate + drain, so drawn Statuses can fire
  // Evolve / Fire Breathing.
  // Barricade keeps block across the turn boundary (query, Stage 4b).
  state.character.current_block = block_after_turn_start(state);
  // Ice Cream carries unspent energy forward, so this is a query rather than a
  // plain assignment (query.cc). Turn 1 is set in start_combat, where there is
  // nothing to carry.
  state.character.energy = energy_after_turn_start(state);
  // Panache's countdown restarts every turn: four cards this turn and one the
  // next must not set it off.
  state.character.panache_counter = kPanacheCardsPerTrigger;
  // Battle Trance's NoDraw needs no clear here — it is a Debuff now (ROB-40 B2)
  // and the end-of-turn tick already expired it.
  // The turn cost layer ends here, wherever the card sits — StS's endTurn runs
  // resetAttributes over the hand, draw and discard piles, and the exhaust pile
  // was reset on entry. This now covers Mummified Hand too, whose discount is a
  // turn-layer write in StS and used to outlive the turn (§6.27). The combat
  // layer and free_once survive.
  for (std::vector<Card>* pile :
       {&state.current_hand, &state.draw_pile, &state.discard_pile,
        &state.exhaust_pile}) {
    for (Card& c : *pile) reset_turn_cost(c);
  }
  state.turn_number += 1;
  state.character_turn = true;
  {
    // The queue is built in the order of StS's turn-start block
    // (GameActionManager, the `turnHasEnded` branch): relics' atTurnStart,
    // then powers' atStartOfTurn, then the draw, then the post-draw relics.
    // Every hook fires as the queue is BUILT, and what each one queues
    // resolves later, in that order (card-queue.md §3, Q2).
    //
    // Relics before powers (Rob, 2026-10-10, Q2). It became observable with
    // Mayhem, which rolls its target when its action runs: Mercury Hourglass's
    // damage now lands first, so the roll skips an enemy it killed. Turn 1 is
    // deliberately not handled here: start_combat's CombatStart sub-phase is
    // turn 1's start, so firing both would double-count anything that counts
    // turns.
    ActionQueue q;
    ResolutionContext ctx;
    fire_relic_hooks(state, Hook::TurnStartPlayer, q);
    fire_player_power_hooks(state, Hook::TurnStartPlayer, q);
    // Cleared AFTER the hooks have read them: Art of War grants its energy on
    // the strength of the turn that just ended, and Orange Pellets needs all
    // three to have been played within ONE turn. Cleared for every fight, not
    // only when a relic cares, so the flags never carry a stale turn's value.
    state.character.played_attack_this_turn = false;
    state.character.played_skill_this_turn = false;
    state.character.played_power_this_turn = false;
    Action draw;
    draw.kind = ActionKind::DrawCards;
    draw.amount = hand_size(state);  // 7 with Snecko Eye
    q.push_back(draw);
    // Post-draw relics (Pocketwatch), fired now as StS's
    // applyStartOfTurnPostDrawRelics is — their actions land behind the draw,
    // which is what makes them post-draw. This used to be an action that ran
    // after the draw, which put its actions behind anything the draw itself
    // triggered (Evolve) and behind Mayhem's take; StS puts them in front.
    fire_relic_hooks(state, Hook::TurnStartPostDraw, q);
    drain(state, q, ctx);
    // Brutality's HP loss can kill; Fire Breathing can clear the room.
    check_character_terminal(state);
    if (state.outcome != Outcome::InProgress) return;
    check_enemy_terminal(state);
  }
}

}  // namespace

// Draw the opening hand. Innate cards (Brutality+) and bottled cards are pulled
// from the draw pile into the hand FIRST and count toward the opening draw.
// Combat-start powers don't exist yet, so this needs no queue — the CardDrawn
// hook can't have a listener on turn 1.
//
// ALL of them arrive, even past the hand size. StS's initializeDeck puts every
// Innate and bottled card on top, draws the hand, and queues an extra draw for
// however many did not fit (§6.23). This loop used to stop at 5, which three
// Bottles plus Innate cards could reach.
//
// The hand size is hand_size(state) — 7 with Snecko Eye — and every card here
// is DRAWN in StS, so Confusion rolls the pulled ones as well as the rest
// (§6.28).
void draw_opening_hand(CombatState& state, ActionQueue& q) {
  int drawn = 0;
  for (auto it = state.draw_pile.begin(); it != state.draw_pile.end();) {
    if (CARD_DATABASE.at(it->card_id).innate || it->bottle != Bottle::None) {
      state.current_hand.push_back(*it);
      confuse_drawn_card(state, state.current_hand.back());
      it = state.draw_pile.erase(it);
      ++drawn;
    } else {
      ++it;
    }
  }
  for (const int size = hand_size(state); drawn < size; ++drawn) {
    draw_one(state, q);
  }
}

int compute_attack_damage(
    int base, const std::unordered_map<Power, int>& attacker_powers,
    const std::unordered_map<Debuff, int>& attacker_debuffs,
    const std::unordered_map<Debuff, int>& defender_debuffs,
    int strength_mult, float vulnerable_mult) {
  // Float-internal, truncated once at the end (per the STS wiki rounding rule).
  // strength_mult is Heavy Blade's "Strength affects this 3x/5x" (Stage 4b);
  // 1 for everything else.
  float d = static_cast<float>(base) +
            static_cast<float>(
                get_status(attacker_powers, Power::Strength) * strength_mult);
  // Vigor is ADDITIVE, alongside Strength — "X additional damage per hit" — so
  // Weak and Vulnerable scale it like any other base damage. Adding it after
  // the multipliers would make it immune to Weak, which it is not.
  d += static_cast<float>(get_status(attacker_powers, Power::Vigor));
  if (get_status(attacker_debuffs, Debuff::Weak) > 0) d *= 0.75f;
  if (get_status(defender_debuffs, Debuff::Vulnerable) > 0) d *= vulnerable_mult;
  // Pen Nib doubles. Position in this chain does not matter arithmetically —
  // everything is float until the single floor below — but it sits last because
  // that is where it reads in the game's own description: the final number is
  // doubled.
  if (get_status(attacker_powers, Power::PenNibCharge) > 0) d *= 2.0f;
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

CombatState start_combat(CombatSetup setup) {
  CombatState state;
  state.seed = setup.seed;
  state.rng = std::mt19937(setup.seed);
  // A SEPARATE generator for in-combat card generation (§3.5). Mixed rather
  // than used raw so that a standalone fight, whose card_seed defaults to 0,
  // still differs from its shuffle stream instead of tracking it.
  state.card_rng = std::mt19937(static_cast<std::mt19937::result_type>(
      splitmix64(static_cast<uint64_t>(setup.card_seed) << 32 | setup.seed)));
  // The floor's potion generator, lent by the run and handed back after the
  // fight (potion-effects.md §5.6). Taken as it is, not re-derived: the run
  // may already have drawn from it this floor.
  state.potion_rng = setup.potion_rng;

  state.character.max_hp = setup.max_hp;
  state.character.hp = setup.hp;
  state.character.current_block = 0;

  // Present before anything reads them. A start-of-combat relic effect works
  // off the character's real HP, and one that changes the opening draw has to
  // be here before the hand is dealt below.
  state.relics = std::move(setup.relics);
  state.potions = std::move(setup.potions);
  state.potion_slots = setup.potion_slots;

  // Energy relics are read AFTER relics are assigned, for the same reason
  // everything else in CombatSetup is: a bonus computed before the relics exist
  // is always zero. Slaver's Collar needs to know the fight's kind, which only
  // the setup has — CombatState does not record it.
  state.is_elite = setup.pool == EncounterPool::Elite;
  state.character.energy_per_turn =
      IRONCLAD_ENERGY_PER_TURN + relic_bonus_energy(state, state.is_elite);
  state.character.energy = state.character.energy_per_turn;

  // Sample the encounter (each enemy primed with its turn-1 intent).
  state.enemies = sample_encounter(setup.pool, state.rng);

  // Shuffle the given deck into the draw pile.
  state.draw_pile = std::move(setup.deck);
  std::shuffle(state.draw_pile.begin(), state.draw_pile.end(), state.rng);

  state.turn_number = 1;
  state.character_turn = true;
  state.outcome = Outcome::InProgress;

  // Start-of-combat relics, in the ordered sub-phases the real game uses
  // (docs/design/relic-effects.md §3.1):
  //
  //   Hook::CombatStartPreDraw  -- before the hand exists (Toolbox)
  //   DrawOpeningHand           -- the opening hand, Innate-aware
  //   Hook::CombatStart         -- post-draw relics
  //   Hook::TurnStartPostDraw   -- the last sub-phase
  //
  // ONE queue, drained once. Everything after the pre-draw hooks is an ACTION
  // rather than an imperative call, which is what lets a pre-draw relic PAUSE
  // the whole sequence: Toolbox asks the player to choose a card before the
  // hand is dealt, so a fight can open already waiting on a choice. The queue
  // keeps the order without needing a drain between each step.
  //
  // The deck shuffle above is NOT part of this and fires no ShuffleDrawPile:
  // StS shuffles the opening draw pile inside CardGroup.initializeDeck, which
  // never reaches the relics, so Sundial and The Abacus do not trigger here.
  ActionQueue q;
  ResolutionContext ctx;

  fire_relic_hooks(state, Hook::CombatStartPreDraw, q);
  q.push_back(Action{ActionKind::DrawOpeningHand});
  q.push_back(Action{ActionKind::CombatStartPostDraw});
  drain(state, q, ctx);

  return state;
}

CombatState start_combat(uint32_t seed, EncounterPool pool,
                         std::vector<Card> deck) {
  CombatSetup setup;
  setup.seed = seed;
  setup.pool = pool;
  setup.deck = std::move(deck);
  return start_combat(std::move(setup));
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

  // A queue of its own, drained here. The fixture holds no relics, so nothing
  // can respond, but the opening draw can reshuffle and a reshuffle now pushes
  // actions. Using the real path keeps this in step with start_combat.
  ActionQueue fixture_q;
  ResolutionContext fixture_ctx;
  draw_opening_hand(state, fixture_q);
  drain(state, fixture_q, fixture_ctx);

  return state;
}

int encode_action(ActionBlock block, int entity, int target, int cost) {
  const ActionBlockSpan& b = kActionBlocks[static_cast<std::size_t>(block)];
  assert(entity >= 0 && entity * b.stride() < b.size &&
         "entity out of range for its action block");
  assert(target >= 0 && target < b.targets &&
         "target out of range for its action block");
  assert(cost >= 0 && cost < b.costs &&
         "cost column out of range for its action block");
  return b.first + (entity * b.costs + cost) * b.targets + target;
}

DecodedAction decode_action(int action) {
  assert(action >= 0 && action < kTotalActions &&
         "decode_action outside the action space");
  // Twelve blocks, scanned in layout order. Combat is block 0, so a card play —
  // the overwhelmingly common action on the benchmarked hot path — resolves on
  // the first comparison.
  for (std::size_t i = 0; i < kActionBlocks.size(); ++i) {
    const ActionBlockSpan& b = kActionBlocks[i];
    if (action < b.first + b.size) {
      const int offset = action - b.first;
      const int target = offset % b.targets;
      const int cost = (offset / b.targets) % b.costs;
      const int entity = offset / b.stride();
      return DecodedAction{static_cast<ActionBlock>(i), entity, target, cost};
    }
  }
  // Unreachable while action_blocks_tile_the_space() holds, which is a
  // static_assert. Returned rather than UB if the range assert is compiled out.
  return DecodedAction{ActionBlock::Decline, 0, 0};
}

namespace {

// Is one decoded card action legal right now? The single source of truth for
// legality: valid_actions loops it, apply_action calls it once. `entangled` is
// hoisted by the caller (it's per-state, not per-action).
bool card_action_is_legal(const CombatState& state, const DecodedAction& d) {
  if (d.block != ActionBlock::Combat) return false;
  const int card_idx = d.entity;
  if (card_idx < 0 || card_idx >= static_cast<int>(CARD_DATABASE.size())) {
    return false;
  }
  const CardId card = d.card();
  const CardData& data = CARD_DATABASE.at(card);
  // Playability (unplayable / Entangle / Clash) and cost (Corruption, Blood for
  // Blood) come from the query layer, so the mask can't disagree with what
  // resolution actually does (Stage 4b, §4.5).
  if (!is_playable(state, card)) return false;
  // The action names a cost column (v2-spec.md §6.5): some copy of the card in
  // that column must be in hand. Every copy in one column costs the same — the
  // column IS the effective cost — so whichever is found answers affordability.
  const Card* copy = nullptr;
  for (const Card& c : state.current_hand) {
    if (c.card_id == card && cost_column(state, c) == d.cost) {
      copy = &c;
      break;
    }
  }
  if (copy == nullptr) return false;
  // X-cost cards (ROB-80) are always affordable (X = current energy, may be 0);
  // fixed-cost cards need enough energy.
  const int cost = instance_effective_cost(state, *copy);
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

bool holds_potion(const CombatState& state, PotionId p) {
  return std::find(state.potions.begin(), state.potions.end(), p) !=
         state.potions.end();
}

// Is drinking `p` at `target` legal now? The one source of truth for potion
// legality, shared by the mask and apply_action like card_action_is_legal.
// A potion that targets an enemy needs a living one in that slot; any other
// uses the canonical slot 0 (potion-effects.md §3.1).
bool potion_use_is_legal(const CombatState& state, PotionId p, int target) {
  if (!holds_potion(state, p) || !potion_can_be_drunk(state, p)) return false;
  if (potion_targets_enemy(p)) {
    return target >= 0 && target < static_cast<int>(state.enemies.size()) &&
           state.enemies[target].hp > 0;
  }
  return target == 0;
}

}  // namespace

std::vector<bool> valid_actions(const CombatState& state) {
  const int num_card_ids = static_cast<int>(CARD_DATABASE.size());
  std::vector<bool> mask(kTotalActions, false);

  if (state.outcome != Outcome::InProgress) {
    return mask;  // all false
  }

  // Choice mode: the combat block is entirely illegal, and the legal actions
  // are the OFFERED CARDS, indexed by (CardId, cost column) (§6.2, §6.5).
  //
  // v1.0.0 indexed these by rank — "the 3rd option" — so the same index meant
  // different cards in different states. Entity-indexing makes index k mean
  // "this card at this cost" forever, which is the property §6 requires of the
  // whole space.
  //
  // Still walks the option list rather than the action space: the list is <= 10
  // against 8,615 actions, and walking the space was a measured 39% of step
  // cost at Stage 4a.
  if (state.pending_choice.active()) {
    const PendingChoice& pc = state.pending_choice;
    for (int i = 0; i < pc.num_options; ++i) {
      mask[encode_action(ActionBlock::CardSelect,
                         static_cast<int>(pc.options[i].card_id), 0,
                         cost_column(state, pc.options[i]))] = true;
    }
    if (pc.is_optional) mask[encode_action(ActionBlock::Decline)] = true;
    return mask;
  }

  // Walk the HAND, not the whole action space: a card not in hand is illegal in
  // all of its target slots, and the hand is <= 10 cards against 400+ actions.
  // (The all-actions loop hashed CARD_DATABASE once per action — the dominant
  // per-step cost once the pool reached 80 cards.)
  for (const Card& c : state.current_hand) {
    const int card_idx = static_cast<int>(c.card_id);
    if (card_idx < 0 || card_idx >= num_card_ids) continue;
    // Each copy lights its OWN cost column, so a 0-cost and a 2-cost Bash are
    // two separate plays (§6.5).
    const int column = cost_column(state, c);
    for (int target = 0; target < kMaxEnemies; ++target) {
      const int action =
          encode_action(ActionBlock::Combat, card_idx, target, column);
      if (mask[action]) continue;  // same (card, column) seen, already decided
      mask[action] = card_action_is_legal(
          state, DecodedAction{ActionBlock::Combat, card_idx, target, column});
    }
  }

  // Potions (potion-effects.md §3.1). Walk the belt, not the 198 potion
  // actions; two copies of one potion light the same indices. A potion can
  // always be discarded; it can be drunk when potion_use_is_legal says so.
  for (PotionId p : state.potions) {
    const int id = static_cast<int>(p);
    mask[encode_action(ActionBlock::PotionDiscard, id)] = true;
    for (int target = 0; target < kMaxEnemies; ++target) {
      if (potion_use_is_legal(state, p, target)) {
        mask[encode_action(ActionBlock::PotionUse, id, target)] = true;
      }
    }
  }

  // End turn is always legal while in progress. Encoded, never computed as
  // `size - 1` — the last index is the decline action, not end-turn.
  mask[encode_action(ActionBlock::EndTurn)] = true;
  return mask;
}

namespace {

// Unceasing Top (relic-effects.md §6.26): "whenever you have no cards in hand
// during your turn, draw a card." StS checks it in CardGroup.refreshHandLayout,
// which GameActionManager calls each time its queue drains and control returns
// to the player — the moment apply_action returns here, with the queue empty by
// our standing invariant. Mid-effect empties (a card that discards and draws)
// can never trigger it, because no decision point falls inside a drain.
//
// The nine decompiled conditions, mapped:
//   queue empty            — always, at this point
//   hand empty             — the loop condition, tested FIRST
//   turn not ended         — character_turn; apply_action(EndTurn) runs the
//                            enemy turn and the next turn's start internally
//   canDraw latch          — false only before the first turn starts, where the
//                            one reachable decision point is a combat-start
//                            choice, already refused by the open-choice test
//   no No Draw             — can_draw
//   no screen up           — no pending choice
//   combat phase           — outcome in progress (also refreshHandLayout's own
//                            early return when every monster is dead)
//   !disabledUntilEndOfTurn— set by an end-of-turn autoplay (Burn, Doubt, …)
//                            and cleared at the next turn's start, a window with
//                            no decision point here; never observable
//   something to draw      — draw or discard pile non-empty
//
// A LOOP, because StS re-checks after every drain: a draw that left the hand
// empty again would draw again. No card in the pool does that today; the loop
// ends as soon as a card stays in hand or the piles run out.
void refresh_hand(CombatState& state) {
  // Hand first: this runs at the end of every apply_action, and the hand is
  // almost never empty, so the usual cost is this one comparison.
  while (state.current_hand.empty()) {
    if (state.outcome != Outcome::InProgress) return;
    if (!state.character_turn) return;
    if (state.pending_choice.active()) return;
    if (!state.has_relic(RelicId::UnceasingTop)) return;
    if (!can_draw(state)) return;
    if (state.draw_pile.empty() && state.discard_pile.empty()) return;

    // A real draw, through the queue, so CardDrawn listeners (Evolve, Fire
    // Breathing) fire. Fire Breathing deals damage, hence the death and
    // terminal checks the per-turn start also makes.
    ActionQueue q;
    ResolutionContext ctx;
    Action draw;
    draw.kind = ActionKind::DrawCards;
    draw.amount = 1;
    q.push_back(draw);
    drain(state, q, ctx);
    process_deaths(state, ctx);
    check_character_terminal(state);
    if (state.outcome != Outcome::InProgress) return;
    check_enemy_terminal(state);
  }
}

// Take one copy of `p` off the belt. Potions carry no per-copy state, so any
// copy is the copy.
void remove_one_potion(CombatState& state, PotionId p) {
  const auto it = std::find(state.potions.begin(), state.potions.end(), p);
  if (it != state.potions.end()) state.potions.erase(it);
}

// Drink a potion: StS's PotionPopUp order (potion-effects.md §2). The slot is
// freed FIRST, synchronously, before any queued effect runs — which is why an
// Entropic Brew refills its own slot. Then the potion's effects, then
// Hook::PotionDrunk (Toy Ornithopter), so the relic's heal resolves after the
// potion's own effects. Not a card: no energy, no card-played hooks.
void use_potion(CombatState& state, PotionId p, int target) {
  remove_one_potion(state, p);
  ActionQueue q;
  ResolutionContext ctx;
  push_potion_effects(state, p, target, q);
  fire_relic_hooks(state, Hook::PotionDrunk, q);
  drain(state, q, ctx);
  // Explosive and Fire Potion can kill; the on-death hooks fire before the
  // terminal checks, as after a card.
  process_deaths(state, ctx);
  check_character_terminal(state);
  if (state.outcome != Outcome::InProgress) return;
  check_enemy_terminal(state);
  // Smoke Bomb (§5.5). StS's escape takes 2.5 seconds, during which queued
  // actions — Toy Ornithopter's heal — still run, and then endBattle. Draining
  // first stands in for that, so the heal lands before the fight ends.
  if (p == PotionId::SmokeBomb && state.outcome == Outcome::InProgress) {
    state.outcome = Outcome::Escaped;
  }
}

// A replayed Rampage's ModifyDamageAction reaches every in-battle copy sharing
// its uuid (GetAllInBattleInstances, which searches every pile and limbo):
// the original, wherever it now is, and any sibling replay still waiting
// (#44). So Rampage + Double Tap plays 8 then 13 and leaves the original at
// 18; with Duplication too, the second replay plays at 18 and the original
// ends at 23.
//
// The engine has no uuid. The original is found by identity — id, bonus,
// upgrades — searching the pile it went to first, then the rest, in case a
// reshuffle moved it. Identical copies are interchangeable, so growing one
// that is not the original changes nothing anyone can see. Siblings share the
// replay's lineage number.
void spread_replay_growth(CombatState& state, const QueuedPlay& p,
                          const CardData& data) {
  auto same_identity = [&p](const Card& c) {
    return c.card_id == p.original.card_id &&
           c.bonus_damage == p.original.bonus_damage &&
           c.upgrades == p.original.upgrades;
  };
  std::vector<std::vector<Card>*> piles;
  if (p.original_pile == OriginPile::Discard) piles.push_back(&state.discard_pile);
  if (p.original_pile == OriginPile::Exhaust) piles.push_back(&state.exhaust_pile);
  for (std::vector<Card>* pile : {&state.current_hand, &state.draw_pile,
                                  &state.discard_pile, &state.exhaust_pile}) {
    piles.push_back(pile);
  }
  Card grown_original = p.original;
  bool found = false;
  for (std::vector<Card>* pile : piles) {
    for (Card& c : *pile) {
      if (!same_identity(c)) continue;
      grow_copy(c, data);
      grown_original = c;
      found = true;
      break;
    }
    if (found) break;
  }
  for (int i = 0; i < state.card_queue.size(); ++i) {
    QueuedPlay& sibling = state.card_queue.at(i);
    if (sibling.lineage != p.lineage) continue;
    grow_copy(sibling.instance, data);
    sibling.original = grown_original;
  }
}

// One card from the card queue — GameActionManager's card branch for a card
// the player did not pick (card-queue.md §3). It is played only if
// canUse(player, its target) holds: is_playable, and for a single-target card a
// target that is not dying (AbstractCard.cardPlayable). The target was rolled,
// or inherited, when the card was queued, and may have died since.
void resolve_queued_play(CombatState& state, const QueuedPlay& p) {
  const CardId id = p.instance.card_id;
  const CardData& data = CARD_DATABASE.at(id);
  const bool usable =
      is_playable(state, id) &&
      (!card_targets_enemy(data) ||
       (p.target >= 0 && p.target < static_cast<int>(state.enemies.size()) &&
        state.enemies[p.target].hp > 0));
  if (!usable) {
    // A replay is purgeOnUse: it simply vanishes.
    if (p.replay) return;
    // UseCardAction with dontTriggerOnUseCard: no effect, no on-play
    // triggers, only the pile move (potion-effects.md T5). It exhausts under
    // Havoc or by its own Exhaust and is discarded otherwise. An exhaust goes
    // through the ExhaustCard ACTION, the one place Feel No Pain, Dark Embrace,
    // Sentinel and Charon's Ashes fire (ROB-85), and Strange Spoon applies, as
    // it does in UseCardAction.
    bool exhausts = p.exhausts || data.exhaust;
    if (exhausts && strange_spoon_spares(state)) exhausts = false;
    ActionQueue q;
    ResolutionContext ctx;
    Action move;
    move.kind = exhausts ? ActionKind::ExhaustCard : ActionKind::DiscardCard;
    move.carry(p.instance);
    q.push_back(move);
    drain(state, q, ctx);
    process_deaths(state, ctx);  // Charon's Ashes can kill
    check_character_terminal(state);
    if (state.outcome != Outcome::InProgress) return;
    check_enemy_terminal(state);
    return;
  }
  PlayContext pc;
  pc.pay_energy = false;      // every queued card is a free play
  pc.take_from_hand = false;  // it is in limbo, not in hand
  pc.instance = p.instance;
  // A replay "uses the same value of X as the first"; a taken card uses the
  // energy read when it was taken, not what is left now (card-queue.md §3.2).
  pc.forced_x = p.x;
  if (p.replay) {
    pc.enters_pile = false;  // the original already went to its pile
  } else {
    pc.force_exhaust = p.exhausts;
  }
  handle_play_card(state, id, std::max(p.target, 0), pc);
  if (p.replay && p.lineage != 0 && data.bonus_damage_per_play > 0) {
    spread_replay_growth(state, p, data);
  }
}

bool apply_action_inner(CombatState& state, int action);

}  // namespace

void play_queued_cards(CombatState& state) {
  while (!state.card_queue.empty() && !state.pending_choice.active() &&
         state.outcome == Outcome::InProgress) {
    resolve_queued_play(state, state.card_queue.pop_front());
  }
  // A fight that ends with cards still waiting leaves them unplayed.
  if (state.outcome != Outcome::InProgress) state.card_queue.clear();
}

bool apply_action(CombatState& state, int action) {
  // Every exit of the action — a card, the end of the turn, a resolved choice
  // — returns control to the player here, which is where StS refreshes the
  // hand. One call rather than one per exit. The card queue is played out
  // first: StS reads it only once every action has run, and hands control
  // back only once it is empty (card-queue.md).
  if (!apply_action_inner(state, action)) return false;
  play_queued_cards(state);
  refresh_hand(state);
  return true;
}

namespace {

bool apply_action_inner(CombatState& state, int action) {
  if (state.outcome != Outcome::InProgress) return false;
  if (action < 0 || action >= kTotalActions) return false;

  // Decoded ONCE. From here on legality is a question about which BLOCK an
  // action is in — never about where one index sits relative to another. The
  // offset comparisons this replaced (`action > kEndTurnAction`) encoded "combat
  // is first and everything later is illegal", which would have silently
  // rejected map, shop and rest actions the moment those phases got producers.
  const DecodedAction d = decode_action(action);

  // A pending choice and normal combat are mutually exclusive: while a choice
  // is open, only a card selection or a decline is live. Every block is listed
  // rather than caught by a default, so wiring a new kind of choice is adding a
  // case, not discovering that a blanket "illegal" swallowed it.
  if (state.pending_choice.active()) {
    switch (d.block) {
      case ActionBlock::Decline:
        return resolve_choice(state, kDeclineChoice);
      case ActionBlock::CardSelect: {
        // Which offered option is this (card, cost column)? Options are one per
        // pair (build_choice), so at most one matches. The mask only lit pairs
        // that ARE on offer, so a miss means the caller ignored the mask —
        // refused, and resolve_choice never sees an index it cannot use.
        const PendingChoice& pc = state.pending_choice;
        for (int i = 0; i < pc.num_options; ++i) {
          if (pc.options[i].card_id == d.card() &&
              cost_column(state, pc.options[i]) == d.cost) {
            return resolve_choice(state, i);
          }
        }
        return false;
      }
      case ActionBlock::Combat:
      case ActionBlock::EndTurn:
      case ActionBlock::Map:
      case ActionBlock::RelicSelect:
      case ActionBlock::PotionUse:
      case ActionBlock::PotionDiscard:  // Q8: no potion action mid-choice
      case ActionBlock::EventOption:
      case ActionBlock::RestOption:
      case ActionBlock::Purpose:
      case ActionBlock::TakeMaxHp:
        return false;
    }
    return false;
  }

  switch (d.block) {
    case ActionBlock::Combat:
      // Validate just THIS action rather than building the whole mask (building
      // it here doubled the per-step mask cost). Shares card_action_is_legal
      // with valid_actions, so the two can't disagree.
      if (!card_action_is_legal(state, d)) return false;
      {
        PlayContext ctx;
        ctx.cost_column = d.cost;  // the copy this play names (§6.5)
        handle_play_card(state, d.card(), d.target, ctx);
      }
      return true;
    case ActionBlock::EndTurn:
      handle_end_turn(state);
      return true;
    case ActionBlock::PotionUse: {
      // Shares potion_use_is_legal with valid_actions, so the two can't
      // disagree.
      const PotionId p = static_cast<PotionId>(d.entity);
      if (!potion_use_is_legal(state, p, d.target)) return false;
      use_potion(state, p, d.target);
      return true;
    }
    case ActionBlock::PotionDiscard: {
      // Always available while the potion is held; it does nothing else.
      const PotionId p = static_cast<PotionId>(d.entity);
      if (!holds_potion(state, p)) return false;
      remove_one_potion(state, p);
      return true;
    }
    // Run-layer blocks. A CombatState has no map, shop, event or rest phase, so
    // none is ever live here. Enumerated for the same reason as above: a phase
    // that gains a producer becomes a new case, visibly.
    case ActionBlock::Map:
    case ActionBlock::CardSelect:
    case ActionBlock::RelicSelect:
    case ActionBlock::EventOption:
    case ActionBlock::RestOption:
    case ActionBlock::Purpose:
    case ActionBlock::TakeMaxHp:
    case ActionBlock::Decline:
      return false;
  }
  return false;
}

}  // namespace

}  // namespace minispire
