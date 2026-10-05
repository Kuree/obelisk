// Statement DAG with an artificial process SCC and externally writable internal values.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 1 : i32
} {
  simulation.design @address_then_result {
    simulation.scope.decl 0 hierarchy "dag"
    simulation.code_unit.decl 5 in 0 root_initializer hierarchy "dag.root"
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design hierarchy "dag.input"
    simulation.storage.decl 1 in 0 : !simulation.logic<8> design hierarchy "dag.address"
    simulation.storage.decl 2 in 0 : !simulation.logic<8> design hierarchy "dag.internal"
    simulation.storage.decl 3 in 0 : !simulation.logic<8> design hierarchy "dag.output"
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "producer_and_consumer"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "transform"
    simulation.code_unit.decl 4 in 0 initial hierarchy "driver"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 5 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      %seven = simulation.logic.constant 7 : i8, 0 : i8 : !simulation.logic<8>
      simulation.ref.store %seven to %input : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      %a = simulation.spawn @producer_and_consumer(%ctx) : !simulation.context -> !simulation.process
      %b = simulation.spawn @transform(%ctx) : !simulation.context -> !simulation.process
      %d = simulation.spawn @driver(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @producer_and_consumer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %address = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      %bank_address = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<8>>
      %bank_result = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<8>>
      %result = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<8>>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %address : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.ref.store %a to %bank_address : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      %r = simulation.ref.load %bank_result : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.ref.store %r to %result : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.any %address, %bank_result edges [0, 0] to ^body : !simulation.ref<!simulation.logic<8>>, !simulation.ref<!simulation.logic<8>>
    }
    simulation.func @transform(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %address = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<8>>
      %result = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<8>>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %address : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      %r = simulation.logic.binary add %a, %one : !simulation.logic<8>
      simulation.ref.store %r to %result : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.change %address to ^body : !simulation.ref<!simulation.logic<8>>
    }
    simulation.func @driver(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      %internal = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<8>>
      %output = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<8>>
      %fmt = simulation.bytes.constant "step %0d input=%h internal=%h output=%h"
      %channel = arith.constant 1 : i32
      %delay = simulation.time.constant 1
      %zero = arith.constant 0 : i32
      simulation.suspend.delay %delay to ^step(%zero : i32)
    ^step(%index: i32):
      %i = simulation.ref.load %input : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %m = simulation.ref.load %internal : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %o = simulation.ref.load %output : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.display %ctx to %channel(%fmt, %index, %i, %m, %o) newline = true radix = <hex> flags = [0, 0, 0, 0, 0] : !simulation.bytes, i32, !simulation.logic<8>, !simulation.logic<8>, !simulation.logic<8>
      %one = arith.constant 1 : i32
      %next = arith.addi %index, %one : i32
      %limit = arith.constant 13 : i32
      %again = arith.cmpi ult, %next, %limit : i32
      cf.cond_br %again, ^wait, ^done
    ^wait:
      simulation.suspend.delay %delay to ^step(%next : i32)
    ^done:
      simulation.finish %ctx, %zero
      simulation.return
    }
  }
}
