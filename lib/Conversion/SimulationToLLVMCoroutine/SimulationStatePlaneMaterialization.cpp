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
               ArrayRef<NativeStateLayout::Driver> highImpedanceDrivers,
               ArrayRef<NativeStateLayout::Net> highImpedanceNets) {
  OpBuilder builder(module.getContext());
  builder.setInsertionPointToStart(module.getBody());
  Location location = module.getLoc();
  Type i8 = builder.getI8Type();
  Type array = LLVM::LLVMArrayType::get(i8, bytes);
  auto global =
      LLVM::GlobalOp::create(builder, location, array, false,
                             LLVM::Linkage::Internal, name, Attribute{}, 8);
  Block *block = new Block;
  global.getInitializerRegion().push_back(block);
  builder.setInsertionPointToStart(block);
  Value value = LLVM::ZeroOp::create(builder, location, array);
  SmallVector<uint8_t> initial(bytes, unknown ? UINT8_MAX : 0);
  if (!unknown)
    for (const NativeStateLayout::Driver &driver : highImpedanceDrivers)
      for (unsigned bit = 0; bit < driver.width; ++bit) {
        uint64_t absolute = driver.offset + bit;
        initial[absolute / 8] |= static_cast<uint8_t>(1u << (absolute % 8));
      }
  if (!unknown)
    for (const NativeStateLayout::Net &net : highImpedanceNets) {
      if (!net.fourState)
        continue;
      for (unsigned bit = 0; bit < net.width; ++bit) {
        uint64_t absolute = net.offset + bit;
        initial[absolute / 8] |= static_cast<uint8_t>(1u << (absolute % 8));
      }
    }
  for (auto [index, byte] : llvm::enumerate(initial))
    if (byte != 0)
      value = LLVM::InsertValueOp::create(
          builder, location, value, llvmConstant(builder, location, i8, byte),
          ArrayRef<int64_t>{static_cast<int64_t>(index)});
  LLVM::ReturnOp::create(builder, location, value);
  return global;
}

} // namespace obelisk::detail

