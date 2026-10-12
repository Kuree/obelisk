//===- CompileRandomPlans.cpp - Materialize exact random plans -----------===//

#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Dialect/Simulation/Transforms/Passes.h"
#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Solver/ConstraintSolver.h"

#include "mlir/IR/BuiltinOps.h"

using namespace mlir;

namespace obelisk {
#define GEN_PASS_DEF_OBELISKSIMCOMPILERANDOMPLANSPASS
#include "obelisk/Dialect/Simulation/Transforms/Passes.h.inc"

namespace {
uint32_t read32(const uint8_t *p) {
  uint32_t value = 0;
  for (unsigned byte = 0; byte != 4; ++byte)
    value |= uint32_t(p[byte]) << (byte * 8);
  return value;
}

template <typename Op> LogicalResult compile(Op op) {
  StringRef bytes = op.getProgram();
  if (bytes.size() < OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE)
    return op.emitOpError("has a truncated random program");
  const auto *data = reinterpret_cast<const uint8_t *>(bytes.data());
  uint32_t flags = read32(data + 20);
  if ((flags & OBELISK_RT_RANDOM_PROGRAM_HAS_SAMPLER_PLAN) ||
      !(flags & OBELISK_RT_RANDOM_PROGRAM_SMT_UNIFORM))
    return success();
  std::vector<uint32_t> captures;
  for (Value capture : op.getCaptures())
    captures.push_back(cast<IntegerType>(capture.getType()).getWidth());
  auto plan = solver::compileRandomSamplerPlan(data, bytes.size(), captures);
  if (!plan)
    return op.emitOpError("cannot compile an exact parameterized random plan");
  std::vector<uint8_t> program(data, data + bytes.size());
  flags |= OBELISK_RT_RANDOM_PROGRAM_HAS_SAMPLER_PLAN;
  for (unsigned byte = 0; byte != 4; ++byte)
    program[20 + byte] = static_cast<uint8_t>(flags >> (byte * 8));
  program.insert(program.end(), plan->begin(), plan->end());
  op.setProgramAttr(
      StringAttr::get(op->getContext(),
                      StringRef(reinterpret_cast<const char *>(program.data()),
                                program.size())));
  return success();
}

class ObeliskSimCompileRandomPlansPass final
    : public impl::ObeliskSimCompileRandomPlansPassBase<
          ObeliskSimCompileRandomPlansPass> {
public:
  using Base::Base;
  void runOnOperation() override {
    WalkResult result = getOperation().walk([&](Operation *op) {
      LogicalResult compiled = success();
      if (auto narrow = dyn_cast<sim::SimRandomSolveOp>(op))
        compiled = compile(narrow);
      else if (auto wide = dyn_cast<sim::SimRandomSolveWideOp>(op))
        compiled = compile(wide);
      return failed(compiled) ? WalkResult::interrupt() : WalkResult::advance();
    });
    if (result.wasInterrupted())
      signalPassFailure();
  }
};
} // namespace
} // namespace obelisk
