// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @queue_mutation {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.process"
    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %queue: !simulation.queue<i64, 4>
            {simulation.capture_kind = 2 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %index = arith.constant 1 : i64
      %value = arith.constant 42 : i64
      simulation.queue.insert %value into %queue[%index] :
          !simulation.queue<i64, 4>, i64
      simulation.queue.delete %queue[%index] : !simulation.queue<i64, 4>
      simulation.return
    }
  }
}

// NATIVE-DAG: llvm.func @obelisk_rt_v1_queue_insert
// NATIVE-DAG: llvm.func @obelisk_rt_v1_queue_delete_index
// NATIVE-LABEL: llvm.func @process(
// NATIVE: llvm.call @obelisk_rt_v1_queue_insert
// NATIVE: llvm.call @obelisk_rt_v1_queue_delete_index
// NATIVE-NOT: simulation.queue

// Queue insert (0x00010445) and queue delete (0x00010444) are encoded in
// program order in the bytecode image.
// BYTECODE: obelisk.bytecode.image = array<i8: {{.*}}69, 4, 1, 0{{.*}}68, 4, 1, 0
// BYTECODE: obelisk.bytecode.function = 0 : i32
