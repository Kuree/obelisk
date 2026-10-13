// Compare the default object runtime with LTO. Flag selection and precedence
// are checked directly in lto-options.test; they need no executable matrix.
// The LTO link also leaves unused DPI support out of the executable.
// RUN: obelisk -O3 -flto %s -o %t.lto
// RUN: %llvm_dist/bin/llvm-nm %t.lto | FileCheck %s --check-prefix=NO-DPI --allow-empty
// RUN: %t.lto > %t.lto.out
// RUN: obelisk -O3 %s -o %t.nolto
// RUN: %t.nolto | diff %t.lto.out -
// RUN: FileCheck %s --check-prefix=STDOUT < %t.lto.out

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
