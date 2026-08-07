// RUN: not obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   2>&1 | FileCheck %s --check-prefix=ERROR

// A path-sensitive promotion probe must not make a runtime checkpoint leaf
// part of the generated call closure. Until that leaf has an explicit Tier-3
// return route, forced eval rejects the model instead of hiding a runtime call
// behind the cold side of an indirect dispatcher.
//
// `guarded_blocking` publishes with a blocking store on its fast path, which
// the path predicate cannot model, so the owner is declined and keeps the
// canonical four-state body that calls the runtime inline. That is diagnosed
// at the scheduler decision, before the owner can enter a generated closure.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 3 : i32
} {
  obelisk_sim.design @path_promotion {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "path.root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "path.clock"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "path.guarded_nba"
    obelisk_sim.code_unit.decl 4 in 0 always hierarchy "path.guarded_blocking"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 3 in 0 : !obelisk_sim.logic<1> design

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %source = obelisk_sim.context.storage %ctx[1] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %nba_destination = obelisk_sim.context.storage %ctx[2] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %blocking_destination = obelisk_sim.context.storage %ctx[3] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %clock_process = obelisk_sim.spawn @clock(%ctx, %clock) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
          -> !obelisk_sim.process
      %nba_process = obelisk_sim.spawn @guarded_nba(
          %ctx, %clock, %source, %nba_destination) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %blocking_process = obelisk_sim.spawn @guarded_blocking(
          %ctx, %source, %source, %blocking_destination) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func @clock(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
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

    obelisk_sim.func @guarded_nba(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %source: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64},
        %destination: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 2 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %clock to ^resume
          {site = #obelisk_sim.continuation<id = 2>} :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^resume:
      %value = obelisk_sim.ref.load %source :
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %zero = obelisk_sim.logic.constant 0 : i1, 0 : i1 :
          !obelisk_sim.logic<1>
      %take_fast_path = obelisk_sim.logic.compare case_eq %value, %zero :
          (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
      cf.cond_br %take_fast_path, ^publish, ^checkpoint
    ^publish:
      obelisk_sim.nba.enqueue %value to %destination :
          (!obelisk_sim.logic<1>,
           !obelisk_sim.ref<!obelisk_sim.logic<1>>) -> ()
      cf.br ^wait
    ^checkpoint:
      %stdout = arith.constant -2147483647 : i32
      %message = obelisk_sim.bytes.constant "nba checkpoint"
      obelisk_sim.display %ctx to %stdout(%message) newline = true radix = 10
          flags = [0] : !obelisk_sim.bytes
      cf.br ^wait
    }

    obelisk_sim.func @guarded_blocking(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64},
        %source: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64},
        %destination: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 3 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %clock to ^resume
          {site = #obelisk_sim.continuation<id = 3>} :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^resume:
      %value = obelisk_sim.ref.load %source :
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %zero = obelisk_sim.logic.constant 0 : i1, 0 : i1 :
          !obelisk_sim.logic<1>
      %take_fast_path = obelisk_sim.logic.compare case_eq %value, %zero :
          (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
      cf.cond_br %take_fast_path, ^publish, ^checkpoint
    ^publish:
      obelisk_sim.ref.store %value to %destination :
          !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      cf.br ^wait
    ^checkpoint:
      %stdout = arith.constant -2147483647 : i32
      %message = obelisk_sim.bytes.constant "blocking checkpoint"
      obelisk_sim.display %ctx to %stdout(%message) newline = true radix = 10
          flags = [0] : !obelisk_sim.bytes
      cf.br ^wait
    }
  }
}

// ERROR: an eval owner keeps an unguarded runtime leaf
// ERROR-SAME: in guarded_blocking
