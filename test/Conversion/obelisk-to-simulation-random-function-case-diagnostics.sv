// RUN: %split-file %s %t
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/casex.sv | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=ACCEPT
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/casez.sv | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=ACCEPT
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/case-inside.sv | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=ACCEPT
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/tolerance.sv | not obelisk-opt '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/unique.sv | not obelisk-opt '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=REJECT

// IEEE 1800-2023 12.5 and 18.5.11: wildcard (casex, casez) and membership
// (case inside) statements in a constraint function compile into the Z3
// sampling plan as exact symbol comparisons. Qualifier reports (unique) and
// tolerance ranges (11.4.13) still require their own function implementation
// and are diagnosed.
// ACCEPT: simulation.random.solve
// ACCEPT-SAME: "ODR1
// REJECT: error: constraint function id must be a side-effect-free expression function or an admitted automatic statement body

//--- casex.sv
class C;
  rand bit [3:0] x;
  rand bit [9:0] y;
  function automatic int id(bit [9:0] value);
    casex (value)
      10'b1x: id = 0;
    endcase
  endfunction
  constraint relation { x == id(y); }
endclass
module top;
  C object = new;
  initial void'(object.randomize());
endmodule

//--- casez.sv
class C;
  rand bit [3:0] x;
  rand bit [9:0] y;
  function automatic int id(bit [9:0] value);
    casez (value)
      10'b1z: id = 0;
    endcase
  endfunction
  constraint relation { x == id(y); }
endclass
module top;
  C object = new;
  initial void'(object.randomize());
endmodule

//--- case-inside.sv
class C;
  rand bit [3:0] x;
  rand bit [9:0] y;
  function automatic int id(bit [9:0] value);
    case (value) inside
      10'd0: id = 0;
    endcase
  endfunction
  constraint relation { x == id(y); }
endclass
module top;
  C object = new;
  initial void'(object.randomize());
endmodule

//--- tolerance.sv
class C;
  rand bit [3:0] x;
  rand bit [9:0] y;
  function automatic int id(bit [9:0] value);
    case (value) inside
      [10'd0 +/- 10'd1]: id = 0;
    endcase
  endfunction
  constraint relation { x == id(y); }
endclass
module top;
  C object = new;
  initial void'(object.randomize());
endmodule

//--- unique.sv
class C;
  rand bit [3:0] x;
  rand bit [9:0] y;
  function automatic int id(bit [9:0] value);
    unique case (value)
      10'd0: id = 0;
    endcase
  endfunction
  constraint relation { x == id(y); }
endclass
module top;
  C object = new;
  initial void'(object.randomize());
endmodule
