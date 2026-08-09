// RUN: obelisk --std=1800-2023 -O0 %s -o %t.o0.native
// RUN: obelisk --std=1800-2023 -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: obelisk --std=1800-2023 -O3 %s -o %t.o3.native
// RUN: obelisk --std=1800-2023 -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: not obelisk --std=1800-2023 -O3 --native-scheduler=aot %s -o %t.o3.aot 2>&1 | FileCheck %s --check-prefix=AOT
// RUN: %t.o0.native > %t.o0.native.out
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: %t.o3.native > %t.o3.native.out
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.out %t.o3.bytecode.out
// RUN: FileCheck %s < %t.o0.native.out
// RUN: obelisk --std=1800-2023 -emit-obelisk %s -o %t.obelisk.mlir
// RUN: obelisk-opt %t.obelisk.mlir --pass-pipeline='builtin.module(obelisk-sim-prepare,obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s --check-prefix=SIM --implicit-check-not=obelisk_sim.control.enter --implicit-check-not=obelisk_sim.detached_controls
// RUN: obelisk --std=1800-2023 -O0 -emit-sim %s | FileCheck %s --check-prefix=FINAL --implicit-check-not=obelisk_sim.control.enter --implicit-check-not=obelisk_sim.detached_controls

module native_concurrent_sva;
  logic clk = 0;
  logic alternate_clk = 0;
  logic a = 0;
  logic b = 0;
  logic c = 0;

  default clocking cb @(posedge clk); endclocking

  // The label remains a stable assertion identity, but does not become a
  // procedural named-block activation that lives across clock waits.
  initial begin
    labeled_delay: restrict property (a ##2 b);
  end

  // Attempts start on two adjacent samples, exercising overlapping fixed
  // delay and repetition state in independent concurrent monitors.
  restrict property (a ##2 c);
  restrict property (a[*2] ##1 b);
  restrict property (a |=> b);
  restrict property (a |-> c);

  // An explicit clock is independent of the resolved default clock.
  restrict property (@(posedge alternate_clk) c);

  initial begin
    #1 begin a = 1; b = 0; c = 1; end
    #1 alternate_clk = 1;
    #1 alternate_clk = 0;

    // Default-clock samples one and two start overlapping attempts.
    #1 clk = 1;
    #1 clk = 0;
    #1 clk = 1;
    #1 clk = 0;

    // Samples three and four complete the two ##2 attempts.
    #1 begin a = 0; b = 1; end
    #1 clk = 1;
    #1 clk = 0;
    #1 clk = 1;
    #1 clk = 0;

    // Exercise misses as well as matches. Restrict is simulation-silent.
    #1 begin b = 0; c = 0; end
    #1 alternate_clk = 1;
    #1 alternate_clk = 0;
    #1 clk = 1;
    #1 clk = 0;
    #1 $display("restrict monitors completed");
    $finish;
  end
endmodule

// CHECK: restrict monitors completed
// AOT: design is ineligible for native AOT scheduling: non-Active process scheduling requires generic ordering
// SIM-DAG: obelisk_sim.assert.sampled_read
// SIM-DAG: obelisk_sim.assertion_path = "native_concurrent_sva.labeled_delay"
// SIM-DAG: obelisk_sim.assertion_target_id = {{[0-9]+}} : i64
// SIM-DAG: home_region = 8 : i32
// SIM-DAG: resume_region = 8 : i32
// SIM-DAG: obelisk_sim.ref.alloc
// SIM-DAG: arith.constant 2 : i64
// SIM-DAG: arith.constant 4 : i64
// SIM-DAG: obelisk_sim.ref.store
// FINAL-DAG: obelisk_sim.assertion_path = "native_concurrent_sva.labeled_delay"
// FINAL-DAG: home_region = 8 : i32
// FINAL-DAG: resume_region = 8 : i32
