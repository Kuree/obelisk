// REQUIRES: native-elf
// The harness interposes runtime calls with the ELF linker's --wrap option.
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=full},convert-obelisk-sim-processes-to-llvm-coroutines)' | obelisk-translate --mlir-to-llvmir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang -I%S/../Conversion/../../runtime/include -I%resource_dir/include -c %S/Inputs/vpi-local-force.c -o %t.helper.o
// RUN: %target_cxx %t.o %t.helper.o -Wl,--wrap=obelisk_rt_v1_scheduler_run -Wl,--wrap=obelisk_rt_v1_scheduler_run_aot %runtime_link_flags -o %t.exe
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.exe > %t.out 2> %t.diag
// RUN: FileCheck %s < %t.out
// RUN: FileCheck %s --check-prefix=TIERS < %t.diag

// Two independent NBA counters share an external clock. Forcing one through
// VPI must not fracture the other's generated execution (LRM 38.34, 10.6.2).
// CHECK: local force count=996 untouched=1000
// The seven forced edges use the affected fine actor. The independent
// counter retains Tier 1, so execution does not grow by 1,000 fine actors.
// TIERS: aot_node_executions=13
// TIERS-SAME: aot_fallbacks=0
// TIERS: obelisk-aot-actor slot=4 executions=1

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  simulation.design @external_clock {
    simulation.scope.decl 0 hierarchy "external_clock"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "external_clock.root"
    simulation.code_unit.decl 2 in 0 always hierarchy "external_clock.counter"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design hierarchy "external_clock.clk"
    simulation.storage.decl 1 in 0 : !simulation.logic<32> design hierarchy "external_clock.accumulator"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "external_clock.relay1"
    simulation.code_unit.decl 4 in 0 always_comb hierarchy "external_clock.relay2"
    simulation.storage.decl 2 in 0 : !simulation.logic<32> design
    simulation.storage.decl 3 in 0 : !simulation.logic<32> design hierarchy "external_clock.count"
    simulation.code_unit.decl 5 in 0 always hierarchy "external_clock.independent"
    simulation.storage.decl 4 in 0 : !simulation.logic<32> design hierarchy "external_clock.untouched"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %count = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<32>>
      %zero = simulation.logic.constant false, false : !simulation.logic<1>
      %zero32 = simulation.logic.constant 0 : i32, 0 : i32 : !simulation.logic<32>
      simulation.ref.store %zero to %clk : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %zero32 to %count : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      %middle = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<32>>
      %visible = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<32>>
      %relay1 = simulation.spawn @relay1(%ctx, %count, %middle) : !simulation.context, !simulation.ref<!simulation.logic<32>>, !simulation.ref<!simulation.logic<32>> -> !simulation.process
      %relay2 = simulation.spawn @relay2(%ctx, %middle, %visible) : !simulation.context, !simulation.ref<!simulation.logic<32>>, !simulation.ref<!simulation.logic<32>> -> !simulation.process
      %process = simulation.spawn @counter(%ctx, %clk) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %independent = simulation.context.storage %ctx[4] : !simulation.ref<!simulation.logic<32>>
      simulation.ref.store %zero32 to %independent : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      %other = simulation.spawn @independent(%ctx, %clk) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }
    simulation.func @counter(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clk: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^step {site = #schedule.continuation<id = 1>} : !simulation.ref<!simulation.logic<1>>
    ^step:
      %count = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<32>>
      %old = simulation.ref.load %count : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      %one = simulation.logic.constant 1 : i32, 0 : i32 : !simulation.logic<32>
      %next = simulation.logic.binary add %old, %one : !simulation.logic<32>
      simulation.nba.enqueue %next to %count : (!simulation.logic<32>, !simulation.ref<!simulation.logic<32>>) -> ()
      cf.br ^wait
    }
    simulation.func @independent(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clk: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^step {site = #schedule.continuation<id = 4>} : !simulation.ref<!simulation.logic<1>>
    ^step:
      %count = simulation.context.storage %ctx[4] : !simulation.ref<!simulation.logic<32>>
      %old = simulation.ref.load %count : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      %one = simulation.logic.constant 1 : i32, 0 : i32 : !simulation.logic<32>
      %next = simulation.logic.binary add %old, %one : !simulation.logic<32>
      simulation.nba.enqueue %next to %count : (!simulation.logic<32>, !simulation.ref<!simulation.logic<32>>) -> ()
      cf.br ^wait
    }
    simulation.func @relay1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %output: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^step
    ^wait:
      simulation.suspend.change %input to ^step {site = #schedule.continuation<id = 2>} : !simulation.ref<!simulation.logic<32>>
    ^step:
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      simulation.ref.store %value to %output : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      cf.br ^wait
    }
    simulation.func @relay2(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64},
        %output: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 4 : i64} {
      cf.br ^step
    ^wait:
      simulation.suspend.change %input to ^step {site = #schedule.continuation<id = 3>} : !simulation.ref<!simulation.logic<32>>
    ^step:
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      simulation.ref.store %value to %output : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      cf.br ^wait
    }
  }
}

