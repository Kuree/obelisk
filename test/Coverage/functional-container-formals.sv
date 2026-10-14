// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: obelisk-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON
// RUN: obelisk-cov report --format=json %t.obcov | FileCheck %s --check-prefix=SIZE

// IEEE 19.3, 19.8.1 and 13.5: inputs copy array values at actual evaluation;
// refs retain storage aliases, including reassignment. Sample inputs are local
// writable copies. Nested sequential values copy recursively.
class Base;
  int values[];
  covergroup group(input int captured[]) with function sample(ref int live[]);
    option.per_instance = 1;
    cp: coverpoint captured[0] + live[0] { bins hit = {3}; }
  endgroup
  function new(input int value[]);
    values = value;
    group = new(value);
  endfunction
endclass
class Derived extends Base;
  covergroup extends group;
  endgroup
  function new(input int value[]);
    super.new(value);
  endfunction
endclass

module top;
  typedef int ints[];
  typedef int int_queue[$:3];
  typedef int int_map[string];
  typedef struct { int inner[]; } row;
  ints values = '{1};
  int_queue queue_values = '{2};
  int_map map_values = '{"key": 3};
  row rows[];
  ints matrix[];
  function automatic int read_input(const ref ints value);
    return value[0];
  endfunction
  function automatic int sample_change(ref ints copy, ref ints live);
    copy[0] = 4;
    live[0] = 5;
    return copy[0] + live[0];
  endfunction
  covergroup arrays(input int captured[], ref int live[], input int unused);
    option.per_instance = 1;
    cp: coverpoint read_input(captured) + live[0] { bins hits[] = {3, 10}; }
  endgroup
  covergroup queues(input int captured[$:3], ref int live[$:3]);
    option.per_instance = 1;
    cp: coverpoint captured[0] + live[0] { bins hit = {11}; }
  endgroup
  covergroup maps(input int captured[string], ref int live[string]);
    option.per_instance = 1;
    cp: coverpoint captured["key"] + live["key"] { bins hit = {13}; }
  endgroup
  covergroup nested(input row captured[], ref row live[]);
    option.per_instance = 1;
    cp: coverpoint captured[0].inner[0] + live[0].inner[0] { bins hit = {3}; }
  endgroup
  covergroup sampled with function sample(input int copy[], ref int live[]);
    option.per_instance = 1;
    cp: coverpoint sample_change(copy, live) { bins hit = {9}; }
  endgroup
  covergroup defaults(input int captured[] = '{6})
      with function sample(input int value[] = '{7});
    option.per_instance = 1;
    cp: coverpoint captured[0] + value[0] { bins hit = {13}; }
  endgroup
  covergroup paths(ref int live[]);
    option.per_instance = 1;
    cp: coverpoint live[0] { bins hits[] = {2, 4}; }
  endgroup
  paths element_path, class_field;
  arrays a;
  queues q;
  maps m;
  nested n;
  sampled s;
  defaults d;
  Derived derived;
  Base base_view;
  initial begin
    a = new(values, values, 0);
    values[0] = 9;
    a.sample();
    values = '{2, 8};
    a.sample();
    q = new(queue_values, queue_values);
    queue_values = '{9};
    q.sample();
    m = new(map_values, map_values);
    map_values = '{"key": 10};
    m.sample();
    rows = new[1];
    rows[0].inner = '{1};
    n = new(rows, rows);
    rows[0].inner[0] = 2;
    n.sample();
    matrix = new[1];
    matrix[0] = '{1};
    element_path = new(matrix[0]);
    matrix[0][0] = 2;
    element_path.sample();
    matrix[0] = '{4};
    element_path.sample();
    values = '{1};
    derived = new(values);
    base_view = derived;
    values[0] = 2;
    base_view.group.sample(values);
    class_field = new(derived.values);
    derived.values = '{2};
    class_field.sample();
    derived.values = '{4};
    class_field.sample();
    s = new;
    s.sample(values, values);
    d = new;
    d.sample();
    $display("arrays=%0.0f queues=%0.0f maps=%0.0f nested=%0.0f sample=%0.0f defaults=%0.0f inherited=%0.0f element=%0.0f field=%0.0f actual=%0d",
             a.get_inst_coverage(), q.get_inst_coverage(), m.get_inst_coverage(),
             n.get_inst_coverage(), s.get_inst_coverage(), d.get_inst_coverage(), derived.group.get_inst_coverage(), element_path.get_inst_coverage(),
             class_field.get_inst_coverage(), values[0]);
    $finish;
  end
endmodule

// CHECK: arrays=100 queues=100 maps=100 nested=100 sample=100 defaults=100 inherited=100 element=100 field=100 actual=5

// Schema: every constructor formal of a container type is a container result
// (result_kind 8) of bit width 0 whose value_type is one of four layouts
// (dynamic array of int, dynamic array of row, bounded queue, associative
// array); the int formal is a plain value with value_type 0. Layout IDs are
// captured from the owning groups' formals and matched in the layout table.
// Derived::group extends Base::group (IEEE 19.4.1), so its type row names
// Base::group as its base.
// JSON: "functional_types":[
// JSON-DAG: {{[{]}}"id":[[BASE:[0-9]+]],"scope":{{[0-9]+}},"name":"group","base":0,"comment":"","hierarchy":"Base::group"
// JSON-DAG: {{[{]}}"id":[[DERIVED:[0-9]+]],"scope":{{[0-9]+}},"name":"group","base":[[BASE]],"comment":"","hierarchy":"Derived::group"
// JSON-DAG: {{[{]}}"id":[[ARRAYS:[0-9]+]],"scope":{{[0-9]+}},"name":"arrays",
// JSON-DAG: {{[{]}}"id":[[PATHS:[0-9]+]],"scope":{{[0-9]+}},"name":"paths",
// JSON-DAG: {{[{]}}"id":[[SAMPLED:[0-9]+]],"scope":{{[0-9]+}},"name":"sampled",
// JSON-DAG: {{[{]}}"id":[[NESTED:[0-9]+]],"scope":{{[0-9]+}},"name":"nested",
// JSON-DAG: {{[{]}}"id":[[QUEUES:[0-9]+]],"scope":{{[0-9]+}},"name":"queues",
// JSON-DAG: {{[{]}}"id":[[MAPS:[0-9]+]],"scope":{{[0-9]+}},"name":"maps",
// JSON-DAG: {{[{]}}"id":[[DEFAULTS:[0-9]+]],"scope":{{[0-9]+}},"name":"defaults",
// JSON: "functional_formals":[
// JSON-DAG: {{[{]}}"id":{{[0-9]+}},"type":[[ARRAYS]],"name":"captured",{{[^}]*}}"result_kind":8,"flags":{{[0-9]+}},"bit_width":0,{{[^}]*}}"value_type":[[INTARR:[0-9]+]]}
// JSON-DAG: {{[{]}}"id":{{[0-9]+}},"type":[[QUEUES]],"name":"captured",{{[^}]*}}"result_kind":8,"flags":{{[0-9]+}},"bit_width":0,{{[^}]*}}"value_type":[[QUEUE:[0-9]+]]}
// JSON-DAG: {{[{]}}"id":{{[0-9]+}},"type":[[MAPS]],"name":"captured",{{[^}]*}}"result_kind":8,"flags":{{[0-9]+}},"bit_width":0,{{[^}]*}}"value_type":[[ASSOC:[0-9]+]]}
// JSON-DAG: {{[{]}}"id":{{[0-9]+}},"type":[[NESTED]],"name":"captured",{{[^}]*}}"result_kind":8,"flags":{{[0-9]+}},"bit_width":0,{{[^}]*}}"value_type":[[ROWARR:[0-9]+]]}
// JSON-DAG: {{[{]}}"id":{{[0-9]+}},"type":[[BASE]],"name":"captured",{{[^}]*}}"result_kind":8,"flags":{{[0-9]+}},"bit_width":0,{{[^}]*}}"value_type":[[INTARR]]}
// JSON-DAG: {{[{]}}"id":{{[0-9]+}},"type":[[BASE]],"name":"live",{{[^}]*}}"result_kind":8,"flags":{{[0-9]+}},"bit_width":0,{{[^}]*}}"value_type":[[INTARR]]}
// JSON-DAG: {{[{]}}"id":{{[0-9]+}},"type":[[DERIVED]],"name":"captured",{{[^}]*}}"result_kind":8,"flags":{{[0-9]+}},"bit_width":0,{{[^}]*}}"value_type":[[INTARR]]}
// JSON-DAG: {{[{]}}"id":{{[0-9]+}},"type":[[DERIVED]],"name":"live",{{[^}]*}}"result_kind":8,"flags":{{[0-9]+}},"bit_width":0,{{[^}]*}}"value_type":[[INTARR]]}
// JSON-DAG: {{[{]}}"id":{{[0-9]+}},"type":[[ARRAYS]],"name":"live",{{[^}]*}}"result_kind":8,"flags":{{[0-9]+}},"bit_width":0,{{[^}]*}}"value_type":[[INTARR]]}
// JSON-DAG: {{[{]}}"id":{{[0-9]+}},"type":[[PATHS]],"name":"live",{{[^}]*}}"result_kind":8,"flags":{{[0-9]+}},"bit_width":0,{{[^}]*}}"value_type":[[INTARR]]}
// JSON-DAG: {{[{]}}"id":{{[0-9]+}},"type":[[SAMPLED]],"name":"copy",{{[^}]*}}"result_kind":8,"flags":{{[0-9]+}},"bit_width":0,{{[^}]*}}"value_type":[[INTARR]]}
// JSON-DAG: {{[{]}}"id":{{[0-9]+}},"type":[[SAMPLED]],"name":"live",{{[^}]*}}"result_kind":8,"flags":{{[0-9]+}},"bit_width":0,{{[^}]*}}"value_type":[[INTARR]]}
// JSON-DAG: {{[{]}}"id":{{[0-9]+}},"type":[[NESTED]],"name":"live",{{[^}]*}}"result_kind":8,"flags":{{[0-9]+}},"bit_width":0,{{[^}]*}}"value_type":[[ROWARR]]}
// JSON-DAG: {{[{]}}"id":{{[0-9]+}},"type":[[QUEUES]],"name":"live",{{[^}]*}}"result_kind":8,"flags":{{[0-9]+}},"bit_width":0,{{[^}]*}}"value_type":[[QUEUE]]}
// JSON-DAG: {{[{]}}"id":{{[0-9]+}},"type":[[MAPS]],"name":"live",{{[^}]*}}"result_kind":8,"flags":{{[0-9]+}},"bit_width":0,{{[^}]*}}"value_type":[[ASSOC]]}
// JSON-DAG: {{[{]}}"id":{{[0-9]+}},"type":[[DEFAULTS]],"name":"captured",{{[^}]*}}"result_kind":8,"flags":{{[0-9]+}},"bit_width":0,{{[^}]*}}"value_type":[[INTARR]]}
// JSON-DAG: {{[{]}}"id":{{[0-9]+}},"type":[[DEFAULTS]],"name":"value",{{[^}]*}}"result_kind":8,"flags":{{[0-9]+}},"bit_width":0,{{[^}]*}}"value_type":[[INTARR]]}
// JSON-DAG: {{[{]}}"id":{{[0-9]+}},"type":[[ARRAYS]],"name":"unused",{{[^}]*}}"result_kind":2,{{[^}]*}}"value_type":0}
// JSON: "functional_container_types":[
// JSON-DAG: {{[{]}}"id":[[ASSOC]],"kind":3,"key_kind":3,"element_type":{{[0-9]+}},"key_width":0,"bound":0}
// JSON-DAG: {{[{]}}"id":[[QUEUE]],"kind":2,"key_kind":0,"element_type":{{[0-9]+}},"key_width":0,"bound":3}
// JSON-DAG: {{[{]}}"id":[[INTARR]],"kind":1,"key_kind":0,"element_type":{{[0-9]+}},"key_width":0,"bound":0}
// JSON-DAG: {{[{]}}"id":[[ROWARR]],"kind":1,"key_kind":0,"element_type":{{[0-9]+}},"key_width":0,"bound":0}
// JSON: "functional_aggregate_types"

// Exactly 18 formals and 4 layouts exist, so every layout is referenced by
// a container formal and no formal other than the int one is outside them.
// SIZE: "functional_formals":[
// SIZE-COUNT-18: {{[{]}}"id":{{[0-9]+}},"type":
// SIZE-NOT: "type":
// SIZE: "functional_container_types":[
// SIZE-COUNT-4: {{[{]}}"id":
// SIZE-NOT: "id":
// SIZE: "functional_aggregate_types"
