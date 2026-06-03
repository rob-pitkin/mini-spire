# Architecture

## Overview

Three strict layers with no logic leaking across boundaries:

```
┌─────────────────────────────────────┐
│  Python RL layer                    │
│  MaskablePPO · DQN · MCTS wrapper   │
└──────────────┬──────────────────────┘
               │ pybind11
┌──────────────▼──────────────────────┐
│  pybind11 boundary                  │
│  reset · step · action_mask · clone │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│  C++ game engine                    │
│  CombatState · Card · Enemy         │
│  TurnLoop · Renderer · RNG          │
└─────────────────────────────────────┘
```

## C++ types (v1 sketch)

### Card

```cpp
enum class CardId {
    Strike, Defend, Bash,
    // extend as card pool grows
};

struct Card {
    CardId id;
    std::string name;
    int energy_cost;
    bool upgraded;

    // Effect applied to state when played
    // Returns false if card cannot be played (cost, targeting)
    bool apply(CombatState& state) const;
};
```

### Enemy

```cpp
struct Move {
    enum class Type { Attack, Block, Buff } type;
    int value;           // damage or block amount
    std::string intent;  // display string
};

struct Enemy {
    std::string name;
    int hp;
    int max_hp;
    int block;
    std::vector<Move> pattern;  // cyclic intent list
    int move_index;             // current position in pattern

    const Move& current_intent() const;
    void advance_intent();
};
```

### CombatState

```cpp
struct CombatState {
    // Player
    int player_hp;
    int player_max_hp;
    int player_block;
    int energy;
    int energy_per_turn;

    // Card piles
    std::vector<Card> hand;
    std::vector<Card> draw_pile;
    std::vector<Card> discard_pile;
    std::vector<Card> exhaust_pile;

    // Enemy
    Enemy enemy;

    // Bookkeeping
    int turn;
    bool player_turn;  // false = enemy acting
    bool terminal;
    bool player_won;

    // RNG — seeded, deterministic
    std::mt19937 rng;
    uint32_t seed;

    // Deep copy — required for MCTS
    CombatState clone() const;

    // Shuffle draw pile using internal RNG
    void shuffle_draw_pile();

    // Draw N cards from draw pile (reshuffles discard if needed)
    void draw_cards(int n);
};
```

### TurnLoop

```cpp
class TurnLoop {
public:
    // Apply player action: card index in hand, or -1 for end turn
    // Returns false if action is invalid
    bool apply_action(CombatState& state, int action) const;

    // Get bitmask of valid actions for current state
    // Size: hand.size() + 1 (last entry = end turn)
    std::vector<bool> valid_actions(const CombatState& state) const;

    // Check and update terminal conditions
    void check_terminal(CombatState& state) const;

private:
    void enemy_turn(CombatState& state) const;
    void start_player_turn(CombatState& state) const;
};
```

## Observation space

Flat `float32` vector exposed to Python as a zero-copy numpy array.

```
Index range    Contents
───────────────────────────────────────────────────
[0]            player_hp / player_max_hp  (normalized)
[1]            player_block / player_max_hp
[2]            energy / energy_per_turn
[3..N+2]       hand: one-hot per card slot × num_card_types
               (N = max_hand_size × num_card_types, ~50 for v1)
[N+3..N+3+C]   draw pile: count per card type, normalized
[N+3+C..+C]    discard pile: count per card type, normalized
[..]           enemy_hp / enemy_max_hp
[..]           enemy_block / enemy_max_hp
[..]           intent: attack_dmg / 30, block_amt / 30, is_buff
───────────────────────────────────────────────────
Total v1: ~60–80 floats
```

Keep values normalized to [0, 1] — faster convergence with MLP policies.

## Action space

`Discrete(num_card_types + 1)`

- Actions 0..num_card_types-1: play a card of that type (if in hand, if affordable)
- Action num_card_types: end turn

Action masking bitmask returned by `action_mask()` — invalid actions are zeroed
in the policy logits by MaskablePPO before sampling.

**Why card type not card index?** Avoids the positional-encoding problem Miles ran
into with embedding layers. The agent learns what each card does, not where it sits
in the hand. Consistent with Miles's final design for the same reason.

## pybind11 module

```cpp
PYBIND11_MODULE(minispire_cpp, m) {
    py::class_<CombatEnv>(m, "CombatEnv")
        .def(py::init<>())
        .def("reset",       &CombatEnv::reset,       py::arg("seed") = 0)
        .def("step",        &CombatEnv::step,         py::arg("action"))
        .def("action_mask", &CombatEnv::action_mask)
        .def("clone",       &CombatEnv::clone)
        .def("render",      &CombatEnv::render);     // terminal ASCII
}
```

`reset()` returns a numpy array (zero-copy via buffer protocol).
`step()` returns `tuple[np.ndarray, float, bool, dict]`.

## State cloning for MCTS

```cpp
CombatState CombatState::clone() const {
    CombatState c = *this;         // shallow copy of value members
    c.hand = hand;                 // deep copy vectors
    c.draw_pile = draw_pile;
    c.discard_pile = discard_pile;
    c.exhaust_pile = exhaust_pile;
    c.rng = rng;                   // copy RNG state exactly
    return c;
}
```

MCTS uses clone to branch the search tree without mutating the real state.
The copied RNG means rollouts are deterministic from any node.

## Reward design

```
terminal win:   R = 1.0 + (player_hp / player_max_hp) * 0.5   (optional HP bonus)
terminal loss:  R = -1.0
non-terminal:   R = 0.0
```

HP shaping is optional — start without it and add only if the sparse signal
produces too slow learning on the M1. Avoid intermediate rewards (e.g. +0.1 for
dealing damage) as they bias toward aggression regardless of context.

## Renderer (terminal)

Not used by the agent. Used by the developer to watch episodes and verify logic.

```
═══════════════════════════════
  Mini-Spire  |  Turn 3
═══════════════════════════════
  Player  HP: ████████░░  42/80    Block: 5
  Enemy   HP: ███░░░░░░░  18/48    [ATTACK 6]
───────────────────────────────
  Hand:  [Strike x2] [Defend] [Bash]
  Draw:  6   Discard: 2   Energy: 2/3
───────────────────────────────
  > action: _
```

## Key design decisions

**Single combat only (v1):** Keeps the scope achievable. The full game adds
~10x complexity without proportionally increasing the RL research interest of v1.

**Terminal renderer, no GUI:** No SDL/SFML/Qt dependency. The engine stays a
pure library; rendering is a thin layer on top. Agent training never touches it.

**Action masking from day one:** Unmasked invalid actions cause agents to learn
"end turn always" as a degenerate policy. MaskablePPO from sb3-contrib handles
this with one flag.

**CombatState::clone() from day one:** MCTS requires branching state without
mutation. Retrofitting clone() after the fact is painful — every new field needs
to be manually added. Build it first, keep it updated.

**Card type actions, not hand position:** Miles struggled with positional
embeddings for the hand. Indexing by card type sidesteps this — the agent always
knows what a Strike does regardless of where it appears in hand.

**Seeded deterministic RNG in state:** Required for reproducibility. Any game
state can be serialized (seed + action history) and replayed exactly.
