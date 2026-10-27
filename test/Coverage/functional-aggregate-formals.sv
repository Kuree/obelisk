// RUN: %talos --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: talos-cov report --format=json %t.obcov | FileCheck %s --check-prefix=LAYOUT
// RUN: talos-cov report --format=json %t.obcov | FileCheck %s --check-prefix=COUNT
// RUN: talos-cov inspect %t.obcov | FileCheck %s --check-prefix=INSPECT

// IEEE 13.5, 19.3 and 19.8.1: fixed aggregate inputs copy values, including
// nested arrays. Constructor refs are live const aliases, while sample inputs
// are writable local copies. Defaults use the same typed capture mechanism.
typedef struct {
  int number;
  string label;
  int values[];
  bit [6:0] bits;
  logic [2:0] tag;
} Record;
typedef int Values[2];
typedef struct { bit [31:0] number; bit [6:0] bits; logic [2:0] tag; } Partial;

module top;
  Record actual;
  Values array_value;
  Partial partial_value;

  function automatic int read_record(const ref Record value);
    return value.number + value.values[0] + (value.label == "before") +
           (value.bits == 5) + (value.tag === 3'bx01);
  endfunction
  function automatic int mutate_record(ref Record value);
    value.number += 5;
    value.values[0] = 99;
    return value.number;
  endfunction
  function automatic int copied_record(input Record value);
    value.number += 100;
    value.values[0] = 88;
    return value.number;
  endfunction
  function automatic int mutate_values(ref int value[2]);
    value[128'h1_0000_0000_0000_0000] = 123;
    value[0] += 5;
    return value[0];
  endfunction
  function automatic int read_partial(const ref Partial value);
    return value.number + value.bits + (value.tag === 3'bx01);
  endfunction

  covergroup records(input Record frozen, ref Record live,
                    input Values fixed_values = '{10, 20})
      with function sample(input Record local_value,
                           input int local_values[2] = '{3, 4});
    option.at_least = 2;
    cp: coverpoint read_record(frozen) + read_record(live) + fixed_values[0] {
      bins expected_value = {26, 27};
    }
    record_cp: coverpoint mutate_record(local_value) {
      bins expected_value = {12, 13};
    }
    by_value_cp: coverpoint copied_record(local_value) {
      bins expected_value = {112, 113};
    }
    frozen_copy_cp: coverpoint copied_record(frozen) { bins expected_value = {103}; }
    retained_local_cp: coverpoint local_value.values[0] { bins expected_value = {99}; }
    local_cp: coverpoint mutate_values(local_values) { bins expected_value = {8}; }
  endgroup
  covergroup arrays(input Values frozen = '{3, 4}, ref Values live = array_value);
    option.at_least = 2;
    cp: coverpoint frozen[0] + live[1] { bins expected_value = {10, 12}; }
  endgroup
  covergroup partials(input Partial frozen, ref Partial live);
    cp: coverpoint read_partial(frozen) + read_partial(live) {
      bins expected_value = {20};
    }
  endgroup
  records record_group;
  arrays array_group;
  partials partial_group;

  initial begin
    actual = '{number: 3, label: "before", values: '{1}, bits: 5, tag: 3'bx01};
    array_value = '{3, 4};
    partial_value = '{3, 5, 3'bx01};
    record_group = new(actual, actual);
    array_group = new();
    partial_group = new(partial_value, partial_value);
    actual.number = 7;
    actual.label = "after";
    actual.values = '{2};
    actual.bits = 0;
    actual.tag = 0;
    array_value[1] = 7;
    partial_value.bits = 7;
    record_group.sample(actual);
    array_group.sample();
    partial_group.sample();
    actual.number = 8;
    array_value[1] = 9;
    record_group.sample(actual, array_value);
    array_group.sample();
    $display("records=%0.0f arrays=%0.0f partial=%0.0f actual=%0d nested=%0d",
             record_group.get_inst_coverage(), array_group.get_inst_coverage(),
             partial_group.get_inst_coverage(), actual.number, actual.values[0]);
    $finish;
  end
endmodule

// CHECK: records=100 arrays=100 partial=100 actual=8 nested=2

// Exactly three aggregate layouts are recorded: the four-state Record (256
// bits, 32 bytes), the four-state Partial and the two-state fixed array (64
// bits, 8 bytes). Nine formals have the aggregate result kind, each pointing at
// the layout of its own type with the same bit width. Native layout includes
// tail padding; Partial stays four-state independently of the two-state array.
// The formals precede the layouts in the report, so all lines form one DAG
// group. Directions are input (1) and ref (2); kinds are constructor (1) and sample (2).
// LAYOUT-DAG: {"id":[[RECORD:[0-9]+]],"element_type":[[RECORD]],"value_size":32,"alignment":1,"bit_width":256,"flags":1}
// LAYOUT-DAG: {"id":[[PARTIAL:[0-9]+]],"element_type":[[PARTIAL]],"value_size":8,"alignment":1,"bit_width":64,"flags":1}
// LAYOUT-DAG: {"id":[[ARRAY:[0-9]+]],"element_type":[[ARRAY]],"value_size":8,"alignment":1,"bit_width":64,"flags":0}
// LAYOUT-DAG: "name":"frozen","kind":1,"direction":1,"result_kind":9,{{[^}]*}}"bit_width":64,{{[^}]*}}"value_type":[[ARRAY]]}
// LAYOUT-DAG: "name":"live","kind":1,"direction":2,"result_kind":9,{{[^}]*}}"bit_width":64,{{[^}]*}}"value_type":[[ARRAY]]}
// LAYOUT-DAG: "name":"frozen","kind":1,"direction":1,"result_kind":9,{{[^}]*}}"bit_width":64,{{[^}]*}}"value_type":[[PARTIAL]]}
// LAYOUT-DAG: "name":"live","kind":1,"direction":2,"result_kind":9,{{[^}]*}}"bit_width":64,{{[^}]*}}"value_type":[[PARTIAL]]}
// LAYOUT-DAG: "name":"frozen","kind":1,"direction":1,"result_kind":9,{{[^}]*}}"bit_width":256,{{[^}]*}}"value_type":[[RECORD]]}
// LAYOUT-DAG: "name":"live","kind":1,"direction":2,"result_kind":9,{{[^}]*}}"bit_width":256,{{[^}]*}}"value_type":[[RECORD]]}
// LAYOUT-DAG: "name":"fixed_values","kind":1,"direction":1,"result_kind":9,{{[^}]*}}"bit_width":64,{{[^}]*}}"value_type":[[ARRAY]]}
// LAYOUT-DAG: "name":"local_value","kind":2,"direction":1,"result_kind":9,{{[^}]*}}"bit_width":256,{{[^}]*}}"value_type":[[RECORD]]}
// LAYOUT-DAG: "name":"local_values","kind":2,"direction":1,"result_kind":9,{{[^}]*}}"bit_width":64,{{[^}]*}}"value_type":[[ARRAY]]}

// No fourth layout exists.
// COUNT: "functional_aggregate_types":[
// COUNT-COUNT-3: "element_type"
// COUNT-NOT: "element_type"
// COUNT: "functional_item_templates"

// There are no formals besides those nine.
// INSPECT: functional-formals: 9
