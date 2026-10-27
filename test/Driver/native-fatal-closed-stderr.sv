// REQUIRES: native-elf
// RUN: talos -O3 %s -o %t.native
// RUN: not --status=19 %t.native > %t.native.out 2> /dev/full
// RUN: FileCheck %s --check-prefix=STDOUT < %t.native.out
// RUN: talos -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: not --status=19 %t.bytecode 2> /dev/full | diff -u %t.native.out -

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
