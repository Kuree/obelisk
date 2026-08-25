// RUN: not obelisk -emit-sim %s -o /dev/null 2>&1 | FileCheck %s

module specify_transition_delays_strength_pair_invalid(
    input wire data, input wire enable, output wire destination);
  bufif1 (strong1, pull0) gate(destination, data, enable);
  specify
    (data, enable *> destination) =
        (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
  endspecify
endmodule

// CHECK: error: specify paths on conditional primitive strength pairs are not executable yet
