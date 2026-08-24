// RUN: obelisk -fno-lto -O0 %s -o %t.native
// RUN: %t.native | FileCheck %s
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s

module native_queue_empty_select;
  // Slang speculatively constant-evaluates the call while binding the if.
  // An empty queue rvalue select must return the element default rather than
  // admitting the queue's one-past-the-end append slot and crashing in at().
  function automatic bit empty_read_is_default;
    int q[$];
    return q[0] == 0;
  endfunction

  initial begin
    int q[$];
    if (!empty_read_is_default())
      $fatal(1, "empty queue read did not return the element default");
    q[0] = 7;
    $display("size=%0d value=%0d", q.size(), q[0]);
    $finish;
  end
endmodule

// CHECK: size=1 value=7
