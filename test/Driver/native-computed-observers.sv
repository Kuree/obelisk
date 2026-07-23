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
module native_computed_observers;
  logic wait_left;
  logic wait_right;

  logic [3:0] values;
  int index;

  logic clock;
  logic selector;
  logic enable;
  logic qualifier;

  logic mixed_direct;
  logic mixed_left;
  logic mixed_right;

  logic [1:0] edge_vector;
  logic negedge_input;
  logic change_left;
  logic change_right;

  logic impure_input;
  logic impure_write_only;

  logic [1:0] order_bits;

  logic [1:0] nba_bits;
  logic [1:0] net_driver;
  wire [1:0] net_bits;
  assign net_bits = net_driver;

  event named;
  event deferred_named;
  event gated_named;
  event repeated_named;
  logic gated_enable;
  logic repeated_qualifier;

  function automatic logic nested_sample(input logic value);
    $display("observer-eval=%0d", value);
    nested_sample = value;
  endfunction

  function automatic logic impure_sample(input logic value);
    impure_write_only = value;
    impure_sample = nested_sample(value);
  endfunction

  function automatic logic ordered_sample(input logic value);
    $display("ordered-eval=%0d", value);
    ordered_sample = value;
  endfunction

  initial begin
    wait_left = 0;
    wait_right = 0;
    values = 0;
    index = 0;
    clock = 0;
    selector = 1;
    enable = 0;
    qualifier = 0;
    mixed_direct = 0;
    mixed_left = 0;
    mixed_right = 0;
    edge_vector = 0;
    negedge_input = 1;
    change_left = 0;
    change_right = 0;
    impure_input = 0;
    impure_write_only = 0;
    order_bits = 0;
    nba_bits = 0;
    net_driver = 0;
    gated_enable = 0;
    repeated_qualifier = 0;

    #1;
    wait_left = 1;
    wait_right = 1;
    wait_right = 0;

    #1;
    index = 1;
    values[0] = 1;
    values[1] = 1;

    #1;
    enable = 1;
    qualifier = 1;
    enable = 0;
    clock = 1;
    clock = 0;
    enable = 1;
    clock = 1;

    #1;
    mixed_left = 1;
    mixed_right = 1;

    #1;
    -> named;

    #1;
    edge_vector[1] = 1;
    edge_vector[0] = 1'bx;
    edge_vector[0] = 0;

    #1;
    negedge_input = 1'bx;
    negedge_input = 1;

    #1;
    change_left = 1'bx;
    change_left = 0;

    #1;
    impure_input = 1;

    #1;
    order_bits = 2'b11;

    #1;
    order_bits = 2'b01;

    #1;
    nba_bits <= 2'b11;

    #1;
    net_driver = 2'b11;

    #1;
    ->> deferred_named;

    #1;
    -> gated_named;

    #1;
    gated_enable = 1;

    #1;
    -> gated_named;

    #1;
    repeated_qualifier = 0;
    -> repeated_named;
    repeated_qualifier = 1;
    -> repeated_named;
  end

  initial begin
    wait (wait_left && wait_right);
    $display("computed-wait=%0d", wait_right);
  end

  initial begin
    @(values[index]);
    $display("dynamic-selection=%0d:%0d", index, values[index]);
  end

  initial begin
    @(posedge (clock & selector) iff (enable && qualifier));
    $display("computed-iff=%0d", enable && qualifier);
  end

  initial begin
    @(posedge mixed_direct or posedge (mixed_left & mixed_right));
    $display("mixed-list=%0d:%0d", mixed_direct,
             mixed_left & mixed_right);
  end

  initial begin
    wait (named.triggered);
    $display("named-triggered");
  end

  initial begin
    @(posedge (edge_vector ^ 2'b00));
    $display("vector-lsb-edge=%b", edge_vector);
  end

  initial begin
    @(negedge (negedge_input | change_right));
    $display("four-state-negedge=%b", negedge_input);
  end

  initial begin
    @(change_left ^ change_right);
    $display("four-state-change=%b", change_left);
  end

  initial begin
    @(posedge impure_sample(impure_input));
    $display("impure-observer=%0d", impure_write_only);
  end

  initial begin
    @(posedge ordered_sample(^order_bits));
    $display("ordered-observer");
  end

  initial begin
    @(posedge (&nba_bits));
    $display("nba-observer=%b", nba_bits);
  end

  initial begin
    @(posedge (&net_bits));
    $display("net-observer=%b", net_bits);
  end

  initial begin
    wait (deferred_named.triggered);
    $display("deferred-named-observer");
  end

  initial begin
    @(gated_named.triggered && gated_enable);
    $display("gated-named-observer");
  end

  initial begin
    @(repeated_named iff repeated_qualifier);
    $display("repeated-named-iff");
  end
endmodule

// CHECK: observer-eval=0
// CHECK-NEXT: ordered-eval=0
// CHECK-NEXT: computed-wait=0
// CHECK-NEXT: dynamic-selection=1:1
// CHECK-NEXT: computed-iff=1
// CHECK-NEXT: mixed-list=0:1
// CHECK-NEXT: named-triggered
// CHECK-NEXT: vector-lsb-edge=10
// CHECK-NEXT: four-state-negedge=1
// CHECK-NEXT: four-state-change=0
// CHECK-NEXT: observer-eval=1
// CHECK-NEXT: impure-observer=1
// CHECK-NEXT: ordered-eval=0
// CHECK-NEXT: ordered-eval=1
// CHECK-NEXT: ordered-observer
// CHECK-NEXT: nba-observer=11
// CHECK-NEXT: net-observer=11
// CHECK-NEXT: deferred-named-observer
// CHECK-NEXT: gated-named-observer
// CHECK-NEXT: repeated-named-iff
