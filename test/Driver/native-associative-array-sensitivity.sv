// RUN: obelisk -O0 %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: FileCheck %s < %t.native.out

module native_associative_array_sensitivity;
  int array[int];
  int sink;

  function automatic int set_element(ref int target);
    target = 9;
    return target;
  endfunction

  always_comb begin
    sink = array.exists(1) ? array[1] : -1;
    $display("wake %0d", sink);
  end

  initial begin
    array[1] = 1;
    #1 array[1] = 7;
    #1 set_element(array[1]);
    #1 array.delete(1);
    #1 array[2] = 5;
    #1 array.delete();
    #1 $finish;
  end

  // IEEE 1800-2023 9.2.2.2 runs always_comb once at time zero after initial
  // and always procedures have started, so the first activation observes 1.
  // CHECK: wake 1
  // CHECK-NEXT: wake 7
  // CHECK-NEXT: wake 9
  // CHECK-NEXT: wake -1
  // CHECK-NEXT: wake -1
  // CHECK-NEXT: wake -1
endmodule
