//===- NativeStateBases.cpp - Address native state through one base -------===//
//
// Native state lives in one internal global per plane, and lowering addresses
// every root as a constant offset from it. LLVM folds those into constant
// expressions, so code generation materializes the global's address together
// with each offset, again in every block that accesses it. Loading each
// plane's address once per function from a global the optimizer cannot see
// through keeps every access a GEP instruction off that base: CodeGenPrepare
// then rebases nearby offsets onto shared bases that serve the whole
// function. Within a single block, code generation already folds the smallest
// offset into the plane address and reaches the others with immediates, which
// an opaque base would prevent, so only functions that access state in several
// blocks change. The base global is written only by its static initializer, so
// the load is invariant, and it is dereferenceable for the whole plane.
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
      // The optimizer must not fold loads of the base to the plane address.
      builder.setInsertionPointAfter(global);
      base = LLVM::GlobalOp::create(builder, global.getLoc(), pointer,
                                    /*isConstant=*/false,
                                    LLVM::Linkage::Internal, name, Attribute(),
                                    /*alignment=*/8);
      base.setExternallyInitialized(true);
      Block *initializer = builder.createBlock(&base.getInitializerRegion());
      builder.setInsertionPointToStart(initializer);
      Value address =
          LLVM::AddressOfOp::create(builder, global.getLoc(), pointer, plane);
      LLVM::ReturnOp::create(builder, global.getLoc(), address);
    } else if (base.getGlobalType() != pointer ||
               !base.getExternallyInitialized()) {
      return base.emitError("native state base must be an externally "
                            "initialized pointer");
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
      for (LLVM::AddressOfOp address : addresses)
        address.replaceAllUsesWith(load.getResult());
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
