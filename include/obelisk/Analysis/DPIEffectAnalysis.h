//===- DPIEffectAnalysis.h - Design state reachable from DPI calls -*- C++ -*-===//

#ifndef OBELISK_ANALYSIS_DPIEFFECTANALYSIS_H
#define OBELISK_ANALYSIS_DPIEFFECTANALYSIS_H

namespace obelisk::sim {
class SimDesignOp;
class SimDPICallOp;
} // namespace obelisk::sim

namespace obelisk::analysis {

/// Which DPI import calls can reach design state beyond their own operands
/// and results.
///
/// IEEE 1800-2023 35.5.1.3: an import that is not declared context shall not
/// read or write any SystemVerilog data object other than its actual
/// arguments, which this IR passes as values. A context import may call
/// exported subroutines (35.5.3); svSetScope can select any instantiated
/// scope, so a call may reach every export of the design. The context
/// property does not enable VPI (35.5.3), and with VPI off the foreign code
/// has no other route into the design. A context call is therefore
/// transparent when VPI is off and every export closure touches only its own
/// locals and arguments, never suspends (35.5.1.5), and neither controls
/// processes nor uses scheduler, heap, random, or coverage state.
///
/// Export transparency is judged from the compute graph's effect summaries.
/// Building the graph records it on the design (`record`), and later
/// instances use that record, so every pass sees the same answer after
/// rewrites drop summaries.
class DPIEffectAnalysis {
public:
  explicit DPIEffectAnalysis(sim::SimDesignOp design);

  /// Record export transparency on `design` from its current summaries.
  static void record(sim::SimDesignOp design);

  /// Whether `call` may access or control design state through foreign code.
  bool mayAccessDesign(sim::SimDPICallOp call) const;
  /// Whether `call` may do so through an export, ignoring VPI.
  bool mayReachDesignThroughExports(sim::SimDPICallOp call) const;
  /// Whether some DPI call of the design may access design state.
  bool anyCallMayAccessDesign() const { return opaqueCalls; }
  /// Whether every export closure is transparent in the sense above.
  bool exportsAreTransparent() const { return transparentExports; }

private:
  bool transparentExports = false;
  bool transparentContext = false;
  bool opaqueCalls = false;
};

} // namespace obelisk::analysis

#endif
