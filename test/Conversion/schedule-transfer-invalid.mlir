// RUN: obelisk-opt %s -split-input-file -verify-diagnostics

!ref = !simulation.ref<!simulation.logic<1>>
module {
  // expected-error @+1 {{requires distinct aligned storage capture slots}}
  schedule.transfer.kernel {sym_name = "overlap", signature = (!simulation.context, !ref, !ref) -> (), source = 1 : i32, destination = 2 : i32, capture_offsets = array<i64: -1, 0, 0>} : () -> ()
}

// -----

!ref = !simulation.ref<!simulation.logic<1>>
module {
  // expected-error @+1 {{requires matching source and destination captures}}
  schedule.transfer.kernel {sym_name = "bad_capture", signature = (!simulation.context, !ref, !ref) -> (), source = 3 : i32, destination = 2 : i32, capture_offsets = array<i64: -1, 0, 8>} : () -> ()
}

// -----

!ref = !simulation.ref<!simulation.logic<1>>
module {
  schedule.transfer.kernel {sym_name = "forward", signature = (!simulation.context, !ref, !ref) -> (), source = 1 : i32, destination = 2 : i32, capture_offsets = array<i64: -1, 0, 8>} : () -> ()
  // expected-error @+1 {{requires a transfer kernel with the same capture ABI}}
  schedule.transfer.activation {actor = @actor, signature = (!simulation.context, !ref, !ref) -> (), source = 2 : i32, destination = 1 : i32, capture_offsets = array<i64: -1, 0, 8>, kernel = @forward} : () -> ()
}
