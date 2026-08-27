// RUN: obelisk-opt %s --verify-diagnostics

module {
  func.func @corrupt_plan(
      %source: !obelisk_sim.dynamic_array<!obelisk_sim.dynamic_array<i8>>) {
    // The outer container child span is 32 instead of the canonical 64 bits.
    // expected-error@+2 {{recursive bit-stream plan does not match the input type}}
    %result, %matched, %watch =
        obelisk_sim.recursive.export_bitstream %source {
          plan = array<i64: 5407724112, 3, 64, 0,
              8589934595, 0, 1, 0, 32, 0,
              4294967299, 0, 1, 0, 8, 0,
              1, 0, 8, 0, 0, 8>
        } : (!obelisk_sim.dynamic_array<!obelisk_sim.dynamic_array<i8>>) ->
            (i32, i1, !obelisk_sim.managed_watch)
    return
  }

  func.func @not_recursive(%source: !obelisk_sim.dynamic_array<i8>) {
    // expected-error@+2 {{input must be a recursively dynamically sized bit-stream source}}
    %result, %matched, %watch =
        obelisk_sim.recursive.export_bitstream %source {
          plan = array<i64: 5407724112, 1, 64, 0,
              4294967299, 0, 1, 0, 8, 0>
        } : (!obelisk_sim.dynamic_array<i8>) ->
            (i8, i1, !obelisk_sim.managed_watch)
    return
  }
}
