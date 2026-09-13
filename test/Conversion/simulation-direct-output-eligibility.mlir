// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s

// Net snapshots with ordinary formats do not carry net handles into the
// direct output ABI. Literal v and escaped %%v are not strength queries.
// Real strength queries and non-stdout channels retain the runtime ABI.
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
  obelisk.native_scheduler = 3 : i32
} {
  obelisk_sim.design @output {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "output.root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "output.safe"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "output.strength"
    obelisk_sim.code_unit.decl 4 in 0 always hierarchy "output.channel"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %net = obelisk_sim.context.net %ctx[0] : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %a = obelisk_sim.spawn @safe(%ctx, %clock, %net) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %b = obelisk_sim.spawn @strength(%ctx, %clock, %net) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %c = obelisk_sim.spawn @channel(%ctx, %clock, %net) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @safe(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %net: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.edge posedge %clock to ^print {site = #obelisk_sim.continuation<id = 1>} : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^print:
      %fd = arith.constant 1 : i32
      %format = obelisk_sim.bytes.constant "value %%v %b"
      %value = obelisk_sim.net.read %net : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.display %ctx to %fd(%format, %value, %net) newline = true radix = 10 flags = [0, 2048] {scope = "output"} : !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>
      cf.br ^wait
    }
    obelisk_sim.func @strength(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %net: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.edge posedge %clock to ^print {site = #obelisk_sim.continuation<id = 2>} : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^print:
      %fd = arith.constant 1 : i32
      %format = obelisk_sim.bytes.constant "%V"
      %value = obelisk_sim.net.read %net : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.display %ctx to %fd(%format, %value, %net) newline = true radix = 10 flags = [0, 2048] {scope = "output"} : !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>
      cf.br ^wait
    }
    obelisk_sim.func @channel(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %net: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.edge posedge %clock to ^print {site = #obelisk_sim.continuation<id = 3>} : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^print:
      %fd = arith.constant 2 : i32
      %format = obelisk_sim.bytes.constant "%b"
      %value = obelisk_sim.net.read %net : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.display %ctx to %fd(%format, %value, %net) newline = true radix = 10 flags = [0, 2048] {scope = "output"} : !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>
      cf.br ^wait
    }
  }
}
