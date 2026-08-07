# Randomization support

This matrix separates the executable unconstrained random-number and stream
foundation from the remaining constrained-randomization work. A row is
“executable” only when it follows the same typed Simulation IR through native
and whole-design bytecode execution.

| Feature | Current boundary |
| --- | --- |
| Generator | Executable PCG-XSH-RR with explicit `{state, increment}` state and the existing `--seed=<u64>` option. The bounded draw uses rejection debiasing. |
| Process streams | Executable. Root processes split from the seeded context stream, and forked processes split the active parent stream in lexical spawn order. Bytecode tasks retain their stream while dequeued from the scheduler. |
| Object streams | Executable for the constrained-randomization slice. Every root class contains two hidden, inline, GC-invisible `i64` fields. Construction initializes them before `new`; class copy reseeds the copy; `randomize()` advances this object-local PCG state in native and bytecode execution. |
| `$urandom` | Executable with zero arguments and with the optional seed argument. |
| `$urandom_range` | Executable with one or two arguments, including reversed bounds, with an unbiased inclusive range. |
| `$random` | Executable with zero arguments. The optional seed form reseeds the active stream and writes the generated value back to the seed variable. |
| `$srandom` | Executable with one argument; it reseeds the active process stream without drawing a value. |
| Stream snapshot API | The runtime C ABI supports exact get/set of both PCG words and pure state-local stepping/bounded draws. Object `get_randstate` / `set_randstate` source methods are pending their string encoding. |
| Constraint blocks and `randomize()` | Executable for non-static, non-enum packed integral `rand` properties with at most 64 aggregate random bits. Total, side-effect-free hard expression constraints, one top-level soft expression preference, `inside` ranges, implication, conditional constraints, uniqueness, inheritance, package/design captures, and inline `with` constraints compile to pointer-free SSA. The object-local PCG draw chooses the starting assignment. Generated code checks up to 64 cyclic candidates, then invokes a versioned, pointer-free residual constraint program in the dependency-free runtime. The fallback has a deterministic attempt budget: it searches the full domain through 20 aggregate bits and up to 2^20 candidates for wider domains. A soft preference is dropped only after a complete finite-domain search proves it unsatisfiable. Property writes occur only on the success edge, so failed solves commit nothing. Tagged-union domains, constraint function calls, and candidate-partial arithmetic such as divide, shift, and power are rejected until their legal-domain, purity, and totality contracts are modeled. |
| `rand_mode` / `constraint_mode` | Pending the versioned randomization plan and dynamic mask layout. |
| `dist`, `solve before`, multiple or nested `soft` constraints | Pending weighted, priority, and solve-order planning. Unsupported forms are diagnosed before executable IR is produced. |
| `randc` | Pending; the intended executable boundary is packed integral widths up to 32 bits. |
| `pre_randomize` / `post_randomize` | Pending virtual-hook execution; user-defined hooks are diagnosed rather than silently skipped. |
| Compiler Z3 planning | A pinned Z3 4.13.4 static library is built from source for the compiler only. Residual programs are decoded into a temporary MLIR SMT bit-vector formula, then an in-process shim executes that formula through the Z3 API; no solver subprocess or SMT-LIB round trip is used. Z3 diagnoses constraints that are unsatisfiable for every runtime capture value and computes conservative per-property domains under a deterministic resource limit. Power-of-two domains are folded without modulo bias into the generated tier-0 proposal, including constant assignments. Direct variable equalities are collapsed into Z3-verified alias classes, so constraints such as `x == y` sample one field and copy it instead of relying on rejection. Acyclic arithmetic or bitwise definition chains such as `x == y + 1; y == z + 1` are extracted from the versioned RPN program, topologically ordered, and emitted directly as MLIR; cyclic components remain on the checker/runtime path. When Z3 proves that the combined domain, alias, and definition proposal implies the hard formula, generated code commits directly without a checker or runtime fallback. The build uses single-threaded polling mode so compiler-side Z3 remains WebAssembly-friendly; Z3 is never linked into `libobelisk_rt`. With `OBELISK_ENABLE_Z3=OFF`, conservative analysis returns unknown and emits the identical runtime fallback. Alias/definition interaction scheduling and correlated-domain sampler plans remain pending. |

Known-answer runtime tests pin the PCG sequence, state restoration, and bounded
draw behavior. Driver tests compare native and bytecode output at `-O0` and
`-O3` and across the 1800-2017 and 1800-2023 language modes.
