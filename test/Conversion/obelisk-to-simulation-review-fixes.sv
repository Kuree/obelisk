// RUN: obelisk -emit-sim %s | FileCheck %s

`timescale 1ns/1ps
module sim_review_fast;
  logic [7:0] value;

  initial begin
    value = '0;
    #5;
  end
endmodule

`timescale 10ns/1ns
module sim_review_slow;
  logic [7:0] lhs;
  logic [7:0] rhs;
  logic logical_result;
  logic logical_not_result;
  bit [7:0] two_state;
  bit two_state_not;
  logic alias_source;
  logic inout_source;
  logic input_source;
  logic function_result;

  function automatic logic observe(input logic scratch,
                                   output logic copied,
                                   inout logic exchanged);
    scratch = ~scratch;
    copied = 1'b1;
    exchanged = ~exchanged;
    return scratch ^ alias_source;
  endfunction

  always_comb begin
    logical_result = lhs && rhs;
    logical_not_result = !lhs;
    two_state_not = !two_state;
    rhs = lhs << 1;
  end

  initial begin
    #5;
    function_result = observe(input_source, alias_source, inout_source);
  end
endmodule

// CHECK: obelisk_sim.design @design attributes {time_precision_fs = 1000 : i64}
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK-DAG: obelisk_sim.time.constant 5000
// CHECK-DAG: obelisk_sim.time.constant 50000
// CHECK-DAG: obelisk_sim.logic.logical and
// CHECK-DAG: obelisk_sim.logic.unary logical_not
// CHECK-DAG: arith.cmpi eq
// CHECK-DAG: obelisk_sim.logic.shift left
// CHECK-DAG: -> (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>) attributes {entry_kind = 8 : i32}
// CHECK: {{%.*}}:3 = obelisk_sim.call
// CHECK: obelisk_sim.ref.store {{%.*}}#1
// CHECK: obelisk_sim.ref.store {{%.*}}#2
// CHECK-NOT: obelisk_sim.ref.alloc
