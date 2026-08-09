// RUN: obelisk --std=1800-2023 -O0 --native-scheduler=generic %s -o %t.generic-o0
// RUN: obelisk --std=1800-2023 -O3 --native-scheduler=generic %s -o %t.generic-o3
// RUN: obelisk --std=1800-2023 -O0 --execution-tier=bytecode %s -o %t.bytecode-o0
// RUN: %t.generic-o0 > %t.generic-o0.out
// RUN: %t.generic-o3 > %t.generic-o3.out
// RUN: %t.bytecode-o0 > %t.bytecode-o0.out
// RUN: diff -u %t.generic-o0.out %t.generic-o3.out
// RUN: diff -u %t.generic-o0.out %t.bytecode-o0.out
// RUN: FileCheck %s --check-prefix=OUTPUT < %t.generic-o0.out
// RUN: obelisk --std=1800-2023 -O0 --native-scheduler=generic -emit-llvm %s | FileCheck %s --check-prefix=GENERIC --implicit-check-not=__obelisk_bytecode_image_v1
// RUN: obelisk --std=1800-2023 -O3 --native-scheduler=aot -emit-llvm %s | FileCheck %s --check-prefix=AOT
// RUN: obelisk --std=1800-2023 -O0 --native-scheduler=generic -emit-llvm %S/declaration-initializers.sv | FileCheck %s --check-prefix=NO-SAMPLED --implicit-check-not=obelisk_rt_v1_native_state_sync --implicit-check-not=__obelisk_execution_extension_v1

module native_sampled_ranges;
  logic clk = 0;
  logic [7:0] prefix = 8'ha5;
  logic [2:0] stored = 3'b001;
  logic [3:0] net_driver = 4'h2;
  wire [3:0] net_value;
  assign net_value = net_driver;

  logic [2:0] sampled_stored;
  logic [3:0] sampled_net;
  always @(posedge clk) begin
    sampled_stored = $sampled(stored);
    sampled_net = $sampled(net_value);
    $display("sampled stored=%b net=%b", sampled_stored, sampled_net);
  end

  initial begin
    #1 begin stored = 3'b101; net_driver = 4'ha; end
    #1 clk = 1;
    #1 clk = 0;
    #1 begin stored = 3'b010; net_driver = 4'h3; end
    #1 clk = 1;
    #1 $finish;
  end
endmodule

// OUTPUT: sampled stored=101 net=1010
// OUTPUT-NEXT: sampled stored=010 net=0011

// GENERIC: @__obelisk_execution_descriptor_v1 = constant
// GENERIC-SAME: { i32 1, i32 32,
// GENERIC-SAME: i64 ptrtoint (ptr @__obelisk_execution_extension_v1 to i64), ptr null, i64 0,
// GENERIC: @__obelisk_execution_extension_v1 = internal constant { i32, i32, ptr, i64 } { i32 1, i32 24, ptr @__obelisk_sampled_ranges_v1, i64 2 }
// GENERIC: @__obelisk_sampled_ranges_v1 = internal constant [2 x { i64, i64, i64 }] [{ i64, i64, i64 } { i64 16, i64 0, i64 3 }, { i64, i64, i64 } { i64 23, i64 1, i64 4 }]

// AOT: @__obelisk_execution_descriptor_v1 = constant
// AOT-SAME: { i32 1, i32 33,
// AOT-SAME: i64 ptrtoint (ptr @__obelisk_execution_extension_v1 to i64),
// AOT: @__obelisk_execution_extension_v1 = internal constant { i32, i32, ptr, i64 } { i32 1, i32 24, ptr @__obelisk_sampled_ranges_v1, i64 2 }
// AOT: @__obelisk_sampled_ranges_v1 = internal constant [2 x { i64, i64, i64 }] [{ i64, i64, i64 } { i64 16, i64 0, i64 3 }, { i64, i64, i64 } { i64 23, i64 1, i64 4 }]

// NO-SAMPLED: @__obelisk_execution_descriptor_v1 = constant
// NO-SAMPLED-SAME: { i32 1, i32 0, i64 0, ptr null, i64 0,
