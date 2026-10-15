// RUN: obelisk --std=1800-2023 -O0 --vpi=read --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk --std=1800-2023 -O3 %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk --std=1800-2023 -O3 --native-scheduler=aot -DAOT %s -o %t.o3.aot
// RUN: %t.o3.aot | FileCheck %s
// RUN: obelisk --std=1800-2023 -O0 -emit-sim %s | FileCheck %s --check-prefix=SIM
// RUN: obelisk --std=1800-2023 -O0 -emit-llvm %s | FileCheck %s --check-prefix=LLVM-FLAG

// The $past gate qualifies its sampler's clock edge (IEEE 1800-2023
// 16.9.3), a conditional wait that native AOT scheduling does not admit.
`ifdef AOT
`define CHECK_GATED(expected)
`else
`define CHECK_GATED(expected) assert (gated_past === expected);
`endif

module native_sampled_values;
  logic clk = 0;
  logic value = 0;
  logic gate = 1;
  int cycle = 0;
  logic sampled_value, past_one, rose_value, fell_value;
  logic stable_value, changed_value, gated_past, past_two;

  always @(posedge clk) begin
    sampled_value = $sampled(value);
    past_one = $past(value);
    rose_value = $rose(value);
    fell_value = $fell(value);
    stable_value = $stable(value);
    changed_value = $changed(value);
`ifndef AOT
    gated_past = $past(value, 1, gate);
`endif
    past_two = $past(value, 2);
    cycle++;
    case (cycle)
      1: begin
        // IEEE 1800-2023 16.5.1: before the first tick, $past reads the
        // default sampled value, which for `value` is its declaration's 0.
        assert (sampled_value === 1'b1);
        assert (past_one === 1'b0);
        assert (rose_value);
        assert (!fell_value);
        assert (!stable_value);
        assert (changed_value);
        `CHECK_GATED(1'b0)
      end
      2: begin
        assert (sampled_value === 1'b0);
        assert (past_one === 1'b1);
        assert (!rose_value);
        assert (fell_value);
        assert (!stable_value);
        assert (changed_value);
        `CHECK_GATED(1'b1)
      end
      3: begin
        assert (past_one === 1'b0);
        assert (past_two === 1'b1);
        assert (stable_value);
        assert (!changed_value);
        `CHECK_GATED(1'b1)
      end
      4: begin
        `CHECK_GATED(1'b0)
        $display("SAMPLED PASS");
      end
    endcase
  end

  initial begin
    #4 value = 1;
    #1 clk = 1;
    #1 clk = 0;
    #3 begin value = 0; gate = 0; end
    #1 clk = 1;
    #1 clk = 0;
    #3 gate = 1;
    #1 clk = 1;
    #1 clk = 0;
    #3 value = 1;
    #1 clk = 1;
    #1 $finish;
  end
endmodule

// CHECK: SAMPLED PASS
// SIM: simulation.assert.sampled_read
// SIM: simulation.assert.clocked_sample_update
// SIM: simulation.assert.clocked_sample_read
// LLVM-FLAG: @__obelisk_execution_descriptor_v1 = constant
// LLVM-FLAG-SAME: { i32 1, i32 1056, i64 120,
// LLVM-FLAG-SAME: { i32 1, i32 72, ptr @__obelisk_sampled_ranges_v1, i64 1,
// LLVM-FLAG: @__obelisk_sampled_ranges_v1 = internal constant [1 x { i64, i64, i64 }] [{ i64, i64, i64 } { i64 8, i64 0, i64 1 }]
