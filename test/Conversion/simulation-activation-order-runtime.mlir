// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' -o %t.graph.mlir
// RUN: FileCheck %s --check-prefix=GRAPH < %t.graph.mlir

// Runtime behavior is checked in ../Runtime/simulation-activation-order-runtime.test.

// The initial producer has a later ID than its combinational consumer.
// Sampling in later time slots is race-free regardless of startup ordering.
// The graph must be acyclic as well as producing the correct settled values.
// GRAPH: compute_graph =
// GRAPH-NOT: schedule = convergence
// GRAPH-NOT: schedule = control_loop
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @activation_order {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always_comb hierarchy "consumer"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "driver"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %consumer = obelisk_sim.spawn @a_consumer(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      %producer = obelisk_sim.spawn @z_driver(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @a_consumer(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %input = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %output = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<8>>
      cf.br ^body
    ^body:
      %old = obelisk_sim.ref.load %input : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %one = obelisk_sim.logic.constant 1 : i8, 0 : i8 : !obelisk_sim.logic<8>
      %next = obelisk_sim.logic.binary add %old, %one : !obelisk_sim.logic<8>
      obelisk_sim.ref.store %next to %output : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.suspend.change %input to ^body : !obelisk_sim.ref<!obelisk_sim.logic<8>>
    }
    obelisk_sim.func @z_driver(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %input = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %output = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %five = obelisk_sim.logic.constant 5 : i8, 0 : i8 : !obelisk_sim.logic<8>
      obelisk_sim.ref.store %five to %input : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^second
    ^second:
      %first = obelisk_sim.ref.load %output : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %fmt = obelisk_sim.bytes.constant "settled %0d"
      %channel = arith.constant 1 : i32
      obelisk_sim.display %ctx to %channel(%fmt, %first) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<8>
      %nine = obelisk_sim.logic.constant 9 : i8, 0 : i8 : !obelisk_sim.logic<8>
      obelisk_sim.ref.store %nine to %input : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %again = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %again to ^done
    ^done:
      %last = obelisk_sim.ref.load %output : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %lastfmt = obelisk_sim.bytes.constant "settled %0d"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%lastfmt, %last) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<8>
      %zero = arith.constant 0 : i32
      obelisk_sim.finish %ctx, %zero
      obelisk_sim.return
    }
  }
}
