//===- PrepareCaptures.cpp - Code-unit capture analysis ------------------===//

#include "PrepareCaptures.h"

#include "obelisk/Dialect/ForeachLoopMetadata.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"

#include <functional>

using namespace mlir;

namespace obelisk::simlowering {

static bool containsNestedOperation(Operation *root, Operation *nested) {
  for (Operation *current = nested; current; current = current->getParentOp())
    if (current == root)
      return true;
  return false;
}

static bool isStorageBaseUse(Operation *lvalue, Operation *reference) {
  if (lvalue == reference)
    return isa<semantic::SVNamedValueExpressionOp,
               semantic::SVHierarchicalValueExpressionOp>(lvalue);
  SmallVector<Operation *> children = getChildren(lvalue);
  if (isa<semantic::SVMemberAccessExpressionOp,
          semantic::SVElementSelectExpressionOp,
          semantic::SVRangeSelectExpressionOp>(lvalue))
    return !children.empty() &&
           containsNestedOperation(children.front(), reference) &&
           isStorageBaseUse(children.front(), reference);
  if (isa<semantic::SVConcatenationExpressionOp>(lvalue))
    return llvm::any_of(children, [&](Operation *child) {
      return containsNestedOperation(child, reference) &&
             isStorageBaseUse(child, reference);
    });
  return false;
}

static bool isWriteOnlyReferenceUse(Operation *reference) {
  for (Operation *ancestor = reference->getParentOp(); ancestor;
       ancestor = ancestor->getParentOp()) {
    auto assignment = dyn_cast<semantic::SVAssignmentExpressionOp>(ancestor);
    if (!assignment)
      continue;
    if (assignment.getOperatorKind())
      return false;
    SmallVector<Operation *> children = getChildren(assignment);
    size_t destinationIndex = assignment.getHasTimingControl() ? 1u : 0u;
    return destinationIndex < children.size() &&
           containsNestedOperation(children[destinationIndex], reference) &&
           isStorageBaseUse(children[destinationIndex], reference);
  }
  return false;
}

static bool isWrittenReferenceUse(Operation *reference) {
  if (isa<semantic::SVVariableDeclStatementOp>(reference))
    return !getChildren(reference).empty();
  for (Operation *ancestor = reference->getParentOp(); ancestor;
       ancestor = ancestor->getParentOp()) {
    if (auto assignment =
            dyn_cast<semantic::SVAssignmentExpressionOp>(ancestor)) {
      SmallVector<Operation *> children = getChildren(assignment);
      size_t destinationIndex = assignment.getHasTimingControl() ? 1u : 0u;
      return destinationIndex < children.size() &&
             containsNestedOperation(children[destinationIndex], reference) &&
             isStorageBaseUse(children[destinationIndex], reference);
    }
    if (auto unary = dyn_cast<semantic::SVUnaryExpressionOp>(ancestor)) {
      using Unary = semantic::SVUnaryOperator;
      Unary kind = unary.getOperatorKind();
      if (kind != Unary::Preincrement && kind != Unary::Predecrement &&
          kind != Unary::Postincrement && kind != Unary::Postdecrement)
        continue;
      SmallVector<Operation *> children = getChildren(unary);
      return children.size() == 1 &&
             containsNestedOperation(children.front(), reference) &&
             isStorageBaseUse(children.front(), reference);
    }
  }
  return false;
}

static bool isFixedForeachCollectionUse(Operation *reference) {
  for (Operation *ancestor = reference->getParentOp(); ancestor;
       ancestor = ancestor->getParentOp()) {
    auto foreach = dyn_cast<semantic::SVForeachLoopStatementOp>(ancestor);
    if (!foreach)
      continue;
    SmallVector<Operation *> children = getChildren(foreach);
    return children.size() == 2 &&
           containsNestedOperation(children.front(), reference) &&
           !foreach_metadata::hasRuntimeDimension(foreach.getLoopDimensions());
  }
  return false;
}

static semantic::SVClassTypeOp getOwningClass(Operation *member) {
  for (Operation *parent = member ? member->getParentOp() : nullptr; parent;
       parent = parent->getParentOp())
    if (auto classType = dyn_cast<semantic::SVClassTypeOp>(parent))
      return classType;
  return {};
}

FailureOr<PreparedCaptures>
analyzeCodeUnitCaptures(const PreparedUnits &units,
                        const llvm::StringMap<DescriptorInfo> &descriptors,
                        const llvm::StringMap<Operation *> &semanticSymbols,
                        ArrayRef<semantic::SVClassTypeOp> classSources) {
  bool invalid = false;
  PreparedCaptures result;
  llvm::DenseMap<Operation *, llvm::StringSet<>> subroutineLocalDescriptors;
  llvm::DenseMap<Operation *, llvm::StringSet<>> writtenDescriptors;

  for (const PreparedUnit &unit : units.units) {
    llvm::StringSet<> seenPaths;
    llvm::StringSet<> seenLocals;
    llvm::StringSet<> seenConstants;
    std::function<void(Operation *)> collectBinding = [&](Operation *nested) {
      StringRef path;
      SymbolRefAttr reference;
      if (auto named = dyn_cast<semantic::SVNamedValueExpressionOp>(nested)) {
        path = named.getReferencedPath();
        reference = named.getReferencedSymbol();
      } else if (auto hierarchical =
                     dyn_cast<semantic::SVHierarchicalValueExpressionOp>(
                         nested)) {
        path = hierarchical.getReferencedPath();
        reference = hierarchical.getReferencedSymbol();
      } else if (auto declaration =
                     dyn_cast<semantic::SVVariableDeclStatementOp>(nested)) {
        path = declaration.getReferencedPath();
        reference = declaration.getReferencedSymbol();
      } else {
        return;
      }
      if (!isa<semantic::SVVariableDeclStatementOp>(nested) &&
          isFixedForeachCollectionUse(nested))
        return;
      Operation *referencedSymbol = nullptr;
      if (reference) {
        auto symbol = semanticSymbols.find(reference.getLeafReference());
        if (symbol != semanticSymbols.end())
          referencedSymbol = symbol->second;
      }
      // Function formals are already represented by public argument bindings.
      // Static tasks retain their descriptor capture because overlapping
      // activations copy each formal into shared task storage.
      if (unit.entryKind == sim::EntryKind::Function &&
          isa_and_nonnull<semantic::SVFormalArgumentSymbolOp>(referencedSymbol))
        return;
      // Unnamed statement scopes are not part of Slang's hierarchical name,
      // so an automatic local can have the same path string as design
      // storage it shadows. Give the local binding a stable symbol-qualified
      // path and freeze that path on every use before the unit is cloned.
      // The descriptor retains the source hierarchy while lowerUnit can now
      // distinguish the two bindings without consulting the symbol table.
      bool automaticLocal =
          referencedSymbol && isAutomaticLocalSymbol(referencedSymbol);
      std::string localPath;
      if (automaticLocal && descriptors.contains(path)) {
        StringRef symbolPath = getHierarchyName(referencedSymbol);
        localPath = (symbolPath.empty() ? path : symbolPath).str();
        localPath += ".$local.";
        localPath += reference.getLeafReference().getValue();
        path = localPath;
        nested->setAttr("referenced_path",
                        StringAttr::get(nested->getContext(), path));
      }
      auto descriptor = descriptors.find(path);
      if (descriptor != descriptors.end() && !automaticLocal) {
        if (seenPaths.insert(path).second)
          result.descriptors[unit.source].push_back(
              {path.str(), descriptor->second});
        if (referencedSymbol &&
            isa<semantic::SVSubroutineSymbolOp>(unit.source) &&
            referencedSymbol != unit.source &&
            containsNestedOperation(unit.source, referencedSymbol))
          subroutineLocalDescriptors[unit.source].insert(path);
        if (isWrittenReferenceUse(nested))
          writtenDescriptors[unit.source].insert(path);
        if (!isa<semantic::SVVariableDeclStatementOp>(nested) &&
            !isWriteOnlyReferenceUse(nested))
          result.readDescriptors[unit.source].insert(path);
        return;
      }
      if (!referencedSymbol)
        return;
      if (isa<semantic::SVParameterSymbolOp, semantic::SVEnumValueSymbolOp,
              semantic::SVSpecparamSymbolOp>(referencedSymbol)) {
        if (auto type = nested->getAttrOfType<TypeAttr>("semantic_type");
            type && isa<ir::UnboundedType>(type.getValue()))
          return;
        if (!seenConstants.insert(path).second)
          return;
        FailureOr<sim::FrozenConstantAttr> value =
            freezeSemanticConstant(referencedSymbol);
        if (failed(value)) {
          invalid = true;
          return;
        }
        result.constants[unit.source].push_back({path.str(), *value});
        return;
      }
      if (!isa<semantic::SVVariableSymbolOp, semantic::SVPatternVarSymbolOp>(
              referencedSymbol) &&
          !(unit.entryKind == sim::EntryKind::Observer &&
            isa<semantic::SVFormalArgumentSymbolOp>(referencedSymbol)))
        return;
      FailureOr<Type> type = getNormalizedSemanticType(referencedSymbol);
      if (failed(type)) {
        invalid = true;
        return;
      }
      if (seenLocals.insert(path).second) {
        auto &destination = unit.entryKind == sim::EntryKind::Observer
                                ? result.observerLocals[unit.source]
                                : result.locals[unit.source];
        destination.push_back(
            {path.str(), *type, isAutomaticLocalSymbol(referencedSymbol),
             isa<semantic::SVPatternVarSymbolOp>(referencedSymbol)});
        referencedSymbol->walk<WalkOrder::PreOrder>(
            [&](Operation *initializerNode) {
              collectBinding(initializerNode);
            });
      }
      if (unit.entryKind == sim::EntryKind::Observer &&
          !isWriteOnlyReferenceUse(nested))
        result.observerReadLocals[unit.source].insert(path);
    };

    bool initializesDesignStorage =
        isa<semantic::SVVariableSymbolOp>(unit.source);
    if (auto property =
            dyn_cast<semantic::SVClassPropertySymbolOp>(unit.source))
      initializesDesignStorage =
          property.getLifetime() == semantic::SVVariableLifetime::Static;
    if (initializesDesignStorage) {
      StringRef path = getHierarchyName(unit.source);
      auto descriptor = descriptors.find(path);
      if (descriptor == descriptors.end()) {
        emitError(getSemanticLocation(unit.source))
            << "design initializer has no storage descriptor";
        invalid = true;
      } else if (seenPaths.insert(path).second) {
        result.descriptors[unit.source].push_back(
            {path.str(), descriptor->second});
      }
    }
    unit.source->walk<WalkOrder::PreOrder>(
        [&](Operation *nested) { collectBinding(nested); });

    // Manual covergroup sampling evaluates declaration expressions in the
    // caller, so their enclosing design storage is part of the sampling ABI.
    unit.source->walk([&](semantic::SVCallExpressionOp call) {
      if (call.getIsSystemCall() || call.getCalleeName() != "sample" ||
          !call.getReferencedSymbol())
        return;
      auto symbol =
          semanticSymbols.find(call.getReferencedSymbol()->getLeafReference());
      if (symbol == semanticSymbols.end())
        return;
      auto covergroup =
          symbol->second->getParentOfType<semantic::SVCovergroupTypeOp>();
      if (!covergroup)
        return;
      covergroup->walk<WalkOrder::PreOrder>(
          [&](Operation *nested) { collectBinding(nested); });
    });
    if (auto connection = dyn_cast<semantic::SVPortConnectionOp>(unit.source)) {
      StringRef internal = connection.getInternalPath().value_or(StringRef{});
      if (!internal.empty())
        if (auto descriptor = descriptors.find(internal);
            descriptor != descriptors.end() &&
            seenPaths.insert(internal).second)
          result.descriptors[unit.source].push_back(
              {internal.str(), descriptor->second});
    }
  }
  if (invalid)
    return failure();

  // A subroutine-local static is part of the callee ABI, but a local that the
  // callee itself writes is scratch state rather than a caller-visible input.
  // Do not propagate it into an enclosing continuous or wildcard process's
  // implicit sensitivity set; otherwise a read-modify-write loop variable can
  // make the caller retrigger itself forever at the same simulation time.
  for (const PreparedUnit &unit : units.units)
    for (const auto &path : subroutineLocalDescriptors[unit.source])
      if (writtenDescriptors[unit.source].contains(path.getKey()))
        result.readDescriptors[unit.source].erase(path.getKey());

  llvm::DenseMap<Operation *, SmallVector<Operation *>> callEdges;
  for (const PreparedUnit &unit : units.units) {
    llvm::SmallDenseSet<Operation *> targets;
    unit.source->walk([&](semantic::SVCallExpressionOp call) {
      Operation *target = units.resolveDirectCallee(call, semanticSymbols);
      if (target && targets.insert(target).second)
        callEdges[unit.source].push_back(target);
      auto addRandomizeHook = [&](StringRef attrName) {
        auto reference = call->getAttrOfType<FlatSymbolRefAttr>(attrName);
        auto hook = reference
                        ? semanticSymbols.find(reference.getLeafReference())
                        : semanticSymbols.end();
        if (hook != semanticSymbols.end() &&
            targets.insert(hook->second).second)
          callEdges[unit.source].push_back(hook->second);
      };
      addRandomizeHook(randomPreHookSourceAttrName);
      addRandomizeHook(randomPostHookSourceAttrName);
    });
  }

  for (const PreparedUnit &unit : units.units)
    unit.source->walk([&](semantic::SVCallExpressionOp call) {
      Operation *target = units.resolveDirectCallee(call, semanticSymbols);
      auto task = dyn_cast_or_null<semantic::SVSubroutineSymbolOp>(target);
      if (!task ||
          task.getSubroutineKind() != semantic::SVSubroutineKind::Task ||
          getOwningClass(task))
        return;
      SmallVector<semantic::SVFormalArgumentSymbolOp> formals;
      for (Operation *child : getChildren(task))
        if (auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(child))
          formals.push_back(formal);
      SmallVector<Operation *> actuals = getChildren(call);
      if (actuals.size() != formals.size())
        return;
      for (auto [actual, formal] : llvm::zip_equal(actuals, formals)) {
        if (formal.getDirection() != semantic::SVArgumentDirection::Ref)
          continue;
        auto select = dyn_cast<semantic::SVElementSelectExpressionOp>(actual);
        if (!select)
          continue;
        SmallVector<Operation *> selection = getChildren(select);
        if (selection.size() != 2)
          continue;
        FailureOr<Type> base = getNormalizedSemanticType(selection.front());
        if (succeeded(base) &&
            isa<sim::DynamicArrayType, sim::QueueType, sim::AssocArrayType>(
                *base)) {
          result.indirectRefTasks.insert(target);
          return;
        }
      }
    });

  SmallVector<std::pair<Operation *, Operation *>> virtualOverrideEdges;
  for (semantic::SVClassTypeOp classType : classSources)
    for (Operation *child : getChildren(classType)) {
      semantic::SVSubroutineSymbolOp method = getClassMethod(child);
      if (!method || !method.getIsVirtual().value_or(false) ||
          !method.getOverrideSymbol())
        continue;
      auto overridden =
          semanticSymbols.find(method.getOverrideSymbol()->getLeafReference());
      if (overridden != semanticSymbols.end())
        virtualOverrideEdges.emplace_back(method, overridden->second);
    }

  bool changed;
  do {
    changed = false;
    auto mergeCaptures = [&](Operation *destination, Operation *source) {
      auto &captures = result.descriptors[destination];
      llvm::StringSet<> seen;
      for (auto &capture : captures)
        seen.insert(capture.first);
      for (auto &capture : result.descriptors[source])
        if (seen.insert(capture.first).second) {
          captures.push_back(capture);
          changed = true;
        }
      for (const auto &read : result.readDescriptors[source])
        changed |=
            result.readDescriptors[destination].insert(read.getKey()).second;
    };
    // Every declaration in a virtual family shares one capture ABI.
    for (auto [method, overridden] : virtualOverrideEdges) {
      mergeCaptures(method, overridden);
      mergeCaptures(overridden, method);
    }
    for (const PreparedUnit &unit : units.units)
      for (Operation *target : callEdges[unit.source])
        mergeCaptures(unit.source, target);
  } while (changed);

  for (const PreparedUnit &unit : units.units) {
    llvm::sort(result.descriptors[unit.source],
               [](const auto &lhs, const auto &rhs) {
                 if (lhs.second.kind != rhs.second.kind)
                   return lhs.second.kind < rhs.second.kind;
                 if (lhs.second.id != rhs.second.id)
                   return lhs.second.id < rhs.second.id;
                 return lhs.first < rhs.first;
               });
    llvm::sort(
        result.locals[unit.source],
        [](const auto &lhs, const auto &rhs) { return lhs.path < rhs.path; });
    llvm::sort(
        result.constants[unit.source],
        [](const auto &lhs, const auto &rhs) { return lhs.path < rhs.path; });
    llvm::sort(
        result.observerLocals[unit.source],
        [](const auto &lhs, const auto &rhs) { return lhs.path < rhs.path; });
  }
  return result;
}

} // namespace obelisk::simlowering
