// RUN: env OBELISK_TEST_INPUT=%s \
// RUN:   %obj_root/test/obelisk-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.InterModPathQueries

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @vpi_intermod_path {
    simulation.scope.decl 0 hierarchy "top"
    simulation.scope.decl 1 parent 0 hierarchy "top.child"
    simulation.net.decl 0 in 0 : !simulation.logic<8> design
        hierarchy "top.connected"
    simulation.net.decl 1 in 0 : !simulation.logic<8> design
        hierarchy "top.unconnected"
    simulation.port.decl 0 in 0 source 0 net = true at 0
        : !simulation.logic<8> output ordinal 0 hierarchy "top.output"
        debug "output"
    simulation.port.decl 1 in 1 source 0 net = true at 0
        : !simulation.logic<8> input ordinal 1 hierarchy "top.child.input"
        debug "input"
    simulation.port.decl 2 in 0 source 1 net = true at 0
        : !simulation.logic<8> input ordinal 2 hierarchy "top.other"
        debug "other"
    simulation.port.decl 3 in 0 source 0 net = true at 0
        : !simulation.logic<8> inout ordinal 3 hierarchy "top.inout"
        debug "inout"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.func @initial(%context: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}
