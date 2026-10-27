// RUN: %talos --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s

// IEEE 13.5.2 and 19.3: refs may select unpacked-struct members and continue
// to refer to removed array elements. Nested views must preserve both planes,
// sibling bits, managed values and the actual field's storage binding.
typedef struct {
  int number;
  bit [6:0] bits;
  logic [2:0] tag;
  real metric;
  string label;
  int values[];
} Record;
typedef struct { int prefix; Record data; int tail; } Envelope;
class Holder;
  Record data;
endclass
module top;
  Envelope items[];
  Record queue_items[$];
  Record map_items[string], empty_map[string];
  Holder object;
  function automatic void set_number(ref int value, input bit second);
    value = second ? 7 : 3;
  endfunction
  function automatic void set_bits(ref bit [6:0] value, input bit second);
    value = second ? 6 : 2;
  endfunction
  function automatic void set_tag(ref logic [2:0] value);
    value = 3'bx01;
  endfunction
  function automatic void set_metric(ref real value, input bit second);
    value = second ? 5.0 : 4.0;
  endfunction
  function automatic void set_label(ref string value);
    value = "a changed label longer than inline storage";
  endfunction
  function automatic void set_values(ref int value[], input bit second);
    value = second ? '{1} : '{6};
  endfunction
  function automatic void change(ref Record record, input bit second);
    set_number(record.number, second);
    set_bits(record.bits, second);
    set_tag(record.tag);
    set_metric(record.metric, second);
    set_label(record.label);
    set_values(record.values, second);
  endfunction
  function automatic int mutate_values(ref int first, ref int second);
    items[0].data.values.delete();
    first = 9;
    return second;
  endfunction
  function automatic int mutate_removed(ref int first, ref int second);
    queue_items.delete();
    first = 9;
    return second;
  endfunction
  covergroup cg(ref int number, ref bit [6:0] bits, ref logic [2:0] tag,
                ref real metric, ref string label, ref int values[]);
    option.per_instance = 1;
    cp: coverpoint number + bits + (tag === 3'bx01 ? 10 : 0) + int'(metric) +
        (label == "a changed label longer than inline storage") + values[0] {
      bins hits[] = {26, 30};
    }
  endgroup
  cg array_cov, queue_cov, map_cov, class_cov;
  Record initial_value;
  initial begin
    initial_value.values = '{0};
    items = new[1];
    items[0].prefix = 77;
    items[0].tail = 88;
    items[0].data = initial_value;
    queue_items = '{initial_value};
    map_items["key"] = initial_value;
    object = new;
    object.data = initial_value;
    array_cov = new(items[0].data.number, items[0].data.bits, items[0].data.tag,
                    items[0].data.metric, items[0].data.label, items[0].data.values);
    queue_cov = new(queue_items[0].number, queue_items[0].bits, queue_items[0].tag,
                    queue_items[0].metric, queue_items[0].label, queue_items[0].values);
    map_cov = new(map_items["key"].number, map_items["key"].bits, map_items["key"].tag,
                  map_items["key"].metric, map_items["key"].label, map_items["key"].values);
    class_cov = new(object.data.number, object.data.bits, object.data.tag,
                    object.data.metric, object.data.label, object.data.values);
    $display("missing=%0d/%b", items[9].data.bits, items[9].data.tag);
    items[9].data.bits = 5;
    $display("ignored=%0d/%b", items[9].data.bits, items[9].data.tag);
    queue_items[1].bits = 5;
    queue_items[1].tag = 3'b101;
    $display("append=%0d/%b size=%0d", queue_items[1].bits,
             queue_items[1].tag, queue_items.size());
    $display("empty-map=%0d/%b", empty_map["fresh"].bits, empty_map["fresh"].tag);
    empty_map["fresh"].bits = 5;
    empty_map["fresh"].tag = 3'b101;
    $display("new-map=%0d/%b", empty_map["fresh"].bits, empty_map["fresh"].tag);
    map_items["fresh"].bits = 5;
    map_items["fresh"].tag = 3'b101;
    $display("map-field=%0d/%b", map_items["fresh"].bits, map_items["fresh"].tag);
    change(items[0].data, 0);
    change(queue_items[0], 0);
    change(map_items["key"], 0);
    change(object.data, 0);
    array_cov.sample(); queue_cov.sample(); map_cov.sample(); class_cov.sample();
    change(items[0].data, 1);
    change(queue_items[0], 1);
    change(map_items["key"], 1);
    change(object.data, 1);
    items[0].data.bits = 6;
    map_items["key"].number = 7;
    object.data.metric = 5.0;
    queue_items[0].number = 7;
    object.data.number <= 7;
    #1;
    $display("prefix=%0d tail=%0d tag=%b", items[0].prefix, items[0].tail, items[0].data.tag);
    items = new[0];
    queue_items.delete();
    map_items.delete();
    array_cov.sample(); queue_cov.sample(); map_cov.sample(); class_cov.sample();
    $display("array=%0.0f queue=%0.0f map=%0.0f class=%0.0f",
             array_cov.get_inst_coverage(), queue_cov.get_inst_coverage(),
             map_cov.get_inst_coverage(), class_cov.get_inst_coverage());
    queue_items = '{initial_value};
    $display("alias=%0d", mutate_removed(queue_items[0].number,
                                         queue_items[0].number));
    items = new[1];
    items[0].data = initial_value;
    $display("nested-alias=%0d", mutate_values(items[0].data.values[0],
                                               items[0].data.values[0]));
    $finish;
  end
endmodule
// CHECK: missing=0/xxx
// CHECK: ignored=0/xxx
// CHECK: append=5/101 size=2
// CHECK: empty-map=0/xxx
// CHECK: new-map=5/101
// CHECK: map-field=5/101
// CHECK: prefix=77 tail=88 tag=x01
// CHECK: array=100 queue=100 map=100 class=100
// CHECK: alias=9
// CHECK: nested-alias=9
