// RUN: obelisk-opt %s --obelisk-materialize-native-eval-groups -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: FileCheck %s --check-prefix=LE < %t.mlir
// RUN: obelisk-opt %s '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"E-m:e-p:64:64-i64:64-n8:16:32:64-S128\"}"' --obelisk-materialize-native-eval-groups | FileCheck %s --check-prefix=BE
// RUN: obelisk-opt %s --mlir-disable-threading --obelisk-materialize-native-eval-groups | diff %t.mlir -
// RUN: obelisk-opt %s '--test-obelisk-set-module-attributes=attrs="{schedule.native.max_inline_ops = 24 : i64}"' --obelisk-materialize-native-eval-groups | FileCheck %s --check-prefix=BUDGET

// Three overlapping reads span 80 bytes. The group keeps the original CFG
// and exact memory operations, including before/after snapshots, inactive
// assignments and two stores crossing 32-byte boundaries. The second group
// covers X/Z storage, an entry argument and multiple return paths. The runtime
// companion compares all bytes, including untouched neighbors, at O0 and O3.
// IEEE 1800-2023 4.3, 4.6 and 6.3.1 require the same ordered four-state updates.
// CHECK-LABEL: llvm.func @wide()
// CHECK-SAME: schedule.eval.control_flow_dataflow
// CHECK-SAME: schedule.eval.dataflow_slots = 10 : i64
// CHECK-SAME: schedule.eval.refresh_group_proofs
// CHECK: llvm.cond_br
// CHECK: %[[CROSS1:.*]] = llvm.getelementptr {{.*}}[31]
// CHECK: %[[CROSS2:.*]] = llvm.getelementptr {{.*}}[63]
// CHECK: llvm.cond_br
// CHECK: llvm.load {{.*}} : !llvm.ptr -> i256
// CHECK: llvm.store {{.*}}, %[[CROSS1]] : i16, !llvm.ptr
// CHECK: llvm.load {{.*}} : !llvm.ptr -> i256
// CHECK: llvm.store {{.*}}, %[[CROSS2]] : i16, !llvm.ptr
// CHECK-LABEL: llvm.func @small(
// CHECK-SAME: schedule.eval.control_flow_dataflow
// CHECK-SAME: schedule.eval.dataflow_slots = 2 : i64
// CHECK-SAME: schedule.eval.refresh_group_proofs
// LE-LABEL: llvm.func @small(
// LE: %[[CROSS:.*]] = llvm.getelementptr {{.*}}[31]
// LE: %[[OLD:.*]] = llvm.load {{.*}} : !llvm.ptr -> i256
// LE: llvm.cond_br
// LE: %[[LOW:.*]] = llvm.trunc %[[OLD]] : i256 to i16
// LE-NEXT: llvm.store %[[LOW]], %[[CROSS]] : i16, !llvm.ptr
// LE: llvm.return
// LE: llvm.return
// BE-LABEL: llvm.func @small(
// BE-SAME: schedule.eval.control_flow_dataflow
// BE: %[[CROSS:.*]] = llvm.getelementptr {{.*}}[31]
// BE: %[[OLD:.*]] = llvm.load {{.*}} : !llvm.ptr -> i256
// BE: llvm.cond_br
// BE: %[[LOW:.*]] = llvm.trunc %[[OLD]] : i256 to i16
// BE-NEXT: llvm.store %[[LOW]], %[[CROSS]] : i16, !llvm.ptr
// The unchanged CFG must still fit the group operation budget.
// BUDGET-LABEL: llvm.func @wide()
// BUDGET-NOT: schedule.eval.control_flow_dataflow
// BUDGET: llvm.mlir.addressof @slow
// BUDGET-LABEL: llvm.func @small(
// BUDGET-SAME: schedule.eval.control_flow_dataflow
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  llvm.mlir.global internal @__obelisk_state_value(dense<[19, 92, 165, 238, 55, 128, 201, 18, 91, 164, 237, 54, 127, 200, 17, 90, 163, 236, 53, 126, 199, 16, 89, 162, 235, 52, 125, 198, 15, 88, 161, 234, 51, 124, 197, 14, 87, 160, 233, 50, 123, 196, 13, 86, 159, 232, 49, 122, 195, 12, 85, 158, 231, 48, 121, 194, 11, 84, 157, 230, 47, 120, 193, 10, 83, 156, 229, 46, 119, 192, 9, 82, 155, 228, 45, 118, 191, 8, 81, 154, 227, 44, 117, 190, 7, 80, 153, 226, 43, 116, 189, 6, 79, 152, 225, 42, 115, 188, 5, 78, 151, 224, 41, 114, 187, 4, 77, 150, 223, 40, 113, 186, 3, 76, 149, 222, 39, 112, 185, 2, 75, 148, 221, 38, 111, 184, 1, 74, 147, 220, 37, 110, 183, 0, 73, 146, 219, 36, 109, 182, 255, 72, 145, 218, 35, 108, 181, 254, 71, 144, 217, 34, 107, 180, 253, 70, 143, 216, 33, 106, 179, 252, 69, 142, 215, 32, 105, 178, 251, 68, 141, 214, 31, 104, 177, 250, 67, 140, 213, 30, 103, 176, 249, 66, 139, 212, 29, 102, 175, 248, 65, 138, 211, 28, 101, 174, 247, 64, 137, 210, 27, 100, 173, 246, 63, 136, 209, 26, 99, 172, 245, 62, 135, 208, 25, 98, 171, 244, 61, 134, 207, 24, 97, 170]> : tensor<224xi8>) : !llvm.array<224 x i8>
  llvm.mlir.global internal @__obelisk_state_unknown(dense<[17, 51, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165]> : tensor<64xi8>) : !llvm.array<64 x i8>
  llvm.mlir.global internal @ready(dense<0> : tensor<1xi64>) : !llvm.array<1 x i64>
  llvm.mlir.global internal @__obelisk_eval_promotion_pending_mask_v1(dense<0> : tensor<1xi64>) : !llvm.array<1 x i64>
  llvm.mlir.global internal @slow(0 : i32) : i32
  llvm.func @wide() attributes {schedule.eval.ranked_members = array<i32: 0, 1>, schedule.eval.group_ingress = @ready} {
    %p = llvm.mlir.addressof @slow : !llvm.ptr
    %one = llvm.mlir.constant(1 : i32) : i32
    llvm.store %one, %p : i32, !llvm.ptr
    llvm.return
  }
  llvm.func @wide_candidate() attributes {schedule.eval.ranked_members = array<i32: 0, 1>, schedule.eval.group_ingress = @ready, schedule.eval.dataflow_candidate = @wide} {
    %base = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %middle = llvm.getelementptr %base[24] : (!llvm.ptr) -> !llvm.ptr, i8
    %tail = llvm.getelementptr %base[48] : (!llvm.ptr) -> !llvm.ptr, i8
    %cross1 = llvm.getelementptr %base[31] : (!llvm.ptr) -> !llvm.ptr, i8
    %cross2 = llvm.getelementptr %base[63] : (!llvm.ptr) -> !llvm.ptr, i8
    %snapshot1 = llvm.getelementptr %base[96] : (!llvm.ptr) -> !llvm.ptr, i8
    %snapshot2 = llvm.getelementptr %base[128] : (!llvm.ptr) -> !llvm.ptr, i8
    %snapshot3 = llvm.getelementptr %base[160] : (!llvm.ptr) -> !llvm.ptr, i8
    %snapshot4 = llvm.getelementptr %base[192] : (!llvm.ptr) -> !llvm.ptr, i8
    %ready = llvm.mlir.addressof @ready : !llvm.ptr
    %pending = llvm.load %ready : !llvm.ptr -> i64
    %zero = llvm.mlir.constant(0 : i64) : i64
    %active = llvm.icmp "ne" %pending, %zero : i64
    llvm.cond_br %active, ^run, ^done
  ^run:
    %a = llvm.load %base : !llvm.ptr -> i256
    llvm.store %a, %snapshot1 : i256, !llvm.ptr
    %b = llvm.load %middle : !llvm.ptr -> i256
    llvm.store %b, %snapshot2 : i256, !llvm.ptr
    %first = llvm.mlir.constant(4660 : i16) : i16
    llvm.store %first, %cross1 : i16, !llvm.ptr
    %c = llvm.load %tail : !llvm.ptr -> i256
    llvm.store %c, %snapshot3 : i256, !llvm.ptr
    %d = llvm.load %middle : !llvm.ptr -> i256
    llvm.store %d, %snapshot4 : i256, !llvm.ptr
    %second = llvm.mlir.constant(22136 : i16) : i16
    llvm.store %second, %cross2 : i16, !llvm.ptr
    llvm.store %zero, %ready : i64, !llvm.ptr
    llvm.br ^done
  ^done:
    llvm.return
  }
  llvm.func @small(%enabled: i1) attributes {schedule.eval.ranked_members = array<i32: 2, 3>, schedule.eval.group_ingress = @ready} {
    %p = llvm.mlir.addressof @slow : !llvm.ptr
    %one = llvm.mlir.constant(1 : i32) : i32
    llvm.store %one, %p : i32, !llvm.ptr
    llvm.return
  }
  llvm.func @small_candidate(%enabled: i1) attributes {schedule.eval.ranked_members = array<i32: 2, 3>, schedule.eval.group_ingress = @ready, schedule.eval.dataflow_candidate = @small} {
    %base = llvm.mlir.addressof @__obelisk_state_unknown : !llvm.ptr
    %cross = llvm.getelementptr %base[31] : (!llvm.ptr) -> !llvm.ptr, i8
    %old = llvm.load %base : !llvm.ptr -> i256
    llvm.cond_br %enabled, ^update, ^skip
  ^update:
    %low = llvm.trunc %old : i256 to i16
    llvm.store %low, %cross : i16, !llvm.ptr
    llvm.return
  ^skip:
    llvm.return
  }
  llvm.func @main() -> i32 {
    %base = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %mask = llvm.mlir.addressof @__obelisk_state_unknown : !llvm.ptr
    %ready = llvm.mlir.addressof @ready : !llvm.ptr
    %slow = llvm.mlir.addressof @slow : !llvm.ptr
    %one = llvm.mlir.constant(1 : i64) : i64
    %zero = llvm.mlir.constant(0 : i64) : i64
    %success = llvm.mlir.constant(0 : i32) : i32
    %failure = llvm.mlir.constant(1 : i32) : i32
    llvm.call @wide() : () -> ()
    %inactive = llvm.load %base : !llvm.ptr -> i1792
    %original = llvm.mlir.constant(185750099682818275762466243853848465303771267256400648794099644386384751099266945852025607259042000376346708302960382341542622282240563392030250863098433374278230859784691476502871328083896713299386689624077985018843170095686071015159794312495613220622237151342521931945428854235859898518695072783294591768292503347458274926145153688963508060802481722591427779154769477054539388349930617326317302981360259499716973138217626972932349817160452199440088989489369759896401352158928070805498465167175702772316618597912474700748156735503854820371 : i1792) : i1792
    %inactive_ok = llvm.icmp "eq" %inactive, %original : i1792
    llvm.store %one, %ready : i64, !llvm.ptr
    llvm.call @wide() : () -> ()
    %updated = llvm.load %base : !llvm.ptr -> i1792
    %expected = llvm.mlir.constant(212017874894850449055828787716361163778334213415853169992499181030237336055259685705143227895319042056120876117256268375600239531766883067385467763990533204246902972541900110651468514353253034193774049560880697861824732843143646584801811608348002665999789639216571554555441626684334677876856269577055138970365955210409294401495647600182057563351275482770069105123174008494808559198162204822608154187590003057087544069616881271538019686934744023995008141620618044198457961704470304991603633049182410558965719816377587343374641179107420101651 : i1792) : i1792
    %value_ok = llvm.icmp "eq" %updated, %expected : i1792
    %pending = llvm.load %ready : !llvm.ptr -> i64
    %ready_ok = llvm.icmp "eq" %pending, %zero : i64
    %disabled = llvm.mlir.constant(false) : i1
    %enabled = llvm.mlir.constant(true) : i1
    llvm.call @small(%disabled) : (i1) -> ()
    %unmodified = llvm.load %mask : !llvm.ptr -> i512
    %original_mask = llvm.mlir.constant(8675640425256974593842016175309665141310177883912725126762304463584670842988765690871801016460937512034726496473608621140664277113612486435927655239201553 : i512) : i512
    %held_mask = llvm.icmp "eq" %unmodified, %original_mask : i512
    llvm.call @small(%enabled) : (i1) -> ()
    %unknown = llvm.load %mask : !llvm.ptr -> i512
    %expected_unknown = llvm.mlir.constant(8675640425256974593842016175309665141310177883912725126762304463584670842975498450397156646757171167768027927281608171178896429362029010443270983682175761 : i512) : i512
    %mask_ok = llvm.icmp "eq" %unknown, %expected_unknown : i512
    %fallback = llvm.load %slow : !llvm.ptr -> i32
    %native_ok = llvm.icmp "eq" %fallback, %success : i32
    %a = llvm.and %inactive_ok, %value_ok : i1
    %both_masks = llvm.and %mask_ok, %held_mask : i1
    %b = llvm.and %ready_ok, %both_masks : i1
    %c = llvm.and %a, %b : i1
    %all = llvm.and %c, %native_ok : i1
    %status = llvm.select %all, %success, %failure : i1, i32
    llvm.return %status : i32
  }
}
