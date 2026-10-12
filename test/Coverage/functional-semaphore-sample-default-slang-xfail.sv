// XFAIL: *
// RUN: %obelisk --std=1800-2023 -emit-obelisk %s -o %t.mlir

// LRM 19.8.1 permits default sample arguments. Pristine Slang 11.0 loses the
// enclosing scope when resolving semaphore in a defaulted sample formal.
module top;
  covergroup cg with function sample(input semaphore value = null);
    cp: coverpoint (value == null);
  endgroup
  cg instance_value;
  initial begin
    instance_value = new;
    instance_value.sample();
  end
endmodule
