//===- SimulationCopyProcessAnalysis.cpp - Copy admission ----------------===//

#include "obelisk/Analysis/SimulationCopyProcessAnalysis.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

using namespace mlir;

namespace obelisk::analysis {

std::optional<CaptureCopyProcess>
getCaptureCopyProcess(sim::SimFuncOp function) {
  if (function.isExternal() ||
      function.getHomeRegion() != sim::EventRegion::Active ||
      (function.getEntryKind() != sim::EntryKind::PortInput &&
       function.getEntryKind() != sim::EntryKind::PortOutput &&
       function.getEntryKind() != sim::EntryKind::Continuous) ||
      function.getBody().getBlocks().size() != 2)
    return std::nullopt;
  Block &entry = function.getBody().front();
  Block &body = function.getBody().back();
  auto branch = dyn_cast<cf::BranchOp>(entry.getTerminator());
  if (!llvm::hasSingleElement(entry) || !branch || branch.getDest() != &body ||
      body.getNumArguments() != 0)
    return std::nullopt;
  auto wait = dyn_cast<sim::SimSuspendChangeOp>(body.getTerminator());
  if (!wait || wait.getContinuation() != &body ||
      !wait.getContinuationOperands().empty())
    return std::nullopt;

  Value source, destination;
  if (body.getOperations().size() == 3) {
    auto load = dyn_cast<sim::SimRefLoadOp>(body.front());
    auto store = dyn_cast<sim::SimRefStoreOp>(*std::next(body.begin()));
    if (!load || !store || store.getValue() != load.getResult())
      return std::nullopt;
    source = load.getReference();
    destination = store.getReference();
  } else if (body.getOperations().size() == 2) {
    auto copy = dyn_cast<sim::SimRefCopyOp>(body.front());
    if (!copy)
      return std::nullopt;
    source = copy.getSource();
    destination = copy.getDestination();
  } else {
    return std::nullopt;
  }
  if (source != wait.getWatched() || source.getType() != destination.getType())
    return std::nullopt;
  // IEEE 1800-2023 7.2.1/7.4.1: whole packed structures/arrays are single
  // vectors. Equal reference types preserve their exact width, state domain
  // and bit ordering; no unpacked layout or assignment conversion is inferred.
  auto reference = dyn_cast<sim::RefType>(source.getType());
  if (!reference || !sim::getPackedWidth(reference.getElementType()))
    return std::nullopt;
  auto storageCapture = [&](Value value) {
    auto argument = dyn_cast<BlockArgument>(value);
    if (!argument || argument.getOwner() != &entry)
      return false;
    auto kind = function.getArgAttrOfType<IntegerAttr>(
        argument.getArgNumber(), sim::metadata::captureKind);
    return kind &&
           kind.getInt() == static_cast<int32_t>(sim::CaptureKind::Storage);
  };
  // IEEE 1800-2023 4.4.2.2, 4.9.1/4.9.6 and 9.4.2: retain Active execution,
  // the first evaluation and every source
  // change activation, including the original store's publication behavior.
  // This is an execution-shape proof, never permission to merge storage.
  if (!storageCapture(source) || !storageCapture(destination))
    return std::nullopt;
  return CaptureCopyProcess{cast<BlockArgument>(source).getArgNumber(),
                            cast<BlockArgument>(destination).getArgNumber()};
}

bool isCaptureCopyProcess(sim::SimFuncOp function) {
  return getCaptureCopyProcess(function).has_value();
}

} // namespace obelisk::analysis
