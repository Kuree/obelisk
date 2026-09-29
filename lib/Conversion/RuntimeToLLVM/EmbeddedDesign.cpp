//===- EmbeddedDesign.cpp - Materialize embedded simulation design --------===//

#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Conversion/RuntimeToLLVM.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"

#include <cstdint>
#include <cstring>
#include <optional>

using namespace mlir;

namespace obelisk {
Type getNativeProcessDescriptorType(MLIRContext *context) {
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = IntegerType::get(context, 32);
  Type i64 = IntegerType::get(context, 64);
  auto handle = LLVM::LLVMStructType::getLiteral(context, {i32, i32, i64});
  return LLVM::LLVMStructType::getLiteral(
      context, {handle, i32, i32, i32, i32, pointer, pointer, pointer, pointer,
                pointer, pointer, pointer});
}
namespace {

constexpr StringLiteral kMaterializedAttr = "obelisk.execution.materialized";
constexpr StringLiteral kBytecodeAttr = "obelisk.bytecode.image";
constexpr StringLiteral kDatabaseAttr = "obelisk.design.database";
constexpr StringLiteral kFlagsAttr = "obelisk.execution.flags";
constexpr StringLiteral kStateBitsAttr = "obelisk.execution.state_bits";
constexpr StringLiteral kSampledRangesAttr = "obelisk.execution.sampled_ranges";
constexpr StringLiteral kFunctionAttr = "obelisk.bytecode.function";
constexpr StringLiteral kExecutionName = "__obelisk_execution_descriptor_v1";
constexpr StringLiteral kBytecodeName = "__obelisk_bytecode_image_v1";
constexpr StringLiteral kDatabaseName = "__obelisk_design_database_v1";
constexpr StringLiteral kDPIScopesName = "__obelisk_dpi_scopes_v1";
constexpr StringLiteral kActivationsName = "__obelisk_activations_v1";
constexpr StringLiteral kObserversName = "__obelisk_observers_v1";
constexpr StringLiteral kSampledRangesName = "__obelisk_sampled_ranges_v1";
constexpr StringLiteral kExportsName = "__obelisk_dpi_exports_v1";
constexpr StringLiteral kClassBitstreamName =
    "__obelisk_class_bitstream_blob_v1";
constexpr StringLiteral kCoverageSchemaName =
    "__obelisk_coverage_schema_blob_v1";
constexpr uint32_t kActivationHasNative = UINT32_C(1) << 0;
constexpr uint32_t kActivationHasBytecode = UINT32_C(1) << 1;
constexpr uint32_t kActivationNoBytecode = UINT32_MAX;

Value integerConstant(OpBuilder &builder, Location location, Type type,
                      uint64_t value) {
  return LLVM::ConstantOp::create(builder, location, type,
                                  builder.getIntegerAttr(type, value));
}

Value insertValue(OpBuilder &builder, Location location, Value aggregate,
                  Value element, int64_t index) {
  return LLVM::InsertValueOp::create(builder, location, aggregate, element,
                                     ArrayRef<int64_t>{index});
}

struct ExportInfo {
  uint32_t exportID;
  uint64_t scopeID;
  uint64_t codeUnitID;
  uint64_t abiSignature;
  uint32_t inputCount;
  uint32_t outputCount;
  std::string symbol;
  std::string bodySymbol;
  std::string cIdentifier;
  ArrayAttr abi;
  ArrayAttr aggregateLayouts;
  DenseI64ArrayAttr elidedInputs;
  std::optional<uint32_t> bytecodeFunction;
  bool isTask;
};

LLVM_ATTRIBUTE_NOINLINE LogicalResult
collectDPIExports(ModuleOp module, bool bytecodeOnly, Type pointer, Type i32,
                  SmallVectorImpl<ExportInfo> &exports) {
  llvm::StringMap<Operation *> directSymbols;
  for (Operation &operation : module.getBody()->getOperations())
    if (auto name = operation.getAttrOfType<StringAttr>(
            SymbolTable::getSymbolAttrName()))
      directSymbols.try_emplace(name.getValue(), &operation);
  llvm::StringMap<Operation *> importCIdentifiers;
  module.walk([&](sim::SimCodeUnitDeclOp declaration) {
    if (!declaration->hasAttr("simulation.dpi_import"))
      return;
    if (auto identifier = declaration->getAttrOfType<StringAttr>(
            "simulation.dpi_c_identifier"))
      importCIdentifiers.try_emplace(identifier.getValue(), declaration);
  });
  module.walk([&](sim::SimDPICallOp call) {
    importCIdentifiers.try_emplace(call.getCIdentifier(), call);
  });
  bool invalid = false;
  module.walk([&](sim::SimFuncOp function) {
    if (!function->hasAttr("simulation.dpi_export_bridge"))
      return;
    auto exportID =
        function->getAttrOfType<IntegerAttr>("simulation.dpi_export_id");
    auto scopeID =
        function->getAttrOfType<IntegerAttr>("simulation.dpi_scope_id");
    auto identifier =
        function->getAttrOfType<StringAttr>("simulation.dpi_c_identifier");
    auto bodySymbol = function->getAttrOfType<StringAttr>(
        "simulation.dpi_export_body_symbol");
    auto signature =
        function->getAttrOfType<ArrayAttr>("simulation.dpi_abi_signature");
    auto aggregateLayouts =
        function->getAttrOfType<ArrayAttr>("simulation.dpi_aggregate_layouts");
    auto inputs =
        function->getAttrOfType<IntegerAttr>("simulation.dpi_logical_inputs");
    std::optional<int64_t> codeUnitID = function.getCodeUnitId();
    if (!exportID || !scopeID || !identifier || !bodySymbol || !signature ||
        !inputs || !codeUnitID || *codeUnitID <= 0 ||
        exportID.getValue().getActiveBits() > 32 ||
        scopeID.getValue().getActiveBits() > 64 ||
        inputs.getValue().getActiveBits() > 32 ||
        exportID.getValue().getZExtValue() == 0 ||
        identifier.getValue().empty()) {
      function.emitOpError("has incomplete or invalid DPI export metadata");
      invalid = true;
      return;
    }
    if (!aggregateLayouts) {
      SmallVector<Attribute> empty(signature.size(),
                                   UnitAttr::get(module.getContext()));
      aggregateLayouts = ArrayAttr::get(module.getContext(), empty);
    }
    if (aggregateLayouts.size() != signature.size()) {
      function.emitOpError("has invalid DPI aggregate export metadata");
      invalid = true;
      return;
    }
    uint64_t inputCount = inputs.getValue().getZExtValue();
    if (signature.size() > UINT32_MAX || inputCount > signature.size()) {
      function.emitOpError("has an invalid DPI export logical input count");
      invalid = true;
      return;
    }
    bool hasResult = false;
    uint32_t copyOuts = 0;
    for (auto [index, attribute] : llvm::enumerate(signature)) {
      auto abi = dyn_cast<sim::DPIABIAttr>(attribute);
      if (!abi) {
        function.emitOpError("has malformed DPI export ABI metadata");
        invalid = true;
        return;
      }
      auto direction = abi.getDirection();
      if (index < inputCount) {
        if (direction == sim::DPIArgumentDirection::Result) {
          function.emitOpError("has a result in its DPI formal inventory");
          invalid = true;
          return;
        }
        copyOuts += direction == sim::DPIArgumentDirection::Output ||
                    direction == sim::DPIArgumentDirection::InOut;
      } else if (direction == sim::DPIArgumentDirection::Result) {
        if (index != inputCount || hasResult) {
          function.emitOpError("has a misplaced DPI function result");
          invalid = true;
          return;
        }
        hasResult = true;
      }
    }
    uint64_t outputCount = signature.size() - inputCount;
    if (outputCount != copyOuts + (hasResult ? 1u : 0u)) {
      function.emitOpError("has inconsistent DPI export copy-out metadata");
      invalid = true;
      return;
    }
    uint64_t outputCursor = inputCount + (hasResult ? 1 : 0);
    for (uint64_t index = 0; index != inputCount; ++index) {
      auto formal = cast<sim::DPIABIAttr>(signature[index]);
      if (formal.getDirection() == sim::DPIArgumentDirection::Input)
        continue;
      if (outputCursor >= signature.size()) {
        function.emitOpError("has a truncated DPI export copy-out inventory");
        invalid = true;
        return;
      }
      auto output = cast<sim::DPIABIAttr>(signature[outputCursor++]);
      if (output.getDirection() != sim::DPIArgumentDirection::Output ||
          output.getKind() != formal.getKind() ||
          output.getWidth() != formal.getWidth() ||
          output.getFourState() != formal.getFourState() ||
          output.getIsSigned() != formal.getIsSigned()) {
        function.emitOpError(
            "has an out-of-order or incompatible DPI export copy-out");
        invalid = true;
        return;
      }
    }
    if (outputCursor != signature.size()) {
      function.emitOpError("has excess DPI export copy-out metadata");
      invalid = true;
      return;
    }
    auto elidedInputs = function->getAttrOfType<DenseI64ArrayAttr>(
        sim::metadata::dpiElidedInputs);
    llvm::BitVector seenElidedInputs(inputCount);
    if (elidedInputs)
      for (int64_t index : elidedInputs.asArrayRef()) {
        if (index < 0 || static_cast<uint64_t>(index) >= inputCount ||
            seenElidedInputs.test(index) ||
            cast<sim::DPIABIAttr>(signature[index]).getDirection() !=
                sim::DPIArgumentDirection::Output) {
          function.emitOpError("has invalid elided DPI output metadata");
          invalid = true;
          return;
        }
        seenElidedInputs.set(index);
      }
    if (importCIdentifiers.contains(identifier.getValue())) {
      function.emitOpError() << "DPI C identifier '" << identifier.getValue()
                             << "' is used by both an import and an export";
      invalid = true;
      return;
    }
    auto bytecodeFunction = function->getAttrOfType<IntegerAttr>(kFunctionAttr);
    if (bytecodeFunction && bytecodeFunction.getValue().getActiveBits() > 32) {
      function.emitOpError("has an invalid DPI export bytecode function");
      invalid = true;
      return;
    }
    exports.push_back(
        {static_cast<uint32_t>(exportID.getValue().getZExtValue()),
         scopeID.getValue().getZExtValue(), static_cast<uint64_t>(*codeUnitID),
         sim::getDPISignatureHash(signature, inputCount),
         static_cast<uint32_t>(inputCount), static_cast<uint32_t>(outputCount),
         function.getSymName().str(), bodySymbol.getValue().str(),
         identifier.getValue().str(), signature, aggregateLayouts, elidedInputs,
         bytecodeFunction ? std::optional<uint32_t>(static_cast<uint32_t>(
                                bytecodeFunction.getValue().getZExtValue()))
                          : std::nullopt,
         function->hasAttr("simulation.dpi_task")});
  });
  if (invalid)
    return failure();
  llvm::sort(exports, [](const ExportInfo &lhs, const ExportInfo &rhs) {
    return std::tie(lhs.exportID, lhs.scopeID) <
           std::tie(rhs.exportID, rhs.scopeID);
  });
  llvm::DenseMap<uint32_t, size_t> exportIDs;
  llvm::StringMap<size_t> exportNames;
  for (auto [index, info] : llvm::enumerate(exports)) {
    if (index != 0 && exports[index - 1].exportID == info.exportID &&
        exports[index - 1].scopeID == info.scopeID)
      return module.emitError() << "duplicate DPI export ID " << info.exportID
                                << " in scope " << info.scopeID;
    if (auto [found, inserted] = exportIDs.try_emplace(info.exportID, index);
        !inserted && exports[found->second].cIdentifier != info.cIdentifier)
      return module.emitError() << "DPI export ID " << info.exportID
                                << " collides between C identifiers '"
                                << exports[found->second].cIdentifier
                                << "' and '" << info.cIdentifier << "'";
    if (auto [found, inserted] =
            exportNames.try_emplace(info.cIdentifier, index);
        !inserted) {
      const ExportInfo &previous = exports[found->second];
      if (previous.exportID != info.exportID ||
          previous.abiSignature != info.abiSignature ||
          previous.inputCount != info.inputCount ||
          previous.outputCount != info.outputCount ||
          previous.abi != info.abi ||
          previous.aggregateLayouts != info.aggregateLayouts)
        return module.emitError()
               << "DPI export C identifier '" << info.cIdentifier
               << "' has incompatible scope-specific signatures";
    }
    if (bytecodeOnly && !info.bytecodeFunction)
      return module.emitError() << "DPI export '" << info.cIdentifier
                                << "' has no bytecode implementation";
  }
  if (bytecodeOnly && !exports.empty())
    module->setAttr("obelisk.feature.dpi_export_bytecode",
                    UnitAttr::get(module.getContext()));

  for (const ExportInfo &info : exports) {
    if (bytecodeOnly && !info.isTask)
      continue;
    std::string thunkName = info.symbol + ".__obelisk_dpi_export";
    if (directSymbols.contains(thunkName))
      return module.emitError()
             << "symbol collision for DPI export thunk '" << thunkName << "'";
    OpBuilder builder(module.getContext());
    builder.setInsertionPointToStart(module.getBody());
    auto thunk = LLVM::LLVMFuncOp::create(
        builder, module.getLoc(), thunkName,
        LLVM::LLVMFunctionType::get(i32, {pointer, pointer, i32, pointer, i32},
                                    false));
    thunk->setAttr("obelisk.dpi.export_bridge",
                   builder.getStringAttr(info.symbol));
    thunk->setAttr("obelisk.dpi.export_body",
                   builder.getStringAttr(info.bodySymbol));
    thunk->setAttr("simulation.dpi_c_identifier",
                   builder.getStringAttr(info.cIdentifier));
    thunk->setAttr("simulation.dpi_export_id",
                   builder.getI32IntegerAttr(info.exportID));
    thunk->setAttr("simulation.dpi_abi_signature", info.abi);
    thunk->setAttr("simulation.dpi_aggregate_layouts", info.aggregateLayouts);
    if (info.elidedInputs)
      thunk->setAttr(sim::metadata::dpiElidedInputs, info.elidedInputs);
    thunk->setAttr("simulation.dpi_logical_inputs",
                   builder.getI32IntegerAttr(info.inputCount));
    if (info.isTask)
      thunk->setAttr("simulation.dpi_task", builder.getUnitAttr());
    directSymbols.try_emplace(thunkName, thunk);
  }
  return success();
}

LLVM::GlobalOp makeByteGlobal(ModuleOp module, StringRef name,
                              DenseI8ArrayAttr bytes, StringRef section) {
  OpBuilder builder(module.getContext());
  builder.setInsertionPointToStart(module.getBody());
  Type array = LLVM::LLVMArrayType::get(builder.getI8Type(), bytes.size());
  ArrayRef<int8_t> data = bytes.asArrayRef();
  StringRef contents(reinterpret_cast<const char *>(data.data()), data.size());
  auto global = LLVM::GlobalOp::create(builder, module.getLoc(), array, true,
                                       LLVM::Linkage::External, name,
                                       builder.getStringAttr(contents), 8);
  global->setAttr("section", builder.getStringAttr(section));
  return global;
}

template <typename Initializer>
LLVM::GlobalOp makeAggregateGlobal(ModuleOp module, Type type, StringRef name,
                                   LLVM::Linkage linkage, StringRef section,
                                   Initializer &&initializer) {
  OpBuilder builder(module.getContext());
  builder.setInsertionPointToStart(module.getBody());
  auto global = LLVM::GlobalOp::create(builder, module.getLoc(), type, true,
                                       linkage, name, Attribute{}, 8);
  if (!section.empty())
    global->setAttr("section", builder.getStringAttr(section));
  Block *block = new Block;
  global.getInitializerRegion().push_back(block);
  builder.setInsertionPointToStart(block);
  LLVM::ReturnOp::create(builder, module.getLoc(), initializer(builder));
  return global;
}

LLVM_ATTRIBUTE_NOINLINE LogicalResult materializeDPIExportDescriptors(
    ModuleOp module, ArrayRef<ExportInfo> exports, bool bytecodeOnly,
    Type pointer, Type i32, Type i64, size_t scopeCount) {
  for (const ExportInfo &info : exports)
    if (info.scopeID >= scopeCount)
      return module.emitError() << "DPI export '" << info.cIdentifier
                                << "' has invalid scope " << info.scopeID;

  MLIRContext *context = module.getContext();
  Type exportType = LLVM::LLVMStructType::getLiteral(
      context, {i32, i32, i64, i64, i64, i32, i32, i32, i32, pointer, i64});
  Type exportsType = LLVM::LLVMArrayType::get(exportType, exports.size());
  makeAggregateGlobal(
      module, exportsType, kExportsName, LLVM::Linkage::Internal,
      ".obelisk.execution", [&](OpBuilder &builder) {
        Value records =
            LLVM::ZeroOp::create(builder, module.getLoc(), exportsType);
        for (auto [index, info] : llvm::enumerate(exports)) {
          Value record =
              LLVM::ZeroOp::create(builder, module.getLoc(), exportType);
          uint32_t exportFlags = 0;
          record = insertValue(
              builder, module.getLoc(), record,
              integerConstant(builder, module.getLoc(), i32, info.exportID), 0);
          if (!bytecodeOnly && !info.isTask)
            exportFlags |= OBELISK_RT_EXPORT_HAS_NATIVE;
          if (bytecodeOnly && info.bytecodeFunction)
            exportFlags |= OBELISK_RT_EXPORT_HAS_BYTECODE;
          if (info.isTask)
            exportFlags |= OBELISK_RT_EXPORT_TASK;
          record = insertValue(
              builder, module.getLoc(), record,
              integerConstant(builder, module.getLoc(), i32, exportFlags), 1);
          record = insertValue(
              builder, module.getLoc(), record,
              integerConstant(builder, module.getLoc(), i64, info.scopeID), 2);
          record = insertValue(
              builder, module.getLoc(), record,
              integerConstant(builder, module.getLoc(), i64, info.codeUnitID),
              3);
          record = insertValue(
              builder, module.getLoc(), record,
              integerConstant(builder, module.getLoc(), i64, info.abiSignature),
              4);
          record = insertValue(
              builder, module.getLoc(), record,
              integerConstant(builder, module.getLoc(), i32, info.inputCount),
              5);
          record = insertValue(
              builder, module.getLoc(), record,
              integerConstant(builder, module.getLoc(), i32, info.outputCount),
              6);
          record = insertValue(
              builder, module.getLoc(), record,
              integerConstant(builder, module.getLoc(), i32,
                              bytecodeOnly ? info.bytecodeFunction.value_or(
                                                 OBELISK_RT_EXPORT_NO_BYTECODE)
                                           : OBELISK_RT_EXPORT_NO_BYTECODE),
              7);
          if (!bytecodeOnly || info.isTask)
            record = insertValue(builder, module.getLoc(), record,
                                 LLVM::AddressOfOp::create(
                                     builder, module.getLoc(), pointer,
                                     info.symbol + ".__obelisk_dpi_export"),
                                 9);
          records = LLVM::InsertValueOp::create(
              builder, module.getLoc(), records, record,
              ArrayRef<int64_t>{static_cast<int64_t>(index)});
        }
        return records;
      });
  return success();
}

uint64_t read64(ArrayRef<int8_t> bytes, size_t offset) {
  uint64_t result = 0;
  if (offset > bytes.size() || bytes.size() - offset < sizeof(result))
    return 0;
  for (unsigned index = 0; index != sizeof(result); ++index)
    result |= uint64_t{static_cast<uint8_t>(bytes[offset + index])}
              << (index * 8);
  return result;
}

FailureOr<int32_t> timeExponent(ModuleOp module, uint64_t femtoseconds) {
  if (femtoseconds == 0)
    return module.emitError("DPI time scale must be nonzero"), failure();
  int32_t exponent = -15;
  while (femtoseconds > 1 && femtoseconds % 10 == 0) {
    femtoseconds /= 10;
    ++exponent;
  }
  if (femtoseconds != 1)
    return module.emitError(
               "DPI time scale must be an integral decimal power in seconds"),
           failure();
  return exponent;
}

LogicalResult checkMagic(ModuleOp module, DenseI8ArrayAttr bytes,
                         StringRef magic, StringRef description) {
  ArrayRef<int8_t> data = bytes.asArrayRef();
  if (data.size() < magic.size() ||
      std::memcmp(data.data(), magic.data(), magic.size()) != 0)
    return module.emitError() << description << " has an invalid magic";
  return success();
}

LogicalResult appendRetentionEntry(ModuleOp module, LLVM::GlobalOp global,
                                   Type pointer, StringRef symbol) {
  auto array = dyn_cast<LLVM::LLVMArrayType>(global.getGlobalType());
  Block *initializer = global.getInitializerBlock();
  auto returnOp = initializer
                      ? dyn_cast<LLVM::ReturnOp>(initializer->getTerminator())
                      : LLVM::ReturnOp{};
  if (!array || array.getElementType() != pointer || !returnOp ||
      returnOp->getNumOperands() != 1 ||
      returnOp->getOperand(0).getType() != array ||
      array.getNumElements() == UINT64_MAX)
    return module.emitError()
           << "cannot append design database to malformed retention global '"
           << global.getSymName() << "'";
  OpBuilder builder(returnOp);
  Type expanded = LLVM::LLVMArrayType::get(pointer, array.getNumElements() + 1);
  Value value = LLVM::ZeroOp::create(builder, module.getLoc(), expanded);
  for (uint64_t index = 0; index != array.getNumElements(); ++index) {
    Value element = LLVM::ExtractValueOp::create(
        builder, module.getLoc(), pointer, returnOp->getOperand(0),
        ArrayRef<int64_t>{static_cast<int64_t>(index)});
    value = insertValue(builder, module.getLoc(), value, element,
                        static_cast<int64_t>(index));
  }
  value = insertValue(
      builder, module.getLoc(), value,
      LLVM::AddressOfOp::create(builder, module.getLoc(), pointer, symbol),
      static_cast<int64_t>(array.getNumElements()));
  returnOp->setOperand(0, value);
  global.setGlobalType(expanded);
  return success();
}

} // namespace

LogicalResult
materializeEmbeddedSimulationDesign(ModuleOp module,
                                    const llvm::DataLayout &dataLayout) {
  if (module->hasAttr(kMaterializedAttr))
    return success();
  if (module.lookupSymbol(kExecutionName))
    return module.emitError()
           << "symbol collision for reserved execution descriptor '"
           << kExecutionName << "'";

  auto bytecode = module->getAttrOfType<DenseI8ArrayAttr>(kBytecodeAttr);
  auto database = module->getAttrOfType<DenseI8ArrayAttr>(kDatabaseAttr);
  auto classBitstream = module->getAttrOfType<DenseI8ArrayAttr>(
      sim::metadata::classBitstreamBlob);
  auto coverageSchema = module->getAttrOfType<DenseI8ArrayAttr>(
      sim::metadata::coverageSchemaBlob);
  if (bytecode && failed(checkMagic(module, bytecode, StringRef("OBBCDS1\0", 8),
                                    "embedded bytecode")))
    return failure();
  if (database && failed(checkMagic(module, database, StringRef("OBDSGN1\0", 8),
                                    "embedded design database")))
    return failure();
  if (coverageSchema &&
      failed(checkMagic(module, coverageSchema, StringRef("OBCOV\r\n\x1a", 8),
                        "embedded coverage schema")))
    return failure();

  if (bytecode && module.lookupSymbol(kBytecodeName))
    return module.emitError()
           << "symbol collision for reserved bytecode image '" << kBytecodeName
           << "'";
  if (database && module.lookupSymbol(kDatabaseName))
    return module.emitError()
           << "symbol collision for reserved design database '" << kDatabaseName
           << "'";
  if (classBitstream && module.lookupSymbol(kClassBitstreamName))
    return module.emitError()
           << "symbol collision for reserved class bit-stream blob '"
           << kClassBitstreamName << "'";
  if (coverageSchema && module.lookupSymbol(kCoverageSchemaName))
    return module.emitError()
           << "symbol collision for reserved coverage schema blob '"
           << kCoverageSchemaName << "'";

  if (bytecode)
    makeByteGlobal(module, kBytecodeName, bytecode, ".obelisk.bytecode");
  if (database)
    makeByteGlobal(module, kDatabaseName, database, ".obelisk.design");
  if (classBitstream)
    makeByteGlobal(module, kClassBitstreamName, classBitstream,
                   ".obelisk.class_bitstream");
  if (coverageSchema)
    makeByteGlobal(module, kCoverageSchemaName, coverageSchema,
                   ".obelisk.coverage");

  MLIRContext *context = module.getContext();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = IntegerType::get(context, 32);
  Type i64 = IntegerType::get(context, 64);

  auto executionFlags =
      module->getAttrOfType<IntegerAttr>("obelisk.execution.flags");
  bool bytecodeOnly =
      executionFlags && (executionFlags.getValue().getZExtValue() &
                         OBELISK_RT_EXECUTION_REQUIRE_BYTECODE) != 0;

  struct ActivationInfo {
    uint64_t codeUnitID;
    std::string symbol;
    std::optional<uint32_t> bytecodeFunction;
  };
  SmallVector<ActivationInfo> activations;
  module.walk([&](sim::SimFuncOp function) {
    if (function.getEntryKind() == sim::EntryKind::Function ||
        function.getEntryKind() == sim::EntryKind::Observer)
      return;
    std::optional<int64_t> codeUnitID = function.getCodeUnitId();
    if (!codeUnitID || *codeUnitID <= 0)
      return;
    auto bytecodeFunction = function->getAttrOfType<IntegerAttr>(kFunctionAttr);
    activations.push_back(
        {static_cast<uint64_t>(*codeUnitID), function.getSymName().str(),
         bytecodeFunction ? std::optional<uint32_t>(static_cast<uint32_t>(
                                bytecodeFunction.getValue().getZExtValue()))
                          : std::nullopt});
  });
  llvm::sort(activations,
             [](const ActivationInfo &lhs, const ActivationInfo &rhs) {
               return lhs.codeUnitID < rhs.codeUnitID;
             });
  for (size_t index = 1; index < activations.size(); ++index)
    if (activations[index - 1].codeUnitID == activations[index].codeUnitID)
      return module.emitError() << "duplicate activation code-unit ID "
                                << activations[index].codeUnitID;

  struct ObserverInfo {
    uint64_t codeUnitID;
    std::string symbol;
    std::optional<uint32_t> bytecodeFunction;
    uint32_t resultWidth;
    bool fourState;
    bool real32;
    bool real64;
    SmallVector<std::pair<uint32_t, uint32_t>> captures;
    std::string capturesSymbol;
  };
  SmallVector<ObserverInfo> observers;
  bool invalidObserver = false;
  module.walk([&](sim::SimFuncOp function) {
    if (function.getEntryKind() != sim::EntryKind::Observer ||
        ::obelisk::schedule::has<::obelisk::schedule::Field::OverrideEvaluator>(
            function))
      return;
    std::optional<int64_t> codeUnitID = function.getCodeUnitId();
    if (!codeUnitID || *codeUnitID <= 0 ||
        function.getFunctionType().getNumResults() != 1)
      return;
    Type resultType = function.getFunctionType().getResult(0);
    std::optional<unsigned> width =
        isa<FloatType>(resultType)
            ? std::optional<unsigned>(cast<FloatType>(resultType).getWidth())
            : sim::getPackedWidth(resultType);
    if (!width)
      return;
    ObserverInfo info{static_cast<uint64_t>(*codeUnitID),
                      function.getSymName().str(),
                      std::nullopt,
                      *width,
                      isa<sim::LogicType>(resultType),
                      resultType.isF32(),
                      resultType.isF64(),
                      {},
                      {}};
    if (auto bytecodeFunction =
            function->getAttrOfType<IntegerAttr>(kFunctionAttr))
      info.bytecodeFunction =
          static_cast<uint32_t>(bytecodeFunction.getValue().getZExtValue());
    for (unsigned index = 1; index < function.getNumArguments(); ++index) {
      Type type = function.getArgumentTypes()[index];
      uint32_t kind = 0;
      uint32_t captureWidth = 0;
      auto widthOf = [](Type element) -> uint32_t {
        if (auto floating = dyn_cast<FloatType>(element))
          return floating.getWidth();
        return analysis::getSimulationStorageBitWidth(element).value_or(0);
      };
      if (isa<sim::RefType>(type))
        kind = 1,
        captureWidth = widthOf(cast<sim::RefType>(type).getElementType());
      else if (isa<sim::NetType>(type))
        kind = 2,
        captureWidth = widthOf(cast<sim::NetType>(type).getElementType());
      else if (isa<sim::EventType>(type))
        kind = 3, captureWidth = 1;
      else if (isa<sim::DriverType>(type))
        kind = 4,
        captureWidth = widthOf(cast<sim::DriverType>(type).getElementType());
      else if (isa<sim::CovergroupHandleType>(type))
        kind = OBELISK_RT_OBSERVER_CAPTURE_COVERGROUP, captureWidth = 64;
      else if (sim::isManagedHandleType(type))
        kind = OBELISK_RT_OBSERVER_CAPTURE_MANAGED, captureWidth = 64;
      else if (auto reference = dyn_cast<sim::ArgumentRefType>(type))
        kind = OBELISK_RT_OBSERVER_CAPTURE_ARGUMENT_REF,
        captureWidth = widthOf(reference.getElementType());
      if (kind == 0 || captureWidth == 0) {
        function.emitError() << "observer capture #" << index - 1
                             << " is not represented by the stable-handle ABI";
        invalidObserver = true;
        continue;
      }
      info.captures.push_back({kind, captureWidth});
    }
    info.capturesSymbol =
        (Twine("__obelisk_observer_capture_") + Twine(info.codeUnitID)).str();
    if (!invalidObserver)
      observers.push_back(std::move(info));
  });
  if (invalidObserver)
    return failure();
  llvm::sort(observers, [](const ObserverInfo &lhs, const ObserverInfo &rhs) {
    return lhs.codeUnitID < rhs.codeUnitID;
  });
  for (size_t index = 1; index < observers.size(); ++index)
    if (observers[index - 1].codeUnitID == observers[index].codeUnitID)
      return module.emitError() << "duplicate observer code-unit ID "
                                << observers[index].codeUnitID;

  // The immutable descriptor global is materialized before observer bodies are
  // lowered.  Give every uniform native evaluator thunk an LLVM declaration
  // now so address-of verification never depends on a later pass phase.
  // One table serves the whole loop: a module lookup per observer would make
  // this quadratic in the design size.
  if (!bytecodeOnly && !observers.empty()) {
    SymbolTable symbols(module);
    OpBuilder builder(context);
    for (const ObserverInfo &observer : observers) {
      std::string thunkName = observer.symbol + ".__obelisk_observer";
      if (symbols.lookup<LLVM::LLVMFuncOp>(thunkName))
        continue;
      builder.setInsertionPointToStart(module.getBody());
      symbols.insert(LLVM::LLVMFuncOp::create(
          builder, module.getLoc(), thunkName,
          LLVM::LLVMFunctionType::get(
              i32, {pointer, pointer, i32, pointer, pointer, i32}, false)));
    }
  }

  SmallVector<ExportInfo> exports;
  if (module->hasAttr("simulation.has_dpi_exports") &&
      failed(collectDPIExports(module, bytecodeOnly, pointer, i32, exports)))
    return failure();

  // The activation table and process bodies are materialized by separate
  // passes. Declare the exact descriptor ABI before taking its address.
  if (!bytecodeOnly && !activations.empty()) {
    SymbolTable symbols(module);
    OpBuilder builder(context);
    builder.setInsertionPointToStart(module.getBody());
    for (const ActivationInfo &activation : activations) {
      std::string name = activation.symbol + ".__obelisk_process_descriptor";
      if (symbols.lookup(name))
        return module.emitError("duplicate native process descriptor symbol: ")
               << name;
      symbols.insert(LLVM::GlobalOp::create(
          builder, module.getLoc(), getNativeProcessDescriptorType(context),
          true, LLVM::Linkage::External, name, Attribute{}, 8));
    }
  }

  Type activationType =
      LLVM::LLVMStructType::getLiteral(context, {i64, pointer, i32, i32});
  if (!activations.empty()) {
    Type activationArray =
        LLVM::LLVMArrayType::get(activationType, activations.size());
    makeAggregateGlobal(
        module, activationArray, kActivationsName, LLVM::Linkage::Internal,
        ".obelisk.execution", [&](OpBuilder &builder) {
          Value records =
              LLVM::ZeroOp::create(builder, module.getLoc(), activationArray);
          for (auto [index, activation] : llvm::enumerate(activations)) {
            Value record =
                LLVM::ZeroOp::create(builder, module.getLoc(), activationType);
            record = insertValue(builder, module.getLoc(), record,
                                 integerConstant(builder, module.getLoc(), i64,
                                                 activation.codeUnitID),
                                 0);
            uint32_t flags = 0;
            if (!bytecodeOnly) {
              record = insertValue(
                  builder, module.getLoc(), record,
                  LLVM::AddressOfOp::create(
                      builder, module.getLoc(), pointer,
                      activation.symbol + ".__obelisk_process_descriptor"),
                  1);
              flags |= kActivationHasNative;
            }
            uint32_t bytecodeFunction = kActivationNoBytecode;
            if (activation.bytecodeFunction) {
              flags |= kActivationHasBytecode;
              bytecodeFunction = *activation.bytecodeFunction;
            }
            record = insertValue(builder, module.getLoc(), record,
                                 integerConstant(builder, module.getLoc(), i32,
                                                 bytecodeFunction),
                                 2);
            record = insertValue(
                builder, module.getLoc(), record,
                integerConstant(builder, module.getLoc(), i32, flags), 3);
            records = LLVM::InsertValueOp::create(
                builder, module.getLoc(), records, record,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return records;
        });
  }

  Type observerCaptureType =
      LLVM::LLVMStructType::getLiteral(context, {i32, i32});
  for (const ObserverInfo &observer : observers) {
    if (observer.captures.empty())
      continue;
    Type capturesType =
        LLVM::LLVMArrayType::get(observerCaptureType, observer.captures.size());
    makeAggregateGlobal(
        module, capturesType, observer.capturesSymbol, LLVM::Linkage::Internal,
        ".obelisk.execution", [&](OpBuilder &builder) {
          Value values =
              LLVM::ZeroOp::create(builder, module.getLoc(), capturesType);
          for (auto [index, capture] : llvm::enumerate(observer.captures)) {
            Value value = LLVM::ZeroOp::create(builder, module.getLoc(),
                                               observerCaptureType);
            value = insertValue(
                builder, module.getLoc(), value,
                integerConstant(builder, module.getLoc(), i32, capture.first),
                0);
            value = insertValue(
                builder, module.getLoc(), value,
                integerConstant(builder, module.getLoc(), i32, capture.second),
                1);
            values = LLVM::InsertValueOp::create(
                builder, module.getLoc(), values, value,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return values;
        });
  }
  Type observerType = LLVM::LLVMStructType::getLiteral(
      context, {i64, pointer, i32, i32, i32, i32, pointer, i64});
  if (!observers.empty()) {
    Type observersType =
        LLVM::LLVMArrayType::get(observerType, observers.size());
    makeAggregateGlobal(
        module, observersType, kObserversName, LLVM::Linkage::Internal,
        ".obelisk.execution", [&](OpBuilder &builder) {
          Value records =
              LLVM::ZeroOp::create(builder, module.getLoc(), observersType);
          for (auto [index, observer] : llvm::enumerate(observers)) {
            Value record =
                LLVM::ZeroOp::create(builder, module.getLoc(), observerType);
            record = insertValue(builder, module.getLoc(), record,
                                 integerConstant(builder, module.getLoc(), i64,
                                                 observer.codeUnitID),
                                 0);
            if (!observer.captures.empty())
              record = insertValue(
                  builder, module.getLoc(), record,
                  LLVM::AddressOfOp::create(builder, module.getLoc(), pointer,
                                            observer.capturesSymbol),
                  1);
            record = insertValue(builder, module.getLoc(), record,
                                 integerConstant(builder, module.getLoc(), i32,
                                                 observer.captures.size()),
                                 2);
            record = insertValue(builder, module.getLoc(), record,
                                 integerConstant(builder, module.getLoc(), i32,
                                                 observer.resultWidth),
                                 3);
            record = insertValue(
                builder, module.getLoc(), record,
                integerConstant(
                    builder, module.getLoc(), i32,
                    (observer.fourState ? OBELISK_RT_OBSERVER_FOUR_STATE : 0) |
                        (observer.real32 ? OBELISK_RT_OBSERVER_REAL32 : 0) |
                        (observer.real64 ? OBELISK_RT_OBSERVER_REAL64 : 0)),
                4);
            record = insertValue(
                builder, module.getLoc(), record,
                integerConstant(builder, module.getLoc(), i32,
                                observer.bytecodeFunction.value_or(UINT32_MAX)),
                5);
            if (!bytecodeOnly)
              record = insertValue(builder, module.getLoc(), record,
                                   LLVM::AddressOfOp::create(
                                       builder, module.getLoc(), pointer,
                                       observer.symbol + ".__obelisk_observer"),
                                   6);
            records = LLVM::InsertValueOp::create(
                builder, module.getLoc(), records, record,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return records;
        });
  }

  SmallVector<sim::SimScopeDeclOp> scopes;
  module.walk([&](sim::SimScopeDeclOp scope) { scopes.push_back(scope); });
  llvm::sort(scopes, [](sim::SimScopeDeclOp lhs, sim::SimScopeDeclOp rhs) {
    return lhs.getId() < rhs.getId();
  });
  Type dpiScopeType = LLVM::LLVMStructType::getLiteral(
      context, {i64, i64, pointer, i64, i32, i32, i32});
  SmallVector<LLVM::GlobalOp> dpiNames;
  SmallVector<std::string> dpiScopeNames;
  SmallVector<int32_t> dpiUnits;
  SmallVector<int32_t> dpiPrecisions;
  int32_t dpiPrecision = 0;
  if (!scopes.empty()) {
    for (auto [index, scope] : llvm::enumerate(scopes)) {
      if (scope.getId() != index)
        return scope.emitOpError("DPI scope IDs must be dense from zero");
      std::string name =
          index == 0 ? std::string("$root")
                     : scope.getHierarchicalName().value_or(StringRef{}).str();
      if (name.empty())
        name = ("scope." + Twine(index)).str();
      dpiScopeNames.push_back(name);
      std::string globalName =
          ("__obelisk_dpi_scope_name_" + Twine(index)).str();
      OpBuilder nameBuilder(context);
      nameBuilder.setInsertionPointToStart(module.getBody());
      Type nameType =
          LLVM::LLVMArrayType::get(nameBuilder.getI8Type(), name.size());
      dpiNames.push_back(LLVM::GlobalOp::create(
          nameBuilder, scope.getLoc(), nameType, true, LLVM::Linkage::Internal,
          globalName, nameBuilder.getStringAttr(name), 1));
    }
    sim::SimDesignOp design;
    module.walk([&](sim::SimDesignOp candidate) {
      if (!design)
        design = candidate;
    });
    if (!design)
      return module.emitError("DPI scopes require a simulation design");
    auto precisionFs = design.getTimePrecisionFsAttr();
    uint64_t designPrecisionFs =
        precisionFs ? precisionFs.getValue().getZExtValue() : 1'000'000;
    FailureOr<int32_t> exponent = timeExponent(module, designPrecisionFs);
    if (failed(exponent))
      return failure();
    dpiPrecision = *exponent;
    for (sim::SimScopeDeclOp scope : scopes) {
      auto unitFs = scope->getAttrOfType<IntegerAttr>("dpi_unit_femtoseconds");
      auto scopePrecisionFs =
          scope->getAttrOfType<IntegerAttr>("dpi_precision_femtoseconds");
      FailureOr<int32_t> unit =
          timeExponent(module, unitFs ? unitFs.getValue().getZExtValue()
                                      : designPrecisionFs);
      FailureOr<int32_t> precision = timeExponent(
          module, scopePrecisionFs ? scopePrecisionFs.getValue().getZExtValue()
                                   : designPrecisionFs);
      if (failed(unit) || failed(precision))
        return failure();
      if (*unit < *precision)
        return scope.emitOpError(
            "DPI scope time unit is finer than its precision");
      dpiUnits.push_back(*unit);
      dpiPrecisions.push_back(*precision);
    }
    Type dpiScopeArray = LLVM::LLVMArrayType::get(dpiScopeType, scopes.size());
    makeAggregateGlobal(
        module, dpiScopeArray, kDPIScopesName, LLVM::Linkage::Internal,
        ".obelisk.execution", [&](OpBuilder &builder) {
          Value records =
              LLVM::ZeroOp::create(builder, module.getLoc(), dpiScopeArray);
          for (auto [index, scope] : llvm::enumerate(scopes)) {
            Value record =
                LLVM::ZeroOp::create(builder, scope.getLoc(), dpiScopeType);
            record = insertValue(
                builder, scope.getLoc(), record,
                integerConstant(builder, scope.getLoc(), i64, scope.getId()),
                0);
            record = insertValue(builder, scope.getLoc(), record,
                                 integerConstant(builder, scope.getLoc(), i64,
                                                 scope.getParent()
                                                     ? *scope.getParent()
                                                     : UINT64_MAX),
                                 1);
            record = insertValue(
                builder, scope.getLoc(), record,
                LLVM::AddressOfOp::create(builder, scope.getLoc(), pointer,
                                          dpiNames[index].getSymName()),
                2);
            record = insertValue(builder, scope.getLoc(), record,
                                 integerConstant(builder, scope.getLoc(), i64,
                                                 dpiScopeNames[index].size()),
                                 3);
            record = insertValue(
                builder, scope.getLoc(), record,
                integerConstant(builder, scope.getLoc(), i32,
                                static_cast<uint32_t>(dpiUnits[index])),
                4);
            record = insertValue(
                builder, scope.getLoc(), record,
                integerConstant(builder, scope.getLoc(), i32,
                                static_cast<uint32_t>(dpiPrecisions[index])),
                5);
            records = LLVM::InsertValueOp::create(
                builder, scope.getLoc(), records, record,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return records;
        });
  }
  if (!exports.empty() &&
      failed(materializeDPIExportDescriptors(module, exports, bytecodeOnly,
                                             pointer, i32, i64, scopes.size())))
    return failure();

  uint32_t flags = 0;
  uint64_t stateBits = 0;
  if (auto attr = module->getAttrOfType<IntegerAttr>(kFlagsAttr))
    flags = static_cast<uint32_t>(attr.getValue().getZExtValue());
  if (!exports.empty())
    flags |= OBELISK_RT_EXECUTION_DPI_EXPORTS;
  if (classBitstream)
    flags |= OBELISK_RT_EXECUTION_CLASS_BITSTREAM;
  if (coverageSchema)
    flags |= OBELISK_RT_EXECUTION_COVERAGE_SCHEMA;
  // IEEE 1800-2023 35.7: exporting a function preserves its SV semantics.
  // Native scheduling must see the same capabilities as the runtime descriptor
  // before choosing a state-publication strategy.
  module->setAttr(kFlagsAttr, IntegerAttr::get(i32, flags));
  if (auto attr = module->getAttrOfType<IntegerAttr>(kStateBitsAttr))
    stateBits = attr.getValue().getZExtValue();
  struct SampledRangeInfo {
    uint64_t sourceBitOffset;
    uint64_t snapshotByteOffset;
    uint64_t bitWidth;
  };
  SmallVector<SampledRangeInfo> sampledRanges;
  uint64_t snapshotBytes = 0;
  if (auto attr =
          module->getAttrOfType<DenseI64ArrayAttr>(kSampledRangesAttr)) {
    ArrayRef<int64_t> values = attr.asArrayRef();
    if (values.size() % 2 != 0)
      return module.emitError("sampled range metadata must contain pairs");
    for (size_t index = 0; index != values.size(); index += 2) {
      uint64_t offset = static_cast<uint64_t>(values[index]);
      uint64_t width = static_cast<uint64_t>(values[index + 1]);
      if (width == 0 || offset > stateBits || width > stateBits - offset ||
          (!sampledRanges.empty() &&
           offset < sampledRanges.back().sourceBitOffset +
                        sampledRanges.back().bitWidth) ||
          width > UINT64_MAX - 7 ||
          snapshotBytes > UINT64_MAX - (width + 7) / 8)
        return module.emitError("sampled range metadata is invalid");
      sampledRanges.push_back({offset, snapshotBytes, width});
      snapshotBytes += (width + 7) / 8;
    }
  }
  if (((flags & OBELISK_RT_EXECUTION_PREPONED_SNAPSHOT) != 0) !=
      !sampledRanges.empty())
    return module.emitError(
        "Preponed snapshot flag and sampled ranges must agree");
  Type sampledRangeType =
      LLVM::LLVMStructType::getLiteral(context, {i64, i64, i64});
  if (!sampledRanges.empty()) {
    Type sampledRangeArray =
        LLVM::LLVMArrayType::get(sampledRangeType, sampledRanges.size());
    makeAggregateGlobal(
        module, sampledRangeArray, kSampledRangesName, LLVM::Linkage::Internal,
        ".obelisk.execution", [&](OpBuilder &builder) {
          Value records =
              LLVM::ZeroOp::create(builder, module.getLoc(), sampledRangeArray);
          for (auto [index, range] : llvm::enumerate(sampledRanges)) {
            Value record = LLVM::ZeroOp::create(builder, module.getLoc(),
                                                sampledRangeType);
            record = insertValue(builder, module.getLoc(), record,
                                 integerConstant(builder, module.getLoc(), i64,
                                                 range.sourceBitOffset),
                                 0);
            record = insertValue(builder, module.getLoc(), record,
                                 integerConstant(builder, module.getLoc(), i64,
                                                 range.snapshotByteOffset),
                                 1);
            record = insertValue(
                builder, module.getLoc(), record,
                integerConstant(builder, module.getLoc(), i64, range.bitWidth),
                2);
            records = LLVM::InsertValueOp::create(
                builder, module.getLoc(), records, record,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return records;
        });
  }
  Type executionExtensionV1Type = LLVM::LLVMStructType::getLiteral(
      context,
      {i32, i32, pointer, i64, pointer, i64, pointer, i64, pointer, i64});
  auto executionType = LLVM::LLVMStructType::getLiteral(
      context, {i32, i32, i64, pointer, i64, pointer, i64, i64, i64, pointer,
                i64, i32, i32, pointer, i64, pointer, i64});
  Type extensionType = coverageSchema || classBitstream || !exports.empty() ||
                               !sampledRanges.empty()
                           ? executionExtensionV1Type
                           : Type{};
  Type executionStorageType =
      extensionType ? Type(LLVM::LLVMStructType::getLiteral(
                          context, {executionType, extensionType}))
                    : Type(executionType);

  // The reserved extension channel is a byte offset from the execution
  // descriptor, not an integerized pointer. Keeping both records in one global
  // makes that offset a plain target-layout constant and avoids the unsupported
  // wasm32 pointer-to-i64 static relocation. Compute both ABI sizes from the
  // selected target DataLayout; sizeof() here would describe the compiler host
  // when a native compiler cross-emits wasm32.
  llvm::LLVMContext layoutContext;
  llvm::Type *layoutPointer = llvm::PointerType::get(layoutContext, 0);
  llvm::Type *layoutI32 = llvm::Type::getInt32Ty(layoutContext);
  llvm::Type *layoutI64 = llvm::Type::getInt64Ty(layoutContext);
  auto *layoutExecution = llvm::StructType::get(
      layoutContext,
      {layoutI32, layoutI32, layoutI64, layoutPointer, layoutI64, layoutPointer,
       layoutI64, layoutI64, layoutI64, layoutPointer, layoutI64, layoutI32,
       layoutI32, layoutPointer, layoutI64, layoutPointer, layoutI64});
  auto *layoutExtensionV1 = llvm::StructType::get(
      layoutContext,
      {layoutI32, layoutI32, layoutPointer, layoutI64, layoutPointer, layoutI64,
       layoutPointer, layoutI64, layoutPointer, layoutI64});
  llvm::StructType *layoutExtension = layoutExtensionV1;
  auto *layoutStorage =
      llvm::StructType::get(layoutContext, {layoutExecution, layoutExtension});
  uint64_t extensionOffset =
      dataLayout.getStructLayout(layoutStorage)->getElementOffset(1);
  uint64_t extensionSize =
      dataLayout.getTypeAllocSize(layoutExtension).getFixedValue();
  uint64_t checksum = bytecode ? read64(bytecode.asArrayRef(), 32) : 0;

  makeAggregateGlobal(
      module, executionStorageType, kExecutionName, LLVM::Linkage::External,
      ".obelisk.execution", [&](OpBuilder &builder) {
        Value value =
            LLVM::ZeroOp::create(builder, module.getLoc(), executionType);
        value = insertValue(
            builder, module.getLoc(), value,
            integerConstant(builder, module.getLoc(), i32, OBELISK_RT_VERSION),
            0);
        value = insertValue(
            builder, module.getLoc(), value,
            integerConstant(builder, module.getLoc(), i32, flags), 1);
        if (extensionType)
          value = insertValue(
              builder, module.getLoc(), value,
              integerConstant(builder, module.getLoc(), i64, extensionOffset),
              2);
        if (bytecode)
          value =
              insertValue(builder, module.getLoc(), value,
                          LLVM::AddressOfOp::create(builder, module.getLoc(),
                                                    pointer, kBytecodeName),
                          3);
        value = insertValue(builder, module.getLoc(), value,
                            integerConstant(builder, module.getLoc(), i64,
                                            bytecode ? bytecode.size() : 0),
                            4);
        if (database)
          value =
              insertValue(builder, module.getLoc(), value,
                          LLVM::AddressOfOp::create(builder, module.getLoc(),
                                                    pointer, kDatabaseName),
                          5);
        value = insertValue(builder, module.getLoc(), value,
                            integerConstant(builder, module.getLoc(), i64,
                                            database ? database.size() : 0),
                            6);
        value = insertValue(
            builder, module.getLoc(), value,
            integerConstant(builder, module.getLoc(), i64, stateBits), 7);
        value = insertValue(
            builder, module.getLoc(), value,
            integerConstant(builder, module.getLoc(), i64, checksum), 8);
        if (!scopes.empty()) {
          value =
              insertValue(builder, module.getLoc(), value,
                          LLVM::AddressOfOp::create(builder, module.getLoc(),
                                                    pointer, kDPIScopesName),
                          9);
          value = insertValue(
              builder, module.getLoc(), value,
              integerConstant(builder, module.getLoc(), i64, scopes.size()),
              10);
          value =
              insertValue(builder, module.getLoc(), value,
                          integerConstant(builder, module.getLoc(), i32,
                                          static_cast<uint32_t>(dpiPrecision)),
                          11);
        }
        if (!activations.empty()) {
          value =
              insertValue(builder, module.getLoc(), value,
                          LLVM::AddressOfOp::create(builder, module.getLoc(),
                                                    pointer, kActivationsName),
                          13);
          value = insertValue(builder, module.getLoc(), value,
                              integerConstant(builder, module.getLoc(), i64,
                                              activations.size()),
                              14);
        }
        if (!observers.empty()) {
          value =
              insertValue(builder, module.getLoc(), value,
                          LLVM::AddressOfOp::create(builder, module.getLoc(),
                                                    pointer, kObserversName),
                          15);
          value = insertValue(
              builder, module.getLoc(), value,
              integerConstant(builder, module.getLoc(), i64, observers.size()),
              16);
        }
        if (!extensionType)
          return value;

        Value extension =
            LLVM::ZeroOp::create(builder, module.getLoc(), extensionType);
        extension =
            insertValue(builder, module.getLoc(), extension,
                        integerConstant(builder, module.getLoc(), i32,
                                        OBELISK_RT_EXECUTION_EXTENSION_VERSION),
                        0);
        extension = insertValue(
            builder, module.getLoc(), extension,
            integerConstant(builder, module.getLoc(), i32, extensionSize), 1);
        if (!sampledRanges.empty())
          extension = insertValue(
              builder, module.getLoc(), extension,
              LLVM::AddressOfOp::create(builder, module.getLoc(), pointer,
                                        kSampledRangesName),
              2);
        extension = insertValue(builder, module.getLoc(), extension,
                                integerConstant(builder, module.getLoc(), i64,
                                                sampledRanges.size()),
                                3);
        if (!exports.empty()) {
          extension =
              insertValue(builder, module.getLoc(), extension,
                          LLVM::AddressOfOp::create(builder, module.getLoc(),
                                                    pointer, kExportsName),
                          4);
          extension = insertValue(
              builder, module.getLoc(), extension,
              integerConstant(builder, module.getLoc(), i64, exports.size()),
              5);
        }
        if (classBitstream) {
          extension = insertValue(
              builder, module.getLoc(), extension,
              LLVM::AddressOfOp::create(builder, module.getLoc(), pointer,
                                        kClassBitstreamName),
              6);
          extension = insertValue(builder, module.getLoc(), extension,
                                  integerConstant(builder, module.getLoc(), i64,
                                                  classBitstream.size()),
                                  7);
        }
        if (coverageSchema) {
          extension = insertValue(
              builder, module.getLoc(), extension,
              LLVM::AddressOfOp::create(builder, module.getLoc(), pointer,
                                        kCoverageSchemaName),
              8);
          extension = insertValue(builder, module.getLoc(), extension,
                                  integerConstant(builder, module.getLoc(), i64,
                                                  coverageSchema.size()),
                                  9);
        }
        Value storage = LLVM::ZeroOp::create(builder, module.getLoc(),
                                             executionStorageType);
        storage = insertValue(builder, module.getLoc(), storage, value, 0);
        return insertValue(builder, module.getLoc(), storage, extension, 1);
      });

  auto entryType =
      LLVM::LLVMStructType::getLiteral(context, {pointer, i32, i32});
  SmallVector<std::pair<std::string, uint32_t>> entries;
  module.walk([&](Operation *operation) {
    // Only process descriptors consume these adapters. Ordinary calls and
    // observers address their bytecode functions by index in the image.
    if (auto function = dyn_cast<sim::SimFuncOp>(operation))
      if (function.getEntryKind() == sim::EntryKind::Function ||
          function.getEntryKind() == sim::EntryKind::Observer)
        return;
    auto index = operation->getAttrOfType<IntegerAttr>(kFunctionAttr);
    auto symbol =
        operation->getAttrOfType<StringAttr>(SymbolTable::getSymbolAttrName());
    if (index && symbol)
      entries.emplace_back(
          symbol.getValue().str(),
          static_cast<uint32_t>(index.getValue().getZExtValue()));
  });
  llvm::sort(entries);
  SymbolTable symbols(module);
  for (auto indexedEntry : llvm::enumerate(entries)) {
    auto index = indexedEntry.index();
    const auto &entry = indexedEntry.value();
    if (index != 0 && entries[index - 1].first == entry.first)
      return module.emitError()
             << "duplicate bytecode symbol '" << entry.first << "'";
    std::string name = entry.first + ".__obelisk_bytecode_entry";
    if (symbols.lookup(name))
      return module.emitError()
             << "symbol collision for bytecode entry '" << name << "'";
    symbols.insert(makeAggregateGlobal(
        module, entryType, name, LLVM::Linkage::Internal, "",
        [&](OpBuilder &builder) {
          Value value =
              LLVM::ZeroOp::create(builder, module.getLoc(), entryType);
          value =
              insertValue(builder, module.getLoc(), value,
                          LLVM::AddressOfOp::create(builder, module.getLoc(),
                                                    pointer, kExecutionName),
                          0);
          return insertValue(
              builder, module.getLoc(), value,
              integerConstant(builder, module.getLoc(), i32, entry.second), 1);
        }));
  }

  // A direct descriptor reference normally retains the database. The explicit
  // llvm.used anchor also preserves reflection-only designs under section GC.
  if (database) {
    Operation *existing = module.lookupSymbol("llvm.used");
    if (!existing)
      existing = module.lookupSymbol("llvm.compiler.used");
    if (existing) {
      auto global = dyn_cast<LLVM::GlobalOp>(existing);
      if (!global ||
          failed(appendRetentionEntry(module, global, pointer, kDatabaseName)))
        return failure();
    } else {
      Type usedType = LLVM::LLVMArrayType::get(pointer, 1);
      makeAggregateGlobal(
          module, usedType, "llvm.used", LLVM::Linkage::Appending,
          "llvm.metadata", [&](OpBuilder &builder) {
            Value value =
                LLVM::ZeroOp::create(builder, module.getLoc(), usedType);
            return insertValue(
                builder, module.getLoc(), value,
                LLVM::AddressOfOp::create(builder, module.getLoc(), pointer,
                                          kDatabaseName),
                0);
          });
    }
  }

  module->setAttr(kMaterializedAttr, UnitAttr::get(context));
  return success();
}

} // namespace obelisk
