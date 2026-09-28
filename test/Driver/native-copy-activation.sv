// RUN: obelisk -O0 --native-scheduler=generic %s -o %t.generic
// RUN: %t.generic | FileCheck %s
// RUN: obelisk -O3 --native-scheduler=auto %s -o %t.auto
// RUN: %t.auto | FileCheck %s
// RUN: obelisk --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s

module copy_child(input var logic [64:0] source,
                  output var logic [64:0] sink);
  assign sink = source;
endmodule

module top;
  logic [64:0] source;
  wire [64:0] sink;
  int changes = 0;
  int saved_changes;
  copy_child child(source, sink);
  always @(sink) changes++;

  initial begin
    source = 65'h10000000000000001;
    #1;
    if (sink !== source) $fatal(1, "initial copy");
    saved_changes = changes;
    source = source;
    #1;
    if (changes != saved_changes) $fatal(1, "unchanged publication");
    source = 'x;
    #1;
    if (sink !== source) $fatal(1, "X copy");
    source = 'z;
    #1;
    if (sink !== source) $fatal(1, "Z copy");
    source = 65'h10000000000000003;
    #1;
    if (sink !== source) $fatal(1, "known recovery");
    force child.source = 65'h55;
    #1;
    if (sink !== 65'h55) $fatal(1, "forced input");
    source = 65'h10000000000000007;
    #1;
    if (sink !== 65'h55) $fatal(1, "force must mask port copy");
    release child.source;
    #1;
    if (sink !== source) $fatal(1, "release must restore port driver");
    $display("copy activation passed");
    $finish;
  end
endmodule

// CHECK: copy activation passed
