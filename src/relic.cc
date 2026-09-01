#include "relic.h"

#include <array>

namespace minispire {
namespace {

struct RelicInfo {
  RelicTier tier;
  const char* name;
};

// Indexed by RelicId. The order MUST match the enum — a static_assert below
// pins the size, and relic_tier() is the only reader, so a mismatch shows up as
// wrong tiers rather than a crash. Generated from the enum rather than typed,
// because 140 hand-transcribed rows is exactly where a silent error hides.
//
// Display names are the wiki's, with apostrophes restored where the enum
// cannot carry them.
constexpr std::array<RelicInfo, kNumRelics> kRelicInfo = {{
    // Starter
    {RelicTier::Starter, "Burning Blood"},
    // Common
    {RelicTier::Common, "Whetstone"},
    {RelicTier::Common, "The Boot"},
    {RelicTier::Common, "Blood Vial"},
    {RelicTier::Common, "Meal Ticket"},
    {RelicTier::Common, "Pen Nib"},
    {RelicTier::Common, "Akabeko"},
    {RelicTier::Common, "Lantern"},
    {RelicTier::Common, "Regal Pillow"},
    {RelicTier::Common, "Bag of Preparation"},
    {RelicTier::Common, "Ancient Tea Set"},
    {RelicTier::Common, "Smiling Mask"},
    {RelicTier::Common, "Potion Belt"},
    {RelicTier::Common, "Preserved Insect"},
    {RelicTier::Common, "Omamori"},
    {RelicTier::Common, "Maw Bank"},
    {RelicTier::Common, "Art of War"},
    {RelicTier::Common, "Toy Ornithopter"},
    {RelicTier::Common, "Ceramic Fish"},
    {RelicTier::Common, "Vajra"},
    {RelicTier::Common, "Centennial Puzzle"},
    {RelicTier::Common, "Strawberry"},
    {RelicTier::Common, "Happy Flower"},
    {RelicTier::Common, "Oddly Smooth Stone"},
    {RelicTier::Common, "War Paint"},
    {RelicTier::Common, "Bronze Scales"},
    {RelicTier::Common, "Juzu Bracelet"},
    {RelicTier::Common, "Dream Catcher"},
    {RelicTier::Common, "Nunchaku"},
    {RelicTier::Common, "Tiny Chest"},
    {RelicTier::Common, "Orichalcum"},
    {RelicTier::Common, "Anchor"},
    {RelicTier::Common, "Bag of Marbles"},
    {RelicTier::Common, "Red Skull"},
    // Uncommon
    {RelicTier::Uncommon, "Bottled Tornado"},
    {RelicTier::Uncommon, "Sundial"},
    {RelicTier::Uncommon, "Kunai"},
    {RelicTier::Uncommon, "Pear"},
    {RelicTier::Uncommon, "Blue Candle"},
    {RelicTier::Uncommon, "Eternal Feather"},
    {RelicTier::Uncommon, "Strike Dummy"},
    {RelicTier::Uncommon, "Singing Bowl"},
    {RelicTier::Uncommon, "Matryoshka"},
    {RelicTier::Uncommon, "Ink Bottle"},
    {RelicTier::Uncommon, "The Courier"},
    {RelicTier::Uncommon, "Frozen Egg"},
    {RelicTier::Uncommon, "Ornamental Fan"},
    {RelicTier::Uncommon, "Bottled Lightning"},
    {RelicTier::Uncommon, "Gremlin Horn"},
    {RelicTier::Uncommon, "Horn Cleat"},
    {RelicTier::Uncommon, "Toxic Egg"},
    {RelicTier::Uncommon, "Letter Opener"},
    {RelicTier::Uncommon, "Question Card"},
    {RelicTier::Uncommon, "Bottled Flame"},
    {RelicTier::Uncommon, "Shuriken"},
    {RelicTier::Uncommon, "Molten Egg"},
    {RelicTier::Uncommon, "Meat on the Bone"},
    {RelicTier::Uncommon, "Darkstone Periapt"},
    {RelicTier::Uncommon, "Mummified Hand"},
    {RelicTier::Uncommon, "Pantograph"},
    {RelicTier::Uncommon, "White Beast Statue"},
    {RelicTier::Uncommon, "Mercury Hourglass"},
    {RelicTier::Uncommon, "Self-Forming Clay"},
    {RelicTier::Uncommon, "Paper Phrog"},
    // Rare
    {RelicTier::Rare, "Ginger"},
    {RelicTier::Rare, "Old Coin"},
    {RelicTier::Rare, "Bird-Faced Urn"},
    {RelicTier::Rare, "Unceasing Top"},
    {RelicTier::Rare, "Torii"},
    {RelicTier::Rare, "Stone Calendar"},
    {RelicTier::Rare, "Shovel"},
    {RelicTier::Rare, "Wing Boots"},
    {RelicTier::Rare, "Thread and Needle"},
    {RelicTier::Rare, "Turnip"},
    {RelicTier::Rare, "Ice Cream"},
    {RelicTier::Rare, "Calipers"},
    {RelicTier::Rare, "Lizard Tail"},
    {RelicTier::Rare, "Prayer Wheel"},
    {RelicTier::Rare, "Girya"},
    {RelicTier::Rare, "Dead Branch"},
    {RelicTier::Rare, "Du-Vu Doll"},
    {RelicTier::Rare, "Pocketwatch"},
    {RelicTier::Rare, "Mango"},
    {RelicTier::Rare, "Incense Burner"},
    {RelicTier::Rare, "Gambling Chip"},
    {RelicTier::Rare, "Peace Pipe"},
    {RelicTier::Rare, "Captain's Wheel"},
    {RelicTier::Rare, "Fossilized Helix"},
    {RelicTier::Rare, "Tungsten Rod"},
    {RelicTier::Rare, "Magic Flower"},
    {RelicTier::Rare, "Charon's Ashes"},
    {RelicTier::Rare, "Champion Belt"},
    // Shop
    {RelicTier::Shop, "Sling of Courage"},
    {RelicTier::Shop, "Hand Drill"},
    {RelicTier::Shop, "Toolbox"},
    {RelicTier::Shop, "Chemical X"},
    {RelicTier::Shop, "Lee's Waffle"},
    {RelicTier::Shop, "Orrery"},
    {RelicTier::Shop, "Dolly's Mirror"},
    {RelicTier::Shop, "Orange Pellets"},
    {RelicTier::Shop, "Prismatic Shard"},
    {RelicTier::Shop, "Clockwork Souvenir"},
    {RelicTier::Shop, "Frozen Eye"},
    {RelicTier::Shop, "The Abacus"},
    {RelicTier::Shop, "Medical Kit"},
    {RelicTier::Shop, "Cauldron"},
    {RelicTier::Shop, "Strange Spoon"},
    {RelicTier::Shop, "Membership Card"},
    {RelicTier::Shop, "Brimstone"},
    // Boss
    {RelicTier::Boss, "Fusion Hammer"},
    {RelicTier::Boss, "Velvet Choker"},
    {RelicTier::Boss, "Runic Dome"},
    {RelicTier::Boss, "Slaver's Collar"},
    {RelicTier::Boss, "Snecko Eye"},
    {RelicTier::Boss, "Pandora's Box"},
    {RelicTier::Boss, "Cursed Key"},
    {RelicTier::Boss, "Busted Crown"},
    {RelicTier::Boss, "Ectoplasm"},
    {RelicTier::Boss, "Tiny House"},
    {RelicTier::Boss, "Sozu"},
    {RelicTier::Boss, "Philosopher's Stone"},
    {RelicTier::Boss, "Astrolabe"},
    {RelicTier::Boss, "Black Star"},
    {RelicTier::Boss, "Sacred Bark"},
    {RelicTier::Boss, "Empty Cage"},
    {RelicTier::Boss, "Runic Pyramid"},
    {RelicTier::Boss, "Calling Bell"},
    {RelicTier::Boss, "Coffee Dripper"},
    {RelicTier::Boss, "Mark of Pain"},
    {RelicTier::Boss, "Runic Cube"},
    // Special — granted by specific Act 1 events
    {RelicTier::Special, "Neow's Lament"},
    {RelicTier::Special, "Golden Idol"},
    {RelicTier::Special, "Odd Mushroom"},
    {RelicTier::Special, "Warped Tongs"},
    {RelicTier::Special, "Spirit Poop"},
    {RelicTier::Special, "Face of Cleric"},
    {RelicTier::Special, "Ssserpent Head"},
    {RelicTier::Special, "Gremlin Visage"},
    {RelicTier::Special, "N'loth's Hungry Face"},
    {RelicTier::Special, "Cultist Headpiece"},
}};

static_assert(kRelicInfo.size() == kNumRelics,
              "kRelicInfo must cover every RelicId");
static_assert(static_cast<int>(RelicId::CultistHeadpiece) == kNumRelics - 1,
              "RelicId must end where kNumRelics says it does");

// Built once, on first use, by walking kRelicInfo. Deriving the pools from the
// tier table rather than listing them twice means the two can never disagree.
std::vector<RelicId> build_pool(RelicTier tier) {
  std::vector<RelicId> pool;
  for (int i = 0; i < kNumRelics; ++i) {
    if (kRelicInfo[i].tier == tier) pool.push_back(static_cast<RelicId>(i));
  }
  return pool;
}

}  // namespace

RelicTier relic_tier(RelicId id) {
  return kRelicInfo[static_cast<int>(id)].tier;
}

const char* relic_name(RelicId id) {
  return kRelicInfo[static_cast<int>(id)].name;
}

const std::vector<RelicId>& relic_pool(RelicTier tier) {
  static const std::vector<RelicId> starter = build_pool(RelicTier::Starter);
  static const std::vector<RelicId> common = build_pool(RelicTier::Common);
  static const std::vector<RelicId> uncommon = build_pool(RelicTier::Uncommon);
  static const std::vector<RelicId> rare = build_pool(RelicTier::Rare);
  static const std::vector<RelicId> shop = build_pool(RelicTier::Shop);
  static const std::vector<RelicId> boss = build_pool(RelicTier::Boss);
  static const std::vector<RelicId> special = build_pool(RelicTier::Special);

  switch (tier) {
    case RelicTier::Starter: return starter;
    case RelicTier::Common: return common;
    case RelicTier::Uncommon: return uncommon;
    case RelicTier::Rare: return rare;
    case RelicTier::Shop: return shop;
    case RelicTier::Boss: return boss;
    case RelicTier::Special: return special;
  }
  return common;
}

}  // namespace minispire
