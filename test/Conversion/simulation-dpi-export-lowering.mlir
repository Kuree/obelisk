// RUN: obelisk-opt %s --obelisk-sim-materialize-dpi-exports --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE --check-prefix=COMMON
// RUN: obelisk-opt %s --obelisk-sim-materialize-dpi-exports --encode-obelisk-sim-to-bytecode='vpi=off require-bytecode=true' --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=BYTECODE --check-prefix=COMMON
// RUN: obelisk-opt %s --obelisk-sim-materialize-dpi-exports --encode-obelisk-sim-to-bytecode='vpi=off require-bytecode=false' --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=AUTO --check-prefix=COMMON
// RUN: obelisk-opt %s --obelisk-sim-materialize-dpi-exports --convert-obelisk-sim-processes-to-llvm-coroutines | mlir-translate --allow-unregistered-dialect --mlir-to-llvmir | opt -passes=verify -disable-output

// Runtime behavior is checked in ../Runtime/simulation-dpi-export-lowering.test.

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  simulation.has_dpi_exports
} {
  simulation.design @exports {
    simulation.scope.decl 0 hierarchy "exports"
    simulation.scope.decl 1 parent 0 hierarchy "exports.child"
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 1 : i8 design
    simulation.code_unit.decl 1 in 0 root_initializer
        hierarchy "exports.root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "exports.initial"
    simulation.code_unit.decl 10 in 0 function hierarchy "exports.narrow"
    simulation.code_unit.decl 11 in 0 function hierarchy "exports.vector"
    simulation.code_unit.decl 12 in 0 function hierarchy "exports.output"
    simulation.code_unit.decl 13 in 0 function hierarchy "exports.ping"
    simulation.code_unit.decl 14 in 0 function hierarchy "exports.logic"
    simulation.code_unit.decl 15 in 0 function hierarchy "exports.branch"
    simulation.code_unit.decl 16 in 0 function hierarchy "exports.echo"
    simulation.code_unit.decl 17 in 0 function hierarchy "exports.reenter"
    simulation.code_unit.decl 18 in 0 function hierarchy "exports.scoped"
    simulation.code_unit.decl 19 in 1 function hierarchy "exports.child.scoped"
    simulation.code_unit.decl 20 in 0 function hierarchy "exports.host" {
      simulation.dpi_abi_signature = [],
      simulation.dpi_c_identifier = "host",
      simulation.dpi_import,
      simulation.dpi_import_id = 200 : i32,
      simulation.dpi_logical_inputs = 0 : i32
    }
    simulation.code_unit.decl 21 in 0 function hierarchy "exports.nested" {
      simulation.dpi_abi_signature = [
        #simulation.dpi_abi<kind = string, direction = result, width = 64,
                              fourState = false, isSigned = false>
      ],
      simulation.dpi_c_identifier = "nested_echo",
      simulation.dpi_import,
      simulation.dpi_import_id = 201 : i32,
      simulation.dpi_logical_inputs = 0 : i32
    }

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %initial = simulation.spawn @initial(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func private @initial(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %status = simulation.dpi.call "host" id 200 scope 0
          context %ctx : !simulation.context() {
            abi_signature = [], is_context = true, is_pure = false,
            is_task = false, source_column = 1 : i32,
            source_file = "dpi-export-runtime.mlir", source_line = 1 : i32
          } : () -> !runtime.status
      simulation.return
    }

    simulation.func @narrow(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %signed: i8 {simulation.capture_kind = 2 : i32},
        %bit: i1 {simulation.capture_kind = 2 : i32},
        %logic: !simulation.logic<1>
            {simulation.capture_kind = 2 : i32}) -> i8
        attributes {
          code_unit_id = 10 : i64, entry_kind = 8 : i32,
          simulation.dpi_export,
          simulation.dpi_c_identifier = "export_narrow",
          simulation.dpi_export_id = -2001470629 : i32,
          simulation.dpi_scope_id = 0 : i64,
          simulation.dpi_logical_inputs = 3 : i32,
          simulation.dpi_abi_signature = [
            #simulation.dpi_abi<kind = byte, direction = input, width = 8,
                                  fourState = false, isSigned = true>,
            #simulation.dpi_abi<kind = bit, direction = input, width = 1,
                                  fourState = false, isSigned = true>,
            #simulation.dpi_abi<kind = logic, direction = input, width = 1,
                                  fourState = true, isSigned = true>,
            #simulation.dpi_abi<kind = byte, direction = result, width = 8,
                                  fourState = false, isSigned = true>
          ]
        } {
      %extended = arith.extui %bit : i1 to i8
      %sum = arith.addi %signed, %extended : i8
      simulation.return %sum : i8
    }

    simulation.func @vector(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<37>
            {simulation.capture_kind = 2 : i32}) -> !simulation.logic<37>
        attributes {
          code_unit_id = 11 : i64, entry_kind = 8 : i32,
          simulation.dpi_export,
          simulation.dpi_c_identifier = "export_vector",
          simulation.dpi_export_id = 101 : i32,
          simulation.dpi_scope_id = 0 : i64,
          simulation.dpi_logical_inputs = 1 : i32,
          simulation.dpi_abi_signature = [
            #simulation.dpi_abi<kind = logic_vector, direction = input,
                                  width = 37, fourState = true,
                                  isSigned = false>,
            #simulation.dpi_abi<kind = logic_vector, direction = result,
                                  width = 37, fourState = true,
                                  isSigned = false>
          ]
        } {
      simulation.return %value : !simulation.logic<37>
    }

    simulation.func @output(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %unused: i16 {simulation.capture_kind = 2 : i32}) -> i16
        attributes {
          code_unit_id = 12 : i64, entry_kind = 8 : i32,
          simulation.dpi_export,
          simulation.dpi_c_identifier = "export_output",
          simulation.dpi_export_id = 102 : i32,
          simulation.dpi_scope_id = 0 : i64,
          simulation.dpi_logical_inputs = 1 : i32,
          simulation.dpi_abi_signature = [
            #simulation.dpi_abi<kind = shortint, direction = output,
                                  width = 16, fourState = false,
                                  isSigned = true>,
            #simulation.dpi_abi<kind = shortint, direction = output,
                                  width = 16, fourState = false,
                                  isSigned = true>
          ]
        } {
      %answer = arith.constant 4660 : i16
      simulation.return %answer : i16
    }

    simulation.func @ping(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {
          code_unit_id = 13 : i64, entry_kind = 8 : i32,
          simulation.dpi_export,
          simulation.dpi_c_identifier = "export_ping",
          simulation.dpi_export_id = 103 : i32,
          simulation.dpi_scope_id = 0 : i64,
          simulation.dpi_logical_inputs = 0 : i32,
          simulation.dpi_abi_signature = []
        } {
      simulation.return
    }

    simulation.func @logic(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<1>
            {simulation.capture_kind = 2 : i32}) -> !simulation.logic<1>
        attributes {
          code_unit_id = 14 : i64, entry_kind = 8 : i32,
          simulation.dpi_export,
          simulation.dpi_c_identifier = "export_logic",
          simulation.dpi_export_id = 104 : i32,
          simulation.dpi_scope_id = 0 : i64,
          simulation.dpi_logical_inputs = 1 : i32,
          simulation.dpi_abi_signature = [
            #simulation.dpi_abi<kind = logic, direction = input, width = 1,
                                  fourState = true, isSigned = true>,
            #simulation.dpi_abi<kind = logic, direction = result, width = 1,
                                  fourState = true, isSigned = true>
          ]
        } {
      simulation.return %value : !simulation.logic<1>
    }

    simulation.func @branch_capture(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i8 {simulation.capture_kind = 2 : i32},
        %bias: !simulation.ref<i8>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64}) -> i8
        attributes {
          code_unit_id = 15 : i64, entry_kind = 8 : i32,
          simulation.dpi_export,
          simulation.dpi_c_identifier = "export_branch_capture",
          simulation.dpi_export_id = 105 : i32,
          simulation.dpi_scope_id = 0 : i64,
          simulation.dpi_logical_inputs = 1 : i32,
          simulation.dpi_abi_signature = [
            #simulation.dpi_abi<kind = byte, direction = input, width = 8,
                                  fourState = false, isSigned = true>,
            #simulation.dpi_abi<kind = byte, direction = result, width = 8,
                                  fourState = false, isSigned = true>
          ]
        } {
      %zero = arith.constant 0 : i8
      %positive = arith.cmpi sgt, %value, %zero : i8
      cf.cond_br %positive, ^captured, ^original
    ^captured:
      %captured = simulation.ref.load %bias : !simulation.ref<i8> -> i8
      simulation.return %captured : i8
    ^original:
      simulation.return %value : i8
    }

    simulation.func @echo_string(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.string
            {simulation.capture_kind = 2 : i32}) -> !simulation.string
        attributes {
          code_unit_id = 16 : i64, entry_kind = 8 : i32,
          simulation.dpi_export,
          simulation.dpi_c_identifier = "export_echo_string",
          simulation.dpi_export_id = 106 : i32,
          simulation.dpi_scope_id = 0 : i64,
          simulation.dpi_logical_inputs = 1 : i32,
          simulation.dpi_abi_signature = [
            #simulation.dpi_abi<kind = string, direction = input, width = 64,
                                  fourState = false, isSigned = false>,
            #simulation.dpi_abi<kind = string, direction = result, width = 64,
                                  fourState = false, isSigned = false>
          ]
        } {
      simulation.return %value : !simulation.string
    }

    simulation.func @reenter_string(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        -> !simulation.string
        attributes {
          code_unit_id = 17 : i64, entry_kind = 8 : i32,
          simulation.dpi_export,
          simulation.dpi_c_identifier = "export_reenter_string",
          simulation.dpi_export_id = 107 : i32,
          simulation.dpi_scope_id = 0 : i64,
          simulation.dpi_logical_inputs = 0 : i32,
          simulation.dpi_abi_signature = [
            #simulation.dpi_abi<kind = string, direction = result, width = 64,
                                  fourState = false, isSigned = false>
          ]
        } {
      %nested:2 = simulation.dpi.call "nested_echo" id 201 scope 0
          context %ctx : !simulation.context() {
            abi_signature = [
              #simulation.dpi_abi<kind = string, direction = result,
                                    width = 64, fourState = false,
                                    isSigned = false>
            ],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "dpi-export-runtime.mlir",
            source_line = 2 : i32
          } : () -> (!simulation.string, !runtime.status)
      simulation.return %nested#0 : !simulation.string
    }

    simulation.func @scoped0(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %state: !simulation.ref<i8>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 1 : i64}) -> i8
        attributes {
          code_unit_id = 18 : i64, entry_kind = 8 : i32,
          simulation.dpi_export,
          simulation.dpi_c_identifier = "export_scoped",
          simulation.dpi_export_id = 108 : i32,
          simulation.dpi_scope_id = 0 : i64,
          simulation.dpi_logical_inputs = 0 : i32,
          simulation.dpi_abi_signature = [
            #simulation.dpi_abi<kind = byte, direction = result, width = 8,
                                  fourState = false, isSigned = true>
          ]
        } {
      %value = simulation.ref.load %state : !simulation.ref<i8> -> i8
      %eleven = arith.constant 11 : i8
      %result = arith.addi %value, %eleven : i8
      simulation.return %result : i8
    }

    simulation.func @scoped1(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %state: !simulation.ref<i8>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 2 : i64}) -> i8
        attributes {
          code_unit_id = 19 : i64, entry_kind = 8 : i32,
          simulation.dpi_export,
          simulation.dpi_c_identifier = "export_scoped",
          simulation.dpi_export_id = 108 : i32,
          simulation.dpi_scope_id = 1 : i64,
          simulation.dpi_logical_inputs = 0 : i32,
          simulation.dpi_abi_signature = [
            #simulation.dpi_abi<kind = byte, direction = result, width = 8,
                                  fourState = false, isSigned = true>
          ]
        } {
      %value = simulation.ref.load %state : !simulation.ref<i8> -> i8
      %twentytwo = arith.constant 22 : i8
      %result = arith.addi %value, %twentytwo : i8
      simulation.return %result : i8
    }
  }
}

// COMMON-DAG: llvm.func @export_narrow(%{{.*}}: i8 {llvm.signext}, %{{.*}}: i8 {llvm.zeroext}, %{{.*}}: i8 {llvm.zeroext}) -> (i8 {llvm.signext})
// COMMON-DAG: llvm.and {{.*}}, %{{.*}} : i8
// COMMON-DAG: llvm.func @export_vector(%{{.*}}: !llvm.ptr, %{{.*}}: !llvm.ptr)
// COMMON-DAG: llvm.call @obelisk_rt_v1_dpi_export_unpack_vector
// COMMON-DAG: llvm.call @obelisk_rt_v1_dpi_export_pack_vector
// COMMON-DAG: llvm.func @export_output(%{{.*}}: !llvm.ptr)
// COMMON-DAG: llvm.store {{.*}}, {{.*}} {alignment = 1 : i64} : i16, !llvm.ptr
// COMMON-DAG: llvm.func @export_ping()
// COMMON-DAG: llvm.func @export_logic(%{{.*}}: i8 {llvm.zeroext}) -> (i8 {llvm.zeroext})
// COMMON-DAG: llvm.func @export_branch_capture(%{{.*}}: i8 {llvm.signext}) -> (i8 {llvm.signext})
// COMMON-DAG: llvm.func @export_echo_string
// COMMON-DAG: llvm.func @export_reenter_string
// COMMON-DAG: llvm.func @export_scoped
// COMMON-DAG: llvm.call @obelisk_rt_v1_export_call
// COMMON-DAG: llvm.mlir.global internal constant @__obelisk_dpi_exports_v1
// NATIVE-NOT: obelisk.feature.dpi_export_bytecode
// NATIVE-NOT: @obelisk_rt_v1_dpi_export_bytecode_link_anchor
// BYTECODE-DAG: obelisk.feature.dpi_export_bytecode
// BYTECODE-DAG: llvm.call @obelisk_rt_v1_dpi_export_bytecode_link_anchor
// AUTO-NOT: obelisk.feature.dpi_export_bytecode
// AUTO-NOT: @obelisk_rt_v1_dpi_export_bytecode_link_anchor
