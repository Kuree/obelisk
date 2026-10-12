// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s

// IEEE 19.3, 19.5, and 13.5.2: constructor refs are read-only aliases that
// remain addressable during procedural coverpoint evaluation. Each const-ref
// helper reads after its other actual has changed the backing storage, so its
// result is independent of actual argument evaluation order.
function automatic int read_current(const ref int target, input int ignored);
  return target;
endfunction

class Base;
  int backing = 1;
  function int read_current(const ref int target, input int ignored);
    return target;
  endfunction
  function int bump();
    backing++;
    return 0;
  endfunction
  covergroup cg(ref int live);
    option.per_instance = 1;
    option.get_inst_coverage = 1;
    changed: coverpoint read_current(live, bump()) { bins values[] = {2, 3}; }
    current: coverpoint live { bins values[] = {2, 3}; }
  endgroup
  function new();
    cg = new(backing);
  endfunction
endclass

class Derived extends Base;
  covergroup extends cg;
    extra: coverpoint read_current(live, 0) { bins values[] = {2, 3}; }
  endgroup
endclass

module top;
  int value = 1;
  int elements[2] = '{1, 1};
  int index_calls;
  string text = "a";
  real number = 1.0;
  logic [3:0] bits = 4'b0001;
  Base base_view;
  Derived derived;
  function automatic int bump();
    value++;
    return 0;
  endfunction
  function automatic int bump_element();
    elements[1]++;
    return 0;
  endfunction
  function automatic int index_once();
    index_calls++;
    return 1;
  endfunction
  function automatic int grow();
    text = {text, "!"};
    return 0;
  endfunction
  function automatic int increase();
    number += 1.0;
    return 0;
  endfunction
  function automatic int change_bits();
    bits = 4'b1x10;
    return 0;
  endfunction
  function automatic int sum_current(const ref int a, const ref int b,
                                    input int ignored);
    return a + b;
  endfunction
  function automatic int read_length(const ref string target, input int ignored);
    return target.len();
  endfunction
  function automatic real read_real(const ref real target, input int ignored);
    return target;
  endfunction
  function automatic logic [3:0] read_bits(const ref logic [3:0] target,
                                         input int ignored);
    return target;
  endfunction
  covergroup integers(ref int live);
    option.per_instance = 1;
    changed: coverpoint read_current(live, bump()) { bins two = {2}; }
    current: coverpoint live { bins two = {2}; }
  endgroup
  covergroup aliases(ref int first, ref int second);
    option.per_instance = 1;
    changed: coverpoint sum_current(first, second, bump_element()) { bins four = {4}; }
    current: coverpoint first + second { bins four = {4}; }
  endgroup
  covergroup strings(input string frozen, ref string live);
    option.per_instance = 1;
    changed: coverpoint read_length(live, grow()) { bins two = {2}; }
    current: coverpoint live.len() { bins two = {2}; }
    snapshot: coverpoint frozen.len() { bins one = {1}; }
  endgroup
  covergroup reals(ref real live);
    option.per_instance = 1;
    changed: coverpoint read_real(live, increase()) { bins two = {2.0}; }
    current: coverpoint live { bins two = {2.0}; }
  endgroup
  covergroup four_state(ref logic [3:0] live);
    option.per_instance = 1;
    changed: coverpoint (read_bits(live, change_bits()) === 4'b1x10) {
      bins selected = {1};
    }
    current: coverpoint (live === 4'b1x10) { bins selected = {1}; }
  endgroup
  covergroup automatic_ref(ref int live);
    option.per_instance = 1;
    cp: coverpoint read_current(live, 0) { bins one = {1}; }
  endgroup
  function automatic automatic_ref make_automatic();
    int local_value = 1;
    automatic_ref cov = new(local_value);
    return cov;
  endfunction
  integers scalar_cov;
  aliases alias_cov;
  strings string_cov;
  reals real_cov;
  four_state bits_cov;
  automatic_ref local_cov;
  initial begin
    scalar_cov = new(value);
    alias_cov = new(elements[index_once()], elements[1]);
    string_cov = new(text, text);
    real_cov = new(number);
    bits_cov = new(bits);
    local_cov = make_automatic();
    derived = new;
    base_view = derived;
    scalar_cov.sample();
    alias_cov.sample();
    string_cov.sample();
    real_cov.sample();
    bits_cov.sample();
    local_cov.sample();
    derived.cg.sample();
    $display("integer=%0d changed=%0.1f current=%0.1f", value,
             scalar_cov.changed.get_inst_coverage(), scalar_cov.current.get_inst_coverage());
    $display("alias=%0d index_calls=%0d changed=%0.1f current=%0.1f", elements[1], index_calls,
             alias_cov.changed.get_inst_coverage(), alias_cov.current.get_inst_coverage());
    $display("string=%s coverage=%0.1f snapshot=%0.1f", text,
             string_cov.get_inst_coverage(), string_cov.snapshot.get_inst_coverage());
    $display("real=%0.1f coverage=%0.1f", number, real_cov.get_inst_coverage());
    $display("bits=%b coverage=%0.1f", bits, bits_cov.get_inst_coverage());
    $display("automatic=%0.1f inherited=%0d coverage=%0.1f extra=%0.1f",
             local_cov.get_inst_coverage(), derived.backing,
             derived.cg.get_inst_coverage(), derived.cg.extra.get_inst_coverage());
    base_view.cg.sample();
    $display("base_view=%0d coverage=%0.1f extra=%0.1f", derived.backing,
             base_view.cg.get_inst_coverage(), derived.cg.extra.get_inst_coverage());
    $finish;
  end
endmodule

// CHECK: integer=2 changed=100.0 current=100.0
// CHECK-NEXT: alias=2 index_calls=1 changed=100.0 current=100.0
// CHECK-NEXT: string=a! coverage=100.0 snapshot=100.0
// CHECK-NEXT: real=2.0 coverage=100.0
// CHECK-NEXT: bits=1x10 coverage=100.0
// CHECK-NEXT: automatic=100.0 inherited=2 coverage=50.0 extra=50.0
// CHECK-NEXT: base_view=3 coverage=100.0 extra=100.0
