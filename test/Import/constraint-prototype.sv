// RUN: obelisk -emit-slang %s | FileCheck %s
// RUN: obelisk -emit-sim %s | FileCheck %s --check-prefix=RANDOMIZE

// A constraint prototype declares no body of its own: a `pure` constraint never
// has one, and an implicit extern prototype leaves it out of block. slang models
// that absence with an InvalidConstraint placeholder and reports nothing for the
// pure case, so importing the placeholder must not reject the compilation.

virtual class base;
  pure constraint c;
endclass

class derived extends base;
  rand int value;
  constraint c { value == 5; }
endclass

class implicit_extern;
  rand int value;
  constraint d;
endclass

module top;
  implicit_extern obj = new;
  initial begin
    if (obj.randomize())
      $display("randomized");
  end
endmodule

// The pure prototype imports as a declaration with an empty body.
// CHECK: slang.symbol.constraint_block
// CHECK-SAME: hierarchical_name = "base::c"
// CHECK-SAME: is_pure
// CHECK-NOT: slang.constraint.list

// The override in the derived class still carries its real body.
// CHECK: slang.symbol.constraint_block
// CHECK-SAME: hierarchical_name = "derived::c"
// CHECK: slang.constraint.list
// CHECK: slang.constraint.expression

// So does an implicit extern prototype whose body was never defined.
// CHECK: slang.symbol.constraint_block
// CHECK-SAME: hierarchical_name = "implicit_extern::d"
// CHECK-NOT: slang.constraint.list

// IEEE 1800-2023 18.5.1 gives an undefined implicit prototype the semantics of
// an empty constraint. It does not prevent executable randomization.
// RANDOMIZE: simulation.class.decl
// RANDOMIZE: simulation.func
