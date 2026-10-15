// RUN: obelisk -emit-slang %s | FileCheck %s

// IEEE 1800-2023 8.16, 8.26.5: a cast to or from an interface class succeeds
// when the referenced object's class implements the destination, so it is a
// runtime check (dynamic_cast_kind 3), as a downcast is. A final class has no
// subclass that could add an interface, and classes unrelated by inheritance
// share no objects, so those casts always fail (dynamic_cast_kind 1).
interface class I; endclass
interface class J; endclass
class F; endclass
class :final N; endclass
module top;
  I i; J j; F f; N n;
  initial begin
    // CHECK: callee_name = "$cast"
    // CHECK-SAME: dynamic_cast_kind = 3
    void'($cast(j, i));
    // CHECK: callee_name = "$cast"
    // CHECK-SAME: dynamic_cast_kind = 3
    void'($cast(j, f));
    // CHECK: callee_name = "$cast"
    // CHECK-SAME: dynamic_cast_kind = 3
    void'($cast(f, i));
    // CHECK: callee_name = "$cast"
    // CHECK-SAME: dynamic_cast_kind = 1
    void'($cast(j, n));
    // CHECK: callee_name = "$cast"
    // CHECK-SAME: dynamic_cast_kind = 1
    void'($cast(n, j));
    // CHECK: callee_name = "$cast"
    // CHECK-SAME: dynamic_cast_kind = 1
    void'($cast(f, n));
  end
endmodule
