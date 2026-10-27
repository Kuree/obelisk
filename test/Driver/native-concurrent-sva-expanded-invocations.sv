// RUN: talos --std=1800-2023 -O3 --native-scheduler=aot %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: FileCheck %s < %t.native.out
// RUN: talos --std=1800-2023 -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | diff -u %t.native.out -
// RUN: talos --std=1800-2023 -O3 --native-scheduler=aot -emit-llvm %s | FileCheck %s --check-prefix=AOT --implicit-check-not=talos_rt_v1_scheduler_run_aot_nodes

module native_concurrent_sva_expanded_invocations;
  logic clk = 0, alternate_clk = 0;
  logic request = 0, acknowledge = 0;
  logic typed_hit = 0, default_hit = 0, sequence_hit = 0;

  default clocking cb @(posedge clk); endclocking

  sequence response;
    acknowledge;
  endsequence

  sequence delayed(logic first, logic second = acknowledge);
    first ##1 second;
  endsequence

  property typed(event sampling, logic first, sequence consequent);
    @sampling first |=> consequent;
  endproperty

  property defaulted(logic first = request);
    first |=> acknowledge;
  endproperty

  // Slang has already substituted each formal in child zero. The trailing
  // scalar, event, sequence, and selected-default children are metadata and
  // must not be evaluated as extra monitor predicates.
  cover property (typed(posedge alternate_clk, request, response))
    typed_hit = 1;
  cover property (defaulted()) default_hit = 1;
  cover sequence (delayed(request)) sequence_hit = 1;

  initial begin
    #1 request = 1;

    // Start the explicit-event property attempt.
    #1 alternate_clk = 1;
    #1 alternate_clk = 0;
    #1 acknowledge = 1;
    #1 alternate_clk = 1;
    #1 alternate_clk = 0;

    // Start and complete the defaulted property and sequence calls.
    #1 acknowledge = 0;
    #1 clk = 1;
    #1 clk = 0;
    #1 acknowledge = 1;
    #1 clk = 1;
    #1 clk = 0;

    #1 $display("expanded typed=%0d default=%0d sequence=%0d",
                typed_hit, default_hit, sequence_hit);
    $finish;
  end
endmodule

// CHECK: expanded typed=1 default=1 sequence=1
// AOT: call i64 @talos_rt_v1_process_spawn
// AOT: call i32 @talos_rt_v1_scheduler_run(
