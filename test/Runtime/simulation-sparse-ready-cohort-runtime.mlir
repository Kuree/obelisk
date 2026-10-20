// RUN: obelisk-opt %s --pass-pipeline='builtin.module(test-obelisk-replicate,simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),convert-obelisk-sim-processes-to-llvm-coroutines)' | obelisk-translate --mlir-to-llvmir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %target_cxx %t.o %runtime_link_flags -o %t.exe
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.exe --execution-tier=native 2>&1 | FileCheck %s

// Reconstructing a small ready cohort must scale with that cohort, not all
// unrelated sleeping actors. Seventeen hot actors toggle 1000 times beside 128
// sleepers: the scheduler may visit about 17 candidates per step plus startup,
// far below the 145 per step of a full inventory scan. Check deterministic
// scheduler work, not wall time; every resubscription must be retained.
// CHECK: cohort=17000
// CHECK: candidate_inventory_visits={{([1-9][0-9]{0,3}|[1-4][0-9]{4}) }}

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @sparse {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i1 design
    simulation.storage.decl 2 in 0 : i32 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "sleep"
    simulation.code_unit.decl 3 in 0 always hierarchy "hot"
    simulation.code_unit.decl 4 in 0 initial hierarchy "drive"
    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %dormant = simulation.context.storage %ctx[1] : !simulation.ref<i1>
      %count = simulation.context.storage %ctx[2] : !simulation.ref<i32>
      %zero = arith.constant 0 : i32
      %false = arith.constant false
      simulation.ref.store %zero to %count : i32, !simulation.ref<i32>
      simulation.ref.store %false to %clock : i1, !simulation.ref<i1>
      simulation.ref.store %false to %dormant : i1, !simulation.ref<i1>

      %sleeper = simulation.spawn @sleep(%ctx, %dormant) {test.replicate = 128 : i64} :
          !simulation.context, !simulation.ref<i1> -> !simulation.process
      %hot = simulation.spawn @hot(%ctx, %clock, %count) {test.replicate = 17 : i64} :
          !simulation.context, !simulation.ref<i1>, !simulation.ref<i32>
          -> !simulation.process
      %d = simulation.spawn @drive(%ctx, %clock, %count) :
          !simulation.context, !simulation.ref<i1>, !simulation.ref<i32>
          -> !simulation.process
      simulation.return
    }
    simulation.func @sleep(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<i1> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %ref to ^wait : !simulation.ref<i1>
    }
    simulation.func @hot(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<i1> {simulation.capture_kind = 1 : i32},
        %count: !simulation.ref<i32> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %ref to ^resume : !simulation.ref<i1>
    ^resume:
      %old = simulation.ref.load %count : !simulation.ref<i32> -> i32
      %one = arith.constant 1 : i32
      %next = arith.addi %old, %one : i32
      simulation.ref.store %next to %count : i32, !simulation.ref<i32>
      cf.br ^wait
    }
    simulation.func @drive(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<i1> {simulation.capture_kind = 1 : i32},
        %count: !simulation.ref<i32> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %zero = arith.constant 0 : i32
      cf.br ^wait(%zero : i32)
    ^wait(%iteration: i32):
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^toggle(%iteration : i32)
    ^toggle(%iteration2: i32):
      %old = simulation.ref.load %ref : !simulation.ref<i1> -> i1
      %true = arith.constant true
      %new = arith.xori %old, %true : i1
      simulation.ref.store %new to %ref : i1, !simulation.ref<i1>
      %one = arith.constant 1 : i32
      %next = arith.addi %iteration2, %one : i32
      %limit = arith.constant 1000 : i32
      %more = arith.cmpi ult, %next, %limit : i32
      cf.cond_br %more, ^wait(%next : i32), ^settle
    ^settle:
      %delay2 = simulation.time.constant 1
      simulation.suspend.delay %delay2 to ^done
    ^done:
      %value = simulation.ref.load %count : !simulation.ref<i32> -> i32
      %fmt = simulation.bytes.constant "cohort=%0d"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%fmt, %value) newline = true
          radix = <decimal> flags = [0, 0] : !simulation.bytes, i32
      simulation.return
    }
  }
}
