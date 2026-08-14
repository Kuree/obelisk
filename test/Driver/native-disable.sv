// RUN: obelisk -fno-lto -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -fno-lto -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o3.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: FileCheck %s < %t.o3.native.out

`timescale 1ns/1ps
module native_disable;
  task automatic guarded(input int id);
    begin : activation
      if (id == 1) begin
        #1;
        disable activation;
      end
      #2;
      $display("activation=%0d", id);
    end
  endtask

  task automatic hierarchical_worker(input int id);
    begin : target
      if (id == 1) begin
        #1;
        disable native_disable.hierarchical_worker.target;
      end
      #10;
      $display("hierarchical-worker-cancelled=%0d", id);
    end
    if (id == 1)
      $display("hierarchical-self-tail");
  endtask

  initial begin : named_outer
    fork
      begin #10; $display("named-cancelled"); end
      begin #1; disable named_outer; end
    join
    $display("named-tail");
  end

  initial begin
    fork
      guarded(1);
      guarded(2);
    join
    $display("activations-done");
  end

  initial begin
    fork
      hierarchical_worker(1);
      hierarchical_worker(2);
    join
    $display("hierarchical-self-done");
  end

  initial begin
    fork : named_fork
      begin #10; $display("named-fork-cancelled"); end
      begin #1; end
    join_any
    disable named_fork;
    $display("named-fork-done");
  end

  initial begin
    fork
      begin
        fork
          begin #10; $display("grandchild-cancelled"); end
        join_none
        #10;
      end
      begin #10; $display("child-cancelled"); end
    join_none
    #3;
    disable fork;
    wait fork;
    $display("disable-fork");
  end

  initial begin
    #4;
    $display("unrelated");
  end

  initial begin : hierarchical_target
    #10;
    $display("hierarchical-cancelled");
  end

  initial begin
    #1;
    disable native_disable.hierarchical_target;
  end

  initial begin
    #5;
    repeat (32) begin
      fork
        begin #10; end
      join_none
      disable fork;
      wait fork;
    end
    $display("cleanup-stress");
  end
endmodule

// CHECK-DAG: activation=2
// CHECK-DAG: activations-done
// CHECK-DAG: hierarchical-self-tail
// CHECK-DAG: hierarchical-self-done
// CHECK-DAG: named-fork-done
// CHECK: disable-fork
// CHECK: unrelated
// CHECK: cleanup-stress
// CHECK-NOT: cancelled
// CHECK-NOT: named-tail
