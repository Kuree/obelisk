// RUN: obelisk -fno-lto -O0 %s -o %t.o0.native
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: obelisk -fno-lto -O3 %s -o %t.o3.native
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o0.native > %t.o0.native.out
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: %t.o3.native > %t.o3.native.out
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.out %t.o3.bytecode.out
// RUN: FileCheck %s < %t.o0.native.out

// IEEE 1800-2017 6.6.8: interconnect is structural and each element of a
// fixed unpacked array independently infers its connected net-port type.

package interconnect_types;
  function automatic real sum(input real drivers[]);
    sum = 0.0;
    foreach (drivers[i])
      sum += drivers[i];
  endfunction
  nettype real resolved_real with sum;
endpackage

module logic_source(output wire logic value);
  assign value = 1'b1;
endmodule

module real_source(output interconnect_types::resolved_real value);
  assign value = 6.25;
endmodule

module logic_sink(input wire logic value);
  initial begin
    #1;
    if (value !== 1'b1)
      $fatal(1, "heterogeneous logic leaf mismatch");
  end
endmodule

module real_sink(input interconnect_types::resolved_real value);
  initial begin
    #1;
    if (value != 6.25)
      $fatal(1, "heterogeneous real leaf mismatch: %0.2f", value);
    $display("heterogeneous-interconnect: PASS");
    $finish;
  end
endmodule

module relay(input interconnect bus[0:1]);
  logic_sink logic_consumer(bus[0]);
  real_sink real_consumer(bus[1]);
endmodule

module native_interconnect_heterogeneous;
  interconnect bus[0:1];
  logic_source logic_driver(bus[0]);
  real_source real_driver(bus[1]);
  relay consumer(bus);
endmodule

// CHECK: heterogeneous-interconnect: PASS
