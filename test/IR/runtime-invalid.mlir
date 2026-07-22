// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

func.func @leak(%ctx: !obelisk_rt.context) {
  // expected-error @+1 {{owned buffer requires one release and at most one size and one packed read}}
  %status, %message = obelisk_rt.last_error %ctx :
      (!obelisk_rt.context) -> (!obelisk_rt.status, !obelisk_rt.buffer)
  return
}

// -----

func.func @double_release(%ctx: !obelisk_rt.context) {
  // expected-error @+1 {{owned buffer requires one release and at most one size and one packed read}}
  %status, %message = obelisk_rt.last_error %ctx :
      (!obelisk_rt.context) -> (!obelisk_rt.status, !obelisk_rt.buffer)
  obelisk_rt.buffer.release %message : (!obelisk_rt.buffer) -> ()
  obelisk_rt.buffer.release %message : (!obelisk_rt.buffer) -> ()
  return
}

// -----

func.func @release_borrowed(%message: !obelisk_rt.buffer) {
  // expected-error @+1 {{requires a buffer produced directly by an owned-buffer runtime operation}}
  obelisk_rt.buffer.release %message : (!obelisk_rt.buffer) -> ()
  return
}

// -----

func.func @release_in_successor(%ctx: !obelisk_rt.context) {
  // expected-error @+1 {{owned buffer has an unsupported consumer cf.br}}
  %status, %message = obelisk_rt.last_error %ctx :
      (!obelisk_rt.context) -> (!obelisk_rt.status, !obelisk_rt.buffer)
  cf.br ^release(%message : !obelisk_rt.buffer)
^release(%forwarded: !obelisk_rt.buffer):
  obelisk_rt.buffer.release %forwarded : (!obelisk_rt.buffer) -> ()
  return
}

// -----

func.func private @steal(!obelisk_rt.buffer)
func.func @illegal_transfer(%ctx: !obelisk_rt.context) {
  // expected-error @+1 {{owned buffer has an unsupported consumer func.call}}
  %status, %message = obelisk_rt.last_error %ctx :
      (!obelisk_rt.context) -> (!obelisk_rt.status, !obelisk_rt.buffer)
  func.call @steal(%message) : (!obelisk_rt.buffer) -> ()
  return
}

// -----

func.func @bad_radix(%ctx: !obelisk_rt.context, %fd: !obelisk_rt.fd,
    %args: !obelisk_rt.args, %env: !obelisk_rt.format_env, %newline: i1) {
  // expected-error @+1 {{attribute 'default_radix' failed to satisfy constraint}}
  %status = obelisk_rt.display %ctx, %fd, %newline, %args, %env
      {default_radix = 3 : i32} :
      (!obelisk_rt.context, !obelisk_rt.fd, i1, !obelisk_rt.args,
       !obelisk_rt.format_env) -> !obelisk_rt.status
  return
}

// -----

func.func @bad_scratch() {
  // expected-error @+1 {{scratch byte count must be nonnegative}}
  %bytes = obelisk_rt.bytes.scratch -1
  return
}

// -----

func.func @bad_byte_container(%value: i32) {
  // expected-error @+1 {{requires a byte span, mutable byte span, or buffer}}
  %size = obelisk_rt.bytes.size %value : (i32) -> i64
  return
}

// -----

func.func @bad_unknown_plane(%value: i8, %unknown: i16) {
  // expected-error @+1 {{unknown plane must match the value plane type}}
  %argument = obelisk_rt.argument.packed %value, %unknown
      {is_signed = false} : (i8, i16) -> !obelisk_rt.arg
  return
}

// -----

func.func @double_size(%ctx: !obelisk_rt.context, %fd: !obelisk_rt.fd,
    %limit: i64) {
  // expected-error @+1 {{owned buffer requires one release and at most one size and one packed read}}
  %status, %line = obelisk_rt.file.getline %ctx, %fd, %limit :
      (!obelisk_rt.context, !obelisk_rt.fd, i64) ->
      (!obelisk_rt.status, !obelisk_rt.buffer)
  %first = obelisk_rt.bytes.size %line : (!obelisk_rt.buffer) -> i64
  %second = obelisk_rt.bytes.size %line : (!obelisk_rt.buffer) -> i64
  obelisk_rt.buffer.release %line : (!obelisk_rt.buffer) -> ()
  return
}

// -----

func.func @read_after_release(%ctx: !obelisk_rt.context, %fd: !obelisk_rt.fd,
    %limit: i64) {
  // expected-error @+1 {{owned buffer size and packed reads must precede its release}}
  %status, %line = obelisk_rt.file.getline %ctx, %fd, %limit :
      (!obelisk_rt.context, !obelisk_rt.fd, i64) ->
      (!obelisk_rt.status, !obelisk_rt.buffer)
  obelisk_rt.buffer.release %line : (!obelisk_rt.buffer) -> ()
  %size = obelisk_rt.bytes.size %line : (!obelisk_rt.buffer) -> i64
  return
}

// -----

func.func @scratch_escape(%count: i64) -> !obelisk_rt.mut_bytes {
  // expected-error @+1 {{stack-backed scratch span has an unsupported consumer func.return}}
  %scratch = obelisk_rt.bytes.scratch 4
  return %scratch : !obelisk_rt.mut_bytes
}

// -----

func.func @bad_time_multiplier() {
  // expected-error @+1 {{time multiplier must be positive}}
  %env = obelisk_rt.format.environment {time_multiplier = 0 : i64}
  return
}
