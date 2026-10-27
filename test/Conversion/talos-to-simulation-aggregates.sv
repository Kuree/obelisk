// RUN: talos -emit-slang %s | talos-opt --convert-slang-to-talos '--lower-talos-to-sim=opt-level=0' | FileCheck %s

// CHECK: dynamic = true
// CHECK: !simulation.packed_struct<[
// CHECK: !simulation.unpacked_struct<[
// CHECK: !simulation.unpacked_array<3 : 1 x !simulation.packed_array<7 : 0 x !simulation.logic<1>>>
// CHECK: !simulation.unpacked_array<-1 : 1 x !simulation.packed_array<7 : 0 x !simulation.logic<1>>>
// CHECK: !simulation.packed_union<fields = [
// CHECK: !simulation.unpacked_union<fields = [
// CHECK: !simulation.packed_union<fields = {{.*}}isTagged = true, tagBits = 2>
// Named setters were written valid-first, but aggregate construction follows
// declaration order: payload, then valid.
// CHECK: %[[NAMED_PAYLOAD:.*]] = simulation.logic.constant 6 : i4, 0 : i4
// CHECK: %[[NAMED_VALID:.*]] = arith.constant true
// CHECK: %[[NAMED_PAYLOAD_ARRAY:.*]] = simulation.packed.unflatten %[[NAMED_PAYLOAD]]
// CHECK: simulation.aggregate.construct %[[NAMED_PAYLOAD_ARRAY]], %[[NAMED_VALID]]
// CHECK: simulation.aggregate.construct
// Descending range [3:1] maps source indices 3 and 2 to ordinals 0 and 1.
// CHECK: simulation.ref.subelement {{.*}}{{\[\[0\]\]}} : !simulation.ref<!simulation.unpacked_array<3 : 1
// CHECK: simulation.ref.subelement {{.*}}{{\[\[1\]\]}} : !simulation.ref<!simulation.unpacked_array<3 : 1
// CHECK: %[[DYNAMIC_WRITE:.*]] = simulation.ref.array_element
// CHECK: simulation.ref.store {{.*}} to %[[DYNAMIC_WRITE]]
// A dynamic element read addresses the element instead of loading the whole
// array and selecting out of the loaded value.
// CHECK: %[[DYNAMIC_READ:.*]] = simulation.ref.array_element
// CHECK: simulation.ref.load %[[DYNAMIC_READ]]
// Ascending range [-1:1] maps source indices -1 and 1 to ordinals 0 and 2.
// CHECK: simulation.ref.subelement {{.*}}{{\[\[0\]\]}} : !simulation.ref<!simulation.unpacked_array<-1 : 1
// CHECK: simulation.ref.subelement {{.*}}{{\[\[2\]\]}} : !simulation.ref<!simulation.unpacked_array<-1 : 1
// An unpacked union member write preserves every overlapping byte through a
// whole-value read/modify/write instead of manufacturing a subreference.
// CHECK: %[[UNTAGGED_OLD:.*]] = simulation.ref.load {{.*}} : {{.*}} -> !simulation.unpacked_union<{{.*}}isTagged = false>
// CHECK: %[[UNTAGGED_UPDATED:.*]] = simulation.aggregate.insert {{.*}} into %[[UNTAGGED_OLD]][1] :
// CHECK: simulation.ref.store %[[UNTAGGED_UPDATED]] to {{.*}} : !simulation.unpacked_union<{{.*}}isTagged = false>
// CHECK: simulation.union.construct {{.*}} as 1 : {{.*}}isTagged = true, tagBits = 2>
// CHECK: simulation.union.construct {{.*}} as 1 : {{.*}}isTagged = true>
// Ambiguous fixed-array merging compares tagged-union activity before values.
// CHECK: simulation.union.is_active
// CHECK: simulation.logic.compare eq
// Aggregate output and inout values are explicit copy-out results.
// CHECK: %[[COPY_CALL:[0-9]+]]:3 = simulation.call
// CHECK: simulation.ref.store %[[COPY_CALL]]#1
// CHECK: simulation.ref.store %[[COPY_CALL]]#2
// CHECK-NOT: talos.sv.

module simulation_aggregates;
  typedef struct packed {
    logic [3:0] payload;
    bit valid;
  } packed_record_t;
  typedef struct {
    logic [7:0] payload;
    bit valid;
  } unpacked_record_t;
  typedef logic [7:0] word_array_t[3:1];
  typedef logic [7:0] ascending_word_array_t[-1:1];
  typedef union packed {
    logic [7:0] logic_word;
    bit [7:0] bit_word;
  } packed_union_t;
  typedef union {
    logic [7:0] byte_value;
    int int_value;
  } unpacked_union_t;
  typedef union tagged packed {
    logic [7:0] byte_value;
    logic [15:0] word_value;
    logic [3:0] nibble_value;
  } tagged_packed_union_t;
  typedef union tagged {
    logic [7:0] byte_value;
    int int_value;
  } tagged_unpacked_union_t;

  packed_record_t packed_record;
  unpacked_record_t unpacked_record, copied_record, output_record;
  word_array_t words;
  ascending_word_array_t ascending_words;
  logic [7:0] selected;
  int index;
  packed_union_t packed_choice;
  unpacked_union_t unpacked_choice;
  tagged_packed_union_t tagged_packed_choice;
  tagged_unpacked_union_t tagged_unpacked_choice;
  tagged_unpacked_union_t tagged_left[0:1], tagged_right[0:1];
  tagged_unpacked_union_t tagged_merged[0:1];
  logic ambiguous;

  function automatic unpacked_record_t copy_record(unpacked_record_t value);
    copy_record = value;
  endfunction

  function automatic unpacked_record_t copy_update(
      input unpacked_record_t value, output unpacked_record_t output_value,
      inout unpacked_record_t inout_value);
    output_value = value;
    inout_value = value;
    copy_update = value;
  endfunction

  initial begin
    unpacked_record = '{8'd16, 1'b0};
    packed_record = '{valid: 1'b1, payload: 4'b0110};
    words = '{8'd17, 8'd34, 8'd51};
    packed_record.payload = 4'b1010;
    packed_record.valid = 1'b1;
    unpacked_record.payload = 8'd42;
    unpacked_record.valid = packed_record.valid;
    words[3] = unpacked_record.payload;
    words[2] = 8'd85;
    words[index] = 8'd119;
    selected = words[index];
    ascending_words[-1] = 8'd136;
    ascending_words[1] = selected;
    packed_choice.logic_word = selected;
    unpacked_choice.int_value = index;
    selected = unpacked_choice.byte_value;
    tagged_packed_choice = tagged word_value 16'd4660;
    tagged_unpacked_choice = tagged int_value index;
    // An X condition merges the arrays element by element.
    ambiguous = 1'bx;
    tagged_left[0] = tagged int_value 7;
    tagged_right[0] = tagged int_value 7;
    tagged_left[1] = tagged int_value 9;
    tagged_right[1] = tagged byte_value 8'd9;
    tagged_merged = ambiguous ? tagged_left : tagged_right;
    copied_record = copy_record(unpacked_record);
    copied_record = copy_update(unpacked_record, output_record, copied_record);
  end
endmodule
