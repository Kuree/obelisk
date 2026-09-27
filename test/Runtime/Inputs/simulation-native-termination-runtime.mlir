// This fixture is compiled for each scheduler and each explicit termination
// operation. The helper checks exit status, the final actor's exact state, and
// generated scheduler selection. Neither the pending NBA nor another active
// iteration may execute after termination. The first 499 activations avoid
// the cold termination branch.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 0 : i32
} {
  obelisk_sim.design @termination {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.storage.decl 1 in 0 : i32 design
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
      // This read must see the preceding blocking store in both checkpoint
      // predicates. The probe models it without publishing a real increment.
      %observed = obelisk_sim.ref.load %count : !obelisk_sim.ref<i32> -> i32
      %limit = arith.constant 500 : i32
      %done = arith.cmpi eq, %observed, %limit : i32
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
