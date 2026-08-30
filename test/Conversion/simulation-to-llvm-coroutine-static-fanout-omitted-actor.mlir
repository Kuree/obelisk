// RUN: %split-file %s %t
// RUN: obelisk-opt %t/input.mlir \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-plan-static-superstep))' \
// RUN:   > %t/planned.mlir
// RUN: obelisk-opt %t/planned.mlir \
// RUN:   --pass-pipeline='builtin.module(convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s --check-prefix=EXACT
// RUN: sed -f %t/noncertified.sed %t/planned.mlir > %t/noncertified.mlir
// RUN: obelisk-opt %t/noncertified.mlir -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(test-obelisk-native-aot-analysis)' \
// RUN:   2>&1 | FileCheck %s --check-prefix=ACTORS
// RUN: obelisk-opt %t/noncertified.mlir \
// RUN:   --pass-pipeline='builtin.module(convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s --check-prefix=FALLBACK

// EXACT: llvm.mlir.global internal constant @__obelisk_aot_static_fanout_v1
// ACTORS: native-aot eligible=true fully=false{{.*}}forced_hybrid=true
// ACTORS-NEXT: actor 0 @root
// ACTORS-NEXT: actor 1 @ordinary
// ACTORS-NEXT: actor 2 @clock
// ACTORS-NEXT: bytecode @coordinator bb1
// FALLBACK-LABEL: module attributes
// FALLBACK-NOT: @__obelisk_aot_static_fanout_v1
// FALLBACK: llvm.func @obelisk_rt_v1_scheduler_signal_transition

// The planner first certifies the exact Clause 31 clock coordinator as the
// sole runtime-owned omission. The sed variant preserves that actor inventory
// but changes the omitted function to a different cold callback and makes its
// spawn dynamic. NativeAOT therefore has no actor slot for it, but static
// fanout must not silently drop its Watch dependency.

//--- input.mlir
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 3 : i32
} {
  obelisk_sim.design @mixed attributes {
    compute_graph = #obelisk_sim.graph<
      version = 1, vpi = off, workers = 1,
      nodes = [
        #obelisk_sim.fragment<id = 0, function = @root, block = 0,
          region = active, action = terminate, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #obelisk_sim.fragment<id = 1, function = @coordinator, block = 0,
          region = observed, action = continue, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #obelisk_sim.fragment<id = 2, function = @coordinator, block = 1,
          region = observed, action = suspend_any, tier = native, cost = 1,
          lane = 0, twoState = true, effects = [
            #obelisk_sim.effect<effect = watch, resource = storage,
              target = descriptor, descriptor = 0, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false,
              trigger = posedge>]>,
        #obelisk_sim.fragment<id = 3, function = @ordinary, block = 1,
          region = observed, action = suspend_change, tier = native, cost = 1,
          lane = 0, twoState = true, effects = [
            #obelisk_sim.effect<effect = watch, resource = storage,
              target = descriptor, descriptor = 0, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false,
              trigger = change>]>,
        #obelisk_sim.fragment<id = 4, function = @clock, block = 0,
          region = active, action = continue, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #obelisk_sim.fragment<id = 5, function = @clock, block = 1,
          region = active, action = suspend_delay, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #obelisk_sim.fragment<id = 6, function = @clock, block = 2,
          region = active, action = continue, tier = native, cost = 1,
          lane = 0, twoState = true, effects = [
            #obelisk_sim.effect<effect = read, resource = storage,
              target = descriptor, descriptor = 0, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false, trigger = none>,
            #obelisk_sim.effect<effect = write, resource = storage,
              target = descriptor, descriptor = 0, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false, trigger = none>]>,
        #obelisk_sim.fragment<id = 7, function = @ordinary, block = 0,
          region = observed, action = continue, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>
      ],
      edges = [
        #obelisk_sim.edge<source = 0, target = 1, kind = spawn>,
        #obelisk_sim.edge<source = 0, target = 7, kind = spawn>,
        #obelisk_sim.edge<source = 0, target = 4, kind = spawn>,
        #obelisk_sim.edge<source = 1, target = 2, kind = process_order>,
        #obelisk_sim.edge<source = 2, target = 3, kind = process_order>,
        #obelisk_sim.edge<source = 3, target = 2, kind = process_order>,
        #obelisk_sim.edge<source = 4, target = 5, kind = process_order>,
        #obelisk_sim.edge<source = 5, target = 6, kind = resume>,
        #obelisk_sim.edge<source = 6, target = 5, kind = process_order>,
        #obelisk_sim.edge<source = 6, target = 2, kind = sensitivity,
          resource = <effect = watch, resource = storage,
            target = descriptor, descriptor = 0, formal = 0, low = 0,
            width = 1, dynamic = false, deferred = false,
            trigger = posedge>>,
        #obelisk_sim.edge<source = 7, target = 3, kind = process_order>
      ],
      regions = [
        #obelisk_sim.region<kind = active, groups = [
          #obelisk_sim.group<fragments = [0], schedule = acyclic,
            feedback = []>,
          #obelisk_sim.group<fragments = [4], schedule = acyclic,
            feedback = []>,
          #obelisk_sim.group<fragments = [5], schedule = acyclic,
            feedback = []>,
          #obelisk_sim.group<fragments = [6], schedule = acyclic,
            feedback = []>]>,
        #obelisk_sim.region<kind = nba, groups = []>,
        #obelisk_sim.region<kind = observed, groups = [
          #obelisk_sim.group<fragments = [1], schedule = acyclic,
            feedback = []>,
          #obelisk_sim.group<fragments = [7], schedule = acyclic,
            feedback = []>,
          #obelisk_sim.group<fragments = [2, 3], schedule = control_loop,
            feedback = []>]>,
        #obelisk_sim.region<kind = reactive, groups = []>,
        #obelisk_sim.region<kind = postponed, groups = []>]>
  } {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "coordinator"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "ordinary"
    obelisk_sim.code_unit.decl 4 in 0 always hierarchy "clock"

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %coordinator = obelisk_sim.spawn @coordinator(%ctx, %clock) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>> ->
          !obelisk_sim.process
      %ordinary = obelisk_sim.spawn @ordinary(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      %periodic = obelisk_sim.spawn @clock(%ctx, %clock) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>> ->
          !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @coordinator(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64,
                    domain = 0 : i32, home_region = 8 : i32,
                    obelisk_sim.timing_check_coordinator} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.clock_set %clock conditions 0 edges [1]
          indices [-1] site 23 to ^wait :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
    }

    obelisk_sim.func @ordinary(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64,
                    domain = 0 : i32, home_region = 8 : i32} {
      %clock = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %clock to ^wait
          {site = #obelisk_sim.continuation<id = 2>} :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
    }

    obelisk_sim.func @clock(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64,
                    domain = 0 : i32, home_region = 2 : i32} {
      cf.br ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^toggle
          {site = #obelisk_sim.continuation<id = 1>,
           timing = #obelisk_sim.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = obelisk_sim.ref.load %clock :
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %new = obelisk_sim.logic.unary bit_not %old :
          (!obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %new to %clock : !obelisk_sim.logic<1>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      cf.br ^wait
    }
  }
}

//--- noncertified.sed
s/code_unit.decl 2 in 0 always/code_unit.decl 2 in 0 final/
s/entry_kind = 3 : i32, home_region = 8 : i32, obelisk_sim.timing_check_coordinator/entry_kind = 2 : i32, home_region = 2 : i32, internal, obelisk_sim.multiclock_sequence_eos_coordinator, obelisk_sim.concurrent_eos_coordinator, obelisk_sim.concurrent_eos_counted, obelisk_sim.detached_controls/
s/obelisk_sim.suspend.clock_set %arg1 conditions 0 edges \[1\] indices \[-1\] site 23 to \^bb1 : !obelisk_sim.ref<!obelisk_sim.logic<1>>/obelisk_sim.return/
/obelisk_sim.func @ordinary/,/^    }/ s|      cf.br \^bb1|      %spawn = obelisk_sim.spawn @coordinator(%arg0, %0) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process\n      cf.br ^bb1|
