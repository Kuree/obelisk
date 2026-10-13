// RUN: obelisk -O3 %s -o %t.native
// RUN: not --status=19 %t.native > %t.native.out 2> %t.native.err
// RUN: FileCheck %s --check-prefix=STDOUT < %t.native.out
// RUN: FileCheck %s --check-prefix=STDERR < %t.native.err
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: not --status=19 %t.bytecode 2> %t.bytecode.err | diff -u %t.native.out -
// RUN: diff -u %t.native.err %t.bytecode.err

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
