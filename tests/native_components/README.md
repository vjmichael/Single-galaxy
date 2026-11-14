# SPACE-05H native worker component acceptance

This standalone CMake target compiles the **same native C++ source** shipped
by the original `Single-galaxy` repository. It intentionally does not link
the full `galaxy_kernel`, `PersistentWorld`, regional authority or
original SQLite simulation, and must **never** be reported as passing those
integration gates.

It is useful when external CI is unavailable or when an OS worker process
regression must be reproduced independently of the large game build.

## Reproduce on Linux (GCC or Clang)

From the repository root:

```bash
cmake -S tests/native_components -B build/space-components -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ \
  '-DCMAKE_CXX_FLAGS=-Wall -Wextra -Werror'
cmake --build build/space-components --parallel 4
ctest --test-dir build/space-components --output-on-failure -V \
  --output-junit build/space-component-results.xml
```

Set `-DCMAKE_CXX_COMPILER=clang++` to repeat with Clang. For a Linux UBSan
variant use Clang, `-DCMAKE_BUILD_TYPE=RelWithDebInfo` and
`-DCMAKE_CXX_FLAGS=-Wall -Wextra -Werror -fsanitize=undefined
-fno-sanitize-recover=all`.

The component target covers:

- `galaxy_native_cell_process_stress`: 4 lanes x 20 independent native OS
  worker launches, three forced process timeouts, detached stdout-pipe
  rejection, exact report validation, kernel fork-refusal proof, invalid
  request rejection.
- `galaxy_native_cell_process_long_stress`: identical logic at
  4 lanes x 1000 launches (4,000 successful admission attempts).
  It logs mean, p95 and maximum per-launch wall latency and refuses any
  individual worker deadline miss.
- `galaxy_high_order_math_property` (GCC/Clang): 200,004 actual source
  coordinate normalization cases checked against an independent
  `__int128` arithmetic oracle. This reference oracle is deliberately
  unavailable with MSVC; shipping `galactic::Axis` does not depend on it.

## Executed independent Linux evidence — 2026-11-07

At source checkpoint `SPACE-05H` on stacked draft PR #95:

| Build | Worker 80 / Worker 4000 / Property 200004 | Long-run mean / p95 / max |
| --- | --- | --- |
| GCC 14.2 Release `-Werror` | PASS / PASS / PASS | 5.66924 / 6.65904 / 17.3232 ms |
| Clang 17 Release `-Werror` | PASS / PASS / PASS | 5.53153 / 5.93363 / 40.9318 ms |
| Clang 17 UBSan `-Werror` | PASS / PASS / PASS | 5.73271 / 6.31931 / 23.4998 ms |

One real compilation failure was repaired before the final rerun:
`NativeCellProcessStressTests.cpp` had a signed-vs-unsigned comparison when
the round count became runtime-configurable. It now compares appropriately
cast `std::size_t` quantities and builds under `-Werror`.

The local working copy was checked against exact Git blob IDs for the
tested worker runner (`36839b0a74aa65811164fcc7f1d06f619f2d7ebb`),
current stress test (`a7412c00a228c894aa9ef9adfb01f0852675782a`),
and portable galactic position header
(`11e308ddaa4142ecae50537333dbd01c8b1324a5`).

**Evidence boundaries:** These are real, completed Linux native
compile/link/CTest executions on exact component source, not fabricated
Windows/Actions logs. They prove neither four actual `PersistentWorld`
SQLite cells running simultaneously nor the native Windows Job Object
implementation. The original complete game kernel CMake/CTest, WO3/WO4/WO5
regressions, Windows native jobs, and installed 3D client remain OPEN.
GitHub-hosted exact-head jobs continue failing before runner assignment,
tracked in issue #90. See
`docs/engineering/SPACE01_SEAMLESS_SPATIAL_OWNERSHIP_20261107.md`.
