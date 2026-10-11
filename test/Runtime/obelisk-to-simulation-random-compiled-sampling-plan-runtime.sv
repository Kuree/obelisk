// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=PLAN
// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t +ntb_random_seed=13 | FileCheck %s
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.5/18.5.9: the live captures count and exponent
// specialize one compiled sampling plan, embedded in the solve operation.
// PLAN: simulation.random.solve
// PLAN-SAME: SAMP
// PLAN-SAME: PLAN

// The same plan serves zero, singleton, mid-popcount, full-popcount and
// impossible states of a 130-bit variable. The 130-bit cases are sparse enough
// to force residual sampling. A failed call (18.6.3) preserves the last
// assignment. Each line below is the verdict of one captured state.
// CHECK: bit 129 1
// CHECK-NEXT: popcount 65 1
// CHECK-NEXT: zero 1
// CHECK-NEXT: full 1
// CHECK-NEXT: negative count 1
// CHECK-NEXT: exponent past width 1

module top;
  bit [129:0] x = '0;
  int count = 0;
  int exponent = 0;
  int status;
  bit [129:0] kept;

  task automatic draw();
    status = std::randomize(x) with { $countones(x) == count;
                                      $clog2(x) == exponent; };
  endtask

  initial begin
    count = 1; exponent = 129; draw();
    $display("bit 129 %0d",
             status == 1 && x == 130'h200000000000000000000000000000000);

    count = 65; exponent = 130; draw();
    $display("popcount 65 %0d",
             status == 1 && $countones(x) == 65 && $clog2(x) == 130);

    count = 0; exponent = 0; draw();
    $display("zero %0d", status == 1 && x == 130'h0);

    count = 130; exponent = 130; draw();
    $display("full %0d",
             status == 1 && x == 130'h3ffffffffffffffffffffffffffffffff);

    kept = x;
    count = -1; exponent = 130; draw();
    $display("negative count %0d", status == 0 && x == kept);

    count = 1; exponent = 131; draw();
    $display("exponent past width %0d", status == 0 && x == kept);
  end
endmodule
