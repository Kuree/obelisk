# Coverage codec v2 and scaling

The compiler, native runtime, bytecode runtime, and `obelisk-cov` share the
portable codec in `lib/Coverage/CoverageDatabase.cpp`. The declarative source
of record sizes, domains, and wire constants is
`include/obelisk/Coverage/CoverageDatabase.td`; `obelisk-tblgen` generates the
declarations, parser descriptors, and serializer descriptors. Embedded static
schemas and persisted runs use the same codec. V1 files are rejected.

## Wire layout

Integers are little endian. The header is 72 bytes:

| Offset | Width | Field |
| --- | --- | --- |
| 0 | 8 | `OBCOV\r\n\x1a` magic |
| 8 | 4 | Codec version, 2 |
| 12 | 4 | Flags, currently zero |
| 16 | 8 | Total file size, including checksum |
| 24 | 8 | Directory offset, 72 |
| 32 | 4 | Directory record count |
| 36 | 4 | Header size, 72 |
| 40 | 32 | Static schema fingerprint |

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

The JSON report schema retains its independent
`obelisk-coverage-report-v1` identifier. Its `codec_version` is 2; it is also the
payload embedded in the HTML report. Text, JSON, HTML, LCOV, inspect, and merge
all read through the shared v2 parser. The Python schema dump helper verifies
the same trailer and payload bounds.

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
128 different configurations, samples every instance, checks the type's bin
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

Indexes are local to one invocation and borrow immutable input. There are no
global mutable compiler caches. Wire declarations stay in TableGen, and runtime
code stays independent of MLIR and LLVM. This follows the analysis-lifetime and
declarative-definition guidance in [MLIR pass management](https://mlir.llvm.org/docs/PassManagement/)
and [MLIR operation definitions](https://mlir.llvm.org/docs/DefiningDialects/Operations/).

The coverage-related regression suite probes existing coverage semantics; it
does not certify complete implementation of every feature in the LRM.

See `benchmark/coverage/README.md` for reproducible scaling probes and measured
results. Timing ratios are evidence in addition to the traversal bounds; they
are not CI assertions.
