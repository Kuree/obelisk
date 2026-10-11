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
