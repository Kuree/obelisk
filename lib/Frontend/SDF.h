//===- SDF.h - Static Standard Delay Format annotation --------*- C++ -*-===//
//
// Private frontend support for compiling statically named SDF files into the
// elaborated timing metadata imported into Obelisk IR.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_LIB_FRONTEND_SDF_H
#define OBELISK_LIB_FRONTEND_SDF_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"

#include <cstdint>
#include <memory>
#include <optional>

namespace slang {
class SourceManager;
namespace ast {
class CallExpression;
class Compilation;
class TimingPathSymbol;
} // namespace ast
} // namespace slang

namespace obelisk::frontend {

class SDFAnnotationDatabase {
public:
  using DelayVector = llvm::SmallVector<std::optional<int64_t>, 12>;

  const DelayVector *
  getTimingPathDelays(const slang::ast::TimingPathSymbol &path) const;
  bool isAppliedCall(const slang::ast::CallExpression &call) const;

private:
  friend std::unique_ptr<SDFAnnotationDatabase>
  buildSDFAnnotationDatabase(slang::ast::Compilation &,
                             const slang::SourceManager &);

  llvm::DenseMap<const slang::ast::TimingPathSymbol *, DelayVector>
      timingPathDelays;
  llvm::DenseSet<const slang::ast::CallExpression *> appliedCalls;
};

/// Parse and resolve every statically supported `$sdf_annotate` call in the
/// elaborated design. Returns null after any fatal annotation diagnostic.
std::unique_ptr<SDFAnnotationDatabase>
buildSDFAnnotationDatabase(slang::ast::Compilation &compilation,
                           const slang::SourceManager &sourceManager);

} // namespace obelisk::frontend

#endif // OBELISK_LIB_FRONTEND_SDF_H
