// RUN: %llvm_dist/bin/clang --target=x86_64-unknown-linux-gnu -fPIC -c %S/Inputs/dpi_impl.c -I%resource_dir/include -o %t.o
// RUN: obelisk %s %t.o -o %t.native
// RUN: %t.native | FileCheck %s --check-prefix=OUTPUT
// RUN: llvm-readelf --dyn-syms %t.native | FileCheck %s --check-prefix=EXPORTS --implicit-check-not=obelisk_rt_v1_

module dpi_driver;
  import "DPI-C" pure dpi_add = function int add(input int value);
  import "DPI-C" function longint dpi_scalars(
      input byte byte_value, input shortint short_value,
      input int int_value, input longint long_value,
      input bit bit_value, input logic logic_value,
      output int output_value, inout int inout_value);
  import "DPI-C" context task dpi_update(input logic [64:0] source,
                                         output bit [32:0] destination);
  import "DPI-C" function int dpi_unused(input int value);
  import "DPI-C" task dpi_logic_inout(inout logic [64:0] value);
  import "DPI-C" function void dpi_void(input int value,
                                         output int doubled);
  import "DPI-C" function real dpi_reals(
      input real source_value, input shortreal scale,
      output shortreal rounded, inout realtime accumulated);
  int result;
  longint scalar_result;
  int output_value;
  int inout_value;
  logic [64:0] source;
  bit [32:0] destination;
  logic [64:0] vector_value;
  int void_output;
  real real_result;
  shortreal rounded;
  realtime accumulated;

  initial begin
    result = add(7);
    inout_value = 5;
    scalar_result = dpi_scalars(1, 2, 3, 4, 1'b1, 1'bx,
                                output_value, inout_value);
    dpi_update(source, destination);
    vector_value = {1'b1, 60'b0, 4'b0zx1};
    dpi_logic_inout(vector_value);
    dpi_void(21, void_output);
    accumulated = 4.0;
    real_result = dpi_reals(2.5, 1.5, rounded, accumulated);
    if (vector_value === {1'b0, 60'b0, 4'b1xz0})
      $display("vector-ok");
    $display("%0d %0d %0d %0d %h", result, scalar_result,
             output_value, inout_value, destination);
    $display("void=%0d", void_output);
    $display("real=%0.2f rounded=%0.2f accumulated=%0.2f",
             real_result, rounded, accumulated);
  end
endmodule

// OUTPUT: vector-ok
// OUTPUT: 12 10 40 7 100000000
// OUTPUT: void=42
// OUTPUT: real=3.75 rounded=4.00 accumulated=4.25
// EXPORTS-DAG: svGetScope
// EXPORTS-DAG: svGetNameFromScope
