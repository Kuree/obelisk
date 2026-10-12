// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s

// IEEE 1800-2023 13.5.2 and 19.8.1: a sample ref shares its actual's storage.
// Later actuals mutate that storage before any coverpoint is evaluated. The
// expected result therefore does not depend on coverpoint evaluation order or
// the unspecified argument evaluation order (13.5).
class storage_box;
  int value = 1;
endclass

class Base;
  int backing = 1;
  covergroup cg(ref int constructor_value)
      with function sample(ref int live, input int ignored);
    option.per_instance = 1;
    option.get_inst_coverage = 1;
    cp: coverpoint live { bins values[] = {2, 3}; }
    ctor: coverpoint constructor_value { bins one = {1}; }
  endgroup
  function new;
    cg = new(backing);
  endfunction
endclass

class Derived extends Base;
  covergroup extends cg;
    extra: coverpoint live { bins values[] = {2, 3}; }
  endgroup
endclass

module top;
  int value;
  int elements[2] = '{1, 1};
  int index_calls;
  storage_box box;
  string text;
  real number;

  function automatic int bump(inout int target);
    target++;
    return 0;
  endfunction
  function automatic int append(inout string target);
    target = {target, "!"};
    return 0;
  endfunction
  function automatic int increase(inout real target);
    target += 1.0;
    return 0;
  endfunction
  function automatic int index_once;
    index_calls++;
    return 1;
  endfunction

  covergroup integral with function sample(ref int live, input int ignored);
    option.per_instance = 1;
    option.get_inst_coverage = 1;
    cp: coverpoint live { bins two = {2}; }
  endgroup
  covergroup aliases with function sample(ref int left, ref int right,
                                          input int ignored);
    cp: coverpoint left + right { bins four = {4}; }
  endgroup
  covergroup strings with function sample(ref string live, input int ignored);
    cp: coverpoint live.len() { bins two = {2}; }
  endgroup
  covergroup reals with function sample(ref real live, input int ignored);
    cp: coverpoint live { bins two = {2.0}; }
  endgroup
  covergroup readonly with function sample(const ref int live, input int ignored);
    cp: coverpoint live { bins two = {2}; }
  endgroup

  integral scalar_cov, element_cov, property_cov;
  aliases alias_cov;
  strings string_cov;
  reals real_cov;
  readonly readonly_cov;
  Derived derived;
  Base base_view;
  int covered, total;
  real percentage;
  initial begin
    scalar_cov = new;
    element_cov = new;
    property_cov = new;
    alias_cov = new;
    string_cov = new;
    real_cov = new;
    readonly_cov = new;

    value = 1;
    scalar_cov.sample(value, bump(value));
    percentage = scalar_cov.cp.get_inst_coverage(covered, total);
    $display("scalar %0d %.0f %0d/%0d", value, percentage, covered, total);

    value = 1;
    alias_cov.sample(value, value, bump(value));
    percentage = alias_cov.cp.get_inst_coverage(covered, total);
    $display("aliases %0d %.0f %0d/%0d", value, percentage, covered, total);

    text = "a";
    string_cov.sample(text, append(text));
    percentage = string_cov.cp.get_inst_coverage(covered, total);
    $display("string %s %.0f %0d/%0d", text, percentage, covered, total);

    number = 1.0;
    real_cov.sample(number, increase(number));
    percentage = real_cov.cp.get_inst_coverage(covered, total);
    $display("real %.0f %.0f %0d/%0d", number, percentage, covered, total);

    value = 1;
    readonly_cov.sample(value, bump(value));
    percentage = readonly_cov.cp.get_inst_coverage(covered, total);
    $display("readonly %0d %.0f %0d/%0d", value, percentage, covered, total);

    element_cov.sample(elements[index_once()], bump(elements[1]));
    percentage = element_cov.cp.get_inst_coverage(covered, total);
    $display("element %0d calls=%0d %.0f %0d/%0d", elements[1], index_calls,
             percentage, covered, total);

    box = new;
    property_cov.sample(box.value, bump(box.value));
    percentage = property_cov.cp.get_inst_coverage(covered, total);
    $display("property %0d %.0f %0d/%0d", box.value, percentage, covered, total);

    begin : automatic_storage
      automatic int local_value = 1;
      integral local_cov;
      local_cov = new;
      local_cov.sample(local_value, bump(local_value));
      percentage = local_cov.cp.get_inst_coverage(covered, total);
      $display("automatic %0d %.0f %0d/%0d", local_value, percentage, covered, total);
    end

    derived = new;
    value = 1;
    derived.cg.sample(value, bump(value));
    percentage = derived.cg.extra.get_inst_coverage(covered, total);
    $display("inherited %0d %.0f %0d/%0d", value, percentage, covered, total);
    base_view = derived;
    base_view.cg.sample(value, bump(value));
    percentage = derived.cg.extra.get_inst_coverage(covered, total);
    $display("base-view %0d %.0f %0d/%0d", value, percentage, covered, total);
    $finish;
  end
endmodule

// CHECK: scalar 2 100 1/1
// CHECK-NEXT: aliases 2 100 1/1
// CHECK-NEXT: string a! 100 1/1
// CHECK-NEXT: real 2 100 1/1
// CHECK-NEXT: readonly 2 100 1/1
// CHECK-NEXT: element 2 calls=1 100 1/1
// CHECK-NEXT: property 2 100 1/1
// CHECK-NEXT: automatic 2 100 1/1
// CHECK-NEXT: inherited 2 50 1/2
// CHECK-NEXT: base-view 3 100 2/2
