//===- ComputeFusion.h - Static process-body fusion helpers -----*- C++ -*-===//

#ifndef OBELISK_CONVERSION_OBELISKTOSIMULATION_COMPUTEFUSION_H
#define OBELISK_CONVERSION_OBELISKTOSIMULATION_COMPUTEFUSION_H

#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "llvm/ADT/DenseSet.h"

namespace obelisk {

/// A finite, idempotent combinational activation. Every read is watched and
/// every written range is exclusive, disjoint and never read by this actor.
/// This is deliberately not an always_latch or arbitrary always certificate.
struct CombinationalFusionBody {
  mlir::Block *activation;
  mlir::Operation *suspend;
  mlir::SmallVector<mlir::Block *> blocks;
  mlir::SmallVector<mlir::Value> outputs;
};

/// Immutable, revision-local writer inventory for local ranked kernels.
class CombinationalFusionAnalysis {
public:
  CombinationalFusionAnalysis(
      sim::SimDesignOp design,
      const analysis::DescriptorProvenanceAnalysis &provenance);
  std::optional<CombinationalFusionBody>
  analyze(sim::SimFuncOp function,
          const analysis::DescriptorProvenanceAnalysis &provenance) const;

private:
  bool unsupported = false;
  using Writer = std::pair<mlir::StringAttr, sim::ComputeEffectAttr>;
  llvm::DenseMap<uint64_t, mlir::SmallVector<Writer>> storageWriters;
  llvm::DenseSet<mlir::StringAttr> nonNativeOwners;
};

/// Return true when a process body can be merged without combining
/// actor-local state or admitting behavior outside the static digital subset.
bool isComputeBodyFusionEligible(
    sim::SimFuncOp function,
    const analysis::DescriptorProvenanceAnalysis &provenance);

/// Primitive-only union kernels additionally admit the UDP driver-state read
/// and inertial publication operations that their materializer preserves.
/// General and eval body fusion deliberately retain the narrower contract.
bool isPrimitiveComputeBodyFusionEligible(
    sim::SimFuncOp function,
    const analysis::DescriptorProvenanceAnalysis &provenance);

/// Return continuation targets that can coexist in the Active ready set when
/// the given sensitivity awakens. A constant-delay continuation is excluded
/// only when graph activation edges prove it is the unique producer currently
/// publishing the sensitivity; independent deadlines remain barriers.
mlir::SmallVector<uint32_t>
getComputeFusionReadyTargets(sim::ComputeGraphAttr graph,
                             sim::ComputeEffectAttr sensitivity);

} // namespace obelisk

#endif // OBELISK_CONVERSION_OBELISKTOSIMULATION_COMPUTEFUSION_H
