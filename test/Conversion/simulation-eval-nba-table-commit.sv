// RUN: obelisk -emit-slang %s | obelisk-opt --test-obelisk-native-backend='scheduler=eval' -o %t.mlir
// RUN: FileCheck %s < %t.mlir

// IEEE 1800-2023 6.3.1 and 10.4.2: when eight or more scalar NBA roots would
// cost more as straight-line commits than as one bounded loop, the eval NBA
// commit walks constant per-root tables instead.
// The tables keep each root's state offset, its width mask, and its consumers.
// `g[7].q`, whose bit 0 two processes watch, also records the posedge (1) and
// negedge (2) of its two edge observers.
// CHECK-DAG: llvm.mlir.global internal constant @__obelisk_eval_nba_commit_offsets_v1(dense<[18, 19, 20, 24, 32, 40, 48, 56]>
// CHECK-DAG: llvm.mlir.global internal constant @__obelisk_eval_nba_commit_fixed_masks_v1(dense<[1, 127, 511, 131071, 8589934591, 9223372036854775807, -1, 1]>
// CHECK-DAG: llvm.mlir.global internal constant @__obelisk_eval_nba_commit_starts_v1(dense<[0, 1, 2, 3, 4, 5, 6, 7]>
// CHECK-DAG: llvm.mlir.global internal constant @__obelisk_eval_nba_commit_ends_v1(dense<[1, 2, 3, 4, 5, 6, 7, 10]>
// CHECK-DAG: llvm.mlir.global internal constant @__obelisk_eval_nba_commit_edges_v1(dense<[0, 0, 0, 0, 0, 0, 0, 0, 1, 2]>
// The dispatcher calls no per-root commit helper; the NBA commit indexes the
// offset table.
// CHECK-LABEL: llvm.func @__obelisk_eval_dispatch_v1(
// CHECK-NOT: @__obelisk_eval_nba_commit_
// CHECK: {{^  }}}
// A root's dirty bit is consumed only by its own commit. The other roots of
// its word, including a canonical accumulator staged by a runtime-executed
// actor, keep their bits for the runtime's index (IEEE 1800-2023 4.9.4,
// 10.4.2); clearing the whole word dropped such an update.
// CHECK-LABEL: llvm.func @__obelisk_aot_static_nba_commit_v1(
// CHECK: %[[OFFSETS:.*]] = llvm.mlir.addressof @__obelisk_eval_nba_commit_offsets_v1
// CHECK: %[[DIRTY:.*]] = llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1
// CHECK: %[[WORD_ADDRESS:.*]] = llvm.getelementptr %[[DIRTY]][%{{.*}}] : (!llvm.ptr, i64) -> !llvm.ptr, i64
// CHECK-NEXT: llvm.load %[[WORD_ADDRESS]]
// CHECK-NOT: llvm.store {{.*}}, %[[WORD_ADDRESS]]
// CHECK: %[[BIT:.*]] = "llvm.intr.cttz"
// The load and mask calculation are independent builder arguments; their
// emission order depends on the host compiler's argument evaluation order.
// CHECK-DAG: %[[CURRENT:.*]] = llvm.load %[[WORD_ADDRESS]]
// CHECK-DAG: %[[ONE_HOT:.*]] = llvm.shl %{{.*}}, %[[BIT]] : i64
// CHECK-DAG: %[[KEEP:.*]] = llvm.xor %[[ONE_HOT]], %{{.*}} : i64
// CHECK-NEXT: %[[REST:.*]] = llvm.and %[[CURRENT]], %[[KEEP]] : i64
// CHECK-NEXT: llvm.store %[[REST]], %[[WORD_ADDRESS]]
// CHECK: llvm.getelementptr %[[OFFSETS]][%{{.*}}] : (!llvm.ptr, i64) -> !llvm.ptr, i64

module native_nba_table_commit;
  bit clk = 0;
  logic [63:0] data = 0;
  bit enable = 1;
  wire [7:0] correct;
  initial forever #5 clk = ~clk;
  for (genvar i = 0; i < 8; ++i) begin : g
    localparam W = i % 7 == 0 ? 1 : i % 7 == 1 ? 7 :
                   i % 7 == 2 ? 9 : i % 7 == 3 ? 17 :
                   i % 7 == 4 ? 33 : i % 7 == 5 ? 63 : 64;
    logic [W-1:0] q;
    always @(posedge clk)
      if (enable || i == 7) q <= data[W-1:0];
    assign correct[i] = ^q;
  end
  integer rises = 0;
  integer falls = 0;
  always @(posedge g[7].q[0]) rises++;
  always @(negedge g[7].q[0]) falls++;
  initial begin
    #10 data = '1;
    #10 data = 'x;
    #10 begin enable = 0; data = 0; end
    #10 $display("%0d %0d %b", rises, falls, correct);
    $finish;
  end
endmodule
