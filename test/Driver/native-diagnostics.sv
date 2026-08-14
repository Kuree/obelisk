// RUN: obelisk -fno-lto -O0 %s -o %t.o0.native
// RUN: /bin/sh -c '"%t.o0.native" > "%t.o0.native.out" 2> "%t.o0.native.err"; test $? -eq 19'
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: /bin/sh -c '"%t.o0.bytecode" > "%t.o0.bytecode.out" 2> "%t.o0.bytecode.err"; test $? -eq 19'
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.err %t.o0.bytecode.err
// RUN: obelisk -fno-lto -O3 %s -o %t.o3.native
// RUN: /bin/sh -c '"%t.o3.native" > "%t.o3.native.out" 2> "%t.o3.native.err"; test $? -eq 19'
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: /bin/sh -c '"%t.o3.bytecode" > "%t.o3.bytecode.out" 2> "%t.o3.bytecode.err"; test $? -eq 19'
// RUN: diff -u %t.o3.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o3.native.err %t.o3.bytecode.err
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.err %t.o3.native.err
// RUN: FileCheck %s --check-prefix=STDOUT < %t.o3.native.out
// RUN: FileCheck %s --check-prefix=STDERR < %t.o3.native.err

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
// STDERR: INFO: {{.*}}native-diagnostics.sv:{{[0-9]+}}: value=1
// STDERR-NEXT: WARNING: {{.*}}native-diagnostics.sv:{{[0-9]+}}: $warning called.
// STDERR-NEXT: ERROR: {{.*}}native-diagnostics.sv:{{[0-9]+}}: continued
// STDERR-NEXT: FATAL: {{.*}}native-diagnostics.sv:{{[0-9]+}}: done
