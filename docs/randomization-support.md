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
| Constraint blocks and `randomize()` | Executable for non-static, non-enum packed integral `rand` properties with at most 64 aggregate random bits. Total, side-effect-free hard expression constraints, `inside` ranges, implication, conditional constraints, uniqueness, inheritance, package/design captures, and inline `with` constraints compile to pointer-free SSA. The object-local PCG draw chooses the starting assignment; a cyclic exhaustive fallback proves success or failure over the complete finite domain. Property writes occur only in the success block, so failed solves commit nothing. Tagged-union domains, constraint function calls, and candidate-partial arithmetic such as divide, shift, and power are rejected until their legal-domain, purity, and totality contracts are modeled. |
| `rand_mode` / `constraint_mode` | Pending the versioned randomization plan and dynamic mask layout. |
| `dist`, `solve before`, `soft` | Pending weighted/preference and solve-order planning. Unsupported forms are diagnosed before executable IR is produced. |
| `randc` | Pending; the intended executable boundary is packed integral widths up to 32 bits. |
| `pre_randomize` / `post_randomize` | Pending virtual-hook execution; user-defined hooks are diagnosed rather than silently skipped. |
| Compiler Z3 planning | Pending. Runtime Z3 is not permitted; the non-Z3 build must use the same complete runtime fallback. |

Known-answer runtime tests pin the PCG sequence, state restoration, and bounded
draw behavior. Driver tests compare native and bytecode output at `-O0` and
`-O3` and across the 1800-2017 and 1800-2023 language modes.
