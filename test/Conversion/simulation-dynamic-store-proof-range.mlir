// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=DYNAMIC

// Dynamic writes are clipped to their physical root (LRM 11.5.1), even for
// negative, partially out-of-range and invalid offsets. A root outside all
// certificates needs no promotion publication; a possibly overlapping root
// must retain it. The unaligned 129-bit root shares bytes with its neighbors.
// The cold publication helper and its calls use a register-preserving
// convention, so a caller keeps no frame for it on the hot path.
// DYNAMIC: llvm.func preserve_mostcc @__obelisk_eval_promotion_publish_unknown_v1(
// DYNAMIC-LABEL: llvm.func @write_disjoint(
// DYNAMIC-NOT: llvm.call preserve_mostcc @__obelisk_eval_promotion_publish_unknown_v1
// DYNAMIC: llvm.mlir.addressof @__obelisk_state_unknown
// DYNAMIC-NOT: llvm.call preserve_mostcc @__obelisk_eval_promotion_publish_unknown_v1
// DYNAMIC-LABEL: llvm.func @write_overlapping(
// DYNAMIC: llvm.call preserve_mostcc @__obelisk_eval_promotion_publish_unknown_v1
// DYNAMIC: llvm.func

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  simulation.design @dynamic_store_proof_range {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !simulation.logic<160> design
    simulation.storage.decl 2 in 0 : !simulation.logic<129> design
    simulation.storage.decl 3 in 0 : !simulation.logic<64> design
    simulation.storage.decl 4 in 0 : !simulation.logic<1> design
    simulation.storage.decl 5 in 0 : !simulation.logic<16> design
    simulation.storage.decl 6 in 0 : !simulation.logic<16> design
    simulation.storage.decl 7 in 0 : !simulation.logic<8> design
    simulation.storage.decl 8 in 0 : !simulation.logic<8> design
    simulation.storage.decl 9 in 0 : !simulation.logic<129> design
    simulation.code_unit.decl 6 in 0 function hierarchy "write_disjoint"
    simulation.code_unit.decl 7 in 0 function hierarchy "write_overlapping"
    simulation.code_unit.decl 4 in 0 always hierarchy "other_clock"
    simulation.code_unit.decl 5 in 0 always hierarchy "other_work"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "work"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      // Keep all roots visible while constructing the promotion certificates.
      %levels = arith.constant 0 : i64
      %scope = simulation.bytes.constant ""
      simulation.dump.vars %ctx, %levels, %scope :
          (!simulation.context, i64, !simulation.bytes) -> ()
      %offset = arith.constant 0 : i64
      %bits = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      simulation.call @write_disjoint(%ctx, %offset, %bits) : (!simulation.context, i64, !simulation.logic<8>) -> ()
      simulation.call @write_overlapping(%ctx, %offset, %bits) : (!simulation.context, i64, !simulation.logic<8>) -> ()
      %clk = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %c = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %w = simulation.spawn @work(%ctx, %clk) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %other_clk = simulation.context.storage %ctx[4] : !simulation.ref<!simulation.logic<1>>
      %oc = simulation.spawn @other_clock(%ctx, %other_clk) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %ow = simulation.spawn @other_work(%ctx, %other_clk) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }
    simulation.func @write_disjoint(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %offset: i64 {simulation.capture_kind = 2 : i32},
        %bits: !simulation.logic<8> {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %root = simulation.context.storage %ctx[9] : !simulation.ref<!simulation.logic<129>>
      %slice = simulation.ref.dyn_extract %root from %offset : (!simulation.ref<!simulation.logic<129>>, i64) -> !simulation.ref<!simulation.logic<8>>
      simulation.ref.store %bits to %slice : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.return
    }
    simulation.func @write_overlapping(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %offset: i64 {simulation.capture_kind = 2 : i32},
        %bits: !simulation.logic<8> {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64} {
      %root = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<160>>
      %slice = simulation.ref.dyn_extract %root from %offset : (!simulation.ref<!simulation.logic<160>>, i64) -> !simulation.ref<!simulation.logic<8>>
      simulation.ref.store %bits to %slice : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.return
    }
    simulation.func @clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clk: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^toggle
    ^toggle:
      %old = simulation.ref.load %clk : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %next = simulation.logic.unary bit_not %old : (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %next to %clk : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      cf.br ^wait
    }
    simulation.func @work(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clk: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clk to ^body : !simulation.ref<!simulation.logic<1>>
    ^body:
      %input = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<160>>
      %output = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<129>>
      %other = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<64>>
      %view = simulation.ref.extract %input from 3 : !simulation.ref<!simulation.logic<160>> -> !simulation.ref<!simulation.logic<129>>
      %overlap = simulation.ref.extract %input from 11 : !simulation.ref<!simulation.logic<160>> -> !simulation.ref<!simulation.logic<64>>
      %a = simulation.ref.load %view : !simulation.ref<!simulation.logic<129>> -> !simulation.logic<129>
      %b = simulation.ref.load %overlap : !simulation.ref<!simulation.logic<64>> -> !simulation.logic<64>
      simulation.ref.store %a to %output : !simulation.logic<129>, !simulation.ref<!simulation.logic<129>>
      simulation.ref.store %b to %other : !simulation.logic<64>, !simulation.ref<!simulation.logic<64>>
      %nba = simulation.context.storage %ctx[7] : !simulation.ref<!simulation.logic<8>>
      %zero = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      simulation.nba.enqueue %zero to %nba : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      cf.br ^wait
    }
    simulation.func @other_clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clk: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^toggle
    ^toggle:
      %old = simulation.ref.load %clk : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %next = simulation.logic.unary bit_not %old : (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %next to %clk : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      cf.br ^wait
    }
    simulation.func @other_work(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clk: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clk to ^body : !simulation.ref<!simulation.logic<1>>
    ^body:
      %input = simulation.context.storage %ctx[5] : !simulation.ref<!simulation.logic<16>>
      %output = simulation.context.storage %ctx[6] : !simulation.ref<!simulation.logic<16>>
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<16>> -> !simulation.logic<16>
      simulation.ref.store %value to %output : !simulation.logic<16>, !simulation.ref<!simulation.logic<16>>
      %nba = simulation.context.storage %ctx[8] : !simulation.ref<!simulation.logic<8>>
      %zero = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      simulation.nba.enqueue %zero to %nba : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      cf.br ^wait
    }
  }
}
