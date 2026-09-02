// RUN: obelisk -O0 -emit-sim %s | FileCheck %s --check-prefix=O0
// RUN: obelisk -O1 -emit-sim %s | FileCheck %s --check-prefix=INLINE
// RUN: obelisk -O2 -emit-sim %s | FileCheck %s --check-prefix=INLINE
// RUN: obelisk -O3 -emit-sim %s | FileCheck %s --check-prefix=INLINE
// RUN: obelisk -emit-sim %s | FileCheck %s --check-prefix=INLINE
// RUN: obelisk -O1 -emit-sim --compile-threads=1 %s > %t.o1.single
// RUN: obelisk -O1 -emit-sim --compile-threads=4 %s > %t.o1.threaded
// RUN: diff -u %t.o1.single %t.o1.threaded
// RUN: obelisk -O3 -O0 -emit-sim %s | FileCheck %s --check-prefix=O0
// RUN: obelisk -O0 -O1 -emit-sim %s | FileCheck %s --check-prefix=INLINE
// RUN: obelisk -O0 -emit-llvm %s | FileCheck %s --check-prefix=LLVM-O0
// RUN: obelisk -O1 -emit-llvm %s | FileCheck %s --check-prefix=LLVM-OPT
// RUN: obelisk -O2 -emit-llvm %s | FileCheck %s --check-prefix=LLVM-OPT
// RUN: obelisk -O3 -emit-llvm %s | FileCheck %s --check-prefix=LLVM-O3

module optimization_levels;
  logic [7:0] result;
  logic dead_capture;

  function automatic logic [7:0] add_one(input logic [7:0] value);
    add_one = value + 1;
  endfunction

  initial begin
    // Early boundary DCE proves this capture dead at every level before graph
    // and bytecode/native construction. O1+ additionally inlines add_one.
    if (1'b0)
      result = dead_capture;
    result = add_one(8'd41);
  end
endmodule

// O0: obelisk_sim.code_unit.decl {{[0-9]+}} in 1 function hierarchy "optimization_levels.add_one" debug "add_one"
// O0: obelisk_sim.func private @unit_0
// O0: obelisk_sim.func private @unit_1(%arg0: !obelisk_sim.context
// O0-SAME: %arg1: !obelisk_sim.ref<!obelisk_sim.packed_array
// O0-SAME: obelisk_sim.descriptor_id = 0 : i64}) attributes
// O0: obelisk_sim.call @unit_0

// INLINE: obelisk_sim.code_unit.decl {{[0-9]+}} in 1 function hierarchy "optimization_levels.add_one" debug "add_one"
// INLINE-NOT: obelisk_sim.func private @unit_0
// INLINE-NOT: obelisk_sim.call @unit_0
// INLINE: obelisk_sim.func private @unit_1(%arg0: !obelisk_sim.context
// INLINE-SAME: %arg1: !obelisk_sim.ref<!obelisk_sim.packed_array
// INLINE-SAME: obelisk_sim.descriptor_id = 0 : i64}) attributes

// The selected level also reaches LLVM's optimization pipeline.
// LLVM-O0: define i32 @__obelisk_root(ptr
// LLVM-OPT: define noundef i32 @__obelisk_root(ptr
// LLVM-OPT-SAME: local_unnamed_addr
// LLVM-O3: define noundef i32 @__obelisk_root(ptr
// LLVM-O3-SAME: local_unnamed_addr
