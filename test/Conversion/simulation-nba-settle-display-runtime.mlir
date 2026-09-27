// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph{vpi=read},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=read},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.read.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.read.llvm.mlir

// Runtime behavior is checked in ../Runtime/simulation-nba-settle-display-runtime.test.

// Read-only VPI with no readers must preserve the same native lowering and
// mixed-tier checkpoint semantics, including clock phase and X clearing.
//
// IEEE 1800-2023 4.5, 4.9.4, 10.4.2, and 21.2.1:
// a clock activation stages A, A's publication stages B in a SECOND NBA
// iteration, and immediate displays must observe values before their own NBA.
// Empty-barrier bypass must not suppress the second commit, and canonical
// handover must clear B's initial X. No display may execute in a dry-run probe.
// PLAN-DAG: llvm.call @obelisk_rt_v1_eval_display
// PLAN-DAG: llvm.func @__obelisk_eval_dispatch_v1
// PLAN-DAG: llvm.call @obelisk_rt_v1_scheduler_prepare_periodic_aot

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  simulation.design @nba_settle {
    simulation.scope.decl 0 hierarchy "nba_settle"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "nba_settle.root"
    simulation.code_unit.decl 2 in 0 always hierarchy "nba_settle.clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "nba_settle.writer"
    simulation.code_unit.decl 4 in 0 always hierarchy "nba_settle.cascade"
    simulation.code_unit.decl 5 in 0 initial hierarchy "nba_settle.check"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design
    simulation.storage.decl 2 in 0 : !simulation.logic<1> design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %a = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<1>>
      %b = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<1>>
      %zero = simulation.logic.constant false, false : !simulation.logic<1>
      %x = simulation.logic.constant false, true : !simulation.logic<1>
      simulation.ref.store %zero to %clk : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %zero to %a : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %x to %b : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %p = simulation.spawn @writer(%ctx, %clk) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %q = simulation.spawn @cascade(%ctx, %a) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %r = simulation.spawn @check(%ctx) : !simulation.context -> !simulation.process
      %c = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }
    simulation.func @clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clk: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^toggle {site = #schedule.continuation<id = 1>, timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = simulation.ref.load %clk : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %new = simulation.logic.unary bit_not %old : (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %new to %clk : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      cf.br ^wait
    }
    simulation.func @writer(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clk: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^write {site = #schedule.continuation<id = 2>} : !simulation.ref<!simulation.logic<1>>
    ^write:
      %a = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<1>>
      %b = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<1>>
      %old = simulation.ref.load %a : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %new = simulation.logic.unary bit_not %old : (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.nba.enqueue %new to %a : (!simulation.logic<1>, !simulation.ref<!simulation.logic<1>>) -> ()
      %current = simulation.ref.load %a : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %downstream = simulation.ref.load %b : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %channel = arith.constant 1 : i32
      %format = simulation.bytes.constant "active %b %b"
      simulation.display %ctx to %channel(%format, %current, %downstream) newline = true radix = <binary> flags = [0, 0, 0] {scope = "nba_settle.writer"} : !simulation.bytes, !simulation.logic<1>, !simulation.logic<1>
      cf.br ^wait
    }
    simulation.func @cascade(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %a to ^write {site = #schedule.continuation<id = 3>} : !simulation.ref<!simulation.logic<1>>
    ^write:
      %b = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<1>>
      %new = simulation.ref.load %a : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.nba.enqueue %new to %b : (!simulation.logic<1>, !simulation.ref<!simulation.logic<1>>) -> ()
      %old = simulation.ref.load %b : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %channel = arith.constant 1 : i32
      %format = simulation.bytes.constant "cascade %b %b"
      simulation.display %ctx to %channel(%format, %new, %old) newline = true radix = <binary> flags = [0, 0, 0] {scope = "nba_settle.cascade"} : !simulation.bytes, !simulation.logic<1>, !simulation.logic<1>
      cf.br ^wait
    }
    simulation.func @check(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 5 : i64} {
      %delay = simulation.time.constant 2
      simulation.suspend.delay %delay to ^first {site = #schedule.continuation<id = 4>, timing = #schedule.timing_site<id = 1, kind = calendar>}
    ^first:
      %a0 = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<1>>
      %b0 = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<1>>
      %va0 = simulation.ref.load %a0 : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %vb0 = simulation.ref.load %b0 : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %ch0 = arith.constant 1 : i32
      %now0 = simulation.time.now %ctx
      %fmt0 = simulation.bytes.constant "settled %0d %b %b"
      simulation.display %ctx to %ch0(%fmt0, %now0, %va0, %vb0) newline = true radix = <binary> flags = [0, 0, 0, 0] : !simulation.bytes, i64, !simulation.logic<1>, !simulation.logic<1>
      %delay0 = simulation.time.constant 2
      simulation.suspend.delay %delay0 to ^second {site = #schedule.continuation<id = 5>, timing = #schedule.timing_site<id = 2, kind = calendar>}
    ^second:
      %a1 = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<1>>
      %b1 = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<1>>
      %va1 = simulation.ref.load %a1 : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %vb1 = simulation.ref.load %b1 : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %ch1 = arith.constant 1 : i32
      %now1 = simulation.time.now %ctx
      %fmt1 = simulation.bytes.constant "settled %0d %b %b"
      simulation.display %ctx to %ch1(%fmt1, %now1, %va1, %vb1) newline = true radix = <binary> flags = [0, 0, 0, 0] : !simulation.bytes, i64, !simulation.logic<1>, !simulation.logic<1>
      %delay1 = simulation.time.constant 2
      simulation.suspend.delay %delay1 to ^third {site = #schedule.continuation<id = 6>, timing = #schedule.timing_site<id = 3, kind = calendar>}
    ^third:
      %a2 = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<1>>
      %b2 = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<1>>
      %va2 = simulation.ref.load %a2 : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %vb2 = simulation.ref.load %b2 : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %ch2 = arith.constant 1 : i32
      %now2 = simulation.time.now %ctx
      %fmt2 = simulation.bytes.constant "settled %0d %b %b"
      simulation.display %ctx to %ch2(%fmt2, %now2, %va2, %vb2) newline = true radix = <binary> flags = [0, 0, 0, 0] : !simulation.bytes, i64, !simulation.logic<1>, !simulation.logic<1>
      %status = arith.constant 1 : i32
      simulation.finish %ctx, %status
      simulation.return
    }
  }
}
