module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @vpi_nettypes {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top.d" {vpi_kind = 32 : i32}
    simulation.vpi_object.anchor @top_d id 0 type 32 in 1 ordinal 0
        hierarchy "top.d" debug "d" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>
    }
    simulation.vpi_object.anchor @resolver id 1 type 20 in 1 parent @top_d
        ordinal 0 hierarchy "top.d.resolve" debug "resolve"
    simulation.vpi_nettype.decl @base_nt id 0 in 1 owner @top_d
        hierarchy "top.d.base_nt" debug "base_nt" {
      resolution_function = @resolver,
      target_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [1, 0], children = [], childNames = []>
    }
    simulation.vpi_nettype.decl @alias_nt id 1 in 1 owner @top_d
        hierarchy "top.d.alias_nt" debug "alias_nt" {
      direct_alias = @base_nt, resolution_function = @resolver,
      target_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [1, 0], children = [], childNames = []>
    }
    simulation.net.decl 0 in 1 : !simulation.logic<2> design
        hierarchy "top.d.n" debug "n" {
      nettype = @alias_nt,
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 14 : i32>
      ]>,
      vpi_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [1, 0], children = [], childNames = []>
    }
    simulation.vpi_net_identity.decl 0 backed_by 0 in 1
        : !simulation.logic<2> hierarchy "top.d.alias_n" debug "alias_n" {
      nettype = @alias_nt,
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 14 : i32>
      ]>,
      vpi_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [1, 0], children = [], childNames = []>
    }
    simulation.vpi_relation.decl <kind = net_identity, id = 0 : i64>
        selector 126 handle ordinal 0 to <kind = net, id = 0 : i64>
    simulation.net.decl 1 in 1
        : !simulation.unpacked_array<3 : 0 x !simulation.logic<2>> design
        hierarchy "top.d.array_n" debug "array_n" {
      nettype = @alias_nt,
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 14 : i32>
      ]>,
      vpi_type = #simulation.vpi_type<kind = unpacked_array,
          isSigned = false, isFourState = true, range = [3, 0], children = [
            #simulation.vpi_type<kind = logic, isSigned = false,
                isFourState = true, range = [1, 0], children = [],
                childNames = []>
          ], childNames = []>
    }
    simulation.vpi_net_identity.decl 1 backed_by 1 in 1
        : !simulation.unpacked_array<3 : 0 x !simulation.logic<2>>
        hierarchy "top.d.alias_array_n" debug "alias_array_n" {
      nettype = @alias_nt,
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 14 : i32>
      ]>,
      vpi_type = #simulation.vpi_type<kind = unpacked_array,
          isSigned = false, isFourState = true, range = [3, 0], children = [
            #simulation.vpi_type<kind = logic, isSigned = false,
                isFourState = true, range = [1, 0], children = [],
                childNames = []>
          ], childNames = []>
    }
    simulation.vpi_relation.decl <kind = net_identity, id = 1 : i64>
        selector 126 handle ordinal 0 to <kind = net, id = 1 : i64>
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}
