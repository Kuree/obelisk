// Fixed partial writes must be forwarded into subsequent whole-root reads,
// preserving untouched bits. Exercise two-state slices and four-state packed
// struct fields across all scheduler and termination modes. The private
// predicate must not publish either speculative update to canonical state.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 0 : i32
} {
  simulation.design @termination {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i32 design
    simulation.storage.decl 2 in 0 : !simulation.packed_struct<[#simulation.field<name = "low", type = !simulation.logic<4>, ordinal = 0, packedOffset = 0>, #simulation.field<name = "high", type = !simulation.logic<4>, ordinal = 1, packedOffset = 4>]> design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "work"
    simulation.code_unit.decl 4 in 0 final hierarchy "final"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %count = simulation.context.storage %ctx[1] : !simulation.ref<i32>
      %zero = arith.constant 0 : i32
      %false = arith.constant false
      simulation.ref.store %zero to %count : i32, !simulation.ref<i32>
      simulation.ref.store %false to %clock : i1, !simulation.ref<i1>
      %c = simulation.spawn @clock(%ctx, %clock) : !simulation.context, !simulation.ref<i1> -> !simulation.process
      %w = simulation.spawn @work(%ctx, %clock, %count) : !simulation.context, !simulation.ref<i1>, !simulation.ref<i32> -> !simulation.process
      %f = simulation.spawn @final(%ctx, %count) : !simulation.context, !simulation.ref<i32> -> !simulation.process
      simulation.return
    }
    simulation.func @clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^toggle
    ^toggle:
      %old = simulation.ref.load %clock : !simulation.ref<i1> -> i1
      %true = arith.constant true
      %new = arith.xori %old, %true : i1
      simulation.ref.store %new to %clock : i1, !simulation.ref<i1>
      cf.br ^wait
    }
    simulation.func @work(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %count: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clock to ^resume : !simulation.ref<i1>
    ^resume:
      %old = simulation.ref.load %count : !simulation.ref<i32> -> i32
      %one = arith.constant 1 : i32
      %next = arith.addi %old, %one : i32
      simulation.ref.store %next to %count : i32, !simulation.ref<i32>
      %packed = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.packed_struct<[#simulation.field<name = "low", type = !simulation.logic<4>, ordinal = 0, packedOffset = 0>, #simulation.field<name = "high", type = !simulation.logic<4>, ordinal = 1, packedOffset = 4>]>>
      %zero8 = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      %initialPacked = simulation.packed.unflatten %zero8 : (!simulation.logic<8>) -> !simulation.packed_struct<[#simulation.field<name = "low", type = !simulation.logic<4>, ordinal = 0, packedOffset = 0>, #simulation.field<name = "high", type = !simulation.logic<4>, ordinal = 1, packedOffset = 4>]>
      simulation.ref.store %initialPacked to %packed : !simulation.packed_struct<[#simulation.field<name = "low", type = !simulation.logic<4>, ordinal = 0, packedOffset = 0>, #simulation.field<name = "high", type = !simulation.logic<4>, ordinal = 1, packedOffset = 4>]>, !simulation.ref<!simulation.packed_struct<[#simulation.field<name = "low", type = !simulation.logic<4>, ordinal = 0, packedOffset = 0>, #simulation.field<name = "high", type = !simulation.logic<4>, ordinal = 1, packedOffset = 4>]>>
      %high = simulation.ref.subelement %packed[[1]] : !simulation.ref<!simulation.packed_struct<[#simulation.field<name = "low", type = !simulation.logic<4>, ordinal = 0, packedOffset = 0>, #simulation.field<name = "high", type = !simulation.logic<4>, ordinal = 1, packedOffset = 4>]>> -> !simulation.ref<!simulation.logic<4>>
      %seven = simulation.logic.constant 7 : i4, 0 : i4 : !simulation.logic<4>
      simulation.ref.store %seven to %high : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      %whole = simulation.ref.load %packed : !simulation.ref<!simulation.packed_struct<[#simulation.field<name = "low", type = !simulation.logic<4>, ordinal = 0, packedOffset = 0>, #simulation.field<name = "high", type = !simulation.logic<4>, ordinal = 1, packedOffset = 4>]>> -> !simulation.packed_struct<[#simulation.field<name = "low", type = !simulation.logic<4>, ordinal = 0, packedOffset = 0>, #simulation.field<name = "high", type = !simulation.logic<4>, ordinal = 1, packedOffset = 4>]>
      %flat = simulation.packed.flatten %whole : (!simulation.packed_struct<[#simulation.field<name = "low", type = !simulation.logic<4>, ordinal = 0, packedOffset = 0>, #simulation.field<name = "high", type = !simulation.logic<4>, ordinal = 1, packedOffset = 4>]>) -> !simulation.logic<8>
      %expected = simulation.logic.constant 112 : i8, 0 : i8 : !simulation.logic<8>
      %badPacked = simulation.logic.compare case_ne %flat, %expected : (!simulation.logic<8>, !simulation.logic<8>) -> i1
      cf.cond_br %badPacked, ^terminate, ^count_adjust
    ^count_adjust:
      %odd = arith.trunci %old : i32 to i1
      cf.cond_br %odd, ^observe, ^adjust
    ^adjust:
      %bumped = arith.addi %next, %one : i32
      simulation.ref.store %bumped to %count : i32, !simulation.ref<i32>
      %temporary = simulation.ref.load %count : !simulation.ref<i32> -> i32
      %restored = arith.subi %temporary, %one : i32
      simulation.ref.store %restored to %count : i32, !simulation.ref<i32>
      cf.br ^observe
    ^observe:
      %upper = simulation.ref.extract %count from 16 : !simulation.ref<i32> -> !simulation.ref<i16>
      %one16 = arith.constant 1 : i16
      simulation.ref.store %one16 to %upper : i16, !simulation.ref<i16>
      %observed = simulation.ref.load %count : !simulation.ref<i32> -> i32
      %limit = arith.constant 66036 : i32
      %done = arith.cmpi eq, %observed, %limit : i32
      // This terminal partial write overlaps the promoted count cell, but no
      // later probe read can observe it. Keep it in the real activation only.
      %zero16 = arith.constant 0 : i16
      simulation.ref.store %zero16 to %upper : i16, !simulation.ref<i16>
      cf.cond_br %done, ^terminate, ^wait
    ^terminate:
      %nba = arith.constant 999 : i32
      simulation.nba.enqueue %nba to %count : (i32, !simulation.ref<i32>) -> ()
      %verbosity = arith.constant 0 : i32
      simulation.fatal %ctx, %verbosity
      simulation.return
    }
    simulation.func @final(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %count: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 2 : i32, code_unit_id = 4 : i64} {
      %value = simulation.ref.load %count : !simulation.ref<i32> -> i32
      %fmt = simulation.bytes.constant "final=%0d"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%fmt, %value) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, i32
      simulation.return
    }
  }
}
