// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=ENCODE --implicit-check-not=obelisk.design.database
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=full' --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=LOWER
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=full' --convert-obelisk-sim-processes-to-llvm-coroutines | mlir-translate --mlir-to-llvmir | opt -S -passes=verify | FileCheck %s --check-prefix=LLVM

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  llvm.mlir.global internal constant @preexisting() : i8 {
    %zero = llvm.mlir.constant(0 : i8) : i8
    llvm.return %zero : i8
  }
  llvm.mlir.global appending constant @llvm.used()
      {section = "llvm.metadata"} : !llvm.array<1 x ptr> {
    %zero = llvm.mlir.zero : !llvm.array<1 x ptr>
    %address = llvm.mlir.addressof @preexisting : !llvm.ptr
    %used = llvm.insertvalue %address, %zero[0] : !llvm.array<1 x ptr>
    llvm.return %used : !llvm.array<1 x ptr>
  }
  llvm.mlir.global appending constant @llvm.compiler.used()
      {section = "llvm.metadata"} : !llvm.array<1 x ptr> {
    %zero = llvm.mlir.zero : !llvm.array<1 x ptr>
    %address = llvm.mlir.addressof @preexisting : !llvm.ptr
    %used = llvm.insertvalue %address, %zero[0] : !llvm.array<1 x ptr>
    llvm.return %used : !llvm.array<1 x ptr>
  }
  obelisk_sim.design @bytecode {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<65> design
        hierarchy "top.value"

    obelisk_sim.func @add(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %lhs: !obelisk_sim.logic<65> {obelisk_sim.capture_kind = 1 : i32},
        %rhs: !obelisk_sim.logic<65> {obelisk_sim.capture_kind = 1 : i32})
        -> !obelisk_sim.logic<65> attributes {entry_kind = 8 : i32} {
      %sum = obelisk_sim.logic.binary add %lhs, %rhs
          : !obelisk_sim.logic<65>
      obelisk_sim.return %sum : !obelisk_sim.logic<65>
    }

    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32} {
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^done
    ^done:
      obelisk_sim.return
    }
  }
}

// ENCODE: obelisk.bytecode.image = array<i8: 79, 66, 66, 67, 68, 83, 49, 0
// ENCODE: obelisk.execution.flags = 1 : i32
// ENCODE: obelisk.execution.state_bits = 65 : i64
// ENCODE: obelisk.bytecode.function = 0 : i32
// ENCODE: obelisk.bytecode.scratch_alignment = 8 : i64
// ENCODE: obelisk.bytecode.function = 1 : i32

// LOWER: llvm.mlir.global external constant @process.__obelisk_process_descriptor
// LOWER-SAME: !llvm.struct<(struct<(i32, i32, i64)>, i32, i32, i32, i32, ptr, ptr, ptr, ptr, ptr, ptr, ptr)>
// LOWER: llvm.mlir.addressof @__obelisk_execution_descriptor_v1
// LOWER: llvm.mlir.addressof @process.__obelisk_bytecode_entry
// LOWER: llvm.mlir.global internal constant @process.__obelisk_bytecode_entry
// LOWER: llvm.mlir.global external constant @__obelisk_execution_descriptor_v1
// LOWER-SAME: section = ".obelisk.execution"
// LOWER: llvm.mlir.global external constant @__obelisk_design_database_v1
// LOWER-SAME: section = ".obelisk.design"
// LOWER: llvm.mlir.global external constant @__obelisk_bytecode_image_v1
// LOWER-SAME: section = ".obelisk.bytecode"
// LOWER: llvm.mlir.global appending constant @llvm.used
// LOWER-SAME: section = "llvm.metadata"
// LOWER-SAME: !llvm.array<2 x ptr>
// LOWER: llvm.mlir.global appending constant @llvm.compiler.used
// LOWER-SAME: !llvm.array<1 x ptr>

// LLVM: @__obelisk_execution_descriptor_v1 = constant
// LLVM-SAME: section ".obelisk.execution"
// LLVM: @__obelisk_design_database_v1 = constant
// LLVM-SAME: section ".obelisk.design"
// LLVM: @__obelisk_bytecode_image_v1 = constant
// LLVM-SAME: section ".obelisk.bytecode"
// LLVM: @llvm.used = appending constant [2 x ptr] [ptr @preexisting, ptr @__obelisk_design_database_v1], section "llvm.metadata"
// LLVM: @llvm.compiler.used = appending constant [1 x ptr] [ptr @preexisting], section "llvm.metadata"
