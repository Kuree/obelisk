// RUN: not talos-opt %s 2>&1 | FileCheck %s

module {
  sdf.delay_file "bad.sdf" version "4.0" {
    sdf.cell "dut" {
      sdf.path_delay absolute iopath
        #sdf.port<name = "a", hasIndex = false, index = 0, edge = none> to
        #sdf.port<name = "z", hasIndex = false, index = 0, edge = none>
        delays []
    }
  }
}

// CHECK: error: 'sdf.path_delay' op requires 1, 2, 3, 6, or 12 delay values
