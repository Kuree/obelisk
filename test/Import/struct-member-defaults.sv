// RUN: obelisk -emit-slang %s | FileCheck %s

// IEEE 1800-2023 7.2.2: a member's constant initializer is part of an
// unpacked struct type's default value, so the importer records it with the
// member. Only a plain-data member has a default that is a fixed bit pattern;
// a string member's initializer stays with the variables that use the type.
module top;
  typedef struct {
    int v = 7;
    real r = 1.5;
    byte d[2] = '{4, 5};
    string s = "x";
  } S;
  S q[$];
endmodule

// CHECK: {initializer = "7", name = "v"
// CHECK-SAME: {initializer = "1.5", name = "r"
// CHECK-SAME: {initializer = "[8'sd4,8'sd5]", name = "d"
// CHECK-SAME: {name = "s", ordinal = 3
