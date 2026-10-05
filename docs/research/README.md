# Research notes

Dated evidence that informed decisions: prior art from RL papers and other
environments, readings of the decompiled game and of sts_lightspeed, and option
surveys. **Not decisions.** Decisions live in `docs/design/`, which links here.

Each note gives the date and commit it was written against, its sources, and which
claims were verified. Line numbers drift after that commit; a note is corrected in
place when a claim turns out wrong, with the correction listed at its top.

| note | question | informed |
|---|---|---|
| [`rl-action-representation.md`](rl-action-representation.md) | How do RL systems address one copy among identical-looking entities? | `v2-spec.md` §6.5 |
| [`choice-copy-options.md`](choice-copy-options.md) | Which action scheme lets the agent choose a specific card copy? | `v2-spec.md` §6.5 |
| [`pile-card-storage.md`](pile-card-storage.md) | How do StS, sts_lightspeed and this engine store piles and card instances? | open: a possible action/obs redesign; tasks #29, #31–#33 |
