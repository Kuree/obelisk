// RUN: obelisk-opt %s --canonicalize --cse | FileCheck %s

// MLIR's ConstantLike contract requires every constant-like op to fold to an
// Attribute.  Null handles used to claim the trait without a folder, so
// canonicalization cached an invalid result and later crashed while folding
// the managed string select below.  Their UnitAttr fold marker is interpreted
// together with the result type, and CSE still merges equivalent nulls.

simulation.func @managed_select(
    %context: !simulation.context {simulation.capture_kind = 0 : i32},
    %choice: i1 {simulation.capture_kind = 1 : i32},
    %left: !simulation.string {simulation.capture_kind = 1 : i32},
    %right: !simulation.string {simulation.capture_kind = 1 : i32})
    -> !simulation.string attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
  %null = simulation.managed.null : !simulation.string
  %slot = simulation.ref.alloc %null : !simulation.string -> !simulation.ref<!simulation.string>
  simulation.ref.store %null to %slot : !simulation.string, !simulation.ref<!simulation.string>
  cf.cond_br %choice, ^return_null, ^merge(%left, %right : !simulation.string, !simulation.string)
^return_null:
  simulation.return %null : !simulation.string
^merge(%merged_left: !simulation.string, %merged_right: !simulation.string):
  %compared = simulation.string.compare %merged_left, %merged_right case_insensitive = false
  %zero = arith.constant 0 : i32
  %equal = arith.cmpi eq, %compared, %zero : i32
  %local_null = simulation.managed.null : !simulation.string
  %selected = arith.select %equal, %merged_left, %local_null : !simulation.string
  simulation.ref.store %selected to %slot : !simulation.string, !simulation.ref<!simulation.string>
  %result = simulation.ref.load %slot : !simulation.ref<!simulation.string> -> !simulation.string
  simulation.return %result : !simulation.string
}

// CHECK-LABEL: simulation.func @managed_select
// CHECK-COUNT-1: simulation.managed.null
// CHECK: arith.select {{.*}} : !simulation.string
