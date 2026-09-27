// RUN: obelisk -O0 -emit-sim %s -o - | FileCheck %s --check-prefix=SIM

module specify_transition_delays_strength_pair(
    input wire data, input wire enable, output wire destination);
  bufif1 (strong1, pull0) gate(destination, data, enable);
  specify
    (data, enable *> destination) =
        (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
  endspecify
endmodule

// IEEE 1800-2017 28.12.2 and 30.5.1: the L/H strength banks form one logical
// path destination, so every transition-delay group must schedule them with
// one atomic masked operation.
// SIM-COUNT-12: simulation.driver.drive_inertial_path_strength_pair
// SIM-NOT: simulation.driver.drive_inertial_path {{.*}}
// SIM-NOT: simulation.driver.drive_inertial_strength_pair
