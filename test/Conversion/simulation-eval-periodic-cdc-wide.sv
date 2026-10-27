// RUN: talos -emit-slang %s | talos-opt --test-talos-native-backend='scheduler=eval opt-level=2 timing=true' -o %t.mlir 2> %t.trace
// RUN: FileCheck %s --check-prefix=TRACE < %t.trace
// RUN: FileCheck %s < %t.mlir

// Seventy clocked stages on three periodic clocks (half periods 3, 5 and 7).
// Each stage samples the previous one, so adjacent stages cross clock domains
// through NBAs (IEEE 1800-2023 4.9.4). All three clocks enter one periodic
// plan, and the eval owners span more than one 64-bit ready word.
// TRACE: talos periodic signal proof: clocks=3
// TRACE: talos ranked groups: candidates=70 groups=3 segments=0 members=70 clocked_members=70
// TRACE: talos eval owner: bit=63 {{.*}} group=__talos_eval_ranked_group_
// TRACE-NEXT: talos eval owner: bit=64 {{.*}} group=__talos_eval_ranked_group_
// TRACE: talos eval owner: bit=69 {{.*}} group=__talos_eval_ranked_group_
// TRACE-NOT: talos eval owner:
// CHECK: llvm.mlir.global internal constant @__talos_periodic_clock_plan_v1() {{.*}} : !llvm.array<3 x
// CHECK: llvm.mlir.constant(3 : i64)
// CHECK: llvm.mlir.constant(5 : i64)
// CHECK: llvm.mlir.constant(7 : i64)
// CHECK: llvm.return
// The dispatcher also consumes owners from the second ingress word.
// CHECK-LABEL: llvm.func @__talos_eval_dispatch_v1(
// CHECK: %[[INGRESS:.*]] = llvm.mlir.addressof @__talos_aot_model_ingress_v1
// CHECK: llvm.getelementptr %[[INGRESS]][8] : (!llvm.ptr) -> !llvm.ptr, i8

module native_periodic_cdc_wide;
  bit [2:0] clocks;
  always #3 clocks[0] = ~clocks[0];
  always #5 clocks[1] = ~clocks[1];
  always #7 clocks[2] = ~clocks[2];
  bit [7:0] seed = 1;
  for (genvar i = 0; i < 70; ++i) begin : g
    bit [7:0] q;
    if (i == 0) always @(posedge clocks[0]) q <= seed + 1;
    else always @(posedge clocks[i % 3]) q <= g[i - 1].q + 1;
  end
  initial #301 $finish;
endmodule
