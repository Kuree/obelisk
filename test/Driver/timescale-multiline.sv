// RUN: obelisk %s -o %t.native
// RUN: %t.native | FileCheck %s
// RUN: obelisk --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s
// RUN: obelisk -O3 %s -o %t.native-o3
// RUN: %t.native-o3 | FileCheck %s
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.bytecode-o3
// RUN: %t.bytecode-o3 | FileCheck %s

// IEEE 1800-2017 5.4 and 22.7: comments replace whitespace and the
// `timescale directive has a token grammar, so its tokens need not share a
// physical source line.
/* lead */ `timescale /* after directive */ // continue
/* magnitude */ 1 /* before suffix */ // continue
/* suffix */ s /* before slash */ // continue
/* separator */ / /* before precision */ // continue
/* precision magnitude */ 100 /* before precision suffix */ // continue
/* precision suffix */ ms

module timescale_multiline;
  initial begin
    if ($timeunit != 0 || $timeprecision != -1)
      $fatal(1, "multiline timescale was not preserved");
    $display("TIMESCALE MULTILINE PASS");
  end
endmodule

// CHECK: TIMESCALE MULTILINE PASS
