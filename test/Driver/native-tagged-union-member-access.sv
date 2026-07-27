// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: /bin/sh -c '"%t.o0.native" > "%t.o0.native.out" 2> "%t.o0.native.err"; test $? -eq 19'
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: /bin/sh -c '"%t.o0.bytecode" > "%t.o0.bytecode.out" 2> "%t.o0.bytecode.err"; test $? -eq 19'
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.err %t.o0.bytecode.err
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: /bin/sh -c '"%t.o3.native" > "%t.o3.native.out" 2> "%t.o3.native.err"; test $? -eq 19'
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: /bin/sh -c '"%t.o3.bytecode" > "%t.o3.bytecode.out" 2> "%t.o3.bytecode.err"; test $? -eq 19'
// RUN: diff -u %t.o3.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o3.native.err %t.o3.bytecode.err
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.err %t.o3.native.err
// RUN: FileCheck %s --check-prefix=STDOUT < %t.o3.native.out
// RUN: FileCheck %s --check-prefix=STDERR < %t.o3.native.err

module native_tagged_union_member_access;
  typedef union tagged {
    void Invalid;
    int Valid;
  } tagged_int_t;

  tagged_int_t value;
  int result;

  initial begin
    value = tagged Valid(42);
    result = value.Valid;
    $display("valid=%0d", result);

    value = tagged Invalid;
    result = value.Valid;
    $display("after-invalid");
  end

  final $display("final");
endmodule

// STDOUT: valid=42
// STDOUT-NEXT: final
// STDOUT-NOT: after-invalid
// STDERR: FATAL: {{.*}}native-tagged-union-member-access.sv:{{[0-9]+}}: tagged union member access selected an inactive member.
