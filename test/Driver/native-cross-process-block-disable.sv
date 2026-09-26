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
module native_cross_process_block_disable;
  int initial_tail;
  int task_result;
  int same_process_result;
  int function_value;
  int loop_entries;

  task automatic child(output int value);
    #10 value = 99;
  endtask

  task automatic worker(output int value);
    begin : task_target
      value = 1;
      child(value);
      value = 2;
    end
    value = 3;
  endtask

  task automatic concurrent_worker(input int id);
    begin : target
      #10;
      $display("bad-concurrent=%0d", id);
    end
    $display("concurrent-tail=%0d t=%0t", id, $time);
  endtask

  task automatic same_process_helper(output int value);
    value = 99;
    disable native_cross_process_block_disable.same_process_owner.same_target;
    $display("bad-same-process-helper");
  endtask

  task automatic same_process_bridge(output int value);
    same_process_helper(value);
    $display("bad-same-process-bridge");
  endtask

  task automatic same_process_owner(output int value);
    begin : same_target
      value = 1;
      same_process_bridge(value);
      value = 2;
      $display("bad-same-process-target");
    end
    value = 3;
    $display("same-process-tail=%0d t=%0t", value, $time);
  endtask

  task automatic inactive_target_disable;
    disable native_cross_process_block_disable.same_process_owner.same_target;
    $display("inactive-target-tail t=%0t", $time);
  endtask

  function automatic void function_helper;
    disable native_cross_process_block_disable.function_target;
    $display("function-inactive-tail t=%0t", $time);
  endfunction

  function automatic int function_value_helper;
    disable native_cross_process_block_disable.function_value_target;
    return 99;
  endfunction

  initial begin
    begin : initial_target
      fork
        begin #8; $display("bad-descendant"); end
      join_none
      #10;
      $display("bad-initial");
    end
    initial_tail = 1;
    $display("initial-tail=%0d t=%0t", initial_tail, $time);
  end

  initial begin
    worker(task_result);
    $display("task-tail=%0d t=%0t", task_result, $time);
  end

  initial begin
    same_process_owner(same_process_result);
    $display("same-process-done=%0d t=%0t", same_process_result, $time);
    begin : function_target
      function_helper();
      $display("bad-function-target");
    end
    $display("function-owner-tail t=%0t", $time);
    #1;
    function_helper();
    function_value = 1;
    begin : function_value_target
      function_value = function_value_helper();
      $display("bad-function-value-target");
    end
    $display("function-value=%0d t=%0t", function_value, $time);
  end

  initial begin
    #1;
    inactive_target_disable();
  end

  initial begin
    fork
      concurrent_worker(1);
      concurrent_worker(2);
    join
    $display("concurrent-done t=%0t", $time);
  end

  initial begin
    begin : lexical_target
      fork
        begin #1; disable lexical_target; end
        begin #9; $display("bad-lexical-child"); end
      join
      $display("bad-lexical-body");
    end
    $display("lexical-tail t=%0t", $time);
  end

  always begin : repeating_target
    ++loop_entries;
    #10;
    $display("bad-loop");
  end

  initial begin
    #1;
    disable native_cross_process_block_disable.initial_target;
    disable native_cross_process_block_disable.worker.task_target;
    disable native_cross_process_block_disable.repeating_target;
    #1;
    disable native_cross_process_block_disable.repeating_target;
    disable native_cross_process_block_disable.concurrent_worker.target;
    #1;
    $display("loop-entries=%0d t=%0t", loop_entries, $time);
    $finish;
  end
endmodule

// CHECK-DAG: initial-tail=1 t=1000
// CHECK-DAG: task-tail=3 t=1000
// CHECK-DAG: same-process-tail=3 t=0
// CHECK-DAG: same-process-done=3 t=0
// CHECK-DAG: inactive-target-tail t=1000
// CHECK-DAG: function-owner-tail t=0
// CHECK-DAG: function-inactive-tail t=1000
// CHECK-DAG: function-value=1 t=1000
// CHECK-DAG: lexical-tail t=1000
// CHECK-DAG: concurrent-tail=1 t=2000
// CHECK-DAG: concurrent-tail=2 t=2000
// CHECK-DAG: concurrent-done t=2000
// CHECK-DAG: loop-entries=3 t=3000
// CHECK-NOT: bad-
