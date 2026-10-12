// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s

// IEEE 19.3: constructor inputs retain their original values and are const
// variables, so const ref helpers may address them in bins, options and samples.
// IEEE 13.5.2 and 19.8.1: custom sample inputs are separate local variables;
// writable ref helpers change those copies without modifying the caller.
class Base;
  function automatic int read_input(const ref int value);
    return value;
  endfunction
  covergroup cg(input int frozen);
    option.per_instance = 1;
    cp: coverpoint read_input(frozen) { bins one = {1}; }
  endgroup
  function new;
    cg = new(1);
  endfunction
endclass
class Derived extends Base;
endclass

module top;
  int value = 1;
  string text = "old";
  real number = 1.0;
  logic [3:0] bits = 4'b1x10;
  function automatic int read_int(const ref int target);
    return target;
  endfunction
  function automatic string read_text(const ref string target);
    return target;
  endfunction
  function automatic real read_real(const ref real target);
    return target;
  endfunction
  function automatic logic [3:0] read_bits(const ref logic [3:0] target);
    return target;
  endfunction
  function automatic int bump(ref int target);
    target++;
    return target;
  endfunction
  function automatic int distinguish(ref int first, const ref int second);
    first++;
    return first * 10 + second;
  endfunction
  function automatic int mutate_types(ref string t, ref real r,
                                       ref logic [3:0] b);
    t = "new";
    r += 1.0;
    b = 4'b1x10;
    return 1;
  endfunction

  covergroup captured(input int frozen, input string frozen_text,
                      input real frozen_real, input logic [3:0] frozen_bits,
                      input bit choose);
    option.per_instance = 1;
    option.at_least = read_int(frozen);
    int_point: coverpoint read_int(frozen) {
      bins one = {read_int(frozen)};
    }
    text_point: coverpoint read_text(frozen_text) == "old" { bins yes = {1}; }
    real_point: coverpoint read_real(frozen_real) { bins one = {1.0}; }
    bits_point: coverpoint read_bits(frozen_bits) === 4'b1x10 { bins yes = {1}; }
    branch_point: coverpoint choose ? read_int(frozen) : frozen { bins one = {1}; }
  endgroup
  covergroup copied with function sample(input int local_value = 1,
                                         input bit choose = 1);
    option.per_instance = 1;
    cp: coverpoint choose ? (bump(local_value) ? local_value : -1) : local_value {
      bins both = {1, 2};
    }
  endgroup
  covergroup independent with function sample(input int first, input int second,
                                               input bit choose);
    option.per_instance = 1;
    cp: coverpoint choose ? distinguish(first, second) : read_int(first) {
      bins both = {1, 21};
    }
  endgroup
  covergroup typed with function sample(input string t, input real r,
                                         input logic [3:0] b);
    option.per_instance = 1;
    cp: coverpoint mutate_types(t, r, b) ?
        (read_text(t) == "new" && read_real(r) == 2.0 &&
         (read_bits(b) === 4'b1x10)) : 0 { bins yes = {1}; }
  endgroup

  captured frozen_true, frozen_false;
  copied local_copy;
  independent separate_copies;
  typed typed_copies;
  Derived inherited;
  initial begin
    frozen_true = new(value, text, number, bits, 1);
    frozen_false = new(value, text, number, bits, 0);
    value = 9;
    text = "changed";
    number = 2.0;
    bits = 0;
    frozen_true.sample();
    frozen_false.sample();
    inherited = new;
    inherited.cg.sample();
    $display("captured=%0.1f,%0.1f inherited=%0.1f",
             frozen_true.get_inst_coverage(), frozen_false.get_inst_coverage(),
             inherited.cg.get_inst_coverage());

    value = 1;
    local_copy = new;
    separate_copies = new;
    for (int i = 0; i < 2; i++) begin
      local_copy.sample(value, i == 0);
      // Identical SSA constants must still produce two independent copies.
      separate_copies.sample(1, 1, i == 0);
    end
    local_copy.sample();
    $display("copied=%0.1f independent=%0.1f caller=%0d",
             local_copy.get_inst_coverage(), separate_copies.get_inst_coverage(),
             value);

    text = "old";
    number = 1.0;
    typed_copies = new;
    typed_copies.sample(text, number, bits);
    $display("typed=%0.1f caller=%s,%0.1f,%04b",
             typed_copies.get_inst_coverage(), text, number, bits);
    $finish;
  end
endmodule

// CHECK: captured=100.0,100.0 inherited=100.0
// CHECK: copied=100.0 independent=100.0 caller=1
// CHECK: typed=100.0 caller=old,1.0,0000
