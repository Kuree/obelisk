//===- SimulationSchedulerLowering.cpp - Native scheduler support -------===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Transforms/DialectConversion.h"

using namespace mlir;

namespace obelisk::detail {
namespace {

class SpawnTypeConversion final : public OpConversionPattern<sim::SimSpawnOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimSpawnOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    for (auto [operand, converted] :
         llvm::zip_equal(op.getOperands(), adaptor.getOperands()))
      if (isa<sim::RefType>(operand.getType()) && converted.size() == 1)
        emitNativeStateRetain(rewriter, op.getLoc(), converted.front());
    OperationState state(op.getLoc(), op->getName());
    state.addOperands(flatten(adaptor.getOperands()));
    state.addTypes(rewriter.getI64Type());
    state.addAttributes(op->getAttrs());
    Operation *replacement = rewriter.create(state);
    rewriter.replaceOp(op, replacement->getResults());
    return success();
  }
};

} // namespace

void populateSchedulerToLLVMConversionPatterns(RewritePatternSet &patterns,
                                               TypeConverter &converter) {
  patterns.add<SpawnTypeConversion>(converter, patterns.getContext());
}

void materializeNativeSchedulerGlobals(ModuleOp module) {
  OpBuilder builder(module.getContext());
  builder.setInsertionPointToStart(module.getBody());
  Location location = module.getLoc();

  Type pointer = LLVM::LLVMPointerType::get(module.getContext());
  constexpr llvm::StringLiteral currentContextName =
      "__obelisk_current_context";
  if (!module.lookupSymbol(currentContextName)) {
    auto global = LLVM::GlobalOp::create(
        builder, location, pointer, false, LLVM::Linkage::Internal,
        currentContextName, Attribute{}, 8, 0, false, true);
    Block *block = new Block;
    global.getInitializerRegion().push_back(block);
    builder.setInsertionPointToStart(block);
    LLVM::ReturnOp::create(builder, location,
                           LLVM::ZeroOp::create(builder, location, pointer));
    builder.setInsertionPointToStart(module.getBody());
  }

  constexpr llvm::StringLiteral specializationFastName =
      "__obelisk_static_specialization_fast_v1";
  if (!module.lookupSymbol(specializationFastName)) {
    Type i32 = builder.getI32Type();
    auto global = LLVM::GlobalOp::create(
        builder, location, i32, false, LLVM::Linkage::Internal,
        specializationFastName, Attribute{}, 4);
    Block *block = new Block;
    global.getInitializerRegion().push_back(block);
    builder.setInsertionPointToStart(block);
    LLVM::ReturnOp::create(builder, location,
                           llvmConstant(builder, location, i32, uint32_t{0}));
  }
}

} // namespace obelisk::detail
