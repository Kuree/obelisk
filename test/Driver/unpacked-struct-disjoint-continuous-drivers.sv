// RUN: obelisk -O0 --vpi=off %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk -O0 --vpi=off --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.bytecode.out %t.native.out
// RUN: FileCheck %s < %t.native.out

// IEEE 1800-2017 10.3.2 permits distinct continuous assignments to disjoint
// members of an unpacked aggregate variable.
module unpacked_struct_disjoint_continuous_drivers;
  typedef struct {
    string text;
    struct {
      bit flag;
      bit [3:0] nibble;
      bit [7:0] bytes[2];
    } nested;
  } aggregate_t;

  aggregate_t value;
  assign value.text = "ready";
  assign {value.nested.flag, value.nested.nibble} = {1'b1, 4'ha};
  assign value.nested.bytes[0] = 8'h12;
  assign value.nested.bytes[1] = 8'h34;

  initial begin
    #1;
    $display("%s %0d %0h %0h %0h", value.text, value.nested.flag,
             value.nested.nibble, value.nested.bytes[0],
             value.nested.bytes[1]);
    // CHECK: ready 1 a 12 34
  end
endmodule
