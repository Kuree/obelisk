// RUN: obelisk -fno-lto -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out 2> %t.o0.native.err
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out 2> %t.o0.bytecode.err
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.err %t.o0.bytecode.err
// RUN: obelisk -fno-lto -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out 2> %t.o3.native.err
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out 2> %t.o3.bytecode.err
// RUN: diff -u %t.o3.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o3.native.err %t.o3.bytecode.err
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.err %t.o3.native.err
// RUN: FileCheck %s --check-prefix=STDOUT < %t.o3.native.out
// RUN: FileCheck %s --check-prefix=EMPTY --allow-empty < %t.o3.native.err

module native_finish_in_final;
  initial $display("body");

  final begin
    $display("final-request");
    $finish;
    $display("final-after");
  end

  final $display("final-other");
endmodule

// STDOUT: body
// STDOUT-DAG: final-request
// STDOUT-DAG: final-other
// STDOUT-NOT: final-after
// EMPTY-NOT: {{.}}
