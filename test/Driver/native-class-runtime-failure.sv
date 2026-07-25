// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: /bin/sh -c '"%t.o0.native" > "%t.o0.native.out"; test $? -eq 2'
// RUN: test ! -s %t.o0.native.out
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: /bin/sh -c '"%t.o3.native" > "%t.o3.native.out"; test $? -eq 2'
// RUN: test ! -s %t.o3.native.out
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: /bin/sh -c '"%t.bytecode" > "%t.bytecode.out"; test $? -eq 2'
// RUN: test ! -s %t.bytecode.out
// RUN: obelisk -O0 -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM

class item;
  int value;
endclass

module top;
  function automatic int fail_with_live_root();
    item live = new;
    item trigger = new;
    item object = null;
    return object.value + live.value + trigger.value;
  endfunction

  initial begin
    $display("%0d", fail_with_live_root());
    $display("continued");
  end
endmodule

// One pop is the success exit. Additional pops prove that generated runtime
// failure returns also unwind the activation's managed-root range.
// LLVM: call i32 @obelisk_rt_v1_gc_root_range_push
// LLVM-COUNT-2: call i32 @obelisk_rt_v1_gc_root_range_pop
