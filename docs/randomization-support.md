# Randomization support

This matrix separates the executable unconstrained random-number and stream
foundation from the remaining constrained-randomization work. A row is
“executable” only when it follows the same typed Simulation IR through native
and whole-design bytecode execution.

The [Clause 18 audit](lrm-randomization-audit.json) records 113 feature checks
across all 57 numbered subsections of IEEE 1800-2023 Clause 18, with source
and MLIR regression evidence. Partial, missing, and review entries remain open;
the inventory is not a full conformance certification. Check its coverage
against a local licensed LRM with
`python3 scripts/audit-lrm-randomization.py --lrm build/lrm-2023.txt`.

Packed structs and untagged unions have the full integral random domain,
including unnamed encodings of nested enum members. Standalone enums and
arrays of enums retain enumerator membership at arbitrary integral widths.
Compiler Z3 proves their exact finite domains; native and WASM compiled samplers
preserve signed representations, high words and live state-dependent restrictions.
Disabled enum elements retain state, including non-enumerator encodings. Wide
version-2 domain records store masks and values in disjoint literal-pool ranges;
malformed indices, padding, overlaps and unused words are rejected before sampling.
Packed tagged-union random
declarations are illegal under 18.4; legacy synthetic IR domain tests do not
make them supported source declarations.

Unpacked `rand` structure properties contribute only their explicitly declared
`rand` and `randc` members. Fixed integral leaves, fixed integral arrays, enum
domains, scalar real leaves, and nested qualified structures use the same Z3
relation as other properties.
Ordinary members retain their values, including strings and four-state symbols.
Each integral `randc` leaf through 32 bits has its own persistent cycle,
including fixed array members; static members share
their cycle and modes across instances. Whole-structure, member, nested-structure,
and array-element mode setters preserve other modes. An explicit property list
temporarily activates the selected structure's random members. Unpacked
structure `randc` declarations and nonsingular mode queries are diagnosed.
Dynamic container members, random object members, real array members, and
structures in recursively randomized objects remain open.

For compiled integral constraints, scalar and fixed-array `randc` leaves through
32 bits share one Z3 relation with ordinary fields. Each stage projects an exact
legal domain with later cyclic and ordinary fields free, then permutes its dense
ranks. Persistent managed snapshots detect domain changes caused by live bounds,
constraint modes
and inline constraints, including transitions to an unconstrained full range.
Instance fields, qualified structure members and static storage retain their
own histories; static histories are shared across objects. Automatic temporary
storage holds assignments, keys, positions, snapshots and sampler RNG until the
complete solve succeeds. The same plan then samples ordinary fields with cyclic
values frozen, keeping ordinary soft preferences and `solve before` conditional
on them. Inactive fields and checker calls preserve histories. Shallow copies
retain RNG, modes and cyclic histories under 8.12. Source MLIR tests cover these
paths in native, bytecode and WASM execution, including sparse signed enums
after a 130-bit ordinary field, multidimensional arrays with negative bounds,
and qualified and static array members. Shape-preserving key, position and
snapshot arrays retain independent histories through shallow copies and element
modes. Stage construction emits a fixed number of operations per cyclic leaf,
reuses one cycle operation and compiler plan, and builds masks from 64-bit
constants and SSA extensions. Shared tree extracts and one aggregate history
construction per commit avoid rebuilding an entire array for each leaf.
Source constraint-context changes that preserve the legal domain, runtime
foreach combinations,
function-priority stages, dynamic-container histories and aliased object cycles
remain open.

The typed `simulation.random.cycle_sample` primitive projects the compiler's
hard relation onto one cyclic field, then permutes dense legal ranks. Sparse
32-bit domains therefore do not require scanning billions of declared values.
Later random fields are existentially free; ordinary soft preferences and
ordering are resolved after the cyclic value is frozen. Hard distributions
contribute only their positive-weight support. Canonical snapshots use a fixed
physical bit order and exact bytes, preserving the same rank map across plans
with different ordinary completion counts. A changed legal domain starts a new
cycle. Assignments, immutable managed snapshots, keys, positions and RNG outputs
remain tentative until the caller commits the complete randomization.
Projection constructs the cyclic diagram directly instead of first building
every ordinary completion. A bounded attempt prioritizes retained inputs for
sparse domains; exhaustion retries exactly with the compiler's variable order.
Both paths preserve identical canonical snapshots, cardinalities and rank maps.
MLIR checks cover live bounds, complete non-power-of-two and unconstrained
8-bit cycles, sparse enum values, 130-bit ordinary fields and weights, zero
weights, full 32-bit domains, reordered plans and failure retention in native,
bytecode and WASM execution.
Repeated plan attributes with the same capture layout compile once in the
Simulation plan pass. Scalar and fixed-array source leaves use this primitive;
the open contexts and container histories above keep the `randc` audit rows
partial.

Fixed unpacked arrays of integral elements use their logical element bitstream
for class and scope randomization, including multidimensional arrays and
ascending or negative bounds. Explicit import and export preserve declaration
order; enum domains apply independently to each element. Integral selected
elements support `solve before`, with exact projection sampling from compiled
plans. Whole unpacked arrays are not legal ordering operands. Fixed integral arrays have
persistent per-element `rand_mode`, with multidimensional subarray setters,
static sharing, and resets through the dynamic class hierarchy. Disabled
four-state leaves preserve their X/Z symbols on successful commits. Queries
require singular elements. Integral `randc` arrays retain independent histories
per leaf, including static sharing and qualified structure members. Dynamic
container histories and modes, non-integral leaves, and generalized function
ordering on selected paths remain open. A failed solve preserves the arrays.
Generated proposal draws advance
the caller's stream independently of whether the assignments are committed;
the runtime solver's own draws remain transactional.

State indices in ordinary class and scope constraints select fixed integral
array elements through balanced compiler expressions with exact property bit
bindings. Nested unpacked and packed dimensions retain their declared bounds
and logical bit order. Selected elements contribute their own live modes and
frozen X/Z symbols; other active elements do not turn a selected state value
into a random guard. Independent sibling references still contribute their
own random bits. Signed, negative and wide indices are compared without
truncation. Unknown and out-of-bounds indices remain evaluation errors even
inside two-state casts. Only state guards and disabled constraint blocks can
discard those errors. Z3 compiles the finite selection relation into the same
Boolean plans used by native and WASM sampling. Packed bit-selects, constant
part-selects and variable indexed part-selects preserve separate value, X and Z
planes, including computed operands from admitted functions and arithmetic.
Both indexed directions retain ascending or descending declared coordinates,
with widened address arithmetic and structural bounds checks. Size-only fixed
array types use the same normalized descending coordinates in compiler bindings
and native reads. Generalized selected-path function ordering remains open.

Disabled four-state properties, qualified structure leaves, and fixed-array
elements retain their stored X/Z symbols as state in admitted integral
constraints. Each call captures their original unknown bits and masks them with
the current active-bit inventory. Active candidate bits remain two-state.
Compiler Z3 plans propagate those unknown planes through admitted arithmetic;
legal two-state casts and known absorbing bits consume them. For example, a
two-state cast of the complement of frozen `4'b10xz` produces `4'b0100`, while
the stored property retains X/Z. Generated candidate checks use the same
pre-solve snapshot. Temporary property selection activates the listed fields
regardless of their persistent modes; `randomize(null)` consumes the frozen
planes without writes or random draws. Scalar, 130-bit, array-element, and
structure-member MLIR regressions cover native, bytecode, and WASM execution.

`unique` expands fixed unpacked arrays and slices to their leaf elements before
Z3 dependency analysis. Multidimensional operands, singular values, and fixed
state arrays form the same joint relation in class and scope randomization.
Overlapping operands retain duplicate references and therefore fail; fewer
than two leaves add no restriction. Dynamic container operands and real leaves
remain open.

Integral system functions `$signed`, `$unsigned`, `$clog2`, `$countones`,
`$countbits`, `$onehot`, and `$onehot0` participate in the bidirectional
bit-vector constraint relation without introducing user-function priority.
Bit counting uses balanced sums at the minimum count width in Z3; wide
results can also become proven definitions in generated code. Random
inputs are two-state. Compiler value and unknown planes also preserve
`$isunknown`, `$countones`, `$onehot`, and `$onehot0` for admitted unknown
arithmetic and packed state captures. A compiler high-impedance plane separates
stored Z symbols from X. `$countbits` applies the distinct control LSBs as a
set of 0/1/X/Z symbols, including live four-state controls. Casts,
shifts, concatenation, replication, and conditional merging preserve Z where
required; undefined arithmetic and unary arithmetic on X/Z operands produce X.
`$clog2` uses the unsigned value bits after X/Z-to-zero conversion, including
four-state random declarations.
These cases have scalar and 130-bit MLIR regressions in native, bytecode, and
WASM execution. General mixed unknown selections and non-integral bitstreams
remain open.

Compiler Z3 resolves static domains, aliases, definitions and small complete
assignment tables, and lowers every admitted residual arithmetic expression
into a parameterized Boolean sampling plan. The plan names random bits,
state-capture bits and constraint modes explicitly. Bounded decision-diagram
preparation and dead-gate elimination move additional work into compilation;
wide popcount equalities use exact cardinality relations. Preparation budgets
cover inner diagram products as well as outer cofactor traversal. Exhaustion
retains the complete circuit rather than truncating its sample space. The fixed
cyclic-array MLIR regression checks that independent element constraints and
live modes cannot expand an unbudgeted diagram product.

Sampler input preparation uses linear passes over the lowered program and its
declared bits: field boundaries use a bitmap, finished fields leave the column
traversal, and input columns use stable buckets. Z3 constructs fixed
cardinality relations; host recurrences have a fixed sparse-count limit.
Live and candidate count targets compare one shared prefix sum instead of
enumerating every possible cardinality. Its precision is at most the 32 bits
specified for the count functions in 20.9; consuming casts retain their modular
results. Prefixes simplify as state and input bits are bound, allowing equal
partial counts to share cofactors. The live 65-bit equality/inequality plan
shrinks from 64,545 to 2,064 Boolean nodes. Its fixture covers all count values,
signed out-of-range targets, frozen inputs, joint candidate counts, three-bit
truncation and 130-bit captures without aliasing their high bits.
`$clog2` tracks the highest set bit and whether lower bits are nonzero without
rebuilding wide threshold constants. Each edge of Z3's Boolean result is visited
once during conversion, and at most 32 atom diagrams receive bounded compiler
preparation. Larger atom sets retain their exact circuits. Reading and emitting
Z3's result costs O(M) for its M nodes and edges; Z3 can produce a graph larger
than the lowered input.

The structural proposal decoder indexes variables, captures and solve-order
masks. It validates ordering and schedules definitions with linear graph
traversals. Dependency discovery visits each constraint once and indexes all
overlapping variable views by bit; a linear work budget stops optional metadata
when dense overlap would expand it excessively. Source semantic-domain pattern
validation uses deterministic radix ordering over their complete values. Equal
canonical values witness overlap; distinct equal-mask values prove disjointness.
Mixed masks use one shared-prefix compiler Z3 query, constructed in linear
pattern-word work independently of user constraints. Solver-free compiler builds
bound mixed-mask pair checks to four times the input word count, reporting an
unproven domain rather than exceeding that linear budget. The SMT proposal
decoder also uses a shared-prefix formula for unequal-mask records. The
SMT-to-Z3 shim visits each SSA edge once with an explicit
stack. Component masks and bounded assignment tables have a linear output
budget, and wide assignments use in-place bit insertion. Budget exhaustion
retains the complete compiled Boolean plan. Here N counts lowered operations,
expanded loops and array elements, bit widths and serialized constraint
metadata. These bounds exclude work performed inside Z3 and do not imply that
its result graph has O(N) size. Constraint lowering also hashes repeated
ordering edges, joins overlapping selected paths through per-bit adjacency,
and schedules their latest legal layers with one sink traversal. Candidate and
runtime-input subtree summaries share an iterative postorder cache; property
offsets and alias target counts use indexed lookups. Fixed inside-array
expansion clones each base once per emitted leaf and wraps it by moving
preceding selections, with work linear in the generated leaf paths. Reusable
constraint templates serialize shared candidate computations and wide literals
once, using immutable temporary slots for subsequent uses. Both compiler Z3
decoders retain the shared graph, and legacy target interpreters refresh those
slots for each assignment. Slot indices, widths and initialization are validated
before evaluation. The MLIR fixtures with 96 arithmetic diamonds emit 501 scalar or
692 wide instructions instead of expanding the graph into a tree. The wide
fixture reuses one 130-bit zero literal rather than copying its words at each
use. Compiler value, X and Z planes also retain graph edges: shallow semantic
lowering templates bind formal operands to the shared values instead of cloning
their expressions. Node construction and dependency discovery visit each edge
once. Candidate checks reuse generated plane values within one evaluation and
start a fresh bank when evaluating another root, so values cannot cross candidate
bindings or unrelated control-flow branches. Shared program values use the same
immutable temporary namespace as reusable templates. The 96-element membership
fixture emits 2,794 instructions; variants with 12, 24 and 48 entries emit 358,
706 and 1,402 respectively, following `29 * entries + 10`. Frozen symbols,
wildcard absorption, consuming casts, mode changes and inverse membership retain
the same native, bytecode and WASM semantics. Other proposal lowering paths
remain under review; the full O(N) generation requirement is still open.

Native and WASM targets contain no Z3 library or solver calls. A simple sampler
binds current captures and inactive bits, selects the optimal enabled soft
preferences, and samples exact ranks using arbitrary-precision cardinalities.
It counts only random inputs. Cofactoring the final relation before constructing
its decision diagram prunes impossible prefixes without constructing diagrams
for intermediate arithmetic. Free bits contribute full cubes, and frozen state
never multiplies a count. Mandatory Boolean literals are substituted before
branching through arithmetic gates, then restored in the relation so weights
and solve-order projections keep their exact counts. The compiler uses the
same optimization during bounded plan preparation. Exact counting can still
require exponential work for
complex Boolean relations; the compiler's preparation budget does not bound
runtime correctness.

Real and shortreal class and scope randomization now lower scalar properties
into finite numeric-coordinate plans. Qualified real structure members and
scalar real leaves in finite active object graphs use the same relation.
Both native and WASM archives provide interval sampling and rank conversion.
Coordinates use half the least positive subnormal as their
quantum, with 2,100 signed bits for real and 279 for shortreal. Decoding rounds
towards zero and retains both zero encodings. Interval leaves weight each
admitted value by the size of its numeric coordinate cell, clipping the finite
extremes; they do not choose IEEE encodings with equal probability. Empty
intervals preserve the destination and RNG, and singleton intervals need no
draw. MLIR regressions cover both precisions, subnormal and normal boundaries,
finite extremes, interval density and invalid arguments at O0 and O3. Compiler
Z3 compiles typed IEEE
comparisons, arithmetic, integral conversions, logical operations and selection
into the same parameterized Boolean plans as integral constraints. Explicit
numeric-rank decoders preserve both zero encodings; callers emit independent
finite-domain constraints. Bitcasts retain their original payloads. Arithmetic
and precision conversions use round-to-nearest, ties-to-even. Bitcasts of
computed arithmetic are diagnosed while their platform-dependent NaN semantics
remain open. The Simulation plan pass rejects unsupported or ill-typed
expressions, and both target readers require an exact uniform plan for IEEE
programs. Full-width rank comparisons against retained IEEE bounds use signed
numeric-cell predicates. Captured first/last cell bits are deterministic state
parameters, converted once per payload in the sampler; they never contribute
random witnesses or extra model counts. Both readers validate the payload
precision, coordinate indices and capture bindings. Real coordinates use high
prefix traversal, while ordinary integral fields retain their low-prefix order.
Simulation rank conversion operations share the existing numeric leaves in
native and bytecode execution, including nonfinite capture placeholders.
Promoted shortreal comparisons preserve their original coordinate precision.
The compiler proves their relation to a real bound through the bound's nearest
shortreal cell and which side of that cell the bound occupies. Captured cell
conversion uses integer round-to-nearest, ties-to-even, independently of host
floating rounding modes. Wire readers require all 64 source payload bits.
MLIR tests cover every comparison in both operand orders, live captures,
disabled constraints, frozen inputs, soft priorities, finite extremes, signed
zero, NaN/infinity captures and rank decoding in native, bytecode and WASM
execution. Source regressions additionally cover the 18.4.1 density example,
empty promoted-shortreal intervals, explicit property lists, checker-only
calls, live mode changes, frozen NaN payloads and scope randomization. Parent
and descendant-owned constraints bind object fields by their static receiver
paths; alias equations retain shared identity. Frozen child state, guarded and
unguarded nulls, disabled blocks and failed solves retain the normal error and
commit rules. Integral casts and `$rtoi` lower to compiler Z3 bit-vector
relations, including computed IEEE arithmetic. Nonfinite results preserve
four-state unknown planes; two-state casts consume them as zero. Computed
numeric conversions extract sanitized IEEE bits and reuse the conversion
kernel for rounding halfway away from zero, truncation and arbitrary-width
low bits. The process-wide `rewriter.hi_fp_unspecified` Z3 parameter selects
Z3's deterministic completion; unreachable NaN completion functions
never become sampler inputs. MLIR covers active real points, live addition
captures, arithmetic at both precisions and 1/32/130-bit results. Universal
Z3 checks cover every binary32/64 arithmetic-result encoding at widths
1/8/32/64/130 for both rounding and truncation.

General full-coordinate arithmetic efficiency, computed arithmetic payload
bitcasts, real arrays, joint real/integral distributions
and complete real probability semantics remain open.

The compiler appends `HAS_SAMPLER_PLAN` bundles and opts into exact sampling
through the `EXACT_UNIFORM` or `EXACT_WEIGHTED` C ABI flag; either requires a
compiled plan rather than Z3. A nonzero work bound enables exact solving; zero
still requests no solve. Programs without an exact flag, which only a compiler
built without Z3 emits, retain the bounded traversal and its work limit.
For integral bitstreams of arbitrary width, ordered runtime residuals
count existential projections of each enabled solve-order layer, sample its
legal values uniformly, and cofactor the original relation before solving
later layers. This works with live captures, semantic domains, frozen properties,
constraint modes and optimal soft priorities without the old 2^20 traversal
limit. Version-2 records carry multiword ordering masks, and compiler
tables use arbitrary-width projection keys. Weighted and dynamic-foreach
residual paths retain their existing limits. This does not close the non-integral
or container gaps in the audit.

User-function argument priority subdivides integral constraints into separate
Boolean relations. Each higher-only set is sampled independently and its accepted values
become state for later sets, which can then fail. Explicit ordering still uses
conditional projections inside each function stage. Source masks retain the
referenced packed paths even when Z3 simplifies their Boolean dependence away;
atomic constraints inside guards keep their separate stages. Function-return
checks run after their mutable arguments are selected, including expressions
with no lower result variable. Soft preferences are selected within their stage,
and failures commit neither assignments nor sampler RNG state. Both runtimes
support wide paths, live modes, frozen arguments and intrinsic semantic domains.
Staged randc histories, aliased object identity and dynamic foreach combinations
remain open and are diagnosed.

Automatic constraint-function bodies admit packed locals and constants, typed
initializers, blocking assignments and compound-assignment RHS expressions,
mutable input copies, blocks, `if`/`else`, early returns, return-variable
assignments and fallthrough. Each branch carries its own local values; executed
initializers and assignments retain their evaluation checks even when their
values are unused. State-selected branches validate only the selected arm.
Procedural conditions reduce the full value before converting the truth value
to two-state, so an unknown condition is false and a known one bit is true.
Native and WASM tests cover independent argument stages, failed-solve field
preservation, recovery from state errors, and 130-bit local values and results.
Constant `repeat` counts and `for` loops that test a packed variable against
zero and shift it right by a known positive amount have exact finite expansions.
Shift steps preserve the variable's width and the body cannot update the loop
variable. The LRM's `count_ones` function follows this path. Lexical `break` and
`continue`, early returns, fresh automatic local declarations, and standalone
increment/decrement statements retain their source behavior. Repeat counts
apply casts before treating negative, X and Z values as zero under 12.7.2.
Each loop admits at most 256 iterations, and each call has an expansion budget
of 4,096 loop bodies. Packed `case`, `casez`, `casex` and `case inside` statements
preserve the first matching item, grouped labels, defaults, fallthrough and lexical loop
exits. Compiler symbol planes retain X and Z in normal case equality; wildcard
cases ignore the designated symbols in either operand under 12.5.1. Procedural
`===`/`!==` consume complete symbols, while `==?`/`!=?` take wildcards only from
the right operand and preserve unmatched left unknowns. Integral `inside`
compares singular members with those RHS wildcards and combines member and
inclusive-range predicates with four-state OR before any consuming result cast.
Fixed unpacked array members descend through every dimension to integral leaves,
including ascending, descending and negative coordinates. Class and scope plans
retain live state, candidate array relations and frozen X/Z at those leaves.
Case-inside uses the leaf types in its common comparison context; ordinary
function execution also traverses nested array labels. Function-origin literals
retain their procedural symbol semantics after expression-return expansion.
Simple integral ranges admit a `$` lower or upper endpoint. `case inside` uses
that asymmetric membership under 12.5.4, takes a branch only for a known true
match, and skips later labels after the first match.
Comparisons use the longest operand width and common signedness from 12.5;
unbased literals fill that context, and frozen or mixed random/state X/Z
comparisons remain exact. Nested case selectors preserve those symbols and
wildcard semantics during safe capture as well as compiler normalization.
Identical immutable state expressions share pre-solve captures and serialized
program inputs, keeping repeated symbol projections from inflating native capture
buffers. Raw X/Z reads and normalized values have separate caches. Dependency inventories, constants,
symbol planes and known predicates also reuse immutable subtree summaries.
Labels after an earlier match do not contribute evaluation errors. Class and
scope MLIR tests cover these rules and 130-bit results on native and WASM targets.
Larger, unbounded or general dynamic loops, dynamic-container membership,
tolerance ranges and pattern case statements, case qualifier reports,
non-integral comparisons, selected local assignment targets, non-integral locals,
and functions on other instance
receivers remain open. A candidate-selected
branch that can produce an evaluation error requires staged function execution
and is explicitly diagnosed.

`std::randomize` shares the same expression and admitted statement-body function
plans as class randomization. Non-instance user functions retain argument
priority and frozen body reads, including wide integral scope assignments.
Validation rejects output, inout and mutable ref formals, persistent local or
nonlocal writes, const writes, recursion and cyclic priority dependencies.
General procedural functions and scope calls through instance receivers remain
open.

Constraint guards use the FALSE/TRUE/ERROR/RANDOM algebra from 18.5.12.
Safe captures check null handles, array and queue bounds, and state tagged-union
arms before reading them. Only state TRUE/FALSE can suppress an error; a
proposed random value cannot hide it. Guarded body errors, frozen variables,
disabled blocks and unused function input evaluation follow the same rules.
Hard creation-validity predicates reach both the generated checker and Z3.
Live reference and argument masks independently remove sifted guard dependencies
from function-priority stages, including overlapping guard and body variables.
The versioned runtime records accept two consecutive mask captures when flag
`HAS_GUARD_INPUT_MASKS` is set. Native and WASM support these masks at arbitrary
integral width. General candidate-dependent accesses, unknown-plane predicates
outside admitted integral normalization, and complete runtime-sized foreach
solving remain open.

The little-endian sampling bundle has a versioned header, topological Boolean
gates, an atom-root directory and a trailer bounding the original residual
metadata independently. Native and WASM validators check all handles, inputs,
roots and lengths before specialization. Compiler Z3 remains statically linked
into compiler tools only. Native archive tests check both definitions and
unresolved symbols to ensure no Z3 API enters ordinary or LTO target archives.

Each elaborated module, interface, program, and package owns an initialization
RNG seeded with the same configured default. Static declaration initializers
and static process creation resume that scope's stream, including class objects
created before threads start. Lexical semantic owners supply the scope identity;
unrelated instances cannot consume one another's initialization draws. Dynamic
threads still split their parent's stream. Native, bytecode, and WASM execution
use the same bindings, including the optional `$urandom(seed)` argument in
static initializers.

Resolved external constraint bodies use the ordinary Z3 and runtime plans;
undefined implicit prototypes are empty constraints, and missing explicit
extern bodies are diagnosed.

`disable soft` supports direct variables and fixed packed paths, declaration
priority, conditional and implication guards, and live constraint block modes.
Both Z3 planning and the generated/runtime checkers preserve the same rules.
`randcase` interprets its self-determined weights as unsigned and sums them at
the maximum operand precision, including modular overflow and zero-total cases.
Integral weights may have arbitrary packed widths. Generated MLIR computes a
significant-bit mask and rejection-samples an exact rank using the process
stream; each rank has equal probability, including totals across word boundaries.
Tiny live totals avoid rejection over unused representation bits. Weight
expressions are evaluated once before the loop, and zero totals select no branch
and consume no RNG state. Native, bytecode and WASM use the same generated plan.

Finite `randsequence` grammars pass typed input, output, inout, and ref arguments.
Production weights support arbitrary integral widths. The compiler includes
enough carry bits for the natural sum of all alternatives, then emits exact
rejection sampling. Default weights are one, zero weights exclude alternatives,
and a zero total consumes no RNG state. Every explicit weight is evaluated once
on production entry, including single-rule productions; negative signed values
and X/Z values are diagnosed before selection. Native and WASM MLIR tests cover
64-bit sums that require carry bits, 257-bit weights, live tiny totals, and errors.
Defaults evaluate in the declaration scope only when used; actual values and
addresses are captured before formal bindings. Outputs copy back on return or
grammar abort, while ref arguments alias ordinary and managed storage. Each
rule owns its production results: one value for a single occurrence, or an
array indexed from 1 through the number of syntactic occurrences. Conditional
and case items retain separate slots, and repeat overwrites one slot on each
generation. Typed returns end the current production; break ends the current
grammar while respecting enclosing loops and nested grammars. These paths use
the ordinary typed Simulation IR for native and WASM execution, including
strings, wide integral values and unpacked arrays. Recursive activations and
general rand join remain open.

Fixed `foreach` constraints expand into ordinary Z3 predicates before random
dependency analysis in both class and scope randomization. Fixed packed and
unpacked dimensions can be multidimensional, descending, negative, or omitted;
fixed state arrays supply per-element captures. Typed iterator constants preserve selected
bit paths, and later iterations receive higher soft priority. An all-omitted
loop contributes an empty conjunction. Explicit ordering and live modes use the
ordinary exact runtime solver after expansion.

Constraint execution also includes IEEE 1800-2023 18.5.7.1 universal `foreach`
predicates over one-dimensional dynamic arrays and queues. The live container
size is read for each candidate, the declared iterator type is respected,
and an empty container contributes the identity value `true`. Generalized
runtime dimensions, associative arrays, and strings remain explicit boundaries.
An unconstrained `rand` associative array randomizes the value at each
existing key and keeps its keys (18.4), including inside a `rand` object;
constraints that reference a random associative array are rejected.
For hard constraints over state arrays and queues, complete integral domains
through 2^20 semantic assignments use generated mixed-radix rank traversal and
reservoir sampling. This preserves the uniform distribution over legal tuples
required by 18.5.9, including unequal companion completion counts and gaps in
the legal domain. Semantic cardinality counts actual enum values, including
sparse 130-bit enums and fixed arrays of those enums. Live constraint modes,
frozen properties and frozen array leaves use the same checker and commit path;
frozen unnamed enum values remain state. Empty containers, checker calls and
unsatisfiable relations retain their normal behavior.

The compiler emits one decoder per property/domain pattern and one rank loop,
without expanding the Cartesian sample space. Domain range overlap validation
visits each covered bit once, and both rank expansion and checker predicates
keep constants at their natural group widths. These passes are linear in the
lowered domain metadata and bit widths. Arrays of 2, 4, 8 and 12 three-value enum
leaves emit 33, 67, 135 and 203 decoder operations even though their semantic
spaces contain 9, 81, 6,561 and 531,441 ranks. The runtime visits every semantic
rank and retains one satisfying tuple using unbiased bounded draws; no Z3 is
linked or called by either runtime. MLIR fixtures cover native and bytecode
execution at O0/O3, and were checked in both WASM CLI and browser configurations.
Independent Z3 checks prove the emitted decoders, their unfrozen rank bijections, frozen
value preservation, the reservoir probability invariant and the equivalence
of domain checks at their natural widths.

This complete path excludes cyclic, soft, function-priority, explicit ordering,
distribution and randomized-container stages. Finite semantic domains combined
with those stages are diagnosed. Ordinary larger or staged runtime-`foreach`
relations retain their existing bounded solver behavior; generalized dynamic
container solving and the full compiler O(N) audit remain open.

Recursive polymorphic object paths specialize only dynamic classes with
observable randomization behavior (rand state, effective constraints, or
non-builtin lifecycle hooks). Null and behaviorally inert concrete classes
share one no-op alternative. Behavior-bearing alternatives retain their
effective virtual `pre_randomize` and `post_randomize` hooks in the selected
whole-object-graph plan. For finite acyclic type graphs, runtime class identity
also joins distinct paths that reach the same object: corresponding packed
leaves are one solver variable, lifecycle hooks run once, and each dynamic
container field is randomized once. Recursive type cycles remain an explicit
boundary because a static path expansion cannot distinguish an ancestor alias
from an arbitrarily long chain of fresh objects; they require a runtime object
graph walk.

| Feature | Current boundary |
| --- | --- |
| Generator | Executable PCG-XSH-RR with explicit `{state, increment}` state and the existing `--seed=<u64>` option. The bounded draw uses rejection debiasing. |
| Process streams | Executable. Root processes split from the seeded context stream, and forked processes split the active parent stream in lexical spawn order. Bytecode tasks retain their stream while dequeued from the scheduler. |
| Object streams | Executable for the constrained-randomization slice. Every root class contains two hidden, inline, GC-invisible `i64` fields. Construction initializes them before `new`; class copy reseeds the copy; `randomize()` advances this object-local PCG state in native and bytecode execution. |
| `$urandom` | Executable with zero arguments and with the optional seed argument. |
| `$urandom_range` | Executable with one or two arguments, including reversed bounds, with an unbiased inclusive range. |
| `$random` | Executable with zero arguments. The optional seed form reseeds the active stream and writes the generated value back to the seed variable. |
| `$srandom` | Executable with one argument; it reseeds the active process stream without drawing a value. |
| Stream snapshot API | The runtime C ABI supports exact get/set of both PCG words and pure state-local stepping/bounded draws. Object and `process` `get_randstate` / `set_randstate` source methods preserve exact snapshots; object and process `srandom` and `$urandom(seed)` use the same canonical seed mapping, replacing inherited stream selectors as well as state so equal explicit seeds give equal sequences across threads. |
| Constraint blocks and `randomize()` | Executable for packed integral, enum, packed aggregate, and frontend-admitted packed tagged-union `rand` properties and for `randc` properties up to the boundary below, including static properties in ordinary `randomize()`. Static values use class-wide design storage and static `randc` properties use one shared persistent cycle advanced by calls through any instance. An explicit object property argument list temporarily makes exactly those properties random for the call: unlisted instance `rand`/`randc` properties become state, listed non-rand properties become ordinary random variables, listed `randc` properties retain cyclic behavior, and the persistent `rand_mode` mask is not changed or consulted for that temporary selection. Explicitly selected static properties use the same class-wide storage and cycle. Enum enumerators and canonical tagged-union arm encodings form exact semantic domains rather than exposing unused packed encodings; packed arrays compose enum domains, while packed structs and untagged unions admit their complete integral representation. A base- or interface-class-typed handle is specialized at compile time for every compatible concrete class and selects the most-derived plan with generated in-process type tests; the receiver is evaluated once, and a null or otherwise unmatched handle returns zero without touching object state. Active, non-null `rand` object handles are traversed recursively: descendant variables, constraint blocks, and descendant-local state captures participate in one solve, with every ancestor `rand_mode`, null state, and descendant `constraint_mode` applied along the exact object path. Dynamic arrays and queues whose class-handle element type has no compatible concrete class in the compilation follow the exact zero-object LRM case: unconstrained size and null elements are retained without allocation, writes, or random draws; constrained size still uses ordinary resize semantics and initializes any growth to null. `randomize(null)` uses the same dynamic plan selection as `randomize()`, invokes `pre_randomize`, checks the current property values against the live constraint-mode mask, and invokes `post_randomize` only on success; it performs no random draws, property writes, or hidden-state changes. Total, side-effect-free hard expression constraints, any number of soft expression preferences including implication- and conditional-guarded preferences, `inside` ranges, implication, conditional constraints, uniqueness, inheritance, package/design captures, inline `with` constraints, and side-effect-free user constraint functions compile to pointer-free SSA. Packed concatenation and known-positive constant replication are preserved in compile-time definitions and in residual programs; replication uses a fixed-width geometric placement multiplier rather than expanding one runtime operation per repetition. Admitted functions have packed input or const-ref arguments and define their result with an expression return, blocking result assignment, or automatic statement body using packed locals, initializers, blocking assignments, if/else, packed case/casex/casez/case-inside, admitted finite loops and early returns; nested calls are expanded recursively and virtual calls on the randomized receiver resolve against the selected concrete-class plan. Rand packed value paths passed as function arguments remain solver variables and create bit-range-precise LRM implicit solve-order edges, while rand properties read only inside the function body are captured at their pre-solve values. Dynamic candidate-dependent packed selections in those paths remain unsupported. Each constraint tree is separated into its hard projection and guarded soft predicates. Later class declarations, derived-class constraints, and inline constraints receive progressively higher soft priority. For capture-free formulas, Z3 selects the lexicographically best satisfiable priority set and proves exact structural domains, definitions, or tables for generated code. Capture-dependent or dynamically disabled preferences remain on the complete residual traversal, which selects the best enabled priority set before dropping lower-priority preferences; the compiled-plan sampler decides each preference against the relation of its own input component. Fixed-width shifts are total, including oversized amounts. Division and modulo admit variable and state divisors, including wide and signed operands. Integral power admits signed self-determined exponents with the Table 11-4 rules for zero, one, minus one, parity, and truncated reciprocals. Compiler value and unknown planes propagate undefined arithmetic until a legal two-state conversion consumes its unknown bits; bitwise and reduction operators preserve known absorbing bits. Undefined uncoerced constraint values, including zero divisors and zero raised to a negative exponent, cannot be sampled. These operators participate in compiler Z3 proofs and generated definitions; target fallback executes the compiled Boolean plan. Packed bit-selects and constant or variable indexed part-selects preserve value, X and Z planes through computed operands and frozen array elements. General procedural functions and non-integral selections remain open. The object-local PCG draw chooses the starting assignment. Generated code checks up to 64 cyclic semantic candidates, then invokes the fallback with a deterministic attempt budget of up to 2^20 semantic assignments. Property and hidden `randc` cycle-state writes occur only on the success edge, so failed solves commit nothing. Constraint functions with loops beyond the admitted finite expansions, dynamic-container membership, tolerance ranges, pattern cases and case qualifier reports, non-integral locals, selected local assignment targets, candidate-selected evaluation errors, or an explicit receiver other than the randomized object, system functions beyond the integral bitstream set, or non-integral selections remain unsupported. |
| `std::randomize` | Executable for up to 64 distinct direct variable identifiers naming automatic, formal, static, package, or design-scope values with fixed packed integral, enum, packed aggregate, scalar real/shortreal, or frontend-admitted packed tagged-union types. The listed variables are the random set; every other referenced value is captured as state at the call. Inline total expression constraints, soft priorities, semantic domains, direct-variable `solve before`, and non-instance expression and admitted automatic statement-body function calls use the same frozen solver plan as object randomization. Fixed packed paths inside constraints retain their exact bit masks; listed arguments remain direct identifiers. Successful solves commit every listed variable and advance the active process or initialization stream; failed solves commit no variable changes. The zero-argument form checks its inline constraints without changing variables or advancing the stream. Dynamic containers, class handles, general procedural functions, instance receiver calls, and selected list entries remain explicit diagnostics. |
| `rand_mode` / `constraint_mode` | Class-wide and property-specific `rand_mode` setters and queries are executable through zero-initialized disabled-property state. Instance properties use the object's mask; each static random property has one class-wide mode word shared by every instance. Both `C::property.rand_mode(...)` and object-qualified static-property forms address that shared word, and class-wide calls use dynamic-class dispatch to update the exact inherited static set. Disabled properties retain their current values, participate in constraints as state variables, consume no search-domain bits, and are not written on success. Class-wide `constraint_mode(on_off)` and named-block setters and queries are executable through a separate disabled-block mask for up to 64 effective inherited constraint blocks. Overrides retain the base block's stable bit; `constraint ... extends` declarations append under that same bit. The all-enabled constraint path retains its Z3-planned sampler. Any disabled block bypasses plan-specific narrowing and extra draws, uses the generated live-mask checker, and passes both property and constraint masks to the deterministic residual solver. Inline constraints remain unmaskable. Fixed unpacked integral arrays also have an arbitrary-width disabled-leaf bitmap. Element and subarray setters preserve every other leaf's mode; whole-array and class-wide setters reset the bitmap. A static array shares its bitmap across instances, and dynamic-class dispatch resets derived arrays through base handles. Invalid array indices have no mode storage. Exact masked plan sampling freezes the inactive leaves, and four-state commits preserve their complete symbols. If all random properties or array elements are disabled, `randomize()` checks current values against the enabled hard constraints without advancing the object stream or writing properties. |
| `solve before` | Direct rand-property DAGs are executable. The compiler validates self edges and cycles, reorients pure alias classes toward the earliest ordered member, and emits conditional sampling as MLIR when Z3 proves complete global/component tables or exact structural plans whose transitive dependencies follow the order. Residual programs retain constraint-block-aware ordering edges. For packed integral aggregates of arbitrary width, the compiled-plan sampler selects the optimal enabled soft priorities, then counts existential projections of each active topological layer and samples its distinct legal values uniformly. Compiler and runtime DAG plans defer partially ordered fields to their latest legal layer and include unordered fields in the final joint group. Cofactoring the original relation preserves constraints on later fields. Live captures, semantic domains, partial rand modes and constraint modes use the same exact policy; disabling an ordering block removes its edges. Remaining unordered fields use their conditional joint distribution. Version-2 metadata carries multiword before/after masks and validates padding, block identities and declaration cycles before applying modes. Real ordering and general selected paths remain incomplete. Without compiler Z3, narrow ordered residual traversal retains its 2^20 semantic-assignment limit and wide ordered residuals are diagnosed. |
| `dist` | With compiler-generated plans, integral targets up to 64 bits support constant and state-valued two-state endpoints, unsigned state and candidate expression weights of arbitrary width, original range mass after filtering, additive overlapping items, and a single noncontiguous default item. Exact projected counts preserve target weights independently of unweighted companion completions in arbitrarily wide aggregates. Enabled soft distributions follow global soft priority; whole-target solve-before ordering conditions the distribution on preceding choices. Candidate expression weights may reference their target or explicit predecessors of the complete target. Multiple active distributions combine their per-value masses while retaining every legal binding. State guards and candidate guards fully solved by preceding always-active or same-block edges are supported. A target is a `rand` property, a constant selection of one (bit- and part-selects, packed members, fixed-array elements), or any other integral expression containing a `rand` variable (18.5.3), which is weighted by its values rather than by its witnesses. Split-target ordering, unresolved candidate activation, independent function-priority stages with distributions, unresolved companion weights, candidate-dependent endpoints, unknown-plane state endpoints, and real ranges remain explicit boundaries. `dist` on `randc` and default `:=` are illegal. Without compiler Z3, only complete `rand` properties are distributed, under the existing bounded checked 64-bit policy. |
| `randc` | Executable for integral scalar and fixed-array leaves through 32 bits, including sparse signed enums and qualified structure members. Compiler Z3 produces one shared hard relation. Each cyclic stage projects its exact legal domain with later cyclic and ordinary fields free, then permutes dense legal ranks; ordinary fields are sampled with cyclic values frozen. Changed legal-domain snapshots restart the cycle, and tentative histories, fields and sampler RNG commit only after the final solve succeeds. Native, bytecode and WASM MLIR cover multidimensional and negative bounds, instance and static sharing, copied per-element histories, live bounds, checker calls, element modes, frozen X/Z, explicit selection and failure retention. Dynamic-container histories, equal-domain source constraint-context changes, runtime foreach combinations, function-priority stages and aliased object cycles remain open. The runtime permutation and projected sampling remain independent of Z3. Larger leaf representations are diagnosed. |
| `pre_randomize` / `post_randomize` | Executable with virtual override semantics through the same compile-time-specialized dynamic-class plan as `randomize()`. The effective `pre_randomize` runs after receiver selection but before mode, constraint, property, or stream state is read. The effective `post_randomize` runs after all successful property and `randc` state commits, including a successful current-value check when every property is disabled, and is skipped on failure and null receivers. Inherited hooks receive a receiver cast to their declaring class. Package and design captures used by either hook are propagated through the generated call plan. |
| Compiler Z3 planning | A pinned Z3 4.13.4 static library is built from source for compiler analysis. Residual programs are decoded into a temporary MLIR SMT bit-vector formula, then an in-process shim executes that formula through the Z3 API; no solver subprocess or SMT-LIB round trip is used. Z3 diagnoses constraints that are unsatisfiable for every runtime capture value and computes conservative per-property domains under a deterministic resource limit. Contiguous domains up to 64 bits are folded without modulo bias into the generated tier-0 proposal: power-of-two cardinalities use random low bits, while arbitrary cardinalities use generated 64-bit rejection sampling and overflow-safe cyclic retry traversal. Wider variables skip width-linear min/max discovery and instead use bounded solution enumeration when sparse. Direct signed and unsigned capture bounds up to 64 bits, including side-effect-free capture expressions evaluated once at driver entry, such as `x <= limit`, `x >= limit`, or their strict forms, become parameterized MLIR samplers: captures are masked once, signed endpoints are mapped to sign-bit-biased unsigned coordinates, strict endpoints are normalized with overflow guards, compatible lower and upper bounds and compile-time envelopes are intersected, and the runtime cardinality is sampled with unbiased 64-bit rejection. A zero cardinality represents the full 2^64-element domain and selects the original random draw while generated modulo operations use a nonzero safe divisor. An empty runtime intersection fails before modulo or fallback, while an exact Z3 proof removes both checking and fallback on the valid edge. Direct variable equalities are collapsed into Z3-verified alias classes, so constraints such as `x == y` sample one field and copy it instead of relying on rejection. Acyclic arithmetic, bitwise, comparison, reduction, logical, conditional-select, concatenation, and constant-replication definition chains such as `x == y + 1; y == z + 1` are extracted from the versioned RPN program, topologically ordered, and emitted directly as MLIR; these definitions may read runtime captures, so `x == f(capture)` becomes generated MLIR instead of runtime search. Cyclic components remain on the checker/runtime path. Alias classes and definitions share one dependency plan: a uniquely defined class member becomes the representative, definition reads are canonicalized to representatives, and aliases are copied after their defining chains. Hard constraints are partitioned deterministically by shared random properties before table planning, with capture dependencies tracked per constraint; the conservative component masks are exposed with the analysis so distribution-sensitive clients can prove that unresolved variables do not cross ordering layers. Capture-free residual formulas and connected components with at most 4096 representation bits are completely enumerated when they have at most 16 solutions. Clients that require the complete joint distribution can request bounded global enumeration even when a structural proposal is already exact; if that global enumeration exceeds its bound, component enumeration still proceeds. Each component table is independently Z3-proven and retained even when another component exceeds the limits or depends on a runtime capture; the whole-formula proof composes retained tables with structural capture plans, while unresolved components continue through the generated checker and runtime fallback. When every component is covered, the Cartesian product may be exact even with far more than 16 aggregate solutions and can bypass checking. Every complete solution set becomes a sorted assignment table: power-of-two cardinalities use random low bits, while other cardinalities use generated 64-bit rejection sampling that threads additional object-local PCG draws only across rejection edges and commits the accepted state once. `solve before` global and component tables instead become topologically layered decision trees, with uniform sampling at each conditional node. For exact structural plans, lowering computes the transitive property dependency closure after validating that every solver step was materialized, and routes any later-layer source that determines an earlier-layer target to the ordered residual fallback. When Z3 proves that a structural proposal or complete assignment-table product implies the hard formula, generated code commits directly. If a definition or alias overwrites an independently constrained target domain, the generated checker and runtime fallback are retained. The encoder preserves implicit conversions structurally instead of turning converted constants into runtime captures. Native compiler workers use Z3's thread-safe build with isolated thread-local contexts, while the non-pthread WebAssembly compiler retains single-threaded polling mode and a process gate. Z3 is statically linked only into compiler tools. Compiler-generated Boolean sampling plans preserve complete integral residual support in both target runtimes without a solver dependency. With `OBELISK_ENABLE_Z3=OFF`, conservative analysis retains the existing bounded traversal and its explicit support boundaries. |

Known-answer runtime tests pin the PCG sequence, state restoration, and bounded
draw behavior. Driver tests compare native and bytecode output at `-O0` and
`-O3` and across the 1800-2017 and 1800-2023 language modes.

Compiled weighted sampling plans keep raw source item identities and unsigned
captured weights rather than a fixed-width normalization coefficient. For a
`:=` range the original mass is its full numeric cardinality times the weight;
for `:/` the original mass is the weight. Compiler Z3 bit-blasts the
parameterized hard and soft predicates; the sampler binds live state and selects
enabled preferences. Existential projection then counts
legal target values within each source item, including enum-domain filtering.
The original mass is divided equally among those values. Excluded items vanish,
overlapping contributions add, and default complement intervals share one item
identity. Common denominators and sampling ranks use arbitrary-precision
integers, including full 2^64 ranges and weights wider than 64 bits.
State-valued integral endpoints are read once per call. Inclusive bounds are
normalized before clipping to the target domain, while the original range size
is retained for := mass. Captured ordinal bounds use the same interval metadata
in native and WASM. A default item is resolved as the complement of all explicit
intervals regardless of their weights, including empty clipped intervals and
a default-only distribution. Constant endpoint casts retain every intermediate
width and signedness so membership and the weighted policy agree.

A constant selection of a `rand` property is weighted as its assignment field.
Any other target expression is exported bit by bit after its group's guard
(`END_DIST_TARGET_BIT`); the target sampler adds one auxiliary variable per
exported bit, constrained equal to it, and weighs the projection onto those
auxiliaries. Each value of the expression thus receives its item's mass however
many assignments produce it. Auxiliaries are decided with the last of their
inputs and are never written back, and the compiler rejects an expression whose
inputs a `solve before` path splits, as it does for fields.

Integral weights may also depend on the distributed target or its explicit
`solve before` predecessors. The compiler emits each expression into a shared
immutable temporary, zero-extends its unsigned bit pattern, and asks Z3 for one
Boolean root per result bit. Each weight adds one marker and one root record
per result bit while sharing the compiled Boolean nodes. The target
sampler assigns each set bit its exact power-of-two mass using the original
source item's legal-target denominator. Captured original `:=` cardinalities
scale those expressions before exporting their bits. This preserves additive
overlaps, default complements and arbitrary-width weights without multiplying
target mass by unweighted completion counts. Both runtimes validate complete,
unique bit sequences and reject weights that still vary with unresolved
companions. Neither runtime links or calls Z3. General companion-dependent
weights, candidate-dependent endpoints and function-priority combinations remain
open; the full compiler O(N) audit is still incomplete.

After each solve-before layer is accepted, the original relation is cofactored
by that prefix. Weighted targets are drawn from their projected measure, then
other fields in that layer are drawn uniformly conditional on those targets.
Thus a target's mass does not grow with its number of unweighted completions.
For several distributions, products of their per-value factors provide a
positive measure on the intersection of their permitted values; conflicting
requested marginals retain legal bindings as permitted by 18.5.3. Item diagrams
are built from the compiled Boolean relation and reused for combination counts.
Sampling failure commits neither assignment nor sampler RNG state; caller
proposal draws remain observable.

The MLIR weighted class, dynamic-endpoint, candidate-weight and scope runtime
fixtures check
filtered := and :/ ranges, live and reversed endpoints, domain clipping,
default complements, 129-bit endpoints and weights, wide companions, soft
conflicts, guarded activation, ordering, negative signed weights, candidate
zero-weight exclusions, mode changes, checker-only calls, transactional
failures and multiple distributions at O0/O3 in native and bytecode execution.
Runtime C ABI tests also cover an overlapping full range with
2^64-1 legal values, whose exact normalization exceeds fixed-width arithmetic.
The same MLIR fixtures run with the Z3-free WASM runtime and the browser
launcher with both WASM exception-handling features disabled.
