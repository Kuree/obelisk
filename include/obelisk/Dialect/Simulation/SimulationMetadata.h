//===- SimulationMetadata.h - Shared simulation metadata -------*- C++ -*-===//
//
// Transient metadata names shared by simulation lowering, analysis, and
// optimization. Keep classification here so conservative transformation
// allowlists cannot drift apart.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_DIALECT_SIMULATION_SIMULATIONMETADATA_H
#define OBELISK_DIALECT_SIMULATION_SIMULATIONMETADATA_H

#include "obelisk/Dialect/DPI/DPIDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleFieldEnums.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationDialect.h"
#include "llvm/ADT/StringRef.h"

#include <cstdint>

namespace obelisk::sim::metadata {

/// Transient function attribute containing ArgumentBindingAttr,
/// LocalBindingAttr, and ConstantBindingAttr entries.
inline constexpr llvm::StringLiteral bindings = "simulation.bindings";
/// Frozen by the bytecode encoder from complete call-closure effect analysis.
/// Recomputed whenever the bytecode image is rebuilt.
inline constexpr llvm::StringLiteral bytecodeReadOnly =
    "simulation.bytecode_read_only";
inline constexpr llvm::StringLiteral delayScale = "simulation.delay_scale";
inline constexpr llvm::StringLiteral delayQuantum = "simulation.delay_quantum";
inline constexpr llvm::StringLiteral captureKind = "simulation.capture_kind";
inline constexpr llvm::StringLiteral descriptorId = "simulation.descriptor_id";
/// An exclusive slot whose initializer can be recreated at every activation:
/// mutable lanes are defined before reads, and all other lanes keep defaults.
inline constexpr llvm::StringLiteral activationDefaultSlot =
    "simulation.activation_default_slot";
inline constexpr llvm::StringLiteral descriptorRootType =
    "simulation.descriptor_root_type";
inline constexpr llvm::StringLiteral descriptorLow =
    "simulation.descriptor_low";
inline constexpr llvm::StringLiteral descriptorIndices =
    "simulation.descriptor_indices";
inline constexpr llvm::StringLiteral descriptorAggregateType =
    "simulation.descriptor_aggregate_type";
inline constexpr llvm::StringLiteral descriptorPackedLow =
    "simulation.descriptor_packed_low";
inline constexpr llvm::StringLiteral hierarchicalName =
    "simulation.hierarchical_name";
/// Exact frontend enum identity retained solely to reconnect a value object
/// to its anonymous enum typespec in the immutable VPI relation image.
inline constexpr llvm::StringLiteral vpiSourceTypeIdentity =
    "simulation.vpi_source_type_identity";
/// FlatSymbolRefAttr naming the immutable relation-backed anchor that owns the
/// public VPI identity of executable named-event-array storage. The storage
/// remains in the execution layout but is omitted from the VPI object
/// inventory.
inline constexpr llvm::StringLiteral vpiIdentityDelegated =
    "simulation.vpi_identity_delegated";
/// Marks a storage descriptor that a subroutine owns. Its writers are the
/// subroutine's callers rather than drivers of a design variable.
inline constexpr llvm::StringLiteral subroutineStorage =
    "simulation.subroutine_storage";
/// Hierarchical path of the variable that holds a function's return value.
inline constexpr llvm::StringLiteral returnVariablePath =
    "simulation.return_variable_path";
/// Stable base-to-derived index of an effective rand/randc class property.
inline constexpr llvm::StringLiteral randomModeIndex =
    "simulation.random_mode_index";
/// Marks a non-static rand class-handle field as a recursive object edge.
inline constexpr llvm::StringLiteral randomObjectEdge =
    "simulation.random_object_edge";
/// Typed RandomVariableKindAttr on a direct packed instance rand property.
inline constexpr llvm::StringLiteral randomVariableKind =
    "simulation.random_variable_kind";
/// Source signedness of a direct packed random variable.
inline constexpr llvm::StringLiteral randomVariableSigned =
    "simulation.random_variable_signed";
/// Hidden i64 fields that carry one randc property's permutation state.
inline constexpr llvm::StringLiteral randomCycleKeyField =
    "simulation.random_cycle_key_field";
inline constexpr llvm::StringLiteral randomCyclePositionField =
    "simulation.random_cycle_position_field";
/// Root-class field containing the 64-bit disabled-property mask.
inline constexpr llvm::StringLiteral randomModeField =
    "simulation.random_mode_field";
/// Marks an executable class field that corresponds to a source-declared
/// instance property. Compiler-owned fields and static properties are absent
/// from the object bit-stream inventory.
inline constexpr llvm::StringLiteral classBitstreamMember =
    "simulation.class_bitstream_member";
/// Source member visibility retained for recursive class bit-stream legality.
inline constexpr llvm::StringLiteral classBitstreamVisibility =
    "simulation.class_bitstream_visibility";
/// Marks an explicit class-containing bit-stream conversion whose source is
/// exactly the enclosing method's current-instance `this`.  This exception is
/// semantic: aliases and handles reached through another expression never
/// inherit it.
inline constexpr llvm::StringLiteral classBitstreamAllowHiddenRoot =
    "simulation.class_bitstream_allow_hidden_root";
/// Dense nonzero identifier of a materialized class bit-stream cast site.
inline constexpr llvm::StringLiteral classBitstreamSiteID =
    "simulation.class_bitstream_site_id";
inline constexpr llvm::StringLiteral classBitstreamBytecodeFunction =
    "simulation.class_bitstream_bytecode_function";
inline constexpr llvm::StringLiteral classBitstreamBytecodeSite =
    "simulation.class_bitstream_bytecode_site";
/// Marks a process-lifetime `simulation.ref.alloc` copy of an exclusive root
/// that is flushed back to that root at every suspension and return. Its
/// contents at any resumption equal a fresh load of the root.
inline constexpr llvm::StringLiteral flushedPrivateSlot =
    "simulation.flushed_private_slot";
/// Module marker set by preparation when source classes may be bitstreamed;
/// it keeps whole-design class planning off ordinary compilation paths.
inline constexpr llvm::StringLiteral classBitstreamSource =
    "simulation.class_bitstream_source";
inline constexpr llvm::StringLiteral thisArgument = "simulation.this_argument";
inline constexpr llvm::StringLiteral lowered = "simulation.lowered";

inline bool isKnownBoundary(llvm::StringRef name) {
  if (auto field = schedule::symbolizeField(name))
    return !schedule::metadata::requiresOperationScope(*field);
  return name == captureKind || name == descriptorId ||
         name == IntegerRange::getNameStr() || name == descriptorRootType ||
         name == descriptorLow || name == descriptorIndices ||
         name == descriptorAggregateType || name == descriptorPackedLow;
}

inline bool isKnownOperation(llvm::StringRef name) {
  if (auto field = schedule::symbolizeField(name))
    return !schedule::metadata::pinsOperationBoundary(*field);
  return isKnownBoundary(name) || name == bindings || name == delayScale ||
         name == delayQuantum || name == hierarchicalName ||
         name == returnVariablePath || name == lowered ||
         name == randomModeIndex || name == randomObjectEdge ||
         name == randomVariableKind || name == randomVariableSigned ||
         name == randomCycleKeyField || name == randomCyclePositionField ||
         name == randomModeField || name == classBitstreamMember ||
         name == classBitstreamVisibility ||
         name == schedule::metadata::staticBodyFusion ||
         name == schedule::metadata::evalDiscardableStore ||
         name == schedule::metadata::staticFusion ||
         name == schedule::metadata::computeKernels ||
         name == schedule::metadata::threeTierSchedule ||
         name == schedule::metadata::staticSpecialization ||
         name == schedule::metadata::staticSuperstep ||
         name == schedule::metadata::topLevelWildcardWait ||
         name == schedule::metadata::proceduralEventWait ||
         name == schedule::metadata::repeatingAlwaysWait ||
         name == dpi::ElidedInputs::getNameStr() ||
         name == ResultRanges::getNameStr() ||
         name == ResultExprs::getNameStr() ||
         name == KnownLogicResult::getNameStr() ||
         name == InlineLegalityCache::getNameStr();
}

/// Namespaces whose attributes are compiler-owned facts. A transformation
/// that clones, inlines or erases an operation must understand each such
/// attribute it carries: `isKnownOperation` and `isKnownBoundary` list the
/// ones that survive these transformations.
inline bool isCompilerMetadata(llvm::StringRef name) {
  return name.starts_with("simulation.") || name.starts_with("schedule.") ||
         name.starts_with("dpi.");
}

} // namespace obelisk::sim::metadata

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONMETADATA_H
