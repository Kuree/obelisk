// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-native-aot-analysis)' \
// RUN:   2>&1 | FileCheck %s --check-prefix=AOT
// RUN: obelisk-opt %s -o - \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep))' \
// RUN:   | FileCheck %s --check-prefix=SUPERSTEP

// A compiler-owned covergroup clock-event registration is a runtime-owned
// cold observer. It must not make an otherwise periodic static island
// ineligible, and it must not acquire a generated actor slot.
// AOT: native-aot eligible=true fully=false{{.*}}periodic=true{{.*}}forced_hybrid=true
// AOT-NEXT: actor 0 @root
// AOT-NEXT: actor 1 @clock
// AOT-NEXT: actor 2 @initialize
// AOT-NOT: actor {{[0-9]+}} @sampler
// AOT: reason dynamic spawn multiplicity
// AOT: reason task, await, or join control is present

// Static-superstep planning uses the same structural certificate. The nested
// registration spawn stays runtime-owned while its root-spawned initializer
// and periodic clock retain exact generated identities.
// SUPERSTEP: schedule.static_superstep
// SUPERSTEP-SAME: actors = [@root, @clock, @initialize]

module {
  obelisk_sim.design @covergroup_clocking_sampler {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.covergroup.decl @cg schema 1
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "clock"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "initialize"
    obelisk_sim.code_unit.decl 4 in 0 observer hierarchy "evaluate"
    obelisk_sim.code_unit.decl 5 in 0 fork hierarchy "sampler"
    obelisk_sim.code_unit.decl 6 in 0 observer hierarchy "primary"

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock_ref = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<i1>
      %clock_process = obelisk_sim.spawn @clock(%ctx, %clock_ref) :
          !obelisk_sim.context, !obelisk_sim.ref<i1> -> !obelisk_sim.process
      %initializer = obelisk_sim.spawn @initialize(%ctx, %clock_ref) :
          !obelisk_sim.context, !obelisk_sim.ref<i1> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @clock(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock_ref: !obelisk_sim.ref<i1>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^toggle
    ^toggle:
      %old = obelisk_sim.ref.load %clock_ref : !obelisk_sim.ref<i1> -> i1
      %true = arith.constant true
      %new = arith.xori %old, %true : i1
      obelisk_sim.ref.store %new to %clock_ref : i1, !obelisk_sim.ref<i1>
      cf.br ^wait
    }

    obelisk_sim.func private @initialize(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock_ref: !obelisk_sim.ref<i1>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %handle = obelisk_sim.covergroup.null :
          !obelisk_sim.covergroup_handle<@cg>
      %process = obelisk_sim.spawn @sampler(%ctx, %handle) :
          !obelisk_sim.context, !obelisk_sim.covergroup_handle<@cg> ->
          !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @evaluate(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@cg>
            {obelisk_sim.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 4 : i64,
                    obelisk_sim.covergroup_event_sample_evaluator,
                    obelisk_sim.covergroup_strobe_sample_evaluator} {
      %true = arith.constant true
      obelisk_sim.return %true : i1
    }

    obelisk_sim.func private @primary(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock_ref: !obelisk_sim.ref<i1>
            {obelisk_sim.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 6 : i64} {
      %value = obelisk_sim.ref.load %clock_ref : !obelisk_sim.ref<i1> -> i1
      obelisk_sim.return %value : i1
    }

    obelisk_sim.func private @sampler(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@cg>
            {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 5 : i64,
                    domain = 0 : i32, home_region = 2 : i32, internal,
                    schedule.covergroup_clocking_sampler,
                    schedule.detached_controls,
                    schedule.prime_on_spawn} {
      %clock_ref = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<i1>
      %initial = obelisk_sim.ref.load %clock_ref :
          !obelisk_sim.ref<i1> -> i1
      %primary = obelisk_sim.observer.bind @primary
          values(%clock_ref, %clock_ref : !obelisk_sim.ref<i1>,
                 !obelisk_sim.ref<i1>) captures 1 :
          !obelisk_sim.observer<i1>
      %observer = obelisk_sim.observer.bind @evaluate
          values(%handle : !obelisk_sim.covergroup_handle<@cg>) captures 1 :
          !obelisk_sim.observer<i1>
      obelisk_sim.covergroup.clock_event.register %ctx, %handle
          events [%primary, %initial, %observer] conditions 0 edges [1]
          indices [-1]
          {strobe = true} : !obelisk_sim.covergroup_handle<@cg>,
          !obelisk_sim.observer<i1>, i1, !obelisk_sim.observer<i1>
      obelisk_sim.suspend.forever to ^parked
    ^parked:
      obelisk_sim.return
    }
  }

}
