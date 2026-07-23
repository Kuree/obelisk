//===- EmbeddedDesign.cpp - Materialize embedded simulation design --------===//

#include "obelisk/Conversion/RuntimeToLLVM.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/SmallVector.h"

#include <cstdint>
#include <cstring>
#include <optional>

using namespace mlir;

namespace obelisk {
namespace {

constexpr StringLiteral kMaterializedAttr = "obelisk.execution.materialized";
constexpr StringLiteral kBytecodeAttr = "obelisk.bytecode.image";
constexpr StringLiteral kDatabaseAttr = "obelisk.design.database";
constexpr StringLiteral kFlagsAttr = "obelisk.execution.flags";
constexpr StringLiteral kStateBitsAttr = "obelisk.execution.state_bits";
constexpr StringLiteral kFunctionAttr = "obelisk.bytecode.function";
constexpr StringLiteral kExecutionName = "__obelisk_execution_descriptor_v1";
constexpr StringLiteral kBytecodeName = "__obelisk_bytecode_image_v1";
constexpr StringLiteral kDatabaseName = "__obelisk_design_database_v1";
constexpr StringLiteral kDPIScopesName = "__obelisk_dpi_scopes_v1";
constexpr StringLiteral kActivationsName = "__obelisk_activations_v1";
constexpr StringLiteral kObserversName = "__obelisk_observers_v1";
constexpr uint32_t kRuntimeABIGeneration = 3;
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

LLVM::GlobalOp makeByteGlobal(ModuleOp module, StringRef name,
                              DenseI8ArrayAttr bytes, StringRef section) {
  OpBuilder builder(module.getContext());
  builder.setInsertionPointToStart(module.getBody());
  Type array = LLVM::LLVMArrayType::get(builder.getI8Type(), bytes.size());
  ArrayRef<int8_t> data = bytes.asArrayRef();
  StringRef contents(reinterpret_cast<const char *>(data.data()), data.size());
  auto global = LLVM::GlobalOp::create(
      builder, module.getLoc(), array, true, LLVM::Linkage::External, name,
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
  if (femtoseconds != 1 || exponent > 0)
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
  auto returnOp = initializer ? dyn_cast<LLVM::ReturnOp>(initializer->getTerminator())
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

LogicalResult materializeEmbeddedSimulationDesign(ModuleOp module) {
  if (module->hasAttr(kMaterializedAttr))
    return success();
  if (module.lookupSymbol(kExecutionName))
    return module.emitError()
           << "symbol collision for reserved execution descriptor '"
           << kExecutionName << "'";

  auto bytecode = module->getAttrOfType<DenseI8ArrayAttr>(kBytecodeAttr);
  auto database = module->getAttrOfType<DenseI8ArrayAttr>(kDatabaseAttr);
  if (bytecode && failed(checkMagic(module, bytecode, StringRef("OBBCDS1\0", 8),
                                    "embedded bytecode")))
    return failure();
  if (database &&
      failed(checkMagic(module, database, StringRef("OBDSGN1\0", 8),
                        "embedded design database")))
    return failure();

  if (bytecode && module.lookupSymbol(kBytecodeName))
    return module.emitError()
           << "symbol collision for reserved bytecode image '" << kBytecodeName
           << "'";
  if (database && module.lookupSymbol(kDatabaseName))
    return module.emitError()
           << "symbol collision for reserved design database '"
           << kDatabaseName << "'";

  if (bytecode)
    makeByteGlobal(module, kBytecodeName, bytecode, ".obelisk.bytecode");
  if (database)
    makeByteGlobal(module, kDatabaseName, database, ".obelisk.design");

  MLIRContext *context = module.getContext();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = IntegerType::get(context, 32);
  Type i64 = IntegerType::get(context, 64);

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
    auto bytecodeFunction =
        function->getAttrOfType<IntegerAttr>(kFunctionAttr);
    activations.push_back(
        {static_cast<uint64_t>(*codeUnitID), function.getSymName().str(),
         bytecodeFunction
             ? std::optional<uint32_t>(static_cast<uint32_t>(
                   bytecodeFunction.getValue().getZExtValue()))
             : std::nullopt});
  });
  llvm::sort(activations, [](const ActivationInfo &lhs,
                             const ActivationInfo &rhs) {
    return lhs.codeUnitID < rhs.codeUnitID;
  });
  for (size_t index = 1; index < activations.size(); ++index)
    if (activations[index - 1].codeUnitID == activations[index].codeUnitID)
      return module.emitError()
             << "duplicate activation code-unit ID "
             << activations[index].codeUnitID;

  struct ObserverInfo {
    uint64_t codeUnitID;
    std::string symbol;
    std::optional<uint32_t> bytecodeFunction;
    uint32_t resultWidth;
    bool fourState;
    SmallVector<std::pair<uint32_t, uint32_t>> captures;
    std::string capturesSymbol;
  };
  SmallVector<ObserverInfo> observers;
  bool invalidObserver = false;
  module.walk([&](sim::SimFuncOp function) {
    if (function.getEntryKind() != sim::EntryKind::Observer)
      return;
    std::optional<int64_t> codeUnitID = function.getCodeUnitId();
    if (!codeUnitID || *codeUnitID <= 0 ||
        function.getFunctionType().getNumResults() != 1)
      return;
    std::optional<unsigned> width =
        sim::getPackedWidth(function.getFunctionType().getResult(0));
    if (!width)
      return;
    ObserverInfo info{
        static_cast<uint64_t>(*codeUnitID),
        function.getSymName().str(),
        std::nullopt,
        *width,
        isa<sim::LogicType>(function.getFunctionType().getResult(0)),
        {},
        {}};
    if (auto bytecodeFunction =
            function->getAttrOfType<IntegerAttr>(kFunctionAttr))
      info.bytecodeFunction = static_cast<uint32_t>(
          bytecodeFunction.getValue().getZExtValue());
    for (unsigned index = 1; index < function.getNumArguments(); ++index) {
      Type type = function.getArgumentTypes()[index];
      uint32_t kind = 0;
      uint32_t captureWidth = 0;
      if (isa<sim::RefType>(type))
        kind = 1, captureWidth = sim::getPackedWidth(
                                      cast<sim::RefType>(type).getElementType())
                                      .value_or(0);
      else if (isa<sim::NetType>(type))
        kind = 2, captureWidth = sim::getPackedWidth(
                                      cast<sim::NetType>(type).getElementType())
                                      .value_or(0);
      else if (isa<sim::EventType>(type))
        kind = 3, captureWidth = 1;
      else if (isa<sim::DriverType>(type))
        kind = 4, captureWidth = sim::getPackedWidth(
                                      cast<sim::DriverType>(type).getElementType())
                                      .value_or(0);
      if (kind == 0 || captureWidth == 0) {
        function.emitError()
            << "observer capture #" << index - 1
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
  llvm::sort(observers, [](const ObserverInfo &lhs,
                           const ObserverInfo &rhs) {
    return lhs.codeUnitID < rhs.codeUnitID;
  });
  for (size_t index = 1; index < observers.size(); ++index)
    if (observers[index - 1].codeUnitID == observers[index].codeUnitID)
      return module.emitError()
             << "duplicate observer code-unit ID "
             << observers[index].codeUnitID;

  // The immutable descriptor global is materialized before observer bodies are
  // lowered.  Give every uniform native evaluator thunk an LLVM declaration
  // now so address-of verification never depends on a later pass phase.
  for (const ObserverInfo &observer : observers) {
    std::string thunkName = observer.symbol + ".__obelisk_observer";
    if (module.lookupSymbol<LLVM::LLVMFuncOp>(thunkName))
      continue;
    OpBuilder builder(context);
    builder.setInsertionPointToStart(module.getBody());
    LLVM::LLVMFuncOp::create(
        builder, module.getLoc(), thunkName,
        LLVM::LLVMFunctionType::get(
            i32, {pointer, pointer, i32, pointer, pointer, i32}, false));
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
            Value record = LLVM::ZeroOp::create(
                builder, module.getLoc(), activationType);
            record = insertValue(
                builder, module.getLoc(), record,
                integerConstant(builder, module.getLoc(), i64,
                                activation.codeUnitID),
                0);
            record = insertValue(
                builder, module.getLoc(), record,
                LLVM::AddressOfOp::create(
                    builder, module.getLoc(), pointer,
                    activation.symbol + ".__obelisk_process_descriptor"),
                1);
            uint32_t flags = kActivationHasNative;
            uint32_t bytecodeFunction = kActivationNoBytecode;
            if (activation.bytecodeFunction) {
              flags |= kActivationHasBytecode;
              bytecodeFunction = *activation.bytecodeFunction;
            }
            record = insertValue(
                builder, module.getLoc(), record,
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
    Type capturesType = LLVM::LLVMArrayType::get(
        observerCaptureType, observer.captures.size());
    makeAggregateGlobal(
        module, capturesType, observer.capturesSymbol,
        LLVM::Linkage::Internal, ".obelisk.execution",
        [&](OpBuilder &builder) {
          Value values =
              LLVM::ZeroOp::create(builder, module.getLoc(), capturesType);
          for (auto [index, capture] :
               llvm::enumerate(observer.captures)) {
            Value value = LLVM::ZeroOp::create(
                builder, module.getLoc(), observerCaptureType);
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
            record = insertValue(
                builder, module.getLoc(), record,
                integerConstant(builder, module.getLoc(), i64,
                                observer.codeUnitID),
                0);
            if (!observer.captures.empty())
              record = insertValue(
                  builder, module.getLoc(), record,
                  LLVM::AddressOfOp::create(
                      builder, module.getLoc(), pointer,
                      observer.capturesSymbol),
                  1);
            record = insertValue(
                builder, module.getLoc(), record,
                integerConstant(builder, module.getLoc(), i32,
                                observer.captures.size()),
                2);
            record = insertValue(
                builder, module.getLoc(), record,
                integerConstant(builder, module.getLoc(), i32,
                                observer.resultWidth),
                3);
            record = insertValue(
                builder, module.getLoc(), record,
                integerConstant(builder, module.getLoc(), i32,
                                observer.fourState ? 1 : 0),
                4);
            record = insertValue(
                builder, module.getLoc(), record,
                integerConstant(
                    builder, module.getLoc(), i32,
                    observer.bytecodeFunction.value_or(UINT32_MAX)),
                5);
            record = insertValue(
                builder, module.getLoc(), record,
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
          index == 0
              ? std::string("$root")
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
          nameBuilder, scope.getLoc(), nameType, true,
          LLVM::Linkage::Internal, globalName,
          nameBuilder.getStringAttr(name), 1));
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
    FailureOr<int32_t> exponent =
        timeExponent(module, designPrecisionFs);
    if (failed(exponent))
      return failure();
    dpiPrecision = *exponent;
    for (sim::SimScopeDeclOp scope : scopes) {
      auto unitFs =
          scope->getAttrOfType<IntegerAttr>("dpi_unit_femtoseconds");
      auto scopePrecisionFs =
          scope->getAttrOfType<IntegerAttr>(
              "dpi_precision_femtoseconds");
      FailureOr<int32_t> unit = timeExponent(
          module, unitFs ? unitFs.getValue().getZExtValue()
                         : designPrecisionFs);
      FailureOr<int32_t> precision = timeExponent(
          module, scopePrecisionFs
                      ? scopePrecisionFs.getValue().getZExtValue()
                      : designPrecisionFs);
      if (failed(unit) || failed(precision))
        return failure();
      if (*unit < *precision)
        return scope.emitOpError(
            "DPI scope time unit is finer than its precision");
      dpiUnits.push_back(*unit);
      dpiPrecisions.push_back(*precision);
    }
    Type dpiScopeArray =
        LLVM::LLVMArrayType::get(dpiScopeType, scopes.size());
    makeAggregateGlobal(
        module, dpiScopeArray, kDPIScopesName, LLVM::Linkage::Internal,
        ".obelisk.execution", [&](OpBuilder &builder) {
          Value records =
              LLVM::ZeroOp::create(builder, module.getLoc(), dpiScopeArray);
          for (auto [index, scope] : llvm::enumerate(scopes)) {
            Value record = LLVM::ZeroOp::create(
                builder, scope.getLoc(), dpiScopeType);
            record = insertValue(
                builder, scope.getLoc(), record,
                integerConstant(builder, scope.getLoc(), i64, scope.getId()),
                0);
            record = insertValue(
                builder, scope.getLoc(), record,
                integerConstant(
                    builder, scope.getLoc(), i64,
                    scope.getParent()
                        ? *scope.getParent()
                        : UINT64_MAX),
                1);
            record = insertValue(
                builder, scope.getLoc(), record,
                LLVM::AddressOfOp::create(builder, scope.getLoc(), pointer,
                                          dpiNames[index].getSymName()),
                2);
            record = insertValue(
                builder, scope.getLoc(), record,
                integerConstant(
                    builder, scope.getLoc(), i64,
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
                                static_cast<uint32_t>(
                                    dpiPrecisions[index])),
                5);
            records = LLVM::InsertValueOp::create(
                builder, scope.getLoc(), records, record,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return records;
        });
  }
  auto executionType = LLVM::LLVMStructType::getLiteral(
      context, {i32, i32, i32, i32, pointer, i64, pointer, i64, i64, i64,
                pointer, i64, i32, i32, pointer, i64, pointer, i64});
  uint32_t flags = 0;
  uint64_t stateBits = 0;
  if (auto attr = module->getAttrOfType<IntegerAttr>(kFlagsAttr))
    flags = static_cast<uint32_t>(attr.getValue().getZExtValue());
  if (auto attr = module->getAttrOfType<IntegerAttr>(kStateBitsAttr))
    stateBits = attr.getValue().getZExtValue();
  uint64_t checksum = bytecode ? read64(bytecode.asArrayRef(), 32) : 0;

  makeAggregateGlobal(
      module, executionType, kExecutionName, LLVM::Linkage::External,
      ".obelisk.execution", [&](OpBuilder &builder) {
        Value value = LLVM::ZeroOp::create(builder, module.getLoc(),
                                           executionType);
        value = insertValue(builder, module.getLoc(), value,
                            integerConstant(builder, module.getLoc(), i32, 2),
                            0);
        value = insertValue(builder, module.getLoc(), value,
                            integerConstant(builder, module.getLoc(), i32,
                                            kRuntimeABIGeneration),
                            1);
        value = insertValue(
            builder, module.getLoc(), value,
            integerConstant(builder, module.getLoc(), i32, flags), 2);
        if (bytecode)
          value = insertValue(
              builder, module.getLoc(), value,
              LLVM::AddressOfOp::create(builder, module.getLoc(), pointer,
                                        kBytecodeName),
              4);
        value = insertValue(
            builder, module.getLoc(), value,
            integerConstant(builder, module.getLoc(), i64,
                            bytecode ? bytecode.size() : 0),
            5);
        if (database)
          value = insertValue(
              builder, module.getLoc(), value,
              LLVM::AddressOfOp::create(builder, module.getLoc(), pointer,
                                        kDatabaseName),
              6);
        value = insertValue(
            builder, module.getLoc(), value,
            integerConstant(builder, module.getLoc(), i64,
                            database ? database.size() : 0),
            7);
        value = insertValue(
            builder, module.getLoc(), value,
            integerConstant(builder, module.getLoc(), i64, stateBits), 8);
        value = insertValue(
            builder, module.getLoc(), value,
            integerConstant(builder, module.getLoc(), i64, checksum), 9);
        if (!scopes.empty()) {
          value = insertValue(
              builder, module.getLoc(), value,
              LLVM::AddressOfOp::create(builder, module.getLoc(), pointer,
                                        kDPIScopesName),
              10);
          value = insertValue(
              builder, module.getLoc(), value,
              integerConstant(builder, module.getLoc(), i64, scopes.size()),
              11);
          value = insertValue(
              builder, module.getLoc(), value,
              integerConstant(builder, module.getLoc(), i32,
                              static_cast<uint32_t>(dpiPrecision)),
              12);
        }
        if (!activations.empty()) {
          value = insertValue(
              builder, module.getLoc(), value,
              LLVM::AddressOfOp::create(builder, module.getLoc(), pointer,
                                        kActivationsName),
              14);
          value = insertValue(
              builder, module.getLoc(), value,
              integerConstant(builder, module.getLoc(), i64,
                              activations.size()),
              15);
        }
        if (!observers.empty()) {
          value = insertValue(
              builder, module.getLoc(), value,
              LLVM::AddressOfOp::create(builder, module.getLoc(), pointer,
                                        kObserversName),
              16);
          value = insertValue(
              builder, module.getLoc(), value,
              integerConstant(builder, module.getLoc(), i64,
                              observers.size()),
              17);
        }
        return value;
      });

  auto entryType =
      LLVM::LLVMStructType::getLiteral(context, {pointer, i32, i32});
  SmallVector<std::pair<std::string, uint32_t>> entries;
  module.walk([&](Operation *operation) {
    auto index = operation->getAttrOfType<IntegerAttr>(kFunctionAttr);
    auto symbol = operation->getAttrOfType<StringAttr>(
        SymbolTable::getSymbolAttrName());
    if (index && symbol)
      entries.emplace_back(symbol.getValue().str(),
                           static_cast<uint32_t>(
                               index.getValue().getZExtValue()));
  });
  llvm::sort(entries);
  for (auto [index, entry] : llvm::enumerate(entries)) {
    if (index != 0 && entries[index - 1].first == entry.first)
      return module.emitError()
             << "duplicate bytecode symbol '" << entry.first << "'";
    std::string name = entry.first + ".__obelisk_bytecode_entry";
    if (module.lookupSymbol(name))
      return module.emitError()
             << "symbol collision for bytecode entry '" << name << "'";
    makeAggregateGlobal(
        module, entryType, name, LLVM::Linkage::Internal, "",
        [&](OpBuilder &builder) {
          Value value = LLVM::ZeroOp::create(builder, module.getLoc(),
                                             entryType);
          value = insertValue(
              builder, module.getLoc(), value,
              LLVM::AddressOfOp::create(builder, module.getLoc(), pointer,
                                        kExecutionName),
              0);
          return insertValue(
              builder, module.getLoc(), value,
              integerConstant(builder, module.getLoc(), i32, entry.second), 1);
        });
  }

  // A direct descriptor reference normally retains the database. The explicit
  // llvm.used anchor also preserves reflection-only designs under section GC.
  if (database) {
    Operation *existing = module.lookupSymbol("llvm.used");
    if (!existing)
      existing = module.lookupSymbol("llvm.compiler.used");
    if (existing) {
      auto global = dyn_cast<LLVM::GlobalOp>(existing);
      if (!global || failed(appendRetentionEntry(module, global, pointer,
                                                 kDatabaseName)))
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
