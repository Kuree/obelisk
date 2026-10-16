// RUN: obelisk -emit-obelisk %s -o %t.mlir
// RUN: obelisk -emit-sim %s -o %t.sim.mlir
module top;
  function automatic void work;
    typedef int same_t;
    begin
      typedef enum bit [1:0] {A = 1} same_t;
      same_t state;
      begin
        typedef int same_t;
        same_t value;
        enum bit [1:0] {B = 2} anonymous;
        value = 23;
      end
    end
  endfunction
  initial work();
endmodule
