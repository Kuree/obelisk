// RUN: obelisk-opt %s | FileCheck %s

// CHECK: func.func private @abi_records(!runtime.handle, !runtime.action, !runtime.fragment, !runtime.bytecode, !runtime.bytecode_entry, !runtime.bytecode_validation, !runtime.bytecode_operand, !runtime.bytecode_service_site, !runtime.opcode, !runtime.bytecode_type, !runtime.bytecode_operand_kind, !runtime.bytecode_operand_direction, !runtime.bytecode_service_value, !runtime.bytecode_service, !runtime.arg, !runtime.cstring)
module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8"
} {
func.func private @abi_records(!runtime.handle, !runtime.action,
    !runtime.fragment, !runtime.bytecode, !runtime.bytecode_entry,
    !runtime.bytecode_validation, !runtime.bytecode_operand,
    !runtime.bytecode_service_site, !runtime.opcode,
    !runtime.bytecode_type, !runtime.bytecode_operand_kind,
    !runtime.bytecode_operand_direction, !runtime.bytecode_service_value,
    !runtime.bytecode_service, !runtime.arg, !runtime.cstring)

func.func private @identity(!runtime.context) -> !runtime.context
func.func private @tuple_identity(tuple<!runtime.context>)
    -> tuple<!runtime.context>

func.func @call_indirect(%ctx: !runtime.context)
    -> !runtime.context {
  %callee = func.constant @identity :
      (!runtime.context) -> !runtime.context
  %result = func.call_indirect %callee(%ctx) :
      (!runtime.context) -> !runtime.context
  return %result : !runtime.context
}

func.func @call_and_branch(%ctx: !runtime.context, %condition: i1)
    -> !runtime.context {
  %called = func.call @identity(%ctx) :
      (!runtime.context) -> !runtime.context
  cf.cond_br %condition, ^called(%called : !runtime.context),
      ^original(%ctx : !runtime.context)
^called(%called_result: !runtime.context):
  return %called_result : !runtime.context
^original(%original_result: !runtime.context):
  return %original_result : !runtime.context
}

func.func @loop_alloca(%ctx: !runtime.context, %fd: !runtime.fd,
    %again: i1) {
  cf.br ^loop
^loop:
  %status, %byte = runtime.file.getc %ctx, %fd :
      (!runtime.context, !runtime.fd) -> (!runtime.status, i8)
  cf.cond_br %again, ^loop, ^exit
^exit:
  return
}

func.func @materializer_roundtrip(%status: !runtime.status) -> i1 {
  %empty = runtime.argument.empty : () -> !runtime.arg
  %arguments = runtime.argument.array %empty :
      (!runtime.arg) -> !runtime.args
  %bits = runtime.status.to_bits %status : (!runtime.status) -> i32
  %roundtrip = runtime.status.from_bits %bits :
      (i32) -> !runtime.status
  %same = runtime.status.is %roundtrip, <ok>
  return %same : i1
}

func.func @managed_object_argument(%object: i64) {
  %argument = runtime.argument.managed_object %object :
      (i64) -> !runtime.arg
  %arguments = runtime.argument.array %argument :
      (!runtime.arg) -> !runtime.args
  return
}

func.func @virtual_interface_argument(%scope: i64) {
  %argument = runtime.argument.virtual_interface %scope :
      (i64) -> !runtime.arg
  %arguments = runtime.argument.array %argument :
      (!runtime.arg) -> !runtime.args
  return
}

func.func @cross_block_pure_arguments(%bytes: !runtime.bytes) {
  %empty = runtime.argument.empty : () -> !runtime.arg
  %string = runtime.argument.bytes %bytes {is_format_string = true} :
      (!runtime.bytes) -> !runtime.arg
  cf.br ^consumer
^consumer:
  %arguments = runtime.argument.array %empty, %string :
      (!runtime.arg, !runtime.arg) -> !runtime.args
  return
}

// CHECK-LABEL: func.func @runtime_calls
func.func @runtime_calls(
    %ctx: !runtime.context, %input_status: !runtime.status,
    %bytes: !runtime.bytes, %mut_bytes: !runtime.mut_bytes,
    %args: !runtime.args, %env: !runtime.format_env,
    %fd: !runtime.fd, %fragment: !runtime.fragment,
    %continuation: i32, %limit: i64, %offset: i64, %byte: i8,
    %newline: i1) {
  // CHECK: runtime.context.create
  %create_status, %created = runtime.context.create : () ->
      (!runtime.status, !runtime.context)
  %status_text = runtime.status.string %input_status :
      (!runtime.status) -> !runtime.cstring

  %error_status, %last_error = runtime.last_error %ctx :
      (!runtime.context) -> (!runtime.status, !runtime.buffer)
  runtime.buffer.release %last_error : (!runtime.buffer) -> ()

  %verbosity = arith.constant 1 : i32
  %finish_status = runtime.finish %ctx, %verbosity :
      (!runtime.context, i32) -> !runtime.status
  %stop_status = runtime.stop %ctx, %verbosity :
      (!runtime.context, i32) -> !runtime.status
  %fatal_status = runtime.fatal %ctx, %verbosity :
      (!runtime.context, i32) -> !runtime.status
  %runtime_error_status = runtime.error %ctx :
      (!runtime.context) -> !runtime.status
  %termination_requested = runtime.termination.requested %ctx :
      (!runtime.context) -> i1
  %time_input = arith.constant 1.25 : f64
  %time_multiplier = arith.constant 1000 : i64
  %time_precision = arith.constant -12 : i32
  %scaled_time = runtime.time.scan_scale %ctx, %time_input,
      %time_multiplier, %time_precision :
      (!runtime.context, f64, i64, i32) -> f64

  // CHECK: runtime.format
  %format_status, %formatted = runtime.format %ctx, %bytes, %args, %env :
      (!runtime.context, !runtime.bytes, !runtime.args,
       !runtime.format_env) -> (!runtime.status, !runtime.buffer)
  runtime.buffer.release %formatted : (!runtime.buffer) -> ()

  // CHECK: runtime.string_output_format
  %string_status, %string = runtime.string_output_format %ctx, %args, %env
      {default_radix = #runtime.radix<octal>} :
      (!runtime.context, !runtime.args, !runtime.format_env) ->
      (!runtime.status, i64)

  %display_status = runtime.display %ctx, %fd, %newline, %args, %env
      {default_radix = #runtime.radix<hex>} :
      (!runtime.context, !runtime.fd, i1, !runtime.args,
       !runtime.format_env) -> !runtime.status

  %mcd_status, %mcd = runtime.file.open_mcd %ctx, %bytes :
      (!runtime.context, !runtime.bytes) ->
      (!runtime.status, !runtime.fd)
  %open_status, %opened = runtime.file.open %ctx, %bytes, %bytes :
      (!runtime.context, !runtime.bytes, !runtime.bytes) ->
      (!runtime.status, !runtime.fd)
  %close_status = runtime.file.close %ctx, %fd :
      (!runtime.context, !runtime.fd) -> !runtime.status
  %flush_status = runtime.file.flush %ctx, %fd :
      (!runtime.context, !runtime.fd) -> !runtime.status
  %write_status, %written = runtime.file.write %ctx, %fd, %bytes :
      (!runtime.context, !runtime.fd, !runtime.bytes) ->
      (!runtime.status, i64)
  %read_status, %read = runtime.file.read %ctx, %fd, %mut_bytes :
      (!runtime.context, !runtime.fd, !runtime.mut_bytes) ->
      (!runtime.status, i64)
  %radix = arith.constant 16 : i32
  %bit_width = arith.constant 65 : i64
  %token_status, %token_kind, %token_address =
      runtime.file.readmem_token %ctx, %fd, %radix, %bit_width,
          %mut_bytes, %mut_bytes :
      (!runtime.context, !runtime.fd, i32, i64, !runtime.mut_bytes,
       !runtime.mut_bytes) -> (!runtime.status, i32, i64)
  %getc_status, %read_byte = runtime.file.getc %ctx, %fd :
      (!runtime.context, !runtime.fd) -> (!runtime.status, i8)
  %ungetc_status = runtime.file.ungetc %ctx, %fd, %byte :
      (!runtime.context, !runtime.fd, i8) -> !runtime.status
  %line_status, %line = runtime.file.getline %ctx, %fd, %limit :
      (!runtime.context, !runtime.fd, i64) ->
      (!runtime.status, !runtime.buffer)
  runtime.buffer.release %line : (!runtime.buffer) -> ()
  %eof_status, %is_eof = runtime.file.eof %ctx, %fd :
      (!runtime.context, !runtime.fd) -> (!runtime.status, i32)
  %file_error_status, %error_code, %message =
      runtime.file.error %ctx, %fd :
      (!runtime.context, !runtime.fd) ->
      (!runtime.status, i32, !runtime.buffer)
  runtime.buffer.release %message : (!runtime.buffer) -> ()
  %seek_status = runtime.file.seek %ctx, %fd, %offset, %continuation :
      (!runtime.context, !runtime.fd, i64, i32) -> !runtime.status
  %tell_status, %position = runtime.file.tell %ctx, %fd :
      (!runtime.context, !runtime.fd) -> (!runtime.status, i64)
  %rewind_status = runtime.file.rewind %ctx, %fd :
      (!runtime.context, !runtime.fd) -> !runtime.status

  // CHECK: runtime.fragment.execute
  %execute_status, %action = runtime.fragment.execute
      %fragment, %ctx, %mut_bytes, %continuation :
      (!runtime.fragment, !runtime.context, !runtime.mut_bytes, i32)
      -> (!runtime.status, !runtime.action)
  %bounded_status, %bounded_action = runtime.bytecode.execute_bounded
      %fragment, %ctx, %mut_bytes, %continuation, %limit :
      (!runtime.fragment, !runtime.context, !runtime.mut_bytes, i32,
       i64) -> (!runtime.status, !runtime.action)
  runtime.context.destroy %created : (!runtime.context) -> ()
  return
}
}
