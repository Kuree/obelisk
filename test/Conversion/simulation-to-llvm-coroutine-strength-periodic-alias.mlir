// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s --check-prefix=ALIAS
// RUN: sed -e 's/strength1 = 6/strength1 = 0/' \
// RUN:   -e 's/schedule.native_scheduler = 3/schedule.native_scheduler = 0/' %s \
// RUN:   | obelisk-opt - --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s --check-prefix=HIGHZ

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  simulation.design @strength_periodic_alias {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "strength_periodic_alias.root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "strength_periodic_alias.initialize"
    simulation.code_unit.decl 3 in 0 always hierarchy "strength_periodic_alias.watcher"
    simulation.code_unit.decl 4 in 0 always hierarchy "strength_periodic_alias.clock"
    simulation.code_unit.decl 5 in 0 always hierarchy "strength_periodic_alias.forward"
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design
        {strength1 = 6 : i32}

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %data = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<8>>
      %clock = simulation.context.storage %ctx[1] :
          !simulation.ref<!simulation.logic<1>>
      %driver = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<1>>
      %initialize = simulation.spawn @initialize(%ctx, %data) :
          !simulation.context, !simulation.ref<!simulation.logic<8>>
          -> !simulation.process
      %watcher = simulation.spawn @watcher(%ctx, %data) :
          !simulation.context, !simulation.ref<!simulation.logic<8>>
          -> !simulation.process
      %clock_process = simulation.spawn @clock(%ctx, %clock) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>
          -> !simulation.process
      %forward_process = simulation.spawn @forward(%ctx, %clock, %driver) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>,
          !simulation.driver<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }

    simulation.func @initialize(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %destination: !simulation.ref<!simulation.logic<8>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %value = simulation.logic.constant 42 : i8, 0 : i8 :
          !simulation.logic<8>
      simulation.nba.enqueue %value to %destination :
          (!simulation.logic<8>,
           !simulation.ref<!simulation.logic<8>>) -> ()
      simulation.return
    }

    simulation.func @watcher(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !simulation.ref<!simulation.logic<8>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %source to ^resume
          {site = #schedule.continuation<id = 1>} :
          !simulation.ref<!simulation.logic<8>>
    ^resume:
      cf.br ^wait
    }

    simulation.func @clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 2
      simulation.suspend.delay %delay to ^toggle
          {site = #schedule.continuation<id = 2>,
           timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = simulation.ref.load %clock :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %new = simulation.logic.unary bit_not %old :
          (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %new to %clock : !simulation.logic<1>,
          !simulation.ref<!simulation.logic<1>>
      cf.br ^wait
    }

    simulation.func @forward(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 1 : i64},
        %driver: !simulation.driver<!simulation.logic<1>>
            {simulation.capture_kind = 5 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64} {
      cf.br ^wait
    ^wait:
      %value = simulation.ref.load %source :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.driver.drive %driver = %value :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>
      simulation.suspend.change %source to ^wait
          {site = #schedule.continuation<id = 3>} :
          !simulation.ref<!simulation.logic<1>>
    }
  }
}

// Ordinary strengths preserve the forwarded four-state bit exactly.
// ALIAS: llvm.mlir.global internal constant @__obelisk_periodic_alias_plan_v1

// highz1 maps a source 1 to Z, so copying the source planes is unsound.
// HIGHZ-NOT: @__obelisk_periodic_alias_plan_v1
// HIGHZ: llvm.mlir.global internal constant @__obelisk_periodic_clock_plan_v1
// HIGHZ-NOT: @__obelisk_periodic_alias_plan_v1
