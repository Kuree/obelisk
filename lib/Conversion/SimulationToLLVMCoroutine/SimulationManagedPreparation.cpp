//===- SimulationManagedPreparation.cpp - Managed IR preparation -----===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Analysis/ClassDispatchAnalysis.h"
#include "obelisk/Analysis/DescriptorOrdering.h"
#include "obelisk/Analysis/ManagedClassLayoutAnalysis.h"
#include "obelisk/Analysis/NativeStorageLayout.h"
#include "obelisk/Dialect/Native/NativeDialect.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/DescriptorMap.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/PatternMatch.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/DataLayout.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <tuple>

using namespace mlir;

namespace obelisk::detail {

namespace {

struct ManagedTraceLayout {
  uint64_t offset = 0;
  bool weak = false;
  uint32_t slotKind = OBELISK_RT_MANAGED_SLOT_CLASS;
};

struct ManagedRandomEdge {
  uint64_t handleOffset = 0;
  uint64_t modeOffset = 0;
  uint64_t modeMask = 0;
};

struct ManagedRandomVariable {
  uint64_t valueOffset = 0;
  uint64_t modeOffset = 0;
  uint64_t modeMask = 0;
  uint64_t randcKeyOffset = UINT64_MAX;
  uint64_t randcPositionOffset = UINT64_MAX;
  uint32_t bitWidth = 0;
  uint32_t flags = 0;
};

struct ManagedClassLayout {
  struct Interface {
    sim::SimClassDeclOp declaration;
  };

  sim::SimClassDeclOp declaration;
  uint64_t size = sizeof(obelisk_rt_managed_word_v1);
  uint32_t alignment = alignof(obelisk_rt_managed_word_v1);
  SmallVector<ManagedTraceLayout> tracedFields;
  SmallVector<ManagedRandomEdge> randomEdges;
  SmallVector<ManagedRandomVariable> randomVariables;
  // Own instance (offset, size) ranges a shallow copy clears.
  SmallVector<std::pair<uint64_t, uint64_t>> copyResets;
  SmallVector<sim::SimClassMethodDeclOp> methods;
  SmallVector<Interface> interfaces;
};

LogicalResult collectManagedTraceSlots(
    Type type, uint64_t baseBitOffset,
    SmallVectorImpl<std::pair<uint64_t, uint32_t>> &slots) {
  SmallVector<sim::ManagedHandleSlot, 2> managed;
  if (!sim::getManagedHandleSlots(type, managed))
    return failure();
  auto exactKind = [](uint32_t mask) -> std::optional<uint32_t> {
    if (mask == static_cast<uint32_t>(sim::ManagedHandleKind::Class))
      return OBELISK_RT_MANAGED_SLOT_CLASS;
    if (mask == static_cast<uint32_t>(sim::ManagedHandleKind::String))
      return OBELISK_RT_MANAGED_SLOT_STRING;
    if (mask == static_cast<uint32_t>(sim::ManagedHandleKind::Container))
      return OBELISK_RT_MANAGED_SLOT_CONTAINER;
    if (mask == static_cast<uint32_t>(sim::ManagedHandleKind::ReferencePath))
      return OBELISK_RT_MANAGED_SLOT_REFERENCE_PATH;
    return std::nullopt;
  };
  for (const sim::ManagedHandleSlot &slot : managed) {
    if ((slot.bitOffset & 7) != 0 ||
        slot.bitOffset > UINT64_MAX - baseBitOffset)
      return failure();
    uint64_t absolute = baseBitOffset + slot.bitOffset;
    if ((absolute & 7) != 0)
      return failure();
    std::optional<uint32_t> kind = exactKind(slot.kindMask);
    if (!slot.conditional && !kind)
      return failure();
    slots.push_back(
        {absolute / 8, slot.conditional
                           ? OBELISK_RT_MANAGED_SLOT_CANDIDATE | slot.kindMask
                           : *kind});
  }
  return success();
}

LogicalResult
prepareManagedClassInventory(ModuleOp module,
                             const llvm::DataLayout &dataLayout,
                             llvm::StringMap<ManagedClassLayout> &layouts) {
  SmallVector<sim::SimClassDeclOp> classes;
  SmallVector<sim::SimClassMethodDeclOp> methods;
  module.walk([&](sim::SimClassDeclOp op) { classes.push_back(op); });
  module.walk([&](sim::SimClassMethodDeclOp op) { methods.push_back(op); });
  if (classes.empty())
    return success();

  llvm::StringMap<sim::SimClassDeclOp> classesByName;
  llvm::StringMap<SmallVector<sim::SimClassMethodDeclOp>> methodsByOwner;
  for (sim::SimClassDeclOp declaration : classes)
    classesByName[declaration.getSymName()] = declaration;
  for (sim::SimClassMethodDeclOp method : methods)
    methodsByOwner[method.getOwner()].push_back(method);

  FailureOr<analysis::ManagedClassLayoutAnalysis> analyzed =
      analysis::ManagedClassLayoutAnalysis::compute(
          classes.front()->getParentOfType<sim::SimDesignOp>(), dataLayout);
  if (failed(analyzed) ||
      failed(analysis::materializeManagedClassFieldOffsets(*analyzed)))
    return failure();
  llvm::StringMap<uint64_t> fieldOffsets;
  for (const analysis::ManagedClassLayoutAnalysis::Class &shared :
       analyzed->classes)
    for (const analysis::ManagedClassLayoutAnalysis::Field &field :
         shared.fields) {
      sim::SimClassFieldDeclOp declaration = field.declaration;
      fieldOffsets[declaration.getSymName()] = field.offset;
    }

  // The shared layout analysis visits bases before derived classes.
  using SharedClass = analysis::ManagedClassLayoutAnalysis::Class;
  SmallVector<SharedClass *> orderedClasses;
  for (SharedClass &shared : analyzed->classes)
    orderedClasses.push_back(&shared);

  llvm::StringMap<sim::SimClassDeclOp> hierarchyRoots;
  for (SharedClass *sharedPtr : orderedClasses) {
    SharedClass &shared = *sharedPtr;
    sim::SimClassDeclOp declaration = shared.declaration;
    sim::SimClassDeclOp hierarchyRoot = declaration;
    if (auto baseName = declaration.getBase()) {
      auto root = hierarchyRoots.find(*baseName);
      if (root == hierarchyRoots.end())
        return declaration.emitError("managed base hierarchy root is missing");
      hierarchyRoot = root->second;
    }
    hierarchyRoots[declaration.getSymName()] = hierarchyRoot;
    ManagedClassLayout layout;
    layout.declaration = declaration;
    if (auto baseName = declaration.getBase()) {
      auto base = layouts.find(*baseName);
      if (base == layouts.end())
        return declaration.emitError("managed base layout is missing");
    }
    layout.size = shared.size;
    layout.alignment = shared.alignment;
    if (shared.weakReferentOffset)
      layout.tracedFields.push_back(
          {*shared.weakReferentOffset, true, OBELISK_RT_MANAGED_SLOT_CLASS});

    auto getModeOffset =
        [&](sim::SimClassFieldDeclOp field,
            IntegerAttr modeIndex) -> FailureOr<std::pair<uint64_t, uint64_t>> {
      if (!modeIndex || modeIndex.getValue().isNegative() ||
          modeIndex.getValue().getActiveBits() > 6) {
        field.emitError(
            "random property exceeds the 64-property rand_mode boundary");
        return failure();
      }
      auto modeField = sim::get<sim::metadata::RandomModeField>(hierarchyRoot);
      auto modeOffset = modeField ? fieldOffsets.find(modeField.getValue())
                                  : fieldOffsets.end();
      if (!modeField || modeOffset == fieldOffsets.end()) {
        field.emitError("random property has no executable rand_mode field");
        return failure();
      }
      uint64_t index = modeIndex.getValue().getZExtValue();
      return std::pair(modeOffset->second, UINT64_C(1) << index);
    };

    for (const analysis::ManagedClassLayoutAnalysis::Field &sharedField :
         shared.fields) {
      sim::SimClassFieldDeclOp field = sharedField.declaration;
      SmallVector<std::pair<uint64_t, uint32_t>, 2> traceSlots;
      if (failed(collectManagedTraceSlots(field.getType(), 0, traceSlots)) ||
          traceSlots.size() != sharedField.storage.managedRootOffsets.size())
        return field.emitError("class property has no typed managed layout");
      for (auto [rootOffset, slotKind] : traceSlots)
        layout.tracedFields.push_back(
            {sharedField.offset + rootOffset,
             isa<sim::ClassHandleType>(field.getType()) && field.getIsWeak(),
             slotKind});
      if (sim::has<sim::metadata::EmbeddedCovergroup>(field))
        layout.copyResets.push_back(
            {sharedField.offset, sharedField.storage.size});
      if (sim::has<sim::metadata::RandomObjectEdge>(field)) {
        auto modeIndex = sim::get<sim::metadata::RandomModeIndex>(field);
        FailureOr<std::pair<uint64_t, uint64_t>> mode =
            getModeOffset(field, modeIndex);
        if (failed(mode))
          return failure();
        layout.randomEdges.push_back(
            {sharedField.offset, mode->first, mode->second});
      }
      if (auto kind = sim::get<sim::metadata::RandomVariableKind>(field)) {
        auto modeIndex = sim::get<sim::metadata::RandomModeIndex>(field);
        FailureOr<std::pair<uint64_t, uint64_t>> mode =
            getModeOffset(field, modeIndex);
        std::optional<unsigned> width = sim::getPackedWidth(field.getType());
        auto signedness = sim::get<sim::metadata::RandomVariableSigned>(field);
        if (failed(mode))
          return failure();
        if (!width || !signedness)
          return field.emitError(
              "random variable has incomplete executable metadata");
        ManagedRandomVariable variable;
        variable.valueOffset = sharedField.offset;
        variable.modeOffset = mode->first;
        variable.modeMask = mode->second;
        variable.bitWidth = *width;
        if (sharedField.storage.fourState)
          variable.flags |= OBELISK_RT_RANDOM_VARIABLE_FOUR_STATE;
        if (signedness.getValue())
          variable.flags |= OBELISK_RT_RANDOM_VARIABLE_SIGNED;
        if (kind.getValue() == sim::RandomVariableKind::RandC) {
          variable.flags |= OBELISK_RT_RANDOM_VARIABLE_RANDC;
          auto key = sim::get<sim::metadata::RandomCycleKeyField>(field);
          auto position =
              sim::get<sim::metadata::RandomCyclePositionField>(field);
          auto keyOffset =
              key ? fieldOffsets.find(key.getValue()) : fieldOffsets.end();
          auto positionOffset = position
                                    ? fieldOffsets.find(position.getValue())
                                    : fieldOffsets.end();
          if (!key || !position || keyOffset == fieldOffsets.end() ||
              positionOffset == fieldOffsets.end())
            return field.emitError(
                "randc variable has no executable cycle state");
          variable.randcKeyOffset = keyOffset->second;
          variable.randcPositionOffset = positionOffset->second;
        }
        layout.randomVariables.push_back(variable);
      }
    }

    for (sim::SimClassMethodDeclOp method :
         methodsByOwner[declaration.getSymName()]) {
      if (!method.getSlot())
        continue;
      if (declaration.getIsInterface())
        continue;
      uint64_t slot = *method.getSlot();
      if (slot >= UINT32_MAX)
        return method.emitError(
            "class method uses the interface dispatch sentinel");
      layout.methods.push_back(method);
    }
    analysis::orderByUnsignedKey(layout.methods,
                                 [](auto method) { return *method.getSlot(); });
    layouts[declaration.getSymName()] = std::move(layout);
  }

  // Emit signatures once per interface. A class records only interfaces it
  // introduces; inheritance resolves those bindings against its dynamic table.
  llvm::StringMap<SmallVector<uint64_t>> interfaceSignatures;
  for (sim::SimClassDeclOp declaration : classes) {
    if (!declaration.getIsInterface())
      continue;
    SmallVector<sim::SimClassMethodDeclOp> localMethods;
    for (auto method : methodsByOwner[declaration.getSymName()])
      if (method.getInterfaceOrdinalAttr())
        localMethods.push_back(method);
    auto &signatures = interfaceSignatures[declaration.getSymName()];
    signatures.resize(localMethods.size());
    for (auto method : localMethods) {
      uint64_t ordinal = *method.getInterfaceOrdinal();
      if (ordinal >= signatures.size() || signatures[ordinal] ||
          !method.getSignatureId() || !*method.getSignatureId())
        return method.emitError("interface method ordinals are not dense");
      signatures[ordinal] = *method.getSignatureId();
    }
  }
  llvm::StringMap<SmallVector<sim::SimClassDeclOp>> interfaceBases;
  for (sim::SimClassDeclOp declaration : classes) {
    SmallVector<sim::SimClassDeclOp> interfaces;
    if (ArrayAttr declared = declaration.getInterfacesAttr())
      for (Attribute attribute : declared) {
        auto reference = cast<FlatSymbolRefAttr>(attribute);
        auto found = classesByName.find(reference.getValue());
        if (found == classesByName.end() || !found->second.getIsInterface())
          return declaration.emitError(
              "managed interface descriptor is missing");
        interfaces.push_back(found->second);
      }
    analysis::orderByUnsignedKey(
        interfaces, [](auto interface) { return interface.getId(); });
    if (declaration.getIsInterface())
      interfaceBases[declaration.getSymName()] = interfaces;
    auto &layout = layouts[declaration.getSymName()];
    for (auto interface : interfaces)
      layout.interfaces.push_back({interface});
  }

  // Validate concrete implementations without rescanning inherited contracts.
  // Interface requirements share Patricia subtrees just like runtime indexes.
  using Requirements =
      obelisk::runtime::DescriptorMap<sim::SimClassMethodDeclOp>;
  Requirements::MergeCache requirementMerges;
  llvm::StringMap<Requirements> interfaceRequirements;
  llvm::StringMap<uint8_t> interfaceState;
  struct InterfaceFrame {
    sim::SimClassDeclOp declaration;
    size_t base = 0;
    Requirements requirements;
  };
  for (auto root : classes) {
    if (!root.getIsInterface() || interfaceState.lookup(root.getSymName()) == 2)
      continue;
    SmallVector<InterfaceFrame> pending{{root}};
    interfaceState[root.getSymName()] = 1;
    while (!pending.empty()) {
      InterfaceFrame &frame = pending.back();
      auto &bases = interfaceBases[frame.declaration.getSymName()];
      if (frame.base != bases.size()) {
        auto base = bases[frame.base];
        uint8_t state = interfaceState.lookup(base.getSymName());
        if (state == 1)
          return base.emitError(
              "managed interface inheritance contains a cycle");
        if (state == 0) {
          interfaceState[base.getSymName()] = 1;
          pending.push_back({base});
          continue;
        }
        frame.requirements = frame.requirements.merge(
            interfaceRequirements[base.getSymName()], requirementMerges);
        ++frame.base;
        continue;
      }
      for (auto method : methodsByOwner[frame.declaration.getSymName()])
        if (method.getInterfaceOrdinalAttr())
          frame.requirements =
              frame.requirements.set(*method.getSignatureId(), method);
      interfaceRequirements[frame.declaration.getSymName()] =
          std::move(frame.requirements);
      interfaceState[frame.declaration.getSymName()] = 2;
      pending.pop_back();
    }
  }
  analysis::ClassDispatchAnalysis dispatch(
      classes.front()->getParentOfType<sim::SimDesignOp>());
  struct ContractState {
    Requirements requirements, signatures;
  };
  obelisk::runtime::DescriptorMatchCache matches;
  llvm::StringMap<ContractState> contracts;
  auto missingImplementation = [&](sim::SimClassDeclOp declaration,
                                   uint64_t signature) {
    auto implementation = dispatch.resolve(declaration, UINT32_MAX, signature);
    return !implementation || implementation.getIsPure() ||
           !implementation.getImplementationAttr();
  };
  for (SharedClass *shared : orderedClasses) {
    auto declaration = shared->declaration;
    if (declaration.getIsInterface())
      continue;
    ContractState state;
    if (auto baseName = declaration.getBase())
      state = contracts[*baseName];
    for (auto method : layouts[declaration.getSymName()].methods) {
      uint64_t signature = *method.getSignatureId();
      state.signatures = state.signatures.set(
          signature, dispatch.resolve(declaration, UINT32_MAX, signature));
    }
    for (auto interface : layouts[declaration.getSymName()].interfaces)
      state.requirements = state.requirements.merge(
          interfaceRequirements[interface.declaration.getSymName()],
          requirementMerges);
    size_t implemented = state.requirements.countMatches(
        state.signatures, matches, [](auto, auto implementation) {
          return implementation && !implementation.getIsPure() &&
                 implementation.getImplementationAttr();
        });
    if (!declaration.getIsAbstract() &&
        implemented != state.requirements.size()) {
      sim::SimClassMethodDeclOp missing;
      state.requirements.forEach([&](auto requirement) {
        if (!missing &&
            missingImplementation(declaration, *requirement.getSignatureId()))
          missing = requirement;
      });
      return declaration.emitError()
             << "concrete class does not implement interface "
             << missing.getOwner();
    }
    contracts[declaration.getSymName()] = std::move(state);
  }

  MLIRContext *context = module.getContext();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = IntegerType::get(context, 32);
  Type i64 = IntegerType::get(context, 64);
  Type traceEntryType = LLVM::LLVMStructType::getLiteral(
      context, {i64, i64, i64, i32, i32, pointer});
  Type traceLayoutType = LLVM::LLVMStructType::getLiteral(
      context, {i32, i32, i64, i64, pointer, i64});
  Type randomEdgeType =
      LLVM::LLVMStructType::getLiteral(context, {i64, i64, i64});
  Type randomVariableType = LLVM::LLVMStructType::getLiteral(
      context, {i64, i64, i64, i64, i64, i32, i32});
  Type randomLayoutType = LLVM::LLVMStructType::getLiteral(
      context, {i32, i32, pointer, i64, pointer, i64});
  Type methodType = LLVM::LLVMStructType::getLiteral(
      context, {i64, i32, i32, pointer, pointer, i64});
  Type interfaceType = LLVM::LLVMStructType::getLiteral(
      context, {i64, pointer, i64, pointer, i64});
  Type byteRangeType = LLVM::LLVMStructType::getLiteral(context, {i64, i64});
  Type classType = LLVM::LLVMStructType::getLiteral(
      context, {i32, i32, i64, i64, i64, pointer, pointer, i64, pointer,
                pointer, i64, pointer, i64, pointer, pointer, i64});
  bool bytecodeOnly =
      runtime::bitEnumContainsAll(runtime::getExecutionFlags(module),
                                  runtime::ExecutionFlags::RequireBytecode);
  if (!bytecodeOnly) {
    OpBuilder builder(context);
    builder.setInsertionPointToStart(module.getBody());
    for (auto method : methods) {
      if (!method.getImplementation() || !method.getIsVirtual())
        continue;
      LLVM::LLVMFuncOp::create(
          builder, method.getLoc(), managedMethodThunkName(method.getSymName()),
          LLVM::LLVMFunctionType::get(
              i32, {pointer, pointer, pointer, pointer, i32, pointer, i64},
              false));
    }
  }
  llvm::StringMap<uint32_t> bytecodeFunctions;
  ::obelisk::detail::walkNativeFunctions<sim::SimFuncOp>(
      module, [&](sim::SimFuncOp function) {
        if (auto index = runtime::get<runtime::BytecodeFunction>(function))
          bytecodeFunctions[function.getSymName()] =
              static_cast<uint32_t>(index.getValue().getZExtValue());
      });

  for (const auto &entry : interfaceSignatures) {
    if (entry.second.empty())
      continue;
    Type arrayType = LLVM::LLVMArrayType::get(i64, entry.second.size());
    makeConstantGlobal(
        module, classes.front().getLoc(), arrayType,
        entry.getKey().str() + ".__obelisk_interface_signatures",
        LLVM::Linkage::Internal, 8, [&](OpBuilder &builder) {
          Location location = classes.front().getLoc();
          Value array = LLVM::ZeroOp::create(builder, location, arrayType);
          for (auto [index, signature] : llvm::enumerate(entry.second))
            array = LLVM::InsertValueOp::create(
                builder, location, array,
                llvmConstant(builder, location, i64, signature),
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          return array;
        });
  }

  auto interfaceRecord = [&](OpBuilder &builder, Location location,
                             sim::SimClassDeclOp declaration) {
    Value record = LLVM::ZeroOp::create(builder, location, interfaceType);
    record = insertValue(
        builder, location, record,
        llvmConstant(builder, location, i64, declaration.getId()), 0);
    const auto &signatures = interfaceSignatures[declaration.getSymName()];
    if (!signatures.empty())
      record = insertValue(
          builder, location, record,
          LLVM::AddressOfOp::create(builder, location, pointer,
                                    declaration.getSymName().str() +
                                        ".__obelisk_interface_signatures"),
          1);
    record =
        insertValue(builder, location, record,
                    llvmConstant(builder, location, i64, signatures.size()), 2);
    const auto &bases = interfaceBases[declaration.getSymName()];
    if (!bases.empty())
      record = insertValue(
          builder, location, record,
          LLVM::AddressOfOp::create(builder, location, pointer,
                                    declaration.getSymName().str() +
                                        ".__obelisk_interface_bases"),
          3);
    return insertValue(builder, location, record,
                       llvmConstant(builder, location, i64, bases.size()), 4);
  };
  for (sim::SimClassDeclOp declaration : classes) {
    if (!declaration.getIsInterface())
      continue;
    const auto &bases = interfaceBases[declaration.getSymName()];
    if (!bases.empty()) {
      Type arrayType = LLVM::LLVMArrayType::get(pointer, bases.size());
      makeConstantGlobal(
          module, declaration.getLoc(), arrayType,
          declaration.getSymName().str() + ".__obelisk_interface_bases",
          LLVM::Linkage::Internal, 8, [&](OpBuilder &builder) {
            Location location = declaration.getLoc();
            Value array = LLVM::ZeroOp::create(builder, location, arrayType);
            for (auto [index, base] : llvm::enumerate(bases)) {
              sim::SimClassDeclOp baseDeclaration = base;
              array = LLVM::InsertValueOp::create(
                  builder, location, array,
                  LLVM::AddressOfOp::create(builder, location, pointer,
                                            baseDeclaration.getSymName().str() +
                                                ".__obelisk_interface"),
                  ArrayRef<int64_t>{static_cast<int64_t>(index)});
            }
            return array;
          });
    }
    makeConstantGlobal(module, declaration.getLoc(), interfaceType,
                       declaration.getSymName().str() + ".__obelisk_interface",
                       LLVM::Linkage::Internal, 8, [&](OpBuilder &builder) {
                         return interfaceRecord(builder, declaration.getLoc(),
                                                declaration);
                       });
  }

  // Base descriptors must exist before derived initializers take their
  // addresses. The validated inventory order above already provides that
  // topological order, so materialize each class exactly once.
  for (SharedClass *shared : orderedClasses) {
    if (failed([&]() -> LogicalResult {
          sim::SimClassDeclOp declaration = shared->declaration;
          ManagedClassLayout &layout = layouts[declaration.getSymName()];
          std::string prefix = declaration.getSymName().str();
          std::string entriesName = prefix + ".__obelisk_trace_entries";
          std::string traceName = prefix + ".__obelisk_trace_layout";
          std::string randomEdgesName = prefix + ".__obelisk_random_edges";
          std::string randomVariablesName =
              prefix + ".__obelisk_random_variables";
          std::string randomLayoutName = prefix + ".__obelisk_random_layout";
          std::string methodsName = prefix + ".__obelisk_methods";
          std::string interfacesName = prefix + ".__obelisk_interfaces";
          std::string copyResetsName = prefix + ".__obelisk_copy_resets";
          std::string debugName = prefix + ".__obelisk_debug_name";
          std::string descriptorName = managedClassDescriptorName(
              FlatSymbolRefAttr::get(context, declaration.getSymName()));
          Location location = declaration.getLoc();

          size_t inheritedEntries = declaration.getBase().has_value() ? 1 : 0;
          size_t traceEntryCount =
              layout.tracedFields.size() + inheritedEntries;
          Type entriesType =
              LLVM::LLVMArrayType::get(traceEntryType, traceEntryCount);
          if (traceEntryCount)
            makeConstantGlobal(
                module, location, entriesType, entriesName,
                LLVM::Linkage::Internal, 8, [&](OpBuilder &builder) {
                  Value array =
                      LLVM::ZeroOp::create(builder, location, entriesType);
                  if (auto base = declaration.getBase()) {
                    Value entry =
                        LLVM::ZeroOp::create(builder, location, traceEntryType);
                    entry =
                        insertValue(builder, location, entry,
                                    llvmConstant(builder, location, i64, 1), 2);
                    entry = insertValue(builder, location, entry,
                                        llvmConstant(builder, location, i32,
                                                     OBELISK_RT_TRACE_EMBEDDED),
                                        3);
                    entry = insertValue(
                        builder, location, entry,
                        LLVM::AddressOfOp::create(
                            builder, location, pointer,
                            base->str() + ".__obelisk_trace_layout"),
                        5);
                    array = LLVM::InsertValueOp::create(
                        builder, location, array, entry, ArrayRef<int64_t>{0});
                  }
                  for (auto [index, field] :
                       llvm::enumerate(layout.tracedFields)) {
                    Value entry =
                        LLVM::ZeroOp::create(builder, location, traceEntryType);
                    entry = insertValue(
                        builder, location, entry,
                        llvmConstant(builder, location, i64, field.offset), 0);
                    entry =
                        insertValue(builder, location, entry,
                                    llvmConstant(builder, location, i64, 1), 2);
                    entry = insertValue(
                        builder, location, entry,
                        llvmConstant(builder, location, i32,
                                     field.weak ? OBELISK_RT_TRACE_WEAK
                                                : OBELISK_RT_TRACE_STRONG),
                        3);
                    entry = insertValue(
                        builder, location, entry,
                        llvmConstant(builder, location, i32, field.slotKind),
                        4);
                    array = LLVM::InsertValueOp::create(
                        builder, location, array, entry,
                        ArrayRef<int64_t>{
                            static_cast<int64_t>(index + inheritedEntries)});
                  }
                  return array;
                });
          makeConstantGlobal(
              module, location, traceLayoutType, traceName,
              LLVM::Linkage::Internal, 8, [&](OpBuilder &builder) {
                Value trace =
                    LLVM::ZeroOp::create(builder, location, traceLayoutType);
                trace = insertValue(
                    builder, location, trace,
                    llvmConstant(builder, location, i32, OBELISK_RT_VERSION),
                    0);
                trace = insertValue(
                    builder, location, trace,
                    llvmConstant(builder, location, i64, layout.size), 2);
                trace = insertValue(
                    builder, location, trace,
                    llvmConstant(builder, location, i64, layout.alignment), 3);
                if (traceEntryCount)
                  trace =
                      insertValue(builder, location, trace,
                                  LLVM::AddressOfOp::create(
                                      builder, location, pointer, entriesName),
                                  4);
                trace = insertValue(
                    builder, location, trace,
                    llvmConstant(builder, location, i64, traceEntryCount), 5);
                return trace;
              });

          Type randomEdgesType = LLVM::LLVMArrayType::get(
              randomEdgeType, layout.randomEdges.size());
          if (!layout.randomEdges.empty()) {
            makeConstantGlobal(
                module, location, randomEdgesType, randomEdgesName,
                LLVM::Linkage::Internal, 8, [&](OpBuilder &builder) {
                  Value array =
                      LLVM::ZeroOp::create(builder, location, randomEdgesType);
                  for (auto [index, edge] :
                       llvm::enumerate(layout.randomEdges)) {
                    Value record =
                        LLVM::ZeroOp::create(builder, location, randomEdgeType);
                    record = insertValue(
                        builder, location, record,
                        llvmConstant(builder, location, i64, edge.handleOffset),
                        0);
                    record = insertValue(
                        builder, location, record,
                        llvmConstant(builder, location, i64, edge.modeOffset),
                        1);
                    record = insertValue(
                        builder, location, record,
                        llvmConstant(builder, location, i64, edge.modeMask), 2);
                    array = LLVM::InsertValueOp::create(
                        builder, location, array, record,
                        ArrayRef<int64_t>{static_cast<int64_t>(index)});
                  }
                  return array;
                });
          }
          Type randomVariablesType = LLVM::LLVMArrayType::get(
              randomVariableType, layout.randomVariables.size());
          if (!layout.randomVariables.empty())
            makeConstantGlobal(
                module, location, randomVariablesType, randomVariablesName,
                LLVM::Linkage::Internal, 8, [&](OpBuilder &builder) {
                  Value array = LLVM::ZeroOp::create(builder, location,
                                                     randomVariablesType);
                  for (auto [index, variable] :
                       llvm::enumerate(layout.randomVariables)) {
                    Value record = LLVM::ZeroOp::create(builder, location,
                                                        randomVariableType);
                    record = insertValue(builder, location, record,
                                         llvmConstant(builder, location, i64,
                                                      variable.valueOffset),
                                         0);
                    record = insertValue(builder, location, record,
                                         llvmConstant(builder, location, i64,
                                                      variable.modeOffset),
                                         1);
                    record = insertValue(
                        builder, location, record,
                        llvmConstant(builder, location, i64, variable.modeMask),
                        2);
                    record = insertValue(builder, location, record,
                                         llvmConstant(builder, location, i64,
                                                      variable.randcKeyOffset),
                                         3);
                    record =
                        insertValue(builder, location, record,
                                    llvmConstant(builder, location, i64,
                                                 variable.randcPositionOffset),
                                    4);
                    record = insertValue(
                        builder, location, record,
                        llvmConstant(builder, location, i32, variable.bitWidth),
                        5);
                    record = insertValue(
                        builder, location, record,
                        llvmConstant(builder, location, i32, variable.flags),
                        6);
                    array = LLVM::InsertValueOp::create(
                        builder, location, array, record,
                        ArrayRef<int64_t>{static_cast<int64_t>(index)});
                  }
                  return array;
                });
          if (!layout.randomEdges.empty() || !layout.randomVariables.empty())
            makeConstantGlobal(
                module, location, randomLayoutType, randomLayoutName,
                LLVM::Linkage::Internal, 8, [&](OpBuilder &builder) {
                  Value random =
                      LLVM::ZeroOp::create(builder, location, randomLayoutType);
                  random = insertValue(
                      builder, location, random,
                      llvmConstant(builder, location, i32, OBELISK_RT_VERSION),
                      0);
                  if (!layout.randomEdges.empty())
                    random = insertValue(
                        builder, location, random,
                        LLVM::AddressOfOp::create(builder, location, pointer,
                                                  randomEdgesName),
                        2);
                  random = insertValue(builder, location, random,
                                       llvmConstant(builder, location, i64,
                                                    layout.randomEdges.size()),
                                       3);
                  if (!layout.randomVariables.empty())
                    random = insertValue(
                        builder, location, random,
                        LLVM::AddressOfOp::create(builder, location, pointer,
                                                  randomVariablesName),
                        4);
                  random =
                      insertValue(builder, location, random,
                                  llvmConstant(builder, location, i64,
                                               layout.randomVariables.size()),
                                  5);
                  return random;
                });

          Type methodsType =
              LLVM::LLVMArrayType::get(methodType, layout.methods.size());
          if (!layout.methods.empty())
            makeConstantGlobal(
                module, location, methodsType, methodsName,
                LLVM::Linkage::Internal, 8, [&](OpBuilder &builder) {
                  Value array =
                      LLVM::ZeroOp::create(builder, location, methodsType);
                  for (auto indexedMethod : llvm::enumerate(layout.methods)) {
                    auto index = indexedMethod.index();
                    auto method = indexedMethod.value();
                    Value entry =
                        LLVM::ZeroOp::create(builder, location, methodType);
                    entry = insertValue(builder, location, entry,
                                        llvmConstant(builder, location, i64,
                                                     *method.getSignatureId()),
                                        0);
                    uint32_t flags =
                        method.getIsTask() ? OBELISK_RT_METHOD_TASK : 0;
                    if (method.getIsPure())
                      flags |= OBELISK_RT_METHOD_PURE;
                    entry = insertValue(
                        builder, location, entry,
                        llvmConstant(builder, location, i32, flags), 1);
                    entry = insertValue(
                        builder, location, entry,
                        llvmConstant(
                            builder, location, i32,
                            [&] {
                              if (auto implementation =
                                      method.getImplementation()) {
                                auto found =
                                    bytecodeFunctions.find(*implementation);
                                if (found != bytecodeFunctions.end())
                                  return found->second;
                              }
                              return uint32_t{OBELISK_RT_METHOD_NO_BYTECODE};
                            }()),
                        2);
                    if (!method.getIsPure() && !bytecodeOnly)
                      entry = insertValue(
                          builder, location, entry,
                          LLVM::AddressOfOp::create(
                              builder, location, pointer,
                              managedMethodThunkName(method.getSymName())),
                          3);
                    entry = insertValue(
                        builder, location, entry,
                        llvmConstant(builder, location, i64, *method.getSlot()),
                        5);
                    array = LLVM::InsertValueOp::create(
                        builder, location, array, entry,
                        ArrayRef<int64_t>{static_cast<int64_t>(index)});
                  }
                  return array;
                });

          Type interfacesType =
              LLVM::LLVMArrayType::get(interfaceType, layout.interfaces.size());
          if (!layout.interfaces.empty())
            makeConstantGlobal(
                module, location, interfacesType, interfacesName,
                LLVM::Linkage::Internal, 8, [&](OpBuilder &builder) {
                  Value array =
                      LLVM::ZeroOp::create(builder, location, interfacesType);
                  for (auto [index, interface] :
                       llvm::enumerate(layout.interfaces)) {
                    Value record = interfaceRecord(builder, location,
                                                   interface.declaration);
                    array = LLVM::InsertValueOp::create(
                        builder, location, array, record,
                        ArrayRef<int64_t>{static_cast<int64_t>(index)});
                  }
                  return array;
                });

          Type copyResetsType =
              LLVM::LLVMArrayType::get(byteRangeType, layout.copyResets.size());
          if (!layout.copyResets.empty())
            makeConstantGlobal(
                module, location, copyResetsType, copyResetsName,
                LLVM::Linkage::Internal, 8, [&](OpBuilder &builder) {
                  Value array =
                      LLVM::ZeroOp::create(builder, location, copyResetsType);
                  for (auto [index, range] :
                       llvm::enumerate(layout.copyResets)) {
                    Value record =
                        LLVM::ZeroOp::create(builder, location, byteRangeType);
                    record = insertValue(
                        builder, location, record,
                        llvmConstant(builder, location, i64, range.first), 0);
                    record = insertValue(
                        builder, location, record,
                        llvmConstant(builder, location, i64, range.second), 1);
                    array = LLVM::InsertValueOp::create(
                        builder, location, array, record,
                        ArrayRef<int64_t>{static_cast<int64_t>(index)});
                  }
                  return array;
                });

          StringRef debug = declaration.getDebugNameAttr()
                                ? declaration.getDebugNameAttr().getValue()
                                : StringRef{};
          if (!debug.empty())
            makeByteArrayGlobal(module, location, debugName, debug);
          makeConstantGlobal(
              module, location, classType, descriptorName,
              LLVM::Linkage::Internal, 8, [&](OpBuilder &builder) {
                Value descriptor =
                    LLVM::ZeroOp::create(builder, location, classType);
                descriptor = insertValue(
                    builder, location, descriptor,
                    llvmConstant(builder, location, i32, OBELISK_RT_VERSION),
                    0);
                uint32_t flags =
                    declaration.getIsAbstract() ? OBELISK_RT_CLASS_ABSTRACT : 0;
                if (declaration.getIsInterface())
                  flags |= OBELISK_RT_CLASS_INTERFACE;
                if (declaration.getIsFinal())
                  flags |= OBELISK_RT_CLASS_FINAL;
                if (declaration.getWeakReferentAttr())
                  flags |= OBELISK_RT_CLASS_WEAK_WRAPPER;
                descriptor =
                    insertValue(builder, location, descriptor,
                                llvmConstant(builder, location, i32, flags), 1);
                descriptor = insertValue(
                    builder, location, descriptor,
                    llvmConstant(builder, location, i64, declaration.getId()),
                    2);
                descriptor = insertValue(
                    builder, location, descriptor,
                    llvmConstant(builder, location, i64, layout.size), 3);
                descriptor = insertValue(
                    builder, location, descriptor,
                    llvmConstant(builder, location, i64, layout.alignment), 4);
                if (auto base = declaration.getBase())
                  descriptor = insertValue(
                      builder, location, descriptor,
                      LLVM::AddressOfOp::create(
                          builder, location, pointer,
                          managedClassDescriptorName(
                              FlatSymbolRefAttr::get(context, *base))),
                      5);
                if (!layout.interfaces.empty())
                  descriptor = insertValue(
                      builder, location, descriptor,
                      LLVM::AddressOfOp::create(builder, location, pointer,
                                                interfacesName),
                      6);
                descriptor = insertValue(builder, location, descriptor,
                                         llvmConstant(builder, location, i64,
                                                      layout.interfaces.size()),
                                         7);
                descriptor =
                    insertValue(builder, location, descriptor,
                                LLVM::AddressOfOp::create(builder, location,
                                                          pointer, traceName),
                                8);
                if (!layout.methods.empty())
                  descriptor =
                      insertValue(builder, location, descriptor,
                                  LLVM::AddressOfOp::create(
                                      builder, location, pointer, methodsName),
                                  9);
                descriptor = insertValue(
                    builder, location, descriptor,
                    llvmConstant(builder, location, i64, layout.methods.size()),
                    10);
                if (!debug.empty())
                  descriptor =
                      insertValue(builder, location, descriptor,
                                  LLVM::AddressOfOp::create(builder, location,
                                                            pointer, debugName),
                                  11);
                descriptor = insertValue(
                    builder, location, descriptor,
                    llvmConstant(builder, location, i64, debug.size()), 12);
                if (!layout.randomEdges.empty() ||
                    !layout.randomVariables.empty())
                  descriptor = insertValue(
                      builder, location, descriptor,
                      LLVM::AddressOfOp::create(builder, location, pointer,
                                                randomLayoutName),
                      13);
                if (!layout.copyResets.empty())
                  descriptor = insertValue(
                      builder, location, descriptor,
                      LLVM::AddressOfOp::create(builder, location, pointer,
                                                copyResetsName),
                      14);
                descriptor = insertValue(
                    builder, location, descriptor,
                    llvmConstant(builder, location, i64,
                                 layout.copyResets.size()),
                    15);
                return descriptor;
              });
          return success();
        }()))
      return failure();
  }
  return success();
}

/// Arithmetic selects only support arithmetic and shaped result types. The
/// simulation dialect nevertheless permits class handles to flow through SSA
/// merges, and a conditional class assignment can therefore be represented as
/// an arith.select before native type conversion. Do not rely on the arithmetic
/// folder seeing that foreign type: spell these selects as ordinary CFG joins
/// while their managed type and root liveness are still visible.
void expandManagedSelectsToCFG(ModuleOp module) {
  SmallVector<arith::SelectOp> selects;
  module.walk([&](arith::SelectOp select) {
    if (sim::isManagedHandleType(select.getType()))
      selects.push_back(select);
  });

  IRRewriter rewriter(module.getContext());
  for (arith::SelectOp select : selects) {
    Block *head = select->getBlock();
    Block *continuation = rewriter.splitBlock(head, select->getIterator());
    BlockArgument merged =
        continuation->addArgument(select.getType(), select.getLoc());
    select.getResult().replaceAllUsesWith(merged);
    Value condition = select.getCondition();
    Value trueValue = select.getTrueValue();
    Value falseValue = select.getFalseValue();
    rewriter.eraseOp(select);

    rewriter.setInsertionPointToEnd(head);
    cf::CondBranchOp::create(rewriter, merged.getLoc(), condition, continuation,
                             ValueRange{trueValue}, continuation,
                             ValueRange{falseValue});
  }
}

} // namespace

/// IEEE 1800-2023 7.2.2 and Table 7-1: an element whose unpacked struct type
/// declares member defaults starts from them, whether the runtime creates it
/// or reads a nonexistent entry. Record each such container element default
/// so the executable declares it before any process runs.
static LogicalResult collectElementDefaults(ModuleOp module) {
  std::map<uint64_t, SmallVector<int8_t>> images;
  WalkResult walked = module.walk([&](Operation *operation) {
    Type element;
    uint64_t typeID = 0, valueSize = 0;
    sim::ElementFlags flags = sim::ElementFlags::None;
    auto describe = [&](auto create, Type elementType) {
      element = elementType;
      typeID = create.getTypeId();
      valueSize = create.getValueSize();
      flags = create.getElementFlags();
    };
    if (auto create = dyn_cast<sim::SimContainerCreateOp>(operation))
      describe(create, sim::getContainerElement(create.getResult().getType()));
    else if (auto create = dyn_cast<sim::SimAssocCreateOp>(operation))
      describe(create, sim::getContainerElement(create.getResult().getType()));
    else if (auto create = dyn_cast<sim::SimMailboxCreateOp>(operation))
      describe(create, create.getResult().getType().getElementType());
    if (!element || !analysis::hasMemberDefaults(element))
      return WalkResult::advance();
    analysis::DefaultStoragePlanes planes =
        analysis::getDefaultStoragePlanes(element);
    bool fourState =
        sim::bitEnumContainsAll(flags, sim::ElementFlags::FourState);
    if (planes.value.getBitWidth() > valueSize * 8) {
      operation->emitOpError("element default exceeds the element size");
      return WalkResult::interrupt();
    }
    SmallVector<int8_t> image;
    image.reserve(valueSize * (fourState ? 2 : 1));
    for (const APInt *plane : {&planes.value, &planes.unknown}) {
      if (plane == &planes.unknown && !fourState)
        break;
      APInt bits = plane->zext(valueSize * 8);
      for (uint64_t byte = 0; byte != valueSize; ++byte)
        image.push_back(
            static_cast<int8_t>(bits.extractBitsAsZExtValue(8, byte * 8)));
    }
    auto [found, inserted] = images.try_emplace(typeID, image);
    if (!inserted && found->second != image) {
      operation->emitOpError("element type ID has conflicting defaults");
      return WalkResult::interrupt();
    }
    return WalkResult::advance();
  });
  if (walked.wasInterrupted())
    return failure();
  if (images.empty())
    return success();
  MLIRContext *context = module.getContext();
  SmallVector<Attribute> defaults;
  for (const auto &[typeID, image] : images)
    defaults.push_back(native::ElementDefaultAttr::get(
        context, typeID, DenseI8ArrayAttr::get(context, image)));
  native::set<native::ElementDefaults>(module,
                                       ArrayAttr::get(context, defaults));
  return success();
}

LogicalResult prepareManagedLowering(ModuleOp module,
                                     const llvm::DataLayout &dataLayout) {
  llvm::StringMap<ManagedClassLayout> layouts;
  if (failed(prepareManagedClassInventory(module, dataLayout, layouts)))
    return failure();
  if (failed(collectElementDefaults(module)))
    return failure();
  bool bytecodeOnly =
      runtime::bitEnumContainsAll(runtime::getExecutionFlags(module),
                                  runtime::ExecutionFlags::RequireBytecode);
  if (bytecodeOnly)
    return success();
  if (failed(sim::normalizeClassDirectCalls(module)))
    return failure();
  expandManagedSelectsToCFG(module);
  return success();
}

} // namespace obelisk::detail
