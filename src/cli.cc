#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "card.h"
#include "combat_state.h"
#include "enemy.h"
#include "status_effect.h"
#include "turn_loop.h"

namespace minispire::cli {

// ============================================================================
// Static name lookups — debug strings for cards, statuses, moves.
// ============================================================================

const char* card_name(CardId id) {
  switch (id) {
    case CardId::Strike: return "Strike";
    case CardId::StrikePlus: return "Strike+";
    case CardId::Defend: return "Defend";
    case CardId::DefendPlus: return "Defend+";
    case CardId::Bash: return "Bash";
    case CardId::BashPlus: return "Bash+";
  }
  return "?";
}

const char* status_name(StatusEffect s) {
  switch (s) {
    case StatusEffect::Vulnerable: return "Vulnerable";
    case StatusEffect::Weak: return "Weak";
    case StatusEffect::Strength: return "Strength";
    case StatusEffect::Dexterity: return "Dexterity";
  }
  return "?";
}

const char* move_name(MoveName m) {
  switch (m) {
    case MoveName::Chomp: return "Chomp";
    case MoveName::Thrash: return "Thrash";
    case MoveName::Bellow: return "Bellow";
  }
  return "?";
}

const char* enemy_kind_name(EnemyKind k) {
  switch (k) {
    case EnemyKind::JawWorm: return "JawWorm";
  }
  return "?";
}

// ============================================================================
// Hand-rolled JSON writing helpers.
// ============================================================================

std::string json_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 2);
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += c; break;
    }
  }
  return out;
}

std::string status_effects_json(const std::unordered_map<StatusEffect, int>& effects) {
  std::ostringstream os;
  os << "{";
  bool first = true;
  for (const auto& [effect, stacks] : effects) {
    if (!first) os << ",";
    first = false;
    os << "\"" << status_name(effect) << "\":" << stacks;
  }
  os << "}";
  return os.str();
}

std::string hand_json(const std::vector<Card>& hand) {
  std::ostringstream os;
  os << "[";
  for (std::size_t i = 0; i < hand.size(); ++i) {
    if (i > 0) os << ",";
    os << "\"" << card_name(hand[i].card_id) << "\"";
  }
  os << "]";
  return os.str();
}

const char* outcome_name(Outcome o) {
  switch (o) {
    case Outcome::InProgress: return "InProgress";
    case Outcome::Won: return "Won";
    case Outcome::Lost: return "Lost";
  }
  return "?";
}

void log_state(std::ostream& log, const CombatState& state) {
  if (!log) return;
  const Character& c = state.character;
  const Enemy& e = state.enemies[0];

  std::ostringstream os;
  os << "{\"event\":\"state\""
     << ",\"turn\":" << state.turn_number
     << ",\"character_turn\":" << (state.character_turn ? "true" : "false")
     << ",\"character\":{"
     << "\"hp\":" << c.hp
     << ",\"max_hp\":" << c.max_hp
     << ",\"block\":" << c.current_block
     << ",\"energy\":" << c.energy
     << ",\"energy_per_turn\":" << c.energy_per_turn
     << ",\"status_effects\":" << status_effects_json(c.status_effects)
     << "}";

  os << ",\"enemy\":{"
     << "\"kind\":\"" << enemy_kind_name(e.kind) << "\""
     << ",\"hp\":" << e.hp
     << ",\"max_hp\":" << e.max_hp
     << ",\"block\":" << e.current_block
     << ",\"status_effects\":" << status_effects_json(e.status_effects);
  if (e.last_move.has_value()) {
    os << ",\"intent\":\"" << move_name(*e.last_move) << "\"";
  } else {
    os << ",\"intent\":null";
  }
  os << "}";

  os << ",\"hand\":" << hand_json(state.current_hand)
     << ",\"draw_pile_size\":" << state.draw_pile.size()
     << ",\"discard_pile_size\":" << state.discard_pile.size()
     << ",\"exhaust_pile_size\":" << state.exhaust_pile.size()
     << ",\"outcome\":\"" << outcome_name(state.outcome) << "\""
     << "}\n";

  log << os.str();
}

void log_action(std::ostream& log, int turn_number, int global_action,
                int num_card_ids) {
  if (!log) return;
  std::ostringstream os;
  os << "{\"event\":\"action\",\"turn\":" << turn_number;
  if (global_action == num_card_ids) {
    os << ",\"action_type\":\"end_turn\"";
  } else {
    os << ",\"action_type\":\"play_card\",\"card\":\""
       << card_name(static_cast<CardId>(global_action)) << "\"";
  }
  os << "}\n";
  log << os.str();
}

// ============================================================================
// Rendering.
// ============================================================================

constexpr int HP_BAR_WIDTH = 18;

std::string hp_bar(int hp, int max_hp) {
  if (max_hp <= 0) return std::string(HP_BAR_WIDTH, '-');
  int filled = (hp * HP_BAR_WIDTH + max_hp - 1) / max_hp;  // ceil
  if (filled < 0) filled = 0;
  if (filled > HP_BAR_WIDTH) filled = HP_BAR_WIDTH;
  return std::string(filled, '#') + std::string(HP_BAR_WIDTH - filled, '-');
}

std::string status_list(const std::unordered_map<StatusEffect, int>& effects) {
  if (effects.empty()) return "";
  std::ostringstream os;
  bool first = true;
  for (const auto& [effect, stacks] : effects) {
    if (!first) os << ", ";
    first = false;
    os << status_name(effect) << "(" << stacks << ")";
  }
  return os.str();
}

// Format the enemy's intent string: e.g. "Thrash  (7 atk + 5 blk)".
// The displayed attack damage reflects the actual damage that will be dealt
// once Strength / Weak / Vulnerable are applied, matching STS behavior where
// intent numbers always show the final value.
std::string format_intent(const Enemy& e, const Character& c) {
  if (!e.last_move.has_value()) return "(none)";
  MoveName name = *e.last_move;
  auto it = e.moves.find(name);
  if (it == e.moves.end()) return move_name(name);
  const Move& m = it->second;

  std::ostringstream os;
  os << move_name(name);

  bool has_atk = m.damage > 0;
  bool has_blk = m.block > 0;
  bool has_buff = false;
  std::ostringstream buff_part;
  for (const auto& app : m.applies) {
    if (has_buff) buff_part << ", ";
    has_buff = true;
    buff_part << "gains " << status_name(app.effect) << " " << app.amount;
  }

  if (has_atk || has_blk || has_buff) {
    os << "  (";
    bool first = true;
    if (has_atk) {
      // Compute the effective attack damage so the player sees what the
      // enemy will actually deal, accounting for enemy Strength / Weak and
      // character Vulnerable. Block is shown raw because Dexterity does not
      // apply to enemy block in STS (it's a character-only relic effect).
      int displayed =
          compute_attack_damage(m.damage, e.status_effects, c.status_effects);
      os << displayed << " atk";
      first = false;
    }
    if (has_blk) {
      if (!first) os << " + ";
      os << m.block << " blk";
      first = false;
    }
    if (has_buff) {
      if (!first) os << " + ";
      os << buff_part.str();
    }
    os << ")";
  }
  return os.str();
}

void render_header(int turn) {
  std::cout << "================================ MINI-SPIRE ================================\n";
  std::cout << "                                  Turn  " << turn << "\n\n";
}

// Two-column layout: each column has a stable label area, value area, and
// detail area. Widths chosen so HP bar + numeric value comfortably fits.
//   "  HP   [<18-bar>]  ddd / ddd " = 2 + 5 + 1 + 18 + 1 + 2 + 3 + 3 + 3 + 1 = 39
// Padded to 44 for breathing room between columns.
constexpr int COL_WIDTH = 44;

// Render one entity's HP row (label + bar + value).
std::string fmt_hp_row(int hp, int max_hp, const char* label) {
  int clamped_hp = hp < 0 ? 0 : hp;
  int clamped_max = max_hp < 0 ? 0 : max_hp;
  std::ostringstream os;
  os << "  " << label << "  [" << hp_bar(clamped_hp, clamped_max) << "]  "
     << std::setw(3) << clamped_hp << " / " << std::setw(3) << clamped_max;
  return os.str();
}

// Render one entity's block row. Pips truncate at 10; numeric value is always
// shown. When block is 0, blank pip area.
std::string fmt_block_row(int block, const char* label) {
  int clamped = block < 0 ? 0 : block;
  int pip_count = clamped > 10 ? 10 : clamped;
  std::string pips(pip_count, '#');
  std::ostringstream os;
  os << "  " << label << "  " << std::setw(10) << std::left << pips
     << std::right << "  " << std::setw(3) << clamped;
  return os.str();
}

// Render the energy row. Pips ("*  *  *") wrap at 10 (rare).
std::string fmt_energy_row(int energy, int per_turn) {
  std::string pips;
  int n = energy < 0 ? 0 : (energy > 10 ? 10 : energy);
  for (int i = 0; i < n; ++i) {
    if (i > 0) pips += "  ";
    pips += "*";
  }
  std::ostringstream os;
  os << "  NRG  " << std::setw(10) << std::left << pips
     << std::right << "  " << energy << " / " << per_turn;
  return os.str();
}

// Pad a string to COL_WIDTH characters (right-pad with spaces).
std::string pad_col(const std::string& s) {
  if (static_cast<int>(s.size()) >= COL_WIDTH) return s;
  return s + std::string(COL_WIDTH - s.size(), ' ');
}

// Single-character avatar tokens, roguelike-style.
char character_token() {
  return '@';
}

char enemy_token(EnemyKind k) {
  switch (k) {
    case EnemyKind::JawWorm: return 'W';
  }
  return '?';
}

// Build a column-aligned row with a single character "centered" under the
// label. Centering uses the label start offset so the token visually sits
// over the label name rather than at the absolute column midpoint.
std::string fmt_avatar_row(char token, const char* label) {
  // Match the indent of the labels in the header row: "  IRONCLAD" starts
  // at col 2; the label center is col 2 + len/2. Place token at that
  // center, padded with spaces so the row aligns with the column.
  int label_len = static_cast<int>(std::char_traits<char>::length(label));
  int token_col = 2 + label_len / 2;
  std::string row(token_col, ' ');
  row.push_back(token);
  return row;
}

// Renders the IRONCLAD / JAW WORM two-column block.
void render_entities(const Character& c, const Enemy& e) {
  // Avatar row
  std::string left_avatar = fmt_avatar_row(character_token(), "IRONCLAD");
  std::string right_avatar = fmt_avatar_row(enemy_token(e.kind), enemy_kind_name(e.kind));
  std::cout << pad_col(left_avatar) << right_avatar << "\n";

  // Header row
  std::string left_h = "  IRONCLAD";
  std::string right_h = std::string("  ") + enemy_kind_name(e.kind);
  std::cout << pad_col(left_h) << right_h << "\n";

  // HP row
  std::cout << pad_col(fmt_hp_row(c.hp, c.max_hp, "HP "))
            << fmt_hp_row(e.hp, e.max_hp, "HP ") << "\n";

  // Block row
  std::cout << pad_col(fmt_block_row(c.current_block, "BLK"))
            << fmt_block_row(e.current_block, "BLK") << "\n";

  // Energy row on left; status / intent on right.
  std::string left_nrg = fmt_energy_row(c.energy, c.energy_per_turn);
  std::string right_first;
  std::string e_status = status_list(e.status_effects);
  if (!e_status.empty()) {
    right_first = "  " + e_status;
  } else {
    right_first = "  Intent: " + format_intent(e, c);
  }
  std::cout << pad_col(left_nrg) << right_first << "\n";

  // If we showed status on the right first row, show intent on the next row.
  if (!e_status.empty()) {
    std::cout << pad_col("") << "  Intent: " << format_intent(e, c) << "\n";
  }

  // Character statuses, if any
  std::string c_status = status_list(c.status_effects);
  if (!c_status.empty()) {
    std::cout << "  Statuses: " << c_status << "\n";
  }
}

void render_divider() {
  std::cout << "----------------------------------------------------------------------------\n";
}

// Result of building the action-key map. Maps local CLI index -> global action.
struct ActionMap {
  std::vector<int> global_actions;  // local index -> global action id
  int end_turn_local;               // local index for end-turn
};

// Build the local-to-global action mapping for the hand.
ActionMap build_action_map(const CombatState& state) {
  ActionMap am;
  for (std::size_t i = 0; i < state.current_hand.size(); ++i) {
    CardId id = state.current_hand[i].card_id;
    const CardData& data = CARD_DATABASE.at(id);
    if (state.character.energy >= data.cost) {
      am.global_actions.push_back(static_cast<int>(id));
    }
  }
  am.end_turn_local = static_cast<int>(am.global_actions.size());
  return am;
}

// Render the HAND block. Playable cards get local action indices; unplayable
// cards get a "- " prefix.
void render_hand(const CombatState& state, const ActionMap& am) {
  std::cout << "  HAND\n";
  int local_idx = 0;
  std::ostringstream line;
  int per_line = 0;
  for (std::size_t i = 0; i < state.current_hand.size(); ++i) {
    CardId id = state.current_hand[i].card_id;
    const CardData& data = CARD_DATABASE.at(id);
    bool playable = state.character.energy >= data.cost;

    std::ostringstream entry;
    if (playable) {
      entry << "(" << local_idx << ") " << std::setw(8) << std::left
            << card_name(id) << " {" << data.cost << "}";
      ++local_idx;
    } else {
      entry << "  - " << std::setw(8) << std::left
            << card_name(id) << " {" << data.cost << "}";
    }

    if (per_line == 0) {
      line << "   " << entry.str();
    } else {
      line << "        " << entry.str();
    }
    ++per_line;

    if (per_line == 3) {
      std::cout << line.str() << "\n";
      line.str("");
      line.clear();
      per_line = 0;
    }
  }
  if (per_line > 0) {
    std::cout << line.str() << "\n";
  }
  if (state.current_hand.empty()) {
    std::cout << "   (empty)\n";
  }

  (void)am;
}

void render_piles_summary(const CombatState& state) {
  std::cout << "  Draw " << state.draw_pile.size()
            << "   Discard " << state.discard_pile.size()
            << "   Exhaust " << state.exhaust_pile.size() << "\n";
}

void render_pile_list(const char* label, std::size_t count,
                      const std::vector<Card>& pile, bool top_first) {
  std::cout << "  " << label << " (" << count;
  if (top_first) std::cout << ", top first";
  std::cout << "):\n";
  if (pile.empty()) {
    std::cout << "    (empty)\n";
    return;
  }
  // The "top" of draw is the back of the vector (we pop_back in draw_one).
  if (top_first) {
    for (auto it = pile.rbegin(); it != pile.rend(); ++it) {
      const CardData& data = CARD_DATABASE.at(it->card_id);
      std::cout << "    " << std::setw(8) << std::left << card_name(it->card_id)
                << " {" << data.cost << "}\n";
    }
  } else {
    for (const Card& c : pile) {
      const CardData& data = CARD_DATABASE.at(c.card_id);
      std::cout << "    " << std::setw(8) << std::left << card_name(c.card_id)
                << " {" << data.cost << "}\n";
    }
  }
}

void render_pile_view(const CombatState& state) {
  std::cout << "  PILES\n\n";
  render_pile_list("DRAW", state.draw_pile.size(), state.draw_pile, true);
  std::cout << "\n";
  render_pile_list("DISCARD", state.discard_pile.size(), state.discard_pile, false);
  std::cout << "\n";
  render_pile_list("EXHAUST", state.exhaust_pile.size(), state.exhaust_pile, false);
}

void render_prompt_normal(const ActionMap& am) {
  std::cout << "\n  Action: ";
  if (am.global_actions.empty()) {
    std::cout << "(no playable cards)   ";
  } else if (am.global_actions.size() == 1) {
    std::cout << "(0) play   ";
  } else {
    std::cout << "(0.." << am.global_actions.size() - 1 << ") play   ";
  }
  std::cout << "(" << am.end_turn_local << ") end turn   "
            << "(p) pile view   (q) quit\n  > " << std::flush;
}

void render_prompt_pile() {
  std::cout << "\n  (p) hand view   (q) quit\n  > " << std::flush;
}

void clear_screen() {
  std::cout << "\x1b[2J\x1b[H";
}

void render(const CombatState& state, bool pile_view, const ActionMap& am) {
  clear_screen();
  render_header(state.turn_number);
  render_entities(state.character, state.enemies[0]);
  std::cout << "\n";
  render_divider();
  if (pile_view) {
    render_pile_view(state);
    render_divider();
    render_prompt_pile();
  } else {
    render_hand(state, am);
    std::cout << "\n";
    render_divider();
    render_piles_summary(state);
    render_prompt_normal(am);
  }
}

// ============================================================================
// End-of-fight screen.
// ============================================================================

void render_terminal_screen(const CombatState& state, const std::string& log_path) {
  clear_screen();
  render_header(state.turn_number);

  const char* banner = (state.outcome == Outcome::Won) ? "*** YOU WIN ***" : "*** YOU LOSE ***";
  std::cout << "                            " << banner << "\n\n";

  const Character& c = state.character;
  const Enemy& e = state.enemies[0];
  render_entities(c, e);

  std::cout << "\n  Combat lasted " << state.turn_number << " turns.\n";
  if (!log_path.empty()) {
    std::cout << "  Trajectory saved to " << log_path << "\n";
  }
  std::cout << "\n  (press enter to exit)\n  > " << std::flush;
}

// ============================================================================
// Logging filesystem setup.
// ============================================================================

std::string iso8601_timestamp() {
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::ostringstream os;
  os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
  return os.str();
}

std::string open_log_path(uint32_t seed) {
  std::filesystem::path dir = "logs";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) {
    std::cerr << "warning: could not create logs/ directory: " << ec.message() << "\n";
    return "";
  }
  std::ostringstream filename;
  filename << iso8601_timestamp() << "-seed" << seed << ".jsonl";
  return (dir / filename.str()).string();
}

// ============================================================================
// Main loop.
// ============================================================================

uint32_t parse_seed_or_default(int argc, char** argv) {
  if (argc < 2) return 0;
  try {
    long v = std::stol(argv[1]);
    if (v < 0) {
      std::cerr << "warning: negative seed not allowed; using 0\n";
      return 0;
    }
    return static_cast<uint32_t>(v);
  } catch (...) {
    std::cerr << "warning: could not parse seed \"" << argv[1] << "\"; using 0\n";
    return 0;
  }
}

}  // namespace minispire::cli

int main(int argc, char** argv) {
  using namespace minispire;
  using namespace minispire::cli;

  uint32_t seed = parse_seed_or_default(argc, argv);

  std::string log_path = open_log_path(seed);
  std::ofstream log;
  if (!log_path.empty()) {
    log.open(log_path);
    if (!log) {
      std::cerr << "warning: could not open log file " << log_path
                << "; continuing without logging\n";
      log_path.clear();
    }
  }

  CombatState state = start_v1_combat(seed);
  bool pile_view = false;

  const int num_card_ids = static_cast<int>(CARD_DATABASE.size());

  while (state.outcome == Outcome::InProgress) {
    log_state(log, state);

    ActionMap am = build_action_map(state);
    render(state, pile_view, am);

    std::string input;
    if (!std::getline(std::cin, input)) {
      return 2;  // EOF = quit
    }

    // Trim leading/trailing whitespace.
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    input.erase(input.begin(), std::find_if(input.begin(), input.end(), not_space));
    input.erase(std::find_if(input.rbegin(), input.rend(), not_space).base(), input.end());

    if (input == "q" || input == "Q") return 2;
    if (input == "p" || input == "P") {
      pile_view = !pile_view;
      continue;
    }
    if (pile_view) continue;
    if (input.empty()) continue;

    // Parse integer.
    int local_action;
    try {
      std::size_t consumed = 0;
      local_action = std::stoi(input, &consumed);
      if (consumed != input.size()) continue;
    } catch (...) {
      continue;
    }

    int global_action;
    if (local_action == am.end_turn_local) {
      global_action = num_card_ids;  // end turn
    } else if (local_action >= 0 &&
               local_action < static_cast<int>(am.global_actions.size())) {
      global_action = am.global_actions[local_action];
    } else {
      continue;  // out of range
    }

    int turn_at_action = state.turn_number;
    bool ok = apply_action(state, global_action);
    if (!ok) continue;
    log_action(log, turn_at_action, global_action, num_card_ids);
  }

  log_state(log, state);

  render_terminal_screen(state, log_path);
  std::string discard;
  std::getline(std::cin, discard);

  return state.outcome == Outcome::Won ? 0 : 1;
}
