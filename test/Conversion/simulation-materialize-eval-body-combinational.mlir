// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=CONTROL

// IEEE 1800-2023 9.2.2.2/9.2.2.3: keep the implicit sensitivity and the
// activation's ordered, conditional stores. A combinational/latch process
// that cannot be fused still needs an exact standalone Tier-1 body. In
// particular, operations preceding the terminal implicit wait are part of
// the activation; a false latch enable must not manufacture a store.
module attributes {schedule.native_scheduler = 3 : i32} {
  simulation.design @combinational {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i1 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.storage.decl 4 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "comb"
    simulation.code_unit.decl 2 in 0 always_latch hierarchy "latch"
    simulation.code_unit.decl 3 in 0 function hierarchy "disable_latch"
    simulation.code_unit.decl 4 in 0 function hierarchy "existing_eval_name"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %data = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %enable = simulation.context.storage %ctx[1] : !simulation.ref<i1>
      %out = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %held = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %marker = simulation.context.storage %ctx[4] : !simulation.ref<i8>
      %a = simulation.spawn @comb(%ctx, %data, %enable, %out, %marker) : !simulation.context, !simulation.ref<i8>, !simulation.ref<i1>, !simulation.ref<i8>, !simulation.ref<i8> -> !simulation.process
      %b = simulation.spawn @latch(%ctx, %data, %enable, %held) : !simulation.context, !simulation.ref<i8>, !simulation.ref<i1>, !simulation.ref<i8> -> !simulation.process
      simulation.return
    }
    // CHECK-LABEL: simulation.func @comb(
    // CHECK-SAME: schedule.eval.body = @[[COMB:comb.__obelisk_eval_body_1]]
    // CHECK: simulation.suspend.any %{{.*}}, %{{.*}} edges [0, 0]
    simulation.func @comb(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %data: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %enable: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %out: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64},
        %marker: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      cf.br ^body
    ^body:
      %scope = simulation.control.enter 10
      %value = simulation.ref.load %data : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %out : i8, !simulation.ref<i8>
      %en = simulation.ref.load %enable : !simulation.ref<i1> -> i1
      cf.cond_br %en, ^override, ^wait
    ^override:
      %ones = arith.constant -1 : i8
      simulation.ref.store %ones to %out : i8, !simulation.ref<i8>
      cf.br ^wait
    ^wait:
      %tag = arith.constant 7 : i8
      simulation.ref.store %tag to %marker : i8, !simulation.ref<i8>
      simulation.control.leave %scope
      simulation.suspend.any %data, %enable edges [0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i1>
    }
    // CHECK-LABEL: simulation.func @latch(
    // CHECK-SAME: schedule.eval.body = @[[LATCH:latch.__obelisk_eval_body_[0-9]+]]
    simulation.func @latch(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %data: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %enable: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %held: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64})
        attributes {entry_kind = 6 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %scope = simulation.control.enter 11
      %en = simulation.ref.load %enable : !simulation.ref<i1> -> i1
      cf.cond_br %en, ^assign, ^wait
    ^assign:
      %value = simulation.ref.load %data : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %held : i8, !simulation.ref<i8>
      cf.br ^wait
    ^wait:
      simulation.control.leave %scope
      simulation.suspend.any %data, %enable edges [0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i1>
    }
    simulation.func @disable_latch(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64,
                    simulation.dpi_export,
                    simulation.dpi_c_identifier = "disable_latch",
                    simulation.dpi_scope_id = 0 : i64,
                    simulation.dpi_export_id = 1 : i32,
                    simulation.dpi_logical_inputs = 0 : i32,
                    simulation.dpi_abi_signature = [],
                    simulation.dpi_aggregate_layouts = []} {
      simulation.control.disable 11 {hierarchical = true}
      simulation.return
    }
    // An existing symbol occupies the first generated name. The shared symbol
    // index must preserve it and assign the clone a distinct name.
    simulation.func private @comb.__obelisk_eval_body_0(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      simulation.return
    }
    // CHECK: simulation.func private @comb.__obelisk_eval_body_0(
    // CHECK-NEXT: simulation.return
    // CHECK: simulation.func private @[[COMB]](
    // CHECK: simulation.ref.store
    // CHECK: cf.cond_br
    // CHECK: arith.constant 7 : i8
    // CHECK: simulation.ref.store
    // CHECK: simulation.return
    // CHECK: arith.constant -1 : i8
    // CHECK: simulation.ref.store
    // CHECK: simulation.func private @[[LATCH]](
    // CHECK: cf.cond_br %{{.*}}, ^[[ASSIGN:bb[0-9]+]], ^[[WAIT:bb[0-9]+]]
    // CHECK: ^[[WAIT]]:
    // CHECK-NEXT: simulation.control.leave
    // CHECK-NEXT: simulation.return
    // CHECK: ^[[ASSIGN]]:
    // CHECK: simulation.ref.store
    // CHECK: cf.br ^[[WAIT]]
    // Canonical scopes remain intact. Only the unaddressable eval-only scope
    // is erased; an exported hierarchical disable pins target 11. Merely
    // exporting the callable must not pin the unrelated target 10.
    // CONTROL-LABEL: simulation.func @comb(
    // CONTROL: simulation.control.enter 10
    // CONTROL: simulation.control.leave
    // CONTROL-LABEL: simulation.func private @comb.__obelisk_eval_body_1(
    // CONTROL-NOT: simulation.control.
    // CONTROL-LABEL: simulation.func private @latch.__obelisk_eval_body_0(
    // CONTROL: simulation.control.enter 11
    // CONTROL: simulation.control.leave
  }
}
