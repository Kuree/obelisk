// RUN: obelisk -fno-lto --std=1800-2023 -O0 --native-scheduler=generic %s -o %t.native
// RUN: obelisk -fno-lto --std=1800-2023 -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.native > %t.native.out
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: FileCheck %s < %t.native.out

module sampled_leaf;
  logic active = 0, endpoint = 0;
  int passes = 0, fails = 0;
  assert property (@(posedge endpoint) $future_gclk(active))
    begin passes++; $display("%m pass"); end
  else begin fails++; $display("%m FAIL"); end
endmodule

module sampled_subsystem;
  logic clk = 0;
  global clocking gcb @(posedge clk); endclocking
  sampled_leaf child();
endmodule

module l18_global_sampled_hierarchy;
  sampled_subsystem left();
  sampled_subsystem right();

  initial begin
    #1 begin left.child.endpoint = 1; right.child.endpoint = 1; end
    #1 begin
      left.child.endpoint = 0;
      right.child.endpoint = 0;
      left.child.active = 1;
      right.child.active = 1;
    end
    #1 left.clk = 1;
    #1 left.clk = 0;
    if (left.child.passes != 1 || right.child.passes != 0)
      $display("left isolation FAIL");
    #1 right.clk = 1;
    #1 right.clk = 0;
    if (left.child.passes != 1 || right.child.passes != 1 ||
        left.child.fails != 0 || right.child.fails != 0)
      $display("right isolation FAIL");
    $display("DONE %0d %0d", left.child.passes, right.child.passes);
    $finish;
  end
endmodule

// CHECK-DAG: l18_global_sampled_hierarchy.left.child pass
// CHECK-DAG: l18_global_sampled_hierarchy.right.child pass
// CHECK: DONE 1 1
// CHECK-NOT: FAIL
