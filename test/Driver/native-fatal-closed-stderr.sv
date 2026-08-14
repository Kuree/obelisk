// RUN: obelisk -fno-lto -O0 %s -o %t.o0.native
// RUN: /bin/sh -c '"%t.o0.native" > "%t.o0.native.out" 2>&-; test $? -eq 19'
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: /bin/sh -c '"%t.o0.bytecode" > "%t.o0.bytecode.out" 2>&-; test $? -eq 19'
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -fno-lto -O3 %s -o %t.o3.native
// RUN: /bin/sh -c '"%t.o3.native" > "%t.o3.native.out" 2>&-; test $? -eq 19'
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: /bin/sh -c '"%t.o3.bytecode" > "%t.o3.bytecode.out" 2>&-; test $? -eq 19'
// RUN: diff -u %t.o3.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: FileCheck %s --check-prefix=STDOUT < %t.o3.native.out

module native_fatal_closed_stderr;
  initial begin
    $display("before");
    $fatal;
    $display("after");
  end

  final $display("final");
endmodule

// STDOUT: before
// STDOUT-NEXT: final
// STDOUT-NOT: after
