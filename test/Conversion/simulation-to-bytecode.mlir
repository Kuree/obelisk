// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=ENCODE --implicit-check-not=obelisk.design.database
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=full' | FileCheck %s --check-prefix=DATABASE
// RUN: obelisk-opt %s --mlir-print-debuginfo --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-inline{opt-level=3 caller-growth-percent=10000 caller-growth-constant=10000 design-growth-percent=10000 design-growth-constant=10000}),encode-obelisk-sim-to-bytecode{vpi=full})' | FileCheck %s --check-prefix=INLINED-DATABASE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=full' --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=LOWER
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=full' --convert-obelisk-sim-processes-to-llvm-coroutines | mlir-translate --mlir-to-llvmir | opt -S -passes=verify | FileCheck %s --check-prefix=LLVM
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py \
// RUN:   | FileCheck %s --check-prefix=INSTRUCTIONS

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
    obelisk_sim.code_unit.decl 70 in 0 function hierarchy "top.add" debug "add" loc("design.sv":7:3)
    obelisk_sim.code_unit.decl 71 in 0 initial hierarchy "top.process" debug "process"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<65> design
        hierarchy "top.value"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<2> design
        {resolution_kind = 2 : i32}
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<2> design
        {resolution_kind = 2 : i32}
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<2> design
        {driven_low = 0 : i64, driven_width = 1 : i64}
    obelisk_sim.driver.decl 1 in 0 drives 0 : !obelisk_sim.logic<2> design
        {driven_low = 1 : i64, driven_width = 1 : i64}
    obelisk_sim.net.connect.decl 0 in 0 0[0] to 1[0] width 2 reversed = false

    obelisk_sim.func private @add(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %lhs: !obelisk_sim.logic<65> {obelisk_sim.capture_kind = 1 : i32},
        %rhs: !obelisk_sim.logic<65> {obelisk_sim.capture_kind = 1 : i32})
        -> !obelisk_sim.logic<65> attributes {code_unit_id = 70 : i64,
                                              entry_kind = 8 : i32} {
      %sum = obelisk_sim.logic.binary add %lhs, %rhs
          : !obelisk_sim.logic<65>
      obelisk_sim.return %sum : !obelisk_sim.logic<65>
    }

    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 71 : i64, entry_kind = 1 : i32} {
      %two = obelisk_sim.logic.constant 2 : i65, 0 : i65 : !obelisk_sim.logic<65>
      %three = obelisk_sim.logic.constant 3 : i65, 0 : i65 : !obelisk_sim.logic<65>
      %narrow = obelisk_sim.logic.resize %two signed = false
          : !obelisk_sim.logic<65> -> !obelisk_sim.logic<64>
      %extended = obelisk_sim.logic.resize %narrow signed = true
          : !obelisk_sim.logic<64> -> !obelisk_sim.logic<65>
      %replacement = obelisk_sim.logic.constant 6 : i3, 0 : i3 : !obelisk_sim.logic<3>
      %index = obelisk_sim.logic.constant -1 : i8, 0 : i8 : !obelisk_sim.logic<8>
      %updated = obelisk_sim.logic.dyn_insert %replacement into %extended at %index
          : (!obelisk_sim.logic<65>, !obelisk_sim.logic<3>, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<65>
      %storage = obelisk_sim.context.storage %ctx[0]
          : !obelisk_sim.ref<!obelisk_sim.logic<65>>
      obelisk_sim.ref.store %updated to %storage
          {obelisk_sim.continuous_store} : !obelisk_sim.logic<65>,
          !obelisk_sim.ref<!obelisk_sim.logic<65>>
      obelisk_sim.override %storage = %extended assign true
          : !obelisk_sim.ref<!obelisk_sim.logic<65>>, !obelisk_sim.logic<65>
      obelisk_sim.release_override %storage assign true
          : !obelisk_sim.ref<!obelisk_sim.logic<65>>
      obelisk_sim.override %storage = %extended assign false
          : !obelisk_sim.ref<!obelisk_sim.logic<65>>, !obelisk_sim.logic<65>
      obelisk_sim.release_override %storage assign false
          : !obelisk_sim.ref<!obelisk_sim.logic<65>>
      %sum = obelisk_sim.call @add(%ctx, %two, %three)
          : (!obelisk_sim.context, !obelisk_sim.logic<65>, !obelisk_sim.logic<65>) -> !obelisk_sim.logic<65>
      %first = obelisk_sim.assert.deferred_once 4294967297
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^done
    ^done:
      obelisk_sim.return
    }
  }
}

// Unified runtime artifact version 1 follows the eight-byte magic. It includes
// task activation transfers in addition to
// per-continuation schedule ranks, the canonical connectivity table, and
// disjoint per-bit uwire driver ranges.
// ENCODE: obelisk.bytecode.image = array<i8: 79, 66, 66, 67, 68, 83, 49, 0, 1, 0, 0, 0, 0, 0, 0, 0
// ENCODE: obelisk.execution.flags = 1 : i32
// ENCODE: obelisk.execution.state_bits = 73 : i64
// ENCODE: obelisk.bytecode.function = 0 : i32
// ENCODE: obelisk.bytecode.scratch_alignment = 8 : i64
// ENCODE: obelisk.bytecode.function = 1 : i32

// Dynamic INSERT carries its low-bit register in source2; static INSERT leaves
// source2 zero and uses only the immediate field.
// INSTRUCTIONS: opcode=22 flags=1 {{.*}}src2={{[0-9]+}} {{.*}}imm=0

// Unified runtime artifact version 1 follows the database magic; the next word
// is reserved.
// DATABASE: obelisk.design.database = array<i8: 79, 66, 68, 83, 71, 78, 49, 0, 1, 0, 0, 0, 0, 0, 0, 0

// The last executable copy of @add is erased, but version-1 reflection still
// originates from its immutable record, including parent, name, and source.
// INLINED-DATABASE: obelisk.design.database = array<i8: 79, 66, 68, 83, 71, 78, 49, 0, 1, 0, 0, 0, 0, 0, 0, 0
// INLINED-DATABASE: obelisk_sim.code_unit.decl 70 in 0 function hierarchy "top.add" debug "add"
// INLINED-DATABASE-SAME: loc(#loc[[ADD:[0-9]+]])
// INLINED-DATABASE-NOT: obelisk_sim.func private @add
// INLINED-DATABASE: #loc[[ADD]] = loc("design.sv":7:3)

// LOWER: llvm.mlir.global external constant @process.__obelisk_process_descriptor
// LOWER-SAME: !llvm.struct<(struct<(i32, i32, i64)>, i32, i32, i32, i32, ptr, ptr, ptr, ptr, ptr, ptr, ptr)>
// LOWER: llvm.mlir.constant(71 : i64)
// LOWER: llvm.mlir.addressof @__obelisk_execution_descriptor_v1
// LOWER: llvm.mlir.addressof @process.__obelisk_bytecode_entry
// LOWER: llvm.mlir.global internal constant @process.__obelisk_bytecode_entry
// LOWER: llvm.mlir.global external constant @__obelisk_execution_descriptor_v1
// LOWER-SAME: section = ".obelisk.execution"
// LOWER: llvm.mlir.global internal constant @__obelisk_activations_v1
// LOWER-SAME: section = ".obelisk.execution"
// LOWER: llvm.mlir.constant(71 : i64)
// LOWER: llvm.mlir.addressof @process.__obelisk_process_descriptor
// LOWER: llvm.mlir.constant(3 : i32)
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
// LLVM: @__obelisk_activations_v1 = internal constant
// LLVM-SAME: section ".obelisk.execution"
// LLVM: @__obelisk_design_database_v1 = constant
// LLVM-SAME: section ".obelisk.design"
// LLVM: @__obelisk_bytecode_image_v1 = constant
// LLVM-SAME: section ".obelisk.bytecode"
// LLVM: @llvm.used = appending constant [2 x ptr] [ptr @preexisting, ptr @__obelisk_design_database_v1], section "llvm.metadata"
// LLVM: @llvm.compiler.used = appending constant [1 x ptr] [ptr @preexisting], section "llvm.metadata"
