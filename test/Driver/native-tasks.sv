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
// RUN: obelisk -emit-sim %s | FileCheck %s --check-prefix=SIM

`timescale 1ns/1ps
module native_tasks;
  event ready;
  int gate;

  task automatic arguments(
      input int input_value,
      output int output_value,
      inout int inout_value,
      ref int ref_value);
    #1;
    output_value = input_value + 1;
    inout_value = inout_value + 2;
    ref_value = ref_value + 3;
  endtask

  task automatic await_ready(output int value);
    @ready;
    wait (gate);
    value = 42;
  endtask

  task automatic nested(input int value, output int result);
    automatic int temporary;
    if (value == 0)
      result = 1;
    else begin
      nested(value - 1, temporary);
      result = temporary + value;
    end
  endtask

  task automatic timed_nested(input int value, output int result);
    automatic int temporary;
    #1;
    if (value == 0)
      result = 1;
    else begin
      timed_nested(value - 1, temporary);
      result = temporary + value;
    end
  endtask

  task automatic cancelled_nested(input int value, output int result);
    automatic int temporary;
    if (value == 0) begin
      #10;
      result = 1;
    end else begin
      #1;
      cancelled_nested(value - 1, temporary);
      result = temporary + value;
    end
  endtask

  task static bump(output int value);
    static int count = 5;
    count++;
    value = count;
  endtask

  task automatic choose(input int input_value, output int output_value);
    output_value = input_value + 1;
    if (input_value == 4)
      return;
    output_value = 99;
  endtask

  task automatic even(input int value, output int result);
    if (value == 0)
      result = 1;
    else
      odd(value - 1, result);
  endtask

  task automatic odd(input int value, output int result);
    if (value == 0)
      result = 0;
    else
      even(value - 1, result);
  endtask

  task static shared(input int value);
    #value;
    $display("shared=%0d", value);
  endtask

  initial begin
    int output_value;
    int inout_value;
    int ref_value;
    inout_value = 10;
    ref_value = 20;
    arguments(7, output_value, inout_value, ref_value);
    $display("args=%0d,%0d,%0d",
             output_value, inout_value, ref_value);
  end

  initial begin
    int value;
    fork
      begin
        await_ready(value);
        $display("event=%0d", value);
      end
      begin
        #2 -> ready;
        #1 gate = 1;
      end
    join
  end

  initial begin
    int recursive;
    int first;
    int second;
    int early;
    int mutual;
    nested(3, recursive);
    even(7, mutual);
    bump(first);
    bump(second);
    choose(4, early);
    $display("recursive=%0d mutual=%0d static=%0d,%0d early=%0d",
             recursive, mutual, first, second, early);
  end

  initial begin
    fork
      shared(2);
      shared(1);
    join
  end

  initial begin
    int timed;
    timed_nested(3, timed);
    $display("timed=%0d", timed);
  end

  initial begin
    int untouched;
    untouched = 7;
    begin : cancel_deep_call
      fork
        begin
          cancelled_nested(3, untouched);
          $display("cancelled-task-returned");
        end
      join_none
      #3;
      disable cancel_deep_call;
    end
    $display("cancel-copyout=%0d", untouched);
  end
endmodule

// CHECK: recursive=7 mutual=0 static=6,7 early=5
// CHECK-DAG: shared=1
// CHECK-DAG: args=8,12,23
// CHECK-DAG: shared=1
// CHECK: cancel-copyout=7
// CHECK: event=42
// CHECK: timed=7
// CHECK-NOT: cancelled-task-returned

// SIM-DAG: obelisk_sim.code_unit.decl {{[0-9]+}} in {{[0-9]+}} task hierarchy "native_tasks.arguments"
// SIM-DAG: obelisk_sim.func private @{{.*}} attributes {{.*}}entry_kind = 12 : i32
// SIM-DAG: obelisk_sim.task.call @
// SIM-DAG: obelisk_sim.static.once
