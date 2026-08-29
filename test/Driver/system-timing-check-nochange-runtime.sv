// RUN: obelisk -emit-sim %s -o - | FileCheck %s --check-prefix=SIM
// RUN: obelisk -fno-lto -O0 --native-scheduler=generic %s -o %t.o0.native
// RUN: %t.o0.native 2>&1 | FileCheck %s
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode 2>&1 | FileCheck %s
// RUN: obelisk -fno-lto -O0 --native-scheduler=aot %s -o %t.o0.aot
// RUN: %t.o0.aot 2>&1 | FileCheck %s
// RUN: obelisk -fno-lto -O3 --native-scheduler=generic %s -o %t.o3.native
// RUN: %t.o3.native 2>&1 | FileCheck %s
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode 2>&1 | FileCheck %s
// RUN: obelisk -fno-lto -O3 --native-scheduler=aot %s -o %t.o3.aot
// RUN: %t.o3.aot 2>&1 | FileCheck %s
// RUN: obelisk -O3 --native-scheduler=aot --top=nochange_cell \
// RUN:   -emit-llvm %s -o - | FileCheck %s --check-prefix=AOT

`timescale 1ns / 1ps

module nochange_cell(
    input wire reference, data,
    output reg notifier = 0);
  specify
    $nochange(posedge reference, data, 0, 0, notifier);
  endspecify
endmodule

module nochange_positive_cell(
    input wire reference, data,
    output reg notifier = 0);
  specify
    $nochange(posedge reference, data, 3, 2, notifier);
  endspecify
endmodule

module nochange_negative_cell(
    input wire reference, data,
    output reg notifier = 0);
  specify
    $nochange(posedge reference, data, -2, -2, notifier);
  endspecify
endmodule

module nochange_inverted_cell(
    input wire reference, data,
    output reg notifier = 0);
  specify
    $nochange(posedge reference, data, -10, -10, notifier);
  endspecify
endmodule

module nochange_condition_vector(
    input wire [1:0] reference, data,
    input wire [1:0] reference_enable, data_enable,
    output reg notifier = 0);
  specify
    $nochange(posedge reference &&& reference_enable,
              data &&& data_enable, 0, 0, notifier);
  endspecify
endmodule

module nochange_immediate_cell(
    input wire reference, data,
    output reg notifier = 0);
  specify
    $nochange(posedge reference, data, 0, 0, notifier);
  endspecify
endmodule

module nochange_history_cell(
    input wire reference, data,
    output reg notifier = 0);
  specify
    $nochange(posedge reference, data, 3, 0, notifier);
  endspecify
endmodule

module nochange_deferred_cell(
    input wire reference, data,
    output reg notifier = 0);
  specify
    $nochange(posedge reference, data, 0, -2, notifier);
  endspecify
endmodule

module nochange_negedge_cell(
    input wire reference, data,
    output reg notifier = 0);
  specify
    $nochange(negedge reference, data, 0, 0, notifier);
  endspecify
endmodule

module nochange_same_slot_cell #(
    parameter integer START = 0)(
    input wire reference, data,
    output reg notifier = 0);
  specify
    $nochange(posedge reference, data, START, 0, notifier);
  endspecify
endmodule

module nochange_overlap_no_notifier(
    input wire reference, data);
  specify
    $nochange(posedge reference, data, 0, 10);
  endspecify
endmodule

module system_timing_check_nochange_runtime;
  logic zero_ref = 0, zero_data = 0;
  logic positive_ref = 0, positive_data = 0;
  logic negative_ref = 0, negative_data = 0;
  logic inverted_ref = 0, inverted_data = 0;
  wire zero_notifier, positive_notifier, negative_notifier, inverted_notifier;
  logic [1:0] vector_ref = 0, vector_data = 0;
  logic [1:0] vector_ref_enable = 1, vector_data_enable = 0;
  wire vector_notifier;
  logic immediate_ref = 0, immediate_data = 0;
  logic history_ref = 0, history_data = 0;
  logic deferred_ref = 0, deferred_data = 0;
  logic negedge_ref = 1, negedge_data = 0;
  logic same_zero_ref = 0, same_zero_data = 0;
  logic same_positive_ref = 0, same_positive_data = 0;
  logic overlap_ref = 0, overlap_data = 0;
  wire immediate_notifier, history_notifier, deferred_notifier;
  wire negedge_notifier, same_zero_notifier, same_positive_notifier;

  nochange_cell zero(zero_ref, zero_data, zero_notifier);
  nochange_positive_cell positive(
      positive_ref, positive_data, positive_notifier);
  nochange_negative_cell negative(
      negative_ref, negative_data, negative_notifier);
  nochange_inverted_cell inverted(
      inverted_ref, inverted_data, inverted_notifier);
  nochange_condition_vector vector_check(
      vector_ref, vector_data, vector_ref_enable, vector_data_enable,
      vector_notifier);
  nochange_immediate_cell immediate_check(
      immediate_ref, immediate_data, immediate_notifier);
  nochange_history_cell history_check(
      history_ref, history_data, history_notifier);
  nochange_deferred_cell deferred_check(
      deferred_ref, deferred_data, deferred_notifier);
  nochange_negedge_cell negedge_check(
      negedge_ref, negedge_data, negedge_notifier);
  nochange_same_slot_cell #(.START(0)) same_zero_check(
      same_zero_ref, same_zero_data, same_zero_notifier);
  nochange_same_slot_cell #(.START(1)) same_positive_check(
      same_positive_ref, same_positive_data, same_positive_notifier);
  nochange_overlap_no_notifier overlap_check(overlap_ref, overlap_data);

  initial begin
    #1 immediate_ref = 1;
    #1 immediate_data = 1;
    // IEEE 1800-2017 31.4.6 makes this violation certain before the trailing
    // edge; Clause 31.6 requires its notifier update in the data slot.
    #0.001 $display("nochange-immediate %b", immediate_notifier);
    #1 immediate_ref = 0;
  end

  initial begin
    #1 history_data = 1;
    #2 history_ref = 1;
    // Positive start expansion discovers the prior data at the leading edge.
    #0.001 $display("nochange-history-immediate %b", history_notifier);
    #1 history_ref = 0;
  end

  initial begin
    #1 deferred_ref = 1;
    #1 deferred_data = 1;
    // A negative end can still exclude this event, so no early notifier.
    #0.001 $display("nochange-negative-before-trailing %b", deferred_notifier);
    #3 deferred_ref = 0;
    #0.001 $display("nochange-negative-after-trailing %b", deferred_notifier);
  end

  initial begin
    #1 negedge_ref = 0;
    #1 negedge_data = 1;
    #0.001 $display("nochange-negedge %b", negedge_notifier);
    #1 negedge_ref = 1;
  end

  initial begin
    #1 begin same_zero_ref = 1; same_zero_data = 1; end
    #0.001 $display("nochange-same-leading-zero %b", same_zero_notifier);
    #1 same_zero_data = 0;
    #1 begin same_zero_ref = 0; same_zero_data = 1; end
    #0.001 $display("nochange-same-trailing-zero %b", same_zero_notifier);
  end

  initial begin
    #1 begin same_positive_ref = 1; same_positive_data = 1; end
    #0.001 $display("nochange-same-leading-positive %b",
                    same_positive_notifier);
    #1 same_positive_ref = 0;
  end

  initial begin
    #1 overlap_ref = 1;
    #1 overlap_ref = 0;
    #1 overlap_ref = 1;
    #1 overlap_data = 1;
  end

  initial begin
    #1; zero_ref = 1; zero_data = 1;
    #1 zero_data = 0;
    #1; zero_ref = 0; zero_data = 1;
    #0.001 $display("nochange-zero %b", zero_notifier);
  end

  initial begin
    #1 positive_data = 1; // Exact begin endpoint: 4-3.
    #1 positive_data = 0; // Retained before the leading edge.
    #2; positive_ref = 1; positive_data = 1;
    #2 positive_ref = 0;
    #1 positive_data = 0;
    #1 positive_data = 1; // Exact end endpoint: 6+2.
    #0.001 $display("nochange-positive %b", positive_notifier);
  end

  initial begin
    #10 negative_ref = 1; // Begin is 12.
    #1 negative_data = 1;
    #2 negative_data = 0;
    #4 negative_data = 1;
    #1 negative_data = 0; // Exact end endpoint: 20-2.
    #2 negative_ref = 0;
    #0.001 $display("nochange-negative %b", negative_notifier);
  end

  initial begin
    #30 inverted_ref = 1; // Begin 40 is after end 25.
    #2 inverted_data = 1;
    #3 inverted_ref = 0;
    #0.001 $display("nochange-inverted %b", inverted_notifier);
  end

  initial begin
    #40 vector_ref = 2'b01;
    #1 vector_data = 2'b01; // Condition LSB is zero: no data event.
    #0.5 vector_data_enable = 1;
    #0.5 vector_data = 2'b11;
    #1 vector_ref = 0;
    #0.001 $display("nochange-condition-vector %b", vector_notifier);
    $finish;
  end
endmodule

// SIM: obelisk_sim.suspend.clock_set
// SIM-SAME: edges [1, 0, 2]
// SIM-SAME: slot_final
// SIM: obelisk_sim.assert.clock_occurrence.consume
// SIM: obelisk_sim.assert.nochange.update
// SIM-NOT: timing_check_table
// CHECK-DAG: nochange-zero 1
// CHECK-DAG: nochange-positive 1
// CHECK-DAG: nochange-negative 0
// CHECK-DAG: nochange-inverted 0
// CHECK-DAG: nochange-condition-vector 1
// CHECK-DAG: nochange-immediate 1
// CHECK-DAG: nochange-history-immediate 1
// CHECK-DAG: nochange-negative-before-trailing 0
// CHECK-DAG: nochange-negative-after-trailing 1
// CHECK-DAG: nochange-negedge 1
// CHECK-DAG: nochange-same-leading-zero 0
// CHECK-DAG: nochange-same-trailing-zero 1
// CHECK-DAG: nochange-same-leading-positive 1
// IEEE 1800-2017 31.6 reports twice because the data occurrence lies in the
// retained positive-end tail and the newly open interval.
// CHECK-DAG: warning: system timing check violation
// CHECK-DAG: warning: system timing check violation
// AOT: obelisk_rt_v1_nochange_update
