# Procedural force and assign

Obelisk supports constant-foldable right-hand sides for these initial language
forms:

- whole statically allocated packed variables with `force`/`release`
- whole statically allocated packed variables with procedural
  `assign`/`deassign`
- whole built-in nets and constant built-in net bit/part selects with
  `force`/`release`

Automatic variables, class properties, unpacked or managed values,
concatenations, dynamic selects, and user-defined net types are rejected.
Signal-dependent right-hand sides are diagnosed instead of being approximated
with a statement-time snapshot.

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
