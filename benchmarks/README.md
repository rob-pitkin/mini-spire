# Benchmarks

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target minispire_bench
uv run python benchmarks/bench.py --json benchmarks/results.json
```

**Release only.** Debug and ASan builds differ by an order of magnitude and are
not publishable numbers.

## The two numbers, and why both

| | what it measures | M1 MacBook Pro |
|---|---|---:|
| **engine path** | `CombatEnv` driven from C++ — step, observation, mask. No Python. | ~438k steps/sec |
| **env path** | the same work through pybind11 + the Gymnasium wrapper, end to end. | ~259k steps/sec |

The 1.7× gap between them is the cost of crossing into Python once per step.

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

- **The headline is measured end to end, not derived by subtraction.** The
  `env_e2e` loop reads the mask, picks a legal action, steps, and resets, timed
  from *outside* the loop with no `perf_counter` calls inside it. An earlier
  version reported a `net` figure by timing its own action sampling and
  subtracting it — an estimate wearing a measurement's clothes, and one whose
  instrumentation landed inside the region it was correcting. The two agree to
  within half a percent, which is reassuring but is not a reason to keep
  publishing the derived one.
- **Sampling is still reported, separately.** A trained policy consumes the mask
  directly and never allocates an array of legal indices, so the harness's
  `flatnonzero` + RNG cost is not the environment's. It is about a third of wall
  time, which is the whole reason the two numbers differ.
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
