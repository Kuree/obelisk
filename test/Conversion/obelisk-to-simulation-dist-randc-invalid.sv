// RUN: %split-file %s %t
// RUN: obelisk -emit-slang %t/randc-wide.sv | not obelisk-opt --convert-slang-to-obelisk --lower-obelisk-to-sim=opt-level=0 -o /dev/null 2>&1 | FileCheck %s --check-prefix=RANDC
// RUN: obelisk -emit-slang %t/soft-dist.sv | not obelisk-opt --convert-slang-to-obelisk --lower-obelisk-to-sim=opt-level=0 -o /dev/null 2>&1 | FileCheck %s --check-prefix=SOFT
// RUN: obelisk -emit-slang %t/dist-solve-before.sv | not obelisk-opt --convert-slang-to-obelisk --lower-obelisk-to-sim=opt-level=0 -o /dev/null 2>&1 | FileCheck %s --check-prefix=SOLVE

// randc properties wider than 32 bits are rejected.
// RANDC: randc-wide.sv:2:{{[0-9]+}}: error: randc properties must be packed integral values no wider than 32 bits

// Soft dist constraints are not executable yet.
// SOFT: soft-dist.sv:4:{{[0-9]+}}: error: soft dist constraints are not executable yet

// dist combined with solve-before is not executable yet; randomize() reports it.
// SOLVE: dist-solve-before.sv:11:{{[0-9]+}}: error: dist combined with solve before is not executable yet

//--- randc-wide.sv
class C;
  randc bit [32:0] x;
endclass
module top;
  initial begin
    C c = new;
    void'(c.randomize());
  end
endmodule

//--- soft-dist.sv
class C;
  rand bit [3:0] x;
  constraint c {
    soft x dist {0 := 1, [1:3] :/ 6};
  }
endclass
module top;
  initial begin
    C c = new;
    void'(c.randomize());
  end
endmodule

//--- dist-solve-before.sv
class C;
  rand bit [3:0] x, y;
  constraint c {
    solve x before y;
    y dist {0 := 1, [1:3] :/ 6};
  }
endclass
module top;
  initial begin
    C c = new;
    void'(c.randomize());
  end
endmodule
