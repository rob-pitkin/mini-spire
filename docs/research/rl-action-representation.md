# RL prior art: addressing one copy among identical-looking cards

**Research note, not a decision.** Written 2026-10-04 against commit `c1b578f`.
It informed `docs/design/v2-spec.md` §6.5 (actions indexed by `CardId` × cost),
which is where the decision lives. Sources are listed at the end; anything not
checked against its source is marked *unverified*.

## The question

Two copies of one card can differ in cost (Madness, Enlightenment, Snecko Eye's
Confusion), and a human picks a specific copy by clicking it. An action space
indexed by `CardId` alone cannot express that pick. Which action representations
have RL systems used for "the same entity with a visible variant"?

## Options and prior art

| Option | Shape | Prior art | Notes |
|---|---|---|---|
| **B3** — joint (entity, visible attribute) | one flat, masked product | **Mahjong red fives**: a red five is a five with a visible variant, and Mahjong environments give red fives their own tile ids (34–36), so the discard action has 37 ids (mahjax PR #92). **AlphaZero**: chess 8×8×73 (from-square × move type); shogi 9×9×139 including drops indexed by piece type. **poke-env**: move slot × {plain, mega, Z-move, dynamax, tera}. **Gym-µRTS**: composite actions with per-component masks. **Huang & Ontañón 2020**: masking invalid actions keeps the policy gradient valid | Chosen (§6.5). Red fives are the closest structural match. The action index mirrors an observation feature (hand card × cost planes), the AlphaZero pattern of action planes matching input planes |
| **B1** — sequential (entity, then attribute) | two decisions | **DeepNash** (Stratego): a piece-selection head, then a displacement head. **AlphaStar**: autoregressive heads in one step (action type → selected units via a pointer network → target). **Majeed & Hutter, AAAI 2021**: sequentializing an action space is exact and preserves optimality. This project's own two-phase purpose selection (`v2-spec.md` §6.1) | The env-level form (two transitions) is DeepNash's. The in-network form (one transition, chained heads) is AlphaStar's and needs a custom policy |
| **B2** — (entity, rank among same-id copies) | flat | No direct precedent found | Rank indexing within one id; §6.2 retired rank indexing because an index's meaning shifts with the state |
| **B4** — positional slots | flat | sts_lightspeed (hand index); CommunicationMod (`PLAY index`); Hanabi (hand position, because the holder cannot see the cards); gym-locm (hand/board slots); poke-env switch slots | Standard when entities have no stable identity. This project's earlier hand-slot attempt is recorded in `observation-space.md` §4.1 |
| **B5** — attribute folded into identity | flat | AlphaZero shogi: a promoted piece is its own piece type in the input planes. Red fives can be read the same way | With a two-valued attribute B5 and B3 coincide; for this project B5 would multiply the `CardId` vocabulary |
| **B6** — per-copy handles chosen by pointer/attention | non-flat policy | AlphaStar and OpenAI Five (selection by attention over an entity list); DRRN (He et al. 2016) and DouZero (Zha et al. 2021), which score each legal action from its features; Wolpertinger (Dulac-Arnold et al. 2015); Zwingenberger 2023 (transformer policy for variable action sets, Gym-µRTS) | All need a policy architecture beyond the stock MaskablePPO/DQN this project benchmarks |

## What the literature suggests

- A joint masked product is the usual choice when the product is enumerable:
  AlphaZero, Mahjong, poke-env. Here it is: 1,350 card-selection actions and
  6,750 play actions.
- Sequential decomposition is the usual choice when the joint product is too
  large (DeepNash's board × destinations, AlphaStar's unit sets) or the policy
  is autoregressive anyway.
- Once the observation shows only (card, cost) — discount duration being a
  second-degree fact (Rob, 2026-10-04) — B1 and B3 express exactly the same
  choices. The difference is only factorization: one joint step or two
  sequential ones.

## Closest prior art inside Slay the Spire

- **sts_lightspeed** addresses cards by hand or pile index. Its MCTS
  (`BattleScumSearcher2`) prunes plays of a hand card equal to the previous one
  in (id, upgrades, costForTurn, cost, freeToPlayOnce, specialData), so it
  treats copies that differ in any cost layer as distinct actions.
  *Reported by the 2026-10-04 research agent; unverified here.*
- **CommunicationMod** offers `PLAY <hand index>` and `CHOOSE <index|name>`. The
  name form takes the first match: the same first-match problem the engine had.
  *Reported by the research agent; unverified here.*

## Sources

- mahjax PR #92 (red five tile ids): https://github.com/nissymori/mahjax/pull/92
- poke-env environment docs (action mapping): https://poke-env.readthedocs.io/en/stable/modules/env.html
- Huang & Ontañón, *A Closer Look at Invalid Action Masking in Policy Gradient Algorithms*: https://arxiv.org/pdf/2006.14171
- Huang et al., *Gym-µRTS*: https://ar5iv.labs.arxiv.org/html/2105.13807
- Perolat et al., *Mastering the Game of Stratego with Model-Free Multiagent RL* (DeepNash): https://arxiv.org/pdf/2206.15378
- Vinyals et al. 2019, AlphaStar: https://gwern.net/doc/reinforcement-learning/model-free/alphastar/2019-vinyals.pdf
- Majeed & Hutter, *Exact Reduction of Huge Action Spaces in General RL*: https://arxiv.org/abs/2012.10200
- He et al. 2016, DRRN: http://aclweb.org/anthology/P/P16/P16-1153.pdf
- Zwingenberger 2023, *Transformers as Policies for Variable Action Environments*: https://arxiv.org/abs/2301.03679
- AlphaZero action encodings (Silver et al. 2018), DouZero (Zha et al. 2021),
  Wolpertinger (Dulac-Arnold et al. 2015), OpenAI Five (Berner et al. 2019):
  cited from the papers' well-known descriptions; not re-fetched for this note.
- Full option analysis (sizes, mask/obs/clone consequences, verified engine
  facts): the research agent's report, to be copied to
  `docs/research/choice-copy-options.md`.
