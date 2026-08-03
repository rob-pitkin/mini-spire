# Run-level reward design (v2.0.0)

**Status: ACCEPTED** (2026-08-03). The shape below is settled; the open
questions in §7 are parameter choices, not structural ones.

Scope: this is about v2.0.0, where **an episode is one run** rather than one
fight. v1.0.0's fight-scoped reward (`+1` win / `-1` loss, plus the optional
terminal `hp_reward_coeff` bonus) is unaffected and stays as it is.

---

## 1. The problem

An episode becomes ~50 floors instead of ~18 steps. The obvious reward — "+1 if
you clear the act" — is a single scalar at the end of a very long trajectory,
and that does not train.

That is not a guess. Every comparable project hit it:

| project | episode | how deck decisions were made | what went wrong |
|---|---|---|---|
| [Miles Oram](https://milesoram.github.io/slay-the-spire-ml-project.html) | full run, 4 acts | separate *macro* network + simulate-and-rollback search, not learned with combat | "macro/micro separation preventing consideration of run context" |
| [toypiper](https://www.toypiper.com/creating-an-ai-for-slay-the-spire/) | started at a full dungeon (~15 encounters), **retreated to single combat** | never reached | sparse reward over a long episode. A step penalty meant to speed play taught the agent to die immediately. |
| [PokéRL](https://arxiv.org/abs/2604.10812) | scoped to early-game tasks, not the full game | n/a | needed dense *hierarchical* rewards plus anti-loop machinery, and still reports training as brittle |

**Nobody trained a long-horizon episode on a sparse terminal reward.** Each
project shaped densely, scoped the episode down, or split the problem across
networks. A run-length episode is the right target; it needs help to be
learnable, and that help is what this document specifies.

Miles's failure is also the opportunity. His deck-building was never learned —
it was tree search over simulated outcomes, in a separate model that could not
see run context. Mini-spire's option-slot channel
(`decision-points.md`) already carries heterogeneous "pick one of N" decisions
in a fixed-shape action space; a card reward is that same shape. So **one policy
over combat and deck-building** is reachable here in a way it was not for him.

## 2. Why not just reward HP

HP is a **resource, not an objective**. In Slay the Spire you spend it to get
further: taking a hit to save a turn, skipping a rest to smith, running an elite
for the relic. Paying the agent to hold HP pays it to hoard currency whose only
purpose is being spent.

The failure is not hypothetical or narrow. An HP-maximising agent avoids elites,
declines Bloodletting, never plays Offering, and rests instead of smithing at
every campfire. Rest-vs-smith was the case that prompted this document, and it
generalises to most of the game's interesting decisions.

This is also already project policy — CLAUDE.md's reward section says *"avoid
strategic intermediate rewards, they bias playstyle."* The question is how to
get a dense signal without breaking that rule.

## 3. The decision: potential-based shaping

Shape with

```
F(s, a, s') = γ·Φ(s') − Φ(s)
```

for a potential function `Φ` over states. By
[Ng, Harada & Russell (1999), *Policy Invariance Under Reward Transformations*](https://people.eecs.berkeley.edu/~pabbeel/cs287-fa09/readings/NgHaradaRussell-shaping-ICML1999.pdf),
this leaves the **optimal policy provably unchanged** for any choice of `Φ`.

That is the whole reason it is the right tool here. It is not "shaping we hope
is harmless" — it is the one form of shaping that cannot bias playstyle, which
is exactly the constraint we were trying to satisfy.

### Why it cannot be farmed

The shaping telescopes. Follow `Φ(s₁)` across two consecutive steps:

| step | shaping term | contribution of `Φ(s₁)` |
|---|---|---|
| t=0 | `γΦ(s₁) − Φ(s₀)` | `+γΦ(s₁)`, discounted by `γ⁰` → `+γ¹Φ(s₁)` |
| t=1 | `γΦ(s₂) − Φ(s₁)` | `−Φ(s₁)`, discounted by `γ¹` → `−γ¹Φ(s₁)` |

They cancel exactly. Summed over a trajectory:

```
Σₜ γᵗ[γΦ(sₜ₊₁) − Φ(sₜ)]  =  γᵀΦ(s_T) − Φ(s₀)
```

The total shaping depends only on where the run started and where it ended.

Applied to the rest-vs-smith worry: healing raises `Φ` and pays `+β·Δhp` at that
step — and the next step subtracts the same raised `Φ`. The agent is paid and
immediately un-paid. Smithing pays nothing now and leads to floors that do. The
incentive we were afraid of **cancels by construction**, rather than by tuning a
coefficient until behaviour looks acceptable.

## 4. The shape

```
Φ(s) = α · floors_cleared(s)  +  β · (hp / max_hp)

reward = terminal(win / loss)  +  γ·Φ(s') − Φ(s)
```

| term | role |
|---|---|
| `terminal` | The actual objective, and the largest signal. A completed run is what the agent is for. |
| `α · floors_cleared` | Dense progress. With γ≈1 this is ≈ a constant per floor cleared. |
| `β · (hp/max_hp)` | Keeps HP legible as a resource without making hoarding profitable. |

Note that "a constant reward per floor cleared" — the first instinct — turns out
to be approximately the potential-based form already, for `Φ = α·floors` at
γ≈1. The framework mostly explains *why* that instinct was sound and makes the
HP term safe to add alongside it.

### 4.1 The coefficients are configuration, not constants

`α` and `β` are per-env hyperparameters fixed for the env's lifetime, following
`hp_reward_coeff` (ROB-52). That makes the reward a research knob rather than a
decision baked into the engine:

| `α` | `β` | what you get |
|---:|---:|---|
| 0 | 0 | **Pure terminal win/loss.** `Φ ≡ 0`, so the shaping term is identically zero — sparse reward is a *special case* of this scheme, not a separate code path. |
| > 0 | 0 | Progress only. Tests whether floor-counting alone is a learnable signal. |
| > 0 | > 0 | The default shape in §4. |
| 0 | > 0 | HP only. Mostly useful as an ablation showing what §2 warns about. |

The degeneracy at zero is worth noting as a property rather than a coincidence:
because the shaping is potential-based, turning the coefficients down cannot
change the optimal policy — it only changes how dense the learning signal is.
So the sweep across this table is a study of **sample efficiency**, not of what
the agent is trying to do. That makes it a clean ablation: every row has the
same optimum, and the only thing varying is how quickly the agent finds it.

That is also the honest way to test §7's first open question. If flat floor
counting is too coarse, the `α > 0, β = 0` row is where it shows up.

## 5. Implementation requirements

Two things are load-bearing rather than stylistic:

**`Φ` must be 0 at terminal states.** Otherwise the `γᵀΦ(s_T)` term survives the
telescoping sum, the agent can influence it, and the invariance guarantee — the
entire reason for choosing this scheme — is gone. With `Φ(terminal) = 0` the sum
collapses to the constant `−Φ(s₀)`.

**`γ` in the shaping must equal the learner's `γ`.** A mismatch breaks the
cancellation and silently reintroduces bias.

```python
def potential(state) -> float:
    if state.is_terminal:
        return 0.0                      # required, not an optimisation
    return ALPHA * state.floors_cleared + BETA * (state.hp / state.max_hp)

# in step():
shaped = terminal_reward + GAMMA * potential(next_state) - potential(prev_state)
```

**This is a new mechanism, not a change to `hp_reward_coeff`.** That knob is a
plain terminal bonus on a won fight (ROB-52) and is unaffected; the two can
coexist, and v1.0.0 behaviour does not move.

## 6. What is deliberately not in Φ

**Deck quality.** A stronger deck is objectively closer to winning, and it is
tempting to encode. It is left out because "which cards are good" is precisely
the strategic judgment the agent should discover — putting it in `Φ` would not
bias the optimum (nothing potential-based does), but it would hand the agent an
answer the research question is about. The floor term carries it indirectly: a
better deck clears more floors.

**Relics, potions, gold.** Same argument, weaker case. Revisit only if the floor
term proves too coarse to learn from.

## 7. Open questions

These are parameter and granularity choices. None of them changes the structure
above.

1. **Is `α · floors` too coarse?** A floor-3 Jaw Worm and an Act 1 boss are both
   "one floor". Miles's *third* model existed specifically to predict HP loss per
   fight, which suggests flat progress was not enough for him.

   **Ruled (2026-08-03): start flat.** Weighting floors by act or encounter type
   is a strategic judgment smuggled into the reward — the same objection §2
   raises against rewarding HP, and §6 against encoding deck quality. Flat
   counting says only "further is better", which is true by definition. If it
   proves too coarse to learn from, that is a finding worth having explicitly
   rather than a problem pre-empted by a guess.
2. **Ratio of `α` to `β`, and of both to the terminal reward.** The terminal
   reward should dominate — that is a stated requirement, not a tuning
   preference.
3. **Does the invariance survive function approximation?** The guarantee is
   exact for tabular MDPs. With a neural policy it holds in expectation, but
   shaping still changes *learning dynamics* — some behaviours become easier to
   learn even though the optimum is unmoved. So this is a principled starting
   point, not a promise that training converges.

## 8. Dependencies

The reward cannot be evaluated before the run layer exists. Specifically it
needs **bosses** — without a terminal success condition, the largest intended
signal has nothing to attach to, and the whole scheme reduces to floor-counting.
