# Benchmarks

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target minispire_bench
uv run python benchmarks/bench.py --json benchmarks/results.json
```

**Release only.** Debug and ASan builds differ by an order of magnitude and are
not publishable numbers.

## The two numbers, and why both

| | what it measures |
|---|---|
| **engine path** | `CombatEnv` driven from C++ — step, observation, mask. No Python. |
| **env path** | the same work through pybind11 + the Gymnasium wrapper. |

The engine path bounds what the binding layer could ever reach; the env path is
what a training loop actually gets. Publishing one without the other invites the
reader to assume the wrong one.

## What this harness is careful not to do

The figure published before ROB-81 was mostly measuring itself. A per-step
Python list comprehension over the mask cost roughly eight times the engine work
it was timing, and because that scan scales with the action space, the reported
number *fell 25% while the engine got 2.3× faster*. A benchmark that moves
opposite to the thing it measures cannot support a claim.

Two habits follow from that:

- **Sampling is reported separately, never folded into the headline.** A trained
  policy consumes the mask directly and never scans it, so the harness's
  `flatnonzero` + RNG cost is not the environment's. It is around a third of
  wall time here, which is why the `net` column exists.
- **The engine path drives `CombatEnv`, not raw `CombatState`.** An earlier
  draft of this benchmark timed `valid_actions` + `apply_action` only, which
  skips computing the observation — strictly less work than the env path does,
  making the binding look ~4.7× more expensive than it is. Same work on both
  sides or the comparison is meaningless.

## Batching

The batch sizes step N environments round-robin in **one thread**. That measures
how per-step Python overhead amortises, and the honest answer today is that it
does not — the numbers are flat. Real vectorisation needs ROB-57 (thread-safe
`CombatEnv`); until then, treat the batched rows as a control rather than a
scaling result.

## Results

`results.json` records the machine, the observation and action sizes, and the
raw numbers, because throughput claims are meaningless without them — the same
build on a different machine is a different number.
