// RUN: obelisk -O3 --native-scheduler=auto -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --check-prefix=LLVM < %t.ll
// RUN: obelisk -O3 --native-scheduler=auto %s -o %t.native
// RUN: obelisk -O0 --native-scheduler=generic %s -o %t.reference
// RUN: %t.native > %t.native.out
// RUN: %t.reference > %t.reference.out
// RUN: diff -u %t.reference.out %t.native.out
// RUN: FileCheck %s < %t.native.out

// LRM 6.11.2, 6.21, 7.3.1: promotion of an automatic mixed packed union
// preserves X/Z in its backing value, converts them to zero on a two-state
// member read, and clears only the unknown bits overwritten by that member.
module native_tier1_mixed_packed_union;
  typedef union packed {
    logic [15:0] four;
    bit [15:0] two;
  } mixed_t;
  function automatic logic [31:0] convert(input logic [15:0] seed,
                                          input logic [3:0] index);
    mixed_t value;
    bit [3:0] previous;
    value.four = seed;
    previous = value.two[index +: 4];
    value.two[7:4] = 4'ha;
    return {value.four, 12'b0, previous};
  endfunction

  bit clk;
  bit [3:0] index;
  always #5 clk = ~clk;
  always @(posedge clk) index <= index + 1;
  logic [31:0] result[128];
  for (genvar i = 0; i < 128; i++)
    always_comb result[i] = convert(16'hzx91, index);
  initial begin
    #1 $display("first %h", result[0]);
    #10 $display("second %h", result[0]);
    #10 $display("third %h", result[0]);
    $display("invalid %h", convert(16'hxz09, 4'hx));
    $finish;
  end
endmodule

// LLVM-NOT: call i32 @obelisk_rt_v1_scheduler_execute_aot_actor
// LLVM: define {{.*}}i32 @__obelisk_eval_dispatch_v1
// CHECK: first zxa10001
// CHECK-NEXT: second zxa10008
// CHECK-NEXT: third zxa10004
// CHECK-NEXT: invalid xza90000
