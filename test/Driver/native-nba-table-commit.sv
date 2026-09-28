// RUN: obelisk -O3 --native-scheduler=auto -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --check-prefix=IR < %t.ll
// RUN: obelisk -O3 -fno-lto --compile-threads=8 --native-scheduler=auto %s -o %t.auto
// RUN: %t.auto | FileCheck %s
// RUN: obelisk -O0 --native-scheduler=auto %s -o %t.o0
// RUN: %t.o0 | FileCheck %s
// RUN: obelisk -O3 --native-scheduler=generic %s -o %t.generic
// RUN: %t.generic | FileCheck %s
// IR: @__obelisk_eval_nba_commit_offsets_v1
// IR-LABEL: define {{.*}}@__obelisk_eval_dispatch_v1(
// IR-NOT: @__obelisk_eval_nba_commit_
// IR: {{^}}}
// IR-LABEL: define {{.*}}@__obelisk_aot_static_nba_commit_v1(
// IR: getelementptr i64, ptr @__obelisk_eval_nba_commit_offsets_v1
// CHECK: table commits passed

module native_nba_table_commit;
  bit clk = 0;
  logic [63:0] data = 0;
  bit enable = 1;
  wire [64:0] correct;
  initial forever #5 clk = ~clk;
  for (genvar i = 0; i < 65; ++i) begin : g
    localparam W = i % 7 == 0 ? 1 : i % 7 == 1 ? 7 :
                   i % 7 == 2 ? 9 : i % 7 == 3 ? 17 :
                   i % 7 == 4 ? 33 : i % 7 == 5 ? 63 : 64;
    logic [W-1:0] q;
    always @(posedge clk)
      if (enable || i == 64) q <= data[W-1:0];
    assign correct[i] = q === (enable || i == 64 ? data[W-1:0] : W'(0));
  end
  integer rises = 0;
  integer falls = 0;
  always @(posedge g[64].q[0]) rises++;
  always @(negedge g[64].q[0]) falls++;
  initial begin
    #6; if (&correct !== 1'b1) $fatal(1, "initial");
    #4 data = '1;
    #6; if (&correct !== 1'b1) $fatal(1, "known");
    #4 data = 'x;
    #6; if (&correct !== 1'b1) $fatal(1, "X");
    #4 data = 'z;
    #6; if (&correct !== 1'b1) $fatal(1, "Z");
    #4 data = 0;
    #6; if (&correct !== 1'b1) $fatal(1, "recovery");
    #4 begin enable = 0; data = '1; end
    #6; if (&correct !== 1'b1) $fatal(1, "sparse");
    if (rises != 2 || falls != 3) $fatal(1, "edge counts %0d %0d", rises, falls);
    $display("table commits passed");
    $finish;
  end
endmodule
