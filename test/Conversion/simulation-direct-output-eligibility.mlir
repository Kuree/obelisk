// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph,talos-sim-verify-compute-graph,talos-sim-materialize-graph-regions,talos-sim-materialize-compute-fusion,talos-sim-specialize-static-state-nba,talos-sim-plan-static-superstep),convert-talos-sim-processes-to-llvm-coroutines)' | FileCheck %s
// RUN: talos-opt %s '--test-talos-set-module-attributes=attrs="{native.disable_two_state_specialization}"' | talos-opt --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph,talos-sim-verify-compute-graph,talos-sim-materialize-graph-regions,talos-sim-materialize-compute-fusion,talos-sim-specialize-static-state-nba,talos-sim-plan-static-superstep),convert-talos-sim-processes-to-llvm-coroutines)' | FileCheck %s --implicit-check-not='__talos_two_state' --implicit-check-not='schedule.eval.local_promotion_ranges'

// Net snapshots with ordinary formats do not carry net handles into the
// direct output ABI. Literal v and escaped %%v are not strength queries.
// Real strength queries and user-file channels retain the runtime ABI.
// Disabling state specialization must preserve this domain-independent split.
// CHECK-LABEL: llvm.func @safe.__talos_eval_body
// CHECK-NOT: llvm.call @talos_rt_v1_display(
// CHECK: llvm.call @talos_rt_v1_eval_display(
// CHECK-LABEL: llvm.func @strength.__talos_eval_body
// CHECK-NOT: llvm.call @talos_rt_v1_eval_display(
// CHECK: llvm.call @talos_rt_v1_display(
// CHECK-LABEL: llvm.func @channel.__talos_eval_body
// CHECK-NOT: llvm.call @talos_rt_v1_eval_display(
// CHECK: llvm.call @talos_rt_v1_display(
// An immediate assertion's $error only latches the final status (IEEE
// 1800-2023 20.10), so its report stays in the generated body instead of
// becoming a checkpoint leaf with path probes.
// CHECK-LABEL: llvm.func @failed.__talos_eval_body_0(
// CHECK: llvm.call @talos_rt_v1_scheduler_error(
// CHECK: llvm.call @talos_rt_v1_eval_display(
// CHECK-NOT: @failed.__talos_eval_body_0.__talos_checkpoint_path

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  simulation.design @output {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "output.root"
    simulation.code_unit.decl 2 in 0 always hierarchy "output.safe"
    simulation.code_unit.decl 3 in 0 always hierarchy "output.strength"
    simulation.code_unit.decl 4 in 0 always hierarchy "output.channel"
    simulation.code_unit.decl 5 in 0 always hierarchy "output.failed"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %net = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<1>>
      %a = simulation.spawn @safe(%ctx, %clock, %net) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.net<!simulation.logic<1>> -> !simulation.process
      %b = simulation.spawn @strength(%ctx, %clock, %net) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.net<!simulation.logic<1>> -> !simulation.process
      %c = simulation.spawn @channel(%ctx, %clock, %net) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.net<!simulation.logic<1>> -> !simulation.process
      %d = simulation.spawn @failed(%ctx, %clock, %net) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.net<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }
    simulation.func @safe(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %net: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clock to ^print {site = #schedule.continuation<id = 1>} : !simulation.ref<!simulation.logic<1>>
    ^print:
      %fd = arith.constant 1 : i32
      %format = simulation.bytes.constant "value %%v %b"
      %value = simulation.net.read %net : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.display %ctx to %fd(%format, %value, %net) newline = true radix = <decimal> flags = [0, 2048] {scope = "output"} : !simulation.bytes, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>
      cf.br ^wait
    }
    simulation.func @strength(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %net: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clock to ^print {site = #schedule.continuation<id = 2>} : !simulation.ref<!simulation.logic<1>>
    ^print:
      %fd = arith.constant 1 : i32
      %format = simulation.bytes.constant "%V"
      %value = simulation.net.read %net : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.display %ctx to %fd(%format, %value, %net) newline = true radix = <decimal> flags = [0, 2048] {scope = "output"} : !simulation.bytes, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>
      cf.br ^wait
    }
    simulation.func @channel(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %net: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clock to ^print {site = #schedule.continuation<id = 3>} : !simulation.ref<!simulation.logic<1>>
    ^print:
      %fd = arith.constant 2 : i32
      %format = simulation.bytes.constant "%b"
      %value = simulation.net.read %net : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.display %ctx to %fd(%format, %value, %net) newline = true radix = <decimal> flags = [0, 2048] {scope = "output"} : !simulation.bytes, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>
      cf.br ^wait
    }
    simulation.func @failed(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %net: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge negedge %clock to ^check {site = #schedule.continuation<id = 4>} : !simulation.ref<!simulation.logic<1>>
    ^check:
      %value = simulation.net.read %net : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %zero = simulation.logic.constant 0 : i1, 0 : i1 : !simulation.logic<1>
      %low = simulation.logic.compare eq %value, %zero : (!simulation.logic<1>, !simulation.logic<1>) -> !simulation.logic<1>
      %ok = simulation.logic.is_true %low : !simulation.logic<1>
      cf.cond_br %ok, ^wait, ^fail
    ^fail:
      simulation.error %ctx
      %fd = arith.constant -2147483646 : i32
      %format = simulation.bytes.constant "failed %x"
      simulation.display %ctx to %fd(%format, %value, %net) newline = true radix = <decimal> flags = [0, 2048] {scope = "output"} : !simulation.bytes, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>
      cf.br ^wait
    }
  }
}
