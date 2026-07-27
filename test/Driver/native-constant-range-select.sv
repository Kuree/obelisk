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
// RUN: obelisk --std=1800-2017 -O0 %s -o %t.2017.native
// RUN: %t.2017.native > %t.2017.native.out
// RUN: obelisk --std=1800-2017 -O0 --execution-tier=bytecode %s -o %t.2017.bytecode
// RUN: %t.2017.bytecode > %t.2017.bytecode.out
// RUN: diff -u %t.2017.native.out %t.2017.bytecode.out
// RUN: diff -u %t.o0.native.out %t.2017.native.out
// RUN: FileCheck %s < %t.o3.native.out

module native_constant_range_select;
  localparam int HIGH = 16;
  localparam int LOW = 8;

  logic [31:0] data;
  logic [8:0] parameter_result;
  logic [8:0] literal_result;

  assign parameter_result = data[HIGH:LOW];
  assign literal_result = data[16:8];

  initial begin
    data = '0;
    data[HIGH:LOW] = 9'h1ab;
    #1;
    $display("data=%08h parameter=%03h literal=%03h",
             data, parameter_result, literal_result);
  end
endmodule

// CHECK: data=0001ab00 parameter=1ab literal=1ab
