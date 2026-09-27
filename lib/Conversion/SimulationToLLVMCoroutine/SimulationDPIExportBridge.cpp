//===- SimulationDPIExportBridge.cpp - Scope-local DPI export bridges ----===//

#include "obelisk/Conversion/SimulationToLLVMCoroutine.h"
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"

#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/StableHash.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"

#include <array>
#include <limits>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMMATERIALIZEDPIEXPORTSPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

constexpr StringLiteral kExportAttr = "obelisk_sim.dpi_export";
constexpr StringLiteral kExportBridgeAttr = "obelisk_sim.dpi_export_bridge";
constexpr StringLiteral kExportBodySymbolAttr =
    "obelisk_sim.dpi_export_body_symbol";
constexpr StringLiteral kLogicalInputsAttr = "obelisk_sim.dpi_logical_inputs";
constexpr std::array<StringLiteral, 8> kExportMetadata = {
    kExportAttr,
    "obelisk_sim.dpi_c_identifier",
    "obelisk_sim.dpi_scope_id",
    "obelisk_sim.dpi_export_id",
    "obelisk_sim.dpi_abi_signature",
    "obelisk_sim.dpi_aggregate_layouts",
    kLogicalInputsAttr,
    sim::metadata::dpiElidedInputs,
};

FailureOr<Value> materializeDescriptorCapture(OpBuilder &builder, Location loc,
                                              Value context, Type type,
                                              DictionaryAttr attrs,
                                              Operation *diagnostic) {
  auto kind = dyn_cast_or_null<sim::CaptureKindAttr>(
      attrs ? attrs.get(sim::metadata::captureKind) : Attribute{});
  auto descriptor = attrs
                        ? attrs.getAs<IntegerAttr>(sim::metadata::descriptorId)
                        : IntegerAttr{};
  if (!kind || !descriptor)
    return diagnostic->emitError(
               "DPI export capture has no descriptor metadata"),
           failure();
  uint64_t id = descriptor.getValue().getZExtValue();
  Value result;
  switch (kind.getValue()) {
  case sim::CaptureKind::Storage: {
    auto rootType = attrs.getAs<TypeAttr>(sim::metadata::descriptorRootType);
    Type contextType =
        rootType
            ? Type(sim::RefType::get(builder.getContext(), rootType.getValue()))
            : type;
    result = sim::SimContextStorageOp::create(
        builder, loc, contextType, context, builder.getI64IntegerAttr(id));
    if (!rootType)
      break;
    auto low = attrs.getAs<IntegerAttr>(sim::metadata::descriptorLow);
    if (!low)
      return diagnostic->emitError(
                 "DPI export storage view has no descriptor offset"),
             failure();
    if (auto indices =
            attrs.getAs<DenseI64ArrayAttr>(sim::metadata::descriptorIndices)) {
      auto aggregate =
          attrs.getAs<TypeAttr>(sim::metadata::descriptorAggregateType);
      if (!aggregate)
        return diagnostic->emitError(
                   "DPI export aggregate view has no result type"),
               failure();
      result = sim::SimRefSubelementOp::create(
          builder, loc,
          sim::RefType::get(builder.getContext(), aggregate.getValue()), result,
          indices);
    }
    if (result.getType() != type) {
      auto packedLow =
          attrs.getAs<IntegerAttr>(sim::metadata::descriptorPackedLow);
      if (!packedLow)
        return diagnostic->emitError(
                   "DPI export packed view has no bit offset"),
               failure();
      result =
          sim::SimRefExtractOp::create(builder, loc, type, result, packedLow);
    }
    break;
  }
  case sim::CaptureKind::Net:
    result = sim::SimContextNetOp::create(builder, loc, type, context,
                                          builder.getI64IntegerAttr(id));
    break;
  case sim::CaptureKind::Driver:
    result = sim::SimContextDriverOp::create(builder, loc, type, context,
                                             builder.getI64IntegerAttr(id));
    break;
  case sim::CaptureKind::Event:
    result = sim::SimContextEventOp::create(builder, loc, type, context,
                                            builder.getI64IntegerAttr(id));
    break;
  case sim::CaptureKind::Context:
  case sim::CaptureKind::Formal:
  case sim::CaptureKind::Value:
    return diagnostic->emitError(
               "DPI export has a non-materializable trailing capture"),
           failure();
  }
  if (!result || result.getType() != type)
    return diagnostic->emitError(
               "DPI export descriptor capture type does not match its body"),
           failure();
  return result;
}

class MaterializeDPIExportsPass final
    : public impl::ObeliskSimMaterializeDPIExportsPassBase<
          MaterializeDPIExportsPass> {
public:
  using Base::Base;
  void runOnOperation() override {
    if (failed(materializeDPIExportBridges(getOperation())))
      signalPassFailure();
  }
};

} // namespace

LogicalResult materializeDPIExportBridges(ModuleOp module) {
  if (!module->hasAttr("obelisk_sim.has_dpi_exports"))
    return success();
  SmallVector<sim::SimFuncOp> exports;
  module.walk([&](sim::SimFuncOp function) {
    if (function->hasAttr(kExportAttr) && !function->hasAttr(kExportBridgeAttr))
      exports.push_back(function);
  });
  if (exports.empty())
    return success();

  llvm::DenseSet<std::pair<Operation *, StringAttr>> siblingSymbols;
  module.walk([&](sim::SimFuncOp function) {
    siblingSymbols.insert({function->getParentOp(), function.getSymNameAttr()});
  });
  llvm::DenseSet<uint64_t> codeUnitIDs;
  llvm::DenseMap<uint64_t, sim::SimCodeUnitDeclOp> codeUnitDeclarations;
  module.walk([&](sim::SimFuncOp function) {
    if (std::optional<int64_t> id = function.getCodeUnitId(); id && *id > 0)
      codeUnitIDs.insert(static_cast<uint64_t>(*id));
  });
  module.walk([&](sim::SimCodeUnitDeclOp declaration) {
    if (declaration.getId() > 0) {
      codeUnitIDs.insert(static_cast<uint64_t>(declaration.getId()));
      codeUnitDeclarations.try_emplace(
          static_cast<uint64_t>(declaration.getId()), declaration);
    }
  });
  llvm::sort(exports, [](sim::SimFuncOp lhs, sim::SimFuncOp rhs) {
    return lhs.getSymName() < rhs.getSymName();
  });
  for (sim::SimFuncOp function : exports) {
    bool task = function.getEntryKind() == sim::EntryKind::Task;
    if ((!task && function.getEntryKind() != sim::EntryKind::Function) ||
        function.isExternal())
      return function.emitOpError(
          "DPI export must be a defined function or task");
    auto logicalInputs =
        function->getAttrOfType<IntegerAttr>(kLogicalInputsAttr);
    if (!logicalInputs || logicalInputs.getValue().isNegative() ||
        logicalInputs.getValue().getActiveBits() > 32)
      return function.emitOpError(
          "DPI export has no valid logical input count");
    uint64_t inputCount = logicalInputs.getValue().getZExtValue();
    if (function.getNumArguments() == 0 ||
        inputCount >= function.getNumArguments())
      return function.emitOpError(
          "DPI export logical inputs do not match its function signature");
    if (!isa<sim::ContextType>(function.getArgumentTypes().front()))
      return function.emitOpError("DPI export has no runtime context argument");
    std::optional<int64_t> sourceCodeUnit = function.getCodeUnitId();
    auto sourceDeclaration =
        sourceCodeUnit && *sourceCodeUnit > 0
            ? codeUnitDeclarations.find(static_cast<uint64_t>(*sourceCodeUnit))
            : codeUnitDeclarations.end();
    if (sourceDeclaration == codeUnitDeclarations.end())
      return function.emitOpError(
          "DPI export has no matching code-unit declaration");

    std::string symbol =
        (function.getSymName() + ".__obelisk_dpi_export_bridge").str();
    StringAttr symbolAttr = StringAttr::get(module.getContext(), symbol);
    auto siblingKey = std::make_pair(function->getParentOp(), symbolAttr);
    if (siblingSymbols.contains(siblingKey))
      return function.emitOpError("DPI export bridge symbol already exists");
    uint64_t codeUnitID = 0;
    for (uint64_t discriminator = 0;; ++discriminator) {
      std::string identity = discriminator == 0
                                 ? symbol
                                 : (symbol + "." + Twine(discriminator)).str();
      codeUnitID = obelisk_stable_hash(identity.data(), identity.size()) &
                   uint64_t{std::numeric_limits<int64_t>::max()};
      if (codeUnitID != 0 && codeUnitIDs.insert(codeUnitID).second)
        break;
    }

    unsigned bridgeInputs = static_cast<unsigned>(inputCount + 1);
    if (task) {
      // Task output/inout formals carry an additional destination reference.
      // Keep every leading context/formal capture on the capture-free bridge;
      // only scope descriptors are reconstructed below.
      bridgeInputs = 0;
      for (unsigned index = 0; index != function.getNumArguments(); ++index) {
        auto kind = dyn_cast_or_null<sim::CaptureKindAttr>(
            function.getArgAttr(index, sim::metadata::captureKind));
        if (!kind || (kind.getValue() != sim::CaptureKind::Context &&
                      kind.getValue() != sim::CaptureKind::Formal))
          break;
        ++bridgeInputs;
      }
    }
    SmallVector<Type> inputs(
        function.getArgumentTypes().take_front(bridgeInputs));
    FunctionType type =
        FunctionType::get(module.getContext(), inputs,
                          task ? TypeRange{} : function.getResultTypes());
    SmallVector<DictionaryAttr> argAttrs;
    argAttrs.reserve(bridgeInputs);
    for (unsigned index = 0; index != bridgeInputs; ++index)
      argAttrs.push_back(function.getArgAttrDict(index));
    NamedAttrList attrs;
    for (NamedAttribute attr : function->getAttrs()) {
      if (attr.getName() == SymbolTable::getSymbolAttrName() ||
          attr.getName() == function.getFunctionTypeAttrName() ||
          attr.getName() == function.getArgAttrsAttrName() ||
          attr.getName() == function.getResAttrsAttrName() ||
          attr.getName() == function.getEntryKindAttrName() ||
          attr.getName() == function.getCodeUnitIdAttrName())
        continue;
      attrs.append(attr);
    }
    attrs.set(kExportBridgeAttr, UnitAttr::get(module.getContext()));
    if (task)
      attrs.set("obelisk_sim.dpi_task", UnitAttr::get(module.getContext()));
    attrs.set(kExportBodySymbolAttr,
              StringAttr::get(module.getContext(), function.getSymName()));
    attrs.set("code_unit_id",
              IntegerAttr::get(IntegerType::get(module.getContext(), 64),
                               codeUnitID));
    OpBuilder builder(function);
    builder.setInsertionPointAfter(function);
    sim::SimCodeUnitDeclOp::create(
        builder, function.getLoc(), codeUnitID,
        sourceDeclaration->second.getScopeId(),
        task ? sim::EntryKind::Task : sim::EntryKind::Function,
        builder.getStringAttr(symbol),
        builder.getStringAttr("scope-specific DPI export bridge"),
        builder.getUnitAttr());
    auto bridge = sim::SimFuncOp::create(
        builder, function.getLoc(), symbol, type,
        task ? sim::EntryKind::Task : sim::EntryKind::Function,
        attrs.getAttrs(), argAttrs);
    siblingSymbols.insert(siblingKey);
    bridge.setVisibility(SymbolTable::Visibility::Private);
    Block *entry = &bridge.getBody().front();
    builder.setInsertionPointToStart(entry);
    SmallVector<Value> operands(entry->getArguments());
    Value context = entry->getArgument(0);
    for (unsigned index = bridgeInputs; index != function.getNumArguments();
         ++index) {
      FailureOr<Value> capture = materializeDescriptorCapture(
          builder, function.getLoc(), context,
          function.getArgumentTypes()[index], function.getArgAttrDict(index),
          function);
      if (failed(capture)) {
        bridge.erase();
        return failure();
      }
      operands.push_back(*capture);
    }
    if (task) {
      Block *continuation = new Block;
      bridge.getBody().push_back(continuation);
      sim::SimTaskCallOp::create(
          builder, function.getLoc(),
          FlatSymbolRefAttr::get(module.getContext(), function.getSymName()),
          operands, builder.getI64IntegerAttr(operands.size()),
          schedule::ContinuationSiteAttr{}, continuation);
      builder.setInsertionPointToStart(continuation);
      sim::SimReturnOp::create(builder, function.getLoc(), ValueRange{});
    } else {
      auto call = sim::SimCallOp::create(
          builder, function.getLoc(), function.getResultTypes(),
          FlatSymbolRefAttr::get(module.getContext(), function.getSymName()),
          operands, ArrayAttr{}, ArrayAttr{});
      sim::SimReturnOp::create(builder, function.getLoc(), call.getResults());
    }
    // Only the capture-free bridge is externally reachable. Keeping the
    // marker on both functions would create duplicate scope registrations;
    // the ordinary body remains reachable through this bridge and internal
    // SystemVerilog calls.
    function->setAttr("obelisk_sim.dpi_export_body",
                      UnitAttr::get(module.getContext()));
    for (StringLiteral name : kExportMetadata)
      function->removeAttr(name);
  }
  return success();
}

} // namespace obelisk
