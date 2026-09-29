//===- PrepareRuntimeToLLVM.cpp - Prepare target runtime materialization
//---===//

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Pass/PassRegistry.h"
#include "obelisk/Conversion/RuntimeToLLVM.h"
#include "obelisk/Dialect/Runtime/RuntimeOps.h"
#include "obelisk/Dialect/Schedule/ScheduleDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleOps.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"

using namespace mlir;

namespace obelisk {
#define GEN_PASS_DEF_PREPARERUNTIMETOLLVMPASS
#define GEN_PASS_DEF_MATERIALIZERUNTIMEBYTEGLOBALSPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {
class PrepareRuntimeToLLVMPass
    : public impl::PrepareRuntimeToLLVMPassBase<PrepareRuntimeToLLVMPass> {
public:
  void runOnOperation() override {
    ModuleOp module = getOperation();
    auto layout = module->getAttrOfType<StringAttr>("llvm.data_layout");
    if (!layout) {
      module.emitError(
          "runtime lowering requires an explicit llvm.data_layout");
      return signalPassFailure();
    }
    auto parsed = llvm::DataLayout::parse(layout.getValue());
    if (!parsed) {
      module.emitError() << "invalid LLVM data layout: "
                         << llvm::toString(parsed.takeError());
      return signalPassFailure();
    }
    if (failed(validateRuntimeToLLVMPreconditions(module, *parsed)) ||
        failed(materializeEmbeddedSimulationDesign(module, *parsed)) ||
        failed(prepareRuntimeToLLVMByteGlobals(module)))
      return signalPassFailure();
    module->setAttr(preparedRuntimeLLVMAttr, UnitAttr::get(&getContext()));
  }
};
class MaterializeRuntimeByteGlobalsPass
    : public impl::MaterializeRuntimeByteGlobalsPassBase<
          MaterializeRuntimeByteGlobalsPass> {
  void runOnOperation() override {
    if (failed(materializeDeferredRuntimeByteGlobals(getOperation())))
      signalPassFailure();
  }
};
} // namespace

LogicalResult prepareRuntimeToLLVMByteGlobals(ModuleOp module) {
  MLIRContext *context = module.getContext();
  llvm::StringSet<> reservedSymbols;
  for (Operation &operation : *module.getBody())
    if (StringAttr symbol = SymbolTable::getSymbolName(&operation))
      reservedSymbols.insert(symbol.getValue());

  uint64_t ordinal = 0;
  auto allocateName = [&]() {
    std::string name;
    do {
      name = (Twine("__obelisk_rt_bytes.") + Twine(ordinal++)).str();
    } while (!reservedSymbols.insert(name).second);
    return StringAttr::get(context, name);
  };
  module.walk([&](Operation *operation) {
    SmallVector<StringRef> values;
    if (auto bytes = dyn_cast<runtime::RTBytesConstantOp>(operation))
      values.push_back(bytes.getValue());
    else if (auto environment =
                 dyn_cast<runtime::RTFormatEnvironmentOp>(operation)) {
      values.push_back(environment.getScope());
      values.push_back(environment.getLibraryCell());
      values.push_back(environment.getTimeSuffix());
    } else {
      return;
    }
    SmallVector<Attribute> names;
    names.reserve(values.size());
    for (StringRef value : values)
      names.push_back(value.empty() ? StringAttr::get(context, "")
                                    : allocateName());
    operation->setAttr(preparedRuntimeByteGlobalsAttr,
                       ArrayAttr::get(context, names));
  });
  return success();
}

LogicalResult materializeDeferredRuntimeByteGlobals(ModuleOp module) {
  SmallVector<schedule::NativeByteAddressOp> addresses;
  module.walk(
      [&](schedule::NativeByteAddressOp op) { addresses.push_back(op); });
  if (addresses.empty())
    return success();
  SymbolTable symbols(module);
  llvm::MapVector<StringAttr, schedule::NativeByteAddressOp> literals;
  for (auto address : addresses) {
    if (address.getName().empty() || address.getValue().empty() ||
        !llvm::isPowerOf2_64(address.getAlignment()) ||
        cast<LLVM::LLVMPointerType>(address.getType()).getAddressSpace() != 0)
      return address.emitOpError("has invalid deferred byte storage");
    auto [entry, inserted] =
        literals.try_emplace(address.getNameAttr(), address);
    if (inserted) {
      if (symbols.lookup(address.getName()))
        return address.emitOpError("byte global name conflicts with a symbol");
    } else if (entry->second.getValueAttr() != address.getValueAttr() ||
               entry->second.getAlignment() != address.getAlignment()) {
      return address.emitOpError("byte global name has conflicting payloads");
    }
  }
  // IEEE 1800-2023 5.9, 21.2.1: preserve exact bytes and formatting context.
  // A cloned literal keeps its prepared identity; only surviving owners emit
  // storage. Late constants use the backend's primary-partition fallback.
  OpBuilder builder(module.getContext());
  for (auto [name, address] : literals) {
    builder.setInsertionPointToStart(module.getBody());
    auto type = LLVM::LLVMArrayType::get(builder.getI8Type(),
                                         address.getValue().size());
    auto global = LLVM::GlobalOp::create(
        builder, address.getLoc(), type, true, name.getValue(),
        LLVM::Linkage::Internal, false, false, false, address.getValueAttr(),
        address.getAlignmentAttr(), 0, {}, {}, {}, {},
        LLVM::Visibility::Default, {});
    global.setPrivate();
    symbols.insert(global);
  }
  for (auto address : addresses) {
    builder.setInsertionPoint(address);
    Value replacement = LLVM::AddressOfOp::create(
        builder, address.getLoc(), address.getType(), address.getName());
    address.replaceAllUsesWith(replacement);
    address.erase();
  }
  return success();
}

void buildRuntimeToLLVMPipeline(OpPassManager &manager) {
  manager.addPass(createPrepareRuntimeToLLVMPass());
  manager.addPass(createConvertRuntimeToLLVMPass());
}

void registerRuntimeToLLVMPipeline() {
  PassPipelineRegistration<>(
      "convert-obelisk-runtime-to-llvm",
      "Prepare and convert runtime operations to the in-tree LLVM C ABI",
      buildRuntimeToLLVMPipeline);
}
} // namespace obelisk
