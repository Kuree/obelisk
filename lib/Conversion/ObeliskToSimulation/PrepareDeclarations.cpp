//===- PrepareDeclarations.cpp - Executable declaration planning ---------===//
//
// Freezes covergroup and class metadata into deterministic executable
// declarations before individual code units are isolated.
//
//===----------------------------------------------------------------------===//

#include "PrepareDeclarations.h"

#include "Detail.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"

#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Twine.h"

#include <functional>

using namespace mlir;

namespace obelisk::simlowering {
namespace {

bool isWeakReferenceClass(semantic::SVClassTypeOp classType) {
  return getHierarchyName(classType).starts_with("std::weak_reference#(");
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
materializeCovergroupDeclarations(semantic::SVRootSymbolOp semanticRoot,
                                  OpBuilder &builder) {
  SmallVector<semantic::SVCovergroupTypeOp> covergroupSources;
  semanticRoot->walk([&](semantic::SVCovergroupTypeOp covergroup) {
    covergroupSources.push_back(covergroup);
  });
  llvm::sort(covergroupSources, [](semantic::SVCovergroupTypeOp lhs,
                                   semantic::SVCovergroupTypeOp rhs) {
    return std::tuple(getHierarchyName(lhs), lhs.getSymName()) <
           std::tuple(getHierarchyName(rhs), rhs.getSymName());
  });

  bool invalid = false;
  for (auto [index, covergroup] : llvm::enumerate(covergroupSources)) {
    auto handle =
        dyn_cast<semantic::CovergroupHandleType>(covergroup.getSemanticType());
    if (!handle) {
      emitError(getSemanticLocation(covergroup))
          << "covergroup declaration has no handle type";
      invalid = true;
      continue;
    }
    SmallVector<int64_t> coverpointBins;
    for (Operation *child : getChildren(covergroup)) {
      auto body = dyn_cast<semantic::SVCovergroupBodySymbolOp>(child);
      if (!body)
        continue;
      for (Operation *member : getChildren(body)) {
        if (auto coverpoint =
                dyn_cast<semantic::SVCoverpointSymbolOp>(member)) {
          int64_t bins =
              llvm::count_if(getChildren(coverpoint), [](Operation *candidate) {
                return isa<semantic::SVCoverageBinSymbolOp>(candidate);
              });
          coverpointBins.push_back(bins);
        }
      }
    }
    if (coverpointBins.empty()) {
      emitError(getSemanticLocation(covergroup))
          << "covergroup requires at least one coverpoint";
      invalid = true;
      continue;
    }
    StringAttr symbol =
        getSimulationCovergroupSymbol(handle.getCovergroupName());
    auto declaration = sim::SimCovergroupDeclOp::create(
        builder, getSemanticLocation(covergroup), symbol, index + 1,
        builder.getDenseI64ArrayAttr(coverpointBins),
        builder.getStringAttr(getDebugName(covergroup)));
    SymbolTable::setSymbolVisibility(declaration,
                                     SymbolTable::Visibility::Public);
  }
  return failure(invalid);
}

FailureOr<PreparedClassDeclarations> materializeClassDeclarations(
    ModuleOp module, sim::SimDesignOp design,
    semantic::SVRootSymbolOp semanticRoot, OpBuilder &builder,
    const llvm::StringMap<Operation *> &semanticSymbols) {
  MLIRContext *context = module.getContext();
  PreparedClassDeclarations result;
  semanticRoot->walk([&](semantic::SVClassTypeOp classType) {
    if (!classType.getIsUninstantiated())
      result.sources.push_back(classType);
  });
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
  bool invalid = false;
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
    auto declaration = sim::SimClassDeclOp::create(
        builder, getSemanticLocation(classType), classSymbol,
        classIDs.lookup(classType), base,
        interfaces.empty() ? ArrayAttr{} : builder.getArrayAttr(interfaces),
        weakReferent, classType.getIsAbstract() || classType.getIsInterface(),
        classType.getIsInterface(), classType.getIsFinal(),
        builder.getStringAttr(getDebugName(classType)));
    // Class inventory is part of the executable ABI. Keep descriptors even
    // when the only current reference is embedded in a type.
    SymbolTable::setSymbolVisibility(declaration,
                                     SymbolTable::Visibility::Public);
    bool hasExplicitConstructor =
        llvm::any_of(getChildren(classType), [](Operation *child) {
          semantic::SVSubroutineSymbolOp method = getClassMethod(child);
          return method && method.getIsConstructor().value_or(false);
        });
    if (!hasExplicitConstructor && !classType.getIsInterface()) {
      FlatSymbolRefAttr constructor = FlatSymbolRefAttr::get(
          context, (classSymbol.getValue() + "_implicit_new").str());
      result.implicitConstructorSymbols[classType] = constructor;
      declaration->setAttr("obelisk_sim.implicit_constructor",
                           builder.getStringAttr(constructor.getValue()));
    }

    uint64_t ordinal = 0;
    for (Operation *child : getChildren(classType)) {
      auto property = dyn_cast<semantic::SVClassPropertySymbolOp>(child);
      if (!property)
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
      if (property.getRandMode() != semantic::SVRandMode::None) {
        field->setAttr(sim::metadata::randomModeIndex,
                       builder.getI64IntegerAttr(randomPropertyIndex++));
        if (!isStatic && isa<sim::ClassHandleType>(*type))
          field->setAttr(sim::metadata::randomObjectEdge,
                         builder.getUnitAttr());
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
          "obelisk_sim.random_state_field",
          addRandomField("__obelisk_rng_state", "__obelisk_rng_state"));
      declaration->setAttr(
          "obelisk_sim.random_increment_field",
          addRandomField("__obelisk_rng_increment", "__obelisk_rng_increment"));
      declaration->setAttr(
          sim::metadata::randomModeField,
          addRandomField("__obelisk_rand_mode", "__obelisk_rand_mode"));
      declaration->setAttr(
          "obelisk_sim.constraint_mode_field",
          addRandomField("__obelisk_constraint_mode",
                         "__obelisk_constraint_mode"));
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
      if (std::optional<SymbolRefAttr> overridden =
              method.getOverrideSymbol()) {
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
    uint64_t designPrecisionFemtoseconds, OpBuilder &builder) {
  PreparedScopeDeclarations result;
  bool invalid = false;
  uint64_t nextScopeId = 0;
  result.ids[semanticRoot] = nextScopeId;
  result.declarations.push_back(sim::SimScopeDeclOp::create(
      builder, getSemanticLocation(semanticRoot), nextScopeId++, IntegerAttr{},
      builder.getStringAttr(getHierarchyName(semanticRoot)),
      builder.getStringAttr(getDebugName(semanticRoot)), StringAttr{}));
  semanticRoot->walk<WalkOrder::PreOrder>(
      [&](semantic::SVInstanceBodySymbolOp body) {
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
        sim::SimScopeDeclOp declaration = sim::SimScopeDeclOp::create(
            builder, getSemanticLocation(body), id,
            builder.getI64IntegerAttr(parentId),
            builder.getStringAttr(getHierarchyName(body)),
            builder.getStringAttr(getDebugName(body)), interfaceType);
        result.declarations.push_back(declaration);
        auto unitAttr = body->getAttrOfType<IntegerAttr>("time_unit_fs");
        auto precisionAttr =
            body->getAttrOfType<IntegerAttr>("time_precision_fs");
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
