#ifndef OBELISK_ANALYSIS_LOGICBITANALYSIS_H
#define OBELISK_ANALYSIS_LOGICBITANALYSIS_H
#include "mlir/IR/Value.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include <optional>

namespace obelisk {
/// Unconditional facts about an SSA snapshot. No stored-state, equality, or
/// initialization assumption is made. Masks are compact words, not per-bit
/// analysis nodes. Large values stay conservative under the analysis budget.
struct LogicBitFacts {
  bool initialized = false;
  llvm::APInt zero, one, known;
  LogicBitFacts() : zero(1, 0), one(1, 0), known(1, 0) {}
  explicit LogicBitFacts(unsigned width)
      : initialized(true), zero(width, 0), one(width, 0), known(width, 0) {}
  bool operator==(const LogicBitFacts &other) const;
  static LogicBitFacts join(const LogicBitFacts &lhs, const LogicBitFacts &rhs);
  void print(llvm::raw_ostream &os) const;
};
class LogicBitAnalysis {
public:
  LogicBitAnalysis() = default;
  explicit LogicBitAnalysis(mlir::Operation *operation);
  std::optional<LogicBitFacts> get(mlir::Value value) const;

private:
  llvm::DenseMap<mlir::Value, LogicBitFacts> facts;
};
} // namespace obelisk
#endif
