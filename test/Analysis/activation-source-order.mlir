// RUN: obelisk-opt %s --test-obelisk-activation-regions='functions=fused,middle order=true' -o %t.analyzed 2>&1 | FileCheck %s
// RUN: obelisk-opt %s -o %t.original
// RUN: diff %t.original %t.analyzed
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,schedule-plan-combinational-statements,obelisk-sim-materialize-compute-fusion))' -o %t.fused
// RUN: FileCheck %s --check-prefix=FUSE < %t.fused
// RUN: obelisk-opt %t.fused --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' -o /dev/null
// Typed original-owner provenance preserves each process's source order without
// inventing a C -> A relation because those independent processes were fused.
// SSA edges and physical memory dependencies still require A -> B -> C.
#a = #schedule.source_owner<codeUnit = 1 : i64, continuation = 1 : i32>
#c = #schedule.source_owner<codeUnit = 2 : i64, continuation = 2 : i32>
module {
  simulation.design @separate {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "fused"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "original_c"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "middle"
    simulation.code_unit.decl 4 in 0 root_initializer hierarchy "root"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 4 : i64} {
      %a = simulation.spawn @fused(%ctx) : !simulation.context -> !simulation.process
      %b = simulation.spawn @middle(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    // CHECK-LABEL: regions @fused valid=1
    simulation.func @fused(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64, schedule.eval.source_owners = [#a, #c]} {
      cf.br ^body
    ^body:
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %request = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %response = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %output = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %r = simulation.ref.load %response {schedule.eval.source_owner = #c} : !simulation.ref<i8> -> i8
      simulation.ref.store %r to %output {schedule.eval.source_owner = #c} : i8, !simulation.ref<i8>
      %q = simulation.ref.load %input {schedule.eval.source_owner = #a} : !simulation.ref<i8> -> i8
      simulation.ref.store %q to %request {schedule.eval.source_owner = #a} : i8, !simulation.ref<i8>
      simulation.suspend.any %input, %response edges [0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i8>
    }
    // CHECK-LABEL: regions @middle valid=1
    simulation.func @middle(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %request = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %response = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %q = simulation.ref.load %request : !simulation.ref<i8> -> i8
      simulation.ref.store %q to %response : i8, !simulation.ref<i8>
      simulation.suspend.change %request to ^body : !simulation.ref<i8>
    }
    // CHECK: region order: acyclic
    // CHECK-NEXT: write s1[0,8)
    // CHECK-NEXT: write s2[0,8)
    // CHECK-NEXT: write s3[0,8)
  }
  simulation.design @same {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "fused"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "original_c"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "middle"
    simulation.code_unit.decl 4 in 0 root_initializer hierarchy "root"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 4 : i64} {
      %a = simulation.spawn @fused(%ctx) : !simulation.context -> !simulation.process
      %b = simulation.spawn @middle(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    // CHECK-LABEL: regions @fused valid=1
    simulation.func @fused(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64, schedule.eval.source_owners = [#a, #c]} {
      cf.br ^body
    ^body:
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %request = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %response = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %output = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %r = simulation.ref.load %response {schedule.eval.source_owner = #a} : !simulation.ref<i8> -> i8
      simulation.ref.store %r to %output {schedule.eval.source_owner = #a} : i8, !simulation.ref<i8>
      %q = simulation.ref.load %input {schedule.eval.source_owner = #a} : !simulation.ref<i8> -> i8
      simulation.ref.store %q to %request {schedule.eval.source_owner = #a} : i8, !simulation.ref<i8>
      simulation.suspend.any %input, %response edges [0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i8>
    }
    // CHECK-LABEL: regions @middle valid=1
    simulation.func @middle(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %request = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %response = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %q = simulation.ref.load %request : !simulation.ref<i8> -> i8
      simulation.ref.store %q to %response : i8, !simulation.ref<i8>
      simulation.suspend.change %request to ^body : !simulation.ref<i8>
    }
    // CHECK: region order: cyclic
  }
  simulation.design @incomplete {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "fused"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "original_c"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "middle"
    simulation.code_unit.decl 4 in 0 root_initializer hierarchy "root"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 4 : i64} {
      %a = simulation.spawn @fused(%ctx) : !simulation.context -> !simulation.process
      %b = simulation.spawn @middle(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    // CHECK-LABEL: regions @fused valid=1
    simulation.func @fused(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64, schedule.eval.source_owners = [#a, #c]} {
      cf.br ^body
    ^body:
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %request = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %response = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %output = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %r = simulation.ref.load %response : !simulation.ref<i8> -> i8
      simulation.ref.store %r to %output {schedule.eval.source_owner = #c} : i8, !simulation.ref<i8>
      %q = simulation.ref.load %input {schedule.eval.source_owner = #a} : !simulation.ref<i8> -> i8
      simulation.ref.store %q to %request {schedule.eval.source_owner = #a} : i8, !simulation.ref<i8>
      simulation.suspend.any %input, %response edges [0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i8>
    }
    // CHECK-LABEL: regions @middle valid=1
    simulation.func @middle(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %request = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %response = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %q = simulation.ref.load %request : !simulation.ref<i8> -> i8
      simulation.ref.store %q to %response : i8, !simulation.ref<i8>
      simulation.suspend.change %request to ^body : !simulation.ref<i8>
    }
    // CHECK: region order: cyclic
  }
}

// FUSE-LABEL: simulation.design @separate
// FUSE: simulation.spawn @__obelisk_region_kernel_
// FUSE-LABEL: simulation.func private @__obelisk_region_kernel_
// FUSE-SAME: schedule.eval.source_owners = [#schedule.source_owner<codeUnit = 1 : i64, continuation = 1 : i32>, #schedule.source_owner<codeUnit = 2 : i64, continuation = 2 : i32>, #schedule.source_owner<codeUnit = 3 : i64,
// FUSE-LABEL: simulation.design @same
// FUSE: simulation.spawn @fused
// FUSE: simulation.spawn @middle
// FUSE-LABEL: simulation.design @incomplete
// FUSE: simulation.spawn @fused
// FUSE: simulation.spawn @middle
