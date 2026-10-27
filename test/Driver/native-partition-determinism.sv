// RUN: rm -rf %t.serial %t.parallel && mkdir %t.serial %t.parallel
// RUN: talos -O1 -fno-lto --vpi=off --compile-threads=1 --mlir-timing %s -o %t.serial/sim 2> %t.serial.log
// RUN: FileCheck %s --check-prefix=PARTITIONS < %t.serial.log
// RUN: talos -O1 -fno-lto --vpi=off --compile-threads=8 %s -o %t.parallel/sim
// RUN: cmp %t.serial/sim %t.parallel/sim
// RUN: %t.serial/sim | FileCheck %s

// Keep enough live coroutine bodies to exercise native module partitioning
// (32 leaves; 16 give one module). -O1 partitions like -O3 at half the cost.
// Changing worker count must preserve optimization boundaries and the binary.
// Both links use one file name, which Mach-O records in the image.
// The ThinLTO link of the same design is checked in
// native-partition-determinism-thinlto.test.
// PARTITIONS: talos backend timing: partition 0
// PARTITIONS: talos backend timing: partition 1
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
