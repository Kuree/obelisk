// RUN: not talos-opt %s 2>&1 | FileCheck %s

module {
  talos_sdf.delay_file "bad.sdf" version "4.0" {
    talos_sdf.cell "dut" {
      talos_sdf.path_delay absolute iopath
        #talos_sdf.port<name = "a", hasIndex = false, index = 0, edge = none> to
        #talos_sdf.port<name = "z", hasIndex = false, index = 0, edge = none>
        delays []
    }
  }
}

// CHECK: error: 'talos_sdf.path_delay' op requires 1, 2, 3, 6, or 12 delay values
