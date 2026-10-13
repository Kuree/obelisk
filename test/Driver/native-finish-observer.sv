// RUN: obelisk -O3 %s -o %t.native
// RUN: %t.native > %t.native.out 2> %t.native.err
// RUN: FileCheck %s --check-prefix=STDOUT < %t.native.out
// RUN: FileCheck %s --check-prefix=STDERR < %t.native.err
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode 2> %t.bytecode.err | diff -u %t.native.out -
// RUN: diff -u %t.native.err %t.bytecode.err

module native_finish_observer;
  logic trigger;

  function automatic logic finish_observer(input logic value);
    if (value) begin
      $display("observer-finish");
      // STDERR: $finish: {{.*}}native-finish-observer.sv:[[# @LINE + 1]]: simulation time 1
      $finish;
      $display("observer-after");
    end
    return value;
  endfunction

  initial begin
    trigger = 1'b0;
    #1;
    trigger = 1'b1;
  end

  initial begin
    @(posedge finish_observer(trigger));
    $display("waiter-after");
  end

  final $display("final");
endmodule

// STDOUT: observer-finish
// STDOUT-NEXT: final
// STDOUT-NOT: observer-after
// STDOUT-NOT: waiter-after
// IEEE 1800-2017 20.2, Table 20-1: the default verbosity of 1 prints the
// simulation time and the location of the call.
// STDERR-NOT: {{.}}
