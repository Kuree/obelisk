// XFAIL: *
// RUN: %obelisk --std=1800-2023 -emit-obelisk %s -o %t.mlir

// LRM 19.8.1 permits default sample arguments. Pristine Slang 11.0 loses the
// enclosing scope for a named covergroup type in a defaulted sample formal.
// It reports source_cg as undeclared; no frontend workaround is applied.
module top;
  covergroup source_cg; cp: coverpoint 1; endgroup
  covergroup observer with function sample(input source_cg value = null);
    cp: coverpoint (value == null);
  endgroup
  observer instance_value;
  initial begin
    instance_value = new;
    instance_value.sample();
  end
endmodule
