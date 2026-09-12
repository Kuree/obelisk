// RUN: not %obelisk --std=1800-2023 -emit-obelisk %s 2>&1 \
// RUN:   | FileCheck %s

class duplicate_cross_owner;
  bit a;
  bit b;

  covergroup group;
    cp_a: coverpoint a;
    cp_b: coverpoint b;
    duplicate: cross cp_a, cp_b;
    // CHECK: error: redefinition of 'duplicate'
    duplicate: cross cp_a, cp_b;
  endgroup
endclass
