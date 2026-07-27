// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

module native_indexed_part_select;
  bit [7:0] descending;
  bit [0:7] ascending;
  parameter int width = 3;

  initial begin
    descending = '0;
    descending[4 +: width] = 3'b111;
    $display("descending-up=%b", descending);
    descending = '0;
    descending[6 -: width] = 3'b111;
    $display("descending-down=%b", descending);

    ascending = '0;
    ascending[3 +: width] = 3'b111;
    $display("ascending-up=%b", ascending);
    ascending = '0;
    ascending[5 -: width] = 3'b111;
    $display("ascending-down=%b", ascending);
  end
endmodule

// CHECK: descending-up=01110000
// CHECK: descending-down=01110000
// CHECK: ascending-up=00011100
// CHECK: ascending-down=00011100
