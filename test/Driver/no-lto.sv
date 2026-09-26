// Compare the default object runtime with LTO. Flag selection and precedence
// are checked directly in lto-options.test; they need no executable matrix.
// RUN: obelisk -O3 -flto %s -o %t.lto
// RUN: %t.lto > %t.lto.out
// RUN: obelisk -O3 %s -o %t.nolto
// RUN: %t.nolto > %t.nolto.out
// RUN: diff -u %t.lto.out %t.nolto.out
// RUN: FileCheck %s --check-prefix=STDOUT < %t.nolto.out

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
