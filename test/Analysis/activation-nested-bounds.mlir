// RUN: talos-opt %s --test-talos-activation-regions -o %t.analyzed 2>&1 | FileCheck %s
// RUN: talos-opt %s -o %t.original
// RUN: diff %t.original %t.analyzed
// RUN: talos-opt %s --test-talos-activation-memory -o /dev/null 2>&1 | FileCheck %s --check-prefix=MEM
// RUN: talos-opt %s '--test-talos-activation-regions=compare-input-dependencies=true' -o /dev/null 2>&1 | FileCheck %s --check-prefix=COMPARE

// The input-only dependency proof agrees with full publication analysis.
// COMPARE: inputs @signed agree
// COMPARE: inputs @unsigned agree
// COMPARE: inputs @reverse_operand agree
// COMPARE: inputs @descending agree
// COMPARE: inputs @zero_trip agree
// COMPARE: inputs @shifted agree

// IEEE 1800-2023 12.7.1 and 11.6: the inner limit is an invariant outer
// induction, and the final step must fit the actual integer width. No source
// loop is replicated. Per-context bounds keep the triangular memory footprint.
!array = !simulation.unpacked_array<0 : 7 x i8>
module {
  simulation.design @nested_bounds {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !array design
    simulation.storage.decl 1 in 0 : !array design
    simulation.storage.decl 2 in 0 : i8 design
    // CHECK-LABEL: regions @signed valid=1
    // CHECK-NEXT: node cf.br read:s0[0,24) write:s1[0,24)
    // MEM-LABEL: activation @signed valid=1 complete=1
    // MEM-NEXT: live-in s0[0,24)
    // MEM-NEXT: must-write s1[0,24)
    // MEM-NEXT: may-write s1[0,24)
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "signed"
    simulation.func @signed(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %src = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %dst = simulation.context.storage %ctx[1] : !simulation.ref<!array>
      %bound = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %start = arith.constant 0 : i8
      %limit = arith.constant 4 : i8
      %step = arith.constant 1 : i8
      %kstart = arith.constant 0 : i8
      %kstep = arith.constant 1 : i8
      cf.br ^outer(%start : i8)
    ^outer(%i: i8):
      %more = arith.cmpi slt, %i, %limit : i8
      cf.cond_br %more, ^inner(%kstart : i8), ^wait
    ^inner(%k: i8):
      %again = arith.cmpi slt, %k, %i : i8
      cf.cond_br %again, ^body, ^advance
    ^body:
      %s = simulation.ref.array_element %src[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %d = simulation.ref.array_element %dst[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %v = simulation.ref.load %s : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %d : i8, !simulation.ref<i8>
      %knext = arith.addi %k, %kstep : i8
      cf.br ^inner(%knext : i8)
    ^advance:
      %next = arith.addi %i, %step : i8
      cf.br ^outer(%next : i8)
    ^wait:
      simulation.suspend.change %src to ^resume : !simulation.ref<!array>
    ^resume:
      cf.br ^outer(%start : i8)
    }
    // CHECK-LABEL: regions @unsigned valid=1
    // CHECK-NEXT: node cf.br read:s0[0,24) write:s1[0,24)
    // MEM-LABEL: activation @unsigned valid=1 complete=1
    // MEM-NEXT: live-in s0[0,24)
    // MEM-NEXT: must-write s1[0,24)
    // MEM-NEXT: may-write s1[0,24)
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "unsigned"
    simulation.func @unsigned(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %src = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %dst = simulation.context.storage %ctx[1] : !simulation.ref<!array>
      %bound = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %start = arith.constant 0 : i8
      %limit = arith.constant 4 : i8
      %step = arith.constant 1 : i8
      %kstart = arith.constant 0 : i8
      %kstep = arith.constant 1 : i8
      cf.br ^outer(%start : i8)
    ^outer(%i: i8):
      %more = arith.cmpi ult, %i, %limit : i8
      cf.cond_br %more, ^inner(%kstart : i8), ^wait
    ^inner(%k: i8):
      %again = arith.cmpi ult, %k, %i : i8
      cf.cond_br %again, ^body, ^advance
    ^body:
      %s = simulation.ref.array_element %src[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %d = simulation.ref.array_element %dst[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %v = simulation.ref.load %s : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %d : i8, !simulation.ref<i8>
      %knext = arith.addi %k, %kstep : i8
      cf.br ^inner(%knext : i8)
    ^advance:
      %next = arith.addi %i, %step : i8
      cf.br ^outer(%next : i8)
    ^wait:
      simulation.suspend.change %src to ^resume : !simulation.ref<!array>
    ^resume:
      cf.br ^outer(%start : i8)
    }
    // CHECK-LABEL: regions @reverse_operand valid=1
    // CHECK-NEXT: node cf.br read:s0[0,24) write:s1[0,24)
    // MEM-LABEL: activation @reverse_operand valid=1 complete=1
    // MEM-NEXT: live-in s0[0,24)
    // MEM-NEXT: must-write s1[0,24)
    // MEM-NEXT: may-write s1[0,24)
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "reverse_operand"
    simulation.func @reverse_operand(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      %src = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %dst = simulation.context.storage %ctx[1] : !simulation.ref<!array>
      %bound = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %start = arith.constant 0 : i8
      %limit = arith.constant 4 : i8
      %step = arith.constant 1 : i8
      %kstart = arith.constant 0 : i8
      %kstep = arith.constant 1 : i8
      cf.br ^outer(%start : i8)
    ^outer(%i: i8):
      %more = arith.cmpi slt, %i, %limit : i8
      cf.cond_br %more, ^inner(%kstart : i8), ^wait
    ^inner(%k: i8):
      %again = arith.cmpi sgt, %i, %k : i8
      cf.cond_br %again, ^body, ^advance
    ^body:
      %s = simulation.ref.array_element %src[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %d = simulation.ref.array_element %dst[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %v = simulation.ref.load %s : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %d : i8, !simulation.ref<i8>
      %knext = arith.addi %k, %kstep : i8
      cf.br ^inner(%knext : i8)
    ^advance:
      %next = arith.addi %i, %step : i8
      cf.br ^outer(%next : i8)
    ^wait:
      simulation.suspend.change %src to ^resume : !simulation.ref<!array>
    ^resume:
      cf.br ^outer(%start : i8)
    }
    // CHECK-LABEL: regions @descending valid=1
    // CHECK-NEXT: node cf.br read:s0[16,40) write:s1[16,40)
    // MEM-LABEL: activation @descending valid=1 complete=1
    // MEM-NEXT: live-in s0[16,40)
    // MEM-NEXT: must-write s1[16,40)
    // MEM-NEXT: may-write s1[16,40)
    simulation.code_unit.decl 4 in 0 always_comb hierarchy "descending"
    simulation.func @descending(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 4 : i64} {
      %src = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %dst = simulation.context.storage %ctx[1] : !simulation.ref<!array>
      %bound = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %start = arith.constant 4 : i8
      %limit = arith.constant 0 : i8
      %step = arith.constant -1 : i8
      %kstart = arith.constant 4 : i8
      %kstep = arith.constant -1 : i8
      cf.br ^outer(%start : i8)
    ^outer(%i: i8):
      %more = arith.cmpi sgt, %i, %limit : i8
      cf.cond_br %more, ^inner(%kstart : i8), ^wait
    ^inner(%k: i8):
      %again = arith.cmpi sgt, %k, %i : i8
      cf.cond_br %again, ^body, ^advance
    ^body:
      %s = simulation.ref.array_element %src[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %d = simulation.ref.array_element %dst[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %v = simulation.ref.load %s : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %d : i8, !simulation.ref<i8>
      %knext = arith.addi %k, %kstep : i8
      cf.br ^inner(%knext : i8)
    ^advance:
      %next = arith.addi %i, %step : i8
      cf.br ^outer(%next : i8)
    ^wait:
      simulation.suspend.change %src to ^resume : !simulation.ref<!array>
    ^resume:
      cf.br ^outer(%start : i8)
    }
    // CHECK-LABEL: regions @zero_trip valid=1
    simulation.code_unit.decl 5 in 0 always_comb hierarchy "zero_trip"
    simulation.func @zero_trip(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 5 : i64} {
      %src = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %dst = simulation.context.storage %ctx[1] : !simulation.ref<!array>
      %bound = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %start = arith.constant 0 : i8
      %limit = arith.constant 1 : i8
      %step = arith.constant 1 : i8
      %kstart = arith.constant 0 : i8
      %kstep = arith.constant 1 : i8
      cf.br ^outer(%start : i8)
    ^outer(%i: i8):
      %more = arith.cmpi slt, %i, %limit : i8
      cf.cond_br %more, ^inner(%kstart : i8), ^wait
    ^inner(%k: i8):
      %again = arith.cmpi slt, %k, %i : i8
      cf.cond_br %again, ^body, ^advance
    ^body:
      %s = simulation.ref.array_element %src[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %d = simulation.ref.array_element %dst[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %v = simulation.ref.load %s : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %d : i8, !simulation.ref<i8>
      %knext = arith.addi %k, %kstep : i8
      cf.br ^inner(%knext : i8)
    ^advance:
      %next = arith.addi %i, %step : i8
      cf.br ^outer(%next : i8)
    ^wait:
      simulation.suspend.change %src to ^resume : !simulation.ref<!array>
    ^resume:
      cf.br ^outer(%start : i8)
    }
    // CHECK-LABEL: regions @wrap valid=0 reason=termination
    simulation.code_unit.decl 6 in 0 always_comb hierarchy "wrap"
    simulation.func @wrap(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 6 : i64} {
      %src = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %dst = simulation.context.storage %ctx[1] : !simulation.ref<!array>
      %bound = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %start = arith.constant 120 : i8
      %limit = arith.constant 127 : i8
      %step = arith.constant 1 : i8
      %kstart = arith.constant 0 : i8
      %kstep = arith.constant 5 : i8
      cf.br ^outer(%start : i8)
    ^outer(%i: i8):
      %more = arith.cmpi slt, %i, %limit : i8
      cf.cond_br %more, ^inner(%kstart : i8), ^wait
    ^inner(%k: i8):
      %again = arith.cmpi slt, %k, %i : i8
      cf.cond_br %again, ^body, ^advance
    ^body:
      %s = simulation.ref.array_element %src[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %d = simulation.ref.array_element %dst[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %v = simulation.ref.load %s : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %d : i8, !simulation.ref<i8>
      %knext = arith.addi %k, %kstep : i8
      cf.br ^inner(%knext : i8)
    ^advance:
      %next = arith.addi %i, %step : i8
      cf.br ^outer(%next : i8)
    ^wait:
      simulation.suspend.change %src to ^resume : !simulation.ref<!array>
    ^resume:
      cf.br ^outer(%start : i8)
    }
    // CHECK-LABEL: regions @loaded_bound valid=0 reason=termination
    simulation.code_unit.decl 7 in 0 always_comb hierarchy "loaded_bound"
    simulation.func @loaded_bound(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 7 : i64} {
      %src = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %dst = simulation.context.storage %ctx[1] : !simulation.ref<!array>
      %bound = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %start = arith.constant 0 : i8
      %limit = arith.constant 4 : i8
      %step = arith.constant 1 : i8
      %kstart = arith.constant 0 : i8
      %kstep = arith.constant 1 : i8
      cf.br ^outer(%start : i8)
    ^outer(%i: i8):
      %more = arith.cmpi slt, %i, %limit : i8
      cf.cond_br %more, ^inner(%kstart : i8), ^wait
    ^inner(%k: i8):
      %dynamic = simulation.ref.load %bound : !simulation.ref<i8> -> i8
      %again = arith.cmpi slt, %k, %dynamic : i8
      cf.cond_br %again, ^body, ^advance
    ^body:
      %s = simulation.ref.array_element %src[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %d = simulation.ref.array_element %dst[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %v = simulation.ref.load %s : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %d : i8, !simulation.ref<i8>
      %knext = arith.addi %k, %kstep : i8
      cf.br ^inner(%knext : i8)
    ^advance:
      %next = arith.addi %i, %step : i8
      cf.br ^outer(%next : i8)
    ^wait:
      simulation.suspend.change %src to ^resume : !simulation.ref<!array>
    ^resume:
      cf.br ^outer(%start : i8)
    }
    // CHECK-LABEL: regions @unproved_inner valid=0 reason=termination
    simulation.code_unit.decl 8 in 0 always_comb hierarchy "unproved_inner"
    simulation.func @unproved_inner(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 8 : i64} {
      %src = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %dst = simulation.context.storage %ctx[1] : !simulation.ref<!array>
      %bound = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %start = arith.constant 0 : i8
      %limit = arith.constant 4 : i8
      %step = arith.constant 1 : i8
      %kstart = arith.constant 0 : i8
      %kstep = arith.constant 0 : i8
      cf.br ^outer(%start : i8)
    ^outer(%i: i8):
      %more = arith.cmpi slt, %i, %limit : i8
      cf.cond_br %more, ^inner(%kstart : i8), ^wait
    ^inner(%k: i8):
      %again = arith.cmpi slt, %k, %i : i8
      cf.cond_br %again, ^body, ^advance
    ^body:
      %s = simulation.ref.array_element %src[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %d = simulation.ref.array_element %dst[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %v = simulation.ref.load %s : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %d : i8, !simulation.ref<i8>
      %knext = arith.addi %k, %kstep : i8
      cf.br ^inner(%knext : i8)
    ^advance:
      %next = arith.addi %i, %step : i8
      cf.br ^outer(%next : i8)
    ^wait:
      simulation.suspend.change %src to ^resume : !simulation.ref<!array>
    ^resume:
      cf.br ^outer(%start : i8)
    }
    // CHECK-LABEL: regions @shifted valid=1
    // CHECK-NEXT: node cf.br read:s0[0,64) write:s1[0,64)
    // MEM-LABEL: activation @shifted valid=1 complete=1
    // MEM-NEXT: live-in s0[0,64)
    // MEM-NEXT: must-write s1[0,64)
    // MEM-NEXT: may-write s1[0,64)
    simulation.code_unit.decl 9 in 0 always_comb hierarchy "shifted"
    simulation.func @shifted(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 9 : i64} {
      %src = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %dst = simulation.context.storage %ctx[1] : !simulation.ref<!array>
      %start = arith.constant 0 : i8
      %limit = arith.constant 4 : i8
      %step = arith.constant 1 : i8
      %kstart = arith.constant 0 : i8
      %kstep = arith.constant 1 : i8
      cf.br ^outer(%start : i8)
    ^outer(%i: i8):
      %more = arith.cmpi slt, %i, %limit : i8
      cf.cond_br %more, ^inner(%kstart : i8), ^wait
    ^inner(%k: i8):
      %bound = arith.shli %step, %i : i8
      %again = arith.cmpi slt, %k, %bound : i8
      cf.cond_br %again, ^body, ^advance
    ^body:
      %s = simulation.ref.array_element %src[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %d = simulation.ref.array_element %dst[%k] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %v = simulation.ref.load %s : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %d : i8, !simulation.ref<i8>
      %knext = arith.addi %k, %kstep : i8
      cf.br ^inner(%knext : i8)
    ^advance:
      %next = arith.addi %i, %step : i8
      cf.br ^outer(%next : i8)
    ^wait:
      simulation.suspend.change %src to ^resume : !simulation.ref<!array>
    ^resume:
      cf.br ^outer(%start : i8)
    }
  }
}
