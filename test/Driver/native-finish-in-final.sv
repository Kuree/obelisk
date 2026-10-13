// RUN: obelisk -O3 %s -o %t.native
// RUN: %t.native > %t.native.out 2> %t.native.err
// RUN: FileCheck %s --check-prefix=STDOUT < %t.native.out
// RUN: FileCheck %s --check-prefix=STDERR < %t.native.err
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode 2> %t.bytecode.err | diff -u %t.native.out -
// RUN: diff -u %t.native.err %t.bytecode.err

module native_finish_in_final;
  initial $display("body");

  final begin
    $display("final-request");
    // STDERR: $finish: {{.*}}native-finish-in-final.sv:[[# @LINE + 1]]: simulation time 0
    $finish;
    $display("final-after");
  end

  final $display("final-other");
endmodule

// STDOUT: body
// STDOUT: final-request
// STDOUT-NOT: final-after
// IEEE 1800-2017 9.2.3: $finish in a final procedure ends simulation
// immediately, so a later final procedure is not dispatched.
// STDOUT-NOT: final-other
// IEEE 1800-2017 20.2, Table 20-1: the default verbosity of 1 prints the
// simulation time and the location of the call.
// STDERR-NOT: {{.}}
