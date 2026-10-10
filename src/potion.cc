#include "potion.h"

#include <array>

namespace minispire {
namespace {

struct PotionInfo {
  PotionRarity rarity;
  const char* name;
  // Drunk at a single enemy. The rest either affect the player or hit
  // everything, and asking them for a target would be a dead action index.
  bool targets_enemy;
  // StS's getPotency(ascension), before Sacred Bark doubles it. 0 where the
  // potion reads no potency (Blessing of the Forge, Elixir, Gambler's Brew,
  // Smoke Bomb) and for Entropic Brew, whose loop reads the slot count.
  int potency;
};

// Indexed by PotionId; order must match the enum, pinned by static_assert.
// Potencies from each decompiled potion's getPotency, cross-checked against
// wiki.gg's Potions_List (potion-effects.md §0).
constexpr std::array<PotionInfo, kNumPotions> kPotionInfo = {{
    // Common (17)
    {PotionRarity::Common, "Blood Potion", false, 20},  // % of max HP
    {PotionRarity::Common, "Block Potion", false, 12},
    {PotionRarity::Common, "Dexterity Potion", false, 2},
    {PotionRarity::Common, "Energy Potion", false, 2},
    {PotionRarity::Common, "Explosive Potion", false, 10},
    {PotionRarity::Common, "Fire Potion", true, 20},
    {PotionRarity::Common, "Strength Potion", false, 2},
    {PotionRarity::Common, "Swift Potion", false, 3},
    {PotionRarity::Common, "Weak Potion", true, 3},
    {PotionRarity::Common, "Fear Potion", true, 3},
    {PotionRarity::Common, "Attack Potion", false, 1},  // copies
    {PotionRarity::Common, "Skill Potion", false, 1},
    {PotionRarity::Common, "Power Potion", false, 1},
    {PotionRarity::Common, "Colorless Potion", false, 1},
    {PotionRarity::Common, "Flex Potion", false, 5},
    {PotionRarity::Common, "Speed Potion", false, 5},
    {PotionRarity::Common, "Blessing of the Forge", false, 0},
    // Uncommon (9)
    {PotionRarity::Uncommon, "Elixir", false, 0},
    {PotionRarity::Uncommon, "Regen Potion", false, 5},
    {PotionRarity::Uncommon, "Ancient Potion", false, 1},
    {PotionRarity::Uncommon, "Liquid Bronze", false, 3},
    {PotionRarity::Uncommon, "Gambler's Brew", false, 0},
    {PotionRarity::Uncommon, "Essence of Steel", false, 4},
    {PotionRarity::Uncommon, "Duplication Potion", false, 1},
    {PotionRarity::Uncommon, "Distilled Chaos", false, 3},
    {PotionRarity::Uncommon, "Liquid Memories", false, 1},
    // Rare (7)
    {PotionRarity::Rare, "Heart of Iron", false, 6},
    {PotionRarity::Rare, "Cultist Potion", false, 1},
    {PotionRarity::Rare, "Fruit Juice", false, 5},
    {PotionRarity::Rare, "Snecko Oil", false, 5},
    {PotionRarity::Rare, "Fairy in a Bottle", false, 30},  // % of max HP
    {PotionRarity::Rare, "Smoke Bomb", false, 0},
    {PotionRarity::Rare, "Entropic Brew", false, 0},
}};

static_assert(kPotionInfo.size() == kNumPotions,
              "kPotionInfo must cover every PotionId");
static_assert(static_cast<int>(PotionId::EntropicBrew) == kNumPotions - 1,
              "PotionId must end where kNumPotions says it does");

std::vector<PotionId> build_pool(PotionRarity rarity) {
  std::vector<PotionId> pool;
  for (int i = 0; i < kNumPotions; ++i) {
    if (kPotionInfo[i].rarity == rarity) pool.push_back(static_cast<PotionId>(i));
  }
  return pool;
}

}  // namespace

PotionRarity potion_rarity(PotionId id) {
  return kPotionInfo[static_cast<int>(id)].rarity;
}

const char* potion_name(PotionId id) {
  return kPotionInfo[static_cast<int>(id)].name;
}

bool potion_targets_enemy(PotionId id) {
  return kPotionInfo[static_cast<int>(id)].targets_enemy;
}

int potion_base_potency(PotionId id) {
  return kPotionInfo[static_cast<int>(id)].potency;
}

bool potion_effect_implemented(PotionId id) {
  switch (id) {
    // Step 1 (potion-effects.md §8): the potions built from existing
    // mechanisms, plus Speed (Dexterity Down), Regen and Cultist.
    case PotionId::BloodPotion:
    case PotionId::BlockPotion:
    case PotionId::DexterityPotion:
    case PotionId::EnergyPotion:
    case PotionId::ExplosivePotion:
    case PotionId::FirePotion:
    case PotionId::StrengthPotion:
    case PotionId::SwiftPotion:
    case PotionId::WeakPotion:
    case PotionId::FearPotion:
    case PotionId::FlexPotion:
    case PotionId::SpeedPotion:
    case PotionId::BlessingOfTheForge:
    case PotionId::RegenPotion:
    case PotionId::AncientPotion:
    case PotionId::LiquidBronze:
    case PotionId::EssenceOfSteel:
    case PotionId::HeartOfIron:
    case PotionId::CultistPotion:
    case PotionId::FruitJuice:
    // Step 2: the choice potions (§5.2, §5.7).
    case PotionId::AttackPotion:
    case PotionId::SkillPotion:
    case PotionId::PowerPotion:
    case PotionId::ColorlessPotion:
    case PotionId::ElixirPotion:
    case PotionId::GamblersBrew:
    case PotionId::LiquidMemories:
    // Step 3: the card-play potions (§1.2).
    case PotionId::DuplicationPotion:
    case PotionId::DistilledChaos:
    case PotionId::SneckoOil:
      return true;
    // Later steps. Fairy in a Bottle is never drinkable at all (§5.4), so it
    // stays false even once its death intercept lands.
    case PotionId::FairyPotion:
    case PotionId::SmokeBomb:
    case PotionId::EntropicBrew:
      return false;
  }
  return false;
}

const std::vector<PotionId>& potion_pool(PotionRarity rarity) {
  static const std::vector<PotionId> common = build_pool(PotionRarity::Common);
  static const std::vector<PotionId> uncommon =
      build_pool(PotionRarity::Uncommon);
  static const std::vector<PotionId> rare = build_pool(PotionRarity::Rare);

  switch (rarity) {
    case PotionRarity::Common: return common;
    case PotionRarity::Uncommon: return uncommon;
    case PotionRarity::Rare: return rare;
  }
  return common;
}

}  // namespace minispire
