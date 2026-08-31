//===- PrepareCaptures.cpp - Code-unit capture analysis ------------------===//

#include "PrepareCaptures.h"

#include "obelisk/Dialect/ForeachLoopMetadata.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"

#include <functional>
#include <optional>

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
               semantic::SVHierarchicalValueExpressionOp>(lvalue) ||
           lvalue->hasAttr(staticClassPropertyAttrName);
  // A static class property selected through an object expression is its own
  // storage root. The object qualifier is evaluated, but is neither read as
  // the property's value nor written by an assignment to the property.
  if (lvalue->hasAttr(staticClassPropertyAttrName))
    return false;
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

static semantic::SVSubroutineSymbolOp getOwningSubroutine(Operation *nested) {
  for (Operation *parent = nested ? nested->getParentOp() : nullptr; parent;
       parent = parent->getParentOp())
    if (auto subroutine = dyn_cast<semantic::SVSubroutineSymbolOp>(parent))
      return subroutine;
  return {};
}

static bool isRandSequenceFormal(Operation *operation) {
  return isa_and_nonnull<semantic::SVFormalArgumentSymbolOp>(operation) &&
         operation
             ->getParentOfType<semantic::SVRandSeqProductionSymbolOp>();
}

/// A non-ANSI subroutine port and the separate data declaration that gives it
/// a type declare a single object (IEEE 1800-2017 13.3). The declaration keeps
/// its own symbol, which the port names as its merged variable, so a reference
/// through it is a reference to the formal.
static bool isMergedFormalVariable(Operation *operation) {
  auto variable = dyn_cast_or_null<semantic::SVVariableSymbolOp>(operation);
  if (!variable)
    return false;
  auto subroutine =
      dyn_cast_or_null<semantic::SVSubroutineSymbolOp>(operation->getParentOp());
  if (!subroutine)
    return false;
  StringAttr name = variable.getSymNameAttr();
  for (Operation *sibling : getChildren(subroutine)) {
    auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(sibling);
    if (!formal)
      continue;
    if (SymbolRefAttr merged = formal.getMergedVariableSymbolAttr();
        merged && merged.getLeafReference() == name)
      return true;
  }
  return false;
}

FailureOr<PreparedCaptures>
analyzeCodeUnitCaptures(const PreparedUnits &units,
                        const llvm::StringMap<DescriptorInfo> &descriptors,
                        const llvm::StringMap<Operation *> &semanticSymbols,
                        ArrayRef<semantic::SVClassTypeOp> classSources) {
  bool invalid = false;
  PreparedCaptures result;
  PreparedUnits analysisUnits = units;
  llvm::DenseMap<Operation *, Operation *> constructorSources;
  llvm::DenseMap<Operation *, SmallVector<Operation *>> propertyInitializers;
  llvm::StringMap<semantic::SVClassTypeOp> classesBySymbol;
  for (semantic::SVClassTypeOp classType : classSources) {
    auto handle =
        dyn_cast<semantic::ClassHandleType>(classType.getSemanticType());
    if (handle)
      classesBySymbol[handle.getClassName().getLeafReference()] = classType;

    Operation *constructor = nullptr;
    for (Operation *child : getChildren(classType)) {
      semantic::SVSubroutineSymbolOp method = getClassMethod(child);
      if (method && method.getIsConstructor().value_or(false)) {
        constructor = method;
        break;
      }
    }
    if (!constructor && !classType.getIsInterface())
      constructor = classType;
    if (constructor)
      constructorSources[classType] = constructor;

    for (Operation *child : getChildren(classType)) {
      auto property = dyn_cast<semantic::SVClassPropertySymbolOp>(child);
      if (!property ||
          property.getLifetime() == semantic::SVVariableLifetime::Static ||
          getChildren(property).empty())
        continue;
      propertyInitializers[classType].push_back(property);
      // Instance-property initializers execute in the constructor, but Slang
      // keeps them on their declarations. Analyze each declaration as a
      // synthetic zero-time source so its direct and transitive captures can
      // participate in the same closure as ordinary code units.
      analysisUnits.units.push_back(
          {property, 0, sim::EntryKind::Function, {}, {}, {},
           ObserverResult::None});
    }
  }
  llvm::DenseMap<Operation *, llvm::StringSet<>> subroutineLocalDescriptors;
  auto &writtenDescriptors = result.writtenDescriptors;

  for (const PreparedUnit &unit : analysisUnits.units) {
    llvm::StringSet<> seenPaths;
    llvm::StringSet<> seenLocals;
    llvm::StringSet<> seenConstants;
    bool clockingEventMonitor =
        isa<semantic::SVClockingBlockSymbolOp>(unit.source) &&
        (unit.source->hasAttr(clockingEventMonitorRequiredAttrName) ||
         unit.source->hasAttr(clockingEventListAttrName));
    if (clockingEventMonitor) {
      StringRef path = getHierarchyName(unit.source);
      auto descriptor = descriptors.find(path);
      if (path.empty() || descriptor == descriptors.end() ||
          descriptor->second.kind != DescriptorInfo::Kind::Event) {
        emitError(getSemanticLocation(unit.source))
            << "clocking event monitor has no event descriptor";
        invalid = true;
      } else {
        seenPaths.insert(path);
        result.descriptors[unit.source].push_back(
            {path.str(), descriptor->second});
      }
    }
    if (unit.entryKind == sim::EntryKind::Observer) {
      semantic::SVSubroutineSymbolOp subroutine =
          getOwningSubroutine(unit.source);
      semantic::SVClassTypeOp owner = getOwningClass(subroutine);
      if (subroutine && owner &&
          !subroutine.getIsStatic().value_or(false)) {
        FailureOr<Type> type = getNormalizedSemanticType(owner);
        std::optional<StringRef> path = subroutine.getThisVariablePath();
        if (failed(type) || !path) {
          emitError(getSemanticLocation(unit.source))
              << "observer in an instance method has no resolved this "
                 "binding";
          invalid = true;
        } else {
          result.observerValues[unit.source].push_back(
              {path->str(), *type, false, false});
        }
      }
    }
    auto qualifiedAutomaticPath =
        [&](StringRef path, SymbolRefAttr reference,
            Operation *referencedSymbol) -> std::optional<std::string> {
      if (!reference || !referencedSymbol ||
          !isAutomaticLocalSymbol(referencedSymbol) ||
          (!descriptors.contains(path) &&
           !isa<semantic::SVPatternVarSymbolOp>(referencedSymbol)))
        return std::nullopt;
      StringRef symbolPath = getHierarchyName(referencedSymbol);
      std::string localPath = (symbolPath.empty() ? path : symbolPath).str();
      localPath += ".$local.";
      localPath += reference.getLeafReference().getValue();
      return localPath;
    };
    std::function<void(Operation *)> collectBinding = [&](Operation *nested) {
      if (auto path = nested->getAttrOfType<StringAttr>(
              interconnectLeafPathAttrName)) {
        auto descriptor = descriptors.find(path.getValue());
        if (descriptor == descriptors.end() ||
            descriptor->second.kind != DescriptorInfo::Kind::Net) {
          emitError(getSemanticLocation(nested))
              << "interconnect leaf has no typed net descriptor: "
              << path.getValue();
          invalid = true;
          return;
        }
        if (seenPaths.insert(path.getValue()).second)
          result.descriptors[unit.source].push_back(
              {path.getValue().str(), descriptor->second});
        if (isWrittenReferenceUse(nested))
          writtenDescriptors[unit.source].insert(path.getValue());
        if (!isWriteOnlyReferenceUse(nested))
          result.readDescriptors[unit.source].insert(path.getValue());
        return;
      }
      if (isa<semantic::SVCycleDelayControlOp>(nested)) {
        auto path =
            nested->getAttrOfType<StringAttr>(clockingEventPathAttrName);
        if (!path)
          return;
        auto descriptor = descriptors.find(path.getValue());
        if (descriptor == descriptors.end())
          return;
        if (seenPaths.insert(path.getValue()).second)
          result.descriptors[unit.source].push_back(
              {path.getValue().str(), descriptor->second});
        result.readDescriptors[unit.source].insert(path.getValue());
        return;
      }
      if (nested->hasAttr(clockingVariableAttrName)) {
        auto captureDescriptor = [&](StringLiteral pathName, bool read,
                                     bool written) {
          auto path = nested->getAttrOfType<StringAttr>(pathName);
          if (!path)
            return;
          auto descriptor = descriptors.find(path.getValue());
          if (descriptor == descriptors.end())
            return;
          if (seenPaths.insert(path.getValue()).second)
            result.descriptors[unit.source].push_back(
                {path.getValue().str(), descriptor->second});
          if (read)
            result.readDescriptors[unit.source].insert(path.getValue());
          if (written)
            writtenDescriptors[unit.source].insert(path.getValue());
        };
        bool written = isWrittenReferenceUse(nested);
        captureDescriptor(clockingSourcePathAttrName,
                          !isWriteOnlyReferenceUse(nested), written);
        captureDescriptor(clockingEventPathAttrName, /*read=*/true,
                          /*written=*/false);
        captureDescriptor(clockingEventRawPathAttrName, /*read=*/true,
                          /*written=*/false);
        if (written &&
            !nested->getAttrOfType<StringAttr>(clockingSourcePathAttrName)) {
          auto reference =
              nested->getAttrOfType<SymbolRefAttr>("referenced_symbol");
          auto symbol = reference
                            ? semanticSymbols.find(reference.getLeafReference())
                            : semanticSymbols.end();
          if (symbol != semanticSymbols.end() &&
              isa<semantic::SVClockVarSymbolOp>(symbol->second))
            for (Operation *expression : getChildren(symbol->second))
              expression->walk<WalkOrder::PreOrder>(
                  [&](Operation *child) { collectBinding(child); });
        }
        return;
      }
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
      } else if (auto pattern =
                     dyn_cast<semantic::SVVariablePatternOp>(nested)) {
        path = pattern.getReferencedPath();
        reference = pattern.getReferencedSymbol();
      } else if (auto member =
                     dyn_cast<semantic::SVMemberAccessExpressionOp>(nested)) {
        if (!member->hasAttr(staticClassPropertyAttrName))
          return;
        reference = member.getReferencedSymbol();
        path = member.getReferencedPath();
      } else if (auto instance =
                     dyn_cast<semantic::SVAssertionInstanceExpressionOp>(
                         nested)) {
        auto type = instance->getAttrOfType<TypeAttr>("semantic_type");
        if (!type || !isa<semantic::SequenceType>(type.getValue()))
          return;
        reference = instance.getReferencedSymbol();
        path = instance.getReferencedPath();
      } else if (nested->hasAttr(clockingBlockEventAttrName)) {
        auto eventPath =
            nested->getAttrOfType<StringAttr>(clockingEventPathAttrName);
        reference =
            nested->getAttrOfType<SymbolRefAttr>(clockingEventSymbolAttrName);
        if (!eventPath || !reference)
          return;
        path = eventPath.getValue();
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
      // activations copy each formal into shared task storage. Production
      // formals are different: 18.17 places them in the randsequence's
      // automatic scope, so they require activation-local bindings.
      if (unit.entryKind == sim::EntryKind::Function &&
          (isa_and_nonnull<semantic::SVFormalArgumentSymbolOp>(
               referencedSymbol) ||
           isMergedFormalVariable(referencedSymbol)) &&
          !isRandSequenceFormal(referencedSymbol))
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
      if (auto qualified =
              qualifiedAutomaticPath(path, reference, referencedSymbol)) {
        localPath = std::move(*qualified);
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
      bool observerNet = unit.entryKind == sim::EntryKind::Observer &&
                         isa<semantic::SVNetSymbolOp>(referencedSymbol);
      if (!isa<semantic::SVVariableSymbolOp, semantic::SVPatternVarSymbolOp>(
              referencedSymbol) &&
          !observerNet &&
          !isRandSequenceFormal(referencedSymbol) &&
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
             isa<semantic::SVPatternVarSymbolOp>(referencedSymbol),
             observerNet});
        referencedSymbol->walk<WalkOrder::PreOrder>(
            [&](Operation *initializerNode) {
              collectBinding(initializerNode);
            });
      }
      if (unit.entryKind == sim::EntryKind::Observer &&
          !isWriteOnlyReferenceUse(nested))
        result.observerReadLocals[unit.source].insert(path);
    };

    // Runtime foreach lowering binds each iterator by the frozen path stored
    // in loop_dimensions. Keep that metadata in lockstep with body uses when
    // an iterator shadows storage carrying the same source hierarchy.
    unit.source->walk([&](semantic::SVForeachLoopStatementOp foreach) {
      SmallVector<Attribute> dimensions;
      dimensions.reserve(foreach.getLoopDimensions().size());
      bool changed = false;
      for (Attribute attribute : foreach.getLoopDimensions()) {
        auto dimension = dyn_cast<DictionaryAttr>(attribute);
        auto path =
            dimension
                ? dimension.getAs<StringAttr>(foreach_metadata::iteratorPath)
                : StringAttr{};
        auto reference = dimension ? dimension.getAs<SymbolRefAttr>(
                                         foreach_metadata::iteratorSymbol)
                                   : SymbolRefAttr{};
        Operation *referencedSymbol = nullptr;
        if (reference) {
          auto symbol = semanticSymbols.find(reference.getLeafReference());
          if (symbol != semanticSymbols.end())
            referencedSymbol = symbol->second;
        }
        auto qualified =
            path ? qualifiedAutomaticPath(path.getValue(), reference,
                                          referencedSymbol)
                 : std::nullopt;
        if (!qualified) {
          dimensions.push_back(attribute);
          continue;
        }
        NamedAttrList rewritten(dimension);
        rewritten.set(foreach_metadata::iteratorPath,
                      StringAttr::get(foreach.getContext(), *qualified));
        dimensions.push_back(
            DictionaryAttr::get(foreach.getContext(), rewritten.getAttrs()));
        changed = true;
      }
      if (changed)
        foreach
          ->setAttr("loop_dimensions",
                    ArrayAttr::get(foreach.getContext(), dimensions));
    });

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
    if (clockingEventMonitor) {
      for (Operation *child : getChildren(unit.source)) {
        if (isa<semantic::SVClockVarSymbolOp>(child))
          continue;
        child->walk<WalkOrder::PreOrder>(
            [&](Operation *nested) { collectBinding(nested); });
      }
    } else {
      unit.source->walk<WalkOrder::PreOrder>(
          [&](Operation *nested) { collectBinding(nested); });
    }

    // Explicit randomize property names are compile-time selectors and are
    // erased before this analysis. Static selections still need their
    // class-wide value and compiler-owned randc state references in the unit
    // ABI. Freeze those descriptor captures from the prepared property plan;
    // they are writes, while any genuine constraint read is discovered from
    // the cloned constraint expressions above.
    unit.source->walk([&](semantic::SVCallExpressionOp call) {
      auto properties =
          call->getAttrOfType<ArrayAttr>(randomPropertiesAttrName);
      if (!properties)
        return;
      bool scopeRandomize = call->hasAttr(randomizeScopeAttrName);
      SmallVector<Attribute> rewrittenProperties;
      rewrittenProperties.reserve(properties.size());
      bool rewroteProperties = false;
      for (Attribute attribute : properties) {
        auto property = dyn_cast<DictionaryAttr>(attribute);
        if (!property) {
          rewrittenProperties.push_back(attribute);
          continue;
        }
        if (scopeRandomize) {
          auto path = property.getAs<StringAttr>(randomPropertyPathAttrName);
          auto reference =
              property.getAs<SymbolRefAttr>(randomPropertySymbolAttrName);
          Operation *referencedSymbol = nullptr;
          if (reference) {
            auto symbol =
                semanticSymbols.find(reference.getLeafReference());
            if (symbol != semanticSymbols.end())
              referencedSymbol = symbol->second;
          }
          auto qualified =
              path ? qualifiedAutomaticPath(path.getValue(), reference,
                                             referencedSymbol)
                   : std::nullopt;
          if (qualified) {
            NamedAttrList rewritten(property);
            rewritten.set(randomPropertyPathAttrName,
                          StringAttr::get(call.getContext(), *qualified));
            property =
                DictionaryAttr::get(call.getContext(), rewritten.getAttrs());
            rewroteProperties = true;
          }
        }
        rewrittenProperties.push_back(property);
        for (StringRef name :
             {randomPropertyPathAttrName, randomRandCKeyPathAttrName,
              randomRandCPositionPathAttrName}) {
          auto path = property.getAs<StringAttr>(name);
          if (!path)
            continue;
          auto descriptor = descriptors.find(path.getValue());
          if (descriptor == descriptors.end() ||
              descriptor->second.kind != DescriptorInfo::Kind::Storage) {
            // Scope-randomized automatic locals and formal arguments are
            // already present in the unit-local binding inventory. Only
            // design/static scope variables have descriptor-backed state.
            if (scopeRandomize)
              continue;
            emitError(getSemanticLocation(call))
                << "static randomization state has no storage descriptor: "
                << path.getValue();
            invalid = true;
            continue;
          }
          if (seenPaths.insert(path.getValue()).second)
            result.descriptors[unit.source].push_back(
                {path.getValue().str(), descriptor->second});
          writtenDescriptors[unit.source].insert(path.getValue());
        }
      }
      if (rewroteProperties)
        call->setAttr(randomPropertiesAttrName,
                      ArrayAttr::get(call.getContext(), rewrittenProperties));
    });

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
  for (const PreparedUnit &unit : analysisUnits.units)
    for (const auto &path : subroutineLocalDescriptors[unit.source])
      if (writtenDescriptors[unit.source].contains(path.getKey()))
        result.readDescriptors[unit.source].erase(path.getKey());

  llvm::DenseMap<Operation *, SmallVector<Operation *>> callEdges;
  for (const PreparedUnit &unit : analysisUnits.units) {
    llvm::SmallDenseSet<Operation *> targets;
    unit.source->walk([&](semantic::SVCallExpressionOp call) {
      Operation *target =
          analysisUnits.resolveDirectCallee(call, semanticSymbols);
      if (target && targets.insert(target).second)
        callEdges[unit.source].push_back(target);
      for (const PreparedVirtualInterfaceCallee *candidate :
           analysisUnits.resolveVirtualInterfaceCallees(call))
        if (targets.insert(candidate->source).second)
          callEdges[unit.source].push_back(candidate->source);
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
      if (auto nestedHooks =
              call->getAttrOfType<ArrayAttr>(randomNestedHooksAttrName))
        for (Attribute nestedAttr : nestedHooks)
          if (auto nested = dyn_cast<DictionaryAttr>(nestedAttr))
            for (StringRef name : {StringRef("pre_source"),
                                   StringRef("post_source")}) {
              auto reference = nested.getAs<FlatSymbolRefAttr>(name);
              auto hook = reference
                              ? semanticSymbols.find(
                                    reference.getLeafReference())
                              : semanticSymbols.end();
              if (hook != semanticSymbols.end() &&
                  targets.insert(hook->second).second)
                callEdges[unit.source].push_back(hook->second);
            }
    });
  }
  // An extern fork/join stub is a compile-time-generated task whose body calls
  // every provider concurrently. Model those synthetic edges before capture
  // closure so callers retain the exact aggregate read/write contract without
  // adding semantic call nodes or a runtime dispatch inventory.
  for (const auto &aggregation : analysisUnits.externForkJoinTargets) {
    auto &targets = callEdges[aggregation.first];
    llvm::SmallPtrSet<Operation *, 8> seenTargets;
    for (Operation *target : targets)
      seenTargets.insert(target);
    for (Operation *provider : aggregation.second)
      if (seenTargets.insert(provider).second)
        targets.push_back(provider);
  }

  for (const PreparedUnit &unit : analysisUnits.units)
    unit.source->walk([&](semantic::SVCallExpressionOp call) {
      Operation *target =
          analysisUnits.resolveDirectCallee(call, semanticSymbols);
      if (!target) {
        SmallVector<const PreparedVirtualInterfaceCallee *> candidates =
            analysisUnits.resolveVirtualInterfaceCallees(call);
        if (!candidates.empty())
          target = candidates.front()->source;
      }
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

  // An indirect ref actual reaches each provider through the aggregator. Give
  // every provider the same general argument-reference ABI as the stub so a
  // branch can forward the captured element reference without narrowing it.
  for (const auto &aggregation : analysisUnits.externForkJoinTargets)
    if (result.indirectRefTasks.contains(aggregation.first))
      for (Operation *provider : aggregation.second)
        result.indirectRefTasks.insert(provider);

  auto resolveClass = [&](semantic::SVNewClassExpressionOp construct)
      -> semantic::SVClassTypeOp {
    if (construct.getIsSuperClass()) {
      auto owner = construct->getParentOfType<semantic::SVClassTypeOp>();
      if (!owner || !owner.getBaseClass())
        return {};
      auto handle = dyn_cast<semantic::ClassHandleType>(*owner.getBaseClass());
      auto found = handle ? classesBySymbol.find(
                                handle.getClassName().getLeafReference())
                          : classesBySymbol.end();
      return found == classesBySymbol.end() ? semantic::SVClassTypeOp{}
                                             : found->second;
    }
    auto type = construct->getAttrOfType<TypeAttr>("semantic_type");
    auto handle = type ? dyn_cast<semantic::ClassHandleType>(type.getValue())
                       : semantic::ClassHandleType{};
    auto found = handle
                     ? classesBySymbol.find(
                           handle.getClassName().getLeafReference())
                     : classesBySymbol.end();
    return found == classesBySymbol.end() ? semantic::SVClassTypeOp{}
                                           : found->second;
  };
  auto addEdge = [&](Operation *source, Operation *target) {
    if (!source || !target)
      return;
    auto &targets = callEdges[source];
    if (!llvm::is_contained(targets, target))
      targets.push_back(target);
  };
  auto mergeConstantCaptures = [&](Operation *destination,
                                   Operation *source) {
    auto &constants = result.constants[destination];
    llvm::StringSet<> seen;
    for (const PreparedConstant &constant : constants)
      seen.insert(constant.path);
    for (const PreparedConstant &constant : result.constants[source])
      if (seen.insert(constant.path).second)
        constants.push_back(constant);
  };
  auto mergeDescriptorCaptures = [&](Operation *destination,
                                     Operation *source) {
    auto &captures = result.descriptors[destination];
    llvm::StringSet<> seen;
    for (const auto &capture : captures)
      seen.insert(capture.first);
    for (const auto &capture : result.descriptors[source])
      if (seen.insert(capture.first).second)
        captures.push_back(capture);
  };

  // Constructor execution includes the base constructor first and then every
  // declaration-order instance-property initializer. Model both lifecycle
  // edges explicitly because neither is necessarily nested under the
  // semantic constructor operation.
  for (semantic::SVClassTypeOp classType : classSources) {
    Operation *constructor = constructorSources.lookup(classType);
    if (!constructor)
      continue;
    if (std::optional<Type> baseType = classType.getBaseClass()) {
      auto handle = dyn_cast<semantic::ClassHandleType>(*baseType);
      auto found = handle ? classesBySymbol.find(
                                handle.getClassName().getLeafReference())
                          : classesBySymbol.end();
      if (found != classesBySymbol.end())
        addEdge(constructor, constructorSources.lookup(found->second));
    }
    for (Operation *property : propertyInitializers[classType]) {
      addEdge(constructor, property);
      // Property initializers are cloned into the constructor rather than
      // called as independent units. Their bindings must therefore be present
      // on the constructor that lowers the cloned expression.
      mergeDescriptorCaptures(constructor, property);
      mergeConstantCaptures(constructor, property);
    }
  }

  // A childless new-class expression invokes a synthesized constructor and
  // therefore has no semantic call operation from which to infer an edge.
  // Explicit constructor calls are harmlessly deduplicated here as well.
  for (const PreparedUnit &unit : analysisUnits.units)
    unit.source->walk([&](semantic::SVNewClassExpressionOp construct) {
      semantic::SVClassTypeOp classType = resolveClass(construct);
      if (classType)
        addEdge(unit.source, constructorSources.lookup(classType));
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

  for (const PreparedUnit &unit : analysisUnits.units)
    if (unit.entryKind == sim::EntryKind::Function ||
        unit.entryKind == sim::EntryKind::Task)
      result.contextStorageSources.insert(unit.source);
  for (const auto &constructor : constructorSources)
    result.contextStorageSources.insert(constructor.second);
  auto isDirectContextStorage = [&](Operation *source, const auto &capture) {
    return result.contextStorageSources.contains(source) &&
           isContextResolvableStorage(capture.second);
  };

  // A caller depends on each callee's capture contract. Propagate changes
  // backwards through that graph with a worklist instead of rescanning every
  // edge until a global fixed point. UVM has long virtual call chains; the
  // global scan revisited the entire library for every level in those chains.
  struct CaptureDependency {
    Operation *destination;
    size_t descriptorCursor = 0;
    size_t readCursor = 0;
    size_t writtenCursor = 0;
  };
  llvm::DenseMap<Operation *, SmallVector<CaptureDependency>> dependents;
  auto addDependency = [&](Operation *destination, Operation *source) {
    if (destination == source)
      return;
    auto &destinations = dependents[source];
    if (llvm::none_of(destinations, [&](const CaptureDependency &dependency) {
          return dependency.destination == destination;
        }))
      destinations.push_back({destination});
  };
  // Every declaration in a virtual family shares one capture ABI.
  for (auto [method, overridden] : virtualOverrideEdges) {
    addDependency(method, overridden);
    addDependency(overridden, method);
  }
  for (const auto &edge : callEdges)
    for (Operation *target : edge.second)
      addDependency(edge.first, target);

  // Keep membership inventories alive across edge visits. Reconstructing one
  // from the full destination vector for every visit was quadratic in the
  // size of generic class libraries.
  llvm::DenseMap<Operation *, llvm::StringSet<>> capturedPaths;
  for (const auto &entry : result.descriptors)
    for (const auto &capture : entry.second)
      capturedPaths[entry.first].insert(capture.first);

  // StringSet provides fast membership but no insertion-order delta. Mirror
  // its initial entries in vectors so each dependency edge can consume only
  // paths added since its previous visit.
  llvm::DenseMap<Operation *, SmallVector<StringRef>> orderedReads;
  for (const auto &entry : result.readDescriptors)
    for (const auto &read : entry.second)
      orderedReads[entry.first].push_back(read.getKey());
  llvm::DenseMap<Operation *, SmallVector<StringRef>> orderedWrites;
  for (const auto &entry : writtenDescriptors)
    for (const auto &written : entry.second)
      orderedWrites[entry.first].push_back(written.getKey());

  // Populate every node before taking references to DenseMap values in the
  // propagation loop; inserting a previously empty destination there could
  // rehash a map and invalidate the active source vector.
  auto prepareNode = [&](Operation *node) {
    (void)result.descriptors[node];
    (void)result.readDescriptors[node];
    (void)writtenDescriptors[node];
    (void)capturedPaths[node];
    (void)orderedReads[node];
    (void)orderedWrites[node];
  };
  for (const auto &entry : dependents) {
    prepareNode(entry.first);
    for (const CaptureDependency &dependency : entry.second)
      prepareNode(dependency.destination);
  }

  SmallVector<Operation *> worklist;
  llvm::DenseSet<Operation *> queued;
  auto enqueue = [&](Operation *source) {
    if (dependents.contains(source) && queued.insert(source).second)
      worklist.push_back(source);
  };
  for (const auto &entry : dependents)
    enqueue(entry.first);

  for (size_t next = 0; next < worklist.size(); ++next) {
    Operation *source = worklist[next];
    queued.erase(source);
    auto &sourceDescriptors = result.descriptors[source];
    auto &sourceReads = orderedReads[source];
    auto &sourceWrites = orderedWrites[source];
    for (CaptureDependency &dependency : dependents[source]) {
      Operation *destination = dependency.destination;
      bool changed = false;
      auto &captures = result.descriptors[destination];
      auto &seen = capturedPaths[destination];
      for (; dependency.descriptorCursor < sourceDescriptors.size();
           ++dependency.descriptorCursor) {
        const auto &capture = sourceDescriptors[dependency.descriptorCursor];
        // Functions and tasks resolve ordinary design storage from their
        // context. Keeping callee-only storage in the caller made large class
        // libraries grow a quadratic capture ABI even though no reference is
        // passed at the call site.
        if (isDirectContextStorage(destination, capture))
          continue;
        if (seen.insert(capture.first).second) {
          captures.push_back(capture);
          changed = true;
        }
      }
      for (; dependency.readCursor < sourceReads.size();
           ++dependency.readCursor) {
        StringRef read = sourceReads[dependency.readCursor];
        auto inserted = result.readDescriptors[destination].insert(read);
        if (inserted.second) {
          orderedReads[destination].push_back(inserted.first->getKey());
          changed = true;
        }
      }
      for (; dependency.writtenCursor < sourceWrites.size();
           ++dependency.writtenCursor) {
        StringRef written = sourceWrites[dependency.writtenCursor];
        auto inserted = writtenDescriptors[destination].insert(written);
        if (inserted.second) {
          orderedWrites[destination].push_back(inserted.first->getKey());
          changed = true;
        }
      }
      if (changed)
        enqueue(destination);
    }
  }

  // IEEE 1800-2017 9.2.2.2.1 excludes variables written anywhere in an
  // always_comb procedure, including through called functions, from its
  // implicit sensitivity. always_latch uses the same implicit-sensitivity
  // rules (9.2.2.3). Direct writes are also observed during lowering, but the
  // transitive callee contract must be filtered here before it is frozen onto
  // call sites; otherwise a read-modify-write helper makes the process wake on
  // its own output forever in the same time slot.
  for (const PreparedUnit &unit : analysisUnits.units) {
    if (unit.entryKind != sim::EntryKind::AlwaysComb &&
        unit.entryKind != sim::EntryKind::AlwaysLatch)
      continue;
    for (const auto &written : writtenDescriptors[unit.source])
      result.readDescriptors[unit.source].erase(written.getKey());
  }

  // Non-callable code units still need the complete transitive read set for
  // implicit sensitivity and observer dependencies. Reattach those descriptor
  // records only at the process boundary; callable units keep their compact,
  // direct context bindings.
  for (const PreparedUnit &unit : analysisUnits.units) {
    if (result.contextStorageSources.contains(unit.source))
      continue;
    auto &captures = result.descriptors[unit.source];
    llvm::StringSet<> seen;
    for (const auto &capture : captures)
      seen.insert(capture.first);
    for (const auto &read : result.readDescriptors[unit.source]) {
      auto descriptor = descriptors.find(read.getKey());
      if (descriptor != descriptors.end() && seen.insert(read.getKey()).second)
        captures.push_back({read.getKey().str(), descriptor->second});
    }
  }

  for (auto &entry : result.descriptors)
    llvm::sort(entry.second,
               [](const auto &lhs, const auto &rhs) {
                 if (lhs.second.kind != rhs.second.kind)
                   return lhs.second.kind < rhs.second.kind;
                 if (lhs.second.id != rhs.second.id)
                   return lhs.second.id < rhs.second.id;
                 return lhs.first < rhs.first;
               });
  for (const PreparedUnit &unit : analysisUnits.units) {
    llvm::sort(
        result.locals[unit.source],
        [](const auto &lhs, const auto &rhs) { return lhs.path < rhs.path; });
    llvm::sort(
        result.constants[unit.source],
        [](const auto &lhs, const auto &rhs) { return lhs.path < rhs.path; });
    llvm::sort(
        result.observerLocals[unit.source],
        [](const auto &lhs, const auto &rhs) { return lhs.path < rhs.path; });
    llvm::sort(
        result.observerValues[unit.source],
        [](const auto &lhs, const auto &rhs) { return lhs.path < rhs.path; });
  }
  return result;
}

} // namespace obelisk::simlowering
