// RUN: obelisk -emit-sim %s | FileCheck %s

module simulation_aggregates;
  typedef struct packed {
    logic [3:0] payload;
    bit valid;
  } packed_record_t;
  typedef struct {
    logic [7:0] payload;
    bit valid;
  } unpacked_record_t;
  typedef logic [7:0] word_array_t [3:1];
  typedef logic [7:0] ascending_word_array_t [-1:1];
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
  unpacked_record_t unpacked_record;
  unpacked_record_t copied_record;
  unpacked_record_t output_record;
  word_array_t words;
  ascending_word_array_t ascending_words;
  logic [7:0] selected;
  int index;
  packed_union_t packed_choice;
  unpacked_union_t unpacked_choice;
  tagged_packed_union_t tagged_packed_choice;
  tagged_unpacked_union_t tagged_unpacked_choice;

  function automatic unpacked_record_t copy_record(
      input unpacked_record_t value);
    copy_record = value;
  endfunction

  function automatic unpacked_record_t copy_update(
      input unpacked_record_t value,
      output unpacked_record_t output_value,
      inout unpacked_record_t inout_value);
    output_value = value;
    inout_value = value;
    copy_update = value;
  endfunction

  initial begin
    unpacked_record = '{8'h10, 1'b0};
    words = '{8'h11, 8'h22, 8'h33};
    packed_record.payload = 4'ha;
    packed_record.valid = 1'b1;
    unpacked_record.payload = 8'h2a;
    unpacked_record.valid = packed_record.valid;
    words[3] = unpacked_record.payload;
    words[2] = 8'h55;
    words[index] = 8'h77;
    selected = words[index];
    ascending_words[-1] = 8'h88;
    ascending_words[1] = selected;
    packed_choice.logic_word = selected;
    unpacked_choice.int_value = index;
    selected = unpacked_choice.byte_value;
    tagged_packed_choice = tagged word_value 16'h1234;
    tagged_unpacked_choice = tagged int_value index;
    copied_record = copy_record(unpacked_record);
    copied_record = copy_update(unpacked_record, output_record, copied_record);
  end
endmodule

// CHECK: dynamic = true
// CHECK: !obelisk_sim.packed_struct<[
// CHECK: !obelisk_sim.unpacked_struct<[
// CHECK: !obelisk_sim.unpacked_array<3 : 1 x !obelisk_sim.packed_array<7 : 0 x !obelisk_sim.logic<1>>>
// CHECK: !obelisk_sim.unpacked_array<-1 : 1 x !obelisk_sim.packed_array<7 : 0 x !obelisk_sim.logic<1>>>
// CHECK: !obelisk_sim.packed_union<fields = [
// CHECK: !obelisk_sim.unpacked_union<fields = [
// CHECK: !obelisk_sim.packed_union<fields = {{.*}}isTagged = true, tagBits = 2>
// CHECK: obelisk_sim.aggregate.construct
// Descending range [3:1] maps source indices 3 and 2 to ordinals 0 and 1.
// CHECK: obelisk_sim.ref.subelement {{.*}}{{\[\[0\]\]}} : !obelisk_sim.ref<!obelisk_sim.unpacked_array<3 : 1
// CHECK: obelisk_sim.ref.subelement {{.*}}{{\[\[1\]\]}} : !obelisk_sim.ref<!obelisk_sim.unpacked_array<3 : 1
// CHECK: %[[DYNAMIC_WRITE:.*]] = obelisk_sim.ref.array_element
// CHECK: obelisk_sim.ref.store {{.*}} to %[[DYNAMIC_WRITE]]
// CHECK: obelisk_sim.array.extract_dynamic
// Ascending range [-1:1] maps source indices -1 and 1 to ordinals 0 and 2.
// CHECK: obelisk_sim.ref.subelement {{.*}}{{\[\[0\]\]}} : !obelisk_sim.ref<!obelisk_sim.unpacked_array<-1 : 1
// CHECK: obelisk_sim.ref.subelement {{.*}}{{\[\[2\]\]}} : !obelisk_sim.ref<!obelisk_sim.unpacked_array<-1 : 1
// CHECK: obelisk_sim.union.construct {{.*}} as 1 : {{.*}}isTagged = true, tagBits = 2>
// CHECK: obelisk_sim.union.construct {{.*}} as 1 : {{.*}}isTagged = true>
// Aggregate output and inout values are explicit copy-out results.
// CHECK: %[[COPY_CALL:[0-9]+]]:3 = obelisk_sim.call
// CHECK: obelisk_sim.ref.store %[[COPY_CALL]]#1
// CHECK: obelisk_sim.ref.store %[[COPY_CALL]]#2
// CHECK-NOT: obelisk.sv.
