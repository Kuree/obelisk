# Transient SDF IR

`obelisk_sdf` is a frontend-only interchange dialect for normalized Standard
Delay Format data. `obelisk-translate --import-sdf` exposes it for parser,
verifier, and round-trip tests. It is deliberately absent from the simulation
runtime and all native, bytecode, and AOT lowering pipelines.

The schema follows IEEE 1800-2017 Clause 32 semantic families: one
`delay_file` contains typed headers and `cell` operations; cell bodies contain
typed path, timing-check, label, interconnect, terminal, and pulse records.
Delay modes, record subtypes, edges, ports, conditions, units, and delay forms
are enums or dialect attributes rather than strings. Conditions use a bounded
postfix token stream with verifier-enforced stack arity.

Decimal attributes preserve exact source spelling. The SDF annotation
consumer—not the parser—performs unit conversion and checked rounding to the
annotated design scope's precision. Empty and sparse min:typ:max fields remain
explicit and retain distinct `empty` and `triple` forms, as required by Clause
32.3, so later ordered ABSOLUTE and INCREMENT application can retain
preannotation values correctly.

Production frontend code must consume and erase every `obelisk_sdf` operation
before semantic Simulation IR is returned. There is intentionally no SDF
runtime operation, table, interpreter, ABI entry, or lowering pattern.
