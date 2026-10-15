// RUN: obelisk -emit-slang %s 2>/dev/null | FileCheck %s

// IEEE 1800-2023 14.16: the cycle delay on the right-hand side of a
// synchronous drive counts cycles of the target clockvar's own clocking
// block; only a procedural `##` prefix waits on default clocking (14.11).
// The importer therefore attaches the default clocking event to the prefix
// alone and leaves the drive's cycle delay with just its count.
module top;
  bit clk, slow, en, value;
  clocking cb @(posedge clk); output value; endclocking
  default clocking dcb @(negedge slow iff en); endclocking
  initial begin
    cb.value <= ##2 1;
    ##1 cb.value <= 0;
  end
endmodule

// CHECK: slang.timing.cycle_delay attributes {node_id =
// CHECK-NEXT: slang.expression.integer_literal attributes {constant_value = "2"
// CHECK-NEXT: {{^ *}}}{{$}}
// CHECK-NEXT: {{^ *}}}{{$}}
// CHECK: slang.timing.cycle_delay attributes {clocking_event_edge = 0 : i32, clocking_event_monitor, clocking_event_path = "top.dcb"
