// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(talos-sim-thread-process-cfg),talos-sim-build-compute-graph{vpi=full},talos-sim-verify-compute-graph,talos-sim-materialize-graph-regions,talos-sim-materialize-compute-fusion,talos-sim-specialize-static-state-nba,talos-sim-plan-static-superstep),encode-talos-sim-to-bytecode{vpi=full},convert-talos-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir

// Twelve copy actors cross native size-budget splits. Writable VPI retains
// their fine ingress: dirty owners use the shared Tier 2/3 actor selector,
// while an unaffected owner still calls its original Tier 1 executor.
// PLAN-NOT: llvm.func @__talos_eval_ranked_group_
// PLAN-LABEL: llvm.func @__talos_eval_write_guard_0_v1(
// PLAN: llvm.call @talos_rt_v1_static_specialization_guard
// PLAN-LABEL: llvm.func @__talos_eval_write_guard_12_v1(
// PLAN: llvm.call @talos_rt_v1_static_specialization_guard
// PLAN-LABEL: llvm.func @__talos_eval_dispatch_v1(
// PLAN: llvm.call @__talos_eval_write_guard_0_v1(
// PLAN: llvm.call @__talos_eval_write_guard_12_v1(
// PLAN: llvm.call @talos_rt_v1_scheduler_execute_aot_actor(

module attributes {schedule.native.max_inline_ops = 700 : i64,
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
    simulation.storage.decl 3 in 0 : !simulation.logic<32> design
    simulation.code_unit.decl 5 in 0 always_comb hierarchy "external_clock.relay3"
    simulation.storage.decl 4 in 0 : !simulation.logic<32> design
    simulation.code_unit.decl 6 in 0 always_comb hierarchy "external_clock.relay4"
    simulation.storage.decl 5 in 0 : !simulation.logic<32> design
    simulation.code_unit.decl 7 in 0 always_comb hierarchy "external_clock.relay5"
    simulation.storage.decl 6 in 0 : !simulation.logic<32> design
    simulation.code_unit.decl 8 in 0 always_comb hierarchy "external_clock.relay6"
    simulation.storage.decl 7 in 0 : !simulation.logic<32> design
    simulation.code_unit.decl 9 in 0 always_comb hierarchy "external_clock.relay7"
    simulation.storage.decl 8 in 0 : !simulation.logic<32> design
    simulation.code_unit.decl 10 in 0 always_comb hierarchy "external_clock.relay8"
    simulation.storage.decl 9 in 0 : !simulation.logic<32> design
    simulation.code_unit.decl 11 in 0 always_comb hierarchy "external_clock.relay9"
    simulation.storage.decl 10 in 0 : !simulation.logic<32> design
    simulation.code_unit.decl 12 in 0 always_comb hierarchy "external_clock.relay10"
    simulation.storage.decl 11 in 0 : !simulation.logic<32> design
    simulation.code_unit.decl 13 in 0 always_comb hierarchy "external_clock.relay11"
    simulation.storage.decl 12 in 0 : !simulation.logic<32> design
    simulation.code_unit.decl 14 in 0 always_comb hierarchy "external_clock.relay12"
    simulation.storage.decl 13 in 0 : !simulation.logic<32> design hierarchy "external_clock.count"
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
      %relay_value_3 = simulation.context.storage %ctx[4] : !simulation.ref<!simulation.logic<32>>
      %relay3 = simulation.spawn @relay3(%ctx, %visible, %relay_value_3) : !simulation.context, !simulation.ref<!simulation.logic<32>>, !simulation.ref<!simulation.logic<32>> -> !simulation.process
      %relay_value_4 = simulation.context.storage %ctx[5] : !simulation.ref<!simulation.logic<32>>
      %relay4 = simulation.spawn @relay4(%ctx, %relay_value_3, %relay_value_4) : !simulation.context, !simulation.ref<!simulation.logic<32>>, !simulation.ref<!simulation.logic<32>> -> !simulation.process
      %relay_value_5 = simulation.context.storage %ctx[6] : !simulation.ref<!simulation.logic<32>>
      %relay5 = simulation.spawn @relay5(%ctx, %relay_value_4, %relay_value_5) : !simulation.context, !simulation.ref<!simulation.logic<32>>, !simulation.ref<!simulation.logic<32>> -> !simulation.process
      %relay_value_6 = simulation.context.storage %ctx[7] : !simulation.ref<!simulation.logic<32>>
      %relay6 = simulation.spawn @relay6(%ctx, %relay_value_5, %relay_value_6) : !simulation.context, !simulation.ref<!simulation.logic<32>>, !simulation.ref<!simulation.logic<32>> -> !simulation.process
      %relay_value_7 = simulation.context.storage %ctx[8] : !simulation.ref<!simulation.logic<32>>
      %relay7 = simulation.spawn @relay7(%ctx, %relay_value_6, %relay_value_7) : !simulation.context, !simulation.ref<!simulation.logic<32>>, !simulation.ref<!simulation.logic<32>> -> !simulation.process
      %relay_value_8 = simulation.context.storage %ctx[9] : !simulation.ref<!simulation.logic<32>>
      %relay8 = simulation.spawn @relay8(%ctx, %relay_value_7, %relay_value_8) : !simulation.context, !simulation.ref<!simulation.logic<32>>, !simulation.ref<!simulation.logic<32>> -> !simulation.process
      %relay_value_9 = simulation.context.storage %ctx[10] : !simulation.ref<!simulation.logic<32>>
      %relay9 = simulation.spawn @relay9(%ctx, %relay_value_8, %relay_value_9) : !simulation.context, !simulation.ref<!simulation.logic<32>>, !simulation.ref<!simulation.logic<32>> -> !simulation.process
      %relay_value_10 = simulation.context.storage %ctx[11] : !simulation.ref<!simulation.logic<32>>
      %relay10 = simulation.spawn @relay10(%ctx, %relay_value_9, %relay_value_10) : !simulation.context, !simulation.ref<!simulation.logic<32>>, !simulation.ref<!simulation.logic<32>> -> !simulation.process
      %relay_value_11 = simulation.context.storage %ctx[12] : !simulation.ref<!simulation.logic<32>>
      %relay11 = simulation.spawn @relay11(%ctx, %relay_value_10, %relay_value_11) : !simulation.context, !simulation.ref<!simulation.logic<32>>, !simulation.ref<!simulation.logic<32>> -> !simulation.process
      %relay_value_12 = simulation.context.storage %ctx[13] : !simulation.ref<!simulation.logic<32>>
      %relay12 = simulation.spawn @relay12(%ctx, %relay_value_11, %relay_value_12) : !simulation.context, !simulation.ref<!simulation.logic<32>>, !simulation.ref<!simulation.logic<32>> -> !simulation.process
      %process = simulation.spawn @counter(%ctx, %clk) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
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
    simulation.func @relay3(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64},
        %output: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 5 : i64} {
      cf.br ^step
    ^wait:
      simulation.suspend.change %input to ^step {site = #schedule.continuation<id = 4>} : !simulation.ref<!simulation.logic<32>>
    ^step:
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      simulation.ref.store %value to %output : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      cf.br ^wait
    }
    simulation.func @relay4(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64},
        %output: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 5 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 6 : i64} {
      cf.br ^step
    ^wait:
      simulation.suspend.change %input to ^step {site = #schedule.continuation<id = 5>} : !simulation.ref<!simulation.logic<32>>
    ^step:
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      simulation.ref.store %value to %output : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      cf.br ^wait
    }
    simulation.func @relay5(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 5 : i64},
        %output: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 7 : i64} {
      cf.br ^step
    ^wait:
      simulation.suspend.change %input to ^step {site = #schedule.continuation<id = 6>} : !simulation.ref<!simulation.logic<32>>
    ^step:
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      simulation.ref.store %value to %output : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      cf.br ^wait
    }
    simulation.func @relay6(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64},
        %output: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 7 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 8 : i64} {
      cf.br ^step
    ^wait:
      simulation.suspend.change %input to ^step {site = #schedule.continuation<id = 7>} : !simulation.ref<!simulation.logic<32>>
    ^step:
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      simulation.ref.store %value to %output : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      cf.br ^wait
    }
    simulation.func @relay7(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 7 : i64},
        %output: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 8 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 9 : i64} {
      cf.br ^step
    ^wait:
      simulation.suspend.change %input to ^step {site = #schedule.continuation<id = 8>} : !simulation.ref<!simulation.logic<32>>
    ^step:
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      simulation.ref.store %value to %output : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      cf.br ^wait
    }
    simulation.func @relay8(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 8 : i64},
        %output: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 9 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 10 : i64} {
      cf.br ^step
    ^wait:
      simulation.suspend.change %input to ^step {site = #schedule.continuation<id = 9>} : !simulation.ref<!simulation.logic<32>>
    ^step:
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      simulation.ref.store %value to %output : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      cf.br ^wait
    }
    simulation.func @relay9(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 9 : i64},
        %output: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 10 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 11 : i64} {
      cf.br ^step
    ^wait:
      simulation.suspend.change %input to ^step {site = #schedule.continuation<id = 10>} : !simulation.ref<!simulation.logic<32>>
    ^step:
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      simulation.ref.store %value to %output : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      cf.br ^wait
    }
    simulation.func @relay10(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 10 : i64},
        %output: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 11 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 12 : i64} {
      cf.br ^step
    ^wait:
      simulation.suspend.change %input to ^step {site = #schedule.continuation<id = 11>} : !simulation.ref<!simulation.logic<32>>
    ^step:
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      simulation.ref.store %value to %output : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      cf.br ^wait
    }
    simulation.func @relay11(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 11 : i64},
        %output: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 12 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 13 : i64} {
      cf.br ^step
    ^wait:
      simulation.suspend.change %input to ^step {site = #schedule.continuation<id = 12>} : !simulation.ref<!simulation.logic<32>>
    ^step:
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      simulation.ref.store %value to %output : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      cf.br ^wait
    }
    simulation.func @relay12(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 12 : i64},
        %output: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 13 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 14 : i64} {
      cf.br ^step
    ^wait:
      simulation.suspend.change %input to ^step {site = #schedule.continuation<id = 13>} : !simulation.ref<!simulation.logic<32>>
    ^step:
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      simulation.ref.store %value to %output : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      cf.br ^wait
    }

  }
}
