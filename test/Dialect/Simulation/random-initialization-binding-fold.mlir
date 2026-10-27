// RUN: talos-opt %s --canonicalize | FileCheck %s

// IEEE 1800-2023 18.14.1: an instance's initialization RNG is consumed only by
// static process creation and static initializers. A binding that encloses
// nothing with a memory effect folds away; one around a draw or a spawn stays.

// CHECK-LABEL: func.func @bindings
// CHECK-SAME: %[[CTX:[^:]*]]: !simulation.context
func.func @bindings(%ctx: !simulation.context) -> i64 {
  // CHECK-NOT: scope = 1
  %empty = simulation.random.initialization_enter %ctx {scope = 1 : i64} : (!simulation.context) -> i64
  %pure = simulation.context.storage %ctx[0] : !simulation.ref<i64>
  simulation.random.initialization_leave %ctx, %empty : (!simulation.context, i64) -> ()
  // CHECK: %[[DRAW:.*]] = simulation.random.initialization_enter %[[CTX]] {scope = 2 : i64}
  // CHECK-NEXT: %[[VALUE:.*]] = simulation.random.next %[[CTX]]
  // CHECK-NEXT: simulation.random.initialization_leave %[[CTX]], %[[DRAW]]
  %draw = simulation.random.initialization_enter %ctx {scope = 2 : i64} : (!simulation.context) -> i64
  %value = simulation.random.next %ctx : (!simulation.context) -> i64
  simulation.random.initialization_leave %ctx, %draw : (!simulation.context, i64) -> ()
  // CHECK-NOT: scope = 3
  %nested = simulation.random.initialization_enter %ctx {scope = 3 : i64} : (!simulation.context) -> i64
  %inner = simulation.random.initialization_enter %ctx {scope = 4 : i64} : (!simulation.context) -> i64
  simulation.random.initialization_leave %ctx, %inner : (!simulation.context, i64) -> ()
  simulation.random.initialization_leave %ctx, %nested : (!simulation.context, i64) -> ()
  // CHECK: return %[[VALUE]]
  return %value : i64
}
