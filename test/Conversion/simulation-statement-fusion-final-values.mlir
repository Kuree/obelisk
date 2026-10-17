// RUN: obelisk-opt %s --split-input-file '--test-obelisk-activation-regions=functions=overwrite reaching-values=true inputs=true' -o /dev/null 2>&1 | FileCheck %s --check-prefix=INPUT
// RUN: obelisk-opt %s --split-input-file --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,schedule-plan-combinational-statements{grow-acyclic=false},obelisk-sim-materialize-compute-fusion,obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s
// A transient old-state publication is transparent only when all final outputs
// are independent of it AND every output's observer belongs to the transitive
// transition-insensitive closure (IEEE 1800-2023 4.3). Ordinary fusion admission
// still includes intermediate dependencies; @overwrite remains a separate actor
// (growth is off, so it is not absorbed as an acyclic neighbor).
// CHECK-LABEL: simulation.design @final_overwrite
// CHECK: simulation.spawn @__obelisk_region_kernel_0_0
// CHECK: simulation.spawn @overwrite
module {
  simulation.design @final_overwrite {
    simulation.scope.decl 0
    simulation.scope.decl 1 parent 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.storage.decl 4 in 0 : i1 design
    simulation.storage.decl 5 in 0 : i8 design
    simulation.storage.decl 6 in 0 : i8 design
    simulation.storage.decl 7 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "producer_and_consumer"
    simulation.code_unit.decl 2 in 1 always_comb hierarchy "transform"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "overwrite"
    simulation.code_unit.decl 4 in 0 always hierarchy "sample"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %a = simulation.spawn @producer_and_consumer(%ctx) : !simulation.context -> !simulation.process
      %b = simulation.spawn @transform(%ctx) : !simulation.context -> !simulation.process
      %c = simulation.spawn @overwrite(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @producer_and_consumer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %address = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %bank_address = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %bank_result = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %result = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %r = simulation.ref.load %bank_result : !simulation.ref<i8> -> i8
      simulation.ref.store %r to %result : i8, !simulation.ref<i8>
      %a = simulation.ref.load %address : !simulation.ref<i8> -> i8
      simulation.ref.store %a to %bank_address : i8, !simulation.ref<i8>
      simulation.suspend.any %address, %bank_result edges [0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i8>
    }
    simulation.func @transform(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %address = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %result = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %address : !simulation.ref<i8> -> i8
      %one = arith.constant 1 : i8
      %r = arith.addi %a, %one : i8
      simulation.ref.store %r to %result : i8, !simulation.ref<i8>
      simulation.suspend.change %address to ^body : !simulation.ref<i8>
    }
    // INPUT-LABEL: regions @overwrite valid=1
    // INPUT: values valid=1
    // INPUT-NEXT: value-input s3[0,8) s5[0,8)
    // INPUT-NEXT: final-input s3[0,8)
    simulation.func @overwrite(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      %input = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %scratch = simulation.context.storage %ctx[5] : !simulation.ref<i8>
      %output = simulation.context.storage %ctx[6] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %old = simulation.ref.load %scratch : !simulation.ref<i8> -> i8
      simulation.ref.store %old to %output : i8, !simulation.ref<i8>
      %new = simulation.ref.load %input : !simulation.ref<i8> -> i8
      simulation.ref.store %new to %scratch : i8, !simulation.ref<i8>
      simulation.ref.store %new to %output : i8, !simulation.ref<i8>
      simulation.suspend.change %input to ^body : !simulation.ref<i8>
    }
  }
}

// -----

// CHECK-LABEL: simulation.design @old_value_escapes
// CHECK-NOT: __obelisk_region_kernel_
// CHECK: simulation.spawn @producer_and_consumer
// CHECK: simulation.spawn @transform
// CHECK: simulation.spawn @overwrite
// CHECK-NOT: __obelisk_region_kernel_
module {
  simulation.design @old_value_escapes {
    simulation.scope.decl 0
    simulation.scope.decl 1 parent 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.storage.decl 4 in 0 : i1 design
    simulation.storage.decl 5 in 0 : i8 design
    simulation.storage.decl 6 in 0 : i8 design
    simulation.storage.decl 7 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "producer_and_consumer"
    simulation.code_unit.decl 2 in 1 always_comb hierarchy "transform"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "overwrite"
    simulation.code_unit.decl 4 in 0 always hierarchy "sample"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %a = simulation.spawn @producer_and_consumer(%ctx) : !simulation.context -> !simulation.process
      %b = simulation.spawn @transform(%ctx) : !simulation.context -> !simulation.process
      %c = simulation.spawn @overwrite(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @producer_and_consumer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %address = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %bank_address = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %bank_result = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %result = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %r = simulation.ref.load %bank_result : !simulation.ref<i8> -> i8
      simulation.ref.store %r to %result : i8, !simulation.ref<i8>
      %a = simulation.ref.load %address : !simulation.ref<i8> -> i8
      simulation.ref.store %a to %bank_address : i8, !simulation.ref<i8>
      simulation.suspend.any %address, %bank_result edges [0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i8>
    }
    simulation.func @transform(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %address = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %result = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %address : !simulation.ref<i8> -> i8
      %one = arith.constant 1 : i8
      %r = arith.addi %a, %one : i8
      simulation.ref.store %r to %result : i8, !simulation.ref<i8>
      simulation.suspend.change %address to ^body : !simulation.ref<i8>
    }
    // INPUT-LABEL: regions @overwrite valid=1
    // INPUT: values valid=1
    // INPUT-NEXT: value-input s3[0,8) s5[0,8)
    // INPUT-NEXT: final-input s3[0,8) s5[0,8)
    simulation.func @overwrite(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      %input = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %scratch = simulation.context.storage %ctx[5] : !simulation.ref<i8>
      %output = simulation.context.storage %ctx[6] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %old = simulation.ref.load %scratch : !simulation.ref<i8> -> i8
      simulation.ref.store %old to %output : i8, !simulation.ref<i8>
      %new = simulation.ref.load %input : !simulation.ref<i8> -> i8
      simulation.ref.store %new to %scratch : i8, !simulation.ref<i8>
      simulation.suspend.change %input to ^body : !simulation.ref<i8>
    }
  }
}

// -----

// CHECK-LABEL: simulation.design @transition_observed
// CHECK-NOT: __obelisk_region_kernel_
// CHECK: simulation.spawn @producer_and_consumer
// CHECK: simulation.spawn @transform
// CHECK: simulation.spawn @overwrite
// CHECK-NOT: __obelisk_region_kernel_
module {
  simulation.design @transition_observed {
    simulation.scope.decl 0
    simulation.scope.decl 1 parent 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.storage.decl 4 in 0 : i1 design
    simulation.storage.decl 5 in 0 : i8 design
    simulation.storage.decl 6 in 0 : i8 design
    simulation.storage.decl 7 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "producer_and_consumer"
    simulation.code_unit.decl 2 in 1 always_comb hierarchy "transform"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "overwrite"
    simulation.code_unit.decl 4 in 0 always hierarchy "sample"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %a = simulation.spawn @producer_and_consumer(%ctx) : !simulation.context -> !simulation.process
      %b = simulation.spawn @transform(%ctx) : !simulation.context -> !simulation.process
      %c = simulation.spawn @overwrite(%ctx) : !simulation.context -> !simulation.process
      %d = simulation.spawn @sample(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @producer_and_consumer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %address = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %bank_address = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %bank_result = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %result = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %r = simulation.ref.load %bank_result : !simulation.ref<i8> -> i8
      simulation.ref.store %r to %result : i8, !simulation.ref<i8>
      %a = simulation.ref.load %address : !simulation.ref<i8> -> i8
      simulation.ref.store %a to %bank_address : i8, !simulation.ref<i8>
      simulation.suspend.any %address, %bank_result edges [0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i8>
    }
    simulation.func @transform(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %address = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %result = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %address : !simulation.ref<i8> -> i8
      %one = arith.constant 1 : i8
      %r = arith.addi %a, %one : i8
      simulation.ref.store %r to %result : i8, !simulation.ref<i8>
      simulation.suspend.change %address to ^body : !simulation.ref<i8>
    }
    // INPUT-LABEL: regions @overwrite valid=1
    // INPUT: values valid=1
    // INPUT-NEXT: value-input s3[0,8) s5[0,8)
    // INPUT-NEXT: final-input s3[0,8)
    simulation.func @overwrite(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      %input = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %scratch = simulation.context.storage %ctx[5] : !simulation.ref<i8>
      %output = simulation.context.storage %ctx[6] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %old = simulation.ref.load %scratch : !simulation.ref<i8> -> i8
      simulation.ref.store %old to %output : i8, !simulation.ref<i8>
      %new = simulation.ref.load %input : !simulation.ref<i8> -> i8
      simulation.ref.store %new to %scratch : i8, !simulation.ref<i8>
      simulation.ref.store %new to %output : i8, !simulation.ref<i8>
      simulation.suspend.change %input to ^body : !simulation.ref<i8>
    }
    simulation.func @sample(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      %output = simulation.context.storage %ctx[6] : !simulation.ref<i8>
      %count = simulation.context.storage %ctx[7] : !simulation.ref<i8>
      cf.br ^wait
    ^wait:
      simulation.suspend.change %output to ^body : !simulation.ref<i8>
    ^body:
      %old = simulation.ref.load %count : !simulation.ref<i8> -> i8
      %one = arith.constant 1 : i8
      %next = arith.addi %old, %one : i8
      simulation.ref.store %next to %count : i8, !simulation.ref<i8>
      cf.br ^wait
    }
  }
}
