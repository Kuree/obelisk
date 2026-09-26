// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o0.native > %t.o0.native.out
// RUN: %t.o3.native > %t.o3.native.out
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.bytecode.out
// RUN: FileCheck %s < %t.o0.native.out

// IEEE 1800-2017 10.6: force/release accepts nets, variables, constant net
// selects, and concatenations thereof; procedural assign/deassign accepts
// variables and concatenations of variables. Whole variables include fixed
// unpacked aggregates, dynamic containers, strings, and class handles.

class Item;
  int value;
  function new(int value);
    this.value = value;
  endfunction
endclass

class Holder;
  int source;
  int scalar;
  int fixed_source[2];
  int fixed_value[2];
  logic [3:0] logic_source[2];
  logic [3:0] logic_value[2];
  logic [7:0] concat_source;
  logic [7:0] concat_a;
  logic [7:0] concat_b;
  int array_source[];
  int array_value[];

  task run;
    source = 1;
    scalar = 0;
    force scalar = source;
    source = 5;
    #1;
    scalar = 99;
    $display("property-force=%0d", scalar);
    release scalar;

    assign scalar = source + 1;
    source = 7;
    #1;
    scalar = 99;
    $display("property-assign=%0d", scalar);
    deassign scalar;

    fixed_source = '{21, 22};
    fixed_value = '{0, 0};
    force fixed_value = fixed_source;
    fixed_value[0] = 99;
    fixed_source[1] = 23;
    #1;
    $display("property-fixed=%0d,%0d", fixed_value[0], fixed_value[1]);
    release fixed_value;

    logic_source = '{4'h1, 4'h2};
    logic_value = '{default: '0};
    force logic_value = logic_source;
    logic_value[0] = 4'hf;
    logic_source = '{4'hx, 4'ha};
    #1;
    $display("property-logic=%h,%h", logic_value[0], logic_value[1]);
    release logic_value;

    concat_source = 8'h31;
    concat_a = 0;
    concat_b = 0;
    force {concat_a, concat_b} = {concat_source, concat_source + 8'h01};
    concat_source = 8'h41;
    #1;
    concat_a = 8'hff;
    $display("property-concat-force=%h,%h", concat_a, concat_b);
    release {concat_a, concat_b};

    assign {concat_a, concat_b} = {concat_source, concat_source + 8'h02};
    concat_source = 8'h51;
    #1;
    $display("property-concat-assign=%h,%h", concat_a, concat_b);
    deassign {concat_a, concat_b};

    array_source = '{1, 2};
    array_value = '{0};
    force array_value = array_source;
    array_value[0] = 99;
    $display("property-array-masked=%0d,%0d", array_value[0],
             array_value[1]);
    array_source[0] = 7;
    #1;
    $display("property-array-updated=%0d,%0d", array_value[0],
             array_value[1]);
    release array_value;
  endtask
endclass

module native_procedural_override_general_targets;
  typedef struct {
    int number;
    byte tiny;
  } record_t;

  int fixed_source[2], fixed_value[2];
  record_t record_source, record_value;
  int dynamic_source[], dynamic_value[];
  int queue_source[$], queue_value[$];
  int assoc_source[int], assoc_value[int];
  string string_source, string_value;
  Item item_source, item_value;
  Holder holder;
  logic [7:0] left, right, concat_source;
  logic [7:0] driver_a, driver_b;
  wire [7:0] net_a = driver_a;
  wire [7:0] net_b = driver_b;

  initial begin
    fixed_source = '{1, 2};
    fixed_value = '{0, 0};
    force fixed_value = fixed_source;
    fixed_source[0] = 3;
    fixed_source[1] = 4;
    #1;
    fixed_value[0] = 99;
    $display("fixed=%0d,%0d", fixed_value[0], fixed_value[1]);
    release fixed_value;

    record_source = '{number: 5, tiny: 6};
    record_value = '{default: 0};
    force record_value = record_source;
    record_source.number = 7;
    #1;
    $display("record=%0d,%0d", record_value.number, record_value.tiny);
    release record_value;

    dynamic_source = '{8, 9};
    dynamic_value = '{0};
    force dynamic_value = dynamic_source;
    dynamic_value[0] = 99;
    dynamic_source[0] = 10;
    #1;
    $display("dynamic=%0d,%0d", dynamic_value[0], dynamic_value[1]);
    release dynamic_value;

    dynamic_source = '{11, 12};
    assign dynamic_value = dynamic_source;
    dynamic_value[0] = 99;
    dynamic_source[1] = 13;
    #1;
    $display("dynamic-assign=%0d,%0d", dynamic_value[0], dynamic_value[1]);
    deassign dynamic_value;

    queue_source = '{14, 15};
    queue_value = {};
    force queue_value = queue_source;
    queue_value.push_back(99);
    queue_source[1] = 16;
    #1;
    $display("queue=%0d,%0d,%0d", queue_value.size(), queue_value[0],
             queue_value[1]);
    release queue_value;

    assoc_source[2] = 17;
    assoc_value[2] = 0;
    force assoc_value = assoc_source;
    assoc_value[2] = 99;
    assoc_source[2] = 18;
    #1;
    $display("assoc=%0d,%0d", assoc_value.num(), assoc_value[2]);
    release assoc_value;

    string_source = "first";
    string_value = "zero";
    force string_value = string_source;
    string_source = "second";
    #1;
    $display("string=%s", string_value);
    release string_value;

    item_source = new(19);
    item_value = new(0);
    force item_value = item_source;
    item_source = new(20);
    #1;
    item_value = new(99);
    $display("class=%0d", item_value.value);
    release item_value;

    concat_source = 8'h21;
    left = 0;
    right = 0;
    force {left, right} = {concat_source, concat_source + 8'h01};
    concat_source = 8'h31;
    #1;
    left = 8'hff;
    $display("concat-force=%h,%h", left, right);
    release {left, right};

    assign {left, right} = {concat_source, concat_source + 8'h02};
    concat_source = 8'h41;
    #1;
    $display("concat-assign=%h,%h", left, right);
    deassign {left, right};

    driver_a = 0;
    driver_b = 0;
    concat_source = 8'h52;
    force {net_a[7:4], net_b[3:0]} = concat_source;
    concat_source = 8'h63;
    #1;
    $display("concat-net=%h,%h", net_a, net_b);
    release {net_a[7:4], net_b[3:0]};

    holder = new;
    holder.run();
  end
endmodule

// CHECK: fixed=3,4
// CHECK-NEXT: record=7,6
// CHECK-NEXT: dynamic=10,9
// CHECK-NEXT: dynamic-assign=11,13
// CHECK-NEXT: queue=2,14,16
// CHECK-NEXT: assoc=1,18
// CHECK-NEXT: string=second
// CHECK-NEXT: class=20
// CHECK-NEXT: concat-force=31,32
// CHECK-NEXT: concat-assign=41,43
// CHECK-NEXT: concat-net=60,03
// CHECK-NEXT: property-force=5
// CHECK-NEXT: property-assign=8
// CHECK-NEXT: property-fixed=21,23
// CHECK-NEXT: property-logic=x,a
// CHECK-NEXT: property-concat-force=41,42
// CHECK-NEXT: property-concat-assign=51,53
// CHECK-NEXT: property-array-masked=1,2
// CHECK-NEXT: property-array-updated=7,2
