//===- SDF.h - Transient SDF importer -------------------------*- C++ -*-===//
//
// Public frontend entry points for IEEE 1800-2017 Clause 32 data.  Imported
// operations are a normalized, transient exchange format: a production
// SystemVerilog import must consume them before Simulation IR is formed.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_FRONTEND_SDF_H
#define OBELISK_FRONTEND_SDF_H

#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/StringRef.h"

#include <cstdint>

namespace mlir {
class MLIRContext;
}

namespace obelisk::frontend {

/// Deterministic bounds on IEEE 1800-2017 Clause 32 annotation work. Static
/// compilation admits a bounded cache of annotation files rather than
/// unbounded attacker-chosen file, byte, entry, or application state.
struct SDFResourceLimits {
  /// Bytes of one SDF file.
  uint64_t maxFileBytes = 64 * 1024 * 1024;
  /// Bytes of all distinct annotation files, including ones that fail.
  uint64_t maxCacheBytes = 256 * 1024 * 1024;
  /// Distinct annotation files.
  uint64_t maxCacheFiles = 256;
  /// Parsed model entries of all distinct annotation files.
  uint64_t maxCacheEntries = 1 << 20;
  /// Delay updates applied to matched timing paths.
  uint64_t maxMatchedUpdates = 1 << 20;
  /// Call-order proofs, path match attempts, and wildcard instance probes.
  uint64_t maxApplicationWork = 1 << 22;
};

/// Parse one SDF DELAYFILE into a verified module of typed obelisk_sdf ops.
/// Decimal tokens are retained exactly; this API performs no timescale
/// conversion or destination-precision rounding.
mlir::FailureOr<mlir::OwningOpRef<mlir::ModuleOp>>
importSDF(llvm::StringRef sourceName, llvm::StringRef contents,
          mlir::MLIRContext &context, bool verifyIR = true,
          uint64_t maxInputBytes = SDFResourceLimits().maxFileBytes);

} // namespace obelisk::frontend

#endif // OBELISK_FRONTEND_SDF_H
