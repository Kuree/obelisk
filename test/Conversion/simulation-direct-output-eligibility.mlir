// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s

// Net snapshots with ordinary formats do not carry net handles into the
// direct output ABI. Literal v and escaped %%v are not strength queries.
// Real strength queries and user-file channels retain the runtime ABI.
// CHECK-LABEL: llvm.func @safe.__obelisk_eval_body
// CHECK-NOT: llvm.call @obelisk_rt_v1_display(
// CHECK: llvm.call @obelisk_rt_v1_eval_display(
// CHECK-LABEL: llvm.func @strength.__obelisk_eval_body
// CHECK-NOT: llvm.call @obelisk_rt_v1_eval_display(
// CHECK: llvm.call @obelisk_rt_v1_display(
// CHECK-LABEL: llvm.func @channel.__obelisk_eval_body
// CHECK-NOT: llvm.call @obelisk_rt_v1_eval_display(
// CHECK: llvm.call @obelisk_rt_v1_display(

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
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %net = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<1>>
      %a = simulation.spawn @safe(%ctx, %clock, %net) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.net<!simulation.logic<1>> -> !simulation.process
      %b = simulation.spawn @strength(%ctx, %clock, %net) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.net<!simulation.logic<1>> -> !simulation.process
      %c = simulation.spawn @channel(%ctx, %clock, %net) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.net<!simulation.logic<1>> -> !simulation.process
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
  }
}
