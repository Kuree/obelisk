# VPI queries, startup, and backdoor access

VPI is selected explicitly with `--vpi=off`, `--vpi=read`, or `--vpi=full`.
The mode controls design observability and mutation; shared libraries are
ordinary positional inputs:

```sh
obelisk --vpi=read design.sv plugin.so -o simulator
obelisk --vpi=full design.sv uvm_backdoor.so helpers.a -o simulator
```

Obelisk probes each shared object during compilation for the standard
`vlog_startup_routines` symbol. A module exporting that table is rejected under
`--vpi=off`. Probing loads the DSO and can run its ELF constructors, so native
inputs must be trusted.

At runtime every detected module is already present through `DT_NEEDED`.
Obelisk obtains a handle with `RTLD_NOLOAD`, resolves the startup table on that
specific handle, validates its ELF symbol extent and null terminator, and calls
the tables in positional-input order. Repeated filesystem identities are
deduplicated. In accordance with IEEE 1800-2017 36.10.2, startup-table routines
may only register callbacks and system tasks/functions; hierarchy and value
access begins when `cbEndOfCompile` callbacks run. Startup runs after runtime,
DPI, class, and native-state registration and before the root process is
spawned. The VPI context remains active through scheduler execution, including
DPI calls, and is deactivated before context destruction.

The current backdoor subset provides hierarchical and scoped
`vpi_handle_by_name`, `$root.` normalization, scope relations, filtered
iteration/scanning, `vpiType`, `vpiSize`, `vpiName`, `vpiFullName`, and Scalar,
Int, Vector, and BinStr values. `vpiNoDelay`, `vpiForceFlag`, and
`vpiReleaseFlag` require `--vpi=full`; driver handles are read-only. Vector
limbs use the standard 32-bit encoding with `bval = unknown` and
`aval = value XOR unknown`.

Module and interface parameter inventories preserve declaration order and exact
instance identity while sharing declaration metadata and identical effective
specializations. Packed value parameters and localparams expose their elaborated
values, signedness, types, and explicitly declared ranges; type parameters retain
typedef identity. Shared declarations do not add per-instance parameter records
or unused IO endpoint slots. Original defaults and override-assignment relations
for these shared instance parameters remain incomplete: a known original default
reports a notice until its expression is retained. Nonpacked value parameters remain outside this inventory slice. General expression
use edges for these shared parameters await contextual expression recipes.

Elaborated generate scopes preserve packed parameters and localparams, including
implicit genvar localparams, under their exact scope. Distinct declarations use
compact records while their types and immutable values share existing pools.
Nested generate scopes retain Scope, Instance and Module ancestry; inactive
branches do not contribute parameter inventory. Supported original literal and
parameter-reference defaults retain their source identities.

Class queries distinguish a lexical `vpiClassDefn`, a resolved specialization's
`vpiClassTypespec`, and a live `vpiClassObj` (IEEE 1800-2023 37.31–37.33).
`vpiClassTypespec` on a variable reports its declared type; on a live object it
reports the allocated type. Null class variables have `vpiObjId` zero and a
null `vpiClassObj` relation. Aliases of one live object compare equal and share
an identity. Object and automatic-member handles are weak references: they
do not keep an SV object alive, and collection invalidates them.

The class inventory includes data members, methods with explicit declarations,
effective value and type parameters, and normal constraints. Specializations
and live objects include inherited members and methods. Constraint iteration
returns only the class's own declarations in source order; inherited
constraints are reached through `vpiExtends`. A static member has one backing
storage location without an additional declaration in the physical module's
variable inventory. Lexical class handles cannot read member values, and
non-static members reached from a typespec need a live receiver for value
access. Generic lexical value and type parameters are preserved in declaration
order even without a realized specialization. Their `vpiExpr` relations retain
original defaults independently of overridden values: primitive type defaults,
references to preceding parameters, integer literals, and supported unary,
binary, and conditional expressions. Conditional defaults retain all three
operands without choosing a branch. Parameter references preserve declaration
identity, and
`vpiDecompile` preserves expression precedence and escaped identifiers. An absent
default returns a quiet null; an unsupported default form returns a notice.
Unresolved lexical parameters have no elaborated value or type, and those
queries report a notice. Generic lexical methods are retained without a
realized specialization, including constructors, tasks, static methods, extern
declarations, and pure virtual declarations. Their exact names, declaration
order, scope, lifetime, and visibility remain queryable without resolving
dependent signatures or bodies. Explicit or otherwise established virtuality is
queryable; unresolved inherited virtuality reports a notice. Ordinary class
`extern` syntax does not imply DPI access metadata.
Generic and nested lexical classes retain normal constraint declarations in
source order, including a prototype at its declaration position. Static lifetime
and the presence of an actual out-of-block definition remain queryable without
binding dependent constraint bodies.

Generic classes also retain lexical fields with primitive concrete declared
types and concrete typedef chains without creating an executable class. Exact
names, scopes, declared types, lifetime, constness, random mode, and visibility
remain queryable; value access reports a notice. A dependent field such as
`T value` has no public variable kind until specialization and is therefore
not fabricated as a `vpiReg`. Its source declaration is retained for subsequent
specialization. Dependent typedef aliases retain distinct named typespec
identities with exact `vpiTypedefAlias` chains to their unresolved type
parameters. They appear only in the typedef inventory; declaration-only
effective type, default, and localparam queries report a notice. Nested lexical
class declarations retain their own `vpiClassDefn` and `vpiInternalScope`
inventory, including concrete source members and nested generic declarations
that have no realized specialization. Realized nested typespecs refer back to
that same lexical definition across enclosing class specializations. Dependent
base/type syntax and broader original-default expression forms remain incomplete.

Mailbox, semaphore, and process variables also expose `vpiClassObj` and
`vpiClassTypespec`. Upcasting a user class to its mailbox or semaphore base
preserves the outer object's identity and allocated class type. Built-in
typespecs have no `vpiClassDefn`. Process objects retain their identity after
execution finishes or is killed. Mailbox typespecs and live objects expose
their effective type parameter `T` through `vpiParameter`, preserving its
typedef spelling independently of the canonical mailbox type identity.

`vpiWaitingProcesses` on a named event, mailbox, or semaphore returns its
suspended threads, including when the resource is a built-in base of a user
class. Named-event queries follow the current event identity of aliases and
class members, including fixed event-array elements. Fixed arrays preserve
their declared typedefs and select event identities using the actual storage
layout. Event-order waits
include only their remaining events; a satisfied wait is excluded even before
the scheduler dispatches its thread. Thread handles
expose `vpiParent`, child `vpiThread` iteration, and `vpiActive`, which is true
only during that thread's execution. Thread handles expire on finish or kill;
they have `vpiAutomaticScheme` lifetime. Exact code-unit-to-statement bindings
provide `vpiOrigin`, including outlined fork branches and nested native or
bytecode calls. The origin statement keeps its lexical owner and survives
thread termination. Procedural lowering emits exact origins for initial/always
bodies and supported fork branches, with lexical statement nesting retained
through optimization. The inventory includes case-item groups, ordered loop
controls, immediate assertion actions, procedural assignments, and repeated
event controls. Call statements retain their exact function, task, system, or method-call
kind and enclosing scope. Call inventories also retain nested expression calls,
exact lexical owners, direct callee relations, and shared identity for calls
that are themselves statements. Indirect dispatch remains explicitly marked.
Unsupported source statement forms remain unbound.

Virtual-interface call lowering considers every elaborated instance of the exact
nominal interface type, including instances under other top modules, with
modport import/export filtering. Typed source contracts retain the original
callable ABI and declaration-owned extern providers through optimization.
The reflection image retains nominal membership, signatures, and extern-task
aggregation facts so runtime frame queries can validate the actual callee.
Internal DPI code units map to their exact public source declarations, including
imported tasks executed through a function ABI; they do not add public scopes.

The runtime frame API tracks distinct activations and exposes their parent,
origin, thread, current statement, active state, and automatic lifetime.
Suspension preserves a frame; return, disable, and kill invalidate the exited
activations without invalidating their immutable source objects. Bytecode,
native, and AOT scheduler paths restore the saved caller frame depth. A checked
prepare/consume/finish protocol carries the exact source call through nested
synchronous and suspending calls, including native and AOT task handoffs.
Resolution-function activations use the exact net or net-array origin and verify
its retained nettype resolution function. Calls in static initializers retain their source variable and lexical scope;
the runtime permits their exact call-origin frames without an initializer
helper frame. Existing procedural parents remain intact when present.
Imported DPI tasks retain their public
task identity while entering through their physical synchronous function ABI.
Synchronous DPI exports use their exact public callable scope as the activation
origin, preserve the importing frame as parent, and restore its frame depth on
foreign-call failure. Exported tasks retain the importing source thread while
their internal executor waits, calls other tasks, or is killed. Forked children
retain that source-thread parent after the export returns.
Frame-local declaration inventories can be enumerated before storage is bound.
Bound local values and selections use weak automatic references and expire with
their activation; structural typespec and lexical-scope handles remain valid.
Compiler frame instrumentation and source-level local bindings are still being
completed; ordinary source execution does not yet populate these frame queries.

`vpiMessages` snapshots mailbox contents in FIFO order without consuming them.
Integral and packed values retain every bit and unknown bit; real and string
values are copied. Scanned values are read-only; copied values survive removal
from the mailbox and collection of the mailbox. Class references are weak and
expire when their referents are collected; copied aggregates retain their
member and operand positions and type metadata, while access to a collected
referent reports a notice. The iterator's `vpiUse` holds the mailbox weakly. Unpacked arrays, structs, dynamic arrays, and queues expose
nested positional assignment-pattern expressions through `vpiOperand`.
Scanned operands keep their copied values after parent handles are released.
Union snapshots expose read-only `vpiUnionVar` views and typed `vpiMember`
children. Tagged unions retain the active tag; inactive members remain
queryable but their values are unavailable. Void tags appear only in the
typespec's member inventory. Packed union values retain their value and unknown
bits; unpacked union values are accessed through their members. Nested variable
views preserve member names, array indices, packed-bit selections, and parent
relations after the mailbox is collected. Associative arrays expose their
stored values in index order and copied key expressions through `vpiIndex`,
including wide integral, string, composite, and wildcard keys. Default values
are excluded from the stored-entry inventory. Nested unpacked-array iteration
flattens to leaf variables while indexed access retains intermediate arrays.
Untyped mailbox messages retain an exact source type descriptor for each boxed
value, preserving signedness, typedefs, and heterogeneous aggregate contents
through ordinary value copies. A legacy box without a source descriptor reports
a notice when queried.

Virtual-interface variables expose their current `vpiActual` interface or
exact modport; a null variable returns a quiet null handle. `vpiIsModPort`
describes the declared type even for a null variable. Interface modport
iteration includes unused declarations, and a modport's reverse relation is
`vpiInterface`. Virtual-interface iteration flattens fixed arrays, dynamic
arrays, queues, and their nesting into element handles, while `vpiVariables`
returns one array declaration. Element handles reread current storage after
retargeting or resizing and retain their parent, index, and declared typespec
relations without keeping the containers alive. The outer dynamic-array or
queue range reflects its current size; nested managed dimensions retain the
standard empty ranges. Associative selections retain copied keys and resolve
the current entry after deletion, reinsertion, or replacement of the array.
Copied index values survive independently of the selected entry; class-reference
indices remain weak. Default entries are excluded from iteration. Fixed class
event arrays likewise select the event references stored in the object.

Port handles additionally expose `vpiDirection`, `vpiPortIndex`,
`vpiPortType`, `vpiScalar`, and `vpiVector` directly from their immutable
design record. Net and variable handles expose the LRM-defined `vpiScalar` and
`vpiVector` properties from their immutable serialized type graph: unpacked
arrays recursively use their element shape, packed arrays/structs/unions are
vectors even at width one, and unpacked aggregates and real values are neither.
These queries do not inspect or materialize simulation state, register a
callback, or affect scheduler tier selection.

Handles live in a context-owned registry. `vpi_release_handle` returns their
storage immediately, and scanning an iterator through exhaustion automatically
frees the iterator as required by 38.38. Context teardown frees any handles and
callbacks the application did not explicitly release.

Error status follows IEEE 1800-2017 38.2: `vpi_chk_error` observes without
consuming the previous status, and the next call to any other VPI routine
resets it before reporting that call's result. `vpi_get64` accepts only
64-bit integer properties; ordinary integer and Boolean properties must be
queried with `vpi_get`.

Traversal uses the design database encoded into the final simulator. Scope,
module, net, and packed-storage handles wrap validated database cursors; their
stamped state offsets address the descriptor's coherent four-state value at a
scheduler safe point. Bytecode execution, language force/assign, net
resolution, and native region entry/exit use that same materialization layout.
A clean generated region may retain the value in SSA between safe points; it
must materialize before VPI can run. No separate VPI hierarchy or persistent
VPI value copy is constructed at runtime.

The reflection image also has compact, pointer-free statement and semantic
callback-site tables. A statement identity is separate from its source
location and from each site: this preserves distinct elaborated instances and
the two Table 38-6 callback points of a `for` statement. The exact cbStmt
eligibility and placement policy are generated from the VPI TableGen model.
Static inventory includes concrete statement kinds that are not cbStmt-capable;
those kinds have no callback-site records. A statement's hierarchy-scope index
identifies its exact elaborated scope. Behavioral statements also name their
owning process/function object; scope-owned continuous assignments and alias
statements use the absent-owner sentinel. Lexical statement nesting uses the
parent-statement index. Per-design VPI relation edges (such as `vpiStmt`,
`vpiElseStmt`, and ordered case/for children) are stored in a separate table
rather than being overloaded onto either field. Structural statement scope
relations are emitted automatically from the nearest scope-bearing parent
statement, a scope-capable code-unit owner, or the physical hierarchy scope, in
that order. Direct whole-source ports also carry an automatically emitted
`vpiLowConn` relation to their canonical net or variable object, including
explicitly renamed formals. Selected ports remain distinct until
select/ref-object identities are serialized and are never widened to the whole
backing object.
These immutable tables are emitted only for a VPI profile, not for VCD-only
reflection, and do not install executable probes or change the scheduler.
Production lowering emits supported procedural statement inventory and thread
origins. Executable statement-site bindings and cbStmt dispatch are not yet
implemented.

An immediate deposit with an exact descriptor/root mapping also reuses
the generated static fanout index. After updating both four-state planes, the
runtime computes change and edge masks and marks the fanout entries' compute
nodes directly, with the packed ready bits suppressing duplicate wakes. This
skips bytecode stabilization when no observer, conditional wait, force, or
dirty specialization state is active. Deposits without an exact mapping and
force/release operations retain the guarded bytecode handoff.

A combinational DAG can propagate a deposit through its affected downstream
cone once in dependency order, provided no further input change occurs during
that propagation. A write to an internal signal starts at that signal's
consumers; it must not unconditionally replay upstream assignments and erase
the deposited value. Combining process bodies therefore has to preserve these
external activation points as well as the topological order. The experimental
statement-fusion pass currently retains the original processes in full VPI
mode until it can preserve those entry points. The existing scheduler continues
to handle deposits and force/release. These requirements follow IEEE 1800-2023
4.10 and 38.34; acyclicity alone is not an activation or override proof.

The cold lifecycle callbacks `cbEndOfCompile`, `cbStartOfSimulation`, and
`cbEndOfSimulation` are implemented, including callback-object iteration,
`vpi_get_cb_info`, release, and removal. Lifecycle registration does not
activate scheduler observation or alter native specialization. Callback input
is copied at registration, duplicate registrations remain distinct, and each
invocation receives a fresh action-callback record. `cbAfterDelay` runs at
the start of the time slot its delay selects. The synchronization callbacks
run once in that slot's region (IEEE 1800-2023 4.10 Table 4-1): `cbNBASynch`
and `cbReadWriteSynch` after Active and Inactive and before any NBA update,
and `cbReadOnlySynch` in Postponed. A synchronization callback registered for
the current slot by another one runs on the next pass over its region. Other
callback reasons, delayed writes, system task/function registration, VPI
strength and trireg access, and VPI-registered waveform dumping are not
implemented.
Static `vpiStrength0` and `vpiStrength1` queries cover explicit continuous
assignments and built-in gates. Ordinary defaults reuse the shared primitive
subtype or continuous-assignment kind; only nondefault strengths need image
rows. Pull sources ignore the opposite polarity under 28.10; Obelisk reports
canonical Pull for that unused polarity, matching its executable driver
representation. Switch strengths depend on their inputs and remain unavailable
through these static queries.

SystemVerilog trireg execution itself, including charge strength, retention,
decay, and connected charge sharing, is implemented independently of VPI.
Unsupported registration calls made from a startup table produce a clear
unsupported-startup failure rather than an unresolved-symbol loader crash.
Waveform dumping itself is available through the `$dump` system tasks and is
described in [Waveform dumping](waveforms.md); it reads the same design
database as the backdoor and needs no plugin.

The complete canonical `vpi_user.h`, `sv_vpi_user.h`,
`vpi_compatibility.h`, and Obelisk `svdpi.h` are staged under:

```sh
$(obelisk --print-resource-dir)/include
```

Lexical class inheritance is retained for unused generic classes as distinct
source class types, with exact `vpiClassDefn`, `vpiExtends`, and transitive
`vpiDerivedClasses` identities. Omitted class actuals retain ordered
`vpiParamAssign` objects whose `vpiRhs` shares the corresponding declaration's
original default; missing defaults remain absent and unsupported defaults
report a notice. These lexical types never appear in the specialization
inventory. Named and ordered class actuals retain exact formal identities and
`vpiConnByName`. Supported explicit RHS forms include integral literals, pure
operators, conditional expressions, direct derived-class parameter references,
and independent primitive types. Named empty actuals and omitted formals share
the original declared default. Unsupported RHS forms retain their own notice
without hiding the other assignments. Constructor arguments retain unavailable
source evidence until their expression forms are supported.
Dependent or imported base names without exact source proof keep an Extends
occurrence and report a notice for the unresolved base type. Lexical class types
do not expose specialization member inventories. Their parameter iteration
reuses the exact defining class declarations, including body localparams,
preserving declaration scope, original defaults, and ParamAssign Lhs identity.
