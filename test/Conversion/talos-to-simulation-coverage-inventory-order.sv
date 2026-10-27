// RUN: %talos --std=1800-2023 --coverage=line,toggle,functional -emit-talos %s | talos-opt '--lower-talos-to-sim=opt-level=0' --test-talos-print-coverage-schema -o /dev/null 2>&1 | FileCheck %s
// RUN: %talos --std=1800-2023 --coverage=line,toggle,functional -emit-talos %s | talos-opt '--lower-talos-to-sim=opt-level=3' --test-talos-print-coverage-schema -o /dev/null 2>&1 | FileCheck %s

// The schema codec rejects any inventory that is not in canonical order
// (unsigned 64-bit IDs, then each table's composite key), so printing the
// embedded schema at both optimization levels proves the ordering. Nested
// options, two argument kinds, source ranges, cross selectors and transitions
// populate every inventory whose order differs.
// CHECK-DAG: {{^}}line_point 
// CHECK-DAG: {{^}}functional_type 
// CHECK-DAG: {{^}}functional_item 
// CHECK-DAG: {{^}}functional_bin 
// CHECK-DAG: {{^}}transition_program 
// CHECK-DAG: {{^}}cross_plan 
// CHECK-DAG: {{^}}cross_selector 
// CHECK-COUNT-5: {{^}}functional_formal id=
// CHECK-NOT: {{^}}functional_formal id=
// CHECK-DAG: {{^}}functional_value_set 
// CHECK-DAG: {{^}}functional_expression 
// CHECK-DAG: {{^}}functional_source 
// CHECK-DAG: {{^}}functional_bin_plan 
// CHECK-COUNT-9: {{^}}functional_option_plan owner=
// CHECK-NOT: {{^}}functional_option_plan owner=

module top;
  int value = 1;
  logic [3:0] toggled = 0;
  covergroup zebra(input int limit = 1)
      with function sample(input int left = 1, input int right = 0);
    option.per_instance = 1;
    option.goal = 100;
    type_option.weight = 3;
    cp: coverpoint left {
      option.weight = 2;
      type_option.goal = 75;
      bins known[2] = {[0:limit]};
      bins transition = (0 => 1);
    }
    other: coverpoint right { bins known[] = {0, 1}; }
    both: cross cp, other {
      option.weight = 4;
      bins selected = binsof(cp.known) && binsof(other.known);
    }
  endgroup
  covergroup alpha(ref int live, input int limit = 1);
    option.per_instance = 1;
    type_option.goal = 50;
    cp: coverpoint live {
      option.goal = 25;
      bins known = {[0:limit]};
    }
  endgroup
  zebra first;
  alpha second;
  initial begin
    first = new;
    second = new(value);
    first.sample();
    second.sample();
    toggled = 4'b1111;
  end
endmodule
