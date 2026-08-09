// RUN: obelisk --std=1800-2023 -O0 %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk --std=1800-2023 -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk --std=1800-2023 -O3 %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk --std=1800-2023 -O3 --native-scheduler=aot %s -o %t.o3.aot
// RUN: %t.o3.aot | FileCheck %s
// RUN: obelisk --std=1800-2023 -O0 -emit-sim %s | FileCheck %s --check-prefix=SIM
// RUN: obelisk --std=1800-2023 -O0 -emit-llvm %s | FileCheck %s --check-prefix=LLVM-FLAG

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
    gated_past = $past(value, 1, gate);
    past_two = $past(value, 2);
    cycle++;
    case (cycle)
      1: begin
        assert (sampled_value === 1'b1);
        assert (past_one === 1'bx);
        assert (rose_value);
        assert (!fell_value);
        assert (!stable_value);
        assert (changed_value);
        assert (gated_past === 1'bx);
      end
      2: begin
        assert (sampled_value === 1'b0);
        assert (past_one === 1'b1);
        assert (!rose_value);
        assert (fell_value);
        assert (!stable_value);
        assert (changed_value);
        assert (gated_past === 1'b1);
      end
      3: begin
        assert (past_one === 1'b0);
        assert (past_two === 1'b1);
        assert (stable_value);
        assert (!changed_value);
        assert (gated_past === 1'b1);
      end
      4: begin
        assert (gated_past === 1'b0);
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
// SIM: obelisk_sim.assert.sampled_read
// SIM: obelisk_sim.assert.sampled_history
// LLVM-FLAG: @__obelisk_execution_descriptor_v1 = constant
// LLVM-FLAG-SAME: { i32 1, i32 33,
