// RUN: obelisk -fno-lto --std=1800-2023 -O0 --native-scheduler=generic %s -o %t.o0.native
// RUN: obelisk -fno-lto --std=1800-2023 -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: obelisk -fno-lto --std=1800-2023 -O3 --native-scheduler=generic %s -o %t.o3.native
// RUN: obelisk -fno-lto --std=1800-2023 -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: obelisk -fno-lto --std=1800-2023 -O3 --native-scheduler=aot %s -o %t.o3.aot
// RUN: %t.o0.native > %t.o0.native.out
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: %t.o3.native > %t.o3.native.out
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: %t.o3.aot > %t.o3.aot.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.aot.out
// RUN: FileCheck %s < %t.o0.native.out
// RUN: obelisk --std=1800-2023 -O3 --native-scheduler=aot -emit-llvm %s | FileCheck %s --check-prefix=AOT --implicit-check-not=obelisk_rt_v1_scheduler_run_aot_nodes

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
// AOT: call i32 @obelisk_rt_v1_scheduler_add_aot
// AOT: call i32 @obelisk_rt_v1_scheduler_run(
