module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @static_arrays {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" {vpi_kind = 32 : i32}
    simulation.scope.decl 2 parent 1 hierarchy "top.m[1][-1]" {vpi_kind = 32 : i32}
    simulation.scope.decl 3 parent 1 hierarchy "top.m[1][0]" {vpi_kind = 32 : i32}
    simulation.scope.decl 4 parent 1 hierarchy "top.m[0][-1]" {vpi_kind = 32 : i32}
    simulation.scope.decl 5 parent 1 hierarchy "top.m[0][0]" {vpi_kind = 32 : i32}
    simulation.scope.decl 6 parent 1 hierarchy "top.i[2]" {vpi_kind = 601 : i32}
    simulation.scope.decl 7 parent 1 hierarchy "top.p[-2]" {vpi_kind = 602 : i32}
    simulation.scope.decl 8 parent 1 hierarchy "top.generated[-3].child" {vpi_kind = 32 : i32}
    simulation.scope.decl 9 parent 1 hierarchy "top.conditional.child" {vpi_kind = 32 : i32}

    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>
    }
    simulation.vpi_object.anchor @modules id 1 type 112 in 1 parent @top
        ordinal 0 hierarchy "top.m" debug "m" {
      index_ranges = array<i64: 1, 0, -1, 0>
    }
    simulation.vpi_object.anchor @m_1_n1 id 2 type 32 in 2 parent @modules
        ordinal 0 hierarchy "top.m[1][-1]" debug "m[1][-1]" {
      backing = #simulation.vpi_backing<kind = scope, id = 2 : i64>,
      member_indices = array<i64: 1, -1>
    }
    simulation.vpi_object.anchor @m_1_0 id 3 type 32 in 3 parent @modules
        ordinal 1 hierarchy "top.m[1][0]" debug "m[1][0]" {
      backing = #simulation.vpi_backing<kind = scope, id = 3 : i64>,
      member_indices = array<i64: 1, 0>
    }
    simulation.vpi_object.anchor @m_0_n1 id 4 type 32 in 4 parent @modules
        ordinal 2 hierarchy "top.m[0][-1]" debug "m[0][-1]" {
      backing = #simulation.vpi_backing<kind = scope, id = 4 : i64>,
      member_indices = array<i64: 0, -1>
    }
    simulation.vpi_object.anchor @m_0_0 id 5 type 32 in 5 parent @modules
        ordinal 3 hierarchy "top.m[0][0]" debug "m[0][0]" {
      backing = #simulation.vpi_backing<kind = scope, id = 5 : i64>,
      member_indices = array<i64: 0, 0>
    }
    simulation.vpi_object.anchor @interfaces id 6 type 603 in 1 parent @top
        ordinal 1 hierarchy "top.i" debug "i" {
      index_ranges = array<i64: 2, 2>
    }
    simulation.vpi_object.anchor @i_2 id 7 type 601 in 6 parent @interfaces
        ordinal 0 hierarchy "top.i[2]" debug "i[2]" {
      backing = #simulation.vpi_backing<kind = scope, id = 6 : i64>,
      member_indices = array<i64: 2>
    }
    simulation.vpi_object.anchor @programs id 8 type 604 in 1 parent @top
        ordinal 2 hierarchy "top.p" debug "p" {
      index_ranges = array<i64: -2, -2>
    }
    simulation.vpi_object.anchor @p_n2 id 9 type 602 in 7 parent @programs
        ordinal 0 hierarchy "top.p[-2]" debug "p[-2]" {
      backing = #simulation.vpi_backing<kind = scope, id = 7 : i64>,
      member_indices = array<i64: -2>
    }

    simulation.vpi_object.anchor @gate id 10 type 21 in 1 parent @top
        ordinal 3 hierarchy "top.gate" debug "gate" {
      primitive_input_count = 2 : i64
    }
    simulation.vpi_object.anchor @gates id 11 type 111 in 1 parent @top
        ordinal 4 hierarchy "top.ga" debug "ga" {
      index_ranges = array<i64: 3, 3>
    }
    simulation.vpi_object.anchor @gate_3 id 12 type 21 in 1 parent @gates
        ordinal 0 hierarchy "top.ga[3]" debug "ga[3]" {
      member_indices = array<i64: 3>, primitive_input_count = 1 : i64
    }
    simulation.vpi_object.anchor @switch id 13 type 55 in 1 parent @top
        ordinal 5 hierarchy "top.switch" debug "switch" {
      primitive_input_count = 3 : i64
    }
    simulation.vpi_object.anchor @switches id 14 type 117 in 1 parent @top
        ordinal 6 hierarchy "top.sa" debug "sa" {
      index_ranges = array<i64: -1, -1>
    }
    simulation.vpi_object.anchor @switch_n1 id 15 type 55 in 1 parent @switches
        ordinal 0 hierarchy "top.sa[-1]" debug "sa[-1]" {
      member_indices = array<i64: -1>, primitive_input_count = 2 : i64
    }
    simulation.vpi_object.anchor @udp id 16 type 65 in 1 parent @top
        ordinal 7 hierarchy "top.udp" debug "udp" {
      primitive_input_count = 4 : i64
    }
    simulation.vpi_object.anchor @udps id 17 type 118 in 1 parent @top
        ordinal 8 hierarchy "top.ua" debug "ua" {
      index_ranges = array<i64: 9, 9>
    }
    simulation.vpi_object.anchor @udp_9 id 18 type 65 in 1 parent @udps
        ordinal 0 hierarchy "top.ua[9]" debug "ua[9]" {
      member_indices = array<i64: 9>, primitive_input_count = 3 : i64
    }

    simulation.vpi_object.anchor @event id 19 type 34 in 1 parent @top
        ordinal 9 hierarchy "top.event" debug "event"
    simulation.vpi_object.anchor @events id 20 type 129 in 1 parent @top
        ordinal 10 hierarchy "top.events" debug "events" {
      index_ranges = array<i64: 1, 0, -1, 0>
    }
    simulation.vpi_object.anchor @event_1_n1 id 21 type 34 in 1 parent @events
        ordinal 0 hierarchy "top.events[1][-1]" debug "events[1][-1]" {
      member_indices = array<i64: 1, -1>
    }
    simulation.vpi_object.anchor @event_1_0 id 22 type 34 in 1 parent @events
        ordinal 1 hierarchy "top.events[1][0]" debug "events[1][0]" {
      member_indices = array<i64: 1, 0>
    }
    simulation.vpi_object.anchor @event_0_n1 id 23 type 34 in 1 parent @events
        ordinal 2 hierarchy "top.events[0][-1]" debug "events[0][-1]" {
      member_indices = array<i64: 0, -1>
    }
    simulation.vpi_object.anchor @event_0_0 id 24 type 34 in 1 parent @events
        ordinal 3 hierarchy "top.events[0][0]" debug "events[0][0]" {
      member_indices = array<i64: 0, 0>
    }

    simulation.vpi_object.anchor @generated id 25 type 133 in 1 parent @top
        ordinal 11 hierarchy "top.generated" debug "generated" {
      sparse_indices = array<i64: -3, 5>
    }
    simulation.vpi_object.anchor @generated_n3 id 26 type 134 in 1 parent @generated
        ordinal 0 hierarchy "top.generated[-3]" debug "generated[-3]" {
      member_indices = array<i64: -3>
    }
    simulation.vpi_object.anchor @generated_5 id 27 type 134 in 1 parent @generated
        ordinal 1 hierarchy "top.generated[5]" debug "generated[5]" {
      member_indices = array<i64: 5>
    }
    simulation.vpi_object.anchor @empty_generated id 28 type 133 in 1 parent @top
        ordinal 12 hierarchy "top.empty_generated" debug "empty_generated" {
      sparse_indices = array<i64>
    }
    simulation.vpi_object.anchor @generated_child id 29 type 32 in 8 parent @generated_n3
        ordinal 0 hierarchy "top.generated[-3].child" debug "child" {
      backing = #simulation.vpi_backing<kind = scope, id = 8 : i64>
    }
    simulation.vpi_object.anchor @conditional id 30 type 134 in 1 parent @top
        ordinal 13 hierarchy "top.conditional" debug "conditional"
    simulation.vpi_object.anchor @conditional_child id 31 type 32 in 9 parent @conditional
        ordinal 0 hierarchy "top.conditional.child" debug "child" {
      backing = #simulation.vpi_backing<kind = scope, id = 9 : i64>
    }

    simulation.code_unit.decl 1 in 1 initial hierarchy "top.run"
    simulation.func @static_arrays_process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}
