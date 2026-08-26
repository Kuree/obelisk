// RUN: obelisk-opt %s --split-input-file \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

// A wide full-range port alias with one ordinary driver is published as two
// vector ranges.  The proof lives entirely in native lowering; the simulation
// dialect remains one driver.drive and one static topology declaration.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @wide_port_bulk {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "wide_port_bulk"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<65> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<65> design
    obelisk_sim.net.connect.decl 0 in 0 0[0] to 1[0] width 65 reversed = false
    obelisk_sim.driver.decl 0 in 0 drives 1 : !obelisk_sim.logic<65> design
    obelisk_sim.func @wide_port_bulk(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %driver: !obelisk_sim.driver<!obelisk_sim.logic<65>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %value = obelisk_sim.logic.constant 1 : i65, 0 : i65 :
          !obelisk_sim.logic<65>
      obelisk_sim.driver.drive %driver = %value :
          !obelisk_sim.driver<!obelisk_sim.logic<65>>,
          !obelisk_sim.logic<65>
      obelisk_sim.return
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
  obelisk_sim.design @narrow_port_scalar {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "narrow_port_scalar"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<64> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<64> design
    obelisk_sim.net.connect.decl 0 in 0 0[0] to 1[0] width 64 reversed = false
    obelisk_sim.driver.decl 0 in 0 drives 1 : !obelisk_sim.logic<64> design
    obelisk_sim.func @narrow_port_scalar(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %driver: !obelisk_sim.driver<!obelisk_sim.logic<64>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %value = obelisk_sim.logic.constant 1 : i64, 0 : i64 :
          !obelisk_sim.logic<64>
      obelisk_sim.driver.drive %driver = %value :
          !obelisk_sim.driver<!obelisk_sim.logic<64>>,
          !obelisk_sim.logic<64>
      obelisk_sim.return
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
  obelisk_sim.design @competing_port_scalar {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 3 in 0 function hierarchy "competing_port_scalar"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<65> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<65> design
    obelisk_sim.net.connect.decl 0 in 0 0[0] to 1[0] width 65 reversed = false
    obelisk_sim.driver.decl 0 in 0 drives 1 : !obelisk_sim.logic<65> design
    obelisk_sim.driver.decl 1 in 0 drives 0 : !obelisk_sim.logic<65> design
    obelisk_sim.func @competing_port_scalar(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %driver: !obelisk_sim.driver<!obelisk_sim.logic<65>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %value = obelisk_sim.logic.constant 1 : i65, 0 : i65 :
          !obelisk_sim.logic<65>
      obelisk_sim.driver.drive %driver = %value :
          !obelisk_sim.driver<!obelisk_sim.logic<65>>,
          !obelisk_sim.logic<65>
      obelisk_sim.return
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
  obelisk_sim.design @reversed_port_scalar {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 4 in 0 function hierarchy "reversed_port_scalar"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<65> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<65> design
    obelisk_sim.net.connect.decl 0 in 0 0[0] to 1[64] width 65 reversed = true
    obelisk_sim.driver.decl 0 in 0 drives 1 : !obelisk_sim.logic<65> design
    obelisk_sim.func @reversed_port_scalar(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %driver: !obelisk_sim.driver<!obelisk_sim.logic<65>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %value = obelisk_sim.logic.constant 1 : i65, 0 : i65 :
          !obelisk_sim.logic<65>
      obelisk_sim.driver.drive %driver = %value :
          !obelisk_sim.driver<!obelisk_sim.logic<65>>,
          !obelisk_sim.logic<65>
      obelisk_sim.return
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
  obelisk_sim.design @partial_port_scalar {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 9 in 0 function hierarchy "partial_port_scalar"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<66> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<66> design
    obelisk_sim.net.connect.decl 0 in 0 0[0] to 1[0] width 66 reversed = false
    obelisk_sim.driver.decl 0 in 0 drives 1 : !obelisk_sim.logic<66> design
        {driven_low = 1 : i64, driven_width = 65 : i64}
    obelisk_sim.func @partial_port_scalar(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9 : i64} {
      %driver = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<66>>
      %slice = obelisk_sim.driver.extract %driver from 1 :
          !obelisk_sim.driver<!obelisk_sim.logic<66>> ->
          !obelisk_sim.driver<!obelisk_sim.logic<65>>
      %value = obelisk_sim.logic.constant 1 : i65, 0 : i65 :
          !obelisk_sim.logic<65>
      obelisk_sim.driver.drive %slice = %value :
          !obelisk_sim.driver<!obelisk_sim.logic<65>>,
          !obelisk_sim.logic<65>
      obelisk_sim.return
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
  obelisk_sim.design @wired_port_scalar {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 10 in 0 function hierarchy "wired_port_scalar"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<65> design
        {resolution_kind = 3 : i32}
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<65> design
        {resolution_kind = 3 : i32}
    obelisk_sim.net.connect.decl 0 in 0 0[0] to 1[0] width 65 reversed = false
    obelisk_sim.driver.decl 0 in 0 drives 1 : !obelisk_sim.logic<65> design
    obelisk_sim.func @wired_port_scalar(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %driver: !obelisk_sim.driver<!obelisk_sim.logic<65>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 8 : i32, code_unit_id = 10 : i64} {
      %value = obelisk_sim.logic.constant 1 : i65, 0 : i65 :
          !obelisk_sim.logic<65>
      obelisk_sim.driver.drive %driver = %value :
          !obelisk_sim.driver<!obelisk_sim.logic<65>>,
          !obelisk_sim.logic<65>
      obelisk_sim.return
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
  obelisk_sim.design @strength_port_scalar {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 5 in 0 function hierarchy "strength_port_scalar"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<65> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<65> design
    obelisk_sim.net.connect.decl 0 in 0 0[0] to 1[0] width 65 reversed = false
    obelisk_sim.driver.decl 0 in 0 drives 1 : !obelisk_sim.logic<65> design {
      strength0 = 3 : i32, strength1 = 6 : i32
    }
    obelisk_sim.func @strength_port_scalar(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %driver: !obelisk_sim.driver<!obelisk_sim.logic<65>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %value = obelisk_sim.logic.constant 1 : i65, 0 : i65 :
          !obelisk_sim.logic<65>
      obelisk_sim.driver.drive %driver = %value :
          !obelisk_sim.driver<!obelisk_sim.logic<65>>,
          !obelisk_sim.logic<65>
      obelisk_sim.return
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
  obelisk_sim.design @delayed_port_scalar {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 6 in 0 function hierarchy "delayed_port_scalar"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<65> design {
      propagation_delays = array<i64: 1, 1, 1>
    }
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<65> design
    obelisk_sim.net.connect.decl 0 in 0 0[0] to 1[0] width 65 reversed = false rhs_dominates = true
    obelisk_sim.driver.decl 0 in 0 drives 1 : !obelisk_sim.logic<65> design
    obelisk_sim.func @delayed_port_scalar(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %driver: !obelisk_sim.driver<!obelisk_sim.logic<65>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %value = obelisk_sim.logic.constant 1 : i65, 0 : i65 :
          !obelisk_sim.logic<65>
      obelisk_sim.driver.drive %driver = %value :
          !obelisk_sim.driver<!obelisk_sim.logic<65>>,
          !obelisk_sim.logic<65>
      obelisk_sim.return
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
  obelisk_sim.design @pass_port_scalar {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 7 in 0 function hierarchy "pass_port_scalar"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<65> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<65> design
    obelisk_sim.net.connect.decl 0 in 0 0[0] to 1[0] width 65 reversed = false
    obelisk_sim.driver.decl 0 in 0 drives 1 : !obelisk_sim.logic<65> design
    obelisk_sim.net.pass.decl 0 in 0 0[0] to 1[0] width 1 reversed = false
    obelisk_sim.func @pass_port_scalar(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %driver: !obelisk_sim.driver<!obelisk_sim.logic<65>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64} {
      %value = obelisk_sim.logic.constant 1 : i65, 0 : i65 :
          !obelisk_sim.logic<65>
      obelisk_sim.driver.drive %driver = %value :
          !obelisk_sim.driver<!obelisk_sim.logic<65>>,
          !obelisk_sim.logic<65>
      obelisk_sim.return
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
  obelisk_sim.design @override_port_scalar {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 8 in 0 function hierarchy "override_port_scalar"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<65> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<65> design
    obelisk_sim.net.connect.decl 0 in 0 0[0] to 1[0] width 65 reversed = false
    obelisk_sim.driver.decl 0 in 0 drives 1 : !obelisk_sim.logic<65> design
    obelisk_sim.func @override_port_scalar(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %driver: !obelisk_sim.driver<!obelisk_sim.logic<65>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 8 : i32, code_unit_id = 8 : i64} {
      %value = obelisk_sim.logic.constant 1 : i65, 0 : i65 :
          !obelisk_sim.logic<65>
      %net = obelisk_sim.context.net %ctx[0] :
          !obelisk_sim.net<!obelisk_sim.logic<65>>
      obelisk_sim.override %net = %value assign false :
          !obelisk_sim.net<!obelisk_sim.logic<65>>, !obelisk_sim.logic<65>
      obelisk_sim.driver.drive %driver = %value :
          !obelisk_sim.driver<!obelisk_sim.logic<65>>,
          !obelisk_sim.logic<65>
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: llvm.func @override_port_scalar
// CHECK: llvm.call @obelisk_rt_v1_native_override
// CHECK-COUNT-65: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK: llvm.return
