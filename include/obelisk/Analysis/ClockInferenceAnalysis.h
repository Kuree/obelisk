//===- ClockInferenceAnalysis.h - Clock data flow ----------------*- C++ -*-===//

#ifndef OBELISK_ANALYSIS_CLOCKINFERENCEANALYSIS_H
#define OBELISK_ANALYSIS_CLOCKINFERENCEANALYSIS_H

#include "obelisk/Analysis/NativeStateLayoutAnalysis.h"
#include "llvm/ADT/DenseMap.h"

namespace mlir {
class ModuleOp;
class Operation;
} // namespace mlir

namespace obelisk::analysis {

/// A physical state handle and absolute bit offset. Descriptor aliases share
/// this identity, so a second writer cannot hide behind a port projection.
using ClockBit = std::pair<uint32_t, uint64_t>;

/// Finite-height clock lattice: bottom, a constant or exact periodic waveform,
/// a tick-driven waveform, and unknown (top). TickDriven proves at most one
/// update per source tick; it does not promise a periodic output waveform.
struct ClockFact {
  enum class Kind { Bottom, Constant, Periodic, TickDriven, Unknown };
  Kind kind = Kind::Bottom;
  ClockBit source{};
  uint64_t halfPeriod = 0;
  uint8_t constant = 0; // value bit | (unknown bit << 1)

  bool operator==(const ClockFact &other) const;
  static ClockFact join(ClockFact lhs, ClockFact rhs);
  bool hasTickBound() const {
    return kind == Kind::Periodic || kind == Kind::TickDriven;
  }
};

struct PeriodicClockSeed {
  ClockBit bit;
  mlir::Operation *writer;
  uint64_t halfPeriod;
};

/// Immutable inference result. Physical writer coverage is checked before
/// monotone propagation through the signal graph. A dense forward CFG lattice
/// bounds writes between suspension points, including joins and zero-time
/// loops, instead of assuming that a clocked process executes only once.
class ClockInferenceAnalysis {
public:
  ClockInferenceAnalysis(mlir::ModuleOp module,
                         const NativeStateLayoutAnalysis &layout,
                         mlir::ArrayRef<PeriodicClockSeed> seeds);

  ClockFact lookup(ClockBit bit) const;
  const llvm::DenseMap<ClockBit, ClockFact> &getFacts() const { return facts; }
  unsigned getCopyCount() const { return copyCount; }
  unsigned getCadenceCount() const { return cadenceCount; }

private:
  llvm::DenseMap<ClockBit, ClockFact> facts;
  unsigned copyCount = 0;
  unsigned cadenceCount = 0;
};

} // namespace obelisk::analysis

#endif
