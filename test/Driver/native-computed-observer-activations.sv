// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o3.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: FileCheck %s < %t.o3.native.out

`timescale 1ns/1ps
module native_computed_observer_activations;
  logic ref_left;
  logic ref_right;
  int ref_result;
  logic cancel_input;

  function automatic logic canceling_sample(input logic value);
    if (value)
      disable native_computed_observer_activations.callback_waiter;
    canceling_sample = value;
  endfunction

  task automatic observe_refs(
      ref logic left, ref logic right, output int result);
    wait (left && right);
    result = 42;
  endtask

  task automatic observe_automatic;
    logic left;
    logic right;
    left = 0;
    right = 0;
    fork
      begin
        #2;
        left = 1;
        right = 1;
        right = 0;
      end
    join_none
    wait (left && right);
    $display("automatic-captures=%0d", right);
    wait fork;
  endtask

  task automatic canceled_waiter;
    logic left;
    logic right;
    left = 0;
    right = 0;
    wait (left && right);
    $display("unexpected-canceled-wakeup");
  endtask

  initial begin
    ref_left = 0;
    ref_right = 0;
    #1;
    ref_left = 1;
    ref_right = 1;
    ref_right = 0;
  end

  initial begin
    observe_refs(ref_left, ref_right, ref_result);
    $display("ref-captures=%0d:%0d", ref_result, ref_right);
  end

  initial begin
    observe_automatic();
  end

  initial begin
    fork
      canceled_waiter();
    join_none
    #3;
    disable fork;
    wait fork;
    $display("canceled-cleanup");
  end

  initial begin
    cancel_input = 0;
    #4;
    cancel_input = 1;
  end

  initial begin : callback_waiter
    @(posedge canceling_sample(cancel_input));
    $display("unexpected-callback-wakeup");
  end

  initial begin
    #5;
    $display("callback-cancellation");
  end
endmodule

// CHECK: ref-captures=42:0
// CHECK-NEXT: automatic-captures=0
// CHECK-NEXT: canceled-cleanup
// CHECK-NEXT: callback-cancellation
// CHECK-NOT: unexpected
