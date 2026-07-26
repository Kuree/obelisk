// RUN: obelisk -O0 %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: FileCheck %s < %t.native.out

module native_dynamic_array_sensitivity;
  int array[];
  int sink;

  function automatic int set_element(ref int target);
    target = 9;
    return target;
  endfunction

  always_comb begin
    sink = array.size() ? array[0] : -1;
    $display("wake %0d", sink);
  end

  initial begin
    array = '{1};
    #1 array[0] = 7;
    #1 array.reverse();
    #1 set_element(array[0]);
    #1 array.delete();
    #1 $finish;
  end

  // CHECK: wake -1
  // CHECK-NEXT: wake 1
  // CHECK-NEXT: wake 7
  // CHECK-NEXT: wake 7
  // CHECK-NEXT: wake 9
  // CHECK-NEXT: wake -1
endmodule
