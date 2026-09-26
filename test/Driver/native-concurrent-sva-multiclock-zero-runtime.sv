// RUN: obelisk --std=1800-2023 -O0 %s -o %t.o0.native
// RUN: obelisk --std=1800-2023 -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: obelisk --std=1800-2023 -O3 %s -o %t.o3.native
// RUN: obelisk --std=1800-2023 -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: obelisk --std=1800-2023 -O3 --native-scheduler=aot %s -o %t.o3.aot
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

module native_concurrent_sva_multiclock_zero_runtime;
  logic a = 0;
  logic b = 0;
  logic iff_a = 0;
  logic iff_b = 0;
  logic iff_enable = 0;
  int zero_pass = 0;
  int zero_fail = 0;
  int one_pass = 0;
  int one_fail = 0;
  int iff_pass = 0;
  int iff_fail = 0;

  a_zero: assert property (@(posedge a)
                           1'b1 ##0 @(posedge b) 1'b1)
    zero_pass++;
  else
    zero_fail++;

  a_one: assert property (@(posedge a)
                          1'b1 ##1 @(posedge b) 1'b1)
    one_pass++;
  else
    one_fail++;

  a_iff: assert property (@(posedge iff_a iff iff_enable)
                          1'b1 ##0 @(posedge iff_b) 1'b1)
    iff_pass++;
  else
    iff_fail++;

  initial begin
    // Both publication orders form one exact producer cohort.
    #1 begin a = 1; b = 1; end
    #1 begin a = 0; b = 0; end
    #1 begin b = 1; a = 1; end

    // This b edge is same simulation time but a later #0 producer boundary:
    // it is a legal strictly-later ##1 occurrence, never a ##0 coincidence.
    #1 begin a = 0; b = 0; end
    #1 a = 1;
    #0 b = 1;

    // A,A,B in one producer wave yields ordinal cohorts {A|B}, then {A}; the
    // repeated same-time source occurrence is not coalesced.
    #1 begin a = 0; b = 0; end
    #1 begin a = 1; a = 0; a = 1; b = 1; end

    // Complete every ##1 token still live above on its nearest later b edge.
    #1 b = 0;
    #1 b = 1;

    // The iff value is sampled at the source publication. A coincident edge
    // while false contributes only the destination bit; enabling it before a
    // later coincident wave admits exactly one source occurrence.
    #1 begin iff_a = 1; iff_b = 1; end
    #1 begin iff_a = 0; iff_b = 0; iff_enable = 1; end
    #1 begin iff_a = 1; iff_b = 1; end
    #1;
    $display("multiclock zero pass=%0d fail=%0d one_pass=%0d one_fail=%0d iff_pass=%0d iff_fail=%0d",
             zero_pass, zero_fail, one_pass, one_fail, iff_pass, iff_fail);
    $finish;
  end
endmodule

// CHECK: multiclock zero pass=3 fail=2 one_pass=5 one_fail=0 iff_pass=1 iff_fail=0
