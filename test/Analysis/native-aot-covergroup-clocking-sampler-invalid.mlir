// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-native-aot-analysis)' \
// RUN:   2>&1 | FileCheck %s

// A compiler marker is not a trust boundary. An actor that mutates state while
// registering must not acquire the runtime-owned cold exemption or a generated
// actor slot. Pure construction-time evaluation remains legal.
// CHECK: native-aot eligible=true fully=false{{.*}}forced_hybrid=false
// CHECK-NOT: actor {{[0-9]+}} @malformed_sampler
// CHECK: reason task, await, or join control is present

module {
  simulation.design @malformed_covergroup_clocking_sampler {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.covergroup.decl @cg schema 1
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.code_unit.decl 3 in 0 observer hierarchy "evaluate"
    simulation.code_unit.decl 4 in 0 fork hierarchy "malformed_sampler"
    simulation.code_unit.decl 5 in 0 observer hierarchy "primary"

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock_ref = simulation.context.storage %ctx[0] :
          !simulation.ref<i1>
      %handle = simulation.covergroup.null :
          !simulation.covergroup_handle<@cg>
      %clock_process = simulation.spawn @clock(%ctx, %clock_ref) :
          !simulation.context, !simulation.ref<i1> -> !simulation.process
      %sampler_process = simulation.spawn @malformed_sampler(
          %ctx, %handle, %clock_ref) : !simulation.context,
          !simulation.covergroup_handle<@cg>, !simulation.ref<i1> ->
          !simulation.process
      simulation.return
    }

    simulation.func private @clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock_ref: !simulation.ref<i1>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^toggle
    ^toggle:
      %old = simulation.ref.load %clock_ref : !simulation.ref<i1> -> i1
      %true = arith.constant true
      %new = arith.xori %old, %true : i1
      simulation.ref.store %new to %clock_ref : i1, !simulation.ref<i1>
      cf.br ^wait
    }

    simulation.func private @evaluate(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %handle: !simulation.covergroup_handle<@cg>
            {simulation.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 3 : i64,
                    simulation.covergroup_event_sample_evaluator,
                    simulation.covergroup_strobe_sample_evaluator} {
      %true = arith.constant true
      simulation.return %true : i1
    }

    simulation.func private @primary(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock_ref: !simulation.ref<i1>
            {simulation.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 5 : i64} {
      %value = simulation.ref.load %clock_ref : !simulation.ref<i1> -> i1
      simulation.return %value : i1
    }

    simulation.func private @malformed_sampler(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %handle: !simulation.covergroup_handle<@cg>
            {simulation.capture_kind = 2 : i32},
        %clock_ref: !simulation.ref<i1>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 13 : i32, code_unit_id = 4 : i64,
                    domain = 0 : i32, home_region = 2 : i32, internal,
                    schedule.covergroup_clocking_sampler,
                    schedule.detached_controls,
                    schedule.prime_on_spawn} {
      %initial = simulation.ref.load %clock_ref :
          !simulation.ref<i1> -> i1
      %primary = simulation.observer.bind @primary
          values(%clock_ref, %clock_ref : !simulation.ref<i1>,
                 !simulation.ref<i1>) captures 1 :
          !simulation.observer<i1>
      %observer = simulation.observer.bind @evaluate
          values(%handle : !simulation.covergroup_handle<@cg>) captures 1 :
          !simulation.observer<i1>
      %unrelated = arith.constant false
      simulation.ref.store %unrelated to %clock_ref :
          i1, !simulation.ref<i1>
      simulation.covergroup.clock_event.register %ctx, %handle
          events [%primary, %initial, %observer] conditions 0 edges [1]
          indices [-1]
          {strobe = true} : !simulation.covergroup_handle<@cg>,
          !simulation.observer<i1>, i1, !simulation.observer<i1>
      simulation.suspend.forever to ^parked
    ^parked:
      simulation.return
    }
  }
}
