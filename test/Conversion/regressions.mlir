// RUN: obelisk-opt --convert-moore-to-obelisk %s \
// RUN:   | obelisk-opt | FileCheck %s

module {
  // A pre-existing target op must still have recursively converted types.
  %existing = obelisk.semantic.value constant()
      : () -> !moore.l4

  // Moore permits string packing into both two- and four-state integers.
  %twoState = moore.constant_string "a" : i8
  %fourState = moore.constant_string "a" : l8

  // Moore erases net identity from RefType; dynamic selections must recover it.
  %net = moore.net wire : <l8>
  %index = moore.constant 0 : i4
  %selected = moore.dyn_extract_ref %net from %index : <l8>, i4 -> <l4>

  // Generic structural selectors must preserve that same net identity.
  %structNet = moore.net wire : <!moore.struct<{tag: l4, payload: l12}>>
  %field = moore.struct_extract_ref %structNet, "tag"
      : !moore.ref<!moore.struct<{tag: l4, payload: l12}>>
      -> !moore.ref<l4>
  %fieldValue = moore.read %field : <l4>
}

// CHECK-LABEL: module {
// CHECK: %[[EXISTING:.*]] = obelisk.semantic.value constant()
// CHECK-SAME: -> !obelisk.logic<4>
// CHECK: arith.constant 97 : i8
// CHECK: obelisk.logic.constant 97 : i8, 0 : i8 : !obelisk.logic<8>
// CHECK: obelisk.net.dyn_extract
// CHECK: obelisk.semantic.value struct_extract_ref
// CHECK-SAME: -> !obelisk.net<!obelisk.logic<4>>
// CHECK: obelisk.net.read
// CHECK-NOT: moore.
