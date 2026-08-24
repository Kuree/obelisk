// RUN: obelisk -fno-lto -O0 %s -o %t.native
// RUN: %t.native | FileCheck %s
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s
// RUN: obelisk -fno-lto -O3 %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

module native_fixed_array_ordering_compact;
  int values[4096];
  int total;

  initial begin
    foreach (values[index])
      values[index] = index;
    // IEEE 1800-2017 7.12.2 applies ordering methods to fixed unpacked
    // arrays. Lowering this extent must remain counted rather than emitting
    // one compiler operation for each element.
    values.reverse();
    assert (values[0] == 4095 && values[4095] == 0);
    total = values[0] + values[4095];
    assert (total == 4095);
    $display("fixed-array-ordering-compact-pass");
    $finish;
  end
endmodule

// CHECK: fixed-array-ordering-compact-pass
