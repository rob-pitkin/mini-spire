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
};

// Indexed by PotionId; order must match the enum, pinned by static_assert.
constexpr std::array<PotionInfo, kNumPotions> kPotionInfo = {{
    // Common (17)
    {PotionRarity::Common, "Blood Potion", false},
    {PotionRarity::Common, "Block Potion", false},
    {PotionRarity::Common, "Dexterity Potion", false},
    {PotionRarity::Common, "Energy Potion", false},
    {PotionRarity::Common, "Explosive Potion", false},
    {PotionRarity::Common, "Fire Potion", true},
    {PotionRarity::Common, "Strength Potion", false},
    {PotionRarity::Common, "Swift Potion", false},
    {PotionRarity::Common, "Weak Potion", true},
    {PotionRarity::Common, "Fear Potion", true},
    {PotionRarity::Common, "Attack Potion", false},
    {PotionRarity::Common, "Skill Potion", false},
    {PotionRarity::Common, "Power Potion", false},
    {PotionRarity::Common, "Colorless Potion", false},
    {PotionRarity::Common, "Flex Potion", false},
    {PotionRarity::Common, "Speed Potion", false},
    {PotionRarity::Common, "Blessing of the Forge", false},
    // Uncommon (9)
    {PotionRarity::Uncommon, "Elixir", false},
    {PotionRarity::Uncommon, "Regen Potion", false},
    {PotionRarity::Uncommon, "Ancient Potion", false},
    {PotionRarity::Uncommon, "Liquid Bronze", false},
    {PotionRarity::Uncommon, "Gambler's Brew", false},
    {PotionRarity::Uncommon, "Essence of Steel", false},
    {PotionRarity::Uncommon, "Duplication Potion", false},
    {PotionRarity::Uncommon, "Distilled Chaos", false},
    {PotionRarity::Uncommon, "Liquid Memories", false},
    // Rare (7)
    {PotionRarity::Rare, "Heart of Iron", false},
    {PotionRarity::Rare, "Cultist Potion", false},
    {PotionRarity::Rare, "Fruit Juice", false},
    {PotionRarity::Rare, "Snecko Oil", false},
    {PotionRarity::Rare, "Fairy in a Bottle", false},
    {PotionRarity::Rare, "Smoke Bomb", false},
    {PotionRarity::Rare, "Entropic Brew", false},
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
