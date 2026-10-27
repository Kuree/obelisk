// RUN: not talos-opt %s --convert-talos-sim-processes-to-llvm-coroutines 2>&1 | FileCheck %s
// A mixed-owner PHI must keep its existing ownership diagnostic; choosing a
// single allocation's stack address would read the wrong object.
// CHECK: automatic reference block argument merges distinct ownership origins
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "aarch64-unknown-linux-gnu"} {
  simulation.design @mixed {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "mixed"
    // A PHI selecting different allocations cannot use either stack base.
    simulation.func @mixed_references(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %input: i128 {simulation.capture_kind = 2 : i32},
      %choose: i1 {simulation.capture_kind = 2 : i32}) -> i128
      attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %a = simulation.ref.alloc %input : i128 -> !simulation.ref<i128>
      %b = simulation.ref.alloc %input : i128 -> !simulation.ref<i128>
      cf.cond_br %choose, ^done(%a : !simulation.ref<i128>), ^done(%b : !simulation.ref<i128>)
    ^done(%selected: !simulation.ref<i128>):
      %result = simulation.ref.load %selected : !simulation.ref<i128> -> i128
      simulation.return %result : i128
    }
  }
}
