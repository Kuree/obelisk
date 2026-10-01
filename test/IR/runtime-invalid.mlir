// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

func.func @leak(%ctx: !runtime.context) {
  // expected-error @+1 {{owned buffer requires one release and at most one size and one packed read}}
  %status, %message = runtime.last_error %ctx :
      (!runtime.context) -> (!runtime.status, !runtime.buffer)
  return
}

// -----

func.func @double_release(%ctx: !runtime.context) {
  // expected-error @+1 {{owned buffer requires one release and at most one size and one packed read}}
  %status, %message = runtime.last_error %ctx :
      (!runtime.context) -> (!runtime.status, !runtime.buffer)
  runtime.buffer.release %message : (!runtime.buffer) -> ()
  runtime.buffer.release %message : (!runtime.buffer) -> ()
  return
}

// -----

func.func @release_borrowed(%message: !runtime.buffer) {
  // expected-error @+1 {{requires a buffer produced directly by an owned-buffer runtime operation}}
  runtime.buffer.release %message : (!runtime.buffer) -> ()
  return
}

// -----

func.func @release_in_successor(%ctx: !runtime.context) {
  // expected-error @+1 {{owned buffer has an unsupported consumer cf.br}}
  %status, %message = runtime.last_error %ctx :
      (!runtime.context) -> (!runtime.status, !runtime.buffer)
  cf.br ^release(%message : !runtime.buffer)
^release(%forwarded: !runtime.buffer):
  runtime.buffer.release %forwarded : (!runtime.buffer) -> ()
  return
}

// -----

func.func private @steal(!runtime.buffer)
func.func @illegal_transfer(%ctx: !runtime.context) {
  // expected-error @+1 {{owned buffer has an unsupported consumer func.call}}
  %status, %message = runtime.last_error %ctx :
      (!runtime.context) -> (!runtime.status, !runtime.buffer)
  func.call @steal(%message) : (!runtime.buffer) -> ()
  return
}

// -----

func.func @bad_radix(%ctx: !runtime.context, %fd: !runtime.fd,
    %args: !runtime.args, %env: !runtime.format_env, %newline: i1) {
  // expected-error @+1 {{attribute 'default_radix' failed to satisfy constraint}}
  %status = runtime.display %ctx, %fd, %newline, %args, %env
      {default_radix = 3 : i32} :
      (!runtime.context, !runtime.fd, i1, !runtime.args,
       !runtime.format_env) -> !runtime.status
  return
}

// -----

func.func @bad_scratch() {
  // expected-error @+1 {{scratch byte count must be nonnegative}}
  %bytes = runtime.bytes.scratch -1
  return
}

// -----

func.func @bad_byte_container(%value: i32) {
  // expected-error @+1 {{requires a byte span, mutable byte span, or buffer}}
  %size = runtime.bytes.size %value : i32
  return
}

// -----

func.func @bad_unknown_plane(%value: i8, %unknown: i16) {
  // expected-error @+1 {{unknown plane must match the value plane type}}
  %argument = runtime.argument.packed %value, %unknown
      {is_signed = false} : (i8, i16) -> !runtime.arg
  return
}

// -----

func.func @bad_designated_bytes(%value: !runtime.bytes) {
  // expected-error @+1 {{designated format must also be a format string}}
  %argument = runtime.argument.bytes %value
      {designated_format = true, is_format_string = false} :
      (!runtime.bytes) -> !runtime.arg
  return
}

// -----

func.func @bad_designated_managed_string(%value: i64) {
  // expected-error @+1 {{designated format must also be a format string}}
  %argument = runtime.argument.managed_string %value
      {designated_format = true, is_format_string = false} :
      (i64) -> !runtime.arg
  return
}

// -----

func.func @double_size(%ctx: !runtime.context, %fd: !runtime.fd,
    %limit: i64) {
  // expected-error @+1 {{owned buffer requires one release and at most one size and one packed read}}
  %status, %line = runtime.file.getline %ctx, %fd, %limit :
      (!runtime.context, !runtime.fd, i64) ->
      (!runtime.status, !runtime.buffer)
  %first = runtime.bytes.size %line : !runtime.buffer
  %second = runtime.bytes.size %line : !runtime.buffer
  runtime.buffer.release %line : (!runtime.buffer) -> ()
  return
}

// -----

func.func @read_after_release(%ctx: !runtime.context, %fd: !runtime.fd,
    %limit: i64) {
  // expected-error @+1 {{owned buffer size and packed reads must precede its release}}
  %status, %line = runtime.file.getline %ctx, %fd, %limit :
      (!runtime.context, !runtime.fd, i64) ->
      (!runtime.status, !runtime.buffer)
  runtime.buffer.release %line : (!runtime.buffer) -> ()
  %size = runtime.bytes.size %line : !runtime.buffer
  return
}

// -----

func.func @scratch_escape(%count: i64) -> !runtime.mut_bytes {
  // expected-error @+1 {{stack-backed scratch span has an unsupported consumer func.return}}
  %scratch = runtime.bytes.scratch 4
  return %scratch : !runtime.mut_bytes
}

// -----

func.func @bad_time_multiplier() {
  // expected-error @+1 {{time multiplier must be positive}}
  %env = runtime.format.environment {time_multiplier = 0 : i64}
  return
}

// -----

func.func @raw_status(%status: !runtime.status) -> i1 {
  // Generic syntax is needed to give the enum attribute a raw integer type.
  // expected-error @+1 {{attribute 'value' failed to satisfy constraint}}
  %ok = "runtime.status.is"(%status) {value = 0 : i32} : (!runtime.status) -> i1
  return %ok : i1
}

// -----

func.func @wrong_enum(%status: !runtime.status) -> i1 {
  // Generic syntax is needed to use an attribute from the wrong enum.
  // expected-error @+1 {{attribute 'value' failed to satisfy constraint}}
  %ok = "runtime.status.is"(%status) {value = #runtime.radix<binary>} : (!runtime.status) -> i1
  return %ok : i1
}
