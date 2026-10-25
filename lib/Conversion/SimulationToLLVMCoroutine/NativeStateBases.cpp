//===- NativeStateBases.cpp - Address native state through one base -------===//
//
// Native state lives in one internal global per plane. Loading each plane's
// address once keeps accesses as GEPs off a shared base rather than constant
// expressions that code generation rematerializes in each block. These opaque
// pointer loads lose the original global's alias identity, so record that the
// two physical plane allocations are separate storage. Single-block functions
// keep direct addresses, and coroutines do not carry a base across suspension.
//
//===----------------------------------------------------------------------===//

#include "obelisk/Conversion/SimulationToLLVMCoroutine.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Threading.h"
#include "obelisk/Analysis/ParallelWalk.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_ADDRESSNATIVESTATETHROUGHBASESPASS
#include "obelisk/Conversion/Passes.h.inc"

LogicalResult addressNativeStateThroughBases(ModuleOp module) {
  static constexpr StringLiteral planes[] = {"__obelisk_state_value",
                                             "__obelisk_state_unknown"};
  MLIRContext *context = module.getContext();
  SymbolTable symbols(module);
  OpBuilder builder(context);
  auto pointer = LLVM::LLVMPointerType::get(context);
  struct Base {
    StringAttr name;
    uint64_t bytes;
  };
  DenseMap<StringAttr, Base> bases;
  for (StringRef plane : planes) {
    auto global = symbols.lookup<LLVM::GlobalOp>(plane);
    if (!global)
      continue;
    auto array = dyn_cast<LLVM::LLVMArrayType>(global.getGlobalType());
    if (!array || !array.getElementType().isInteger(8))
      return global.emitError("native state plane must be a byte array");
    std::string name = (plane + "_base").str();
    auto base = symbols.lookup<LLVM::GlobalOp>(name);
    if (!base) {
      if (symbols.lookup(name))
        return global.emitError("native state base symbol is not a global");
      // The private constant has only its static initializer. The externally
      // initialized flag keeps LLVM from folding the pointer into each use;
      // it does not install a writer or change the initialized address.
      builder.setInsertionPointAfter(global);
      base = LLVM::GlobalOp::create(builder, global.getLoc(), pointer,
                                    /*isConstant=*/true,
                                    LLVM::Linkage::Internal, name, Attribute(),
                                    /*alignment=*/8);
      base.setExternallyInitialized(true);
      Block *initializer = builder.createBlock(&base.getInitializerRegion());
      builder.setInsertionPointToStart(initializer);
      Value address =
          LLVM::AddressOfOp::create(builder, global.getLoc(), pointer, plane);
      LLVM::ReturnOp::create(builder, global.getLoc(), address);
    } else {
      Region &initializer = base.getInitializerRegion();
      LLVM::AddressOfOp address;
      LLVM::ReturnOp result;
      if (initializer.hasOneBlock() &&
          initializer.front().getOperations().size() == 2) {
        address = dyn_cast<LLVM::AddressOfOp>(initializer.front().front());
        result = dyn_cast<LLVM::ReturnOp>(initializer.front().back());
      }
      if (base.getGlobalType() != pointer || !base.getConstant() ||
          base.getLinkage() != LLVM::Linkage::Internal ||
          !base.getExternallyInitialized() ||
          base.getAlignment().value_or(0) < 8 || base.getAddrSpace() != 0 ||
          !address || !result || result.getNumOperands() != 1 ||
          result.getOperand(0) != address.getResult() ||
          address.getGlobalName() != plane)
        return base.emitError(
            "native state base must be a private externally initialized "
            "aligned constant pointer initialized to its plane address");
    }
    bases[global.getSymNameAttr()] = {base.getSymNameAttr(),
                                      array.getNumElements()};
  }
  if (bases.empty())
    return success();
  // Each function rewrites only its own body.
  SmallVector<LLVM::LLVMFuncOp> functions;
  for (auto function : module.getOps<LLVM::LLVMFuncOp>())
    if (!function.isExternal())
      functions.push_back(function);
  analysis::parallelForChunked(context, functions.size(), [&](size_t index) {
    LLVM::LLVMFuncOp function = functions[index];
    OpBuilder builder(context);
    // A coroutine would carry the base across its suspensions in the frame.
    llvm::MapVector<StringAttr, SmallVector<LLVM::AddressOfOp>> uses;
    bool coroutine = false;
    function.walk([&](Operation *operation) {
      if (isa<LLVM::CoroBeginOp>(operation))
        coroutine = true;
      else if (auto address = dyn_cast<LLVM::AddressOfOp>(operation))
        if (bases.count(address.getGlobalNameAttr().getAttr()))
          uses[address.getGlobalNameAttr().getAttr()].push_back(address);
    });
    if (coroutine || uses.empty())
      return;
    // Within one block, code generation already folds the smallest offset
    // into the plane address and reaches the rest with immediates.
    DenseSet<Block *> blocks;
    for (auto &[plane, addresses] : uses)
      for (LLVM::AddressOfOp address : addresses)
        for (Operation *user : address->getUsers())
          blocks.insert(user->getBlock());
    if (blocks.size() < 2)
      return;
    builder.setInsertionPointToStart(&function.getBody().front());
    SmallVector<Value, 2> loadedBases;
    SmallVector<uint64_t, 2> planeBytes;
    for (auto &[plane, addresses] : uses) {
      Base base = bases.lookup(plane);
      Location location = addresses.front().getLoc();
      Value slot =
          LLVM::AddressOfOp::create(builder, location, pointer, base.name);
      auto load = LLVM::LoadOp::create(builder, location, pointer, slot,
                                       /*alignment=*/8, /*isVolatile=*/false,
                                       /*isNonTemporal=*/false,
                                       /*isInvariant=*/true);
      load.setDereferenceableAttr(
          LLVM::DereferenceableAttr::get(context, base.bytes, false));
      loadedBases.push_back(load);
      planeBytes.push_back(base.bytes);
      for (LLVM::AddressOfOp address : addresses)
        address.replaceAllUsesWith(load.getResult());
    }
    if (loadedBases.size() == 2) {
      // The bases point to different global allocations, not different source
      // signals within one allocation. LLVM's separate_storage assumption
      // restores that fact without restricting unknown pointer arguments or
      // overlapping accesses in one plane. Addresses and ordered value/X/Z
      // effects remain identical (IEEE 1800-2023 4.3, 4.6, 6.3.1).
      // Repeat the existing dereferenceable facts in the same assumption:
      // otherwise LLVM 22 InstCombine can sink a base load and drop the
      // separate_storage bundle before redundant-load elimination sees it.
      Location location = function.getLoc();
      Value condition = LLVM::ConstantOp::create(
          builder, location, builder.getI1Type(), builder.getBoolAttr(true));
      SmallVector<Value, 2> left{loadedBases[0],
                                 LLVM::ConstantOp::create(builder, location,
                                                          builder.getI64Type(),
                                                          planeBytes[0])};
      SmallVector<Value, 2> right{loadedBases[1],
                                  LLVM::ConstantOp::create(builder, location,
                                                           builder.getI64Type(),
                                                           planeBytes[1])};
      SmallVector<ValueRange, 3> bundles{loadedBases, left, right};
      LLVM::AssumeOp::create(
          builder, location, condition, bundles,
          builder.getStrArrayAttr(
              {"separate_storage", "dereferenceable", "dereferenceable"}));
    }
    for (auto &[plane, addresses] : uses)
      for (LLVM::AddressOfOp address : addresses)
        address.erase();
  });
  return success();
}

namespace {
class AddressNativeStateThroughBasesPass final
    : public impl::AddressNativeStateThroughBasesPassBase<
          AddressNativeStateThroughBasesPass> {
  void runOnOperation() override {
    if (failed(addressNativeStateThroughBases(getOperation())))
      signalPassFailure();
  }
};
} // namespace

} // namespace obelisk
