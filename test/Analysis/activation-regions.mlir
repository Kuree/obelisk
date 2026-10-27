// RUN: talos-opt %s --test-talos-activation-regions -o %t.analyzed 2>&1 | FileCheck %s
// RUN: talos-opt %s -o %t.original
// RUN: diff %t.original %t.analyzed

module {
  simulation.design @regions {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.unpacked_array<0 : 2 x i8> design
    simulation.storage.decl 1 in 0 : !simulation.unpacked_array<0 : 2 x i8> design
    simulation.storage.decl 2 in 0 : !simulation.unpacked_array<0 : 2 x i8> design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.storage.decl 4 in 0 : i1 design

    // One process produces addresses and consumes the resulting bank data.
    // Each loop is one CFG region unit. Its iterations and all
    // intermediate reduction writes remain inside the node. The initial and
    // resumed induction zeros are different SSA values but equivalent.
    // CHECK: regions @address_then_reduction valid=1
    // CHECK-NEXT: node cf.br read:s0[0,24) write:s1[0,24)
    // CHECK-NEXT: node simulation.ref.store write:s3[0,8)
    // CHECK-NEXT: node cf.br read:s2[0,24) read:s3[0,8) write:s3[0,8)
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "address_then_reduction"
    simulation.func @address_then_reduction(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %addresses = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.unpacked_array<0 : 2 x i8>>
      %forwarded = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.unpacked_array<0 : 2 x i8>>
      %data = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.unpacked_array<0 : 2 x i8>>
      %result = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %limit = arith.constant 3 : i32
      %seed = arith.constant 0 : i8
      cf.br ^copy(%zero : i32)
    ^resume:
      %reset = arith.constant {simulation.rematerialized} 0 : i32
      cf.br ^copy(%reset : i32)
    ^copy(%i: i32):
      %more = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %more, ^copy_body, ^initialize
    ^copy_body:
      %src = simulation.ref.array_element %addresses[%i] : (!simulation.ref<!simulation.unpacked_array<0 : 2 x i8>>, i32) -> !simulation.ref<i8>
      %dst = simulation.ref.array_element %forwarded[%i] : (!simulation.ref<!simulation.unpacked_array<0 : 2 x i8>>, i32) -> !simulation.ref<i8>
      %address = simulation.ref.load %src : !simulation.ref<i8> -> i8
      simulation.ref.store %address to %dst : i8, !simulation.ref<i8>
      %next_i = arith.addi %i, %one : i32
      cf.br ^copy(%next_i : i32)
    ^initialize:
      simulation.ref.store %seed to %result : i8, !simulation.ref<i8>
      cf.br ^reduce(%zero : i32)
    ^reduce(%j: i32):
      %again = arith.cmpi slt, %j, %limit : i32
      cf.cond_br %again, ^reduce_body, ^wait
    ^reduce_body:
      %lane = simulation.ref.array_element %data[%j] : (!simulation.ref<!simulation.unpacked_array<0 : 2 x i8>>, i32) -> !simulation.ref<i8>
      %value = simulation.ref.load %lane : !simulation.ref<i8> -> i8
      %old = simulation.ref.load %result : !simulation.ref<i8> -> i8
      %new = arith.xori %old, %value : i8
      simulation.ref.store %new to %result : i8, !simulation.ref<i8>
      %next_j = arith.addi %j, %one : i32
      cf.br ^reduce(%next_j : i32)
    ^wait:
      simulation.suspend.any %addresses, %data edges [0, 0] to ^resume : !simulation.ref<!simulation.unpacked_array<0 : 2 x i8>>, !simulation.ref<!simulation.unpacked_array<0 : 2 x i8>>
    }

    // Activation-local retained state cannot be replaced by the initial value.
    // CHECK: regions @changing_resume valid=0 reason=initial/resume equivalence
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "changing_resume"
    simulation.func @changing_resume(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %flag = simulation.context.storage %ctx[4] : !simulation.ref<i1>
      %out = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %zero = arith.constant 0 : i8
      %one = arith.constant 1 : i8
      cf.br ^body(%zero : i8)
    ^body(%state: i8):
      simulation.ref.store %state to %out : i8, !simulation.ref<i8>
      %next = arith.addi %state, %one : i8
      simulation.suspend.change %flag to ^body(%next : i8) : !simulation.ref<i1>
    }

    // No termination proof for an input-dependent procedural loop.
    // CHECK: regions @unbounded valid=0 reason=termination
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "unbounded"
    simulation.func @unbounded(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      %flag = simulation.context.storage %ctx[4] : !simulation.ref<i1>
      cf.br ^loop
    ^loop:
      %again = simulation.ref.load %flag : !simulation.ref<i1> -> i1
      cf.cond_br %again, ^loop, ^wait
    ^wait:
      simulation.suspend.change %flag to ^loop : !simulation.ref<i1>
    }

    // Alternative writes may be joined by ordinary CFG canonicalization.
    // Neither source store must dominate the suspension individually.
    // CHECK: regions @alternative_writes valid=1
    // CHECK-NEXT: node simulation.ref.load read:s4[0,1)
    // CHECK-NEXT: node simulation.ref.store write:s3[0,8)
    simulation.code_unit.decl 4 in 0 always_comb hierarchy "alternative_writes"
    simulation.func @alternative_writes(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 4 : i64} {
      %flag = simulation.context.storage %ctx[4] : !simulation.ref<i1>
      %out = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %zero = arith.constant 0 : i8
      %one = arith.constant 1 : i8
      cf.br ^body
    ^body:
      %test = simulation.ref.load %flag : !simulation.ref<i1> -> i1
      cf.cond_br %test, ^yes, ^no
    ^yes:
      simulation.ref.store %one to %out : i8, !simulation.ref<i8>
      cf.br ^wait
    ^no:
      simulation.ref.store %zero to %out : i8, !simulation.ref<i8>
      cf.br ^wait
    ^wait:
      simulation.suspend.change %flag to ^body : !simulation.ref<i1>
    }

    // An entry-only load is a captured snapshot, not a per-activation input.
    // CHECK: regions @frozen_capture valid=0 reason=initial/resume equivalence
    simulation.code_unit.decl 5 in 0 always_comb hierarchy "frozen_capture"
    simulation.func @frozen_capture(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 5 : i64} {
      %in = simulation.context.storage %ctx[4] : !simulation.ref<i1>
      %out = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %snapshot = simulation.ref.load %in : !simulation.ref<i1> -> i1
      %value = arith.extui %snapshot : i1 to i8
      cf.br ^body
    ^body:
      simulation.ref.store %value to %out : i8, !simulation.ref<i8>
      simulation.suspend.change %in to ^body : !simulation.ref<i1>
    }
    // A terminating path is not equivalent to reactivating an always-comb
    // process, even when all paths write a final value before leaving.
    // CHECK: regions @terminating_path valid=0
    simulation.code_unit.decl 6 in 0 always_comb hierarchy "terminating_path"
    simulation.func @terminating_path(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 6 : i64} {
      %in = simulation.context.storage %ctx[4] : !simulation.ref<i1>
      %out = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %zero = arith.constant 0 : i8
      cf.br ^body
    ^body:
      %test = simulation.ref.load %in : !simulation.ref<i1> -> i1
      simulation.ref.store %zero to %out : i8, !simulation.ref<i8>
      cf.cond_br %test, ^exit, ^wait
    ^exit:
      simulation.return
    ^wait:
      simulation.suspend.change %in to ^body : !simulation.ref<i1>
    }
    // A retained bounds arm is unreachable for every induction partition.
    // Its exact original access sites may be ignored in the CFG view;
    // reachable sites still require their dense memory facts.
    // CHECK: regions @infeasible_bounds_arm valid=1
    // CHECK-NEXT: node cf.br read:s0[0,24) write:s1[0,24)
    simulation.code_unit.decl 7 in 0 always_comb hierarchy "infeasible_bounds_arm"
    simulation.func @infeasible_bounds_arm(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 7 : i64} {
      %src = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.unpacked_array<0 : 2 x i8>>
      %dst = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.unpacked_array<0 : 2 x i8>>
      %unused = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %limit = arith.constant 3 : i32
      %invalid = arith.constant 42 : i32
      cf.br ^loop(%zero : i32)
    ^loop(%i: i32):
      %more = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %more, ^body, ^wait
    ^body:
      %out_of_bounds = arith.cmpi eq, %i, %invalid : i32
      cf.cond_br %out_of_bounds, ^unreachable, ^copy
    ^unreachable:
      %old = simulation.ref.load %unused : !simulation.ref<i8> -> i8
      simulation.ref.store %old to %unused : i8, !simulation.ref<i8>
      cf.br ^step
    ^copy:
      %from = simulation.ref.array_element %src[%i] : (!simulation.ref<!simulation.unpacked_array<0 : 2 x i8>>, i32) -> !simulation.ref<i8>
      %to = simulation.ref.array_element %dst[%i] : (!simulation.ref<!simulation.unpacked_array<0 : 2 x i8>>, i32) -> !simulation.ref<i8>
      %value = simulation.ref.load %from : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %to : i8, !simulation.ref<i8>
      cf.br ^step
    ^step:
      %next = arith.addi %i, %one : i32
      cf.br ^loop(%next : i32)
    ^wait:
      simulation.suspend.change %src to ^loop(%zero : i32) : !simulation.ref<!simulation.unpacked_array<0 : 2 x i8>>
    }
  }
}
