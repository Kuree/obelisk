//===- SimulationStatePlaneMaterialization.cpp - Native state globals -===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"

#include "llvm/ADT/STLExtras.h"

using namespace mlir;

namespace obelisk::detail {

FailureOr<NativeStateLayout> buildNativeStateLayout(ModuleOp module) {
  FailureOr<analysis::NativeStateLayoutAnalysis> analyzed =
      analysis::NativeStateLayoutAnalysis::compute(module);
  if (failed(analyzed))
    return failure();
  NativeStateLayout layout;
  static_cast<analysis::NativeStateLayoutAnalysis &>(layout) =
      std::move(*analyzed);
  return layout;
}


LLVM::GlobalOp
makeStatePlane(ModuleOp module, StringRef name, uint64_t bytes, bool unknown,
               const NativeStateLayout &layout) {
  OpBuilder builder(module.getContext());
  builder.setInsertionPointToStart(module.getBody());
  Location location = module.getLoc();
  Type i8 = builder.getI8Type();
  Type array = LLVM::LLVMArrayType::get(i8, bytes);
  SmallVector<uint8_t> initial(bytes, 0);
  if (unknown) {
    for (const NativeStateLayout::Bound &bound : layout.bounds) {
      if (!bound.fourState)
        continue;
      for (unsigned bit = 0; bit < bound.width; ++bit) {
        uint64_t absolute = bound.offset + bit;
        initial[absolute / 8] |= static_cast<uint8_t>(1u << (absolute % 8));
      }
    }
  } else {
    for (const NativeStateLayout::Driver &driver : layout.driverLayouts)
      for (unsigned bit = 0; bit < driver.width; ++bit) {
        uint64_t absolute = driver.offset + bit;
        initial[absolute / 8] |= static_cast<uint8_t>(1u << (absolute % 8));
      }
    for (const NativeStateLayout::Net &net : layout.netLayouts) {
      if (!net.fourState)
        continue;
      for (unsigned bit = 0; bit < net.width; ++bit) {
        uint64_t absolute = net.offset + bit;
        initial[absolute / 8] |= static_cast<uint8_t>(1u << (absolute % 8));
      }
    }
  }
  // A plane with any set bit is handed over as one blob. Building it with an
  // insertvalue per set byte is quadratic: each insert constant-folds into a
  // fresh `bytes`-element ConstantArray, so a design holding a large unpacked
  // array spends minutes here on an initializer LLVM prints in one line.
  bool anySet = llvm::any_of(initial, [](uint8_t byte) { return byte != 0; });
  if (anySet) {
    StringRef contents(reinterpret_cast<const char *>(initial.data()),
                       initial.size());
    return LLVM::GlobalOp::create(builder, location, array, false,
                                  LLVM::Linkage::Internal, name,
                                  builder.getStringAttr(contents), 8);
  }
  auto global =
      LLVM::GlobalOp::create(builder, location, array, false,
                             LLVM::Linkage::Internal, name, Attribute{}, 8);
  Block *block = new Block;
  global.getInitializerRegion().push_back(block);
  builder.setInsertionPointToStart(block);
  LLVM::ReturnOp::create(builder, location,
                         LLVM::ZeroOp::create(builder, location, array));
  return global;
}

} // namespace obelisk::detail
