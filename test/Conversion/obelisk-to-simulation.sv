// RUN: obelisk -emit-obelisk %s > %t.semantic.mlir
// RUN: obelisk-opt %t.semantic.mlir --lower-obelisk-to-sim > %t.threaded.mlir
// RUN: obelisk-opt %t.semantic.mlir --lower-obelisk-to-sim --mlir-disable-threading > %t.single.mlir
// RUN: diff -u %t.single.mlir %t.threaded.mlir
// RUN: obelisk-opt %t.semantic.mlir '--lower-obelisk-to-sim=workers=2 vpi=read' | FileCheck %s --check-prefix=OPTIONS
// RUN: FileCheck %s --check-prefix=SIM < %t.threaded.mlir
// RUN: FileCheck %s --check-prefix=FINAL < %t.threaded.mlir
// RUN: FileCheck %s --check-prefix=SCCP --implicit-check-not=123456789 < %t.threaded.mlir
// RUN: obelisk -emit-sim %s | FileCheck %s --check-prefix=DRIVER
// RUN: obelisk -emit-sim %s | FileCheck %s --check-prefix=COPYBACK

`timescale 1ns/1ps
// OPTIONS: #obelisk_sim.graph<version = 1, vpi = read, workers = 2
module sim_child(output logic ready);
  initial ready = 1'b1;
endmodule

module sim_e2e;
  logic clk;
  logic ready;
  logic [7:0] input_value;
  logic [7:0] input_b;
  logic [7:0] state;
  logic signed [7:0] signed_a;
  logic signed [7:0] signed_b;
  logic signed_lt;
  logic mirror;
  wire [7:0] driven;

  sim_child child(ready);
  assign driven = input_value + input_b;

  function automatic logic [7:0] increment(
      input logic [7:0] value, output logic echoed);
    echoed = value[0];
    increment = value + 1;
  endfunction

  always_comb begin
    automatic logic [7:0] local_value = driven;
    state = local_value + input_b;
  end

  always_comb signed_lt = signed_a < signed_b;
  always_comb mirror = ready;

  always_ff @(posedge clk) begin
    if (ready)
      state <= increment(state, ready);
    else
      state <= 0;
  end

  initial begin
    automatic logic [7:0] delayed = input_value;
    automatic int index = 3;
    state[3:0] = {2{input_value[1:0]}};
    state[index +: 2] = input_value[index -: 2];
    while (state < 4) begin
      state = state + 1;
      if (state == 2)
        continue;
      if (state == 3)
        break;
    end
    case (state)
      0: state = 1;
      1,
      2: state = 3;
      default: state = 0;
    endcase
    #5;
    state = increment(delayed, ready);
  end

  always @(posedge clk or negedge ready)
    state <= input_b;
endmodule

// Time scaling, logical operators, shifts, and input/output/inout argument
// copy-back are part of the ordinary executable-boundary regression.
module sim_fast_timescale;
  logic [7:0] value;

  initial begin
    value = '0;
    #5;
  end
endmodule

`timescale 10ns/1ns
module sim_expression_and_arguments;
  logic [7:0] lhs;
  logic [7:0] rhs;
  logic logical_result;
  logic logical_not_result;
  bit [7:0] two_state;
  bit two_state_not;
  logic output_source;
  logic inout_source;
  logic input_source;
  logic function_result;

  function automatic logic observe(input logic scratch,
                                   output logic copied,
                                   inout logic exchanged);
    scratch = ~scratch;
    copied = 1'b1;
    exchanged = ~exchanged;
    return scratch ^ output_source;
  endfunction

  always_comb begin
    logical_result = lhs && rhs;
    logical_not_result = !lhs;
    two_state_not = !two_state;
    rhs = lhs << 1;
  end

  initial begin
    #5;
    function_result = observe(input_source, output_source, inout_source);
  end
endmodule

// Exercise the whole-program optimization stage in the production pipeline.
// The unused function must disappear with its private symbol, while the live
// function's argument and result fold across the call.
module sim_sccp_pipeline;
  bit folded_sink;

  function automatic bit fold_identity(input bit value);
    fold_identity = value;
  endfunction

  function automatic int unsigned unused_function(input bit value);
    unused_function = 123456789;
  endfunction

  initial folded_sink = fold_identity(1);
endmodule

// SIM: obelisk_sim.design @design attributes {{.*}}time_precision_fs = 1000 : i64
// SIM-DAG: obelisk_sim.scope.decl 0
// SIM-DAG: obelisk_sim.storage.decl
// SIM-DAG: obelisk_sim.net.decl
// SIM-DAG: obelisk_sim.driver.decl
// SIM-DAG: obelisk_sim.func @__obelisk_root
// SIM-DAG: obelisk_sim.spawn
// SIM-DAG: obelisk_sim.func private @unit_
// SIM-DAG: obelisk_sim.call @unit_
// SIM-DAG: obelisk_sim.ref.extract
// SIM-DAG: obelisk_sim.logic.replicate
// SIM-DAG: obelisk_sim.logic.compare slt
// SIM-DAG: obelisk_sim.time.constant 5000
// SIM-DAG: obelisk_sim.time.constant 50000
// SIM-DAG: obelisk_sim.logic.logical and
// SIM-DAG: obelisk_sim.logic.unary logical_not
// SIM-DAG: arith.cmpi eq
// SIM-DAG: obelisk_sim.logic.shift left
// SIM-DAG: -> (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>) attributes {{.*}}entry_kind = 8 : i32
// SIM-DAG: {{%.*}}:3 = obelisk_sim.call
// SIM-DAG: obelisk_sim.ref.store {{%.*}}#1
// SIM-DAG: obelisk_sim.ref.store {{%.*}}#2
// SIM-DAG: obelisk_sim.ref.dyn_extract
// SIM-DAG: obelisk_sim.logic.dyn_extract
// SIM-DAG: cf.cond_br
// SIM-DAG: obelisk_sim.nba.enqueue
// SIM-DAG: obelisk_sim.suspend.change
// SIM-DAG: obelisk_sim.suspend.edge posedge
// SIM-DAG: obelisk_sim.suspend.any
// SIM-DAG: obelisk_sim.suspend.delay {{.*}} to ^{{.*}}({{.*}}!obelisk_sim.logic<8>{{.*}})

// FINAL-NOT: obelisk.sv.
// FINAL-NOT: obelisk_sim.bindings
// FINAL-NOT: obelisk_sim.delay_scale
// FINAL-NOT: time_unit_fs
// FINAL-NOT: obelisk_sim.ref.alloc
// FINAL-NOT: obelisk_sim.func @unit_

// SCCP: obelisk_sim.func private @[[IDENTITY:unit_[0-9]+]]({{.*}}%arg1: i1
// SCCP-SAME: -> i1
// SCCP: %[[IDENTITY_RESULT:.*]] = arith.constant true
// SCCP: obelisk_sim.return %[[IDENTITY_RESULT]] : i1
// SCCP: obelisk_sim.func private @[[CALLER:unit_[0-9]+]]({{.*}}!obelisk_sim.ref<i1>
// SCCP: %[[FOLDED:.*]] = arith.constant true
// SCCP: obelisk_sim.call @[[IDENTITY]]
// SCCP: obelisk_sim.ref.store %[[FOLDED]]

// DRIVER: obelisk_sim.design @design
// DRIVER: obelisk_sim.func @__obelisk_root
// DRIVER-NOT: obelisk.sv.

// Input arguments are value-only. Only output and inout results are copied
// back before the function return value is stored.
// COPYBACK: obelisk_sim.time.constant 50000
// COPYBACK: obelisk_sim.suspend.delay
// COPYBACK: %[[INPUT_VALUE:.*]] = obelisk_sim.ref.load %[[INPUT_REF:.*]]
// COPYBACK: %[[CALL:.*]]:3 = obelisk_sim.call {{.*}}%[[INPUT_VALUE]]
// COPYBACK-NOT: obelisk_sim.ref.store {{.*}} to %[[INPUT_REF]]
// COPYBACK: obelisk_sim.ref.store %[[CALL]]#1
// COPYBACK-NOT: obelisk_sim.ref.store {{.*}} to %[[INPUT_REF]]
// COPYBACK-NEXT: obelisk_sim.ref.store %[[CALL]]#2
// COPYBACK-NOT: obelisk_sim.ref.store {{.*}} to %[[INPUT_REF]]
// COPYBACK-NEXT: obelisk_sim.ref.store %[[CALL]]#0
// COPYBACK-NOT: obelisk_sim.ref.store {{.*}} to %[[INPUT_REF]]
// COPYBACK-NEXT: obelisk_sim.return
