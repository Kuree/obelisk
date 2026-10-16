// RUN: obelisk-opt %s '--lower-obelisk-to-sim=vpi=full' -o %t.mlir
// RUN: env OBELISK_TEST_INPUT=%t.mlir %obj_root/test/obelisk-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.BuiltinClassAliases

// Direct builtin declarations need canonical specialization inventory even
// when no user class extends a builtin. Typedefs remain separate declarations.
!word = !obelisk.integral<32, true, false, 31 : 0, int>
!mailbox = !obelisk.class_handle<@std::@mailbox>
!semaphore = !obelisk.class_handle<@std::@semaphore>
!process = !obelisk.class_handle<@std::@process>
module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition @top attributes {
    definition_kind = 0 : i32, hierarchical_name = "top", name = "top",
    node_id = 0 : i64} {}
  obelisk.sv.symbol.root @root attributes {
    hierarchical_name = "$root", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @unit attributes {
      hierarchical_name = "$unit", node_id = 2 : i64} {
      obelisk.sv.type.type_alias @sem attributes {
        hierarchical_name = "$unit::Sem", name = "Sem", node_id = 3 : i64,
        semantic_type = !semaphore,
        vpi_typedef_layers = [{aliases = [@root::@unit::@sem], path = array<i64>}]} {}
      obelisk.sv.type.type_alias @process attributes {
        hierarchical_name = "$unit::Process", name = "Process", node_id = 4 : i64,
        semantic_type = !process,
        vpi_typedef_layers = [{aliases = [@root::@unit::@process], path = array<i64>}]} {}
      obelisk.sv.type.type_alias @mailbox attributes {
        hierarchical_name = "$unit::Mailbox", name = "Mailbox", node_id = 5 : i64,
        semantic_type = !mailbox,
        vpi_typedef_layers = [{aliases = [@root::@unit::@mailbox], path = array<i64>}]} {}
    }
    obelisk.sv.symbol.instance @instance attributes {
      hierarchical_name = "top", is_uninstantiated = false, name = "top",
      node_id = 6 : i64, referenced_path = "top", referenced_symbol = @top} {
      obelisk.sv.symbol.instance_body @body attributes {
        hierarchical_name = "top", name = "top", node_id = 7 : i64,
        vpi_scope_kind = 32 : i32, simulation.vpi_top = true,
        simulation.vpi_definition_name = "top",
        time_unit_fs = 1000000 : i64, time_precision_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @sem attributes {
          hierarchical_name = "top.sem", name = "sem", node_id = 8 : i64,
          lifetime = 1 : i32, semantic_type = !semaphore} {}
        obelisk.sv.symbol.variable @sem_alias attributes {
          hierarchical_name = "top.sem_alias", name = "sem_alias", node_id = 9 : i64,
          lifetime = 1 : i32, semantic_type = !semaphore,
          vpi_typedef_layers = [{aliases = [@root::@unit::@sem], path = array<i64>}]} {}
        obelisk.sv.symbol.variable @process attributes {
          hierarchical_name = "top.process", name = "process", node_id = 10 : i64,
          lifetime = 1 : i32, semantic_type = !process} {}
        obelisk.sv.symbol.variable @process_alias attributes {
          hierarchical_name = "top.process_alias", name = "process_alias", node_id = 11 : i64,
          lifetime = 1 : i32, semantic_type = !process,
          vpi_typedef_layers = [{aliases = [@root::@unit::@process], path = array<i64>}]} {}
        obelisk.sv.symbol.variable @mailbox attributes {
          hierarchical_name = "top.mailbox", name = "mailbox", node_id = 12 : i64,
          lifetime = 1 : i32, semantic_type = !mailbox} {}
        obelisk.sv.symbol.variable @mailbox_alias attributes {
          hierarchical_name = "top.mailbox_alias", name = "mailbox_alias", node_id = 13 : i64,
          lifetime = 1 : i32, semantic_type = !mailbox,
          vpi_typedef_layers = [{aliases = [@root::@unit::@mailbox], path = array<i64>}]} {}
      }
    }
  }
  obelisk.sv.symbol.package @std attributes {
    hierarchical_name = "std", name = "std", node_id = 14 : i64} {
    obelisk.sv.type.class_type @semaphore attributes {
      hierarchical_name = "std::semaphore", name = "semaphore", node_id = 15 : i64,
      semantic_type = !obelisk.class_handle<@std::@semaphore>,
      is_abstract = false, is_final = false, is_interface = false,
      is_uninstantiated = false, has_base_constructor_call = false,
      has_cycles = false, bitstream_width = 0 : i64,
      declared_interfaces = [], implemented_interfaces = [],
      generic_parameter_paths = [], generic_parameter_symbols = []} {}
    obelisk.sv.type.class_type @process attributes {
      hierarchical_name = "std::process", name = "process", node_id = 16 : i64,
      semantic_type = !obelisk.class_handle<@std::@process>,
      is_abstract = true, is_final = true, is_interface = false,
      is_uninstantiated = false, has_base_constructor_call = false,
      has_cycles = false, bitstream_width = 0 : i64,
      declared_interfaces = [], implemented_interfaces = [],
      generic_parameter_paths = [], generic_parameter_symbols = []} {}
    obelisk.sv.type.class_type @mailbox attributes {
      hierarchical_name = "std::mailbox#(int)", name = "mailbox", node_id = 17 : i64,
      semantic_type = !mailbox,
      is_abstract = false, is_final = false, is_interface = false,
      is_uninstantiated = false, has_base_constructor_call = false,
      has_cycles = false, bitstream_width = 0 : i64,
      declared_interfaces = [], implemented_interfaces = [],
      generic_parameter_paths = ["std::mailbox#(int)::T"],
      generic_parameter_symbols = [@std::@mailbox::@T]} {
      obelisk.sv.symbol.type_parameter @T attributes {
        hierarchical_name = "std::mailbox#(int)::T", name = "T", node_id = 18 : i64,
        semantic_type = !word} {}
    }
  }
}
