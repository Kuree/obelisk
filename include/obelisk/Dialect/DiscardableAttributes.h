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

#include "llvm/ADT/StringMap.h"

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

/// Whether an attribute value has a declaration's payload kind.
using PayloadCheck = bool (*)(mlir::Attribute);

/// Payload checks of the declarations `Attrs`, keyed by attribute name.
template <typename... Attrs>
const llvm::StringMap<PayloadCheck> &declarations() {
  static const llvm::StringMap<PayloadCheck> table = [] {
    llvm::StringMap<PayloadCheck> result;
    (result.try_emplace(
         Attrs::getNameStr(),
         +[](mlir::Attribute value) {
           return mlir::isa<ValueType<Attrs>>(value);
         }),
     ...);
    return result;
  }();
  return table;
}

/// Verify that `attribute`, whose name carries the dialect prefix, is
/// declared in `table` and has the declared payload kind.
inline mlir::LogicalResult verify(const llvm::StringMap<PayloadCheck> &table,
                                  mlir::Operation *operation,
                                  mlir::NamedAttribute attribute) {
  llvm::StringRef attributeName = attribute.getName().strref();
  auto found = table.find(attributeName);
  if (found == table.end())
    return operation->emitOpError("has undeclared attribute '")
           << attributeName << "'";
  if (!found->second(attribute.getValue()))
    return operation->emitOpError("attribute '")
           << attributeName << "' has the wrong payload kind";
  return mlir::success();
}

template <typename... Attrs>
mlir::LogicalResult verify(mlir::Operation *operation,
                           mlir::NamedAttribute attribute) {
  return verify(declarations<Attrs...>(), operation, attribute);
}

} // namespace obelisk::discardable

#endif // OBELISK_DIALECT_DISCARDABLEATTRIBUTES_H
