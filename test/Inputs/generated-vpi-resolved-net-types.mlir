module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @resolved_net_types {
    simulation.scope.decl 0 hierarchy "top"
    simulation.net.decl 0 in 0 : !simulation.logic<4> design hierarchy "top.sink" {
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 1 : i32>
      ]>
    }
    simulation.net.decl 1 in 0 : !simulation.logic<1> design hierarchy "top.wand" {
      resolution_kind = 3 : i32,
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 2 : i32>
      ]>
    }
    simulation.net.decl 2 in 0 : !simulation.logic<1> design hierarchy "top.triand" {
      resolution_kind = 3 : i32,
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 8 : i32>
      ]>
    }
    simulation.net.decl 3 in 0 : !simulation.logic<1> design hierarchy "top.wor" {
      resolution_kind = 4 : i32,
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 3 : i32>
      ]>
    }
    simulation.net.decl 4 in 0 : !simulation.logic<1> design hierarchy "top.trior" {
      resolution_kind = 4 : i32,
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 9 : i32>
      ]>
    }
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false rhs_dominates = true
    simulation.net.connect.decl 1 in 0 0[1] to 2[0] width 1 reversed = false rhs_dominates = true
    simulation.net.connect.decl 2 in 0 0[2] to 3[0] width 1 reversed = false rhs_dominates = true
    simulation.net.connect.decl 3 in 0 0[3] to 4[0] width 1 reversed = false rhs_dominates = true
    // The same exact types survive when the dominating endpoint is written on
    // the left side of the connection record.
    simulation.net.decl 16 in 0 : !simulation.logic<4> design hierarchy "top.sink_reverse" {
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 1 : i32>
      ]>
    }
    simulation.net.connect.decl 12 in 0 1[0] to 16[0] width 1 reversed = false rhs_dominates = false
    simulation.net.connect.decl 13 in 0 2[0] to 16[1] width 1 reversed = false rhs_dominates = false
    simulation.net.connect.decl 14 in 0 3[0] to 16[2] width 1 reversed = false rhs_dominates = false
    simulation.net.connect.decl 15 in 0 4[0] to 16[3] width 1 reversed = false rhs_dominates = false

    // Two exact wand endpoints are ambiguous physically but unanimous for VPI.
    simulation.net.decl 5 in 0 : !simulation.logic<1> design hierarchy "top.unanimous" {
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 1 : i32>
      ]>
    }
    simulation.net.decl 6 in 0 : !simulation.logic<1> design hierarchy "top.wand_a" {
      resolution_kind = 3 : i32,
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 2 : i32>
      ]>
    }
    simulation.net.decl 7 in 0 : !simulation.logic<1> design hierarchy "top.wand_b" {
      resolution_kind = 3 : i32,
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 2 : i32>
      ]>
    }
    simulation.net.connect.decl 4 in 0 5[0] to 6[0] width 1 reversed = false rhs_dominates = true
    simulation.net.connect.decl 5 in 0 5[0] to 7[0] width 1 reversed = false rhs_dominates = true
    // Equal adjacent results are serialized as one canonical run.
    simulation.net.decl 17 in 0 : !simulation.logic<2> design hierarchy "top.coalesced" {
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 1 : i32>
      ]>
    }
    simulation.net.connect.decl 16 in 0 17[0] to 6[0] width 1 reversed = false rhs_dominates = true
    simulation.net.connect.decl 17 in 0 17[1] to 7[0] width 1 reversed = false rhs_dominates = true

    // Exact wand and triand endpoints disagree despite sharing one execution
    // resolution category, so the source intentionally has no run.
    simulation.net.decl 8 in 0 : !simulation.logic<1> design hierarchy "top.mixed" {
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 1 : i32>
      ]>
    }
    simulation.net.decl 13 in 0 : !simulation.logic<1> design hierarchy "top.mixed_wand" {
      resolution_kind = 3 : i32,
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 2 : i32>
      ]>
    }
    simulation.net.decl 14 in 0 : !simulation.logic<1> design hierarchy "top.mixed_triand" {
      resolution_kind = 3 : i32,
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 8 : i32>
      ]>
    }
    simulation.net.connect.decl 6 in 0 8[0] to 13[0] width 1 reversed = false rhs_dominates = true
    simulation.net.connect.decl 7 in 0 8[0] to 14[0] width 1 reversed = false rhs_dominates = true

    // An untyped interconnect inherits an exact built-in dominant endpoint.
    simulation.net.decl 9 in 0 : !simulation.logic<1> design hierarchy "top.interconnect"
    simulation.net.decl 15 in 0 : !simulation.logic<1> design hierarchy "top.interconnect_wand" {
      resolution_kind = 3 : i32,
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 2 : i32>
      ]>
    }
    simulation.net.connect.decl 8 in 0 9[0] to 15[0] width 1 reversed = false rhs_dominates = true

    // A dominance cycle has no endpoint from which an exact subtype can be
    // recovered, so neither member receives a run.
    simulation.net.decl 10 in 0 : !simulation.logic<1> design hierarchy "top.cycle_a" {
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 1 : i32>
      ]>
    }
    simulation.net.decl 11 in 0 : !simulation.logic<1> design hierarchy "top.cycle_b" {
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 1 : i32>
      ]>
    }
    simulation.net.decl 12 in 0 : !simulation.logic<1> design hierarchy "top.cycle_c" {
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 1 : i32>
      ]>
    }
    simulation.net.connect.decl 9 in 0 10[0] to 11[0] width 1 reversed = false rhs_dominates = true
    simulation.net.connect.decl 10 in 0 11[0] to 12[0] width 1 reversed = false rhs_dominates = true
    simulation.net.connect.decl 11 in 0 10[0] to 12[0] width 1 reversed = false rhs_dominates = false
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}
