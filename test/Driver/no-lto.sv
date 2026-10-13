// The default links the object runtime archive; -flto instead links the
// runtime bitcode with the design. LTO above -O0 must still produce a working
// executable, and must leave unused DPI support out of it. -O1 takes the same
// link path as -O3 at half the cost; flag selection and precedence are checked
// in lto-options.test.
// RUN: obelisk -O1 -flto %s -o %t.lto
// RUN: %llvm_dist/bin/llvm-nm %t.lto | FileCheck %s --check-prefix=NO-DPI --allow-empty
// RUN: %t.lto | FileCheck %s --check-prefix=STDOUT

module no_lto;
  int accumulator;
  initial begin
    for (int index = 0; index < 8; index++)
      accumulator += index * index;
    $display("accumulator=%0d", accumulator);
    #5 $display("time=%0t", $time);
    $finish;
  end
endmodule

// STDOUT: accumulator=140
// STDOUT: time=5

// NO-DPI-NOT: obelisk_rt_v1_dpi_
// NO-DPI-NOT: svAckDisabledState
// NO-DPI-NOT: svGetBitselBit
// NO-DPI-NOT: svGetScope
// NO-DPI-NOT: svIsDisabledState
