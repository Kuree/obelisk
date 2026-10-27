# Coverage codec and scaling

The compiler, native runtime, bytecode runtime, and `talos-cov` share the
portable codec in `lib/Coverage/CoverageDatabase.cpp`. The declarative source
of record sizes, domains, and wire constants is
`include/talos/Coverage/CoverageDatabase.td`; `talos-tblgen` generates the
declarations, parser descriptors, and serializer descriptors. Embedded static
schemas and persisted runs use the same codec. The format is unversioned while
it is under development: producers and consumers are built together.

## Wire layout

Integers are little endian. The header is 64 bytes:

| Offset | Width | Field |
| --- | --- | --- |
| 0 | 8 | `OBCOV\r\n\x1a` magic |
| 8 | 4 | Flags, currently zero |
| 12 | 4 | Directory record count |
| 16 | 8 | Total file size, including checksum |
| 24 | 8 | Directory offset, 64 |
| 32 | 32 | Static schema fingerprint |

Each directory entry contains a 32-bit section kind, 32-bit section flags,
64-bit byte offset, 64-bit byte length, and 64-bit record count. Entries and
records use the canonical order specified by the codec. Sections begin at
eight-byte boundaries; padding is zero. The final 32 bytes contain SHA-256 of
every preceding byte, including the header, directory, and section padding.
Offsets and spans cannot address the checksum trailer.

Serialization plans section lengths, emits each section once, and hashes bytes
as the sink accepts them. It then writes the digest as the final callback.
Validation, sink failure, and allocation diagnostics remain explicit. Atomic
file publication occurs only after a complete successful write.

The JSON report is identified as `talos-coverage-report`; it is also the
payload embedded in the HTML report. Text, JSON, HTML, LCOV, inspect, and merge
all read through the shared parser, and `talos-opt
--test-talos-print-coverage-schema` prints an embedded schema through it.

## Cost model

Let N include represented records, string/key bytes, referenced payload spans,
and generated report rows. Fixed-width IDs, UUIDs, and digests have bounded key
width. The portable radix index has bounded lookup depth, no hash collision
case, stable borrowed record addresses, and linear ordered iteration. Strings
and vectors use prefix-free encodings and cost O(their encoded byte length).
Canonical sorting uses the same index, with an adjacent-order fast path.

The codec's relationship indexing, ordinary record validation, schema hashing,
configuration bundle traversal, serialization, parsing, and merge visit each
represented row or owned payload span a bounded number of times. Configuration
views are built once per operation; hashing each configuration visits its own
rows. Merge rebases spans and maps resolved identities through indexed semantic
keys. It does not scan every provider table for every configuration or bin.

Runtime construction partitions static templates by covergroup type once and
shares the global reserved-ID indexes. Each new configuration resolves and
validates its own template projection, retains immutable local metadata for
sampling, and appends its resolved columns with geometric capacity growth.
Construction no longer copies, validates, sorts, or shifts earlier bundles.
Snapshots and loads pack the bundles in radix order and rebase each referenced
payload once. These storage and indexing steps cost O(N), including amortized
vector growth; exact semantic expansion retains the bounds described below.

Runtime method queries index configurations by type, items by stable identity,
and bins by their owning item. Loaded configurations are partitioned once;
binding adopts each rebased bundle into its static type projection. A single
indexed join groups persisted instances, options, counters, and sparse tuples,
and retains their immutable profiles until the next database load. Queries visit
their configuration's contributors and each item's actual bins, rather than
rescanning every global table per configuration, instance, or item. Index
construction and these joins cost O(N); each ordinary aggregate query costs the
represented contributor/profile data it visits. Symbolic cross unions retain
their separate expansion bounds below.

Snapshot generation indexes items, types, handles, and cross identities once.
Report generation groups configuration options once, aggregates selected
counters once, and visits actual contributors. Toggle field intervals are
decomposed into at most 64 dyadic prefixes per interval; point queries inspect
only present prefix widths. LCOV groups physical points by source line once and
then emits its required rows per selected run. HTML graph membership and item
lookups use indexes rather than repeated edge or item searches.

These bounds describe data traversal. Exact arbitrary-width arithmetic also
costs the number of limbs operated on. Expanding transition alternatives,
enumerating requested missing cross bins, and counting the union of different
symbolic cross graphs cost their generated expansion or union-state graph;
those graphs can exceed the compact input size. Existing traversal and APInt
limits still apply. Browser locale-aware presentation sorts have their usual
comparison-sort cost. This is not a claim that every compiler or runtime
algorithm outside database generation has been made linear.

## LRM and MLIR review

The binary format is an implementation choice. Review against the local IEEE
1800-2023 text (`build/lrm-2023.txt`) covers constructor arguments and independent
covergroup instances (19.3), instance/type options and merging (19.7), coverage
methods and bin counts (19.8), and the disjoint metric slices used by database
operations (40). Physical row IDs and offsets do not change logical bins,
weights, thresholds, excluded/illegal bins, or automatic cross denominators.
Malformed references, cycles, checksums, and conflicting bundles are rejected;
merge remains transactional and counters retain saturation behavior.

The scaling source probe found that arrays of covergroup handles lacked
aggregate storage geometry. The common Simulation provenance helper now gives
each covergroup element a 64-bit span and alignment. The regression constructs
16 different configurations, samples every instance, checks the type's bin
counts, writes native and bytecode databases, reports them, and merges them.
The change uses the existing typed storage layout contract and keeps native and
bytecode representation consistent.

Subsequent probes found the same omission in variable-size containers. Dynamic
arrays (7.5), queues (7.10), associative arrays (7.8), and typed mailboxes (15.4)
now describe a covergroup element as an ordinary two-state 64-bit identity,
without managed-heap trace entries. The existing container operations copy that
identity; they do not construct another coverage instance. The regression checks
resize preservation, independent container copies, queue endpoints, sparse
keys, deletion, mailbox transfer, and exact persisted counters in both execution
tiers at O0 and O3. Type-level coverage retains all constructed instances (19.8).
Container, mailbox, and associative-array verifiers reject truncated, four-state,
or managed-heap representations. Descriptor construction and scalar handle
transfers add constant work; bulk copies retain their existing linear traversal.

Handle probes also exposed missing covergroup identity comparisons and null
conditions. The new pure, commutative `simulation.covergroup.equal` operation
compares instance identities, including across nominal covergroup types; equal
configuration digests do not imply equal instances (19.3, 8.12). Null operands,
logical/case/wildcard equality, unary negation, `if`, array element equality, and
conditional expressions share this operation. Ambiguous conditional arms
preserve equal handles and otherwise produce the type's null default (11.4.11).
Symbol verification uses the supplied `SymbolTableCollection`; folding only
proves reflexivity and known nulls. Native lowering uses converted operands in
one LLVM integer comparison, and bytecode emits one comparison instruction.
The source, backend, canonicalization, and invalid-symbol regressions cover
both tiers at O0 and O3 without adding runtime metadata lookups.

Constructor scaling review preserves distinct instances and configuration
identity (19.3), the single static type-option profile across configurations
(19.7.1), and retained type-level instance coverage (19.8). Local projections
retain functional exclusions and every owned transition, cross, and value-set
payload; the persisted static hierarchy stays in the master schema. Malformed
constructors and conflicting static profiles fail before publishing metadata.
Packing is transactional and shared with merge. Small regressions compare the
complete encoded image after repacking and exercise failed insertion, failed
packing, configuration reuse, and both execution tiers. The runtime indexes
remain context-owned and independent of MLIR; existing dialect verification
and native/bytecode conversion tests exercise the compiler boundary.

Small native and bytecode probes also found that `new()` rejected a covergroup
with no coverage items. IEEE 19.11 explicitly permits this zero-denominator
case. Empty groups now retain independent identities, constructor profiles,
names, and persisted instances without synthesizing items or bins. Their
queries return zero bin counts, a score of 0 for positive group weight or 100
for zero group weight, and no contribution to the overall score. Binding
checks the declared configuration key directly instead of using a nonempty
item list as a proxy for validity. Functional persistence availability follows
the declared types rather than scanning bin/exclusion pairs, including when the
design consists entirely of empty groups. This adds bounded key lookups and
constant query/availability work. The source regression covers O0/O3 in both
tiers, load and merge, and JSON/text/HTML/LCOV generation; the runtime unit checks
invalid references as well as valid empty identities. Existing typed declaration,
symbol, and payload-batch verification accepts the empty inventories without
changing the MLIR operation contracts.

Method-query review preserves the instance-weighted average, name-based bin
union, and maximum contributing `at_least` threshold required by 19.11.1 and
19.11.3. Explicitly named persisted instances merge within their configuration;
generated identities remain independent. Item projections borrow the original
profile ordinal, including procedural instance options. Database loads reset
derived indexes and loaded profiles while retaining live instance counters and
immutable configuration metadata. Procedural type options still affect later
queries without rewriting cached instance profiles. The new load-refresh unit
uses two tiny runs; the existing 16-configuration source fixture now also checks
loaded and merged type/item queries in both tiers. A small MLIR conversion test
checks empty constructor batches, absence of synthesized schema items/bins,
the three query results, both backend ABIs, and LLVM IR verification.

An item-query probe against 19.11.3 found that point/cross `option.weight`
changes were persisted but ignored by item-level type queries. Averaging now
reads each contributing instance's original item-profile ordinal, for both
live and loaded profiles. It adds one bounds check and scalar read per
contributor and shares the same aggregation path for both. The two-instance
regression covers distinct group/item weights, live changes, automatic cross
tuples after load, zero weights, and consistent optional method counts. MLIR
checks preserve the owning point/cross ID through assignment, query, and both
backends; the compiler operation contracts remain unchanged.

A pre-construction probe against 19.7.1, 19.10, and 19.11 found that the
implicit static `type_option` structures were zero-initialized and empty type
queries ignored zero weights. Preparation now freezes elaborated integral,
string, and real definitions, supplies the LRM field defaults, and invokes the
existing static initializer functions before spawning processes. Constant
functions do not run again at `new()`, and later procedural edits remain in
effect. Embedded and inherited groups receive distinct compiler storage
identities even when their source display paths coincide. Point/cross
qualifiers address static storage while evaluating their group receiver once.

Initialization uses linear semantic walks, an owner-local definition index, at most
seven fields per owner, and shared MLIR symbol tables. Runtime option ownership
and mutable profiles use bounded radix indexes; each write changes only its
owner in that type's bound configurations. This avoids both repeated global
schema scans and reapplying every item override after each write. Empty method
queries perform indexed weight lookups, return zero optional counts, and remain
excluded from overall coverage. These changes use the existing persisted type
option rows and require no codec or report-field changes. Fast MLIR checks cover
startup ordering, constant values, independent embedded owners, receiver
selection, both backend ABIs, and LLVM verification. The executable regression
has two small configurations and validates JSON/text/HTML/LCOV generation.

Indexes are local to one invocation and borrow immutable input. There are no
global mutable compiler caches. Wire declarations stay in TableGen, and runtime
code stays independent of MLIR and LLVM. This follows the analysis-lifetime and
declarative-definition guidance in [MLIR pass management](https://mlir.llvm.org/docs/PassManagement/)
and [MLIR operation definitions](https://mlir.llvm.org/docs/DefiningDialects/Operations/).

A procedural `type_option.distribute_first` probe found that legal assignments
were rejected. IEEE 19.7.1 permits these updates, while 19.5.1.1 captures the
distribution/filter order when each instance is constructed. The typed option
now publishes its elaborated definition at startup and accepts group-level
procedural updates. Each constructor records the effective boolean in its
configuration fingerprint; omitted defaults and an explicit procedural false
share the same resolved row. Existing bins and counters stay immutable. Static
profile validation excludes this construction policy while retaining checks
for scoring options and definition-only options.

The context-wide policy uses bounded numeric indexes. An assignment changes
one scalar without visiting existing configurations, instances, or bins. A
constructor without expression payloads caches the exact configuration for
each of its three possible override states (unset, false, true); repeated
construction uses bounded lookups. Schema replacement invalidates this cache.
Loading with live configurations validates their retained keys through one
linear index build and bounded membership checks, avoiding a configurations
times live-configurations scan. The cache regression loads two configurations
while live instances exist, then constructs again using the current policy.
Resolution adds one option row or updates its already indexed ordinal, without
a nested search. The persistence codec retains construction-time policy rows
and separate run-local mutable type-option rows using the existing enum and
record columns, so its layout stays unchanged.

The coverage tool shows the captured `distribute_first` boolean per instance
in JSON, text, and HTML. Final procedural values appear as run-local
`distribute_first_overrides` on JSON/HTML types and as type metadata in text;
different construction policies do not create scoring-profile conflicts.
Discarding test detail removes final run-local policy overrides while retaining
both captured construction policies; it cannot manufacture a final static
value for the combined executions. Merging differing final policies preserves
each run's metadata and leaves instance scores unchanged.
LCOV continues to project line coverage. The small executable regression checks
all four report generators, load, merge, and native/bytecode parity. MLIR
regressions verify startup/write/construction ordering through canonicalization
and CSE, reject point and instance owners, check both backend ABIs, and verify
the resulting LLVM IR. The option remains a TableGen enum with typed operands
and coverage-resource read/write effects, following
[MLIR ODS](https://mlir.llvm.org/docs/DefiningDialects/Operations/) and
[memory effect interfaces](https://mlir.llvm.org/doxygen/SideEffectInterfaces_8h.html).

An instance-option read probe against IEEE 19.7 and 19.10 found that names,
comments, and definition-only option fields fell through to ordinary member
lowering and failed to compile. Typed reads now cover all primitive group,
point, and cross option fields. Construction-only values come from the captured
configuration, including group defaults and item overrides; live name/comment
reads see procedural edits. String results are independent managed snapshots.
The restrictions on procedural writes remain those of 19.7, and weight, goal,
comment, and per-instance settings do not become lower-owner defaults.

Compiler preparation indexes complete semantic handle types in one shared
analysis; these paths may omit instance wrappers in the actual symbol tree.
Option accesses use that type index and cache each body once per worker, then
use the shared symbol-table index for each item.
They no longer walk every item for every field and evaluate the receiver once,
including embedded members and inherited selectors. Runtime profiles build
construction-option and inherited-name indexes in linear passes. Constructor
option resolution, numeric item reads, and constructor comment initialization
use bounded numeric indexes;
inherited fallback costs the selector's name bytes, and string snapshots cost
the returned bytes. Comments use the same bounded index as other runtime
coverage metadata. No getter scans all schema items or configurations.

The managed-string read is declared in TableGen with typed handle operands,
a string result, coverage read effects, and heap allocation/write effects.
This follows [MLIR ODS](https://mlir.llvm.org/docs/DefiningDialects/Operations/),
[symbol tables](https://mlir.llvm.org/docs/SymbolsAndSymbolTables/), and
[memory effects](https://mlir.llvm.org/doxygen/SideEffectInterfaces_8h.html).
Small MLIR regressions cover every field, ordered reads around mutations,
invalid kinds/scopes, both backend ABIs, and LLVM verification. Two fast runtime
units cover defaults, unchanged outputs on invalid arguments, and snapshots
across edits and garbage collection. A three-configuration executable fixture
checks inherited and embedded owners, receiver evaluation, constructor values,
and native/bytecode parity. Existing persistence columns already store these
settings; JSON/text/HTML/LCOV generation is checked without changing the format.

A follow-up 19.10 probe found that copying an entire implicit `option`
structure still failed after primitive field reads were supported. Whole group,
point, and cross values now read their eleven or six fields in declaration
order and construct the ordinary normalized unpacked struct. Every copy
resolves its typed owner once and evaluates its receiver once; each scalar
uses the existing indexed getter and each string uses the managed snapshot
getter. Copies passed through functions, nested records, or dynamic arrays
remain independent of later name, weight, or comment edits on the live group.
Ordinary user records with a field named `option` keep their usual lowering.

The field pass is O(F) with F bounded by eleven, plus the returned string
bytes; it adds no schema or configuration traversal. Existing
`simulation.aggregate.construct` and typed coverage read operations handle
both backends, with their existing verifiers, layout metadata, and memory
effects. This follows the declarative operation and analysis reuse guidance
in [MLIR ODS](https://mlir.llvm.org/docs/DefiningDialects/Operations/) and
[pass management](https://mlir.llvm.org/docs/PassManagement/), while retaining
[string allocation effects](https://mlir.llvm.org/doxygen/SideEffectInterfaces_8h.html).
MLIR regressions check declaration order, one receiver per copy, stable group
and item handles, canonicalization/CSE, both backend ABIs, and LLVM verification.
A two-configuration executable fixture checks all fields, function and array
copies, and native/bytecode parity, together with JSON/text/HTML/LCOV output.
The runtime and persistence format need no new operations or columns for
these value copies; no additional runtime unit or scaling benchmark is needed.

Probes of 11.4.1-2 and 19.10 next found that instance-option increments and
compound assignments failed, while static increments changed struct storage
without publishing the new coverage profile. Incrementing a zero type weight
left `$get_coverage()` at 100% despite uncovered bins. Mutable fields now use
the shared captured-lvalue lowering: one typed receiver or static reference,
the existing arithmetic and selection conversions, and an owner-local write.
Static writes update both the type profile and ordinary struct storage after
conversion to the field type. Prefix/postfix results, packed partial writes,
string character writes, and timed blocking updates follow the same path.
Captured owners and indices cross suspension as typed continuation arguments.
The definition-only restrictions in 19.7 and 19.7.1 still apply.

The review also removed setter scans that made a sequence of item updates
quadratic. Integral setters reuse the immutable item-position index. Threshold
changes visit that item's indexed bin list; a group change walks each item
and its bins once, preserving explicit item overrides. Numeric owner updates
cost O(1), an item threshold update costs O(K) for its K bins, and a group
threshold update costs O(I + B). Mutable threshold and cross-print maps use
bounded radix indexes. String updates additionally cost their bytes; a static
type update visits its bound configurations once. Coverage concatenations
skip the pairwise propagation used for captured managed containers.

The MLIR review reuses typed coverage operations and their coverage/heap
effects, ordinary design references, arithmetic, and continuation machinery,
consistent with [MLIR side-effect modeling](https://mlir.llvm.org/docs/Rationale/SideEffectsAndSpeculation/).
MLIR checks cover single receiver/index evaluation, prefix/postfix SSA values,
field-width conversion before publication, canonicalization/CSE, suspension,
both backend ABIs, and LLVM verification. A two-instance, one-configuration
execution fixture checks all twelve compound operators, string/packed writes,
snapshot isolation, concurrent timed updates, and native/bytecode parity.
Seven definition-only fields have diagnostic probes. One small runtime unit
checks declaration ordinals different from table order, owner-local thresholds,
explicit overrides, and instance isolation. Text, JSON, HTML, and LCOV
generation and backend parity are checked; the wire format and ABI need no
additional columns or ops.

Probes of 10.4.2 and 19.7 then found that valid nonblocking option assignments
were rejected during lowering. Instance and static options now capture typed
managed reference paths and reuse the ordinary managed NBA queue. Integral
paths contain positioned i64 patch bits and a mask; strings carry tagged
managed words, and character paths retain the evaluated index. Partial writes
read the live field at commit so successive NBAs preserve untouched bits.
String selection preserves X/Z indices as invalid rather than clearing the
unknown bits to zero; invalid indices and NUL writes leave the string unchanged.
Static commits publish both the runtime type profile and the ordinary field
storage, after conversion to the declared field width. Receivers, indices,
RHS values, delay expressions and event repetition counts are captured once.
Program assignments use Re-NBA. Definition-only option restrictions still
apply, and schema verification rejects foreign owners and point-only misuse
of cross options.

The managed update calendar uses separate monotone radix heaps for NBA and
Re-NBA. The key is the fixed-width pair `(due time, enqueue sequence)`; an
update crosses at most 128 buckets over its lifetime. Enqueue and removal
therefore cost O(N) in aggregate for N updates, including distinct delayed
deadlines. Deadline inspection never advances the lower bound. Redistribution
reserves all destination storage before moving entries, preserving scheduled
GC roots on allocation failure. Mixed barriers index each pending native,
bytecode and event vector tail once, then compact consumed entries once.
They avoid scanning or shifting a whole pending vector per managed write.
Cancellation leaves positions stable until compaction. The option setter
costs described above and string byte counts remain additional work.

The MLIR review adds explicit heap allocation/read/write effects to path
construction, precise managed collection points in both backends, and typed
element/storage and schema-owner verification. This follows
[MLIR side-effect guidance](https://mlir.llvm.org/docs/Rationale/SideEffectsAndSpeculation/)
and [ODS verification guidance](https://mlir.llvm.org/docs/DefiningDialects/Operations/).
Small MLIR checks run canonicalization/CSE, both lowerings, LLVM verification,
and malformed owner/type/scope diagnostics. Two short execution fixtures
exercise FIFO whole/partial writes, clipped and unknown selections, captured
strings, delayed/event/repeated-event assignments and Re-NBA. A seven-update
runtime fixture forces GC between enqueue and commit. Text, JSON, HTML and
LCOV preserve the existing option rows and agree across native and bytecode.
The bounded update-count probe in `benchmark/coverage` is outside unit/lit
tests; it varies writes and deadlines in one configuration.

A 19.3 constructor probe found that input strings were restricted to
construction-time expressions and `ref string` was rejected. Both now follow
the ordinary captured-formal contract: inputs retain immutable managed values,
and references retain typed storage bindings. The context's GC service roots
visit captured string words and managed reference owners; automatic references
keep their existing storage retain. Sampling reads the captured input or the
current reference value without copying string bytes. Defaults still evaluate
once at construction, and sample-formal scope restrictions in 19.8.1 remain.

Each instance stores formals in a bounded ID index. Building and enumerating
F formals cost O(F); an ordinary scalar/string formal lookup costs O(1), plus
work performed by its selected reference path. The bytecode constructor carries
typed reference widths in one additional scalar per formal. Its decoder visits
the batch once rather than scanning all schema formals for every descriptor.
String and real references use 64-bit storage in both backends. The codec's
existing formal direction/result fields represent these declarations, so no
wire columns or report schema changes are necessary.
The native formal-read ABI receives the generated state planes explicitly;
an unbound canonical shadow is not a valid substitute for a live native real
reference. Bytecode and runtime-only calls use their canonical context state.

The existing typed formal-read operation models storage/heap reads and the
allocation/write effects possible through a reference path, and both backends
treat it as a potential GC collection point. Its result and schema-owner
verification follow [MLIR ODS](https://mlir.llvm.org/docs/DefiningDialects/Operations/)
and [side-effect modeling](https://mlir.llvm.org/docs/Rationale/SideEffectsAndSpeculation/).
Small MLIR regressions cover input/ref string and real reads, construction
ordering, repeated reads around mutation, canonicalization/CSE, GC roots,
malformed types/owners, both backend ABIs and LLVM verification. One short
runtime unit forces GC after the caller drops its captured strings and
reference-path roots. A five-instance, two-configuration execution fixture
checks defaults, independent inputs, class/automatic references, live guards,
exact counters, and native/bytecode parity for text, JSON, HTML and LCOV.

Constructor probes of 19.5.1 and 19.5.3 also exposed repeated table scans and
middle-vector insertion for automatic bins and late default arrays. Resolution
now indexes sample expressions, bin plans, transition programs, value sets,
and `with` predicates once per configuration. Default-array association uses
owner-local set positions and computes the complement once per coverpoint,
including when several default arrays share that domain. Automatic eligibility
uses an indexed owner flag. Ignore and illegal bins still permit automatic
creation; ordinary bins suppress it, and enum templates retain their named
automatic bins.

Every generated group appends its rows. One final pass groups spans by item,
packs them in item and declaration order, recomputes point-local ordinals,
and sets `firstBin` from the running prefix count. Empty groups keep their
positions and consume no rows. Cross rows follow the packed point rows.
Stable bin IDs, expansion ordinals, names, signed uniform partitions, and
exclusion semantics stay consistent. Table joins and packing cost
O(I + B + E + S + G + K), where the terms count items, template bins,
expressions, value sets, groups, and expanded bins. Value generation additionally
costs its output limb and name bytes. Geometric union/complement proof work is
separate from this table-joining bound. Exclusion metadata is indexed once by
owner; copying derived flags back uses the aligned fingerprint span.

Automatic counts and default complement cardinalities now respect the
existing parser record ceiling before output allocation or enumeration, with
explicit `out of resources` status. A 31-bit empty default domain and an
oversized positive `auto_bin_max` exercise that rejection in both backends.
No ABI or wire columns change, and every report format consumes the same
resolved rows. Before/after report comparisons include configuration digests,
stable IDs, ordinals, flags, value sets, counters, and cross data.

The MLIR review keeps constructor-derived data in the shared coverage schema
and the existing typed effectful create/sample operations. It adds no mutable
pass-global cache or backend-specific coverage semantics, consistent with
[pass management](https://mlir.llvm.org/docs/PassManagement/) and
[side-effect modeling](https://mlir.llvm.org/docs/Rationale/SideEffectsAndSpeculation/).
Small MLIR regressions check ownership, declaration metadata, signed samples,
canonicalization/CSE, LLVM verification, and bytecode calls. A four-coverpoint
runtime unit checks exact group spans, empty groups, row ordinals, and reuse;
a one-configuration executable fixture checks native/bytecode parity and
text/JSON/HTML/LCOV generation. Large timings remain standalone bounded probes.

The coverage-related regression suite probes existing coverage semantics; it
does not certify complete implementation of every feature in the LRM.

See `benchmark/coverage/README.md` for reproducible scaling probes and measured
results. Timing ratios are evidence in addition to the traversal bounds; they
are not CI assertions.
