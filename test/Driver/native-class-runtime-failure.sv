// RUN: obelisk -O3 %s -o %t.native
// RUN: not %t.native 2>&1 | FileCheck %s --check-prefix=ERROR --implicit-check-not='{{.}}'
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: not %t.bytecode 2>&1 | FileCheck %s --check-prefix=ERROR --implicit-check-not='{{.}}'
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

// The null dereference ends the simulation with status 2 before any output.
// ERROR: {{^}}error: simulation ended: invalid argument (status 2){{$}}
// One pop is the success exit. Additional pops prove that generated runtime
// failure returns also unwind the activation's managed-root range.
// LLVM: call i32 @obelisk_rt_v1_gc_managed_root_range_push
// LLVM-COUNT-2: call i32 @obelisk_rt_v1_gc_managed_root_range_pop
