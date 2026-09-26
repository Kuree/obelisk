// Fixed partial writes must be forwarded into subsequent whole-root reads,
// preserving untouched bits. Exercise two-state slices and four-state packed
// struct fields across all scheduler and termination modes. The private
// predicate must not publish either speculative update to canonical state.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 0 : i32
} {
  obelisk_sim.design @termination {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.storage.decl 1 in 0 : i32 design
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.packed_struct<[#obelisk_sim.field<name = "low", type = !obelisk_sim.logic<4>, ordinal = 0, packedOffset = 0>, #obelisk_sim.field<name = "high", type = !obelisk_sim.logic<4>, ordinal = 1, packedOffset = 4>]> design
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "clock"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "work"
    obelisk_sim.code_unit.decl 4 in 0 final hierarchy "final"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i1>
      %count = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<i32>
      %zero = arith.constant 0 : i32
      %false = arith.constant false
      obelisk_sim.ref.store %zero to %count : i32, !obelisk_sim.ref<i32>
      obelisk_sim.ref.store %false to %clock : i1, !obelisk_sim.ref<i1>
      %c = obelisk_sim.spawn @clock(%ctx, %clock) : !obelisk_sim.context, !obelisk_sim.ref<i1> -> !obelisk_sim.process
      %w = obelisk_sim.spawn @work(%ctx, %clock, %count) : !obelisk_sim.context, !obelisk_sim.ref<i1>, !obelisk_sim.ref<i32> -> !obelisk_sim.process
      %f = obelisk_sim.spawn @final(%ctx, %count) : !obelisk_sim.context, !obelisk_sim.ref<i32> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @clock(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^toggle
    ^toggle:
      %old = obelisk_sim.ref.load %clock : !obelisk_sim.ref<i1> -> i1
      %true = arith.constant true
      %new = arith.xori %old, %true : i1
      obelisk_sim.ref.store %new to %clock : i1, !obelisk_sim.ref<i1>
      cf.br ^wait
    }
    obelisk_sim.func @work(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %count: !obelisk_sim.ref<i32> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %clock to ^resume : !obelisk_sim.ref<i1>
    ^resume:
      %old = obelisk_sim.ref.load %count : !obelisk_sim.ref<i32> -> i32
      %one = arith.constant 1 : i32
      %next = arith.addi %old, %one : i32
      obelisk_sim.ref.store %next to %count : i32, !obelisk_sim.ref<i32>
      %packed = obelisk_sim.context.storage %ctx[2] : !obelisk_sim.ref<!obelisk_sim.packed_struct<[#obelisk_sim.field<name = "low", type = !obelisk_sim.logic<4>, ordinal = 0, packedOffset = 0>, #obelisk_sim.field<name = "high", type = !obelisk_sim.logic<4>, ordinal = 1, packedOffset = 4>]>>
      %zero8 = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !obelisk_sim.logic<8>
      %initialPacked = obelisk_sim.packed.unflatten %zero8 : (!obelisk_sim.logic<8>) -> !obelisk_sim.packed_struct<[#obelisk_sim.field<name = "low", type = !obelisk_sim.logic<4>, ordinal = 0, packedOffset = 0>, #obelisk_sim.field<name = "high", type = !obelisk_sim.logic<4>, ordinal = 1, packedOffset = 4>]>
      obelisk_sim.ref.store %initialPacked to %packed : !obelisk_sim.packed_struct<[#obelisk_sim.field<name = "low", type = !obelisk_sim.logic<4>, ordinal = 0, packedOffset = 0>, #obelisk_sim.field<name = "high", type = !obelisk_sim.logic<4>, ordinal = 1, packedOffset = 4>]>, !obelisk_sim.ref<!obelisk_sim.packed_struct<[#obelisk_sim.field<name = "low", type = !obelisk_sim.logic<4>, ordinal = 0, packedOffset = 0>, #obelisk_sim.field<name = "high", type = !obelisk_sim.logic<4>, ordinal = 1, packedOffset = 4>]>>
      %high = obelisk_sim.ref.subelement %packed[[1]] : !obelisk_sim.ref<!obelisk_sim.packed_struct<[#obelisk_sim.field<name = "low", type = !obelisk_sim.logic<4>, ordinal = 0, packedOffset = 0>, #obelisk_sim.field<name = "high", type = !obelisk_sim.logic<4>, ordinal = 1, packedOffset = 4>]>> -> !obelisk_sim.ref<!obelisk_sim.logic<4>>
      %seven = obelisk_sim.logic.constant 7 : i4, 0 : i4 : !obelisk_sim.logic<4>
      obelisk_sim.ref.store %seven to %high : !obelisk_sim.logic<4>, !obelisk_sim.ref<!obelisk_sim.logic<4>>
      %whole = obelisk_sim.ref.load %packed : !obelisk_sim.ref<!obelisk_sim.packed_struct<[#obelisk_sim.field<name = "low", type = !obelisk_sim.logic<4>, ordinal = 0, packedOffset = 0>, #obelisk_sim.field<name = "high", type = !obelisk_sim.logic<4>, ordinal = 1, packedOffset = 4>]>> -> !obelisk_sim.packed_struct<[#obelisk_sim.field<name = "low", type = !obelisk_sim.logic<4>, ordinal = 0, packedOffset = 0>, #obelisk_sim.field<name = "high", type = !obelisk_sim.logic<4>, ordinal = 1, packedOffset = 4>]>
      %flat = obelisk_sim.packed.flatten %whole : (!obelisk_sim.packed_struct<[#obelisk_sim.field<name = "low", type = !obelisk_sim.logic<4>, ordinal = 0, packedOffset = 0>, #obelisk_sim.field<name = "high", type = !obelisk_sim.logic<4>, ordinal = 1, packedOffset = 4>]>) -> !obelisk_sim.logic<8>
      %expected = obelisk_sim.logic.constant 112 : i8, 0 : i8 : !obelisk_sim.logic<8>
      %badPacked = obelisk_sim.logic.compare case_ne %flat, %expected : (!obelisk_sim.logic<8>, !obelisk_sim.logic<8>) -> i1
      cf.cond_br %badPacked, ^terminate, ^count_adjust
    ^count_adjust:
      %odd = arith.trunci %old : i32 to i1
      cf.cond_br %odd, ^observe, ^adjust
    ^adjust:
      %bumped = arith.addi %next, %one : i32
      obelisk_sim.ref.store %bumped to %count : i32, !obelisk_sim.ref<i32>
      %temporary = obelisk_sim.ref.load %count : !obelisk_sim.ref<i32> -> i32
      %restored = arith.subi %temporary, %one : i32
      obelisk_sim.ref.store %restored to %count : i32, !obelisk_sim.ref<i32>
      cf.br ^observe
    ^observe:
      %upper = obelisk_sim.ref.extract %count from 16 : !obelisk_sim.ref<i32> -> !obelisk_sim.ref<i16>
      %one16 = arith.constant 1 : i16
      obelisk_sim.ref.store %one16 to %upper : i16, !obelisk_sim.ref<i16>
      %observed = obelisk_sim.ref.load %count : !obelisk_sim.ref<i32> -> i32
      %limit = arith.constant 66036 : i32
      %done = arith.cmpi eq, %observed, %limit : i32
      // This terminal partial write overlaps the promoted count cell, but no
      // later probe read can observe it. Keep it in the real activation only.
      %zero16 = arith.constant 0 : i16
      obelisk_sim.ref.store %zero16 to %upper : i16, !obelisk_sim.ref<i16>
      cf.cond_br %done, ^terminate, ^wait
    ^terminate:
      %nba = arith.constant 999 : i32
      obelisk_sim.nba.enqueue %nba to %count : (i32, !obelisk_sim.ref<i32>) -> ()
      %verbosity = arith.constant 0 : i32
      obelisk_sim.fatal %ctx, %verbosity
      obelisk_sim.return
    }
    obelisk_sim.func @final(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %count: !obelisk_sim.ref<i32> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 2 : i32, code_unit_id = 4 : i64} {
      %value = obelisk_sim.ref.load %count : !obelisk_sim.ref<i32> -> i32
      %fmt = obelisk_sim.bytes.constant "final=%0d"
      %channel = arith.constant 1 : i32
      obelisk_sim.display %ctx to %channel(%fmt, %value) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, i32
      obelisk_sim.return
    }
  }
}
