// RUN: obelisk -O3 %s -o %t.native
// RUN: %t.native > %t.native.out 2> %t.native.err
// RUN: FileCheck %s --check-prefix=STDOUT < %t.native.out
// RUN: FileCheck %s --check-prefix=STDERR < %t.native.err
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode 2> %t.bytecode.err | diff -u %t.native.out -
// RUN: diff -u %t.native.err %t.bytecode.err

module native_stop;
  initial begin
    $display("before-stop");
    // STDERR: $stop: {{.*}}native-stop.sv:[[# @LINE + 1]]: simulation time 0
    $stop;
    $display("after-stop");
  end

  initial begin
    #1;
    $display("late");
  end

  final $display("final");
endmodule

// STDOUT: before-stop
// STDOUT-NEXT: final
// STDOUT-NOT: after-stop
// STDOUT-NOT: late
// IEEE 1800-2017 20.2, Table 20-1: the default verbosity of 1 prints the
// simulation time and the location of the call.
// STDERR-NOT: {{.}}
