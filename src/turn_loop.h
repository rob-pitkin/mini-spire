#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "combat_state.h"
#include "encounter.h"
#include "map.h"      // kMapWidth, kMapHeight — the map block's size
#include "potion.h"   // kNumPotions
#include "relic.h"    // kNumRelics
#include "status_effect.h"

namespace minispire {

constexpr int IRONCLAD_MAX_HP = 80;
constexpr int IRONCLAD_ENERGY_PER_TURN = 3;
constexpr int STARTING_HAND_SIZE = 5;
constexpr int HAND_SIZE_LIMIT = 10;

// Compute the actual damage dealt by an attack with the given base damage,
// given the attacker's status effects (Strength adds, Weak multiplies down)
// and defender's status effects (Vulnerable multiplies up). Float-internal,
// truncated once at the end. Returns max(result, 0).
//
// Exposed so the CLI / observation layer can display the *effective* enemy
// attack damage (Strength-modified, etc.) rather than the raw move.damage.
// `strength_mult` multiplies the attacker's Strength contribution (Heavy
// Blade's 3x/5x, Stage 4b); it defaults to the normal 1x.
// `vulnerable_mult` is how much extra a Vulnerable defender takes: 1.5
// normally, 1.75 when the PLAYER holds Paper Phrog. It is a parameter rather
// than a state lookup because this function takes status maps, not a
// CombatState, and is used for enemy attacks on the player too — where Paper
// Phrog must NOT apply. Callers use query.h's vulnerable_damage_multiplier for
// the player's own attacks and leave the default everywhere else.
int compute_attack_damage(
    int base, const std::unordered_map<Power, int>& attacker_powers,
    const std::unordered_map<Debuff, int>& attacker_debuffs,
    const std::unordered_map<Debuff, int>& defender_debuffs,
    int strength_mult = 1, float vulnerable_mult = 1.5f);

// The Ironclad starter deck: 5 Strike + 4 Defend + 1 Bash (unshuffled).
std::vector<Card> starter_deck();

// Everything a fight needs to exist. See docs/design/v2-spec.md §3.2.
//
// A parameter struct rather than a widening argument list, because the fight's
// inputs keep growing — hp, relics and potions now, curses and ascension later
// — and each addition would otherwise churn every call site and make the order
// of seven positional arguments load-bearing.
//
// More importantly, everything here must be present BEFORE the fight is built,
// not patched in afterwards. Setup reads this state: a start-of-combat heal
// works off `hp`, and a relic that changes the opening draw has to act before
// the hand is dealt. Assigning after construction means those read a fight that
// never existed.
struct CombatSetup {
  uint32_t seed = 0;
  // Seed for the fight's card-generation stream (§3.5, RngStream::CardRandom).
  // Defaulted to 0 rather than derived from `seed`, so a standalone CombatEnv
  // still generates deterministically; a run supplies its own per-floor seed.
  uint32_t card_seed = 0;
  EncounterPool pool = EncounterPool::Weak;
  std::vector<Card> deck;

  // The run's HP, not a fresh Ironclad's.
  int hp = IRONCLAD_MAX_HP;
  int max_hp = IRONCLAD_MAX_HP;

  // First-class combat state (§3.0.1), so the fight can show and use them.
  std::vector<HeldRelic> relics;
  std::vector<PotionId> potions;
};

// Constructs an initial CombatState (ROB-66): seeded RNG, the character at the
// given HP, an encounter sampled from `pool`, and `deck` shuffled into the draw
// pile. Draws the opening hand; enemy intents are primed by their factories.
//
// Takes the setup BY VALUE and moves out of it. Deck, relics and potions all go
// straight into the state, so a by-const-ref signature would copy the deck on
// every call — and CombatEnv::reset is on the reset-latency path this project
// benchmarks. Callers with a deck to spare should std::move into the setup.
CombatState start_combat(CombatSetup setup);

// v1.0.0's shape: a fresh 80/80 Ironclad with no relics or potions. Kept so a
// CombatEnv built the v1.0.0 way behaves exactly as it did (§3.0.1).
CombatState start_combat(uint32_t seed, EncounterPool pool,
                         std::vector<Card> deck);

// Backward-compatible v1 fixture: fixed single Jaw Worm + starter deck. Used by
// M1 / existing tests that want the deterministic Jaw Worm fight.
CombatState start_v1_combat(uint32_t seed);

// THE v2 ACTION SPACE (docs/design/v2-spec.md §6). Flat, masked, and ENTITY
// INDEXED: index k means the same thing in every state, forever — never "the
// k-th option offered".
//
// ┌──────────────────────┬───────┬──────────────────────────────────────────┐
// │ block                │  size │ index means                              │
// ├──────────────────────┼───────┼──────────────────────────────────────────┤
// │ combat: card×target  │ 1,350 │ play card c at enemy slot t              │
// │ end turn             │     1 │                                          │
// │ map: choose node     │   105 │ move to grid position p                  │
// │ card selection       │   270 │ pick card c — purpose from the phase     │
// │ relic selection      │   140 │ pick relic r — shop only                 │
// │ potion: use × target │   165 │ drink potion p at enemy slot t           │
// │ potion: discard      │    33 │                                          │
// │ event option         │    58 │ globally enumerated option id            │
// │ rest option          │     5 │ rest, smith, lift, toke, dig             │
// │ purpose selection    │     6 │ choose WHY a card list opens (§6.1)      │
// │ take Max HP instead  │     1 │ Singing Bowl                             │
// │ decline / skip       │     1 │                                          │
// └──────────────────────┴───────┴──────────────────────────────────────────┘
//                                  = 2,135
//
// The combat block is unchanged in POSITION (still index 0) but is now 270
// cards wide rather than 189 — the colorless and curse blocks landed. Every
// pre-existing card keeps its exact index, because those ids were appended.
//
// THE POSITIONAL OPTION-SLOT CHANNEL IS GONE (§6.2). v1.0.0 indexed a choice by
// RANK — "the 3rd card offered" — which meant the same index meant different
// things in different states. Card choices are now indexed by CardId, so the
// index IS the identity. decision-points.md §5.3's canonical slot ordering was
// a mitigation for rank-indexing and is moot: there are no positional slots
// left to order.
//
// Blocks past `end turn` are sized and reserved but not all wired — map, event
// and purpose have no producer yet, and their indices are permanently masked
// until they do. Reserved rather than appended later, because renumbering the
// action space after a policy trains against it is the expensive kind of
// change, and this is the last moment it is free.
inline constexpr int kNumMapNodes = kMapWidth * kMapHeight;  // 105
inline constexpr int kNumEventOptions = 58;                  // §6.3, counted
inline constexpr int kNumPurposes = 6;                       // §6.1
// The rest block's width. Declared here rather than reused from run_state.h's
// kNumRestOptions, because run_state.h includes THIS header — taking it from
// there would be circular. run_state.h static_asserts the two agree, so they
// cannot drift.
inline constexpr int kRestOptionBlockSize = 5;

inline constexpr int kCombatBlock = 0;
inline constexpr int kEndTurnAction = kNumCardTypes * kMaxEnemies;
inline constexpr int kMapBlock = kEndTurnAction + 1;
inline constexpr int kCardSelectBlock = kMapBlock + kNumMapNodes;
inline constexpr int kRelicSelectBlock = kCardSelectBlock + kNumCardTypes;
inline constexpr int kPotionUseBlock = kRelicSelectBlock + kNumRelics;
inline constexpr int kPotionDiscardBlock = kPotionUseBlock + kNumPotions * kMaxEnemies;
inline constexpr int kEventOptionBlock = kPotionDiscardBlock + kNumPotions;
inline constexpr int kRestOptionBlock = kEventOptionBlock + kNumEventOptions;
inline constexpr int kPurposeBlock = kRestOptionBlock + kRestOptionBlockSize;
inline constexpr int kTakeMaxHpAction = kPurposeBlock + kNumPurposes;
inline constexpr int kDeclineAction = kTakeMaxHpAction + 1;
inline constexpr int kTotalActions = kDeclineAction + 1;

static_assert(kTotalActions == 2135,
              "the v2 action space is 2,135 actions (v2-spec.md §6). If this "
              "fires, a block size changed — update the spec table too, do not "
              "just move the number.");

// Which region of the action space an index belongs to. Declaration order IS
// layout order — the table below is indexed by this enum.
enum class ActionBlock : uint8_t {
  Combat,
  EndTurn,
  Map,
  CardSelect,
  RelicSelect,
  PotionUse,
  PotionDiscard,
  EventOption,
  RestOption,
  Purpose,
  TakeMaxHp,
  Decline,
};
inline constexpr int kNumActionBlocks = 12;

struct ActionBlockSpan {
  int first;   // first action index of the block
  int size;    // indices the block occupies
  int stride;  // indices per entity: kMaxEnemies for the ×target blocks, else 1
};

// The layout as data, indexed by ActionBlock. Built from the k*Block constants
// above, which are themselves derived sequentially — so this adds no new
// arithmetic. It names what already exists, so that encode_action and
// decode_action have exactly one place to read the layout from.
inline constexpr std::array<ActionBlockSpan, kNumActionBlocks> kActionBlocks = {{
    {kCombatBlock, kNumCardTypes * kMaxEnemies, kMaxEnemies},
    {kEndTurnAction, 1, 1},
    {kMapBlock, kNumMapNodes, 1},
    {kCardSelectBlock, kNumCardTypes, 1},
    {kRelicSelectBlock, kNumRelics, 1},
    {kPotionUseBlock, kNumPotions * kMaxEnemies, kMaxEnemies},
    {kPotionDiscardBlock, kNumPotions, 1},
    {kEventOptionBlock, kNumEventOptions, 1},
    {kRestOptionBlock, kRestOptionBlockSize, 1},
    {kPurposeBlock, kNumPurposes, 1},
    {kTakeMaxHpAction, 1, 1},
    {kDeclineAction, 1, 1},
}};

// The blocks tile [0, kTotalActions) in order: each starts where the last
// ended, none is empty, and every size is a whole number of strides. Checked at
// compile time, so a table row that drifts from its constant cannot build.
constexpr bool action_blocks_tile_the_space() {
  int next = 0;
  for (const ActionBlockSpan& b : kActionBlocks) {
    if (b.first != next || b.size <= 0 || b.size % b.stride != 0) return false;
    next = b.first + b.size;
  }
  return next == kTotalActions;
}
static_assert(action_blocks_tile_the_space(),
              "kActionBlocks must tile the action space with no gaps or "
              "overlaps, in ActionBlock order");

struct DecodedAction {
  ActionBlock block;
  int entity;  // which card / map node / relic / potion / option / purpose
  int target;  // enemy slot for Combat and PotionUse; 0 for every other block

  // For the two card-indexed blocks, Combat and CardSelect.
  CardId card() const { return static_cast<CardId>(entity); }
};

// THE ONLY TWO FUNCTIONS ALLOWED TO ADD OR SUBTRACT A BLOCK OFFSET.
//
// Everything else — the mask, apply_action, the bindings, the TUI, the tests —
// goes through these. Offset arithmetic repeated at each call site is how
// v1.0.0 shipped `end-turn = size - 1` into the TUI and 13 Python tests at
// once: the layout knowledge re-derived wherever someone needed an index.
//
// Both are pure — no state — so the mask and the apply path cannot disagree.
// encode_action asserts `entity` and `target` are in range for their block.
int encode_action(ActionBlock block, int entity = 0, int target = 0);
DecodedAction decode_action(int action);

// Validity mask over the full action space. An action is legal iff the card is
// in hand AND affordable, AND — if the card targets an enemy — that target slot
// holds a living enemy; if it does not target an enemy (Defend), only the
// canonical target slot 0 is legal. End-turn is always legal while in progress.
std::vector<bool> valid_actions(const CombatState& state);

// How a card came to be played. A normal play pays energy and leaves the hand;
// re-entrant plays (Double Tap's free replay, Havoc playing off the draw pile)
// do neither. Modelled as a mode rather than duplicated functions so a card
// behaves identically however it was played.
struct PlayContext {
  bool pay_energy = true;      // false for a free replay
  bool take_from_hand = true;  // false when the card is already in flight
  bool force_exhaust = false;  // Havoc: the played card always exhausts
  bool enters_pile = true;     // false for Double Tap's second copy
  Card instance{CardId::Strike};  // the copy being played, when not from hand
  int forced_x = -1;           // reuse the first play's X (Double Tap)
};

// Resolve one card. Public because the PlayCard ACTION re-enters it: a
// meta-card (Double Tap, Havoc) pushes a PlayCard action rather than calling
// this directly, so the nested play is a flat queue step like everything else.
void handle_play_card(CombatState& state, CardId card_id, int target,
                      const PlayContext& ctx_play = PlayContext{});

// Apply a player action. Returns false (silently, no exception) if the
// action is invalid; state is not mutated in that case. Combat must be
// in progress (Outcome::InProgress). Debug builds may log on rejection.
bool apply_action(CombatState& state, int action);

}  // namespace minispire
