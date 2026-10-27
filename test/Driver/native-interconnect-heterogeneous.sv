// XFAIL: *
// Pristine Slang v11 rejects the qualified user-defined interconnect type.
// RUN: talos -O3 %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: FileCheck %s < %t.native.out
// RUN: talos -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | diff -u %t.native.out -

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
