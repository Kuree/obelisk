// RUN: obelisk -fno-lto -O3 --vpi=off %s -o %t
// RUN: %t | FileCheck %s

// A large cohort of scalar built-in primitives stays compact under the
// default native auto policy. The executable selects bytecode execution
// instead of materializing one LLVM coroutine per instance.
module primitive_large_cohort;
  localparam int N = 128;
  logic [N-1:0] data;
  logic [N-1:0] control;
  wire [N-1:0] result;

  genvar i;
  generate
    for (i = 0; i < N; ++i)
      nmos n0(result[i], data[i], control[i]);
  endgenerate

  initial begin
    data = '0;
    control = '1;
    #1 data[37] = 1'b1;
    #1;
    if (result !== data)
      $fatal(0, "large primitive cohort mismatch");
    $display("LARGE PRIMITIVE PASS");
  end
endmodule

// CHECK: LARGE PRIMITIVE PASS
