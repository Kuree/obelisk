// RUN: obelisk-opt %s --canonicalize --cse | FileCheck %s

// MLIR's ConstantLike contract requires every constant-like op to fold to an
// Attribute.  Null handles used to claim the trait without a folder, so
// canonicalization cached an invalid result and later crashed while folding
// the managed string select below.  Their UnitAttr fold marker is interpreted
// together with the result type, and CSE still merges equivalent nulls.

obelisk_sim.func @managed_select(
    %context: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
    %choice: i1 {obelisk_sim.capture_kind = 1 : i32},
    %left: !obelisk_sim.string {obelisk_sim.capture_kind = 1 : i32},
    %right: !obelisk_sim.string {obelisk_sim.capture_kind = 1 : i32})
    -> !obelisk_sim.string attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
  %null = obelisk_sim.managed.null : !obelisk_sim.string
  %slot = obelisk_sim.ref.alloc %null : !obelisk_sim.string -> !obelisk_sim.ref<!obelisk_sim.string>
  obelisk_sim.ref.store %null to %slot : !obelisk_sim.string, !obelisk_sim.ref<!obelisk_sim.string>
  cf.cond_br %choice, ^return_null, ^merge(%left, %right : !obelisk_sim.string, !obelisk_sim.string)
^return_null:
  obelisk_sim.return %null : !obelisk_sim.string
^merge(%merged_left: !obelisk_sim.string, %merged_right: !obelisk_sim.string):
  %compared = obelisk_sim.string.compare %merged_left, %merged_right case_insensitive = false
  %zero = arith.constant 0 : i32
  %equal = arith.cmpi eq, %compared, %zero : i32
  %local_null = obelisk_sim.managed.null : !obelisk_sim.string
  %selected = arith.select %equal, %merged_left, %local_null : !obelisk_sim.string
  obelisk_sim.ref.store %selected to %slot : !obelisk_sim.string, !obelisk_sim.ref<!obelisk_sim.string>
  %result = obelisk_sim.ref.load %slot : !obelisk_sim.ref<!obelisk_sim.string> -> !obelisk_sim.string
  obelisk_sim.return %result : !obelisk_sim.string
}

// CHECK-LABEL: obelisk_sim.func @managed_select
// CHECK-COUNT-1: obelisk_sim.managed.null
// CHECK: arith.select {{.*}} : !obelisk_sim.string
