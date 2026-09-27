// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=CONTROL

// IEEE 1800-2023 9.2.2.2/9.2.2.3: keep the implicit sensitivity and the
// activation's ordered, conditional stores. A combinational/latch process
// that cannot be fused still needs an exact standalone Tier-1 body. In
// particular, operations preceding the terminal implicit wait are part of
// the activation; a false latch enable must not manufacture a store.
module attributes {schedule.native_scheduler = 3 : i32} {
  obelisk_sim.design @combinational {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i8 design
    obelisk_sim.storage.decl 1 in 0 : i1 design
    obelisk_sim.storage.decl 2 in 0 : i8 design
    obelisk_sim.storage.decl 3 in 0 : i8 design
    obelisk_sim.storage.decl 4 in 0 : i8 design
    obelisk_sim.code_unit.decl 1 in 0 always_comb hierarchy "comb"
    obelisk_sim.code_unit.decl 2 in 0 always_latch hierarchy "latch"
    obelisk_sim.code_unit.decl 3 in 0 function hierarchy "disable_latch"
    obelisk_sim.code_unit.decl 4 in 0 function hierarchy "existing_eval_name"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %data = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i8>
      %enable = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<i1>
      %out = obelisk_sim.context.storage %ctx[2] : !obelisk_sim.ref<i8>
      %held = obelisk_sim.context.storage %ctx[3] : !obelisk_sim.ref<i8>
      %marker = obelisk_sim.context.storage %ctx[4] : !obelisk_sim.ref<i8>
      %a = obelisk_sim.spawn @comb(%ctx, %data, %enable, %out, %marker) : !obelisk_sim.context, !obelisk_sim.ref<i8>, !obelisk_sim.ref<i1>, !obelisk_sim.ref<i8>, !obelisk_sim.ref<i8> -> !obelisk_sim.process
      %b = obelisk_sim.spawn @latch(%ctx, %data, %enable, %held) : !obelisk_sim.context, !obelisk_sim.ref<i8>, !obelisk_sim.ref<i1>, !obelisk_sim.ref<i8> -> !obelisk_sim.process
      obelisk_sim.return
    }
    // CHECK-LABEL: obelisk_sim.func @comb(
    // CHECK-SAME: schedule.eval.body = @[[COMB:comb.__obelisk_eval_body_1]]
    // CHECK: obelisk_sim.suspend.any %{{.*}}, %{{.*}} edges [0, 0]
    obelisk_sim.func @comb(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %data: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %enable: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64},
        %out: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64},
        %marker: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 4 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      cf.br ^body
    ^body:
      %scope = obelisk_sim.control.enter 10
      %value = obelisk_sim.ref.load %data : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.ref.store %value to %out : i8, !obelisk_sim.ref<i8>
      %en = obelisk_sim.ref.load %enable : !obelisk_sim.ref<i1> -> i1
      cf.cond_br %en, ^override, ^wait
    ^override:
      %ones = arith.constant -1 : i8
      obelisk_sim.ref.store %ones to %out : i8, !obelisk_sim.ref<i8>
      cf.br ^wait
    ^wait:
      %tag = arith.constant 7 : i8
      obelisk_sim.ref.store %tag to %marker : i8, !obelisk_sim.ref<i8>
      obelisk_sim.control.leave %scope
      obelisk_sim.suspend.any %data, %enable edges [0, 0] to ^body : !obelisk_sim.ref<i8>, !obelisk_sim.ref<i1>
    }
    // CHECK-LABEL: obelisk_sim.func @latch(
    // CHECK-SAME: schedule.eval.body = @[[LATCH:latch.__obelisk_eval_body_[0-9]+]]
    obelisk_sim.func @latch(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %data: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %enable: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64},
        %held: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64})
        attributes {entry_kind = 6 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %scope = obelisk_sim.control.enter 11
      %en = obelisk_sim.ref.load %enable : !obelisk_sim.ref<i1> -> i1
      cf.cond_br %en, ^assign, ^wait
    ^assign:
      %value = obelisk_sim.ref.load %data : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.ref.store %value to %held : i8, !obelisk_sim.ref<i8>
      cf.br ^wait
    ^wait:
      obelisk_sim.control.leave %scope
      obelisk_sim.suspend.any %data, %enable edges [0, 0] to ^body : !obelisk_sim.ref<i8>, !obelisk_sim.ref<i1>
    }
    obelisk_sim.func @disable_latch(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64,
                    obelisk_sim.dpi_export,
                    obelisk_sim.dpi_c_identifier = "disable_latch",
                    obelisk_sim.dpi_scope_id = 0 : i64,
                    obelisk_sim.dpi_export_id = 1 : i32,
                    obelisk_sim.dpi_logical_inputs = 0 : i32,
                    obelisk_sim.dpi_abi_signature = [],
                    obelisk_sim.dpi_aggregate_layouts = []} {
      obelisk_sim.control.disable 11 {hierarchical = true}
      obelisk_sim.return
    }
    // An existing symbol occupies the first generated name. The shared symbol
    // index must preserve it and assign the clone a distinct name.
    obelisk_sim.func private @comb.__obelisk_eval_body_0(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      obelisk_sim.return
    }
    // CHECK: obelisk_sim.func private @comb.__obelisk_eval_body_0(
    // CHECK-NEXT: obelisk_sim.return
    // CHECK: obelisk_sim.func private @[[COMB]](
    // CHECK: obelisk_sim.ref.store
    // CHECK: cf.cond_br
    // CHECK: arith.constant 7 : i8
    // CHECK: obelisk_sim.ref.store
    // CHECK: obelisk_sim.return
    // CHECK: arith.constant -1 : i8
    // CHECK: obelisk_sim.ref.store
    // CHECK: obelisk_sim.func private @[[LATCH]](
    // CHECK: cf.cond_br %{{.*}}, ^[[ASSIGN:bb[0-9]+]], ^[[WAIT:bb[0-9]+]]
    // CHECK: ^[[WAIT]]:
    // CHECK-NEXT: obelisk_sim.control.leave
    // CHECK-NEXT: obelisk_sim.return
    // CHECK: ^[[ASSIGN]]:
    // CHECK: obelisk_sim.ref.store
    // CHECK: cf.br ^[[WAIT]]
    // Canonical scopes remain intact. Only the unaddressable eval-only scope
    // is erased; an exported hierarchical disable pins target 11. Merely
    // exporting the callable must not pin the unrelated target 10.
    // CONTROL-LABEL: obelisk_sim.func @comb(
    // CONTROL: obelisk_sim.control.enter 10
    // CONTROL: obelisk_sim.control.leave
    // CONTROL-LABEL: obelisk_sim.func private @comb.__obelisk_eval_body_1(
    // CONTROL-NOT: obelisk_sim.control.
    // CONTROL-LABEL: obelisk_sim.func private @latch.__obelisk_eval_body_0(
    // CONTROL: obelisk_sim.control.enter 11
    // CONTROL: obelisk_sim.control.leave
  }
}
