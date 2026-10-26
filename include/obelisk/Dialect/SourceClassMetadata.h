//===- SourceClassMetadata.h - Unspecialized class declarations -*- C++ -*-===//

#ifndef OBELISK_DIALECT_SOURCECLASSMETADATA_H
#define OBELISK_DIALECT_SOURCECLASSMETADATA_H

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include <optional>
#include <type_traits>

namespace obelisk::sourceclass {

template <typename GenericClass, typename SourceClass, typename... Members>
mlir::LogicalResult verifyDefinition(SourceClass op) {
  if (!mlir::isa_and_nonnull<GenericClass, SourceClass>(op->getParentOp()))
    return op.emitOpError("source class requires a lexical class owner");
  if (op.getSemanticTypeAttr())
    return op.emitOpError("source class cannot carry an effective type");
  if (!op.getName() || op.getName()->empty() || op.getName()->contains('\0'))
    return op.emitOpError("source class requires a nonempty declaration name");
  if (!op.getBody().empty())
    for (mlir::Operation &child : op.getBody().front())
      if (!mlir::isa<SourceClass, Members...>(child))
        return op.emitOpError("source class contains a non-source declaration");
  for (mlir::Operation &sibling : op->getBlock()->getOperations())
    if (&sibling != op.getOperation() &&
        mlir::isa<SourceClass, Members...>(sibling) &&
        sibling.getAttr("name") == op->getAttr("name"))
      return op.emitOpError("duplicates a lexical class declaration name");
  return mlir::success();
}

// Frontend source references use absolute symbol paths from the enclosing
// module, including across isolated semantic scopes.
inline mlir::Operation *lookupDefinition(mlir::Operation *op,
                                         mlir::SymbolRefAttr reference,
                                         mlir::SymbolTableCollection &symbols) {
  if (!reference)
    return nullptr;
  auto module = op->getParentOfType<mlir::ModuleOp>();
  return module ? symbols.lookupSymbolIn(module, reference) : nullptr;
}

template <typename SourceClass, typename GenericClass, typename ClassType,
          typename Declaration>
mlir::LogicalResult
verifyLexicalDefinition(Declaration op, mlir::SymbolTableCollection &symbols) {
  auto reference = op.getLexicalDefinitionAttr();
  if (!reference)
    return mlir::success();
  auto source = mlir::dyn_cast_or_null<SourceClass>(
      lookupDefinition(op, reference, symbols));
  if (!source)
    return op.emitOpError("lexical definition must name a source class");
  if (source.getNameAttr() != op.getNameAttr() ||
      source.getIsInterface() != op.getIsInterface())
    return op.emitOpError("lexical definition disagrees with class identity");
  if constexpr (std::is_same_v<Declaration, GenericClass>) {
    if (!source.getIsParameterized())
      return op.emitOpError(
          "generic class requires a parameterized definition");
    if (!op.getBody().empty())
      for (mlir::Operation &child : op.getBody().front())
        if (!mlir::isa<ClassType>(child))
          return op.emitOpError(
              "realized generic class contains a duplicate source inventory");
  } else {
    if (source.getIsAbstract() != op.getIsAbstract() ||
        source.getIsFinal() != op.getIsFinal())
      return op.emitOpError(
          "lexical definition disagrees with class modifiers");
    if (auto generic =
            mlir::dyn_cast_or_null<GenericClass>(op->getParentOp())) {
      if (generic.getLexicalDefinitionAttr() != reference)
        return op.emitOpError(
            "specialization must retain its generic definition");
      return mlir::success();
    }
  }
  auto parent = mlir::dyn_cast_or_null<ClassType>(op->getParentOp());
  if (!parent)
    return op.emitOpError("nested class requires an enclosing class type");
  mlir::Operation *owner = nullptr;
  if (auto parentDefinition = parent.getLexicalDefinitionAttr())
    owner = lookupDefinition(parent, parentDefinition, symbols);
  else if (auto generic =
               mlir::dyn_cast_or_null<GenericClass>(parent->getParentOp()))
    owner = generic;
  if (owner != source->getParentOp())
    return op.emitOpError("lexical definition has a different enclosing class");
  return mlir::success();
}

template <typename GenericClass, typename SourceClass, typename... Declarations,
          typename Constraint>
mlir::LogicalResult verifyConstraint(Constraint op) {
  if (!mlir::isa_and_nonnull<GenericClass, SourceClass>(op->getParentOp()))
    return op.emitOpError("source constraint requires a lexical class owner");
  if (op.getSemanticTypeAttr())
    return op.emitOpError("source constraint cannot carry an effective type");
  if (!op.getName() || op.getName()->empty() || op.getName()->contains('\0'))
    return op.emitOpError(
        "source constraint requires a nonempty declaration name");
  if (!op.getBody().empty() && !op.getBody().front().empty())
    return op.emitOpError("source constraint cannot contain a bound body");
  if (op.getIsPureAttr() &&
      (!op.getIsPrototypeAttr() || op.getHasExternalDefinition()))
    return op.emitOpError("pure constraint requires a bodyless prototype");
  if (op.getHasExternalDefinition() && !op.getIsPrototypeAttr())
    return op.emitOpError(
        "external constraint definition requires a prototype");
  for (mlir::Operation &sibling : op->getBlock()->getOperations())
    if (&sibling != op.getOperation() &&
        mlir::isa<Constraint, SourceClass, Declarations...>(sibling) &&
        sibling.getAttr("name") == op->getAttr("name"))
      return op.emitOpError("duplicates a lexical class declaration name");
  return mlir::success();
}

template <typename GenericClass, typename SourceClass, typename ClassType,
          typename Actual, typename Extends>
mlir::LogicalResult verifyExtends(Extends op) {
  if (!mlir::isa_and_nonnull<GenericClass, SourceClass, ClassType>(
          op->getParentOp()))
    return op.emitOpError("source extends requires a lexical class owner");
  auto *owner = op->getParentOp();
  if (auto kind = owner->template getAttrOfType<mlir::BoolAttr>("is_interface");
      kind && kind.getValue())
    return op.emitOpError("normal class extends requires a normal class owner");
  if (auto type = mlir::dyn_cast<ClassType>(owner)) {
    if (type.getLexicalDefinitionAttr() ||
        mlir::isa_and_nonnull<GenericClass>(type->getParentOp()))
      return op.emitOpError(
          "source extends requires the original lexical owner");
  }
  if (auto generic = mlir::dyn_cast<GenericClass>(owner))
    if (generic.getLexicalDefinitionAttr())
      return op.emitOpError(
          "source extends requires the original lexical owner");
  if (op->hasAttr("semantic_type"))
    return op.emitOpError("source extends cannot carry an effective type");
  if (!op.getBody().empty()) {
    llvm::SmallDenseSet<mlir::Attribute, 8> actuals;
    std::optional<bool> named;
    for (mlir::Operation &child : op.getBody().front()) {
      auto actual = mlir::dyn_cast<Actual>(child);
      if (!actual || !op.getBaseDefinitionAttr() ||
          op.getParameterArgumentsAttr())
        return op.emitOpError(
            "source extends children require exact parameter connections");
      if (!actuals.insert(actual.getParameterAttr()).second)
        return op.emitOpError("duplicates a class parameter connection");
      bool isNamed = bool(actual.getIsNamedAttr());
      if (named && *named != isNamed)
        return op.emitOpError("cannot mix ordered and named class connections");
      named = isNamed;
    }
  }
  if (bool(op.getBaseDefinitionAttr()) == bool(op.getUnavailableBaseAttr()))
    return op.emitOpError(
        "source extends requires exactly one base declaration "
        "or unavailable source form");
  for (auto text : {op.getUnavailableBaseAttr(), op.getParameterArgumentsAttr(),
                    op.getConstructorArgumentsAttr()})
    if (text && (text.getValue().empty() || text.getValue().contains('\0')))
      return op.emitOpError(
          "unavailable source syntax must be nonempty without "
          "NUL bytes");
  for (mlir::Operation &sibling : op->getBlock()->getOperations())
    if (&sibling != op.getOperation() && mlir::isa<Extends>(sibling))
      return op.emitOpError("class has more than one source extends clause");
  return mlir::success();
}

template <typename GenericClass, typename SourceClass, typename ClassType,
          typename Extends>
mlir::LogicalResult
verifyExtendsReference(Extends op, mlir::SymbolTableCollection &symbols) {
  if (!op.getBaseDefinitionAttr())
    return mlir::success();
  mlir::Operation *target =
      lookupDefinition(op, op.getBaseDefinitionAttr(), symbols);
  if (!mlir::isa_and_nonnull<GenericClass, SourceClass, ClassType>(target))
    return op.emitOpError("base definition must name an exact source class");
  if (auto type = mlir::dyn_cast<ClassType>(target)) {
    if (type.getLexicalDefinitionAttr() ||
        mlir::isa_and_nonnull<GenericClass>(type->getParentOp()))
      return op.emitOpError(
          "base definition must name the lexical declaration, "
          "not a specialization");
  }
  if (auto generic = mlir::dyn_cast<GenericClass>(target))
    if (generic.getLexicalDefinitionAttr())
      return op.emitOpError("base definition must name the original lexical "
                            "declaration");
  if (target->getAttrOfType<mlir::BoolAttr>("is_interface").getValue())
    return op.emitOpError(
        "normal class extends cannot name an interface class");
  llvm::SmallPtrSet<mlir::Operation *, 8> ancestors;
  ancestors.insert(op->getParentOp());
  while (target) {
    if (!ancestors.insert(target).second)
      return op.emitOpError("source class inheritance contains a cycle");
    Extends next;
    if (!target->getRegion(0).empty())
      for (mlir::Operation &child : target->getRegion(0).front())
        if (auto clause = mlir::dyn_cast<Extends>(child)) {
          next = clause;
          break;
        }
    target = next && next.getBaseDefinitionAttr()
                 ? lookupDefinition(next, next.getBaseDefinitionAttr(), symbols)
                 : nullptr;
  }
  return mlir::success();
}

inline bool isLocalParameter(mlir::Operation *parameter) {
  auto local = parameter->getAttr("is_local_param");
  return mlir::isa_and_nonnull<mlir::UnitAttr>(local) ||
         (mlir::isa_and_nonnull<mlir::BoolAttr>(local) &&
          mlir::cast<mlir::BoolAttr>(local).getValue());
}

template <typename Extends, typename TypeDefault, typename TypeReference,
          typename Unary, typename Binary, typename Conditional,
          typename ValueReference, typename Literal, typename Actual>
mlir::LogicalResult verifyParameterActual(Actual op) {
  if (!mlir::isa_and_nonnull<Extends>(op->getParentOp()) ||
      op->hasAttr("semantic_type"))
    return op.emitOpError(
        "source connection requires an unbound extends owner");
  size_t count =
      op.getBody().empty() ? 0 : op.getBody().front().getOperations().size();
  bool isDefault = bool(op.getUseDefaultAttr());
  auto unavailable = op.getUnavailableRhsAttr();
  if (isDefault && (!op.getIsNamedAttr() || unavailable))
    return op.emitOpError("explicit default connection requires named syntax "
                          "and no unavailable RHS");
  if (unavailable &&
      (unavailable.getValue().empty() || unavailable.getValue().contains('\0')))
    return op.emitOpError("unavailable RHS must retain nonempty source syntax");
  if (isDefault || unavailable) {
    if (count != 0)
      return op.emitOpError(
          "default or unavailable connection cannot contain an expression");
    return mlir::success();
  }
  if (count != 1 ||
      !mlir::isa<TypeDefault, TypeReference, Unary, Binary, Conditional,
                 ValueReference, Literal>(op.getBody().front().front()))
    return op.emitOpError(
        "source connection requires one typed source expression");
  return mlir::success();
}

template <typename Extends, typename SourceValue, typename Value,
          typename SourceType, typename Type, typename TypeDefault,
          typename TypeReference, typename Actual>
mlir::LogicalResult
verifyParameterActualReference(Actual op,
                               mlir::SymbolTableCollection &symbols) {
  auto clause = mlir::dyn_cast<Extends>(op->getParentOp());
  auto *target = lookupDefinition(op, op.getParameterAttr(), symbols);
  auto *base =
      clause && clause.getBaseDefinitionAttr()
          ? lookupDefinition(clause, clause.getBaseDefinitionAttr(), symbols)
          : nullptr;
  if (!mlir::isa_and_nonnull<SourceValue, Value, SourceType, Type>(target) ||
      target->getParentOp() != base || isLocalParameter(target))
    return op.emitOpError("connection must name an overridable parameter of "
                          "the exact base class");
  if (!op.getIsNamedAttr()) {
    size_t ordinal = 0;
    for (mlir::Operation &sibling : clause.getBody().front()) {
      if (&sibling == op.getOperation())
        break;
      ordinal += mlir::isa<Actual>(sibling);
    }
    mlir::Operation *expected = nullptr;
    for (mlir::Operation &member : base->getRegion(0).front()) {
      if (!mlir::isa<SourceValue, Value, SourceType, Type>(member) ||
          isLocalParameter(&member))
        continue;
      if (ordinal-- == 0) {
        expected = &member;
        break;
      }
    }
    if (expected != target)
      return op.emitOpError(
          "ordered connection must follow base parameter declaration order");
  }
  if (!op.getUseDefaultAttr() && !op.getUnavailableRhsAttr()) {
    bool isType = mlir::isa<SourceType, Type>(target);
    bool typeExpression =
        mlir::isa<TypeDefault, TypeReference>(op.getBody().front().front());
    if (isType != typeExpression)
      return op.emitOpError(
          "connection expression must agree with the formal parameter kind");
  }
  return mlir::success();
}

template <typename GenericClass, typename SourceClass, typename ValueParameter,
          typename TypeParameter, typename Method>
mlir::LogicalResult verifyMethod(Method op, bool isTask) {
  if (!mlir::isa_and_nonnull<GenericClass, SourceClass>(op->getParentOp()))
    return op.emitOpError("source method requires a lexical class owner");
  if (op.getSemanticTypeAttr())
    return op.emitOpError("source method cannot carry an effective signature");
  if (!op.getName() || op.getName()->empty())
    return op.emitOpError("source method requires a declaration name");
  if (!op.getBody().empty() && !op.getBody().front().empty())
    return op.emitOpError("source method cannot contain an elaborated body");
  if (op.getIsConstructorAttr() &&
      (isTask || *op.getName() != "new" || op.getIsStaticAttr() ||
       op.getIsPureAttr() || op.getIsDeclaredVirtualAttr()))
    return op.emitOpError(
        "constructor requires a nonstatic, nonvirtual function named new");
  if (op.getIsStaticAttr() &&
      (op.getIsDeclaredVirtualAttr() || op.getIsPureAttr()))
    return op.emitOpError("static method cannot be virtual or pure");
  if (op.getIsPureAttr() && !op.getIsDeclaredVirtualAttr())
    return op.emitOpError("pure method must be declared virtual");
  if (auto virtuality = op.getSourceVirtualityAttr()) {
    if ((virtuality.getValue() &&
         (op.getIsStaticAttr() || op.getIsConstructorAttr())) ||
        (!virtuality.getValue() &&
         (op.getIsDeclaredVirtualAttr() || op.getIsPureAttr())))
      return op.emitOpError(
          "source virtuality conflicts with declaration modifiers");
  }
  for (mlir::Operation &sibling : op->getParentOp()->getRegion(0).front())
    if (&sibling != op.getOperation() &&
        mlir::isa<Method, ValueParameter, TypeParameter>(sibling) &&
        sibling.getAttr("name") == op->getAttr("name"))
      return op.emitOpError("duplicates a lexical class declaration name");
  return mlir::success();
}

template <typename GenericClass, typename SourceClass, typename ConcreteType,
          typename ReferenceType, typename UnavailableType,
          typename... Declarations, typename Declaration>
mlir::LogicalResult verifyDataDeclaration(Declaration op) {
  if (!mlir::isa_and_nonnull<GenericClass, SourceClass>(op->getParentOp()))
    return op.emitOpError("source declaration requires a lexical class owner");
  if (op.getSemanticTypeAttr() || op->hasAttr("constant_value"))
    return op.emitOpError(
        "source declaration cannot carry an effective payload");
  if (!op.getName() || op.getName()->empty())
    return op.emitOpError("source declaration requires a declaration name");
  if (op.getBody().empty() || !llvm::hasSingleElement(op.getBody().front()) ||
      !mlir::isa<ConcreteType, ReferenceType, UnavailableType>(
          op.getBody().front().front()))
    return op.emitOpError(
        "source declaration requires exactly one declared-type node");
  for (mlir::Operation &sibling : op->getParentOp()->getRegion(0).front())
    if (&sibling != op.getOperation() && mlir::isa<Declarations...>(sibling) &&
        sibling.getAttr("name") == op->getAttr("name"))
      return op.emitOpError("duplicates a lexical class declaration name");
  return mlir::success();
}

template <typename Property, typename Alias, typename TypeNode>
mlir::LogicalResult verifyDeclaredTypeOwner(TypeNode op, bool concrete) {
  if (!mlir::isa_and_nonnull<Property, Alias>(op->getParentOp()))
    return op.emitOpError(
        "declared type requires a source property or typedef owner");
  if (!op.getBody().empty() && !op.getBody().front().empty())
    return op.emitOpError("declared type cannot contain elaborated nodes");
  if (!concrete && op->hasAttr("semantic_type"))
    return op.emitOpError(
        "unbound declared type cannot carry an effective type");
  return mlir::success();
}

template <typename Parameter, typename Alias, typename Reference>
mlir::LogicalResult
verifyDeclaredTypeReference(Reference op,
                            mlir::SymbolTableCollection &symbols) {
  auto *owner = op->getParentOp();
  auto *target = symbols.lookupNearestSymbolFrom(op, op.getDeclarationAttr());
  if (!mlir::isa_and_nonnull<Parameter, Alias>(target) ||
      target->getParentOp() != owner->getParentOp())
    return op.emitOpError("declared type reference must name a lexical type in "
                          "the same generic class");
  if (target == owner || !target->isBeforeInBlock(owner))
    return op.emitOpError(
        "declared type reference must name a preceding lexical type");
  return mlir::success();
}

} // namespace obelisk::sourceclass

#endif
