// Four-state exhaustive oracle input. Values encode four low value bits and
// four high unknown bits; X and Z are kept distinct through the ABI.
func.func @decode(%arg: i8) -> !simulation.logic<4> {
  %s0 = arith.constant 0 : i8
  %v0 = arith.shrui %arg, %s0 : i8
  %vb0 = arith.trunci %v0 : i8 to i1
  %u0 = arith.constant 4 : i8
  %h0 = arith.shrui %arg, %u0 : i8
  %ub0 = arith.trunci %h0 : i8 to i1
  %known0 = simulation.logic.from_bits %vb0 : i1 -> !simulation.logic<1>
  %x0 = simulation.logic.constant 0 : i1, 1 : i1 : !simulation.logic<1>
  %z0 = simulation.logic.constant 1 : i1, 1 : i1 : !simulation.logic<1>
  %unknown0 = arith.select %vb0, %z0, %x0 : !simulation.logic<1>
  %bit0 = arith.select %ub0, %unknown0, %known0 : !simulation.logic<1>
  %s1 = arith.constant 1 : i8
  %v1 = arith.shrui %arg, %s1 : i8
  %vb1 = arith.trunci %v1 : i8 to i1
  %u1 = arith.constant 5 : i8
  %h1 = arith.shrui %arg, %u1 : i8
  %ub1 = arith.trunci %h1 : i8 to i1
  %known1 = simulation.logic.from_bits %vb1 : i1 -> !simulation.logic<1>
  %x1 = simulation.logic.constant 0 : i1, 1 : i1 : !simulation.logic<1>
  %z1 = simulation.logic.constant 1 : i1, 1 : i1 : !simulation.logic<1>
  %unknown1 = arith.select %vb1, %z1, %x1 : !simulation.logic<1>
  %bit1 = arith.select %ub1, %unknown1, %known1 : !simulation.logic<1>
  %s2 = arith.constant 2 : i8
  %v2 = arith.shrui %arg, %s2 : i8
  %vb2 = arith.trunci %v2 : i8 to i1
  %u2 = arith.constant 6 : i8
  %h2 = arith.shrui %arg, %u2 : i8
  %ub2 = arith.trunci %h2 : i8 to i1
  %known2 = simulation.logic.from_bits %vb2 : i1 -> !simulation.logic<1>
  %x2 = simulation.logic.constant 0 : i1, 1 : i1 : !simulation.logic<1>
  %z2 = simulation.logic.constant 1 : i1, 1 : i1 : !simulation.logic<1>
  %unknown2 = arith.select %vb2, %z2, %x2 : !simulation.logic<1>
  %bit2 = arith.select %ub2, %unknown2, %known2 : !simulation.logic<1>
  %s3 = arith.constant 3 : i8
  %v3 = arith.shrui %arg, %s3 : i8
  %vb3 = arith.trunci %v3 : i8 to i1
  %u3 = arith.constant 7 : i8
  %h3 = arith.shrui %arg, %u3 : i8
  %ub3 = arith.trunci %h3 : i8 to i1
  %known3 = simulation.logic.from_bits %vb3 : i1 -> !simulation.logic<1>
  %x3 = simulation.logic.constant 0 : i1, 1 : i1 : !simulation.logic<1>
  %z3 = simulation.logic.constant 1 : i1, 1 : i1 : !simulation.logic<1>
  %unknown3 = arith.select %vb3, %z3, %x3 : !simulation.logic<1>
  %bit3 = arith.select %ub3, %unknown3, %known3 : !simulation.logic<1>
  %r = simulation.logic.concat %bit3, %bit2, %bit1, %bit0 : (!simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>) -> !simulation.logic<4>
  return %r : !simulation.logic<4>
}

func.func @encode(%arg: !simulation.logic<4>) -> i8 {
  %zero = arith.constant 0 : i8
  %x = simulation.logic.constant 0 : i1, 1 : i1 : !simulation.logic<1>
  %z = simulation.logic.constant 1 : i1, 1 : i1 : !simulation.logic<1>
  %bit0 = simulation.logic.extract %arg from 0 : !simulation.logic<4> -> !simulation.logic<1>
  %x0 = simulation.logic.compare case_eq %bit0, %x : (!simulation.logic<1>, !simulation.logic<1>) -> i1
  %z0 = simulation.logic.compare case_eq %bit0, %z : (!simulation.logic<1>, !simulation.logic<1>) -> i1
  %one0 = simulation.logic.is_true %bit0 : !simulation.logic<1>
  %v0 = arith.ori %one0, %z0 : i1
  %u0 = arith.ori %x0, %z0 : i1
  %ve0 = arith.extui %v0 : i1 to i8
  %ue0 = arith.extui %u0 : i1 to i8
  %vs0 = arith.constant 0 : i8
  %us0 = arith.constant 4 : i8
  %vp0 = arith.shli %ve0, %vs0 : i8
  %up0 = arith.shli %ue0, %us0 : i8
  %p0 = arith.ori %vp0, %up0 : i8
  %r0 = arith.ori %zero, %p0 : i8
  %bit1 = simulation.logic.extract %arg from 1 : !simulation.logic<4> -> !simulation.logic<1>
  %x1 = simulation.logic.compare case_eq %bit1, %x : (!simulation.logic<1>, !simulation.logic<1>) -> i1
  %z1 = simulation.logic.compare case_eq %bit1, %z : (!simulation.logic<1>, !simulation.logic<1>) -> i1
  %one1 = simulation.logic.is_true %bit1 : !simulation.logic<1>
  %v1 = arith.ori %one1, %z1 : i1
  %u1 = arith.ori %x1, %z1 : i1
  %ve1 = arith.extui %v1 : i1 to i8
  %ue1 = arith.extui %u1 : i1 to i8
  %vs1 = arith.constant 1 : i8
  %us1 = arith.constant 5 : i8
  %vp1 = arith.shli %ve1, %vs1 : i8
  %up1 = arith.shli %ue1, %us1 : i8
  %p1 = arith.ori %vp1, %up1 : i8
  %r1 = arith.ori %r0, %p1 : i8
  %bit2 = simulation.logic.extract %arg from 2 : !simulation.logic<4> -> !simulation.logic<1>
  %x2 = simulation.logic.compare case_eq %bit2, %x : (!simulation.logic<1>, !simulation.logic<1>) -> i1
  %z2 = simulation.logic.compare case_eq %bit2, %z : (!simulation.logic<1>, !simulation.logic<1>) -> i1
  %one2 = simulation.logic.is_true %bit2 : !simulation.logic<1>
  %v2 = arith.ori %one2, %z2 : i1
  %u2 = arith.ori %x2, %z2 : i1
  %ve2 = arith.extui %v2 : i1 to i8
  %ue2 = arith.extui %u2 : i1 to i8
  %vs2 = arith.constant 2 : i8
  %us2 = arith.constant 6 : i8
  %vp2 = arith.shli %ve2, %vs2 : i8
  %up2 = arith.shli %ue2, %us2 : i8
  %p2 = arith.ori %vp2, %up2 : i8
  %r2 = arith.ori %r1, %p2 : i8
  %bit3 = simulation.logic.extract %arg from 3 : !simulation.logic<4> -> !simulation.logic<1>
  %x3 = simulation.logic.compare case_eq %bit3, %x : (!simulation.logic<1>, !simulation.logic<1>) -> i1
  %z3 = simulation.logic.compare case_eq %bit3, %z : (!simulation.logic<1>, !simulation.logic<1>) -> i1
  %one3 = simulation.logic.is_true %bit3 : !simulation.logic<1>
  %v3 = arith.ori %one3, %z3 : i1
  %u3 = arith.ori %x3, %z3 : i1
  %ve3 = arith.extui %v3 : i1 to i8
  %ue3 = arith.extui %u3 : i1 to i8
  %vs3 = arith.constant 3 : i8
  %us3 = arith.constant 7 : i8
  %vp3 = arith.shli %ve3, %vs3 : i8
  %up3 = arith.shli %ue3, %us3 : i8
  %p3 = arith.ori %vp3, %up3 : i8
  %r3 = arith.ori %r2, %p3 : i8
  return %r3 : i8
}

func.func @assoc0(%arg: i8) -> i64 {
  %a = func.call @decode(%arg) : (i8) -> !simulation.logic<4>
  %c = simulation.logic.constant 2 : i4, 0 : i4 : !simulation.logic<4>
  %d = simulation.logic.constant 3 : i4, 0 : i4 : !simulation.logic<4>
  %i0 = simulation.logic.binary add %a, %c : !simulation.logic<4>
  %r0 = simulation.logic.binary add %i0, %d : !simulation.logic<4>
  %i1 = simulation.logic.binary mul %a, %c : !simulation.logic<4>
  %r1 = simulation.logic.binary mul %i1, %d : !simulation.logic<4>
  %i2 = simulation.logic.binary and %a, %c : !simulation.logic<4>
  %r2 = simulation.logic.binary and %i2, %d : !simulation.logic<4>
  %i3 = simulation.logic.binary or %a, %c : !simulation.logic<4>
  %r3 = simulation.logic.binary or %i3, %d : !simulation.logic<4>
  %i4 = simulation.logic.binary xor %a, %c : !simulation.logic<4>
  %r4 = simulation.logic.binary xor %i4, %d : !simulation.logic<4>
  %i5 = simulation.logic.binary xnor %a, %c : !simulation.logic<4>
  %r5 = simulation.logic.binary xnor %i5, %d : !simulation.logic<4>
  %enc0 = func.call @encode(%r0) : (!simulation.logic<4>) -> i8
  %e0 = arith.extui %enc0 : i8 to i64
  %s0 = arith.constant 0 : i64
  %p0 = arith.shli %e0, %s0 : i64
  %enc1 = func.call @encode(%r1) : (!simulation.logic<4>) -> i8
  %e1 = arith.extui %enc1 : i8 to i64
  %s1 = arith.constant 8 : i64
  %p1 = arith.shli %e1, %s1 : i64
  %pack1 = arith.ori %p0, %p1 : i64
  %enc2 = func.call @encode(%r2) : (!simulation.logic<4>) -> i8
  %e2 = arith.extui %enc2 : i8 to i64
  %s2 = arith.constant 16 : i64
  %p2 = arith.shli %e2, %s2 : i64
  %pack2 = arith.ori %pack1, %p2 : i64
  %enc3 = func.call @encode(%r3) : (!simulation.logic<4>) -> i8
  %e3 = arith.extui %enc3 : i8 to i64
  %s3 = arith.constant 24 : i64
  %p3 = arith.shli %e3, %s3 : i64
  %pack3 = arith.ori %pack2, %p3 : i64
  %enc4 = func.call @encode(%r4) : (!simulation.logic<4>) -> i8
  %e4 = arith.extui %enc4 : i8 to i64
  %s4 = arith.constant 32 : i64
  %p4 = arith.shli %e4, %s4 : i64
  %pack4 = arith.ori %pack3, %p4 : i64
  %enc5 = func.call @encode(%r5) : (!simulation.logic<4>) -> i8
  %e5 = arith.extui %enc5 : i8 to i64
  %s5 = arith.constant 40 : i64
  %p5 = arith.shli %e5, %s5 : i64
  %pack5 = arith.ori %pack4, %p5 : i64
  return %pack5 : i64
}

func.func @assoc1(%arg: i8) -> i64 {
  %a = func.call @decode(%arg) : (i8) -> !simulation.logic<4>
  %c = simulation.logic.constant 0 : i4, 0 : i4 : !simulation.logic<4>
  %d = simulation.logic.constant 15 : i4, 0 : i4 : !simulation.logic<4>
  %i0 = simulation.logic.binary add %a, %c : !simulation.logic<4>
  %r0 = simulation.logic.binary add %i0, %d : !simulation.logic<4>
  %i1 = simulation.logic.binary mul %a, %c : !simulation.logic<4>
  %r1 = simulation.logic.binary mul %i1, %d : !simulation.logic<4>
  %i2 = simulation.logic.binary and %a, %c : !simulation.logic<4>
  %r2 = simulation.logic.binary and %i2, %d : !simulation.logic<4>
  %i3 = simulation.logic.binary or %a, %c : !simulation.logic<4>
  %r3 = simulation.logic.binary or %i3, %d : !simulation.logic<4>
  %i4 = simulation.logic.binary xor %a, %c : !simulation.logic<4>
  %r4 = simulation.logic.binary xor %i4, %d : !simulation.logic<4>
  %i5 = simulation.logic.binary xnor %a, %c : !simulation.logic<4>
  %r5 = simulation.logic.binary xnor %i5, %d : !simulation.logic<4>
  %enc0 = func.call @encode(%r0) : (!simulation.logic<4>) -> i8
  %e0 = arith.extui %enc0 : i8 to i64
  %s0 = arith.constant 0 : i64
  %p0 = arith.shli %e0, %s0 : i64
  %enc1 = func.call @encode(%r1) : (!simulation.logic<4>) -> i8
  %e1 = arith.extui %enc1 : i8 to i64
  %s1 = arith.constant 8 : i64
  %p1 = arith.shli %e1, %s1 : i64
  %pack1 = arith.ori %p0, %p1 : i64
  %enc2 = func.call @encode(%r2) : (!simulation.logic<4>) -> i8
  %e2 = arith.extui %enc2 : i8 to i64
  %s2 = arith.constant 16 : i64
  %p2 = arith.shli %e2, %s2 : i64
  %pack2 = arith.ori %pack1, %p2 : i64
  %enc3 = func.call @encode(%r3) : (!simulation.logic<4>) -> i8
  %e3 = arith.extui %enc3 : i8 to i64
  %s3 = arith.constant 24 : i64
  %p3 = arith.shli %e3, %s3 : i64
  %pack3 = arith.ori %pack2, %p3 : i64
  %enc4 = func.call @encode(%r4) : (!simulation.logic<4>) -> i8
  %e4 = arith.extui %enc4 : i8 to i64
  %s4 = arith.constant 32 : i64
  %p4 = arith.shli %e4, %s4 : i64
  %pack4 = arith.ori %pack3, %p4 : i64
  %enc5 = func.call @encode(%r5) : (!simulation.logic<4>) -> i8
  %e5 = arith.extui %enc5 : i8 to i64
  %s5 = arith.constant 40 : i64
  %p5 = arith.shli %e5, %s5 : i64
  %pack5 = arith.ori %pack4, %p5 : i64
  return %pack5 : i64
}

func.func @assoc2(%arg: i8) -> i64 {
  %a = func.call @decode(%arg) : (i8) -> !simulation.logic<4>
  %c = simulation.logic.constant 3 : i4, 10 : i4 : !simulation.logic<4>
  %d = simulation.logic.constant 12 : i4, 5 : i4 : !simulation.logic<4>
  %i0 = simulation.logic.binary add %a, %c : !simulation.logic<4>
  %r0 = simulation.logic.binary add %i0, %d : !simulation.logic<4>
  %i1 = simulation.logic.binary mul %a, %c : !simulation.logic<4>
  %r1 = simulation.logic.binary mul %i1, %d : !simulation.logic<4>
  %i2 = simulation.logic.binary and %a, %c : !simulation.logic<4>
  %r2 = simulation.logic.binary and %i2, %d : !simulation.logic<4>
  %i3 = simulation.logic.binary or %a, %c : !simulation.logic<4>
  %r3 = simulation.logic.binary or %i3, %d : !simulation.logic<4>
  %i4 = simulation.logic.binary xor %a, %c : !simulation.logic<4>
  %r4 = simulation.logic.binary xor %i4, %d : !simulation.logic<4>
  %i5 = simulation.logic.binary xnor %a, %c : !simulation.logic<4>
  %r5 = simulation.logic.binary xnor %i5, %d : !simulation.logic<4>
  %enc0 = func.call @encode(%r0) : (!simulation.logic<4>) -> i8
  %e0 = arith.extui %enc0 : i8 to i64
  %s0 = arith.constant 0 : i64
  %p0 = arith.shli %e0, %s0 : i64
  %enc1 = func.call @encode(%r1) : (!simulation.logic<4>) -> i8
  %e1 = arith.extui %enc1 : i8 to i64
  %s1 = arith.constant 8 : i64
  %p1 = arith.shli %e1, %s1 : i64
  %pack1 = arith.ori %p0, %p1 : i64
  %enc2 = func.call @encode(%r2) : (!simulation.logic<4>) -> i8
  %e2 = arith.extui %enc2 : i8 to i64
  %s2 = arith.constant 16 : i64
  %p2 = arith.shli %e2, %s2 : i64
  %pack2 = arith.ori %pack1, %p2 : i64
  %enc3 = func.call @encode(%r3) : (!simulation.logic<4>) -> i8
  %e3 = arith.extui %enc3 : i8 to i64
  %s3 = arith.constant 24 : i64
  %p3 = arith.shli %e3, %s3 : i64
  %pack3 = arith.ori %pack2, %p3 : i64
  %enc4 = func.call @encode(%r4) : (!simulation.logic<4>) -> i8
  %e4 = arith.extui %enc4 : i8 to i64
  %s4 = arith.constant 32 : i64
  %p4 = arith.shli %e4, %s4 : i64
  %pack4 = arith.ori %pack3, %p4 : i64
  %enc5 = func.call @encode(%r5) : (!simulation.logic<4>) -> i8
  %e5 = arith.extui %enc5 : i8 to i64
  %s5 = arith.constant 40 : i64
  %p5 = arith.shli %e5, %s5 : i64
  %pack5 = arith.ori %pack4, %p5 : i64
  return %pack5 : i64
}

func.func @assoc3(%arg: i8) -> i64 {
  %a = func.call @decode(%arg) : (i8) -> !simulation.logic<4>
  %c = simulation.logic.constant 0 : i4, 15 : i4 : !simulation.logic<4>
  %d = simulation.logic.constant 15 : i4, 15 : i4 : !simulation.logic<4>
  %i0 = simulation.logic.binary add %a, %c : !simulation.logic<4>
  %r0 = simulation.logic.binary add %i0, %d : !simulation.logic<4>
  %i1 = simulation.logic.binary mul %a, %c : !simulation.logic<4>
  %r1 = simulation.logic.binary mul %i1, %d : !simulation.logic<4>
  %i2 = simulation.logic.binary and %a, %c : !simulation.logic<4>
  %r2 = simulation.logic.binary and %i2, %d : !simulation.logic<4>
  %i3 = simulation.logic.binary or %a, %c : !simulation.logic<4>
  %r3 = simulation.logic.binary or %i3, %d : !simulation.logic<4>
  %i4 = simulation.logic.binary xor %a, %c : !simulation.logic<4>
  %r4 = simulation.logic.binary xor %i4, %d : !simulation.logic<4>
  %i5 = simulation.logic.binary xnor %a, %c : !simulation.logic<4>
  %r5 = simulation.logic.binary xnor %i5, %d : !simulation.logic<4>
  %enc0 = func.call @encode(%r0) : (!simulation.logic<4>) -> i8
  %e0 = arith.extui %enc0 : i8 to i64
  %s0 = arith.constant 0 : i64
  %p0 = arith.shli %e0, %s0 : i64
  %enc1 = func.call @encode(%r1) : (!simulation.logic<4>) -> i8
  %e1 = arith.extui %enc1 : i8 to i64
  %s1 = arith.constant 8 : i64
  %p1 = arith.shli %e1, %s1 : i64
  %pack1 = arith.ori %p0, %p1 : i64
  %enc2 = func.call @encode(%r2) : (!simulation.logic<4>) -> i8
  %e2 = arith.extui %enc2 : i8 to i64
  %s2 = arith.constant 16 : i64
  %p2 = arith.shli %e2, %s2 : i64
  %pack2 = arith.ori %pack1, %p2 : i64
  %enc3 = func.call @encode(%r3) : (!simulation.logic<4>) -> i8
  %e3 = arith.extui %enc3 : i8 to i64
  %s3 = arith.constant 24 : i64
  %p3 = arith.shli %e3, %s3 : i64
  %pack3 = arith.ori %pack2, %p3 : i64
  %enc4 = func.call @encode(%r4) : (!simulation.logic<4>) -> i8
  %e4 = arith.extui %enc4 : i8 to i64
  %s4 = arith.constant 32 : i64
  %p4 = arith.shli %e4, %s4 : i64
  %pack4 = arith.ori %pack3, %p4 : i64
  %enc5 = func.call @encode(%r5) : (!simulation.logic<4>) -> i8
  %e5 = arith.extui %enc5 : i8 to i64
  %s5 = arith.constant 40 : i64
  %p5 = arith.shli %e5, %s5 : i64
  %pack5 = arith.ori %pack4, %p5 : i64
  return %pack5 : i64
}

func.func @logical(%arg: i8) -> i64 {
  %a = func.call @decode(%arg) : (i8) -> !simulation.logic<4>
  %zero = simulation.logic.constant 0 : i1, 0 : i1 : !simulation.logic<1>
  %one = simulation.logic.constant 1 : i1, 0 : i1 : !simulation.logic<1>
  %r0 = simulation.logic.logical and %a, %one : (!simulation.logic<4>, !simulation.logic<1>) -> !simulation.logic<1>
  %r1 = simulation.logic.logical or %a, %zero : (!simulation.logic<4>, !simulation.logic<1>) -> !simulation.logic<1>
  %r2 = simulation.logic.logical and %a, %a : (!simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<1>
  %r3 = simulation.logic.logical or %a, %a : (!simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<1>
  %r4 = simulation.logic.compare wild_eq %a, %a : (!simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<1>
  %r5 = simulation.logic.compare wild_ne %a, %a : (!simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<1>
  %wide0 = simulation.logic.resize %r0 signed = false : !simulation.logic<1> -> !simulation.logic<4>
  %enc0 = func.call @encode(%wide0) : (!simulation.logic<4>) -> i8
  %e0 = arith.extui %enc0 : i8 to i64
  %s0 = arith.constant 0 : i64
  %p0 = arith.shli %e0, %s0 : i64
  %wide1 = simulation.logic.resize %r1 signed = false : !simulation.logic<1> -> !simulation.logic<4>
  %enc1 = func.call @encode(%wide1) : (!simulation.logic<4>) -> i8
  %e1 = arith.extui %enc1 : i8 to i64
  %s1 = arith.constant 8 : i64
  %p1 = arith.shli %e1, %s1 : i64
  %pack1 = arith.ori %p0, %p1 : i64
  %wide2 = simulation.logic.resize %r2 signed = false : !simulation.logic<1> -> !simulation.logic<4>
  %enc2 = func.call @encode(%wide2) : (!simulation.logic<4>) -> i8
  %e2 = arith.extui %enc2 : i8 to i64
  %s2 = arith.constant 16 : i64
  %p2 = arith.shli %e2, %s2 : i64
  %pack2 = arith.ori %pack1, %p2 : i64
  %wide3 = simulation.logic.resize %r3 signed = false : !simulation.logic<1> -> !simulation.logic<4>
  %enc3 = func.call @encode(%wide3) : (!simulation.logic<4>) -> i8
  %e3 = arith.extui %enc3 : i8 to i64
  %s3 = arith.constant 24 : i64
  %p3 = arith.shli %e3, %s3 : i64
  %pack3 = arith.ori %pack2, %p3 : i64
  %wide4 = simulation.logic.resize %r4 signed = false : !simulation.logic<1> -> !simulation.logic<4>
  %enc4 = func.call @encode(%wide4) : (!simulation.logic<4>) -> i8
  %e4 = arith.extui %enc4 : i8 to i64
  %s4 = arith.constant 32 : i64
  %p4 = arith.shli %e4, %s4 : i64
  %pack4 = arith.ori %pack3, %p4 : i64
  %wide5 = simulation.logic.resize %r5 signed = false : !simulation.logic<1> -> !simulation.logic<4>
  %enc5 = func.call @encode(%wide5) : (!simulation.logic<4>) -> i8
  %e5 = arith.extui %enc5 : i8 to i64
  %s5 = arith.constant 40 : i64
  %p5 = arith.shli %e5, %s5 : i64
  %pack5 = arith.ori %pack4, %p5 : i64
  return %pack5 : i64
}

func.func @reduction2(%arg: i8) -> i64 {
  %a = func.call @decode(%arg) : (i8) -> !simulation.logic<4>
  %w = simulation.logic.replicate %a times 2 : !simulation.logic<4> -> !simulation.logic<8>
  %r0 = simulation.logic.reduction and %w : !simulation.logic<8> -> !simulation.logic<1>
  %r1 = simulation.logic.reduction nand %w : !simulation.logic<8> -> !simulation.logic<1>
  %r2 = simulation.logic.reduction or %w : !simulation.logic<8> -> !simulation.logic<1>
  %r3 = simulation.logic.reduction nor %w : !simulation.logic<8> -> !simulation.logic<1>
  %r4 = simulation.logic.reduction xor %w : !simulation.logic<8> -> !simulation.logic<1>
  %r5 = simulation.logic.reduction xnor %w : !simulation.logic<8> -> !simulation.logic<1>
  %wide0 = simulation.logic.resize %r0 signed = false : !simulation.logic<1> -> !simulation.logic<4>
  %enc0 = func.call @encode(%wide0) : (!simulation.logic<4>) -> i8
  %e0 = arith.extui %enc0 : i8 to i64
  %s0 = arith.constant 0 : i64
  %p0 = arith.shli %e0, %s0 : i64
  %wide1 = simulation.logic.resize %r1 signed = false : !simulation.logic<1> -> !simulation.logic<4>
  %enc1 = func.call @encode(%wide1) : (!simulation.logic<4>) -> i8
  %e1 = arith.extui %enc1 : i8 to i64
  %s1 = arith.constant 8 : i64
  %p1 = arith.shli %e1, %s1 : i64
  %pack1 = arith.ori %p0, %p1 : i64
  %wide2 = simulation.logic.resize %r2 signed = false : !simulation.logic<1> -> !simulation.logic<4>
  %enc2 = func.call @encode(%wide2) : (!simulation.logic<4>) -> i8
  %e2 = arith.extui %enc2 : i8 to i64
  %s2 = arith.constant 16 : i64
  %p2 = arith.shli %e2, %s2 : i64
  %pack2 = arith.ori %pack1, %p2 : i64
  %wide3 = simulation.logic.resize %r3 signed = false : !simulation.logic<1> -> !simulation.logic<4>
  %enc3 = func.call @encode(%wide3) : (!simulation.logic<4>) -> i8
  %e3 = arith.extui %enc3 : i8 to i64
  %s3 = arith.constant 24 : i64
  %p3 = arith.shli %e3, %s3 : i64
  %pack3 = arith.ori %pack2, %p3 : i64
  %wide4 = simulation.logic.resize %r4 signed = false : !simulation.logic<1> -> !simulation.logic<4>
  %enc4 = func.call @encode(%wide4) : (!simulation.logic<4>) -> i8
  %e4 = arith.extui %enc4 : i8 to i64
  %s4 = arith.constant 32 : i64
  %p4 = arith.shli %e4, %s4 : i64
  %pack4 = arith.ori %pack3, %p4 : i64
  %wide5 = simulation.logic.resize %r5 signed = false : !simulation.logic<1> -> !simulation.logic<4>
  %enc5 = func.call @encode(%wide5) : (!simulation.logic<4>) -> i8
  %e5 = arith.extui %enc5 : i8 to i64
  %s5 = arith.constant 40 : i64
  %p5 = arith.shli %e5, %s5 : i64
  %pack5 = arith.ori %pack4, %p5 : i64
  return %pack5 : i64
}

func.func @reduction3(%arg: i8) -> i64 {
  %a = func.call @decode(%arg) : (i8) -> !simulation.logic<4>
  %w = simulation.logic.replicate %a times 3 : !simulation.logic<4> -> !simulation.logic<12>
  %r0 = simulation.logic.reduction and %w : !simulation.logic<12> -> !simulation.logic<1>
  %r1 = simulation.logic.reduction nand %w : !simulation.logic<12> -> !simulation.logic<1>
  %r2 = simulation.logic.reduction or %w : !simulation.logic<12> -> !simulation.logic<1>
  %r3 = simulation.logic.reduction nor %w : !simulation.logic<12> -> !simulation.logic<1>
  %r4 = simulation.logic.reduction xor %w : !simulation.logic<12> -> !simulation.logic<1>
  %r5 = simulation.logic.reduction xnor %w : !simulation.logic<12> -> !simulation.logic<1>
  %wide0 = simulation.logic.resize %r0 signed = false : !simulation.logic<1> -> !simulation.logic<4>
  %enc0 = func.call @encode(%wide0) : (!simulation.logic<4>) -> i8
  %e0 = arith.extui %enc0 : i8 to i64
  %s0 = arith.constant 0 : i64
  %p0 = arith.shli %e0, %s0 : i64
  %wide1 = simulation.logic.resize %r1 signed = false : !simulation.logic<1> -> !simulation.logic<4>
  %enc1 = func.call @encode(%wide1) : (!simulation.logic<4>) -> i8
  %e1 = arith.extui %enc1 : i8 to i64
  %s1 = arith.constant 8 : i64
  %p1 = arith.shli %e1, %s1 : i64
  %pack1 = arith.ori %p0, %p1 : i64
  %wide2 = simulation.logic.resize %r2 signed = false : !simulation.logic<1> -> !simulation.logic<4>
  %enc2 = func.call @encode(%wide2) : (!simulation.logic<4>) -> i8
  %e2 = arith.extui %enc2 : i8 to i64
  %s2 = arith.constant 16 : i64
  %p2 = arith.shli %e2, %s2 : i64
  %pack2 = arith.ori %pack1, %p2 : i64
  %wide3 = simulation.logic.resize %r3 signed = false : !simulation.logic<1> -> !simulation.logic<4>
  %enc3 = func.call @encode(%wide3) : (!simulation.logic<4>) -> i8
  %e3 = arith.extui %enc3 : i8 to i64
  %s3 = arith.constant 24 : i64
  %p3 = arith.shli %e3, %s3 : i64
  %pack3 = arith.ori %pack2, %p3 : i64
  %wide4 = simulation.logic.resize %r4 signed = false : !simulation.logic<1> -> !simulation.logic<4>
  %enc4 = func.call @encode(%wide4) : (!simulation.logic<4>) -> i8
  %e4 = arith.extui %enc4 : i8 to i64
  %s4 = arith.constant 32 : i64
  %p4 = arith.shli %e4, %s4 : i64
  %pack4 = arith.ori %pack3, %p4 : i64
  %wide5 = simulation.logic.resize %r5 signed = false : !simulation.logic<1> -> !simulation.logic<4>
  %enc5 = func.call @encode(%wide5) : (!simulation.logic<4>) -> i8
  %e5 = arith.extui %enc5 : i8 to i64
  %s5 = arith.constant 40 : i64
  %p5 = arith.shli %e5, %s5 : i64
  %pack5 = arith.ori %pack4, %p5 : i64
  return %pack5 : i64
}

func.func @mux(%arg: i8, %other: i8, %cond: i8) -> i64 {
  %a = func.call @decode(%arg) : (i8) -> !simulation.logic<4>
  %b = func.call @decode(%other) : (i8) -> !simulation.logic<4>
  %cb = arith.trunci %cond : i8 to i1
  %c = simulation.logic.from_bits %cb : i1 -> !simulation.logic<1>
  %x = simulation.logic.constant 0 : i1, 1 : i1 : !simulation.logic<1>
  %known = arith.trunci %arg : i8 to i4
  %k = simulation.logic.from_bits %known : i4 -> !simulation.logic<4>
  %r0 = simulation.logic.mux %c ? %a : %b : (!simulation.logic<1>, !simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<4>
  %r1 = simulation.logic.mux %x ? %a : %a : (!simulation.logic<1>, !simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<4>
  %r2 = simulation.logic.mux %x ? %k : %k : (!simulation.logic<1>, !simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<4>
  %enc0 = func.call @encode(%r0) : (!simulation.logic<4>) -> i8
  %e0 = arith.extui %enc0 : i8 to i64
  %s0 = arith.constant 0 : i64
  %p0 = arith.shli %e0, %s0 : i64
  %enc1 = func.call @encode(%r1) : (!simulation.logic<4>) -> i8
  %e1 = arith.extui %enc1 : i8 to i64
  %s1 = arith.constant 8 : i64
  %p1 = arith.shli %e1, %s1 : i64
  %pack1 = arith.ori %p0, %p1 : i64
  %enc2 = func.call @encode(%r2) : (!simulation.logic<4>) -> i8
  %e2 = arith.extui %enc2 : i8 to i64
  %s2 = arith.constant 16 : i64
  %p2 = arith.shli %e2, %s2 : i64
  %pack2 = arith.ori %pack1, %p2 : i64
  return %pack2 : i64
}

func.func @known(%arg: i8, %other: i8) -> i64 {
  %n = arith.trunci %arg : i8 to i4
  %m = arith.trunci %other : i8 to i4
  %a = simulation.logic.from_bits %n : i4 -> !simulation.logic<4>
  %b = simulation.logic.from_bits %m : i4 -> !simulation.logic<4>
  %r0 = simulation.logic.unary negate %a : (!simulation.logic<4>) -> !simulation.logic<4>
  %r1 = simulation.logic.unary bit_not %a : (!simulation.logic<4>) -> !simulation.logic<4>
  %r2 = simulation.logic.unary logical_not %a : (!simulation.logic<4>) -> !simulation.logic<1>
  %r3 = simulation.logic.reduction and %a : !simulation.logic<4> -> !simulation.logic<1>
  %r4 = simulation.logic.reduction nand %a : !simulation.logic<4> -> !simulation.logic<1>
  %r5 = simulation.logic.reduction or %a : !simulation.logic<4> -> !simulation.logic<1>
  %r6 = simulation.logic.reduction nor %a : !simulation.logic<4> -> !simulation.logic<1>
  %r7 = simulation.logic.logical and %a, %b : (!simulation.logic<4>, !simulation.logic<4>) -> !simulation.logic<1>
  %enc0 = func.call @encode(%r0) : (!simulation.logic<4>) -> i8
  %e0 = arith.extui %enc0 : i8 to i64
  %s0 = arith.constant 0 : i64
  %p0 = arith.shli %e0, %s0 : i64
  %enc1 = func.call @encode(%r1) : (!simulation.logic<4>) -> i8
  %e1 = arith.extui %enc1 : i8 to i64
  %s1 = arith.constant 8 : i64
  %p1 = arith.shli %e1, %s1 : i64
  %pack1 = arith.ori %p0, %p1 : i64
  %wide2 = simulation.logic.resize %r2 signed = false : !simulation.logic<1> -> !simulation.logic<4>
  %enc2 = func.call @encode(%wide2) : (!simulation.logic<4>) -> i8
  %e2 = arith.extui %enc2 : i8 to i64
  %s2 = arith.constant 16 : i64
  %p2 = arith.shli %e2, %s2 : i64
  %pack2 = arith.ori %pack1, %p2 : i64
  %wide3 = simulation.logic.resize %r3 signed = false : !simulation.logic<1> -> !simulation.logic<4>
  %enc3 = func.call @encode(%wide3) : (!simulation.logic<4>) -> i8
  %e3 = arith.extui %enc3 : i8 to i64
  %s3 = arith.constant 24 : i64
  %p3 = arith.shli %e3, %s3 : i64
  %pack3 = arith.ori %pack2, %p3 : i64
  %wide4 = simulation.logic.resize %r4 signed = false : !simulation.logic<1> -> !simulation.logic<4>
  %enc4 = func.call @encode(%wide4) : (!simulation.logic<4>) -> i8
  %e4 = arith.extui %enc4 : i8 to i64
  %s4 = arith.constant 32 : i64
  %p4 = arith.shli %e4, %s4 : i64
  %pack4 = arith.ori %pack3, %p4 : i64
  %wide5 = simulation.logic.resize %r5 signed = false : !simulation.logic<1> -> !simulation.logic<4>
  %enc5 = func.call @encode(%wide5) : (!simulation.logic<4>) -> i8
  %e5 = arith.extui %enc5 : i8 to i64
  %s5 = arith.constant 40 : i64
  %p5 = arith.shli %e5, %s5 : i64
  %pack5 = arith.ori %pack4, %p5 : i64
  %wide6 = simulation.logic.resize %r6 signed = false : !simulation.logic<1> -> !simulation.logic<4>
  %enc6 = func.call @encode(%wide6) : (!simulation.logic<4>) -> i8
  %e6 = arith.extui %enc6 : i8 to i64
  %s6 = arith.constant 48 : i64
  %p6 = arith.shli %e6, %s6 : i64
  %pack6 = arith.ori %pack5, %p6 : i64
  %wide7 = simulation.logic.resize %r7 signed = false : !simulation.logic<1> -> !simulation.logic<4>
  %enc7 = func.call @encode(%wide7) : (!simulation.logic<4>) -> i8
  %e7 = arith.extui %enc7 : i8 to i64
  %s7 = arith.constant 56 : i64
  %p7 = arith.shli %e7, %s7 : i64
  %pack7 = arith.ori %pack6, %p7 : i64
  return %pack7 : i64
}
