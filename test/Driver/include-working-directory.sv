// RUN: %split-file %s %t
// RUN: cd %t/work && talos -emit-slang ../src/top.sv | FileCheck %s

// IEEE 1800-2023 22.4: a quoted relative `include searches the compiler's
// current working directory, not only the including file's directory.
// CHECK: slang.symbol.parameter @{{.*}}V attributes {constant_value = "7"

//--- src/top.sv
`include "header.svh"
module top;
  localparam int V = `VALUE;
endmodule

//--- work/header.svh
`define VALUE 7
