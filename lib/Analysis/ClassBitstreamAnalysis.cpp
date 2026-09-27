//===- ClassBitstreamAnalysis.cpp - Object bit-stream schemas -----------===//

#include "obelisk/Analysis/ClassBitstreamAnalysis.h"

#include "obelisk/Dialect/Simulation/SimulationMetadata.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>

using namespace mlir;

namespace obelisk::analysis {
namespace {

/// Validate the non-class portion of a 6.24.3 bit-stream type and collect the
/// statically named class handles without recursively entering declarations.
bool collectNestedClasses(Type root,
                          SmallVectorImpl<sim::ClassHandleType> &out) {
  SmallVector<Type, 16> pending{root};
  llvm::SmallPtrSet<const void *, 16> visited;
  while (!pending.empty()) {
    Type type = pending.pop_back_val();
    if (!visited.insert(type.getAsOpaquePointer()).second)
      continue;
    if (auto handle = dyn_cast<sim::ClassHandleType>(type)) {
      out.push_back(handle);
      continue;
    }
    if (isa<IntegerType, sim::LogicType, sim::StringType>(type))
      continue;
    if (sim::getPackedWidth(type))
      continue;
    if (auto array = dyn_cast<sim::DynamicArrayType>(type)) {
      pending.push_back(array.getElementType());
      continue;
    }
    if (auto queue = dyn_cast<sim::QueueType>(type)) {
      pending.push_back(queue.getElementType());
      continue;
    }
    if (auto associative = dyn_cast<sim::AssocArrayType>(type)) {
      if (associative.getWildcardIndex())
        return false;
      pending.push_back(associative.getElementType());
      continue;
    }
    if (auto array = dyn_cast<sim::UnpackedArrayType>(type)) {
      if (sim::getAggregateNumElements(array) == 0)
        return false;
      pending.push_back(array.getElementType());
      continue;
    }
    if (isa<sim::UnpackedStructType>(type)) {
      unsigned count = sim::getAggregateNumElements(type);
      if (count == 0)
        return false;
      for (unsigned ordinal = 0; ordinal != count; ++ordinal)
        pending.push_back(sim::getAggregateElementType(type, ordinal));
      continue;
    }
    return false;
  }
  llvm::sort(out, [](sim::ClassHandleType lhs, sim::ClassHandleType rhs) {
    return lhs.getClassName().getRootReference().getValue() <
           rhs.getClassName().getRootReference().getValue();
  });
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return true;
}

} // namespace

FailureOr<ClassBitstreamAnalysis>
ClassBitstreamAnalysis::compute(sim::SimDesignOp design,
                                const llvm::DataLayout &dataLayout) {
  FailureOr<ManagedClassLayoutAnalysis> analyzed =
      ManagedClassLayoutAnalysis::compute(design, dataLayout);
  if (failed(analyzed))
    return failure();
  auto layouts =
      std::make_unique<ManagedClassLayoutAnalysis>(std::move(*analyzed));
  ClassBitstreamAnalysis result(ClassDispatchAnalysis(design),
                                std::move(layouts));
  llvm::DenseMap<Type, std::optional<SmallVector<sim::ClassHandleType>>>
      nestedTypeCache;
  llvm::DenseMap<uint64_t, sim::SimClassDeclOp> seenClassIDs;
  for (const ManagedClassLayoutAnalysis::Class &layout :
       result.layouts->classes) {
    sim::SimClassDeclOp declaration = layout.declaration;
    uint64_t classID = declaration.getId();
    if (classID == 0)
      return declaration.emitOpError(
                 "class bit-stream schema requires a nonzero class ID"),
             failure();
    if (auto [it, inserted] = seenClassIDs.try_emplace(classID, declaration);
        !inserted) {
      it->second.emitRemark("first class with this ID is here");
      return declaration.emitOpError("duplicate class ID in bit-stream schema"),
             failure();
    }
    FailureOr<SmallVector<const ManagedClassLayoutAnalysis::Field *>> fields =
        result.layouts->getBitstreamFields(layout);
    if (failed(fields))
      return declaration.emitOpError(
                 "cannot form its base-to-derived bit-stream field inventory"),
             failure();
    auto schema = std::make_unique<Schema>();
    schema->layout = &layout;
    schema->fields = std::move(*fields);
    for (const ManagedClassLayoutAnalysis::Field *field : schema->fields) {
      sim::SimClassFieldDeclOp fieldDeclaration = field->declaration;
      auto visibility =
          fieldDeclaration->getAttrOfType<sim::MemberVisibilityAttr>(
              sim::metadata::classBitstreamVisibility);
      if (!visibility) {
        schema->invalidField = fieldDeclaration;
        break;
      }
      schema->hasHiddenField |=
          visibility.getValue() != sim::MemberVisibility::Public;
      Type fieldType = fieldDeclaration.getType();
      auto cached = nestedTypeCache.find(fieldType);
      if (cached == nestedTypeCache.end()) {
        SmallVector<sim::ClassHandleType> nested;
        bool legal = collectNestedClasses(fieldType, nested);
        cached =
            nestedTypeCache
                .try_emplace(fieldType, legal ? std::optional(std::move(nested))
                                              : std::nullopt)
                .first;
      }
      if (!cached->second) {
        schema->invalidField = fieldDeclaration;
        break;
      }
      llvm::append_range(schema->nestedStaticTypes, *cached->second);
    }
    llvm::sort(schema->nestedStaticTypes,
               [](sim::ClassHandleType lhs, sim::ClassHandleType rhs) {
                 return lhs.getClassName().getRootReference().getValue() <
                        rhs.getClassName().getRootReference().getValue();
               });
    schema->nestedStaticTypes.erase(
        std::unique(schema->nestedStaticTypes.begin(),
                    schema->nestedStaticTypes.end()),
        schema->nestedStaticTypes.end());
    result.schemaIndices[classID] = result.schemas.size();
    result.schemas.push_back(std::move(schema));
  }
  return result;
}

const ClassBitstreamAnalysis::Schema *
ClassBitstreamAnalysis::lookup(uint64_t classID) const {
  auto found = schemaIndices.find(classID);
  return found == schemaIndices.end() ? nullptr : schemas[found->second].get();
}

FailureOr<const ClassBitstreamAnalysis::CastClosure *>
ClassBitstreamAnalysis::getCastClosure(sim::ClassHandleType source,
                                       bool allowHiddenRoot) const {
  unsigned visibilityIndex = allowHiddenRoot ? 1 : 0;
  auto cached = closureCache.find(source);
  if (cached != closureCache.end() && cached->second[visibilityIndex])
    return cached->second[visibilityIndex].get();

  sim::SimClassDeclOp staticClass = dispatch.lookup(source);
  if (!staticClass)
    return failure();

  auto validateStaticGraph = [&](const Schema *root,
                                 bool allowHiddenAtRoot) -> LogicalResult {
    SmallVector<uint8_t> state(schemas.size());
    struct Frame {
      const Schema *schema;
      bool exit;
      bool root;
    };
    SmallVector<Frame, 16> pending{{root, false, true}};
    while (!pending.empty()) {
      Frame frame = pending.pop_back_val();
      const Schema &schema = *frame.schema;
      sim::SimClassDeclOp declaration = schema.layout->declaration;
      auto found = schemaIndices.find(declaration.getId());
      if (found == schemaIndices.end())
        return failure();
      unsigned index = found->second;
      if (frame.exit) {
        state[index] = 2;
        continue;
      }
      if (state[index] == 2)
        continue;
      if (state[index] == 1)
        return declaration.emitOpError(
            "class bit-stream type graph contains a cycle");
      if (schema.invalidField) {
        sim::SimClassFieldDeclOp invalidField = schema.invalidField;
        return invalidField.emitOpError(
            "is not a legal member of a class bit-stream type");
      }
      if (schema.hasHiddenField && !(frame.root && allowHiddenAtRoot)) {
        if (frame.root)
          return declaration.emitOpError(
              "has a local or protected member but the class bit-stream "
              "source is not the current-instance 'this'");
        return declaration.emitOpError(
            "has a local or protected member reached through a nested class "
            "bit-stream handle");
      }
      state[index] = 1;
      pending.push_back({&schema, true, frame.root});
      for (sim::ClassHandleType nested :
           llvm::reverse(schema.nestedStaticTypes)) {
        sim::SimClassDeclOp nestedStatic = dispatch.lookup(nested);
        if (!nestedStatic)
          return declaration.emitOpError(
              "class bit-stream member references an unknown class");
        const Schema *child = lookup(nestedStatic.getId());
        if (!child)
          return nestedStatic.emitOpError(
              "has no static class bit-stream schema");
        pending.push_back({child, false, false});
      }
    }
    return success();
  };

  const Schema *staticSchema = lookup(staticClass.getId());
  if (!staticSchema ||
      failed(validateStaticGraph(staticSchema, allowHiddenRoot)))
    return failure();
  CastClosure result;
  for (sim::SimClassDeclOp candidate :
       dispatch.compatibleConcreteClasses(staticClass)) {
    const Schema *schema = lookup(candidate.getId());
    if (!schema)
      return candidate.emitOpError("has no concrete bit-stream schema"),
             failure();
    if (failed(validateStaticGraph(schema, allowHiddenRoot)))
      return failure();
    if (schema->invalidField) {
      sim::SimClassFieldDeclOp invalidField = schema->invalidField;
      return invalidField.emitOpError(
                 "is not a legal member of a class bit-stream type"),
             failure();
    }
    if (!allowHiddenRoot && schema->hasHiddenField)
      return candidate.emitOpError(
                 "has a local or protected member but the class bit-stream "
                 "source is not the current-instance 'this'"),
             failure();
    result.roots.push_back(schema);
  }

  // A class is a bit-stream type only when its entire statically reachable
  // class graph is acyclic. This is a type property: a particular runtime
  // chain reaching null does not make a recursive class definition legal.
  SmallVector<uint8_t> state(schemas.size());
  SmallVector<const Schema *> closure;
  struct Frame {
    const Schema *schema;
    bool exit;
  };
  SmallVector<Frame, 16> pending;
  for (const Schema *root : llvm::reverse(result.roots))
    pending.push_back({root, false});
  while (!pending.empty()) {
    Frame frame = pending.pop_back_val();
    const Schema &schema = *frame.schema;
    sim::SimClassDeclOp declaration = schema.layout->declaration;
    auto found = schemaIndices.find(declaration.getId());
    if (found == schemaIndices.end())
      return failure();
    unsigned index = found->second;
    if (frame.exit) {
      state[index] = 2;
      closure.push_back(&schema);
      continue;
    }
    if (state[index] == 2)
      continue;
    if (state[index] == 1)
      return declaration.emitOpError(
                 "class bit-stream type graph contains a cycle"),
             failure();
    if (schema.invalidField) {
      sim::SimClassFieldDeclOp invalidField = schema.invalidField;
      return invalidField.emitOpError(
                 "is not a legal member of a class bit-stream type"),
             failure();
    }
    state[index] = 1;
    pending.push_back({&schema, true});
    SmallVector<const Schema *, 8> children;
    for (sim::ClassHandleType nested : schema.nestedStaticTypes) {
      sim::SimClassDeclOp nestedStatic = dispatch.lookup(nested);
      if (!nestedStatic)
        return declaration.emitOpError(
                   "class bit-stream member references an unknown class"),
               failure();
      for (sim::SimClassDeclOp candidate :
           dispatch.compatibleConcreteClasses(nestedStatic)) {
        const Schema *child = lookup(candidate.getId());
        if (!child)
          return candidate.emitOpError("has no concrete bit-stream schema");
        if (failed(validateStaticGraph(child, false)))
          return failure();
        if (child->hasHiddenField)
          return candidate.emitOpError(
                     "has a local or protected member reached through a "
                     "nested class bit-stream handle"),
                 failure();
        children.push_back(child);
      }
    }
    for (const Schema *child : llvm::reverse(children))
      pending.push_back({child, false});
  }
  llvm::sort(closure, [](const Schema *lhs, const Schema *rhs) {
    sim::SimClassDeclOp lhsDeclaration = lhs->layout->declaration;
    sim::SimClassDeclOp rhsDeclaration = rhs->layout->declaration;
    return std::make_pair(lhsDeclaration.getId(), lhsDeclaration.getSymName()) <
           std::make_pair(rhsDeclaration.getId(), rhsDeclaration.getSymName());
  });
  result.schemas = std::move(closure);
  auto stored = std::make_unique<CastClosure>(std::move(result));
  const CastClosure *answer = stored.get();
  closureCache[source][visibilityIndex] = std::move(stored);
  return answer;
}

} // namespace obelisk::analysis
