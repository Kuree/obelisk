//===- SimulationVPIReferences.cpp - Contextual VPI identities ------------===//

#include "talos/Dialect/Simulation/SimulationOps.h"
#include "talos/Dialect/Simulation/SimulationVPI.h"

using namespace mlir;

namespace talos::sim {
LogicalResult verifyVPIDefinitionMemberRef(Operation *use, SimDesignOp design,
                                           VPIObjectRefAttr reference,
                                           SymbolTableCollection &symbols) {
  if (reference.getKind() != VPIObjectRefKind::DefinitionMember)
    return use->emitOpError("requires a contextual VPI definition member");
  auto member = symbols.lookupSymbolIn<SimVPIDefinitionMemberDeclOp>(
      design, reference.getSymbol());
  if (!member)
    return use->emitOpError("references an unknown VPI definition member");
  uint64_t scopeID = reference.getId().getValue().getZExtValue();
  for (auto scope : design.getOps<SimScopeDeclOp>()) {
    if (scope.getId() != scopeID)
      continue;
    if (scope.getVpiDefinitionAttr() != member.getDefinitionAttr())
      return use->emitOpError(
          "VPI member and instance scope have different definitions");
    return success();
  }
  return use->emitOpError("references an unknown VPI member instance scope");
}

} // namespace talos::sim
