// RUN: obelisk-opt %s | FileCheck %s

// CHECK: func.func private @abi_records(!obelisk_rt.handle, !obelisk_rt.action, !obelisk_rt.fragment, !obelisk_rt.bytecode, !obelisk_rt.bytecode_entry, !obelisk_rt.bytecode_validation, !obelisk_rt.bytecode_operand, !obelisk_rt.bytecode_service_site, !obelisk_rt.opcode, !obelisk_rt.bytecode_type, !obelisk_rt.bytecode_operand_kind, !obelisk_rt.bytecode_operand_direction, !obelisk_rt.bytecode_service_value, !obelisk_rt.bytecode_service, !obelisk_rt.arg, !obelisk_rt.cstring)
module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8"
} {
func.func private @abi_records(!obelisk_rt.handle, !obelisk_rt.action,
    !obelisk_rt.fragment, !obelisk_rt.bytecode, !obelisk_rt.bytecode_entry,
    !obelisk_rt.bytecode_validation, !obelisk_rt.bytecode_operand,
    !obelisk_rt.bytecode_service_site, !obelisk_rt.opcode,
    !obelisk_rt.bytecode_type, !obelisk_rt.bytecode_operand_kind,
    !obelisk_rt.bytecode_operand_direction, !obelisk_rt.bytecode_service_value,
    !obelisk_rt.bytecode_service, !obelisk_rt.arg, !obelisk_rt.cstring)

func.func private @identity(!obelisk_rt.context) -> !obelisk_rt.context
func.func private @tuple_identity(tuple<!obelisk_rt.context>)
    -> tuple<!obelisk_rt.context>

func.func @call_indirect(%ctx: !obelisk_rt.context)
    -> !obelisk_rt.context {
  %callee = func.constant @identity :
      (!obelisk_rt.context) -> !obelisk_rt.context
  %result = func.call_indirect %callee(%ctx) :
      (!obelisk_rt.context) -> !obelisk_rt.context
  return %result : !obelisk_rt.context
}

func.func @call_and_branch(%ctx: !obelisk_rt.context, %condition: i1)
    -> !obelisk_rt.context {
  %called = func.call @identity(%ctx) :
      (!obelisk_rt.context) -> !obelisk_rt.context
  cf.cond_br %condition, ^called(%called : !obelisk_rt.context),
      ^original(%ctx : !obelisk_rt.context)
^called(%called_result: !obelisk_rt.context):
  return %called_result : !obelisk_rt.context
^original(%original_result: !obelisk_rt.context):
  return %original_result : !obelisk_rt.context
}

func.func @loop_alloca(%ctx: !obelisk_rt.context, %fd: !obelisk_rt.fd,
    %again: i1) {
  cf.br ^loop
^loop:
  %status, %byte = obelisk_rt.file.getc %ctx, %fd :
      (!obelisk_rt.context, !obelisk_rt.fd) -> (!obelisk_rt.status, i8)
  cf.cond_br %again, ^loop, ^exit
^exit:
  return
}

func.func @materializer_roundtrip(%status: !obelisk_rt.status) -> i1 {
  %empty = obelisk_rt.argument.empty : () -> !obelisk_rt.arg
  %arguments = obelisk_rt.argument.array %empty :
      (!obelisk_rt.arg) -> !obelisk_rt.args
  %bits = obelisk_rt.status.to_bits %status : (!obelisk_rt.status) -> i32
  %roundtrip = obelisk_rt.status.from_bits %bits :
      (i32) -> !obelisk_rt.status
  %same = obelisk_rt.status.is %roundtrip, 0
  return %same : i1
}

func.func @managed_object_argument(%object: i64) {
  %argument = obelisk_rt.argument.managed_object %object :
      (i64) -> !obelisk_rt.arg
  %arguments = obelisk_rt.argument.array %argument :
      (!obelisk_rt.arg) -> !obelisk_rt.args
  return
}

func.func @virtual_interface_argument(%scope: i64) {
  %argument = obelisk_rt.argument.virtual_interface %scope :
      (i64) -> !obelisk_rt.arg
  %arguments = obelisk_rt.argument.array %argument :
      (!obelisk_rt.arg) -> !obelisk_rt.args
  return
}

func.func @cross_block_pure_arguments(%bytes: !obelisk_rt.bytes) {
  %empty = obelisk_rt.argument.empty : () -> !obelisk_rt.arg
  %string = obelisk_rt.argument.bytes %bytes {is_format_string = true} :
      (!obelisk_rt.bytes) -> !obelisk_rt.arg
  cf.br ^consumer
^consumer:
  %arguments = obelisk_rt.argument.array %empty, %string :
      (!obelisk_rt.arg, !obelisk_rt.arg) -> !obelisk_rt.args
  return
}

// CHECK-LABEL: func.func @runtime_calls
func.func @runtime_calls(
    %ctx: !obelisk_rt.context, %input_status: !obelisk_rt.status,
    %bytes: !obelisk_rt.bytes, %mut_bytes: !obelisk_rt.mut_bytes,
    %args: !obelisk_rt.args, %env: !obelisk_rt.format_env,
    %fd: !obelisk_rt.fd, %fragment: !obelisk_rt.fragment,
    %continuation: i32, %limit: i64, %offset: i64, %byte: i8,
    %newline: i1) {
  // CHECK: obelisk_rt.context.create
  %create_status, %created = obelisk_rt.context.create : () ->
      (!obelisk_rt.status, !obelisk_rt.context)
  %status_text = obelisk_rt.status.string %input_status :
      (!obelisk_rt.status) -> !obelisk_rt.cstring

  %error_status, %last_error = obelisk_rt.last_error %ctx :
      (!obelisk_rt.context) -> (!obelisk_rt.status, !obelisk_rt.buffer)
  obelisk_rt.buffer.release %last_error : (!obelisk_rt.buffer) -> ()

  %verbosity = arith.constant 1 : i32
  %finish_status = obelisk_rt.finish %ctx, %verbosity :
      (!obelisk_rt.context, i32) -> !obelisk_rt.status
  %stop_status = obelisk_rt.stop %ctx, %verbosity :
      (!obelisk_rt.context, i32) -> !obelisk_rt.status
  %fatal_status = obelisk_rt.fatal %ctx, %verbosity :
      (!obelisk_rt.context, i32) -> !obelisk_rt.status
  %runtime_error_status = obelisk_rt.error %ctx :
      (!obelisk_rt.context) -> !obelisk_rt.status
  %termination_requested = obelisk_rt.termination.requested %ctx :
      (!obelisk_rt.context) -> i1

  // CHECK: obelisk_rt.format
  %format_status, %formatted = obelisk_rt.format %ctx, %bytes, %args, %env :
      (!obelisk_rt.context, !obelisk_rt.bytes, !obelisk_rt.args,
       !obelisk_rt.format_env) -> (!obelisk_rt.status, !obelisk_rt.buffer)
  obelisk_rt.buffer.release %formatted : (!obelisk_rt.buffer) -> ()

  // CHECK: obelisk_rt.string_output_format
  %string_status, %string = obelisk_rt.string_output_format %ctx, %args, %env
      {default_radix = 8 : i32} :
      (!obelisk_rt.context, !obelisk_rt.args, !obelisk_rt.format_env) ->
      (!obelisk_rt.status, i64)

  %display_status = obelisk_rt.display %ctx, %fd, %newline, %args, %env
      {default_radix = 16 : i32} :
      (!obelisk_rt.context, !obelisk_rt.fd, i1, !obelisk_rt.args,
       !obelisk_rt.format_env) -> !obelisk_rt.status

  %mcd_status, %mcd = obelisk_rt.file.open_mcd %ctx, %bytes :
      (!obelisk_rt.context, !obelisk_rt.bytes) ->
      (!obelisk_rt.status, !obelisk_rt.fd)
  %open_status, %opened = obelisk_rt.file.open %ctx, %bytes, %bytes :
      (!obelisk_rt.context, !obelisk_rt.bytes, !obelisk_rt.bytes) ->
      (!obelisk_rt.status, !obelisk_rt.fd)
  %close_status = obelisk_rt.file.close %ctx, %fd :
      (!obelisk_rt.context, !obelisk_rt.fd) -> !obelisk_rt.status
  %flush_status = obelisk_rt.file.flush %ctx, %fd :
      (!obelisk_rt.context, !obelisk_rt.fd) -> !obelisk_rt.status
  %write_status, %written = obelisk_rt.file.write %ctx, %fd, %bytes :
      (!obelisk_rt.context, !obelisk_rt.fd, !obelisk_rt.bytes) ->
      (!obelisk_rt.status, i64)
  %read_status, %read = obelisk_rt.file.read %ctx, %fd, %mut_bytes :
      (!obelisk_rt.context, !obelisk_rt.fd, !obelisk_rt.mut_bytes) ->
      (!obelisk_rt.status, i64)
  %radix = arith.constant 16 : i32
  %bit_width = arith.constant 65 : i64
  %token_status, %token_kind, %token_address =
      obelisk_rt.file.readmem_token %ctx, %fd, %radix, %bit_width,
          %mut_bytes, %mut_bytes :
      (!obelisk_rt.context, !obelisk_rt.fd, i32, i64, !obelisk_rt.mut_bytes,
       !obelisk_rt.mut_bytes) -> (!obelisk_rt.status, i32, i64)
  %getc_status, %read_byte = obelisk_rt.file.getc %ctx, %fd :
      (!obelisk_rt.context, !obelisk_rt.fd) -> (!obelisk_rt.status, i8)
  %ungetc_status = obelisk_rt.file.ungetc %ctx, %fd, %byte :
      (!obelisk_rt.context, !obelisk_rt.fd, i8) -> !obelisk_rt.status
  %line_status, %line = obelisk_rt.file.getline %ctx, %fd, %limit :
      (!obelisk_rt.context, !obelisk_rt.fd, i64) ->
      (!obelisk_rt.status, !obelisk_rt.buffer)
  obelisk_rt.buffer.release %line : (!obelisk_rt.buffer) -> ()
  %eof_status, %is_eof = obelisk_rt.file.eof %ctx, %fd :
      (!obelisk_rt.context, !obelisk_rt.fd) -> (!obelisk_rt.status, i32)
  %file_error_status, %error_code, %message =
      obelisk_rt.file.error %ctx, %fd :
      (!obelisk_rt.context, !obelisk_rt.fd) ->
      (!obelisk_rt.status, i32, !obelisk_rt.buffer)
  obelisk_rt.buffer.release %message : (!obelisk_rt.buffer) -> ()
  %seek_status = obelisk_rt.file.seek %ctx, %fd, %offset, %continuation :
      (!obelisk_rt.context, !obelisk_rt.fd, i64, i32) -> !obelisk_rt.status
  %tell_status, %position = obelisk_rt.file.tell %ctx, %fd :
      (!obelisk_rt.context, !obelisk_rt.fd) -> (!obelisk_rt.status, i64)
  %rewind_status = obelisk_rt.file.rewind %ctx, %fd :
      (!obelisk_rt.context, !obelisk_rt.fd) -> !obelisk_rt.status

  // CHECK: obelisk_rt.fragment.execute
  %execute_status, %action = obelisk_rt.fragment.execute
      %fragment, %ctx, %mut_bytes, %continuation :
      (!obelisk_rt.fragment, !obelisk_rt.context, !obelisk_rt.mut_bytes, i32)
      -> (!obelisk_rt.status, !obelisk_rt.action)
  %bounded_status, %bounded_action = obelisk_rt.bytecode.execute_bounded
      %fragment, %ctx, %mut_bytes, %continuation, %limit :
      (!obelisk_rt.fragment, !obelisk_rt.context, !obelisk_rt.mut_bytes, i32,
       i64) -> (!obelisk_rt.status, !obelisk_rt.action)
  obelisk_rt.context.destroy %created : (!obelisk_rt.context) -> ()
  return
}
}
