// RUN: talos-opt %s --split-input-file --verify-diagnostics
module {
  func.func @bad() {
    // expected-error @+1 {{requires a lane count dividing the maximum word width}}
    schedule.word.region 64 {
      schedule.word.yield
    } {lane_count = 3 : i32}
    return
  }
}
// -----
module {
  func.func @bad(%ref: !simulation.ref<i7>, %value: i7) {
    // expected-error @+1 {{requires native-width packed stores}}
    schedule.word.region 64 {
      simulation.ref.store %value to %ref : i7, !simulation.ref<i7>
      schedule.word.yield
    } {lane_count = 8 : i32}
    return
  }
}
// -----
module {
  func.func @bad(%arg: i64) {
    // expected-error @+1 {{requires a lane width dividing the native word width}}
    %v = schedule.word.reverse %arg {lane_width = 7 : i32} : (i64) -> i64
    return
  }
}
// -----
module {
  func.func @bad(%arg: !simulation.ref<!simulation.packed_array<7 : 0 x i1>>, %low: i64) {
    // expected-error @+1 {{requires an unpacked array of packed elements and a packed word result}}
    %v = schedule.word.view %arg, %low : (!simulation.ref<!simulation.packed_array<7 : 0 x i1>>, i64) -> !simulation.ref<i8>
    return
  }
}
// -----
module {
  func.func @bad(%arg: !simulation.ref<!simulation.unpacked_array<0 : 7 x !simulation.logic<1>>>, %low: i64) {
    // expected-error @+1 {{requires a contained native word with the same state domain}}
    %v = schedule.word.view %arg, %low : (!simulation.ref<!simulation.unpacked_array<0 : 7 x !simulation.logic<1>>>, i64) -> !simulation.ref<i8>
    return
  }
}
// -----
module {
  func.func @bad(%arg: !simulation.ref<!simulation.unpacked_array<0 : 7 x i1>>, %low: i64) {
    // expected-error @+1 {{requires a contained native word with the same state domain}}
    %v = schedule.word.view %arg, %low : (!simulation.ref<!simulation.unpacked_array<0 : 7 x i1>>, i64) -> !simulation.ref<i16>
    return
  }
}
// -----
module {
  func.func @bad() {
    // expected-error @+1 {{requires a native word width between 2 and 64}}
    schedule.word.region 65 {
      schedule.word.yield
    }
    return
  }
}
// -----
module {
  func.func @bad() {
    // expected-error @+1 {{requires a nonempty CFG without entry arguments}}
    schedule.word.region 8 {
    ^entry(%arg: i64):
      schedule.word.yield
    }
    return
  }
}
// -----
module {
  func.func @bad() {
    // expected-error @+1 {{does not permit nested regions}}
    schedule.word.region 8 {
      schedule.word.region 8 {
        schedule.word.yield
      }
      schedule.word.yield
    }
    return
  }
}
// -----
module {
  func.func private @callee()
  func.func @bad() {
    // expected-error @+1 {{does not permit calls}}
    schedule.word.region 8 {
      func.call @callee() : () -> ()
      schedule.word.yield
    }
    return
  }
}
// -----
module {
  func.func @bad(%ref: !simulation.ref<!simulation.logic<1>>) {
    // expected-error @+1 {{requires native-width packed stores}}
    schedule.word.region 8 {
      %value = simulation.logic.constant 0 : i1, 0 : i1 : !simulation.logic<1>
      simulation.ref.store %value to %ref : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      schedule.word.yield
    }
    return
  }
}

// -----
module {
  func.func @bad(%arg: !simulation.logic<8>) {
    // expected-error @+1 {{requires complete truth tables for at most six input planes}}
    %v = schedule.word.table %arg {value_table = 2 : i2, unknown_table = 0 : i2} : (!simulation.logic<8>) -> !simulation.logic<8>
    return
  }
}
// -----
module {
  func.func @bad(%arg: i8) {
    // expected-error @+1 {{integer results cannot have unknown bits}}
    %v = schedule.word.table %arg {value_table = 2 : i2, unknown_table = 1 : i2} : (i8) -> i8
    return
  }
}
// -----
module {
  func.func @bad(%arg: i8) {
    // expected-error @+1 {{requires matching native word operand widths}}
    %v = schedule.word.table %arg {value_table = 2 : i2, unknown_table = 0 : i2} : (i8) -> i16
    return
  }
}
// -----
module {
  func.func @bad(%arg: i8) {
    // expected-error @+1 {{requires matching operand and result types}}
    %v = schedule.word.reverse %arg : (i8) -> i16
    return
  }
}
// -----
module {
  func.func @bad(%arg: i65) {
    // expected-error @+1 {{requires a native integer or logic word}}
    %v = schedule.word.reverse %arg : (i65) -> i65
    return
  }
}

// -----
module {
  func.func @bad(%arg: !simulation.unpacked_array<0 : 7 x !simulation.logic<1>>, %low: i65) {
    // expected-error @+1 {{requires a contained native word with the same state domain}}
    %v = schedule.word.extract %arg from %low : (!simulation.unpacked_array<0 : 7 x !simulation.logic<1>>, i65) -> i8
    return
  }
}
// -----
module {
  func.func @bad(%arg: !simulation.unpacked_array<0 : 7 x i1>, %low: i65) {
    // expected-error @+1 {{requires a contained native word with the same state domain}}
    %v = schedule.word.extract %arg from %low : (!simulation.unpacked_array<0 : 7 x i1>, i65) -> i16
    return
  }
}
// -----
module {
  func.func @bad(%arg: i8) {
    // expected-error @+1 {{requires matching counts of complete lanes}}
    %v = schedule.word.resize_lanes %arg {input_lane_width = 1 : i32, result_lane_width = 7 : i32} : (i8) -> i64
    return
  }
}
// -----
module {
  func.func @bad(%arg: i8) {
    // expected-error @+1 {{requires native words with the same state domain}}
    %v = schedule.word.resize_lanes %arg {input_lane_width = 1 : i32, result_lane_width = 8 : i32} : (i8) -> !simulation.logic<64>
    return
  }
}
