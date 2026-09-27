// RUN: obelisk-opt %s | obelisk-opt | FileCheck %s
// RUN: obelisk-opt --mlir-print-op-generic %s | obelisk-opt | FileCheck %s

module attributes {
  test.descriptor = #runtime.descriptor_kind<process>,
  test.action = #runtime.action_kind<suspend>,
  test.suspension = #runtime.suspension_kind<event_order>,
  test.radix = #runtime.radix<hex>,
  test.seek = #runtime.seek_origin<current>,
  test.code = #runtime.code_kind<bytecode>,
  test.value = #runtime.bytecode_value_kind<resource>,
  test.opcode = #runtime.bytecode_opcode<call_service>,
  test.operand = #runtime.bytecode_operand_kind<frame>,
  test.direction = #runtime.bytecode_operand_direction<inout>,
  test.service_value = #runtime.bytecode_service_value_kind<argument_time>,
  test.service = #runtime.bytecode_service<file_seek>,
  test.status = #runtime.status_code<fatal>,
  test.argument = #runtime.argument_kind<managed_string>,
  test.flags = #runtime.argument_flags<signed|format_string|designated_format>
} {
  func.func @status(%status: !runtime.status) -> i1 {
    %eof = runtime.status.is %status, <eof>
    return %eof : i1
  }
}
// CHECK-DAG: test.descriptor = #runtime.descriptor_kind<process>
// CHECK-DAG: test.action = #runtime.action_kind<suspend>
// CHECK-DAG: test.suspension = #runtime.suspension_kind<event_order>
// CHECK-DAG: test.radix = #runtime.radix<hex>
// CHECK-DAG: test.seek = #runtime.seek_origin<current>
// CHECK-DAG: test.code = #runtime.code_kind<bytecode>
// CHECK-DAG: test.value = #runtime.bytecode_value_kind<resource>
// CHECK-DAG: test.opcode = #runtime.bytecode_opcode<call_service>
// CHECK-DAG: test.operand = #runtime.bytecode_operand_kind<frame>
// CHECK-DAG: test.direction = #runtime.bytecode_operand_direction<inout>
// CHECK-DAG: test.service_value = #runtime.bytecode_service_value_kind<argument_time>
// CHECK-DAG: test.service = #runtime.bytecode_service<file_seek>
// CHECK-DAG: test.status = #runtime.status_code<fatal>
// CHECK-DAG: test.argument = #runtime.argument_kind<managed_string>
// CHECK-DAG: test.flags = #runtime.argument_flags<signed|format_string|designated_format>
// CHECK: runtime.status.is %{{.*}}, <eof>
