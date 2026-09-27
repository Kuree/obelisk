//===- PrepareDeclarations.cpp - Executable declaration planning ---------===//
//
// Freezes covergroup and class metadata into deterministic executable
// declarations before individual code units are isolated.
//
//===----------------------------------------------------------------------===//

#include "PrepareDeclarations.h"

#include "Detail.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Reflection/VPIObjectModel.h"

#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/raw_ostream.h"

#include <functional>

using namespace mlir;

namespace obelisk::simlowering {
namespace {

bool isWeakReferenceClass(semantic::SVClassTypeOp classType) {
  return getHierarchyName(classType).starts_with("std::weak_reference#(");
}

/// Whether a normalized source can reach an object handle while recursively
/// forming its bit stream. This is intentionally structural and stops at the
/// class handle: the class declaration graph is symbol-based rather than a
/// recursively embedded MLIR type.
bool containsClassBitstreamSource(Type type) {
  SmallVector<Type, 16> pending{type};
  llvm::SmallPtrSet<const void *, 16> visited;
  while (!pending.empty()) {
    Type current = pending.pop_back_val();
    if (!visited.insert(current.getAsOpaquePointer()).second)
      continue;
    if (isa<sim::ClassHandleType>(current))
      return true;
    if (auto array = dyn_cast<sim::DynamicArrayType>(current)) {
      pending.push_back(array.getElementType());
      continue;
    }
    if (auto queue = dyn_cast<sim::QueueType>(current)) {
      pending.push_back(queue.getElementType());
      continue;
    }
    if (auto associative = dyn_cast<sim::AssocArrayType>(current)) {
      pending.push_back(associative.getElementType());
      continue;
    }
    if (auto array = dyn_cast<sim::UnpackedArrayType>(current)) {
      pending.push_back(array.getElementType());
      continue;
    }
    if (!isa<sim::UnpackedStructType>(current))
      continue;
    for (unsigned ordinal = 0, count = sim::getAggregateNumElements(current);
         ordinal != count; ++ordinal)
      pending.push_back(sim::getAggregateElementType(current, ordinal));
  }
  return false;
}

/// The local/protected-property exception in 6.24.3 applies only to the exact
/// current-instance expression.  Resolve that identity while the semantic
/// symbol reference is still available; lowered SSA equality would also bless
/// aliases of `this` and is therefore too permissive.
bool isExactCurrentInstanceThis(Operation *expression) {
  while (auto conversion =
             dyn_cast<semantic::SVConversionExpressionOp>(expression)) {
    BoolAttr implicit = conversion->getAttrOfType<BoolAttr>("is_implicit");
    if (implicit && !implicit.getValue())
      return false;
    SmallVector<Operation *> children = getChildren(conversion);
    if (children.size() != 1)
      return false;
    expression = children.front();
  }
  auto named = dyn_cast<semantic::SVNamedValueExpressionOp>(expression);
  if (!named)
    return false;
  SymbolRefAttr referenced = named.getReferencedSymbolAttr();
  if (!referenced)
    return false;
  for (Operation *parent = expression->getParentOp(); parent;
       parent = parent->getParentOp())
    if (auto current =
            parent->getAttrOfType<SymbolRefAttr>("this_variable_symbol"))
      return current == referenced;
  return false;
}

uint64_t getVirtualMethodSignatureID(semantic::SVSubroutineSymbolOp method) {
  std::string key;
  llvm::raw_string_ostream stream(key);
  if (auto name = method->getAttrOfType<StringAttr>("name"))
    stream << name.getValue();
  stream << '#' << static_cast<uint32_t>(method.getSubroutineKind()) << '#';
  if (auto type = method->getAttrOfType<TypeAttr>("semantic_type"))
    type.getValue().print(stream);
  return stableCodeUnitID(key);
}

} // namespace

semantic::SVSubroutineSymbolOp getClassMethod(Operation *member) {
  if (auto method = dyn_cast<semantic::SVSubroutineSymbolOp>(member))
    return method;
  if (auto prototype = dyn_cast<semantic::SVMethodPrototypeSymbolOp>(member))
    for (Operation *child : getChildren(prototype))
      if (auto method = dyn_cast<semantic::SVSubroutineSymbolOp>(child))
        return method;
  return {};
}

LogicalResult
materializeInheritedCovergroupPlans(semantic::SVRootSymbolOp semanticRoot,
                                    OpBuilder &builder) {
  llvm::StringMap<semantic::SVCovergroupTypeOp> groups;
  semanticRoot->walk([&](semantic::SVCovergroupTypeOp group) {
    auto handle =
        dyn_cast<semantic::CovergroupHandleType>(group.getSemanticType());
    if (handle)
      groups[handle.getCovergroupName().getLeafReference()] = group;
  });

  auto getBase = [&](semantic::SVCovergroupTypeOp group) {
    TypeAttr baseAttr = group.getBaseGroupAttr();
    auto handle =
        baseAttr ? dyn_cast<semantic::CovergroupHandleType>(baseAttr.getValue())
                 : semantic::CovergroupHandleType{};
    auto found =
        handle ? groups.find(handle.getCovergroupName().getLeafReference())
               : groups.end();
    return found == groups.end() ? semantic::SVCovergroupTypeOp{}
                                 : found->second;
  };
  auto getBody = [](semantic::SVCovergroupTypeOp group) {
    for (Operation *child : getChildren(group))
      if (auto body = dyn_cast<semantic::SVCovergroupBodySymbolOp>(child))
        return body;
    return semantic::SVCovergroupBodySymbolOp{};
  };
  auto getSymbolReference = [](Operation *symbol) {
    SmallVector<StringAttr> path;
    for (Operation *current = symbol; current; current = current->getParentOp())
      if (isa<SymbolOpInterface>(current))
        path.push_back(SymbolTable::getSymbolName(current));
    assert(!path.empty() && "semantic symbol has no symbolic ancestor");
    std::reverse(path.begin(), path.end());
    SmallVector<FlatSymbolRefAttr> nested;
    nested.reserve(path.size() - 1);
    for (StringAttr name : ArrayRef(path).drop_front())
      nested.push_back(FlatSymbolRefAttr::get(name));
    return SymbolRefAttr::get(path.front(), nested);
  };

  llvm::SmallPtrSet<Operation *, 16> done;
  llvm::SmallPtrSet<Operation *, 16> active;
  std::function<LogicalResult(semantic::SVCovergroupTypeOp)> materialize =
      [&](semantic::SVCovergroupTypeOp derived) -> LogicalResult {
    if (done.contains(derived))
      return success();
    if (!active.insert(derived).second)
      return derived.emitError("covergroup inheritance contains a cycle");
    semantic::SVCovergroupTypeOp base = getBase(derived);
    if (!base) {
      active.erase(derived);
      done.insert(derived);
      return success();
    }
    if (failed(materialize(base)))
      return failure();
    auto baseBody = getBody(base);
    auto derivedBody = getBody(derived);
    if (!baseBody || !derivedBody)
      return derived.emitError("inherited covergroup has no effective body");

    llvm::StringSet<> directFormals;
    for (Operation *child : getChildren(derived))
      if (auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(child))
        directFormals.insert(getDebugName(formal));
    builder.setInsertionPoint(derivedBody);
    for (Operation *child : getChildren(base))
      if (auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(child))
        if (!directFormals.contains(getDebugName(formal)))
          builder.clone(*formal);
    for (Operation *child : getChildren(base))
      if (isa<semantic::SVSignalEventControlOp, semantic::SVEventListControlOp>(
              child))
        builder.clone(*child);

    llvm::StringSet<> directPoints;
    llvm::StringSet<> directPointSymbols;
    llvm::StringSet<> directCrosses;
    llvm::DenseSet<uint64_t> directOptions;
    for (Operation *member : getChildren(derivedBody)) {
      if (auto point = dyn_cast<semantic::SVCoverpointSymbolOp>(member)) {
        directPoints.insert(getDebugName(point));
        directPointSymbols.insert(point.getSymName());
      } else if (auto cross = dyn_cast<semantic::SVCoverCrossSymbolOp>(member))
        directCrosses.insert(getDebugName(cross));
      else if (auto option = dyn_cast<semantic::SVCoverageOptionOp>(member))
        directOptions.insert(
            (uint64_t{static_cast<uint32_t>(option.getScopeKind())} << 32) |
            static_cast<uint32_t>(option.getOptionKind()));
    }

    Operation *insertionPoint =
        derivedBody->getRegion(0).front().empty()
            ? nullptr
            : &derivedBody->getRegion(0).front().front();
    if (insertionPoint)
      builder.setInsertionPoint(insertionPoint);
    else
      builder.setInsertionPointToEnd(&derivedBody->getRegion(0).front());
    uint64_t inheritedOptions = 0;
    llvm::StringMap<StringAttr> inheritedPointSymbols;
    for (Operation *member : getChildren(baseBody)) {
      if (auto option = dyn_cast<semantic::SVCoverageOptionOp>(member)) {
        uint64_t key =
            (uint64_t{static_cast<uint32_t>(option.getScopeKind())} << 32) |
            static_cast<uint32_t>(option.getOptionKind());
        if (!directOptions.contains(key)) {
          Operation *clone = builder.clone(*member);
          clone->setAttr("owner_symbol", getSymbolReference(derived));
          ++inheritedOptions;
        }
        continue;
      }
      auto point = dyn_cast<semantic::SVCoverpointSymbolOp>(member);
      if (!point)
        continue;
      Operation *clone = builder.clone(*member);
      auto clonedPoint = cast<semantic::SVCoverpointSymbolOp>(clone);
      if (directPoints.contains(getDebugName(point))) {
        clone->setAttr("obelisk.coverage.inherited_overridden",
                       builder.getUnitAttr());
        if (!clone->hasAttr("obelisk.coverage.inherited_origin_type"))
          clone->setAttr(
              "obelisk.coverage.inherited_origin_type",
              builder.getI64IntegerAttr(stableFunctionalTypeID(base)));
      }
      if (directPointSymbols.contains(point.getSymName())) {
        auto origin = clone->getAttrOfType<IntegerAttr>(
            "obelisk.coverage.inherited_origin_type");
        std::string uniqueName =
            (Twine(point.getSymName()) + "$inherited$" +
             Twine(origin ? origin.getValue().getZExtValue()
                          : stableFunctionalTypeID(base)))
                .str();
        clonedPoint.setSymName(builder.getStringAttr(uniqueName));
      }
      SymbolRefAttr clonedPointReference = getSymbolReference(clonedPoint);
      clonedPoint->walk([&](semantic::SVCoverageOptionOp option) {
        option->setAttr("owner_symbol", clonedPointReference);
      });
      inheritedPointSymbols[point.getSymName()] = clonedPoint.getSymNameAttr();
    }
    for (Operation *member : getChildren(baseBody)) {
      auto cross = dyn_cast<semantic::SVCoverCrossSymbolOp>(member);
      if (!cross || directCrosses.contains(getDebugName(cross)))
        continue;
      Operation *clone = builder.clone(*member);
      auto clonedCross = cast<semantic::SVCoverCrossSymbolOp>(clone);
      SymbolRefAttr clonedCrossReference = getSymbolReference(clonedCross);
      clonedCross->walk([&](Operation *nested) {
        if (isa<semantic::SVCoverageOptionOp>(nested))
          nested->setAttr("owner_symbol", clonedCrossReference);
        if (nested->hasTrait<OpTrait::CoverageSelectorNode>())
          nested->setAttr("enclosing_cross_symbol", clonedCrossReference);
      });
      SmallVector<Attribute> effectiveTargets;
      effectiveTargets.reserve(cross.getTargetSymbols().size());
      ArrayAttr baseEffectiveTargets = cross->getAttrOfType<ArrayAttr>(
          "obelisk.coverage.effective_target_symbols");
      for (auto [ordinal, targetAttr] :
           llvm::enumerate(cross.getTargetSymbols())) {
        auto target = dyn_cast<SymbolRefAttr>(targetAttr);
        StringRef sourceTarget =
            target ? target.getLeafReference() : StringRef{};
        if (baseEffectiveTargets && ordinal < baseEffectiveTargets.size())
          if (auto inherited =
                  dyn_cast<StringAttr>(baseEffectiveTargets[ordinal]))
            sourceTarget = inherited.getValue();
        auto effective = inheritedPointSymbols.find(sourceTarget);
        effectiveTargets.push_back(effective == inheritedPointSymbols.end()
                                       ? builder.getStringAttr(sourceTarget)
                                       : Attribute(effective->second));
      }
      clone->setAttr("obelisk.coverage.effective_target_symbols",
                     builder.getArrayAttr(effectiveTargets));
      clone->walk([&](Operation *nested) {
        auto target = nested->getAttrOfType<SymbolRefAttr>("target_symbol");
        StringRef sourceTarget =
            target ? target.getLeafReference() : StringRef{};
        if (auto inherited = nested->getAttrOfType<StringAttr>(
                "obelisk.coverage.effective_target_symbol"))
          sourceTarget = inherited.getValue();
        auto effective = inheritedPointSymbols.find(sourceTarget);
        if (effective != inheritedPointSymbols.end())
          nested->setAttr("obelisk.coverage.effective_target_symbol",
                          effective->second);
      });
    }
    derivedBody->setAttr("option_count",
                         builder.getI64IntegerAttr(
                             derivedBody.getOptionCount() + inheritedOptions));
    derived->setAttr("obelisk.coverage.base_group", derived.getBaseGroupAttr());
    derived->removeAttr("base_group");
    derived->setAttr("coverage_event_kind",
                     builder.getI32IntegerAttr(
                         static_cast<uint32_t>(base.getCoverageEventKind())));
    active.erase(derived);
    done.insert(derived);
    return success();
  };

  for (const auto &entry : groups)
    if (failed(materialize(entry.second)))
      return failure();
  return success();
}

LogicalResult materializeCovergroupDeclarations(
    semantic::SVRootSymbolOp semanticRoot, OpBuilder &builder,
    const llvm::DenseSet<Type> &unusedEmbeddedCovergroupTypes) {
  SmallVector<semantic::SVCovergroupTypeOp> covergroupSources;
  semanticRoot->walk([&](semantic::SVCovergroupTypeOp covergroup) {
    if (!unusedEmbeddedCovergroupTypes.contains(covergroup.getSemanticType()))
      covergroupSources.push_back(covergroup);
  });
  llvm::sort(covergroupSources, [](semantic::SVCovergroupTypeOp lhs,
                                   semantic::SVCovergroupTypeOp rhs) {
    return std::tuple(getHierarchyName(lhs), lhs.getSymName()) <
           std::tuple(getHierarchyName(rhs), rhs.getSymName());
  });

  bool invalid = false;
  llvm::DenseMap<uint64_t, Operation *> typeIDs;
  for (semantic::SVCovergroupTypeOp covergroup : covergroupSources) {
    auto handle =
        dyn_cast<semantic::CovergroupHandleType>(covergroup.getSemanticType());
    if (!handle) {
      emitError(getSemanticLocation(covergroup))
          << "covergroup declaration has no handle type";
      invalid = true;
      continue;
    }
    const uint64_t typeID = stableFunctionalTypeID(covergroup);
    auto [typeOwner, insertedType] =
        typeIDs.try_emplace(typeID, covergroup.getOperation());
    if (!insertedType) {
      emitError(getSemanticLocation(covergroup))
          << "functional coverage type ID hash collision with "
          << typeOwner->second->getLoc();
      invalid = true;
      continue;
    }
    StringAttr symbol =
        getSimulationCovergroupSymbol(handle.getCovergroupName());
    FlatSymbolRefAttr base;
    TypeAttr baseAttr =
        covergroup->getAttrOfType<TypeAttr>("obelisk.coverage.base_group");
    if (baseAttr) {
      auto baseHandle =
          dyn_cast<semantic::CovergroupHandleType>(baseAttr.getValue());
      if (!baseHandle) {
        emitError(getSemanticLocation(covergroup))
            << "covergroup base is not a covergroup handle";
        invalid = true;
        continue;
      }
      base = FlatSymbolRefAttr::get(
          getSimulationCovergroupSymbol(baseHandle.getCovergroupName()));
    }
    auto declaration = sim::SimCovergroupDeclOp::create(
        builder, getSemanticLocation(covergroup), symbol, typeID, base,
        builder.getStringAttr(getDebugName(covergroup)));
    SymbolTable::setSymbolVisibility(declaration,
                                     SymbolTable::Visibility::Public);
  }
  return failure(invalid);
}

FailureOr<PreparedClassDeclarations> materializeClassDeclarations(
    ModuleOp module, sim::SimDesignOp design,
    semantic::SVRootSymbolOp semanticRoot, OpBuilder &builder,
    const llvm::StringMap<Operation *> &semanticSymbols,
    const llvm::DenseSet<Type> &unusedEmbeddedCovergroupTypes) {
  MLIRContext *context = module.getContext();
  PreparedClassDeclarations result;
  // Inventory classes and detect the feature in one traversal. Designs that
  // do not use class bit-stream casts retain their previous declaration walk
  // and receive no extra field metadata.
  bool needsClassBitstreamMetadata = false;
  semanticRoot->walk<WalkOrder::PreOrder>([&](Operation *operation) {
    if (auto classType = dyn_cast<semantic::SVClassTypeOp>(operation)) {
      if (classType.getIsUninstantiated())
        return WalkResult::skip();
      result.sources.push_back(classType);
      return WalkResult::advance();
    }
    if (auto streaming =
            dyn_cast<semantic::SVStreamingConcatenationExpressionOp>(
                operation)) {
      SmallVector<Operation *> children = getChildren(streaming);
      size_t next = 0;
      for (int64_t withFlag : streaming.getStreamWithFlags()) {
        if (next >= children.size())
          break;
        Operation *child = children[next++];
        auto semanticSource = child->getAttrOfType<TypeAttr>("semantic_type");
        if (semanticSource &&
            isa<semantic::VoidType>(semanticSource.getValue())) {
          if (withFlag != 0)
            ++next;
          continue;
        }
        FailureOr<Type> source = getNormalizedSemanticType(child);
        bool containsClass =
            succeeded(source) && containsClassBitstreamSource(*source);
        needsClassBitstreamMetadata |= containsClass;
        if (containsClass && isExactCurrentInstanceThis(child))
          streaming->setAttr(sim::metadata::classBitstreamAllowHiddenRoot,
                             UnitAttr::get(context));
        if (withFlag != 0)
          ++next;
      }
      return WalkResult::advance();
    }
    auto conversion = dyn_cast<semantic::SVConversionExpressionOp>(operation);
    if (!conversion)
      return WalkResult::advance();
    BoolAttr implicit = conversion->getAttrOfType<BoolAttr>("is_implicit");
    if (!implicit || implicit.getValue())
      return WalkResult::advance();
    SmallVector<Operation *> children = getChildren(conversion);
    if (children.size() != 1)
      return WalkResult::advance();
    // IEEE 1800-2017 13.4.1 permits an explicit cast to void to discard a
    // function result. This inventory walk only detects class bit-stream
    // casts, so a void target is unrelated and must not be normalized as a
    // simulation value type.
    auto semanticTarget = conversion->getAttrOfType<TypeAttr>("semantic_type");
    if (semanticTarget && isa<semantic::VoidType>(semanticTarget.getValue()))
      return WalkResult::advance();
    FailureOr<Type> target = getNormalizedSemanticType(conversion);
    if (failed(target) ||
        (!sim::getPackedScalarType(*target) &&
         !isa<sim::StringType, sim::DynamicArrayType, sim::QueueType,
              sim::UnpackedArrayType, sim::UnpackedStructType>(*target)))
      return WalkResult::advance();
    auto semanticSource =
        children.front()->getAttrOfType<TypeAttr>("semantic_type");
    if (semanticSource && isa<semantic::VoidType>(semanticSource.getValue()))
      return WalkResult::advance();
    FailureOr<Type> source = getNormalizedSemanticType(children.front());
    bool containsClass =
        succeeded(source) && containsClassBitstreamSource(*source);
    needsClassBitstreamMetadata |= containsClass;
    if (containsClass && isExactCurrentInstanceThis(children.front()))
      conversion->setAttr(sim::metadata::classBitstreamAllowHiddenRoot,
                          UnitAttr::get(context));
    return WalkResult::advance();
  });
  if (needsClassBitstreamMetadata)
    module->setAttr(sim::metadata::classBitstreamSourceFeature,
                    UnitAttr::get(context));
  if (needsClassBitstreamMetadata) {
    llvm::DenseMap<Type, uint64_t> fixedClassWidths;
    for (semantic::SVClassTypeOp classType : result.sources)
      if (classType.getBitstreamWidth() != 0)
        fixedClassWidths.try_emplace(classType.getSemanticType(),
                                     classType.getBitstreamWidth());
    semanticRoot->walk([&](Operation *operation) {
      auto semanticType = operation->getAttrOfType<TypeAttr>("semantic_type");
      auto found = semanticType ? fixedClassWidths.find(semanticType.getValue())
                                : fixedClassWidths.end();
      if (found != fixedClassWidths.end())
        operation->setAttr("simulation.class_bitstream_width",
                           builder.getI64IntegerAttr(found->second));
    });
  }
  // The IEEE weak_reference specializations live in the standard package,
  // outside the elaborated source root, but their handles can occur in source
  // storage and function signatures.
  module.walk([&](semantic::SVClassTypeOp classType) {
    if (!classType.getIsUninstantiated() && isWeakReferenceClass(classType) &&
        !llvm::is_contained(result.sources, classType))
      result.sources.push_back(classType);
  });
  llvm::sort(result.sources,
             [](semantic::SVClassTypeOp lhs, semantic::SVClassTypeOp rhs) {
               return std::tuple(getHierarchyName(lhs), lhs.getSymName()) <
                      std::tuple(getHierarchyName(rhs), rhs.getSymName());
             });

  llvm::DenseMap<Operation *, uint64_t> classIDs;
  for (auto [index, classType] : llvm::enumerate(result.sources))
    classIDs[classType] = index + 1;
  for (semantic::SVClassTypeOp classType : result.sources) {
    auto handle = cast<semantic::ClassHandleType>(classType.getSemanticType());
    result.semanticClasses[handle.getClassName().getLeafReference()] =
        classType;
  }

  // rand_mode property indices are defined over the effective base-to-derived
  // property sequence. Freeze the same stable index on each instance field so
  // later ABI preparation can describe direct rand-object edges without
  // retaining the semantic class tree.
  llvm::DenseMap<Operation *, uint64_t> randomPropertyCounts;
  llvm::SmallPtrSet<Operation *, 8> countingRandomProperties;
  std::function<FailureOr<uint64_t>(semantic::SVClassTypeOp)>
      countRandomProperties =
          [&](semantic::SVClassTypeOp classType) -> FailureOr<uint64_t> {
    if (auto found = randomPropertyCounts.find(classType);
        found != randomPropertyCounts.end())
      return found->second;
    if (!countingRandomProperties.insert(classType).second)
      return failure();
    uint64_t count = 0;
    if (std::optional<Type> baseType = classType.getBaseClass()) {
      auto baseHandle = dyn_cast<semantic::ClassHandleType>(*baseType);
      auto base = baseHandle ? result.semanticClasses.find(
                                   baseHandle.getClassName().getLeafReference())
                             : result.semanticClasses.end();
      if (base == result.semanticClasses.end())
        return failure();
      FailureOr<uint64_t> baseCount = countRandomProperties(base->second);
      if (failed(baseCount))
        return failure();
      count = *baseCount;
    }
    for (Operation *child : getChildren(classType))
      if (auto property = dyn_cast<semantic::SVClassPropertySymbolOp>(child))
        if (property.getRandMode() != semantic::SVRandMode::None)
          ++count;
    countingRandomProperties.erase(classType);
    randomPropertyCounts[classType] = count;
    return count;
  };

  auto classReference = [&](Type type) -> FlatSymbolRefAttr {
    auto handle = dyn_cast<semantic::ClassHandleType>(type);
    return handle ? FlatSymbolRefAttr::get(
                        getSimulationClassSymbol(handle.getClassName()))
                  : FlatSymbolRefAttr{};
  };

  llvm::DenseMap<Operation *, Operation *> inheritedCovergroupProperties;
  llvm::StringMap<semantic::SVCovergroupTypeOp> covergroupsBySymbol;
  semanticRoot->walk([&](semantic::SVCovergroupTypeOp covergroup) {
    auto handle =
        dyn_cast<semantic::CovergroupHandleType>(covergroup.getSemanticType());
    if (handle)
      covergroupsBySymbol[handle.getCovergroupName().getLeafReference()] =
          covergroup;
  });
  auto findCovergroupProperty = [](semantic::SVCovergroupTypeOp covergroup)
      -> semantic::SVClassPropertySymbolOp {
    auto owner = covergroup->getParentOfType<semantic::SVClassTypeOp>();
    if (!owner)
      return {};
    for (Operation *child : getChildren(owner))
      if (auto property = dyn_cast<semantic::SVClassPropertySymbolOp>(child))
        if (property.getSemanticType() == covergroup.getSemanticType())
          return property;
    return {};
  };
  for (const auto &entry : covergroupsBySymbol) {
    semantic::SVCovergroupTypeOp derived = entry.second;
    TypeAttr baseAttr =
        derived->getAttrOfType<TypeAttr>("obelisk.coverage.base_group");
    if (!baseAttr)
      continue;
    auto baseHandle =
        dyn_cast<semantic::CovergroupHandleType>(baseAttr.getValue());
    auto base = baseHandle
                    ? covergroupsBySymbol.find(
                          baseHandle.getCovergroupName().getLeafReference())
                    : covergroupsBySymbol.end();
    auto derivedProperty = findCovergroupProperty(derived);
    auto baseProperty = base == covergroupsBySymbol.end()
                            ? semantic::SVClassPropertySymbolOp{}
                            : findCovergroupProperty(base->second);
    if (!derivedProperty || !baseProperty) {
      emitError(getSemanticLocation(derived))
          << "embedded covergroup inheritance has no corresponding class "
             "property";
      return failure();
    }
    inheritedCovergroupProperties[derivedProperty] = baseProperty;
  }

  bool invalid = false;
  llvm::DenseMap<Operation *, sim::SimClassFieldDeclOp> fieldDeclarations;
  for (semantic::SVClassTypeOp classType : result.sources) {
    uint64_t randomPropertyIndex = 0;
    if (std::optional<Type> baseType = classType.getBaseClass()) {
      auto baseHandle = dyn_cast<semantic::ClassHandleType>(*baseType);
      auto base = baseHandle ? result.semanticClasses.find(
                                   baseHandle.getClassName().getLeafReference())
                             : result.semanticClasses.end();
      FailureOr<uint64_t> baseCount = base == result.semanticClasses.end()
                                          ? FailureOr<uint64_t>(failure())
                                          : countRandomProperties(base->second);
      if (failed(baseCount)) {
        emitError(getSemanticLocation(classType))
            << "cannot determine inherited rand_mode property indices";
        invalid = true;
      } else {
        randomPropertyIndex = *baseCount;
      }
    }
    FlatSymbolRefAttr base;
    if (std::optional<Type> baseType = classType.getBaseClass()) {
      base = classReference(*baseType);
      if (!base) {
        emitError(getSemanticLocation(classType))
            << "class base is not a class handle";
        invalid = true;
      }
    }
    SmallVector<Attribute> interfaces;
    for (Attribute attribute : classType.getImplementedInterfaces()) {
      auto type = dyn_cast<TypeAttr>(attribute);
      FlatSymbolRefAttr interface =
          type ? classReference(type.getValue()) : FlatSymbolRefAttr{};
      if (!interface) {
        emitError(getSemanticLocation(classType))
            << "implemented interface is not a class handle";
        invalid = true;
        continue;
      }
      interfaces.push_back(interface);
    }
    auto semanticClassType =
        cast<semantic::ClassHandleType>(classType.getSemanticType());
    StringAttr classSymbol =
        getSimulationClassSymbol(semanticClassType.getClassName());
    result.symbols[classType] = classSymbol;
    FlatSymbolRefAttr weakReferent;
    if (isWeakReferenceClass(classType))
      for (Operation *child : getChildren(classType))
        if (auto parameter =
                dyn_cast<semantic::SVTypeParameterSymbolOp>(child)) {
          if (auto type = parameter->getAttrOfType<TypeAttr>("semantic_type"))
            weakReferent = classReference(type.getValue());
          break;
        }
    bool hasExplicitConstructor =
        llvm::any_of(getChildren(classType), [](Operation *child) {
          semantic::SVSubroutineSymbolOp method = getClassMethod(child);
          return method && method.getIsConstructor().value_or(false);
        });
    FlatSymbolRefAttr implicitConstructor;
    if (!hasExplicitConstructor && !classType.getIsInterface())
      implicitConstructor = FlatSymbolRefAttr::get(
          context, (classSymbol.getValue() + "_implicit_new").str());
    auto declaration = sim::SimClassDeclOp::create(
        builder, getSemanticLocation(classType), classSymbol,
        classIDs.lookup(classType), base,
        interfaces.empty() ? ArrayAttr{} : builder.getArrayAttr(interfaces),
        weakReferent, ArrayAttr{}, FlatSymbolRefAttr{}, implicitConstructor,
        classType.getIsAbstract() || classType.getIsInterface(),
        classType.getIsInterface(), classType.getIsFinal(),
        builder.getStringAttr(getDebugName(classType)));
    result.declarations[classType] = declaration;
    // Class inventory is part of the executable ABI. Keep descriptors even
    // when the only current reference is embedded in a type.
    SymbolTable::setSymbolVisibility(declaration,
                                     SymbolTable::Visibility::Public);
    if (implicitConstructor)
      result.implicitConstructorSymbols[classType] = implicitConstructor;

    uint64_t ordinal = 0;
    for (Operation *child : getChildren(classType)) {
      auto property = dyn_cast<semantic::SVClassPropertySymbolOp>(child);
      if (!property)
        continue;
      if (inheritedCovergroupProperties.contains(property))
        continue;
      if (std::optional<Type> type = property.getSemanticType();
          type && unusedEmbeddedCovergroupTypes.contains(*type))
        continue;
      FailureOr<Type> type = getNormalizedSemanticType(property);
      if (failed(type)) {
        invalid = true;
        continue;
      }
      bool isStatic =
          property.getLifetime() == semantic::SVVariableLifetime::Static;
      std::string fieldName =
          (classSymbol.getValue() + "_field_" + llvm::Twine(ordinal)).str();
      FlatSymbolRefAttr fieldSymbol =
          FlatSymbolRefAttr::get(context, fieldName);
      result.fieldSymbols[property] = fieldSymbol;
      auto field = sim::SimClassFieldDeclOp::create(
          builder, getSemanticLocation(property), fieldName, classSymbol, *type,
          ordinal++, IntegerAttr{}, isStatic,
          /*isWeak=*/false, builder.getStringAttr(getDebugName(property)));
      // IEEE 1800-2023 6.24.3 and 11.4.14.1 stream the data members of the
      // referenced object. Static properties have one class-wide copy (8.9),
      // so they are not object members. Keep the exact source-property marker
      // separate from the physical class layout: synthetic RNG/rand_mode
      // fields must never leak into a language bit stream either.
      if (needsClassBitstreamMetadata && !isStatic) {
        field->setAttr(sim::metadata::classBitstreamMember,
                       builder.getUnitAttr());
        field->setAttr(
            sim::metadata::classBitstreamVisibility,
            sim::MemberVisibilityAttr::get(
                builder.getContext(), static_cast<sim::MemberVisibility>(
                                          property.getMemberVisibility())));
      }
      fieldDeclarations[property] = field;
      if (property.getRandMode() != semantic::SVRandMode::None) {
        field->setAttr(sim::metadata::randomModeIndex,
                       builder.getI64IntegerAttr(randomPropertyIndex++));
        if (!isStatic && isa<sim::ClassHandleType>(*type))
          field->setAttr(sim::metadata::randomObjectEdge,
                         builder.getUnitAttr());
        if (!isStatic && sim::getPackedWidth(*type)) {
          sim::RandomVariableKind kind = sim::RandomVariableKind::Rand;
          switch (property.getRandMode()) {
          case semantic::SVRandMode::Rand:
            kind = sim::RandomVariableKind::Rand;
            break;
          case semantic::SVRandMode::RandC:
            kind = sim::RandomVariableKind::RandC;
            break;
          case semantic::SVRandMode::None:
            llvm_unreachable("non-random property reached random metadata");
          }
          field->setAttr(sim::metadata::randomVariableKind,
                         sim::RandomVariableKindAttr::get(context, kind));
          std::optional<Type> semanticType = property.getSemanticType();
          if (!semanticType) {
            emitError(getSemanticLocation(property))
                << "random property has no semantic type";
            invalid = true;
          } else {
            field->setAttr(
                sim::metadata::randomVariableSigned,
                builder.getBoolAttr(isSignedSemanticType(*semanticType)));
          }
        }
      }
      if (!isStatic && property.getRandMode() == semantic::SVRandMode::RandC) {
        auto addRandCField = [&](StringRef suffix, StringRef debugName) {
          std::string name = (fieldName + suffix).str();
          FlatSymbolRefAttr symbol = FlatSymbolRefAttr::get(context, name);
          sim::SimClassFieldDeclOp::create(
              builder, getSemanticLocation(property), name, classSymbol,
              builder.getI64Type(), ordinal++, IntegerAttr{},
              /*isStatic=*/false, /*isWeak=*/false,
              builder.getStringAttr(debugName));
          return symbol;
        };
        result.randcKeyFieldSymbols[property] =
            addRandCField("_randc_key", "__obelisk_randc_key");
        result.randcPositionFieldSymbols[property] =
            addRandCField("_randc_position", "__obelisk_randc_position");
        if (field->hasAttr(sim::metadata::randomVariableKind)) {
          field->setAttr(sim::metadata::randomCycleKeyField,
                         result.randcKeyFieldSymbols.lookup(property));
          field->setAttr(sim::metadata::randomCyclePositionField,
                         result.randcPositionFieldSymbols.lookup(property));
        }
      }
    }
    // Every inheritance tree owns exactly one inline PCG stream.
    if (!base && !classType.getIsInterface()) {
      auto addRandomField = [&](StringRef suffix, StringRef debugName) {
        std::string name = (classSymbol.getValue() + "_field_" + suffix).str();
        FlatSymbolRefAttr symbol = FlatSymbolRefAttr::get(context, name);
        sim::SimClassFieldDeclOp::create(
            builder, getSemanticLocation(classType), name, classSymbol,
            builder.getI64Type(), ordinal++, IntegerAttr{},
            /*isStatic=*/false, /*isWeak=*/false,
            builder.getStringAttr(debugName));
        return symbol;
      };
      declaration->setAttr(
          "simulation.random_state_field",
          addRandomField("__obelisk_rng_state", "__obelisk_rng_state"));
      declaration->setAttr(
          "simulation.random_increment_field",
          addRandomField("__obelisk_rng_increment", "__obelisk_rng_increment"));
      declaration->setAttr(
          sim::metadata::randomModeField,
          addRandomField("__obelisk_rand_mode", "__obelisk_rand_mode"));
      declaration->setAttr("simulation.constraint_mode_field",
                           addRandomField("__obelisk_constraint_mode",
                                          "__obelisk_constraint_mode"));
    }
  }

  for (const auto &[derived, directBase] : inheritedCovergroupProperties) {
    Operation *storage = directBase;
    llvm::SmallPtrSet<Operation *, 8> visited;
    while (true) {
      auto found = inheritedCovergroupProperties.find(storage);
      if (found == inheritedCovergroupProperties.end())
        break;
      if (!visited.insert(storage).second) {
        emitError(getSemanticLocation(derived))
            << "embedded covergroup property inheritance contains a cycle";
        invalid = true;
        storage = nullptr;
        break;
      }
      storage = found->second;
    }
    if (!storage || !result.fieldSymbols.count(storage)) {
      emitError(getSemanticLocation(derived))
          << "inherited covergroup property has no base storage field";
      invalid = true;
      continue;
    }
    FailureOr<Type> storageType = getNormalizedSemanticType(storage);
    if (failed(storageType)) {
      invalid = true;
      continue;
    }
    result.fieldSymbols[derived] = result.fieldSymbols.lookup(storage);
    result.covergroupFieldStorageTypes[derived] = *storageType;
  }

  // Freeze the effective base-to-derived packed random-variable inventory on
  // each exact class descriptor.  These remain symbolic field references:
  // native and bytecode layouts are deliberately unavailable at preparation.
  // Rand object handles and containers have separate graph/container
  // descriptors and therefore do not masquerade as scalar assignment slots.
  llvm::DenseMap<Operation *, SmallVector<sim::RandomVariableReferenceAttr>>
      randomVariableReferences;
  llvm::SmallPtrSet<Operation *, 8> collectingRandomVariableReferences;
  std::function<LogicalResult(semantic::SVClassTypeOp)>
      collectRandomVariableReferences =
          [&](semantic::SVClassTypeOp classType) -> LogicalResult {
    if (randomVariableReferences.count(classType))
      return success();
    if (!collectingRandomVariableReferences.insert(classType).second)
      return failure();

    SmallVector<sim::RandomVariableReferenceAttr> references;
    if (std::optional<Type> baseType = classType.getBaseClass()) {
      auto baseHandle = dyn_cast<semantic::ClassHandleType>(*baseType);
      auto base = baseHandle ? result.semanticClasses.find(
                                   baseHandle.getClassName().getLeafReference())
                             : result.semanticClasses.end();
      if (base == result.semanticClasses.end() ||
          failed(collectRandomVariableReferences(base->second)))
        return failure();
      llvm::append_range(references,
                         randomVariableReferences.find(base->second)->second);
    }
    for (Operation *child : getChildren(classType)) {
      auto property = dyn_cast<semantic::SVClassPropertySymbolOp>(child);
      sim::SimClassFieldDeclOp field = fieldDeclarations.lookup(child);
      if (!property || !field ||
          !field->hasAttr(sim::metadata::randomVariableKind))
        continue;
      references.push_back(sim::RandomVariableReferenceAttr::get(
          context, {}, result.fieldSymbols.lookup(property)));
    }
    collectingRandomVariableReferences.erase(classType);
    randomVariableReferences[classType] = std::move(references);
    return success();
  };
  for (semantic::SVClassTypeOp classType : result.sources) {
    if (failed(collectRandomVariableReferences(classType))) {
      emitError(getSemanticLocation(classType))
          << "cannot determine inherited random-variable references";
      invalid = true;
      continue;
    }
    ArrayRef<sim::RandomVariableReferenceAttr> references =
        randomVariableReferences.find(classType)->second;
    if (!references.empty()) {
      SmallVector<Attribute> attributes = llvm::map_to_vector(
          references, [](sim::RandomVariableReferenceAttr reference) {
            return Attribute(reference);
          });
      result.declarations.lookup(classType).setRandomVariableReferencesAttr(
          builder.getArrayAttr(attributes));
    }
  }

  llvm::DenseMap<Operation *, uint64_t> classVirtualCounts;
  llvm::SmallPtrSet<Operation *, 8> assigningClasses;
  std::function<LogicalResult(semantic::SVClassTypeOp)> assignVirtualSlots =
      [&](semantic::SVClassTypeOp classType) -> LogicalResult {
    if (classVirtualCounts.count(classType))
      return success();
    if (!assigningClasses.insert(classType).second)
      return classType.emitError("class inheritance contains a cycle");
    uint64_t nextSlot = 0;
    if (std::optional<Type> baseType = classType.getBaseClass()) {
      auto baseHandle = dyn_cast<semantic::ClassHandleType>(*baseType);
      auto base = baseHandle ? result.semanticClasses.find(
                                   baseHandle.getClassName().getLeafReference())
                             : result.semanticClasses.end();
      if (base == result.semanticClasses.end() ||
          failed(assignVirtualSlots(base->second)))
        return failure();
      nextSlot = classVirtualCounts.lookup(base->second);
    }
    uint64_t methodOrdinal = 0;
    uint64_t interfaceMethodOrdinal = 0;
    for (Operation *child : getChildren(classType)) {
      auto method = getClassMethod(child);
      if (!method || method.getIsBuiltin().value_or(false))
        continue;
      std::string methodName = (result.symbols.lookup(classType).getValue() +
                                "_method_" + llvm::Twine(methodOrdinal++))
                                   .str();
      result.methodSymbols[method] =
          FlatSymbolRefAttr::get(context, methodName);
      if (!method.getIsVirtual().value_or(false))
        continue;
      if (classType.getIsInterface()) {
        result.virtualMethodSlots[method] = UINT32_MAX;
        result.virtualMethodSignatures[method] =
            getVirtualMethodSignatureID(method);
        result.interfaceMethodOrdinals[method] = interfaceMethodOrdinal++;
        continue;
      }
      std::optional<SymbolRefAttr> overridden = method.getOverrideSymbol();
      // For extern/out-of-block class methods, Slang attaches the override
      // relationship to the method prototype while getClassMethod() returns
      // its nested executable subroutine. Preserve the inherited vtable slot
      // instead of accidentally appending a second, concrete slot and leaving
      // the pure base entry effective.
      if (!overridden)
        if (auto prototype =
                dyn_cast<semantic::SVMethodPrototypeSymbolOp>(child))
          overridden = prototype.getOverrideSymbol();
      if (overridden) {
        auto target = semanticSymbols.find(overridden->getLeafReference());
        if (target == semanticSymbols.end() ||
            !result.virtualMethodSlots.count(target->second)) {
          method.emitError(
              "virtual override does not resolve to an inherited slot");
          return failure();
        }
        uint64_t inheritedSlot =
            result.virtualMethodSlots.lookup(target->second);
        result.virtualMethodSlots[method] =
            inheritedSlot == UINT32_MAX ? nextSlot++ : inheritedSlot;
        result.virtualMethodSignatures[method] =
            result.virtualMethodSignatures.lookup(target->second);
      } else {
        result.virtualMethodSlots[method] = nextSlot++;
        result.virtualMethodSignatures[method] =
            getVirtualMethodSignatureID(method);
      }
      nextSlot =
          std::max(nextSlot, result.virtualMethodSlots.lookup(method) + 1);
    }
    assigningClasses.erase(classType);
    classVirtualCounts[classType] = nextSlot;
    return success();
  };
  for (semantic::SVClassTypeOp classType : result.sources)
    if (failed(assignVirtualSlots(classType)))
      invalid = true;

  SymbolTable classTable(design);
  for (semantic::SVClassTypeOp classType : result.sources) {
    auto semanticClassType =
        cast<semantic::ClassHandleType>(classType.getSemanticType());
    StringAttr classSymbol =
        getSimulationClassSymbol(semanticClassType.getClassName());
    if (!classTable.lookup(classSymbol)) {
      emitError(getSemanticLocation(classType))
          << "internal error: flattened class symbol was not inserted";
      invalid = true;
    }
  }
  if (invalid)
    return failure();
  return result;
}

uint64_t PreparedScopeDeclarations::lookup(Operation *operation) const {
  for (Operation *cursor = operation; cursor; cursor = cursor->getParentOp())
    if (auto found = ids.find(cursor); found != ids.end())
      return found->second;
  return 0;
}

FailureOr<PreparedScopeDeclarations> materializeScopeDeclarations(
    semantic::SVRootSymbolOp semanticRoot, ArrayRef<Operation *> units,
    uint64_t designPrecisionFemtoseconds, OpBuilder &builder,
    const llvm::StringMap<Operation *> &semanticSymbols) {
  PreparedScopeDeclarations result;
  bool invalid = false;
  uint64_t nextScopeId = 0;
  result.ids[semanticRoot] = nextScopeId;
  result.declarations.push_back(sim::SimScopeDeclOp::create(
      builder, getSemanticLocation(semanticRoot), nextScopeId++, IntegerAttr{},
      builder.getStringAttr(getHierarchyName(semanticRoot)),
      builder.getStringAttr(getDebugName(semanticRoot)), StringAttr{},
      IntegerAttr{}, StringAttr{}, IntegerAttr{}, FlatSymbolRefAttr{},
      FlatSymbolRefAttr{}));
  llvm::DenseMap<Operation *, std::pair<FlatSymbolRefAttr, StringAttr>>
      definitionSymbols;
  llvm::StringMap<semantic::SVDefinitionSymbolOp> definitionsBySymbol;
  if (ModuleOp module = semanticRoot->getParentOfType<ModuleOp>())
    module.walk([&](semantic::SVDefinitionSymbolOp definition) {
      definitionsBySymbol.try_emplace(definition.getSymName(), definition);
    });
  uint64_t nextDefinitionId = 0;
  auto materializeDefinition =
      [&](semantic::SVInstanceBodySymbolOp body) -> FlatSymbolRefAttr {
    auto instance = dyn_cast<semantic::SVInstanceSymbolOp>(body->getParentOp());
    if (!instance)
      return {};
    SymbolRefAttr reference = instance.getReferencedSymbolAttr();
    if (!reference)
      return {};
    semantic::SVDefinitionSymbolOp definition =
        SymbolTable::lookupNearestSymbolFrom<semantic::SVDefinitionSymbolOp>(
            instance, reference);
    if (!definition) {
      auto found = definitionsBySymbol.find(reference.getLeafReference());
      if (found != definitionsBySymbol.end())
        definition = found->second;
    }
    if (!definition)
      return {};
    StringAttr definitionName =
        body->getAttrOfType<StringAttr>("simulation.vpi_definition_name");
    if (!definitionName)
      definitionName = definition->getAttrOfType<StringAttr>("name");
    if (!definitionName)
      definitionName = definition.getSymNameAttr();
    auto existing = definitionSymbols.find(definition);
    if (existing != definitionSymbols.end()) {
      if (existing->second.second != definitionName) {
        emitError(getSemanticLocation(body))
            << "instances of one source definition disagree on the VPI "
               "definition name";
        invalid = true;
      }
      return existing->second.first;
    }

    uint32_t vpiKind = 0;
    switch (definition.getDefinitionKind()) {
    case semantic::SVDefinitionKind::Module:
      vpiKind =
          static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::Module);
      break;
    case semantic::SVDefinitionKind::Interface:
      vpiKind =
          static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::Interface);
      break;
    case semantic::SVDefinitionKind::Program:
      vpiKind =
          static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::Program);
      break;
    }
    std::string symbol =
        (Twine("__obelisk_vpi_definition_") + Twine(nextDefinitionId++)).str();
    Location location = getSemanticLocation(definition);
    LocationAttr definitionLoc;
    if (auto file = location->findInstanceOf<FileLineColLoc>())
      definitionLoc = file;
    sim::SimVPIDefinitionDeclOp::create(builder, location, symbol, vpiKind,
                                        definitionName.getValue(),
                                        definitionLoc);
    FlatSymbolRefAttr result =
        FlatSymbolRefAttr::get(builder.getContext(), symbol);
    definitionSymbols[definition] = {result, definitionName};
    return result;
  };
  semanticRoot->walk<WalkOrder::PreOrder>([&](semantic::SVInstanceBodySymbolOp
                                                  body) {
    Operation *parent = body->getParentOp();
    while (parent && !result.ids.count(parent))
      parent = parent->getParentOp();
    uint64_t parentId = parent ? result.ids.lookup(parent) : 0;
    if (isCompileTimeOnlyInstanceMember(body)) {
      result.ids[body] = parentId;
      return;
    }
    uint64_t id = nextScopeId++;
    result.ids[body] = id;
    StringAttr interfaceType;
    if (auto identity =
            body->getAttrOfType<SymbolRefAttr>("virtual_interface_identity")) {
      std::string key;
      llvm::raw_string_ostream stream(key);
      stream << identity;
      interfaceType = builder.getStringAttr(key);
    }
    StringAttr definitionName;
    auto instance = dyn_cast<semantic::SVInstanceSymbolOp>(body->getParentOp());
    if (instance && instance.getReferencedSymbolAttr()) {
      auto found = semanticSymbols.find(
          instance.getReferencedSymbolAttr().getLeafReference());
      auto definition =
          found == semanticSymbols.end()
              ? semantic::SVDefinitionSymbolOp{}
              : dyn_cast<semantic::SVDefinitionSymbolOp>(found->second);
      if (!definition) {
        emitError(getSemanticLocation(instance))
            << "elaborated instance references an unknown definition";
        invalid = true;
      } else if (definition.getDefinitionKind() ==
                 semantic::SVDefinitionKind::Module) {
        std::optional<StringRef> canonicalName = instance.getReferencedPath();
        if (!canonicalName || canonicalName->empty()) {
          emitError(getSemanticLocation(instance))
              << "module instance has no canonical definition name";
          invalid = true;
        } else {
          definitionName = builder.getStringAttr(*canonicalName);
        }
      }
    }
    sim::SimScopeDeclOp declaration = sim::SimScopeDeclOp::create(
        builder, getSemanticLocation(body), id,
        builder.getI64IntegerAttr(parentId),
        builder.getStringAttr(getHierarchyName(body)),
        builder.getStringAttr(getDebugName(body)), definitionName,
        IntegerAttr{}, interfaceType,
        body->getAttrOfType<IntegerAttr>("vpi_scope_kind"),
        materializeDefinition(body), FlatSymbolRefAttr{});
    if (interfaceType) {
      auto parentBody =
          instance
              ? instance->getParentOfType<semantic::SVInstanceBodySymbolOp>()
              : semantic::SVInstanceBodySymbolOp{};
      if (parentBody && parentBody->hasAttr("virtual_interface_identity"))
        declaration->setAttr(virtualInterfaceParentMemberAttrName,
                             builder.getStringAttr(getDebugName(instance)));
    }
    result.declarations.push_back(declaration);
    auto unitAttr = body->getAttrOfType<IntegerAttr>("time_unit_fs");
    auto precisionAttr = body->getAttrOfType<IntegerAttr>("time_precision_fs");
    if (bool(unitAttr) != bool(precisionAttr)) {
      emitError(getSemanticLocation(body))
          << "elaborated scope time scale must specify both unit and "
             "precision";
      invalid = true;
      return;
    }
    if (!unitAttr)
      return;
    APInt unit = unitAttr.getValue();
    APInt precision = precisionAttr.getValue();
    if (unit.isNegative() || precision.isNegative() ||
        unit.getActiveBits() > 64 || precision.getActiveBits() > 64) {
      emitError(getSemanticLocation(body))
          << "elaborated scope time scale does not fit an unsigned "
             "64-bit value";
      invalid = true;
      return;
    }
    uint64_t unitFs = unit.getZExtValue();
    uint64_t precisionFs = precision.getZExtValue();
    if (unitFs == 0 || precisionFs == 0 || unitFs < precisionFs ||
        unitFs % precisionFs != 0) {
      emitError(getSemanticLocation(body))
          << "invalid elaborated scope time scale " << unitFs << "fs/"
          << precisionFs << "fs";
      invalid = true;
      return;
    }
    declaration->setAttr("dpi_unit_femtoseconds",
                         builder.getI64IntegerAttr(unitFs));
    declaration->setAttr("dpi_precision_femtoseconds",
                         builder.getI64IntegerAttr(precisionFs));
  });

  for (Operation *unit : units) {
    uint64_t scopeID = result.lookup(unit);
    if (scopeID >= result.declarations.size())
      continue;
    auto unitAttr = unit->getAttrOfType<IntegerAttr>("time_unit_fs");
    auto precisionAttr = unit->getAttrOfType<IntegerAttr>("time_precision_fs");
    if (bool(unitAttr) != bool(precisionAttr)) {
      emitError(getSemanticLocation(unit))
          << "elaborated time scale must specify both unit and precision";
      invalid = true;
      continue;
    }
    // Synthetic units inherit their containing scope's time scale. Leaving
    // both fields unset here lets a real elaborated declaration establish it,
    // independent of traversal order.
    if (!unitAttr && !precisionAttr)
      continue;
    uint64_t unitFs = unitAttr.getValue().getZExtValue();
    uint64_t precisionFs = precisionAttr.getValue().getZExtValue();
    sim::SimScopeDeclOp declaration = result.declarations[scopeID];
    if (auto existing =
            declaration->getAttrOfType<IntegerAttr>("dpi_unit_femtoseconds");
        existing && existing.getValue().getZExtValue() != unitFs) {
      emitError(getSemanticLocation(unit))
          << "simulation scope has inconsistent time units";
      invalid = true;
      continue;
    }
    if (auto existing = declaration->getAttrOfType<IntegerAttr>(
            "dpi_precision_femtoseconds");
        existing && existing.getValue().getZExtValue() != precisionFs) {
      emitError(getSemanticLocation(unit))
          << "simulation scope has inconsistent time precisions";
      invalid = true;
      continue;
    }
    declaration->setAttr("dpi_unit_femtoseconds",
                         builder.getI64IntegerAttr(unitFs));
    declaration->setAttr("dpi_precision_femtoseconds",
                         builder.getI64IntegerAttr(precisionFs));
  }
  for (sim::SimScopeDeclOp declaration : result.declarations) {
    sim::SimScopeDeclOp parent;
    if (auto parentID = declaration.getParent();
        parentID && *parentID < result.declarations.size())
      parent = result.declarations[*parentID];
    auto inherited = [&](StringRef name) -> IntegerAttr {
      if (parent)
        if (auto value = parent->getAttrOfType<IntegerAttr>(name))
          return value;
      return builder.getI64IntegerAttr(designPrecisionFemtoseconds);
    };
    if (!declaration->hasAttr("dpi_unit_femtoseconds"))
      declaration->setAttr("dpi_unit_femtoseconds",
                           inherited("dpi_unit_femtoseconds"));
    if (!declaration->hasAttr("dpi_precision_femtoseconds"))
      declaration->setAttr("dpi_precision_femtoseconds",
                           inherited("dpi_precision_femtoseconds"));
  }
  if (invalid)
    return failure();
  return result;
}

} // namespace obelisk::simlowering
