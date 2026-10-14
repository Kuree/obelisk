//===- DiscardableAttributes.h - Declared attributes ------------*- C++ -*-===//
//
// Typed access to discardable attributes that a dialect declares in ODS with
// `discardableAttrs`. Each declaration generates a `<Name>AttrHelper` class in
// the dialect; these templates take that class and use its name and payload
// type, so callers never spell an attribute name or payload kind by hand.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_DIALECT_DISCARDABLEATTRIBUTES_H
#define OBELISK_DIALECT_DISCARDABLEATTRIBUTES_H

#include "mlir/IR/Builders.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Operation.h"

#include <utility>

namespace obelisk::discardable {

/// Payload attribute class of a generated attribute helper.
template <typename Attr>
using ValueType =
    decltype(std::declval<Attr &>().getAttr(std::declval<mlir::Operation *>()));

template <typename Attr> constexpr llvm::StringLiteral name() {
  return Attr::getNameStr();
}

/// The attribute, or null when it is absent or has another payload kind.
template <typename Attr> ValueType<Attr> get(mlir::Operation *operation) {
  return operation->getAttrOfType<ValueType<Attr>>(Attr::getNameStr());
}

template <typename Attr> bool has(mlir::Operation *operation) {
  return static_cast<bool>(get<Attr>(operation));
}

template <typename Attr>
void set(mlir::Operation *operation, ValueType<Attr> value) {
  operation->setAttr(Attr::getNameStr(), value);
}

/// Mark `operation` with a unit attribute.
template <typename Attr> void mark(mlir::Operation *operation) {
  static_assert(std::is_same_v<ValueType<Attr>, mlir::UnitAttr>,
                "only unit attributes are marks");
  operation->setAttr(Attr::getNameStr(),
                     mlir::UnitAttr::get(operation->getContext()));
}

/// Remove the attribute and return its previous value, if any.
template <typename Attr> mlir::Attribute remove(mlir::Operation *operation) {
  return operation->removeAttr(Attr::getNameStr());
}

template <typename Attr> mlir::NamedAttribute named(ValueType<Attr> value) {
  return {mlir::StringAttr::get(value.getContext(), Attr::getNameStr()), value};
}

/// Verify that `attribute`, whose name carries the dialect prefix, is one of
/// `Attrs` and has the declared payload kind.
template <typename... Attrs>
mlir::LogicalResult verify(mlir::Operation *operation,
                           mlir::NamedAttribute attribute) {
  llvm::StringRef attributeName = attribute.getName().strref();
  bool known = false;
  bool valid = false;
  (void)((attributeName == Attrs::getNameStr()
              ? (known = true,
                 valid = mlir::isa<ValueType<Attrs>>(attribute.getValue()),
                 true)
              : false) ||
         ...);
  if (!known)
    return operation->emitOpError("has undeclared attribute '")
           << attributeName << "'";
  if (!valid)
    return operation->emitOpError("attribute '")
           << attributeName << "' has the wrong payload kind";
  return mlir::success();
}

} // namespace obelisk::discardable

#endif // OBELISK_DIALECT_DISCARDABLEATTRIBUTES_H
