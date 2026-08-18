// RUN: obelisk -fno-lto -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -fno-lto -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o3.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: FileCheck %s < %t.o0.native.out

module native_dynamic_array_types;
  string strings[] = '{"b", "a", "b"};
  string string_result[$];
  real reals[] = '{3.0, 1.0, 2.0, 1.0};
  real real_result[$];
  logic [3:0] logics[] = '{4'b0011, 4'bx001, 4'b0010};
  logic [3:0] logic_result[$];
  logic [0:0] unknown_a[] = '{1'bx, 1'b0};
  logic [0:0] unknown_b[] = '{1'bx, 1'b0};
  logic [0:0] mismatch[] = '{1'bx, 1'b1};

  initial begin
    string_result = strings.unique();
    strings.sort();
    real_result = reals.min();
    reals.sort();
    logic_result = logics.unique();
    logics.sort();
    $display("strings=%p unique=%p", strings, string_result);
    $display("reals=%p min=%p", reals, real_result);
    $display("logics=%p unique=%p", logics, logic_result);
    $display("equality unknown=%0d case=%0d mismatch=%0d",
             $isunknown(unknown_a == unknown_b), unknown_a === unknown_b,
             $isunknown(unknown_a == mismatch));
  end

  // IEEE 1800-2017 21.2.1.7 prints a plain integral pattern element "as they
  // would unformatted", which 21.2.1 makes the default decimal format of
  // $display; an element with only some bits unknown reports X, as %d does.
  // CHECK: strings='{"a", "b", "b"} unique='{"b", "a"}
  // CHECK-NEXT: reals='{1, 1, 2, 3} min='{1}
  // CHECK-NEXT: logics='{3, X, 2} unique='{3, X, 2}
  // CHECK-NEXT: equality unknown=1 case=1 mismatch=0
endmodule
