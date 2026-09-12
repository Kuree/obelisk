// RUN: %split-file %s %t
// RUN: obelisk --std=1800-2023 -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   > %t/lowered.mlir
// RUN: FileCheck %s --check-prefix=LOWER < %t/lowered.mlir
// RUN: %python %S/Inputs/dump-coverage-schema.py < %t/lowered.mlir \
// RUN:   | FileCheck %s --check-prefix=SCHEMA

// IEEE 1800-2017 19.5.1.2 permits any array except an associative array as a
// state-bin set expression when its element type is assignment compatible.
// The expression is evaluated during covergroup construction. Fixed arrays
// are imported into the same managed representation used by dynamic arrays
// and queues. IEEE 1800-2023 19.5.7 additionally permits real coverpoints and
// set values; shortreal elements are promoted according to 6.12 and 6.22.3.
// LOWER: obelisk_sim.call {{.*}} -> !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.packed_array<1 : 0 x i1>>
// LOWER: obelisk_sim.container.create {{.*}} bit_width = 2 : i64{{.*}} element_kind = 1 : i32{{.*}} : (i64) -> !obelisk_sim.dynamic_array<!obelisk_sim.packed_array<1 : 0 x i1>>
// LOWER: obelisk_sim.call {{.*}} -> !obelisk_sim.unpacked_array<0 : 2 x i32>
// LOWER: obelisk_sim.container.create {{.*}} bit_width = 32 : i64{{.*}} container_kind = 1 : i32{{.*}} element_kind = 1 : i32{{.*}} : (i64) -> !obelisk_sim.dynamic_array<i32>
// LOWER: obelisk_sim.container.import_fixed {{.*}}element_span = 32 : i64
// LOWER: obelisk_sim.call {{.*}} -> !obelisk_sim.dynamic_array<i32>
// LOWER: obelisk_sim.call {{.*}} -> !obelisk_sim.queue<i32, 3>
// LOWER: obelisk_sim.call {{.*}} -> !obelisk_sim.unpacked_array<0 : 1 x !obelisk_sim.packed_array<3 : 0 x !obelisk_sim.logic<1>>>
// LOWER: obelisk_sim.container.create {{.*}} bit_width = 4 : i64{{.*}} element_flags = 1 : i32{{.*}} element_kind = 2 : i32
// LOWER: obelisk_sim.container.import_fixed {{.*}}element_span = 4 : i64
// LOWER: obelisk_sim.call {{.*}} -> !obelisk_sim.unpacked_array<0 : 1 x f64>
// LOWER: obelisk_sim.container.create {{.*}} bit_width = 64 : i64{{.*}} element_kind = 3 : i32
// LOWER: obelisk_sim.container.import_fixed {{.*}}element_span = 64 : i64
// LOWER: obelisk_sim.call {{.*}} -> !obelisk_sim.unpacked_array<0 : 1 x f32>
// LOWER: obelisk_sim.container.create {{.*}} bit_width = 32 : i64{{.*}} element_kind = 3 : i32
// LOWER: obelisk_sim.container.import_fixed {{.*}}element_span = 32 : i64
// LOWER: obelisk_sim.covergroup.create {{.*}} payloads[{{.*}}] {{.*}} : (!obelisk_sim.dynamic_array<!obelisk_sim.packed_array<1 : 0 x i1>>, !obelisk_sim.dynamic_array<i32>, !obelisk_sim.dynamic_array<i32>, !obelisk_sim.queue<i32, 3>, i32, !obelisk_sim.dynamic_array<!obelisk_sim.packed_array<3 : 0 x !obelisk_sim.logic<1>>>, !obelisk_sim.dynamic_array<f64>, !obelisk_sim.dynamic_array<f32>)

// SCHEMA-DAG: functional_value_set id=[[PACKED_SET:[1-9][0-9]*]] {{.*}} atoms=0 width=32 kind=1 flags=1 signedness=2 set_expression=[[PACKED_EXPR:[1-9][0-9]*]]
// SCHEMA-DAG: functional_value_set id=[[FIXED_SET:[1-9][0-9]*]] {{.*}} atoms=0 width=32 kind=1 flags=1 signedness=2 set_expression=[[FIXED_EXPR:[1-9][0-9]*]]
// SCHEMA-DAG: functional_value_set id=[[DYNAMIC_SET:[1-9][0-9]*]] {{.*}} atoms=0 width=32 kind=1 flags=1 signedness=2 set_expression=[[DYNAMIC_EXPR:[1-9][0-9]*]]
// SCHEMA-DAG: functional_value_set id=[[QUEUE_SET:[1-9][0-9]*]] {{.*}} atoms=0 width=32 kind=1 flags=1 signedness=2 set_expression=[[QUEUE_EXPR:[1-9][0-9]*]]
// SCHEMA-DAG: functional_value_set id=[[LOGIC_SET:[1-9][0-9]*]] {{.*}} atoms=0 width=4 kind=1 flags=1 signedness=2 set_expression=[[LOGIC_EXPR:[1-9][0-9]*]]
// SCHEMA-DAG: functional_value_set id=[[REAL_SET:[1-9][0-9]*]] {{.*}} atoms=0 width=64 kind=2 flags=1 signedness=3 set_expression=[[REAL_EXPR:[1-9][0-9]*]]
// SCHEMA-DAG: functional_value_set id=[[SHORTREAL_SET:[1-9][0-9]*]] {{.*}} atoms=0 width=64 kind=2 flags=1 signedness=3 set_expression=[[SHORTREAL_EXPR:[1-9][0-9]*]]
// SCHEMA-DAG: functional_expression id=[[PACKED_EXPR]] owner=[[PACKED_SET]] owner_kind=5 role=6 result_kind=4 width=2 signedness=1 {{.*}} phase=1 {{.*}} flags=0
// SCHEMA-DAG: functional_expression id=[[FIXED_EXPR]] owner=[[FIXED_SET]] owner_kind=5 role=6 result_kind=4 width=32 signedness=2 {{.*}} phase=1 {{.*}} flags=0
// SCHEMA-DAG: functional_expression id=[[DYNAMIC_EXPR]] owner=[[DYNAMIC_SET]] owner_kind=5 role=6 result_kind=4 width=32 signedness=2 {{.*}} phase=1 {{.*}} flags=0
// SCHEMA-DAG: functional_expression id=[[QUEUE_EXPR]] owner=[[QUEUE_SET]] owner_kind=5 role=6 result_kind=4 width=32 signedness=2 {{.*}} phase=1 {{.*}} flags=0
// SCHEMA-DAG: functional_expression id=[[LOGIC_EXPR]] owner=[[LOGIC_SET]] owner_kind=5 role=6 result_kind=4 width=4 signedness=1 {{.*}} phase=1 {{.*}} flags=1
// SCHEMA-DAG: functional_expression id=[[REAL_EXPR]] owner=[[REAL_SET]] owner_kind=5 role=6 result_kind=4 width=64 signedness=3 {{.*}} phase=1 {{.*}} flags=0
// SCHEMA-DAG: functional_expression id=[[SHORTREAL_EXPR]] owner=[[SHORTREAL_SET]] owner_kind=5 role=6 result_kind=4 width=32 signedness=3 {{.*}} phase=1 {{.*}} flags=0

//--- input.sv
module top;
  typedef bit [1:0][1:0] packed_values_t;
  function automatic packed_values_t make_packed();
    return 4'b0110;
  endfunction

  typedef int fixed_values_t[3];
  function automatic fixed_values_t make_fixed();
    return '{1, 2, 3};
  endfunction

  typedef int dynamic_values_t[];
  function automatic dynamic_values_t make_dynamic();
    dynamic_values_t values = '{4, 5};
    return values;
  endfunction

  typedef int queue_values_t[$:3];
  function automatic queue_values_t make_queue();
    queue_values_t values = '{6, 7};
    return values;
  endfunction

  typedef logic [3:0] logic_values_t[2];
  function automatic logic_values_t make_logic();
    return '{4'hx, 4'hz};
  endfunction

  typedef real real_values_t[2];
  function automatic real_values_t make_real();
    return '{1.25, 2.5};
  endfunction

  typedef shortreal shortreal_values_t[2];
  function automatic shortreal_values_t make_shortreal();
    return '{3.5, 4.5};
  endfunction

  covergroup cg;
    integral_cp: coverpoint 1 {
      bins packed_bins[] = make_packed();
      bins fixed = make_fixed();
      bins dynamic[] = make_dynamic();
      bins queue_bins[2] = make_queue();
    }
    logic_cp: coverpoint 4'(1) {
      wildcard bins logic_values = make_logic();
    }
    real_cp: coverpoint 1.25 {
      bins real_values[] = make_real();
      bins shortreal_values[] = make_shortreal();
    }
  endgroup

  cg cov = new;
endmodule
