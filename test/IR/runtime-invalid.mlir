// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

func.func @leak(%ctx: !obelisk_rt.context) {
  // expected-error @+1 {{owned buffer must have exactly one consuming use}}
  %status, %message = obelisk_rt.last_error %ctx :
      (!obelisk_rt.context) -> (!obelisk_rt.status, !obelisk_rt.buffer)
  return
}

// -----

func.func @double_release(%ctx: !obelisk_rt.context) {
  // expected-error @+1 {{owned buffer must have exactly one consuming use}}
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
  // expected-error @+1 {{owned buffer must be consumed by obelisk_rt.buffer.release}}
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
  // expected-error @+1 {{owned buffer must be consumed by obelisk_rt.buffer.release}}
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
