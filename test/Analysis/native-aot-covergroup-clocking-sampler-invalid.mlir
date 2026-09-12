// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-native-aot-analysis)' \
// RUN:   2>&1 | FileCheck %s

// A compiler marker is not a trust boundary. An actor that mutates state while
// registering must not acquire the runtime-owned cold exemption or a generated
// actor slot. Pure construction-time evaluation remains legal.
// CHECK: native-aot eligible=true fully=false{{.*}}forced_hybrid=false
// CHECK-NOT: actor {{[0-9]+}} @malformed_sampler
// CHECK: reason task, await, or join control is present

module {
  obelisk_sim.design @malformed_covergroup_clocking_sampler {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.covergroup.decl @cg schema 1
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "clock"
    obelisk_sim.code_unit.decl 3 in 0 observer hierarchy "evaluate"
    obelisk_sim.code_unit.decl 4 in 0 fork hierarchy "malformed_sampler"
    obelisk_sim.code_unit.decl 5 in 0 observer hierarchy "primary"

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock_ref = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<i1>
      %handle = obelisk_sim.covergroup.null :
          !obelisk_sim.covergroup_handle<@cg>
      %clock_process = obelisk_sim.spawn @clock(%ctx, %clock_ref) :
          !obelisk_sim.context, !obelisk_sim.ref<i1> -> !obelisk_sim.process
      %sampler_process = obelisk_sim.spawn @malformed_sampler(
          %ctx, %handle, %clock_ref) : !obelisk_sim.context,
          !obelisk_sim.covergroup_handle<@cg>, !obelisk_sim.ref<i1> ->
          !obelisk_sim.process
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

    obelisk_sim.func private @evaluate(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@cg>
            {obelisk_sim.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 3 : i64,
                    obelisk_sim.covergroup_event_sample_evaluator,
                    obelisk_sim.covergroup_strobe_sample_evaluator} {
      %true = arith.constant true
      obelisk_sim.return %true : i1
    }

    obelisk_sim.func private @primary(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock_ref: !obelisk_sim.ref<i1>
            {obelisk_sim.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 5 : i64} {
      %value = obelisk_sim.ref.load %clock_ref : !obelisk_sim.ref<i1> -> i1
      obelisk_sim.return %value : i1
    }

    obelisk_sim.func private @malformed_sampler(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@cg>
            {obelisk_sim.capture_kind = 2 : i32},
        %clock_ref: !obelisk_sim.ref<i1>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 13 : i32, code_unit_id = 4 : i64,
                    domain = 0 : i32, home_region = 2 : i32, internal,
                    obelisk_sim.covergroup_clocking_sampler,
                    obelisk_sim.detached_controls,
                    obelisk_sim.prime_on_spawn} {
      %initial = obelisk_sim.ref.load %clock_ref :
          !obelisk_sim.ref<i1> -> i1
      %primary = obelisk_sim.observer.bind @primary
          values(%clock_ref, %clock_ref : !obelisk_sim.ref<i1>,
                 !obelisk_sim.ref<i1>) captures 1 :
          !obelisk_sim.observer<i1>
      %observer = obelisk_sim.observer.bind @evaluate
          values(%handle : !obelisk_sim.covergroup_handle<@cg>) captures 1 :
          !obelisk_sim.observer<i1>
      %unrelated = arith.constant false
      obelisk_sim.ref.store %unrelated to %clock_ref :
          i1, !obelisk_sim.ref<i1>
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
