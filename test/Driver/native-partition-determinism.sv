// RUN: obelisk -O3 -fno-lto --vpi=off --compile-threads=1 --mlir-timing %s -o %t.serial 2> %t.serial.log
// RUN: FileCheck %s --check-prefix=PARTITIONS < %t.serial.log
// RUN: obelisk -O3 -fno-lto --vpi=off --compile-threads=8 %s -o %t.parallel
// RUN: cmp %t.serial %t.parallel
// RUN: %t.serial | FileCheck %s
// RUN: rm -rf %t.lto.ordered.serial.thinlto-cache %t.lto.ordered.parallel.thinlto-cache
// RUN: obelisk -O3 -flto --vpi=off --compile-threads=1 %s -o %t.lto.ordered.serial
// RUN: obelisk -O3 -flto --vpi=off --compile-threads=8 %s -o %t.lto.ordered.parallel
// RUN: cmp %t.lto.ordered.serial %t.lto.ordered.parallel
// RUN: %t.lto.ordered.parallel | FileCheck %s
// RUN: obelisk -O3 -flto --vpi=off --compile-threads=1 %S/native.sv -o %t.full.serial
// RUN: obelisk -O3 -flto --vpi=off --compile-threads=8 %S/native.sv -o %t.full.parallel
// RUN: cmp %t.full.serial %t.full.parallel

// Keep enough live coroutine bodies to exercise native module partitioning.
// Changing worker count must preserve optimization boundaries and the binary.
// PARTITIONS: obelisk backend timing: partition 0
// PARTITIONS: obelisk backend timing: partition 1
// CHECK: sum=528

module native_partition_leaf #(parameter int ID = 0)(output int value);
  initial begin
    value = 0;
    #1;
    value = ID + 1;
  end
endmodule

module native_partition_determinism;
  wire [31:0] values[32];
  for (genvar i = 0; i < 32; i++) begin : g
    native_partition_leaf #(.ID(i)) leaf(values[i]);
  end
  initial begin
    int sum;
    #2;
    sum = 0;
    foreach (values[i]) sum += values[i];
    $display("sum=%0d", sum);
    $finish;
  end
endmodule
