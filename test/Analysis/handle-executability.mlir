// RUN: talos-opt %s --test-talos-access-footprints='print-handles' -o /dev/null 2>&1 | FileCheck %s

// A missing lattice in a dead block is not an opaque reference in a live
// block. Keep both cases visible in the analysis test interface.
!row = !simulation.unpacked_array<0 : 7 x i3>
!matrix = !simulation.unpacked_array<0 : 7 x !row>
module {
  simulation.design @handles {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !matrix design
    // CHECK-LABEL: constant_guard
    // CHECK: block 0 executable=1
    // CHECK: descriptor=0 low=0 width=192 dynamic=0
    // CHECK: block 1 executable=0
    // CHECK: no fact
    // CHECK: block 2 executable=1
    // CHECK: descriptor=0 low=168 width=24 dynamic=0
    simulation.code_unit.decl 1 in 0 function hierarchy "constant_guard"
    simulation.func @constant_guard(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %root: !simulation.ref<!matrix> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %zero = arith.constant 0 : i32
      %test = arith.cmpi ne, %zero, %zero : i32
      cf.cond_br %test, ^dead, ^live
    ^dead:
      %d = simulation.ref.subelement %root[[7]] : !simulation.ref<!matrix> -> !simulation.ref<!row>
      cf.br ^live
    ^live:
      %l = simulation.ref.subelement %root[[7]] : !simulation.ref<!matrix> -> !simulation.ref<!row>
      simulation.return
    }

    // CHECK-LABEL: unknown_guard
    // CHECK: block 1 executable=1
    // CHECK: descriptor=0 low=168 width=24 dynamic=0
    // CHECK: block 2 executable=1
    simulation.code_unit.decl 2 in 0 function hierarchy "unknown_guard"
    simulation.func @unknown_guard(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %root: !simulation.ref<!matrix> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %test: i1 {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      cf.cond_br %test, ^conditional, ^done
    ^conditional:
      %l = simulation.ref.subelement %root[[7]] : !simulation.ref<!matrix> -> !simulation.ref<!row>
      cf.br ^done
    ^done:
      simulation.return
    }

    // A mixed static/dynamic selection retains its root even without a bound
    // on the dynamic index; invalid indices keep their runtime semantics.
    // CHECK-LABEL: mixed_selection
    // CHECK: block 0 executable=1
    // CHECK: descriptor=0 low=168 width=24 dynamic=0
    // CHECK: descriptor=0 low=168 width=24 dynamic=1
    simulation.code_unit.decl 3 in 0 function hierarchy "mixed_selection"
    simulation.func @mixed_selection(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %root: !simulation.ref<!matrix> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %index: i65 {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %row = simulation.ref.subelement %root[[7]] : !simulation.ref<!matrix> -> !simulation.ref<!row>
      %element = simulation.ref.array_element %row[%index] : (!simulation.ref<!row>, i65) -> !simulation.ref<i3>
      simulation.return
    }
  }
}
