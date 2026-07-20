// RUN: obelisk-translate %s | obelisk-opt --convert-moore-to-obelisk \
// RUN:   | obelisk-opt | FileCheck %s

module constructs(
  input  logic        clk,
  input  logic [15:0] data,
  input  bit   [7:0]  index,
  output logic [15:0] result
);
  timeunit 1ns;
  timeprecision 1ps;

  typedef struct packed {
    logic [3:0] tag;
    logic [11:0] payload;
  } packet_t;

  packet_t packet;
  logic [15:0] value;
  bit [15:0] bits;
  real real_value;
  string text;
  int queue[$:4];
  int associative[int];

  initial begin
    value = {2{data[7:0]}};
    value[index +: 4] = 4'hx;
    bits = value;
    value = bits;
    packet = '{tag: value[15:12], payload: value[11:0]};
    real_value = 1.25 + 2.5;
    text = "obelisk";
    queue.push_back(3);
    queue.push_front(2);
    void'(queue.pop_back());
    associative[1] = 42;
    associative.delete(1);
    if (value === 16'hxxxx)
      $display("%s %h", text, value);
    #5ns;
    @(posedge clk);
  end

  assign result = value;
endmodule

// CHECK-LABEL: obelisk.semantic.graph_symbol @constructs module
// CHECK: !obelisk.packed_struct<!hw.struct<
// CHECK: !sim.dstring
// CHECK: !sim.queue<i32, 4>
// CHECK: !obelisk.assoc<i32, i32>
// CHECK: obelisk.logic.extract
// CHECK: obelisk.logic.replicate
// CHECK: obelisk.ref.dyn_extract
// CHECK: obelisk.logic.to_bits
// CHECK: obelisk.logic.from_bits
// CHECK: push_back
// CHECK: assoc_array.delete
// CHECK: builtin.display
// CHECK: wait_delay
// CHECK: wait_event
// CHECK-NOT: moore.
