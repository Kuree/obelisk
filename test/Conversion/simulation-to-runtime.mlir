// RUN: obelisk-opt --convert-obelisk-sim-to-runtime %s | FileCheck %s

module {
  func.func @io(%ctx: !obelisk_sim.context, %fd_bits: i32,
                %value: !obelisk_sim.logic<13>, %byte: i32,
                %offset: i64, %origin: i32) -> (i13, i32, i13, i32)
      attributes {obelisk_sim.hierarchical_name = "top.io"} {
    %text = obelisk_sim.bytes.constant "%m %l value=%0h"
    %path = obelisk_sim.bytes.constant "input.bin"
    %mode = obelisk_sim.bytes.constant "rb"
    %time_input = arith.constant 1.25 : f64
    %scaled_time = obelisk_sim.time.scan_scale %ctx, %time_input
        time_multiplier = 1000 time_precision = -12
    obelisk_sim.display %ctx to %fd_bits(%text, %value) newline = true
        radix = 16 flags = [0, 2, 1]
        {library_cell = "work.io", scope = "top.io.named",
         time_multiplier = 1000 : i64}
        : !obelisk_sim.bytes, !obelisk_sim.logic<13>
        loc("source.sv":12:7)
    %formatted = obelisk_sim.string.output_format %ctx(%text, %value)
        radix = 8 flags = [0, 1]
        {library_cell = "work.io", scope = "top.io.named",
         time_multiplier = 1000 : i64} : !obelisk_sim.bytes,
        !obelisk_sim.logic<13>
    %designated = obelisk_sim.string.output_format %ctx(%text, %value)
        radix = 8 flags = [32, 1]
        {library_cell = "work.io", scope = "top.io.named",
         time_multiplier = 1000 : i64} : !obelisk_sim.bytes,
        !obelisk_sim.logic<13>
    %mcd = obelisk_sim.file.open_mcd %ctx, %text :
        (!obelisk_sim.context, !obelisk_sim.bytes) -> i32
    %file = obelisk_sim.file.open %ctx, %path, %mode :
        (!obelisk_sim.context, !obelisk_sim.bytes, !obelisk_sim.bytes) -> i32
    obelisk_sim.file.close %ctx, %fd_bits :
        (!obelisk_sim.context, i32) -> ()
    obelisk_sim.file.flush %ctx, %fd_bits :
        (!obelisk_sim.context, i32) -> ()
    %getc = obelisk_sim.file.getc %ctx, %fd_bits :
        (!obelisk_sim.context, i32) -> i32
    %ungetc = obelisk_sim.file.ungetc %ctx, %byte, %fd_bits :
        (!obelisk_sim.context, i32, i32) -> i32
    %line, %line_count = obelisk_sim.file.getline %ctx, %fd_bits :
        (!obelisk_sim.context, i32) -> (i13, i32)
    %data, %read_count = obelisk_sim.file.read_packed %ctx, %fd_bits :
        (!obelisk_sim.context, i32) -> (i13, i32)
    %token_data, %token_kind, %token_address =
        obelisk_sim.file.readmem_token %ctx, %fd_bits {radix = 16 : i32} :
        (!obelisk_sim.context, i32) -> (!obelisk_sim.logic<13>, i32, i64)
    %eof = obelisk_sim.file.eof %ctx, %fd_bits :
        (!obelisk_sim.context, i32) -> i32
    %seek = obelisk_sim.file.seek %ctx, %fd_bits, %offset, %origin :
        (!obelisk_sim.context, i32, i64, i32) -> i32
    %tell = obelisk_sim.file.tell %ctx, %fd_bits :
        (!obelisk_sim.context, i32) -> i64
    %rewind = obelisk_sim.file.rewind %ctx, %fd_bits :
        (!obelisk_sim.context, i32) -> i32
    %verbosity = arith.constant 1 : i32
    obelisk_sim.finish %ctx, %verbosity
    obelisk_sim.stop %ctx, %verbosity
    obelisk_sim.fatal %ctx, %verbosity
    obelisk_sim.error %ctx
    %termination_requested = obelisk_sim.termination.requested %ctx
    return %line, %line_count, %data, %read_count : i13, i32, i13, i32
  }

  func.func @aggregate_io(
      %ctx: !obelisk_sim.context, %fd_bits: i32,
      %value: !obelisk_sim.packed_array<79 : 0 x !obelisk_sim.logic<1>>)
      -> !obelisk_sim.packed_array<79 : 0 x !obelisk_sim.logic<1>> {
    %format = obelisk_sim.bytes.constant "%0h"
    %flat = obelisk_sim.packed.flatten %value :
        (!obelisk_sim.packed_array<79 : 0 x !obelisk_sim.logic<1>>) ->
        !obelisk_sim.logic<80>
    obelisk_sim.display %ctx to %fd_bits(%format, %flat) newline = true
        radix = 16 flags = [0, 0] : !obelisk_sim.bytes,
        !obelisk_sim.logic<80>
    %data, %count = obelisk_sim.file.read_packed %ctx, %fd_bits :
        (!obelisk_sim.context, i32) -> (i80, i32)
    %logic = obelisk_sim.logic.from_bits %data :
        i80 -> !obelisk_sim.logic<80>
    %result = obelisk_sim.packed.unflatten %logic :
        (!obelisk_sim.logic<80>) ->
        !obelisk_sim.packed_array<79 : 0 x !obelisk_sim.logic<1>>
    return %result :
        !obelisk_sim.packed_array<79 : 0 x !obelisk_sim.logic<1>>
  }

  func.func @virtual_interface_io(
      %ctx: !obelisk_sim.context,
      %vif: !obelisk_sim.virtual_interface<"@bus", "">) {
    %fd = arith.constant 1 : i32
    obelisk_sim.display %ctx to %fd(%vif) newline = false radix = 10
        flags = [256] : !obelisk_sim.virtual_interface<"@bus", "">
    return
  }
}

// CHECK-LABEL: func.func @io(
// CHECK: %[[TEXT:.*]] = runtime.bytes.constant "%m %l value=%0h"
// CHECK: %[[PATH:.*]] = runtime.bytes.constant "input.bin"
// CHECK: %[[MODE:.*]] = runtime.bytes.constant "rb"
// CHECK: %[[TIME_INPUT:.*]] = arith.constant 1.250000e+00 : f64
// CHECK: %[[TIME_MULTIPLIER:.*]] = arith.constant 1000 : i64
// CHECK: %[[TIME_PRECISION:.*]] = arith.constant -12 : i32
// CHECK: runtime.time.scan_scale {{.*}}, %[[TIME_INPUT]], %[[TIME_MULTIPLIER]], %[[TIME_PRECISION]]
// CHECK: obelisk_sim.context.runtime
// CHECK: runtime.file_descriptor.from_bits
// CHECK: runtime.argument.bytes %[[TEXT]] {is_format_string = true}
// CHECK: runtime.argument.empty
// CHECK: runtime.argument.packed %{{.*}}, %{{.*}} {is_signed = true}
// CHECK: runtime.argument.array
// CHECK: runtime.format.environment {library_cell = "work.io", scope = "top.io.named", time_multiplier = 1000 : i64}
// CHECK: runtime.display
// CHECK: obelisk_sim.status.check
// CHECK: runtime.argument.bytes %[[TEXT]] {is_format_string = true}
// CHECK: runtime.argument.packed %{{.*}}, %{{.*}} {is_signed = true}
// CHECK: runtime.format.environment {library_cell = "work.io", scope = "top.io.named", time_multiplier = 1000 : i64}
// CHECK: %[[FORMAT_STATUS:.*]], %[[FORMATTED:.*]] = runtime.string_output_format
// CHECK-NEXT: obelisk_sim.status.check %[[FORMAT_STATUS]]
// CHECK: runtime.argument.bytes %[[TEXT]] {{.*}}designated_format = true
// CHECK: %[[DESIGNATED_STATUS:.*]], %[[DESIGNATED:.*]] = runtime.string_output_format
// CHECK-NEXT: obelisk_sim.status.check %[[DESIGNATED_STATUS]]
// CHECK: %[[MCD_STATUS:.*]], %[[MCD_FD:.*]] = runtime.file.open_mcd
// CHECK: %[[MCD_BITS:.*]] = runtime.file_descriptor.to_bits %[[MCD_FD]]
// CHECK: %[[OPEN_ZERO:.*]] = arith.constant 0 : i32
// CHECK: %[[MCD_OK:.*]] = runtime.status.is %[[MCD_STATUS]], <ok>
// CHECK: arith.select %[[MCD_OK]], %[[MCD_BITS]], %[[OPEN_ZERO]] : i32
// CHECK: %[[OPEN_STATUS:.*]], %[[OPEN_FD:.*]] = runtime.file.open {{.*}}, %[[PATH]], %[[MODE]]
// CHECK: %[[OPEN_BITS:.*]] = runtime.file_descriptor.to_bits %[[OPEN_FD]]
// CHECK: %[[OPEN_FAILURE:.*]] = arith.constant 0 : i32
// CHECK: %[[OPEN_OK:.*]] = runtime.status.is %[[OPEN_STATUS]], <ok>
// CHECK: arith.select %[[OPEN_OK]], %[[OPEN_BITS]], %[[OPEN_FAILURE]] : i32
// CHECK: %[[CLOSE_STATUS:.*]] = runtime.file.close
// CHECK: %[[FLUSH_STATUS:.*]] = runtime.file.flush
// CHECK: %[[GETC_STATUS:.*]], %[[BYTE:.*]] = runtime.file.getc
// CHECK: %[[BYTE_I32:.*]] = arith.extui %[[BYTE]] : i8 to i32
// CHECK: %[[GETC_FAILURE:.*]] = arith.constant -1 : i32
// CHECK: %[[GETC_OK:.*]] = runtime.status.is %[[GETC_STATUS]], <ok>
// CHECK: arith.select %[[GETC_OK]], %[[BYTE_I32]], %[[GETC_FAILURE]] : i32
// CHECK: %[[UNGETC_STATUS:.*]] = runtime.file.ungetc
// CHECK: %[[UNGETC_SUCCESS:.*]] = arith.constant 0 : i32
// CHECK: %[[UNGETC_FAILURE:.*]] = arith.constant -1 : i32
// CHECK: %[[UNGETC_OK:.*]] = runtime.status.is %[[UNGETC_STATUS]], <ok>
// CHECK: arith.select %[[UNGETC_OK]], %[[UNGETC_SUCCESS]], %[[UNGETC_FAILURE]] : i32
// CHECK: %[[LINE_LIMIT:.*]] = arith.constant 1 : i64
// CHECK: %[[LINE_STATUS:.*]], %[[LINE:.*]] = runtime.file.getline {{.*}}, %[[LINE_LIMIT]]
// CHECK: %[[LINE_SIZE:.*]] = runtime.bytes.size %[[LINE]]
// CHECK: runtime.bytes.to_packed {{.*}} {high_alignment = false}
// CHECK: runtime.buffer.release %[[LINE]]
// CHECK: %[[LINE_COUNT:.*]] = arith.trunci %[[LINE_SIZE]] : i64 to i32
// CHECK: %[[LINE_FAILURE:.*]] = arith.constant 0 : i32
// CHECK: %[[LINE_OK:.*]] = runtime.status.is %[[LINE_STATUS]], <ok>
// CHECK: arith.select %[[LINE_OK]], %[[LINE_COUNT]], %[[LINE_FAILURE]] : i32
// CHECK: runtime.bytes.scratch 2
// CHECK: %[[READ_STATUS:.*]], %[[READ_COUNT:.*]] = runtime.file.read
// CHECK: runtime.bytes.to_packed {{.*}} {high_alignment = true}
// CHECK: %[[READ_COUNT_I32:.*]] = arith.trunci %[[READ_COUNT]] : i64 to i32
// CHECK: %[[READ_FAILURE:.*]] = arith.constant 0 : i32
// CHECK: %[[READ_OK:.*]] = runtime.status.is %[[READ_STATUS]], <ok>
// CHECK: arith.select %[[READ_OK]], %[[READ_COUNT_I32]], %[[READ_FAILURE]] : i32
// CHECK: %[[TOKEN_VALUE_SCRATCH:.*]] = runtime.bytes.scratch 2
// CHECK: %[[TOKEN_UNKNOWN_SCRATCH:.*]] = runtime.bytes.scratch 2
// CHECK: %[[TOKEN_STATUS:.*]], %[[TOKEN_KIND:.*]], %[[TOKEN_ADDRESS:.*]] = runtime.file.readmem_token
// CHECK-NEXT: obelisk_sim.status.check %[[TOKEN_STATUS]]
// CHECK: runtime.bytes.to_packed %[[TOKEN_VALUE_SCRATCH]]
// CHECK: runtime.bytes.to_packed %[[TOKEN_UNKNOWN_SCRATCH]]
// A descriptor that is not open can never deliver a byte, so IEEE 1800-2017
// 21.3.6's "non-zero when EOF has been detected" is the honest answer for one:
// $feof reports end of file rather than the zero that means more is coming.
// CHECK: %[[EOF_STATUS:.*]], %[[EOF_VALUE:.*]] = runtime.file.eof
// CHECK: %[[EOF_FAILURE:.*]] = arith.constant 1 : i32
// CHECK: %[[EOF_OK:.*]] = runtime.status.is %[[EOF_STATUS]], <ok>
// CHECK: arith.select %[[EOF_OK]], %[[EOF_VALUE]], %[[EOF_FAILURE]] : i32
// CHECK: %[[SEEK_STATUS:.*]] = runtime.file.seek
// CHECK: %[[SEEK_SUCCESS:.*]] = arith.constant 0 : i32
// CHECK: %[[SEEK_FAILURE:.*]] = arith.constant -1 : i32
// CHECK: %[[SEEK_OK:.*]] = runtime.status.is %[[SEEK_STATUS]], <ok>
// CHECK: arith.select %[[SEEK_OK]], %[[SEEK_SUCCESS]], %[[SEEK_FAILURE]] : i32
// CHECK: %[[TELL_STATUS:.*]], %[[OFFSET:.*]] = runtime.file.tell
// CHECK: %[[TELL_FAILURE:.*]] = arith.constant -1 : i64
// CHECK: %[[TELL_OK:.*]] = runtime.status.is %[[TELL_STATUS]], <ok>
// CHECK: arith.select %[[TELL_OK]], %[[OFFSET]], %[[TELL_FAILURE]] : i64
// CHECK: %[[REWIND_STATUS:.*]] = runtime.file.rewind
// CHECK: %[[REWIND_SUCCESS:.*]] = arith.constant 0 : i32
// CHECK: %[[REWIND_FAILURE:.*]] = arith.constant -1 : i32
// CHECK: %[[REWIND_OK:.*]] = runtime.status.is %[[REWIND_STATUS]], <ok>
// CHECK: arith.select %[[REWIND_OK]], %[[REWIND_SUCCESS]], %[[REWIND_FAILURE]] : i32
// CHECK: %[[FINISH_STATUS:.*]] = runtime.finish
// CHECK-NEXT: obelisk_sim.status.check %[[FINISH_STATUS]]
// CHECK: %[[STOP_STATUS:.*]] = runtime.stop
// CHECK-NEXT: obelisk_sim.status.check %[[STOP_STATUS]]
// CHECK: %[[FATAL_STATUS:.*]] = runtime.fatal
// CHECK-NEXT: obelisk_sim.status.check %[[FATAL_STATUS]]
// CHECK: %[[ERROR_STATUS:.*]] = runtime.error
// CHECK-NEXT: obelisk_sim.status.check %[[ERROR_STATUS]]
// CHECK: %[[TERMINATION_REQUESTED:.*]] = runtime.termination.requested

// CHECK-LABEL: func.func @aggregate_io(
// CHECK-SAME: %{{.*}}: i80, %{{.*}}: i80) -> (i80, i80)
// CHECK: runtime.argument.packed {{.*}}, {{.*}} {is_signed = false}
// CHECK: runtime.display
// CHECK: runtime.file.read
// CHECK-NOT: obelisk_sim.packed.

// CHECK-LABEL: func.func @virtual_interface_io(
// CHECK: %[[VIF_ID:.*]] = obelisk_sim.virtual_interface.scope %{{.*}}
// CHECK: runtime.argument.virtual_interface %[[VIF_ID]]
// CHECK: runtime.display
