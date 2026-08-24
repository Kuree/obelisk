// RUN: obelisk -fno-lto -O0 %s -o %t.native-o0
// RUN: obelisk -fno-lto -O3 %s -o %t.native-o3
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.bytecode-o0
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.bytecode-o3
// RUN: %t.native-o0 > %t.native-o0.out
// RUN: %t.native-o3 > %t.native-o3.out
// RUN: %t.bytecode-o0 > %t.bytecode-o0.out
// RUN: %t.bytecode-o3 > %t.bytecode-o3.out
// RUN: diff -u %t.native-o0.out %t.native-o3.out
// RUN: diff -u %t.native-o0.out %t.bytecode-o0.out
// RUN: diff -u %t.native-o0.out %t.bytecode-o3.out
// RUN: FileCheck %s --check-prefix=OUTPUT < %t.native-o0.out
// RUN: not obelisk -fno-lto -DAUTOMATIC %s -o %t.automatic 2>&1 \
// RUN:   | FileCheck %s --check-prefix=AUTOMATIC

module force_alias_leaf(inout wire p);
endmodule

module force_driver;
  logic [3:0] value = 0;
  logic [3:0] driver = 0;
  wire [3:0] driven;
  assign driven = driver;
  wire alias_net;
  force_alias_leaf alias_child(alias_net);
  assign alias_net = 1'b0;

`ifdef AUTOMATIC
  initial begin
    automatic logic temporary = 0;
    force temporary = 1;
  end
`else
  initial begin
    value = 1;
    force value = 10;
    value = 3;
    $display("forced=%0d", value);
    release value;
    $display("released=%0d", value);
    value = 4;
    $display("stored=%0d", value);

    assign value = 5;
    value = 6;
    $display("assigned=%0d", value);
    force value = 9;
    assign value = 7;
    $display("priority=%0d", value);
    release value;
    $display("restore=%0d", value);
    deassign value;
    $display("deassign=%0d", value);
    value = 8;
    $display("after=%0d", value);

    assign value = 2;
    value <= 12;
    #1;
    $display("nba=%0d", value);
    deassign value;

    driver = 4'b1001;
    force driven[2:1] = 2'b11;
    #1;
    $display("net-forced=%b", driven);
    release driven[2:1];
    $display("net-released=%b", driven);

    $display("alias-before=%b/%b", alias_net, alias_child.p);
    force alias_child.p = 1'b1;
    $display("alias-forced=%b/%b", alias_net, alias_child.p);
    release alias_child.p;
    $display("alias-released=%b/%b", alias_net, alias_child.p);

    driver = 4'b0001;
    force alias_child.p = driver[0];
    force alias_net = driver[1];
    driver = 4'b0010;
    #1;
    $display("alias-dynamic=%b/%b", alias_net, alias_child.p);
    release alias_net;
    driver = 4'b0001;
    #1;
    $display("alias-dynamic-released=%b/%b", alias_net, alias_child.p);
  end
`endif
endmodule

// OUTPUT: forced=10
// OUTPUT-NEXT: released=10
// OUTPUT-NEXT: stored=4
// OUTPUT-NEXT: assigned=5
// OUTPUT-NEXT: priority=9
// OUTPUT-NEXT: restore=7
// OUTPUT-NEXT: deassign=7
// OUTPUT-NEXT: after=8
// OUTPUT-NEXT: nba=2
// OUTPUT-NEXT: net-forced=1111
// OUTPUT-NEXT: net-released=1001
// OUTPUT-NEXT: alias-before=0/0
// OUTPUT-NEXT: alias-forced=1/1
// OUTPUT-NEXT: alias-released=0/0
// OUTPUT-NEXT: alias-dynamic=1/1
// OUTPUT-NEXT: alias-dynamic-released=0/0

// AUTOMATIC: cannot refer to automatic variable 'temporary' from non-procedural context
