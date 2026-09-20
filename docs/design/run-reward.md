# Run-level reward design (v2.0.0)

**Status: ACCEPTED** (2026-08-03). The shape below is settled; the open
questions in §7 are parameter choices, not structural ones.

Scope: this is about v2.0.0, where **an episode is one run** rather than one
fight. v1.0.0's fight-scoped reward (`+1` win / `-1` loss, plus the optional
terminal `hp_reward_coeff` bonus) is unaffected and stays as it is.

---

## 1. The problem

An episode becomes ~16 floors and ~200 steps instead of ~18 steps (this document
originally said ~50 floors, which is a four-act run; v2.0.0 is Act 1 only — see
`v2-spec.md`). The obvious reward — "+1 if
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
for the relic. Rewarding the agent for holding HP rewards it for not spending a
resource whose only purpose is to be spent.

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

That is why it is the right tool here: it is the one form of shaping with a
proof attached, rather than shaping we hope is harmless.

> **Do not overstate this, and an earlier draft of this document did.** Ng et al.
> preserve the **optimal policy**. They say nothing about the policy a
> finite-budget, entropy-regularised optimiser with a bootstrapped critic
> actually converges to — and changing which policies are *reachable* is the
> entire point of shaping, so it biases *learned* playstyle by construction.
>
> Worse for us specifically: under **entropy-regularised or MaxEnt objectives —
> which includes PPO's entropy bonus — PBRS invariance genuinely does not hold**
> and the optimum moves.
>
> The correct claim is: **potential-based shaping does not change the optimal
> policy.** That is still the strongest guarantee available and still the reason
> to choose it. §7.3 already conceded this; §3 asserted the absolute version
> anyway, and a referee would catch the contradiction.

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
Φ(s) = α · floors_cleared(s)  +  β · (hp / HP_REF)      # HP_REF is a CONSTANT

reward = terminal(win / loss)  +  γ·Φ(s') − Φ(s)
```

> **CORRECTED 2026-08-13 — this was `hp / max_hp` and that was a bug.**
> Normalising by *current* `max_hp` makes Φ **fall when Max HP is gained**:
> at 50/80, taking +8 Max HP moves Φ from 0.625 to 0.568, a **negative** shaping
> step. So the design produced a wrong-signed incentive on Singing Bowl, Neow's
> Max-HP blessings and several events — precisely the resource-vs-investment
> decisions §2 was written to protect.
>
> Fix: divide by a fixed constant (`HP_REF = 80`, the Ironclad's starting Max
> HP) so gaining Max HP is neutral and gaining HP is positive. Found in review.

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
`hp_reward_coeff` (ROB-52). That makes the reward a research parameter rather
than a decision fixed in the engine:

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

That is also how to test §7's first open question. If flat floor counting is too
coarse, the `α > 0, β = 0` row is where it shows up.

## 5. Implementation requirements

Two things are requirements rather than style:

**`Φ` must be 0 at terminal states.** Otherwise the `γᵀΦ(s_T)` term survives the
telescoping sum, the agent can influence it, and the invariance guarantee — the
entire reason for choosing this scheme — is gone. With `Φ(terminal) = 0` the sum
collapses to the constant `−Φ(s₀)`.

**`γ` in the shaping must equal the learner's `γ`.** A mismatch breaks the
cancellation and silently reintroduces bias.

> **This is a principle-3 violation and it makes M3 incoherent.** If the
> environment's reward depends on a *learner* hyperparameter, the environment is
> taking a position on the algorithm. Worse for the comparison: PPO at γ=0.99,
> DQN at γ=0.995 and MCTS undiscounted would be optimising **three different
> MDPs**, so their win rates would not be comparable.
>
> **Required fix — cheap, and the most important validity item in this
> document: always log the RAW UNSHAPED return alongside the shaped one, and
> evaluate every algorithm on the raw return.** Shaping is then a training aid
> that never touches the reported metric.

**Reward normalisation and clipping destroy the telescoping.** `VecNormalize`
and reward clipping are SB3 defaults people reach for on long episodes, and both
break the exact cancellation the guarantee depends on. If either is used, the
invariance claim no longer applies.

**A depth-scaled step penalty hides inside the floor term.** Within a floor,
`γΦ(s′) − Φ(s) = (γ−1)·α·floors` — at γ=0.99 that is −0.01·α per step on floor 1
and **−0.15·α per step on floor 15**. It cancels exactly in theory. In practice
it is a step penalty that *grows with progress*, and toypiper's documented
failure (§1) was a step penalty teaching the agent to die quickly. **Test for
it.**

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

## 5.1 γ and the horizon — why the default cannot be 0.99

Added 2026-08-13. This was missing, and it changes how §4's floor term should be
justified.

**The arithmetic.** A discount factor has an effective horizon of `1/(1−γ)`:

| γ | effective horizon | `γ^700` |
|---:|---:|---:|
| 0.99 | ~100 steps | **0.0009** |
| 0.995 | ~200 steps | 0.030 |
| 0.999 | ~1000 steps | **0.50** |

v2 episodes are ~400–700 steps (`v2-spec.md` §2). **At the RL default of γ=0.99,
a win at step 700 is worth 0.0009 at step 0** — the terminal reward, which §4
calls "the actual objective, and the largest signal", is numerically invisible to
every decision made in the first half of the run. That includes Neow, the first
card rewards, and the first shop: exactly the deck-building decisions this
project exists to study.

**So γ ≈ 0.999 is the floor for a run-scoped episode**, with the standard cost:
longer horizons mean higher-variance value estimates and slower critic
convergence.

### This is the stronger argument for the floor term

§4 sells `α · floors_cleared` as "dense progress". The better justification is
that **it makes the horizon tractable at any γ**. With ~16 floors over ~700 steps
a floor clears roughly every 40 steps, so a reward arrives every ~40 steps
instead of once at the end. At γ=0.99, `0.99^40 ≈ 0.67` — well inside the
horizon, where `0.99^700 ≈ 0.0009` is not.

Put plainly: the shaping is not merely a convenience, it is what makes the
problem representable at a discount factor a critic can actually learn. That
reframes §7's first open question — *"is `α·floors` too coarse?"* — because the
term does more than add signal density.

### γ is an environment parameter, not a training detail

§5 already requires shaping-γ to equal the learner's γ, and flags that as a
principle-3 violation. The horizon arithmetic makes it concrete: **the
environment's γ default cannot be chosen without knowing the episode length**,
and the episode length is a property of the environment.

The resolution stays the one in §5: ship γ as a constructor parameter, document
that it must match the learner, **and always evaluate on the raw unshaped
return** so no reported metric depends on it.

### Two consequences to check when the skeleton runs

1. **Measure the real episode length first** (`v2-spec.md` §2 — ~400–700 is an
   estimate). If runs are longer than 700 steps, even γ=0.999 starts to truncate.
2. **The §12 turn cap interacts with rollout buffers.** A capped episode at
   ~10,000 steps spans ~5 refreshes at a typical `n_steps=2048`, with no terminal
   in between. Correct with proper truncation bootstrapping, but worth knowing
   before someone debugs it.

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
   is a strategic judgment built into the reward — the same objection §2
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
