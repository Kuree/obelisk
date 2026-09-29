// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=ENCODE --implicit-check-not=obelisk.design.database
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=full' | FileCheck %s --check-prefix=DATABASE
// RUN: obelisk-opt %s --mlir-print-debuginfo --pass-pipeline='builtin.module(simulation.design(obelisk-sim-inline{opt-level=3 caller-growth-percent=10000 caller-growth-constant=10000 design-growth-percent=10000 design-growth-constant=10000}),encode-obelisk-sim-to-bytecode{vpi=full})' | FileCheck %s --check-prefix=INLINED-DATABASE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=full' --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=LOWER --implicit-check-not=add.__obelisk_bytecode_entry --implicit-check-not=observe.__obelisk_bytecode_entry
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=full' --convert-obelisk-sim-processes-to-llvm-coroutines | mlir-translate --mlir-to-llvmir | opt -S -passes=verify | FileCheck %s --check-prefix=LLVM
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py \
// RUN:   | FileCheck %s --check-prefix=INSTRUCTIONS
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py --state \
// RUN:   | FileCheck %s --check-prefix=STATE

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
  simulation.design @bytecode {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 70 in 0 function hierarchy "top.add" debug "add" loc("design.sv":7:3)
    simulation.code_unit.decl 71 in 0 initial hierarchy "top.process" debug "process"
    simulation.code_unit.decl 72 in 0 observer hierarchy "top.observe"
    simulation.storage.decl 0 in 0 : !simulation.logic<65> design
        hierarchy "top.value"
    simulation.net.decl 0 in 0 : !simulation.logic<2> design
        {resolution_kind = 2 : i32}
    simulation.net.decl 1 in 0 : !simulation.logic<2> design
        {resolution_kind = 2 : i32}
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<2> design
        {driven_low = 0 : i64, driven_width = 1 : i64,
         strength0 = 5 : i32, strength1 = 0 : i32}
    simulation.driver.decl 1 in 0 drives 0 : !simulation.logic<2> design
        {driven_low = 1 : i64, driven_width = 1 : i64}
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 2 reversed = false

    simulation.func private @add(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %lhs: !simulation.logic<65> {simulation.capture_kind = 1 : i32},
        %rhs: !simulation.logic<65> {simulation.capture_kind = 1 : i32})
        -> !simulation.logic<65> attributes {code_unit_id = 70 : i64,
                                              entry_kind = 8 : i32} {
      %sum = simulation.logic.binary add %lhs, %rhs
          : !simulation.logic<65>
      simulation.return %sum : !simulation.logic<65>
    }

    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 71 : i64, entry_kind = 1 : i32} {
      %two = simulation.logic.constant 2 : i65, 0 : i65 : !simulation.logic<65>
      %three = simulation.logic.constant 3 : i65, 0 : i65 : !simulation.logic<65>
      %narrow = simulation.logic.resize %two signed = false
          : !simulation.logic<65> -> !simulation.logic<64>
      %extended = simulation.logic.resize %narrow signed = true
          : !simulation.logic<64> -> !simulation.logic<65>
      %replacement = simulation.logic.constant 6 : i3, 0 : i3 : !simulation.logic<3>
      %index = simulation.logic.constant -1 : i8, 0 : i8 : !simulation.logic<8>
      %updated = simulation.logic.dyn_insert %replacement into %extended at %index
          : (!simulation.logic<65>, !simulation.logic<3>, !simulation.logic<8>) -> !simulation.logic<65>
      %storage = simulation.context.storage %ctx[0]
          : !simulation.ref<!simulation.logic<65>>
      simulation.ref.store %updated to %storage
          {simulation.continuous_store} : !simulation.logic<65>,
          !simulation.ref<!simulation.logic<65>>
      simulation.override %storage = %extended assign true
          : !simulation.ref<!simulation.logic<65>>, !simulation.logic<65>
      simulation.release_override %storage assign true
          : !simulation.ref<!simulation.logic<65>>
      simulation.override %storage = %extended assign false
          : !simulation.ref<!simulation.logic<65>>, !simulation.logic<65>
      %owner = simulation.process.current
      simulation.dynamic_override %storage = %extended owner %owner
          assign false claim true :
          !simulation.ref<!simulation.logic<65>>, !simulation.logic<65>
      simulation.dynamic_override %storage = %extended owner %owner
          assign false claim false :
          !simulation.ref<!simulation.logic<65>>, !simulation.logic<65>
      simulation.release_override %storage assign false
          : !simulation.ref<!simulation.logic<65>>
      %sum = simulation.call @add(%ctx, %two, %three)
          : (!simulation.context, !simulation.logic<65>, !simulation.logic<65>) -> !simulation.logic<65>
      %first = simulation.assert.deferred_once 4294967297
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^done
    ^done:
      simulation.return
    }
    simulation.func private @observe(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i1
        attributes {code_unit_id = 72 : i64, entry_kind = 14 : i32,
                    schedule.observer_width = 1 : i32,
                    schedule.observer_four_state = false} {
      %false = arith.constant false
      simulation.return %false : i1
    }
  }
}

// Unified runtime artifact version 1 follows the eight-byte magic. It includes
// task activation transfers in addition to
// per-continuation schedule ranks, the canonical connectivity table, and
// disjoint per-bit uwire driver ranges.
// ENCODE: obelisk.bytecode.image = array<i8: 79, 66, 66, 67, 68, 83, 49, 0, 1, 0, 0, 0, 0, 0, 0, 0
// ENCODE: obelisk.execution.flags = 1 : i32
// ENCODE: obelisk.execution.state_bits = 98 : i64
// ENCODE: obelisk.bytecode.function = 0 : i32
// ENCODE: obelisk.bytecode.scratch_alignment = 8 : i64
// ENCODE: obelisk.bytecode.function = 1 : i32
// ENCODE: obelisk.bytecode.function = 2 : i32

// Dynamic INSERT carries its low-bit register in source2; static INSERT leaves
// source2 zero and uses only the immediate field.
// INSTRUCTIONS: opcode=22 flags=1 {{.*}}src2={{[0-9]+}} {{.*}}imm=0

// Strength codes are serialized one greater than the Figure 28-2 position;
// zero remains the legacy/default-strong encoding.
// STATE: state {{[0-9]+}}: kind=driver flags=181 {{.*}}strength0=5 strength1=0

// Unified runtime artifact version 1 follows the database magic. The extension
// directory offset that follows is intentionally image-layout dependent.
// DATABASE: obelisk.design.database = array<i8: 79, 66, 68, 83, 71, 78, 49, 0, 1, 0, 0, 0

// The last executable copy of @add is erased, but version-1 reflection still
// originates from its immutable record, including parent, name, and source.
// INLINED-DATABASE: obelisk.design.database = array<i8: 79, 66, 68, 83, 71, 78, 49, 0, 1, 0, 0, 0
// INLINED-DATABASE: simulation.code_unit.decl 70 in 0 function hierarchy "top.add" debug "add"
// INLINED-DATABASE-SAME: loc(#loc[[ADD:[0-9]+]])
// INLINED-DATABASE-NOT: simulation.func private @add
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
