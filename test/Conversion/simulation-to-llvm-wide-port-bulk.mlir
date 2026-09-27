// RUN: obelisk-opt %s --split-input-file \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

// A wide full-range port alias with one ordinary driver is published as two
// vector ranges.  The proof lives entirely in native lowering; the simulation
// dialect remains one driver.drive and one static topology declaration.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @wide_port_bulk {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "wide_port_bulk"
    simulation.net.decl 0 in 0 : !simulation.logic<65> design
    simulation.net.decl 1 in 0 : !simulation.logic<65> design
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 65 reversed = false
    simulation.driver.decl 0 in 0 drives 1 : !simulation.logic<65> design
    simulation.func @wide_port_bulk(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %driver: !simulation.driver<!simulation.logic<65>>
            {simulation.capture_kind = 5 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %value = simulation.logic.constant 1 : i65, 0 : i65 :
          !simulation.logic<65>
      simulation.driver.drive %driver = %value :
          !simulation.driver<!simulation.logic<65>>,
          !simulation.logic<65>
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @wide_port_bulk
// CHECK-COUNT-2: llvm.call @obelisk_rt_v1_scheduler_signal_transition
// CHECK-NOT: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK: llvm.return

// -----

// One machine word and narrower deliberately retains the established scalar
// lowering so common small ports have byte-identical IR.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @narrow_port_scalar {
    simulation.scope.decl 0
    simulation.code_unit.decl 2 in 0 function hierarchy "narrow_port_scalar"
    simulation.net.decl 0 in 0 : !simulation.logic<64> design
    simulation.net.decl 1 in 0 : !simulation.logic<64> design
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 64 reversed = false
    simulation.driver.decl 0 in 0 drives 1 : !simulation.logic<64> design
    simulation.func @narrow_port_scalar(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %driver: !simulation.driver<!simulation.logic<64>>
            {simulation.capture_kind = 5 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %value = simulation.logic.constant 1 : i64, 0 : i64 :
          !simulation.logic<64>
      simulation.driver.drive %driver = %value :
          !simulation.driver<!simulation.logic<64>>,
          !simulation.logic<64>
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @narrow_port_scalar
// CHECK-COUNT-64: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK: llvm.return

// -----

// A competing contribution rejects the bulk proof.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @competing_port_scalar {
    simulation.scope.decl 0
    simulation.code_unit.decl 3 in 0 function hierarchy "competing_port_scalar"
    simulation.net.decl 0 in 0 : !simulation.logic<65> design
    simulation.net.decl 1 in 0 : !simulation.logic<65> design
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 65 reversed = false
    simulation.driver.decl 0 in 0 drives 1 : !simulation.logic<65> design
    simulation.driver.decl 1 in 0 drives 0 : !simulation.logic<65> design
    simulation.func @competing_port_scalar(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %driver: !simulation.driver<!simulation.logic<65>>
            {simulation.capture_kind = 5 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %value = simulation.logic.constant 1 : i65, 0 : i65 :
          !simulation.logic<65>
      simulation.driver.drive %driver = %value :
          !simulation.driver<!simulation.logic<65>>,
          !simulation.logic<65>
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @competing_port_scalar
// CHECK-COUNT-65: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK: llvm.return

// -----

// Reversed and partial aliases retain scalar resolution.  They also prove the
// optimization cannot silently reinterpret arbitrary declared ranges.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @reversed_port_scalar {
    simulation.scope.decl 0
    simulation.code_unit.decl 4 in 0 function hierarchy "reversed_port_scalar"
    simulation.net.decl 0 in 0 : !simulation.logic<65> design
    simulation.net.decl 1 in 0 : !simulation.logic<65> design
    simulation.net.connect.decl 0 in 0 0[0] to 1[64] width 65 reversed = true
    simulation.driver.decl 0 in 0 drives 1 : !simulation.logic<65> design
    simulation.func @reversed_port_scalar(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %driver: !simulation.driver<!simulation.logic<65>>
            {simulation.capture_kind = 5 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %value = simulation.logic.constant 1 : i65, 0 : i65 :
          !simulation.logic<65>
      simulation.driver.drive %driver = %value :
          !simulation.driver<!simulation.logic<65>>,
          !simulation.logic<65>
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @reversed_port_scalar
// CHECK-COUNT-65: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK: llvm.return

// -----

// A partial driver slice rejects the exact full-range proof.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @partial_port_scalar {
    simulation.scope.decl 0
    simulation.code_unit.decl 9 in 0 function hierarchy "partial_port_scalar"
    simulation.net.decl 0 in 0 : !simulation.logic<66> design
    simulation.net.decl 1 in 0 : !simulation.logic<66> design
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 66 reversed = false
    simulation.driver.decl 0 in 0 drives 1 : !simulation.logic<66> design
        {driven_low = 1 : i64, driven_width = 65 : i64}
    simulation.func @partial_port_scalar(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9 : i64} {
      %driver = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<66>>
      %slice = simulation.driver.extract %driver from 1 :
          !simulation.driver<!simulation.logic<66>> ->
          !simulation.driver<!simulation.logic<65>>
      %value = simulation.logic.constant 1 : i65, 0 : i65 :
          !simulation.logic<65>
      simulation.driver.drive %slice = %value :
          !simulation.driver<!simulation.logic<65>>,
          !simulation.logic<65>
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @partial_port_scalar
// CHECK-COUNT-65: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK: llvm.return

// -----

// Wired resolution is not equivalent to an ordinary one-driver wire proof.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @wired_port_scalar {
    simulation.scope.decl 0
    simulation.code_unit.decl 10 in 0 function hierarchy "wired_port_scalar"
    simulation.net.decl 0 in 0 : !simulation.logic<65> design
        {resolution_kind = 3 : i32}
    simulation.net.decl 1 in 0 : !simulation.logic<65> design
        {resolution_kind = 3 : i32}
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 65 reversed = false
    simulation.driver.decl 0 in 0 drives 1 : !simulation.logic<65> design
    simulation.func @wired_port_scalar(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %driver: !simulation.driver<!simulation.logic<65>>
            {simulation.capture_kind = 5 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 8 : i32, code_unit_id = 10 : i64} {
      %value = simulation.logic.constant 1 : i65, 0 : i65 :
          !simulation.logic<65>
      simulation.driver.drive %driver = %value :
          !simulation.driver<!simulation.logic<65>>,
          !simulation.logic<65>
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @wired_port_scalar
// CHECK-COUNT-65: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK: llvm.return

// -----

// Explicit strength rejects the one-driver value-copy equivalence.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @strength_port_scalar {
    simulation.scope.decl 0
    simulation.code_unit.decl 5 in 0 function hierarchy "strength_port_scalar"
    simulation.net.decl 0 in 0 : !simulation.logic<65> design
    simulation.net.decl 1 in 0 : !simulation.logic<65> design
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 65 reversed = false
    simulation.driver.decl 0 in 0 drives 1 : !simulation.logic<65> design {
      strength0 = 3 : i32, strength1 = 6 : i32
    }
    simulation.func @strength_port_scalar(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %driver: !simulation.driver<!simulation.logic<65>>
            {simulation.capture_kind = 5 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %value = simulation.logic.constant 1 : i65, 0 : i65 :
          !simulation.logic<65>
      simulation.driver.drive %driver = %value :
          !simulation.driver<!simulation.logic<65>>,
          !simulation.logic<65>
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @strength_port_scalar
// CHECK-COUNT-65: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK: llvm.return

// -----

// Propagation delay rejects the immediate bulk publication proof.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @delayed_port_scalar {
    simulation.scope.decl 0
    simulation.code_unit.decl 6 in 0 function hierarchy "delayed_port_scalar"
    simulation.net.decl 0 in 0 : !simulation.logic<65> design {
      propagation_delays = array<i64: 1, 1, 1>
    }
    simulation.net.decl 1 in 0 : !simulation.logic<65> design
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 65 reversed = false rhs_dominates = true
    simulation.driver.decl 0 in 0 drives 1 : !simulation.logic<65> design
    simulation.func @delayed_port_scalar(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %driver: !simulation.driver<!simulation.logic<65>>
            {simulation.capture_kind = 5 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %value = simulation.logic.constant 1 : i65, 0 : i65 :
          !simulation.logic<65>
      simulation.driver.drive %driver = %value :
          !simulation.driver<!simulation.logic<65>>,
          !simulation.logic<65>
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @delayed_port_scalar
// CHECK-COUNT-2: llvm.call @obelisk_rt_v1_native_state_store_plane
// CHECK: llvm.return

// -----

// Any pass-switch topology uses the dedicated runtime resolver instead.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @pass_port_scalar {
    simulation.scope.decl 0
    simulation.code_unit.decl 7 in 0 function hierarchy "pass_port_scalar"
    simulation.net.decl 0 in 0 : !simulation.logic<65> design
    simulation.net.decl 1 in 0 : !simulation.logic<65> design
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 65 reversed = false
    simulation.driver.decl 0 in 0 drives 1 : !simulation.logic<65> design
    simulation.net.pass.decl 0 in 0 0[0] to 1[0] width 1 reversed = false
    simulation.func @pass_port_scalar(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %driver: !simulation.driver<!simulation.logic<65>>
            {simulation.capture_kind = 5 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64} {
      %value = simulation.logic.constant 1 : i65, 0 : i65 :
          !simulation.logic<65>
      simulation.driver.drive %driver = %value :
          !simulation.driver<!simulation.logic<65>>,
          !simulation.logic<65>
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @pass_port_scalar
// CHECK: llvm.call @obelisk_rt_v1_scheduler_resolve_drivers
// CHECK-NOT: llvm.call @obelisk_rt_v1_scheduler_signal_transition
// CHECK: llvm.return

// -----

// A language force/assign anywhere in the module revokes direct fixed-handle
// authorization, so wide aliases retain override-aware scalar publication.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @override_port_scalar {
    simulation.scope.decl 0
    simulation.code_unit.decl 8 in 0 function hierarchy "override_port_scalar"
    simulation.net.decl 0 in 0 : !simulation.logic<65> design
    simulation.net.decl 1 in 0 : !simulation.logic<65> design
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 65 reversed = false
    simulation.driver.decl 0 in 0 drives 1 : !simulation.logic<65> design
    simulation.func @override_port_scalar(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %driver: !simulation.driver<!simulation.logic<65>>
            {simulation.capture_kind = 5 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 8 : i32, code_unit_id = 8 : i64} {
      %value = simulation.logic.constant 1 : i65, 0 : i65 :
          !simulation.logic<65>
      %net = simulation.context.net %ctx[0] :
          !simulation.net<!simulation.logic<65>>
      simulation.override %net = %value assign false :
          !simulation.net<!simulation.logic<65>>, !simulation.logic<65>
      simulation.driver.drive %driver = %value :
          !simulation.driver<!simulation.logic<65>>,
          !simulation.logic<65>
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @override_port_scalar
// CHECK: llvm.call @obelisk_rt_v1_native_override
// CHECK-COUNT-65: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK: llvm.return
