#ifndef TALOS_DIALECT_SCHEDULE_TRANSFORMS_PASSES_H
#define TALOS_DIALECT_SCHEDULE_TRANSFORMS_PASSES_H
#include "mlir/Pass/Pass.h"
namespace talos {
namespace sim {
class SimFuncOp;
}
#define GEN_PASS_DECL
#include "talos/Dialect/Schedule/Transforms/Passes.h.inc"
#define GEN_PASS_REGISTRATION
#include "talos/Dialect/Schedule/Transforms/Passes.h.inc"
} // namespace talos
#endif
