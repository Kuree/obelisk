// RUN: %llvm_dist/bin/clang --target=x86_64-unknown-linux-gnu -fPIC -c \
// RUN:   %S/Inputs/dpi_impl.c \
// RUN:   -I$(obelisk --print-resource-dir)/include -o %t.o
// RUN: obelisk --dpi-link=%t.o %s -o %t.native
// RUN: %t.native | FileCheck %s --check-prefix=OUTPUT
// RUN: obelisk --execution-tier=bytecode --dpi-link=%t.o %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s --check-prefix=OUTPUT
// RUN: %llvm_dist/bin/llvm-ar rcs %t.a %t.o
// RUN: obelisk --dpi-link=%t.a %s -o %t.archive
// RUN: %t.archive | FileCheck %s --check-prefix=OUTPUT
// RUN: %llvm_dist/bin/clang --target=x86_64-unknown-linux-gnu -nostdlib \
// RUN:   -shared %t.o -o %t.so
// RUN: obelisk --dpi-link=%t.so %s -o %t.shared
// RUN: %t.shared | FileCheck %s --check-prefix=OUTPUT
// RUN: not obelisk %s -o %t.missing 2>&1 \
// RUN:   | FileCheck %s --check-prefix=MISSING

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
  int result;
  longint scalar_result;
  int output_value;
  int inout_value;
  logic [64:0] source;
  bit [32:0] destination;
  logic [64:0] vector_value;

  initial begin
    result = add(7);
    inout_value = 5;
    scalar_result = dpi_scalars(1, 2, 3, 4, 1'b1, 1'bx,
                                output_value, inout_value);
    dpi_update(source, destination);
    vector_value = {1'b1, 60'b0, 4'b0zx1};
    dpi_logic_inout(vector_value);
    if (vector_value === {1'b0, 60'b0, 4'b1xz0})
      $display("vector-ok");
    $display("%0d %0d %0d %0d %h", result, scalar_result,
             output_value, inout_value, destination);
  end
endmodule

// OUTPUT: vector-ok
// OUTPUT: 12 10 40 7 100000000
// MISSING-DAG: undefined symbol: dpi_add
// MISSING-DAG: undefined symbol: dpi_scalars
// MISSING-DAG: undefined symbol: dpi_update
// MISSING-DAG: undefined symbol: dpi_unused
// MISSING-DAG: undefined symbol: dpi_logic_inout
