// RUN: %split-file %s %t
// RUN: %talos --std=1800-2023 -emit-talos %t/zero-shift.sv | not talos-opt '--lower-talos-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: %talos --std=1800-2023 -emit-talos %t/nonzero-exit.sv | not talos-opt '--lower-talos-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: %talos --std=1800-2023 -emit-talos %t/body-write.sv | not talos-opt '--lower-talos-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: %talos --std=1800-2023 -emit-talos %t/signed-widening.sv | not talos-opt '--lower-talos-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: %talos --std=1800-2023 -emit-talos %t/width-bound.sv | not talos-opt '--lower-talos-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: %talos --std=1800-2023 -emit-talos %t/repeat-bound.sv | not talos-opt '--lower-talos-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=REJECT
// RUN: %talos --std=1800-2023 -emit-talos %t/repeat-dynamic.sv | not talos-opt '--lower-talos-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=REJECT

// IEEE 1800-2023 18.5.11 function bodies: a function loop is expanded only
// with an exact admitted bound. Zero shifts, modified variants, and sign
// extension before truncation can prevent progress. Nonzero exit tests and
// loops beyond the compiler expansion policy need staged execution. General
// dynamic repeat counts need a complete staged evaluator.
// REJECT: error: constraint function id must be a side-effect-free expression function or an admitted automatic statement body

//--- zero-shift.sv
class C;
  rand bit [3:0] x;
  rand bit [9:0] y;
  function automatic int id(bit [9:0] value);
    for (id = 0; value != 0; value = value >> 0) begin
      id = 0;
    end
  endfunction
  constraint relation { x == id(y); }
endclass
module top;
  C object = new;
  initial void'(object.randomize());
endmodule

//--- nonzero-exit.sv
class C;
  rand bit [3:0] x;
  rand bit [9:0] y;
  function automatic int id(bit [9:0] value);
    for (id = 0; value != 1; value = value >> 1) begin
      id = 0;
    end
  endfunction
  constraint relation { x == id(y); }
endclass
module top;
  C object = new;
  initial void'(object.randomize());
endmodule

//--- body-write.sv
class C;
  rand bit [3:0] x;
  rand bit [9:0] y;
  function automatic int id(bit [9:0] value);
    for (id = 0; value != 0; value = value >> 1) begin
      id = 0;
      value = 1023;
    end
  endfunction
  constraint relation { x == id(y); }
endclass
module top;
  C object = new;
  initial void'(object.randomize());
endmodule

//--- signed-widening.sv
class C;
  rand bit [3:0] x;
  rand bit signed [9:0] y;
  function automatic int id(bit signed [9:0] value);
    for (id = 0; value != 0; value = int'(value) >> 1) begin
      id = 0;
    end
  endfunction
  constraint relation { x == id(y); }
endclass
module top;
  C object = new;
  initial void'(object.randomize());
endmodule

//--- width-bound.sv
class C;
  rand bit [3:0] x;
  rand bit [256:0] y;
  function automatic int id(bit [256:0] value);
    for (id = 0; value != 0; value = value >> 1) begin
      id = 0;
    end
  endfunction
  constraint relation { x == id(y); }
endclass
module top;
  C object = new;
  initial void'(object.randomize());
endmodule

//--- repeat-bound.sv
class C;
  rand bit [3:0] x;
  rand bit [9:0] y;
  function automatic int id(bit [9:0] value);
    repeat (257) ;
  endfunction
  constraint relation { x == id(y); }
endclass
module top;
  C object = new;
  initial void'(object.randomize());
endmodule

//--- repeat-dynamic.sv
class C;
  rand bit [3:0] x;
  rand bit [9:0] y;
  function automatic int id(bit [9:0] value);
    repeat (value) ;
  endfunction
  constraint relation { x == id(y); }
endclass
module top;
  C object = new;
  initial void'(object.randomize());
endmodule
