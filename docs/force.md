# Procedural force and assign

Obelisk supports continuously reevaluated right-hand sides for these language
forms:

- whole statically allocated variables with `force`/`release`
- whole statically allocated variables with procedural `assign`/`deassign`
- whole built-in nets and constant built-in net bit/part selects with
  `force`/`release`
- legal concatenations of those targets

Fixed unpacked aggregates, whole dynamic containers, strings, class handles,
and the implemented class-property compatibility surface use the same override
machinery. Signal-dependent right-hand sides are observed by exact dependency;
the runtime does not poll them.

IEEE 1800-2017 10.6.1 explicitly prohibits bit- and part-selects of variables
for procedural `assign`/`deassign`, and 10.6.2 likewise prohibits them for
`force`/`release`. Automatic variables, nonconstant net selects, and
user-defined nettypes are also rejected. A few legacy ivtest cases expect a
variable-select extension; their diagnostics are conformance evidence, not
missing-feature evidence.

Force has priority over procedural assign. Assign always updates its shadow
value, including while force owns the published bits; releasing the force then
restores that shadow. Ordinary stores, NBA commits, deposits, and resolved-net
publication cannot replace assigned or forced bits. Deassign retains the last
published assigned value. Variable release retains the forced value when no
assign is active, while net release recomputes the affected connected component
from current drivers and produces Z when it is undriven.

Native and bytecode execution use the same override masks and publication
ordering at scheduler safe points. A clean native region may keep an unforced
value in SSA only while no force/release operation can run; force and release
are materialization and specialization-invalidation boundaries. The compiler
emits the encoded execution/design image whenever these operations occur, even
when VPI is disabled, so stamped native state offsets are checked against the
runtime layout.
