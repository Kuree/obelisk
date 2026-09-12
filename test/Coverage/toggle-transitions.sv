module top;
  logic four_state;
  bit two_state;

  initial begin
    // Establish a known four-state baseline. X->0 is diagnostic only.
    four_state = 1'b0;
    four_state = 1'b1;
    two_state <= 1'b1;
    #1;
    four_state = 1'b0;
    two_state <= 1'b0;
    #1 $finish;
  end
endmodule

// This end-to-end test is needed in addition to pass-level tests: it proves
// that blocking publication and NBA commit both reach the central transition
// recorder in every execution tier and native scheduler specialization.
// RUN: obelisk -fno-lto -O0 --native-scheduler=generic --coverage=toggle %s -o %t.o0
// RUN: %t.o0 --coverage-output=%t.o0.obcov
// RUN: obelisk-cov report --format=text %t.o0.obcov -o %t.o0.txt
// RUN: FileCheck %s --check-prefix=TOGGLE < %t.o0.txt
// RUN: obelisk -fno-lto -O3 --native-scheduler=aot --coverage=toggle %s -o %t.aot
// RUN: %t.aot --coverage-output=%t.aot.obcov
// RUN: obelisk-cov report --format=text %t.aot.obcov -o %t.aot.txt
// RUN: diff -u %t.o0.txt %t.aot.txt
// RUN: obelisk -fno-lto -O3 --native-scheduler=eval --coverage=toggle %s -o %t.eval
// RUN: %t.eval --coverage-output=%t.eval.obcov
// RUN: obelisk-cov report --format=text %t.eval.obcov -o %t.eval.txt
// RUN: diff -u %t.o0.txt %t.eval.txt
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode --coverage=toggle %s -o %t.bytecode
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.o0.txt %t.bytecode.txt

// TOGGLE: toggle: 100.00% (4/4)
