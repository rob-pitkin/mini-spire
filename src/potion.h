#ifndef MINISPIRE_POTION_H
#define MINISPIRE_POTION_H

#include <cstdint>
#include <vector>

namespace minispire {

// Every potion an Ironclad can hold. See docs/design/v2-spec.md §5.1.
//
// 33 — the Ironclad slice of sts_lightspeed's potionPool[4][33]. Unlike relics
// there is no spawn gating to reason about: potions are drawn from the class
// pool by rarity, and every one is reachable in Act 1 through combat drops and
// shops.
//
// (A grep for potion spawn-gating found nothing. That proves the pattern did
// not match rather than that no filter exists, so it is worth re-checking if a
// potion ever appears not to drop.)
enum class PotionId {
  // --- Common (17) ---
  BloodPotion, BlockPotion, DexterityPotion, EnergyPotion, ExplosivePotion,
  FirePotion, StrengthPotion, SwiftPotion, WeakPotion, FearPotion,
  AttackPotion, SkillPotion, PowerPotion, ColorlessPotion, FlexPotion,
  SpeedPotion, BlessingOfTheForge,

  // --- Uncommon (9) ---
  ElixirPotion, RegenPotion, AncientPotion, LiquidBronze, GamblersBrew,
  EssenceOfSteel, DuplicationPotion, DistilledChaos, LiquidMemories,

  // --- Rare (7) ---
  HeartOfIron, CultistPotion, FruitJuice, SneckoOil, FairyPotion, SmokeBomb,
  EntropicBrew,
};

inline constexpr int kNumPotions = 33;

enum class PotionRarity { Common, Uncommon, Rare };

// Shop price by rarity, from sts_lightspeed's potionRarityPrices.
inline constexpr int kPotionRarityPrices[] = {50, 75, 100};

// How many potions the player can carry. Potion Belt raises it by 2.
inline constexpr int kBasePotionSlots = 3;

PotionRarity potion_rarity(PotionId id);
const char* potion_name(PotionId id);

// Potions that need a target when drunk — the ones that hit one enemy.
bool potion_targets_enemy(PotionId id);

const std::vector<PotionId>& potion_pool(PotionRarity rarity);

}  // namespace minispire

#endif  // MINISPIRE_POTION_H
