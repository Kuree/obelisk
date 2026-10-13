// RUN: obelisk -O3 %s -o %t.native
// RUN: not --status=19 %t.native > %t.native.out 2> %t.native.err
// RUN: FileCheck %s --check-prefix=STDOUT < %t.native.out
// RUN: FileCheck %s --check-prefix=STDERR < %t.native.err
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: not --status=19 %t.bytecode 2> %t.bytecode.err | diff -u %t.native.out -
// RUN: diff -u %t.native.err %t.bytecode.err

module native_diagnostics;
  function automatic int fatal_inner();
    $fatal(0, "done");
    $display("fatal-inner-after");
    return 7;
  endfunction

  function automatic int fatal_outer();
    int value;
    value = fatal_inner();
    $display("fatal-outer-after value=%0d", value);
    return value;
  endfunction

  initial begin
    int value;
    $info("value=%0d", 1);
    $warning;
    $error("continued");
    $display("after-error");
    value = fatal_outer();
    $display("after-fatal value=%0d", value);
  end

  final $display("final");
endmodule

// STDOUT: after-error
// STDOUT-NEXT: final
// STDOUT-NOT: fatal-inner-after
// STDOUT-NOT: fatal-outer-after
// STDOUT-NOT: after-fatal
// STDERR: INFO: {{.*}}native-diagnostics.sv:{{[0-9]+}}: native_diagnostics: simulation time 0: value=1
// STDERR-NEXT: WARNING: {{.*}}native-diagnostics.sv:{{[0-9]+}}: native_diagnostics: simulation time 0: $warning called.
// STDERR-NEXT: ERROR: {{.*}}native-diagnostics.sv:{{[0-9]+}}: native_diagnostics: simulation time 0: continued
// STDERR-NEXT: FATAL: {{.*}}native-diagnostics.sv:{{[0-9]+}}: native_diagnostics.fatal_inner: simulation time 0: done
