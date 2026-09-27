// RUN: obelisk -O0 --top=top -emit-sim %s -o - | FileCheck %s --check-prefix=SIM
// RUN: obelisk -O0 --top=top %s -o %t.o0.native
// RUN: obelisk -O0 --execution-tier=bytecode --top=top %s -o %t.o0.bytecode
// RUN: obelisk -O3 --top=top %s -o %t.o3.native
// RUN: obelisk -O3 --execution-tier=bytecode --top=top %s -o %t.o3.bytecode
// RUN: %t.o0.native > %t.o0.native.out
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: %t.o3.native > %t.o3.native.out
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.out %t.o3.bytecode.out
// RUN: FileCheck %s < %t.o0.native.out

module top;
  bit seed;
  bit [65535:0] bits;
  logic logic_seed;
  logic [65535:0] logic_bits;
  logic [7:0] byte_pattern;
  logic [1023:0] byte_bits;
  logic [6:0] unaligned_pattern;
  logic [69:0] unaligned_bits;
  logic [15:0] selected_source;
  logic [5:0] selected;
  int base;
  string text, repeated;
  int count;
  int words[2];
  byte bytes[4];

  initial begin
    // IEEE 1800-2017 11.4.12.1: the replication multiplier is constant, but
    // its expression is evaluated at run time. Wide packed replication must
    // stay compact in the compiler and execute for both state domains.
    seed = 1'b1;
    bits = {65536{seed}};
    assert (bits == '1);
    seed = 1'b0;
    bits = {65536{seed}};
    assert (bits == '0);
    logic_seed = 1'bx;
    logic_bits = {65536{logic_seed}};
    assert (logic_bits === {65536{1'bx}});
    byte_pattern = 8'ha5;
    byte_bits = {128{byte_pattern}};
    assert (byte_bits === {128{8'ha5}});
    unaligned_pattern = 7'b1010011;
    unaligned_bits = {10{unaligned_pattern}};
    assert (unaligned_bits === {10{7'b1010011}});

    // IEEE 1800-2017 11.5.1: an indexed part-select has a run-time base and a
    // constant width, including a window that is partially out of range.
    selected_source = 16'hc39a;
    base = 5;
    selected = selected_source[base +: 6];
    assert (selected == 6'b011100);
    base = -2;
    selected = selected_source[base +: 6];
    assert (selected === 6'b1010xx);

    // IEEE 1800-2017 11.4.12.1 permits a nonconstant multiplier for string
    // replication. The result is a string, not a packed replication.
    text = "ab";
    count = 3;
    repeated = {count{text}};
    assert (repeated == "ababab");

    // IEEE 1800-2017 10.10: unpacked array concatenation spreads an unpacked
    // item and converts every contributed element to the target element type.
    words = '{16'h1234, 16'h5678};
    bytes = {words, 16'h9abc, 8'hde};
    assert (bytes[0] == 8'h34 && bytes[1] == 8'h78 &&
            bytes[2] == 8'hbc && bytes[3] == 8'hde);
    $display("select concat replication matrix passed");
  end
endmodule

// SIM-COUNT-5: simulation.logic.replicate
// SIM: simulation.logic.dyn_extract
// SIM: simulation.string.repeat
// SIM: simulation.aggregate.construct
// CHECK-NOT: ERROR
// CHECK: select concat replication matrix passed
