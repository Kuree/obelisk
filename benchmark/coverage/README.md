# Coverage format scaling probe

`format-scaling.cpp` constructs sparse IDs, a deep scope chain, many source
points, a wide record of one-bit fields, and sparse counters over many runs. It
serializes and parses the database, verifies record counts, prints elapsed
times, and optionally writes the already encoded bytes. No external simulator
or dependency download is needed.

Build against the current runtime archive:

```sh
c++ -O3 -DNDEBUG -std=c++17 -Iinclude -Ibuild/include \
  benchmark/coverage/format-scaling.cpp build/runtime/libobelisk_rt.a \
  -pthread -o build/coverage-format-scaling
build/coverage-format-scaling 65536 128 build/scaling.obcov
build/tools/obelisk-cov/obelisk-cov report --format=json \
  build/scaling.obcov -o build/scaling.json
```

The arguments are point/field count, run count, output path, and optional
`same-line`. The last mode puts all physical source points on one source line;
LCOV must aggregate them before emitting one line per run. The same generator
is the `obelisk-coverage-scaling-fixture` lit target. Its regression verifies
sparse contributors, toggle field paths, test filtering, and LCOV aggregation.
Use a geometric sequence of counts and keep compiler flags, run count, and
machine fixed when comparing implementations. Avoid concurrent benchmarks.

## Measurements

Measurements on Linux ARM64, Release `-O3`, comparing the v2 implementation
before indexing (`2b3aa03f`) with the indexed codec. The first probe contains
one scope, line point, and counter per N, and no toggle fields. Both binaries
used the same optimization settings and produced the same byte counts:

| N | Before serialize (s) | Indexed serialize (s) | Before parse (s) | Indexed parse (s) |
| --- | ---: | ---: | ---: | ---: |
| 2,048 | 0.017 | 0.012 | 0.013 | 0.010 |
| 8,192 | 0.099 | 0.059 | 0.059 | 0.041 |
| 32,768 | 0.864 | 0.199 | 0.843 | 0.170 |
| 65,536 | 4.524 | 0.412 | 4.422 | 0.351 |

JSON reporting with the checked-in probe, 128 runs, and one field per N.
Both reporters linked the indexed codec, so this comparison measures reporter
changes rather than improvements to input parsing. Before and after JSON files
were byte-identical:

| N | Before report (s) | Indexed report (s) | JSON bytes |
| --- | ---: | ---: | ---: |
| 2,048 | 0.158 | 0.035 | 1,901,612 |
| 8,192 | 0.827 | 0.150 | 7,587,123 |
| 32,768 | 5.065 | 0.554 | 30,591,622 |

These are individual observations, not thresholds. See
`docs/coverage-format.md` for the cost model and expansion bounds.

## Bounded constructor probe

`configuration-scaling.sv` constructs and samples distinct constructor-resolved
configurations, then writes a database. It is outside the unit suite, defaults
to 16 configurations, and rejects counts above 128. For a short local check:

```sh
build/tools/driver/obelisk --std=1800-2023 -O3 --target=native \
  --coverage=functional -o build/coverage-configuration-scaling \
  benchmark/coverage/configuration-scaling.sv
timeout 1s build/coverage-configuration-scaling +N=128 \
  --coverage-output=build/configuration-scaling.obcov
```

On the same ARM64 Release build, a fixed 128-configuration input took 178 ms
before append-only construction and 18 ms after, including startup, sampling,
and saving. Both images were 173,816 bytes; every report field except run
metadata matched after normalizing run UUIDs. These are single observations,
not CI timing assertions or an extrapolation to larger inputs.

The format regression uses 16 configurations. Codec and radix-index units use
256 records; large fixture sizes belong in standalone measurements. The four
new packing, failure, and static-profile regressions use at most two bundles.

## Bounded method-query probe

`query-configuration-scaling.sv` checks both instance-weighted averaging and
name-based merging across distinct constructor configurations, including a
load-only process. It defaults to 16 configurations per type and one query
pair, and rejects more than 128 configurations or 16 query pairs. It is outside
the unit and lit suites. Each process below has a one-second deadline:

```sh
build/tools/driver/obelisk --std=1800-2023 -O0 --target=native \
  --coverage=functional -o build/coverage-query-scaling \
  benchmark/coverage/query-configuration-scaling.sv
timeout 1s build/coverage-query-scaling +N=128 +Q=16 \
  --coverage-output=build/query-scaling.obcov
timeout 1s build/coverage-query-scaling +N=128 +Q=16 +load_only \
  --coverage-load=build/query-scaling.obcov \
  --coverage-output=build/query-scaling-loaded.obcov
```

Both processes must print `AVERAGE 50 64/128`, `MERGED 100 1/1`, and
`GLOBAL 75`. Timings include startup, schema loading/construction, all queries,
and persistence. Compare builds with the same flags and inputs, sequentially;
small-count setup overhead and scheduler variation matter at this duration.

On the ARM64 Release build, median elapsed times from three sequential pairs
of native `-O0` load-only processes, with 16 query pairs, were:

| Configurations per type | Before (ms) | Indexed (ms) |
| --- | ---: | ---: |
| 32 | 12.20 | 13.08 |
| 64 | 24.86 | 23.06 |
| 128 | 55.65 | 42.06 |

The indexes add setup cost at small counts. These bounded observations support
the traversal review; they are not timing assertions or larger-input estimates.
Native and bytecode queries agreed, and JSON, HTML, text, and LCOV reports
matched after normalizing run metadata.

## Bounded bin-construction probe

`bin-construction-scaling.cpp` constructs one configuration with automatic
points, ignored values, default arrays preceding explicit bins, and empty
default arrays. It defaults to 16 coverpoints and rejects more than 128. It is
outside the unit and lit suites. The reported timer measures construction;
schema loading and optional saving happen outside that timer.

```sh
c++ -O3 -DNDEBUG -std=c++17 -Iinclude -Iruntime/include -Ibuild/include \
  benchmark/coverage/bin-construction-scaling.cpp build/runtime/libobelisk_rt.a \
  -pthread -o build/coverage-bin-construction-scaling
timeout 1s build/coverage-bin-construction-scaling 128 build/bin-scaling.obcov
```

On ARM64 Release, median construction times from three sequential before/after
pairs were:

| Coverpoints | Before (ms) | Indexed and packed once (ms) |
| --- | ---: | ---: |
| 32 | 9.76 | 16.30 |
| 64 | 20.39 | 19.29 |
| 128 | 47.30 | 40.06 |

The smaller observations include index setup cost and substantial timing
variation. These measurements do not establish overall linear runtime; the
linear table joins and group packing follow from the traversal review. All
text, JSON, HTML, and LCOV report fields agreed after normalizing run metadata,
both for this probe and the mixed automatic/default compiler probe. Unit
regressions use four coverpoints, twelve resolved bins, and two instances;
oversized-domain regressions reject before allocating or enumerating values.

## Bounded option NBA probe

`option-nba-scaling.sv` queues mixed ordinary and coverage-option NBAs, packed
partial writes, and one distinct future deadline per iteration. It uses one
configuration and one instance, defaults to 32 iterations, and rejects counts
above 1024. It is outside the unit and lit suites. Each process is bounded:

```sh
build/tools/driver/obelisk --std=1800-2023 -O0 --target=native \
  --coverage=functional -o build/coverage-option-nba-scaling \
  benchmark/coverage/option-nba-scaling.sv
timeout 1s build/coverage-option-nba-scaling +N=1024
```

The final weight and ordinary variable must both equal `N - 1`. Compare a
short geometric sequence sequentially with identical compiler flags. Timings
include process setup; they are observations, not unit-test timing assertions.

On ARM64 Release with native `-O0`, medians from three sequential processes
per count were 5.86 ms for 128 writes, 3.09 ms for 512 writes, 7.01 ms for
2,048 writes and 10.52 ms for 4,096 writes. Setup overhead and scheduler
variation dominate the smallest counts. The bounded observations support the
queue traversal review; they do not establish overall compiler linearity.
