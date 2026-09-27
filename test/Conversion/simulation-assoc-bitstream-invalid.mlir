// RUN: obelisk-opt %s --verify-diagnostics

module {
  func.func @dynamic_element(
      %array: !simulation.assoc_array<i32, !simulation.dynamic_array<i8>, false, false>) -> i8 {
    // expected-error@+1 {{input must be a sequential container or typed associative array of fixed packed elements}}
    %packed = simulation.container.export_bitstream %array :
        (!simulation.assoc_array<i32, !simulation.dynamic_array<i8>, false, false>) -> i8
    return %packed : i8
  }

  func.func @partial_element(
      %array: !simulation.assoc_array<i32, i8, false, false>) -> i12 {
    // expected-error@+1 {{result must be a nonempty fixed bit-stream containing a whole number of input elements}}
    %packed = simulation.container.export_bitstream %array :
        (!simulation.assoc_array<i32, i8, false, false>) -> i12
    return %packed : i12
  }
}
