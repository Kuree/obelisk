module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @vpi_nettypes {
    obelisk_sim.scope.decl 0 hierarchy "$root"
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top.d" {vpi_kind = 32 : i32}
    obelisk_sim.vpi_object.anchor @top_d id 0 type 32 in 1 ordinal 0
        hierarchy "top.d" debug "d" {
      backing = #obelisk_sim.vpi_backing<kind = scope, id = 1 : i64>
    }
    obelisk_sim.vpi_object.anchor @resolver id 1 type 20 in 1 parent @top_d
        ordinal 0 hierarchy "top.d.resolve" debug "resolve"
    obelisk_sim.vpi_nettype.decl @base_nt id 0 in 1 owner @top_d
        hierarchy "top.d.base_nt" debug "base_nt" {
      resolution_function = @resolver,
      target_type = #obelisk_sim.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [1, 0], children = [], childNames = []>
    }
    obelisk_sim.vpi_nettype.decl @alias_nt id 1 in 1 owner @top_d
        hierarchy "top.d.alias_nt" debug "alias_nt" {
      direct_alias = @base_nt, resolution_function = @resolver,
      target_type = #obelisk_sim.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [1, 0], children = [], childNames = []>
    }
    obelisk_sim.net.decl 0 in 1 : !obelisk_sim.logic<2> design
        hierarchy "top.d.n" debug "n" {
      nettype = @alias_nt,
      vpi_properties = #obelisk_sim.vpi_properties<[
        #obelisk_sim.vpi_property<selector = 22 : i32, value = 14 : i32>
      ]>,
      vpi_type = #obelisk_sim.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [1, 0], children = [], childNames = []>
    }
    obelisk_sim.vpi_net_identity.decl 0 backed_by 0 in 1
        : !obelisk_sim.logic<2> hierarchy "top.d.alias_n" debug "alias_n" {
      nettype = @alias_nt,
      vpi_properties = #obelisk_sim.vpi_properties<[
        #obelisk_sim.vpi_property<selector = 22 : i32, value = 14 : i32>
      ]>,
      vpi_type = #obelisk_sim.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [1, 0], children = [], childNames = []>
    }
    obelisk_sim.vpi_relation.decl <kind = net_identity, id = 0 : i64>
        selector 126 handle ordinal 0 to <kind = net, id = 0 : i64>
    obelisk_sim.net.decl 1 in 1
        : !obelisk_sim.unpacked_array<3 : 0 x !obelisk_sim.logic<2>> design
        hierarchy "top.d.array_n" debug "array_n" {
      nettype = @alias_nt,
      vpi_properties = #obelisk_sim.vpi_properties<[
        #obelisk_sim.vpi_property<selector = 22 : i32, value = 14 : i32>
      ]>,
      vpi_type = #obelisk_sim.vpi_type<kind = unpacked_array,
          isSigned = false, isFourState = true, range = [3, 0], children = [
            #obelisk_sim.vpi_type<kind = logic, isSigned = false,
                isFourState = true, range = [1, 0], children = [],
                childNames = []>
          ], childNames = []>
    }
    obelisk_sim.vpi_net_identity.decl 1 backed_by 1 in 1
        : !obelisk_sim.unpacked_array<3 : 0 x !obelisk_sim.logic<2>>
        hierarchy "top.d.alias_array_n" debug "alias_array_n" {
      nettype = @alias_nt,
      vpi_properties = #obelisk_sim.vpi_properties<[
        #obelisk_sim.vpi_property<selector = 22 : i32, value = 14 : i32>
      ]>,
      vpi_type = #obelisk_sim.vpi_type<kind = unpacked_array,
          isSigned = false, isFourState = true, range = [3, 0], children = [
            #obelisk_sim.vpi_type<kind = logic, isSigned = false,
                isFourState = true, range = [1, 0], children = [],
                childNames = []>
          ], childNames = []>
    }
    obelisk_sim.vpi_relation.decl <kind = net_identity, id = 1 : i64>
        selector 126 handle ordinal 0 to <kind = net, id = 1 : i64>
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.func @initial(%ctx: !obelisk_sim.context
        {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      obelisk_sim.return
    }
  }
}
