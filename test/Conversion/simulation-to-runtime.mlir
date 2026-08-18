// RUN: obelisk-opt --convert-obelisk-sim-to-runtime %s | FileCheck %s

module {
  func.func @io(%ctx: !obelisk_sim.context, %fd_bits: i32,
                %value: !obelisk_sim.logic<13>, %byte: i32,
                %offset: i64, %origin: i32) -> (i13, i32, i13, i32)
      attributes {obelisk_sim.hierarchical_name = "top.io"} {
    %text = obelisk_sim.bytes.constant "%m %l value=%0h"
    %path = obelisk_sim.bytes.constant "input.bin"
    %mode = obelisk_sim.bytes.constant "rb"
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
// CHECK: %[[TEXT:.*]] = obelisk_rt.bytes.constant "%m %l value=%0h"
// CHECK: %[[PATH:.*]] = obelisk_rt.bytes.constant "input.bin"
// CHECK: %[[MODE:.*]] = obelisk_rt.bytes.constant "rb"
// CHECK: obelisk_sim.context.runtime
// CHECK: obelisk_rt.file_descriptor.from_bits
// CHECK: obelisk_rt.argument.bytes %[[TEXT]] {is_format_string = true}
// CHECK: obelisk_rt.argument.empty
// CHECK: obelisk_rt.argument.packed %{{.*}}, %{{.*}} {is_signed = true}
// CHECK: obelisk_rt.argument.array
// CHECK: obelisk_rt.format.environment {library_cell = "work.io", scope = "top.io.named", time_multiplier = 1000 : i64}
// CHECK: obelisk_rt.display
// CHECK: obelisk_sim.status.check
// CHECK: obelisk_rt.argument.bytes %[[TEXT]] {is_format_string = true}
// CHECK: obelisk_rt.argument.packed %{{.*}}, %{{.*}} {is_signed = true}
// CHECK: obelisk_rt.format.environment {library_cell = "work.io", scope = "top.io.named", time_multiplier = 1000 : i64}
// CHECK: %[[FORMAT_STATUS:.*]], %[[FORMATTED:.*]] = obelisk_rt.string_output_format
// CHECK-NEXT: obelisk_sim.status.check %[[FORMAT_STATUS]]
// CHECK: obelisk_rt.argument.bytes %[[TEXT]] {{.*}}designated_format = true
// CHECK: %[[DESIGNATED_STATUS:.*]], %[[DESIGNATED:.*]] = obelisk_rt.string_output_format
// CHECK-NEXT: obelisk_sim.status.check %[[DESIGNATED_STATUS]]
// CHECK: %[[MCD_STATUS:.*]], %[[MCD_FD:.*]] = obelisk_rt.file.open_mcd
// CHECK: %[[MCD_BITS:.*]] = obelisk_rt.file_descriptor.to_bits %[[MCD_FD]]
// CHECK: %[[OPEN_ZERO:.*]] = arith.constant 0 : i32
// CHECK: %[[MCD_OK:.*]] = obelisk_rt.status.is %[[MCD_STATUS]], 0
// CHECK: arith.select %[[MCD_OK]], %[[MCD_BITS]], %[[OPEN_ZERO]] : i32
// CHECK: %[[OPEN_STATUS:.*]], %[[OPEN_FD:.*]] = obelisk_rt.file.open {{.*}}, %[[PATH]], %[[MODE]]
// CHECK: %[[OPEN_BITS:.*]] = obelisk_rt.file_descriptor.to_bits %[[OPEN_FD]]
// CHECK: %[[OPEN_FAILURE:.*]] = arith.constant 0 : i32
// CHECK: %[[OPEN_OK:.*]] = obelisk_rt.status.is %[[OPEN_STATUS]], 0
// CHECK: arith.select %[[OPEN_OK]], %[[OPEN_BITS]], %[[OPEN_FAILURE]] : i32
// CHECK: %[[CLOSE_STATUS:.*]] = obelisk_rt.file.close
// CHECK: %[[FLUSH_STATUS:.*]] = obelisk_rt.file.flush
// CHECK: %[[GETC_STATUS:.*]], %[[BYTE:.*]] = obelisk_rt.file.getc
// CHECK: %[[BYTE_I32:.*]] = arith.extui %[[BYTE]] : i8 to i32
// CHECK: %[[GETC_FAILURE:.*]] = arith.constant -1 : i32
// CHECK: %[[GETC_OK:.*]] = obelisk_rt.status.is %[[GETC_STATUS]], 0
// CHECK: arith.select %[[GETC_OK]], %[[BYTE_I32]], %[[GETC_FAILURE]] : i32
// CHECK: %[[UNGETC_STATUS:.*]] = obelisk_rt.file.ungetc
// CHECK: %[[UNGETC_SUCCESS:.*]] = arith.constant 0 : i32
// CHECK: %[[UNGETC_FAILURE:.*]] = arith.constant -1 : i32
// CHECK: %[[UNGETC_OK:.*]] = obelisk_rt.status.is %[[UNGETC_STATUS]], 0
// CHECK: arith.select %[[UNGETC_OK]], %[[UNGETC_SUCCESS]], %[[UNGETC_FAILURE]] : i32
// CHECK: %[[LINE_LIMIT:.*]] = arith.constant 1 : i64
// CHECK: %[[LINE_STATUS:.*]], %[[LINE:.*]] = obelisk_rt.file.getline {{.*}}, %[[LINE_LIMIT]]
// CHECK: %[[LINE_SIZE:.*]] = obelisk_rt.bytes.size %[[LINE]]
// CHECK: obelisk_rt.bytes.to_packed {{.*}} {high_alignment = false}
// CHECK: obelisk_rt.buffer.release %[[LINE]]
// CHECK: %[[LINE_COUNT:.*]] = arith.trunci %[[LINE_SIZE]] : i64 to i32
// CHECK: %[[LINE_FAILURE:.*]] = arith.constant 0 : i32
// CHECK: %[[LINE_OK:.*]] = obelisk_rt.status.is %[[LINE_STATUS]], 0
// CHECK: arith.select %[[LINE_OK]], %[[LINE_COUNT]], %[[LINE_FAILURE]] : i32
// CHECK: obelisk_rt.bytes.scratch 2
// CHECK: %[[READ_STATUS:.*]], %[[READ_COUNT:.*]] = obelisk_rt.file.read
// CHECK: obelisk_rt.bytes.to_packed {{.*}} {high_alignment = true}
// CHECK: %[[READ_COUNT_I32:.*]] = arith.trunci %[[READ_COUNT]] : i64 to i32
// CHECK: %[[READ_FAILURE:.*]] = arith.constant 0 : i32
// CHECK: %[[READ_OK:.*]] = obelisk_rt.status.is %[[READ_STATUS]], 0
// CHECK: arith.select %[[READ_OK]], %[[READ_COUNT_I32]], %[[READ_FAILURE]] : i32
// CHECK: %[[TOKEN_VALUE_SCRATCH:.*]] = obelisk_rt.bytes.scratch 2
// CHECK: %[[TOKEN_UNKNOWN_SCRATCH:.*]] = obelisk_rt.bytes.scratch 2
// CHECK: %[[TOKEN_STATUS:.*]], %[[TOKEN_KIND:.*]], %[[TOKEN_ADDRESS:.*]] = obelisk_rt.file.readmem_token
// CHECK-NEXT: obelisk_sim.status.check %[[TOKEN_STATUS]]
// CHECK: obelisk_rt.bytes.to_packed %[[TOKEN_VALUE_SCRATCH]]
// CHECK: obelisk_rt.bytes.to_packed %[[TOKEN_UNKNOWN_SCRATCH]]
// A descriptor that is not open can never deliver a byte, so IEEE 1800-2017
// 21.3.6's "non-zero when EOF has been detected" is the honest answer for one:
// $feof reports end of file rather than the zero that means more is coming.
// CHECK: %[[EOF_STATUS:.*]], %[[EOF_VALUE:.*]] = obelisk_rt.file.eof
// CHECK: %[[EOF_FAILURE:.*]] = arith.constant 1 : i32
// CHECK: %[[EOF_OK:.*]] = obelisk_rt.status.is %[[EOF_STATUS]], 0
// CHECK: arith.select %[[EOF_OK]], %[[EOF_VALUE]], %[[EOF_FAILURE]] : i32
// CHECK: %[[SEEK_STATUS:.*]] = obelisk_rt.file.seek
// CHECK: %[[SEEK_SUCCESS:.*]] = arith.constant 0 : i32
// CHECK: %[[SEEK_FAILURE:.*]] = arith.constant -1 : i32
// CHECK: %[[SEEK_OK:.*]] = obelisk_rt.status.is %[[SEEK_STATUS]], 0
// CHECK: arith.select %[[SEEK_OK]], %[[SEEK_SUCCESS]], %[[SEEK_FAILURE]] : i32
// CHECK: %[[TELL_STATUS:.*]], %[[OFFSET:.*]] = obelisk_rt.file.tell
// CHECK: %[[TELL_FAILURE:.*]] = arith.constant -1 : i64
// CHECK: %[[TELL_OK:.*]] = obelisk_rt.status.is %[[TELL_STATUS]], 0
// CHECK: arith.select %[[TELL_OK]], %[[OFFSET]], %[[TELL_FAILURE]] : i64
// CHECK: %[[REWIND_STATUS:.*]] = obelisk_rt.file.rewind
// CHECK: %[[REWIND_SUCCESS:.*]] = arith.constant 0 : i32
// CHECK: %[[REWIND_FAILURE:.*]] = arith.constant -1 : i32
// CHECK: %[[REWIND_OK:.*]] = obelisk_rt.status.is %[[REWIND_STATUS]], 0
// CHECK: arith.select %[[REWIND_OK]], %[[REWIND_SUCCESS]], %[[REWIND_FAILURE]] : i32
// CHECK: %[[FINISH_STATUS:.*]] = obelisk_rt.finish
// CHECK-NEXT: obelisk_sim.status.check %[[FINISH_STATUS]]
// CHECK: %[[STOP_STATUS:.*]] = obelisk_rt.stop
// CHECK-NEXT: obelisk_sim.status.check %[[STOP_STATUS]]
// CHECK: %[[FATAL_STATUS:.*]] = obelisk_rt.fatal
// CHECK-NEXT: obelisk_sim.status.check %[[FATAL_STATUS]]
// CHECK: %[[ERROR_STATUS:.*]] = obelisk_rt.error
// CHECK-NEXT: obelisk_sim.status.check %[[ERROR_STATUS]]
// CHECK: %[[TERMINATION_REQUESTED:.*]] = obelisk_rt.termination.requested

// CHECK-LABEL: func.func @aggregate_io(
// CHECK-SAME: %{{.*}}: i80, %{{.*}}: i80) -> (i80, i80)
// CHECK: obelisk_rt.argument.packed {{.*}}, {{.*}} {is_signed = false}
// CHECK: obelisk_rt.display
// CHECK: obelisk_rt.file.read
// CHECK-NOT: obelisk_sim.packed.

// CHECK-LABEL: func.func @virtual_interface_io(
// CHECK: %[[VIF_ID:.*]] = obelisk_sim.virtual_interface.scope %{{.*}}
// CHECK: obelisk_rt.argument.virtual_interface %[[VIF_ID]]
// CHECK: obelisk_rt.display
