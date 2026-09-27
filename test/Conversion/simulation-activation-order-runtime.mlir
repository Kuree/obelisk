// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' -o %t.graph.mlir
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
  simulation.design @activation_order {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "consumer"
    simulation.code_unit.decl 3 in 0 initial hierarchy "driver"
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    simulation.storage.decl 1 in 0 : !simulation.logic<8> design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %consumer = simulation.spawn @a_consumer(%ctx) : !simulation.context -> !simulation.process
      %producer = simulation.spawn @z_driver(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @a_consumer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      %output = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<8>>
      cf.br ^body
    ^body:
      %old = simulation.ref.load %input : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      %next = simulation.logic.binary add %old, %one : !simulation.logic<8>
      simulation.ref.store %next to %output : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.change %input to ^body : !simulation.ref<!simulation.logic<8>>
    }
    simulation.func @z_driver(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      %output = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<8>>
      %five = simulation.logic.constant 5 : i8, 0 : i8 : !simulation.logic<8>
      simulation.ref.store %five to %input : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^second
    ^second:
      %first = simulation.ref.load %output : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %fmt = simulation.bytes.constant "settled %0d"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%fmt, %first) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !simulation.logic<8>
      %nine = simulation.logic.constant 9 : i8, 0 : i8 : !simulation.logic<8>
      simulation.ref.store %nine to %input : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      %again = simulation.time.constant 1
      simulation.suspend.delay %again to ^done
    ^done:
      %last = simulation.ref.load %output : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %lastfmt = simulation.bytes.constant "settled %0d"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%lastfmt, %last) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !simulation.logic<8>
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
  }
}
