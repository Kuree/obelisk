// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' --convert-obelisk-sim-processes-to-llvm-coroutines -o %t.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.mlir

// Runtime behavior is checked in ../Runtime/simulation-static-registration-batches.test.

// Cross a registration batch boundary, with a managed root on each side.
// This checks native initialization and observes the last registered range.
// PLAN-LABEL: llvm.func @main(
// PLAN: llvm.call @__obelisk_register_static_state_0(
// PLAN-NEXT: llvm.call @__obelisk_register_static_state_256(
// PLAN: llvm.call @obelisk_rt_v1_native_state_sync(
// PLAN-LABEL: llvm.func internal @__obelisk_register_static_state_0(
// PLAN-SAME: no_inline
// PLAN-COUNT-256: llvm.call @obelisk_rt_v1_native_state_register_static(
// PLAN: llvm.call @obelisk_rt_v1_gc_candidate_static_root_register(
// PLAN: llvm.call @obelisk_rt_v1_gc_design_candidate_root_register(
// PLAN: llvm.return
// PLAN-LABEL: llvm.func internal @__obelisk_register_static_state_256(
// PLAN-SAME: no_inline
// PLAN: llvm.call @obelisk_rt_v1_native_state_register_static(
// PLAN: llvm.call @obelisk_rt_v1_gc_candidate_static_root_register(
// PLAN: llvm.call @obelisk_rt_v1_gc_design_candidate_root_register(
// PLAN: llvm.call @obelisk_rt_v1_native_state_register_static(
// PLAN: llvm.return

!byte = !obelisk_sim.logic<8>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk_sim.design @registration {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "registration.root"
    obelisk_sim.storage.decl 0 in 0 : !byte design
    obelisk_sim.storage.decl 1 in 0 : !byte design
    obelisk_sim.storage.decl 2 in 0 : !byte design
    obelisk_sim.storage.decl 3 in 0 : !byte design
    obelisk_sim.storage.decl 4 in 0 : !byte design
    obelisk_sim.storage.decl 5 in 0 : !byte design
    obelisk_sim.storage.decl 6 in 0 : !byte design
    obelisk_sim.storage.decl 7 in 0 : !byte design
    obelisk_sim.storage.decl 8 in 0 : !byte design
    obelisk_sim.storage.decl 9 in 0 : !byte design
    obelisk_sim.storage.decl 10 in 0 : !byte design
    obelisk_sim.storage.decl 11 in 0 : !byte design
    obelisk_sim.storage.decl 12 in 0 : !byte design
    obelisk_sim.storage.decl 13 in 0 : !byte design
    obelisk_sim.storage.decl 14 in 0 : !byte design
    obelisk_sim.storage.decl 15 in 0 : !byte design
    obelisk_sim.storage.decl 16 in 0 : !byte design
    obelisk_sim.storage.decl 17 in 0 : !byte design
    obelisk_sim.storage.decl 18 in 0 : !byte design
    obelisk_sim.storage.decl 19 in 0 : !byte design
    obelisk_sim.storage.decl 20 in 0 : !byte design
    obelisk_sim.storage.decl 21 in 0 : !byte design
    obelisk_sim.storage.decl 22 in 0 : !byte design
    obelisk_sim.storage.decl 23 in 0 : !byte design
    obelisk_sim.storage.decl 24 in 0 : !byte design
    obelisk_sim.storage.decl 25 in 0 : !byte design
    obelisk_sim.storage.decl 26 in 0 : !byte design
    obelisk_sim.storage.decl 27 in 0 : !byte design
    obelisk_sim.storage.decl 28 in 0 : !byte design
    obelisk_sim.storage.decl 29 in 0 : !byte design
    obelisk_sim.storage.decl 30 in 0 : !byte design
    obelisk_sim.storage.decl 31 in 0 : !byte design
    obelisk_sim.storage.decl 32 in 0 : !byte design
    obelisk_sim.storage.decl 33 in 0 : !byte design
    obelisk_sim.storage.decl 34 in 0 : !byte design
    obelisk_sim.storage.decl 35 in 0 : !byte design
    obelisk_sim.storage.decl 36 in 0 : !byte design
    obelisk_sim.storage.decl 37 in 0 : !byte design
    obelisk_sim.storage.decl 38 in 0 : !byte design
    obelisk_sim.storage.decl 39 in 0 : !byte design
    obelisk_sim.storage.decl 40 in 0 : !byte design
    obelisk_sim.storage.decl 41 in 0 : !byte design
    obelisk_sim.storage.decl 42 in 0 : !byte design
    obelisk_sim.storage.decl 43 in 0 : !byte design
    obelisk_sim.storage.decl 44 in 0 : !byte design
    obelisk_sim.storage.decl 45 in 0 : !byte design
    obelisk_sim.storage.decl 46 in 0 : !byte design
    obelisk_sim.storage.decl 47 in 0 : !byte design
    obelisk_sim.storage.decl 48 in 0 : !byte design
    obelisk_sim.storage.decl 49 in 0 : !byte design
    obelisk_sim.storage.decl 50 in 0 : !byte design
    obelisk_sim.storage.decl 51 in 0 : !byte design
    obelisk_sim.storage.decl 52 in 0 : !byte design
    obelisk_sim.storage.decl 53 in 0 : !byte design
    obelisk_sim.storage.decl 54 in 0 : !byte design
    obelisk_sim.storage.decl 55 in 0 : !byte design
    obelisk_sim.storage.decl 56 in 0 : !byte design
    obelisk_sim.storage.decl 57 in 0 : !byte design
    obelisk_sim.storage.decl 58 in 0 : !byte design
    obelisk_sim.storage.decl 59 in 0 : !byte design
    obelisk_sim.storage.decl 60 in 0 : !byte design
    obelisk_sim.storage.decl 61 in 0 : !byte design
    obelisk_sim.storage.decl 62 in 0 : !byte design
    obelisk_sim.storage.decl 63 in 0 : !byte design
    obelisk_sim.storage.decl 64 in 0 : !byte design
    obelisk_sim.storage.decl 65 in 0 : !byte design
    obelisk_sim.storage.decl 66 in 0 : !byte design
    obelisk_sim.storage.decl 67 in 0 : !byte design
    obelisk_sim.storage.decl 68 in 0 : !byte design
    obelisk_sim.storage.decl 69 in 0 : !byte design
    obelisk_sim.storage.decl 70 in 0 : !byte design
    obelisk_sim.storage.decl 71 in 0 : !byte design
    obelisk_sim.storage.decl 72 in 0 : !byte design
    obelisk_sim.storage.decl 73 in 0 : !byte design
    obelisk_sim.storage.decl 74 in 0 : !byte design
    obelisk_sim.storage.decl 75 in 0 : !byte design
    obelisk_sim.storage.decl 76 in 0 : !byte design
    obelisk_sim.storage.decl 77 in 0 : !byte design
    obelisk_sim.storage.decl 78 in 0 : !byte design
    obelisk_sim.storage.decl 79 in 0 : !byte design
    obelisk_sim.storage.decl 80 in 0 : !byte design
    obelisk_sim.storage.decl 81 in 0 : !byte design
    obelisk_sim.storage.decl 82 in 0 : !byte design
    obelisk_sim.storage.decl 83 in 0 : !byte design
    obelisk_sim.storage.decl 84 in 0 : !byte design
    obelisk_sim.storage.decl 85 in 0 : !byte design
    obelisk_sim.storage.decl 86 in 0 : !byte design
    obelisk_sim.storage.decl 87 in 0 : !byte design
    obelisk_sim.storage.decl 88 in 0 : !byte design
    obelisk_sim.storage.decl 89 in 0 : !byte design
    obelisk_sim.storage.decl 90 in 0 : !byte design
    obelisk_sim.storage.decl 91 in 0 : !byte design
    obelisk_sim.storage.decl 92 in 0 : !byte design
    obelisk_sim.storage.decl 93 in 0 : !byte design
    obelisk_sim.storage.decl 94 in 0 : !byte design
    obelisk_sim.storage.decl 95 in 0 : !byte design
    obelisk_sim.storage.decl 96 in 0 : !byte design
    obelisk_sim.storage.decl 97 in 0 : !byte design
    obelisk_sim.storage.decl 98 in 0 : !byte design
    obelisk_sim.storage.decl 99 in 0 : !byte design
    obelisk_sim.storage.decl 100 in 0 : !byte design
    obelisk_sim.storage.decl 101 in 0 : !byte design
    obelisk_sim.storage.decl 102 in 0 : !byte design
    obelisk_sim.storage.decl 103 in 0 : !byte design
    obelisk_sim.storage.decl 104 in 0 : !byte design
    obelisk_sim.storage.decl 105 in 0 : !byte design
    obelisk_sim.storage.decl 106 in 0 : !byte design
    obelisk_sim.storage.decl 107 in 0 : !byte design
    obelisk_sim.storage.decl 108 in 0 : !byte design
    obelisk_sim.storage.decl 109 in 0 : !byte design
    obelisk_sim.storage.decl 110 in 0 : !byte design
    obelisk_sim.storage.decl 111 in 0 : !byte design
    obelisk_sim.storage.decl 112 in 0 : !byte design
    obelisk_sim.storage.decl 113 in 0 : !byte design
    obelisk_sim.storage.decl 114 in 0 : !byte design
    obelisk_sim.storage.decl 115 in 0 : !byte design
    obelisk_sim.storage.decl 116 in 0 : !byte design
    obelisk_sim.storage.decl 117 in 0 : !byte design
    obelisk_sim.storage.decl 118 in 0 : !byte design
    obelisk_sim.storage.decl 119 in 0 : !byte design
    obelisk_sim.storage.decl 120 in 0 : !byte design
    obelisk_sim.storage.decl 121 in 0 : !byte design
    obelisk_sim.storage.decl 122 in 0 : !byte design
    obelisk_sim.storage.decl 123 in 0 : !byte design
    obelisk_sim.storage.decl 124 in 0 : !byte design
    obelisk_sim.storage.decl 125 in 0 : !byte design
    obelisk_sim.storage.decl 126 in 0 : !byte design
    obelisk_sim.storage.decl 127 in 0 : !byte design
    obelisk_sim.storage.decl 128 in 0 : !byte design
    obelisk_sim.storage.decl 129 in 0 : !byte design
    obelisk_sim.storage.decl 130 in 0 : !byte design
    obelisk_sim.storage.decl 131 in 0 : !byte design
    obelisk_sim.storage.decl 132 in 0 : !byte design
    obelisk_sim.storage.decl 133 in 0 : !byte design
    obelisk_sim.storage.decl 134 in 0 : !byte design
    obelisk_sim.storage.decl 135 in 0 : !byte design
    obelisk_sim.storage.decl 136 in 0 : !byte design
    obelisk_sim.storage.decl 137 in 0 : !byte design
    obelisk_sim.storage.decl 138 in 0 : !byte design
    obelisk_sim.storage.decl 139 in 0 : !byte design
    obelisk_sim.storage.decl 140 in 0 : !byte design
    obelisk_sim.storage.decl 141 in 0 : !byte design
    obelisk_sim.storage.decl 142 in 0 : !byte design
    obelisk_sim.storage.decl 143 in 0 : !byte design
    obelisk_sim.storage.decl 144 in 0 : !byte design
    obelisk_sim.storage.decl 145 in 0 : !byte design
    obelisk_sim.storage.decl 146 in 0 : !byte design
    obelisk_sim.storage.decl 147 in 0 : !byte design
    obelisk_sim.storage.decl 148 in 0 : !byte design
    obelisk_sim.storage.decl 149 in 0 : !byte design
    obelisk_sim.storage.decl 150 in 0 : !byte design
    obelisk_sim.storage.decl 151 in 0 : !byte design
    obelisk_sim.storage.decl 152 in 0 : !byte design
    obelisk_sim.storage.decl 153 in 0 : !byte design
    obelisk_sim.storage.decl 154 in 0 : !byte design
    obelisk_sim.storage.decl 155 in 0 : !byte design
    obelisk_sim.storage.decl 156 in 0 : !byte design
    obelisk_sim.storage.decl 157 in 0 : !byte design
    obelisk_sim.storage.decl 158 in 0 : !byte design
    obelisk_sim.storage.decl 159 in 0 : !byte design
    obelisk_sim.storage.decl 160 in 0 : !byte design
    obelisk_sim.storage.decl 161 in 0 : !byte design
    obelisk_sim.storage.decl 162 in 0 : !byte design
    obelisk_sim.storage.decl 163 in 0 : !byte design
    obelisk_sim.storage.decl 164 in 0 : !byte design
    obelisk_sim.storage.decl 165 in 0 : !byte design
    obelisk_sim.storage.decl 166 in 0 : !byte design
    obelisk_sim.storage.decl 167 in 0 : !byte design
    obelisk_sim.storage.decl 168 in 0 : !byte design
    obelisk_sim.storage.decl 169 in 0 : !byte design
    obelisk_sim.storage.decl 170 in 0 : !byte design
    obelisk_sim.storage.decl 171 in 0 : !byte design
    obelisk_sim.storage.decl 172 in 0 : !byte design
    obelisk_sim.storage.decl 173 in 0 : !byte design
    obelisk_sim.storage.decl 174 in 0 : !byte design
    obelisk_sim.storage.decl 175 in 0 : !byte design
    obelisk_sim.storage.decl 176 in 0 : !byte design
    obelisk_sim.storage.decl 177 in 0 : !byte design
    obelisk_sim.storage.decl 178 in 0 : !byte design
    obelisk_sim.storage.decl 179 in 0 : !byte design
    obelisk_sim.storage.decl 180 in 0 : !byte design
    obelisk_sim.storage.decl 181 in 0 : !byte design
    obelisk_sim.storage.decl 182 in 0 : !byte design
    obelisk_sim.storage.decl 183 in 0 : !byte design
    obelisk_sim.storage.decl 184 in 0 : !byte design
    obelisk_sim.storage.decl 185 in 0 : !byte design
    obelisk_sim.storage.decl 186 in 0 : !byte design
    obelisk_sim.storage.decl 187 in 0 : !byte design
    obelisk_sim.storage.decl 188 in 0 : !byte design
    obelisk_sim.storage.decl 189 in 0 : !byte design
    obelisk_sim.storage.decl 190 in 0 : !byte design
    obelisk_sim.storage.decl 191 in 0 : !byte design
    obelisk_sim.storage.decl 192 in 0 : !byte design
    obelisk_sim.storage.decl 193 in 0 : !byte design
    obelisk_sim.storage.decl 194 in 0 : !byte design
    obelisk_sim.storage.decl 195 in 0 : !byte design
    obelisk_sim.storage.decl 196 in 0 : !byte design
    obelisk_sim.storage.decl 197 in 0 : !byte design
    obelisk_sim.storage.decl 198 in 0 : !byte design
    obelisk_sim.storage.decl 199 in 0 : !byte design
    obelisk_sim.storage.decl 200 in 0 : !byte design
    obelisk_sim.storage.decl 201 in 0 : !byte design
    obelisk_sim.storage.decl 202 in 0 : !byte design
    obelisk_sim.storage.decl 203 in 0 : !byte design
    obelisk_sim.storage.decl 204 in 0 : !byte design
    obelisk_sim.storage.decl 205 in 0 : !byte design
    obelisk_sim.storage.decl 206 in 0 : !byte design
    obelisk_sim.storage.decl 207 in 0 : !byte design
    obelisk_sim.storage.decl 208 in 0 : !byte design
    obelisk_sim.storage.decl 209 in 0 : !byte design
    obelisk_sim.storage.decl 210 in 0 : !byte design
    obelisk_sim.storage.decl 211 in 0 : !byte design
    obelisk_sim.storage.decl 212 in 0 : !byte design
    obelisk_sim.storage.decl 213 in 0 : !byte design
    obelisk_sim.storage.decl 214 in 0 : !byte design
    obelisk_sim.storage.decl 215 in 0 : !byte design
    obelisk_sim.storage.decl 216 in 0 : !byte design
    obelisk_sim.storage.decl 217 in 0 : !byte design
    obelisk_sim.storage.decl 218 in 0 : !byte design
    obelisk_sim.storage.decl 219 in 0 : !byte design
    obelisk_sim.storage.decl 220 in 0 : !byte design
    obelisk_sim.storage.decl 221 in 0 : !byte design
    obelisk_sim.storage.decl 222 in 0 : !byte design
    obelisk_sim.storage.decl 223 in 0 : !byte design
    obelisk_sim.storage.decl 224 in 0 : !byte design
    obelisk_sim.storage.decl 225 in 0 : !byte design
    obelisk_sim.storage.decl 226 in 0 : !byte design
    obelisk_sim.storage.decl 227 in 0 : !byte design
    obelisk_sim.storage.decl 228 in 0 : !byte design
    obelisk_sim.storage.decl 229 in 0 : !byte design
    obelisk_sim.storage.decl 230 in 0 : !byte design
    obelisk_sim.storage.decl 231 in 0 : !byte design
    obelisk_sim.storage.decl 232 in 0 : !byte design
    obelisk_sim.storage.decl 233 in 0 : !byte design
    obelisk_sim.storage.decl 234 in 0 : !byte design
    obelisk_sim.storage.decl 235 in 0 : !byte design
    obelisk_sim.storage.decl 236 in 0 : !byte design
    obelisk_sim.storage.decl 237 in 0 : !byte design
    obelisk_sim.storage.decl 238 in 0 : !byte design
    obelisk_sim.storage.decl 239 in 0 : !byte design
    obelisk_sim.storage.decl 240 in 0 : !byte design
    obelisk_sim.storage.decl 241 in 0 : !byte design
    obelisk_sim.storage.decl 242 in 0 : !byte design
    obelisk_sim.storage.decl 243 in 0 : !byte design
    obelisk_sim.storage.decl 244 in 0 : !byte design
    obelisk_sim.storage.decl 245 in 0 : !byte design
    obelisk_sim.storage.decl 246 in 0 : !byte design
    obelisk_sim.storage.decl 247 in 0 : !byte design
    obelisk_sim.storage.decl 248 in 0 : !byte design
    obelisk_sim.storage.decl 249 in 0 : !byte design
    obelisk_sim.storage.decl 250 in 0 : !byte design
    obelisk_sim.storage.decl 251 in 0 : !byte design
    obelisk_sim.storage.decl 252 in 0 : !byte design
    obelisk_sim.storage.decl 253 in 0 : !byte design
    obelisk_sim.storage.decl 254 in 0 : !byte design
    obelisk_sim.storage.decl 255 in 0 : !obelisk_sim.string design
    obelisk_sim.storage.decl 256 in 0 : !obelisk_sim.string design
    obelisk_sim.storage.decl 257 in 0 : !byte design
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %ref = obelisk_sim.context.storage %ctx[257] : !obelisk_sim.ref<!byte>
      %value = obelisk_sim.logic.constant 42 : i8, 0 : i8 : !byte
      obelisk_sim.ref.store %value to %ref : !byte, !obelisk_sim.ref<!byte>
      %read = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!byte> -> !byte
      %format = obelisk_sim.bytes.constant "registered %h"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%format, %read) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !byte
      obelisk_sim.return
    }
  }
}
