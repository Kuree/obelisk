// RUN: not obelisk-opt %s 2>&1 | FileCheck %s

module {
  obelisk_sdf.delay_file "bad.sdf" version "4.0" {
    obelisk_sdf.cell "dut" {
      obelisk_sdf.path_delay absolute iopath
        #obelisk_sdf.port<name = "a", hasIndex = false, index = 0, edge = none> to
        #obelisk_sdf.port<name = "z", hasIndex = false, index = 0, edge = none>
        delays []
    }
  }
}

// CHECK: error: 'obelisk_sdf.path_delay' op requires 1, 2, 3, 6, or 12 delay values
